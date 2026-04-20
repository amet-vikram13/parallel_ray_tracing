#pragma once

// -----------------------------------------------------------------------------
// Ocean fragment shader (host + device).
//
// Evaluates a procedural, animated ocean per primary ray. All functions are
// __host__ __device__ (HD) so they can be called from both the CPU renderer
// (as a plain inline function pointer) and from a CUDA kernel.
//
// Pipeline per pixel:
//   1. Raymarch the camera ray against a 2D sum-of-sines height field to
//      find the water surface intersection.
//   2. Evaluate an analytic surface normal from the wave gradient.
//   3. Blend subsurface (deep teal) with sky reflection via Schlick Fresnel.
//   4. Add a tight Phong-style specular lobe around the sun direction.
//   5. Sample a sky that combines a Rayleigh zenith tint, warm Mie-like
//      horizon tint, and a bright solar disc + halo. The sun elevation and
//      azimuth advance with time, producing a dawn → noon → dusk cycle.
//   6. ACES filmic tone-mapping (Narkowicz fit) before returning LDR color.
// -----------------------------------------------------------------------------

#include "core/common.h"
#include "geometry/ray.h"

#include <cmath>

// Number of sinusoidal wave octaves summed into the height field. Matches
// the 6+-octave requirement from the spec.
#ifndef OCEAN_NUM_WAVES
#define OCEAN_NUM_WAVES 6
#endif

// Single directional wave descriptor. Kept trivial so it can be placed in
// constant memory on the device without special annotations.
struct OceanWave {
    float amp;       // amplitude (meters)
    float k;         // angular wavenumber (2π / wavelength)
    float omega;     // angular frequency (phase speed * k)
    float dir_x;     // 2D horizontal direction (xz plane), unit length
    float dir_z;
    float phase;     // static phase offset
};

// Constant wave bank. Frequencies roughly doubling for an octave-like
// cascade, directions spread around the +x axis to give anisotropic swell.
HD inline OceanWave ocean_wave_get(int i) {
    // Each entry: {amp, k, omega, dir_x, dir_z, phase}
    // Using fixed tables avoids static constexpr arrays in HD code, which
    // some older CUDA toolchains handle poorly.
    switch (i) {
        case 0: return {0.35f, 0.52f, 0.70f,  0.92f,  0.39f, 0.00f};
        case 1: return {0.22f, 0.83f, 1.05f,  0.52f,  0.86f, 1.20f};
        case 2: return {0.14f, 1.57f, 1.55f,  0.97f, -0.25f, 2.40f};
        case 3: return {0.09f, 2.51f, 2.05f, -0.29f,  0.96f, 3.60f};
        case 4: return {0.055f,4.20f, 2.75f,  0.93f,  0.37f, 4.80f};
        case 5: return {0.035f,6.90f, 3.60f,  0.14f,  0.99f, 6.00f};
        default: return {0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f};
    }
}

// Evaluate the summed height and analytic surface normal at world-space
// (x, z, t). Normal is derived from the gradient of the height field, not
// from finite differences.
HD inline void ocean_eval_surface(float x, float z, float t,
                                  float& height_out, Vec3& normal_out) {
    float h  = 0.0f;
    float dx = 0.0f;
    float dz = 0.0f;
    for (int i = 0; i < OCEAN_NUM_WAVES; ++i) {
        OceanWave w = ocean_wave_get(i);
        float phase = w.k * (w.dir_x * x + w.dir_z * z) - w.omega * t + w.phase;
        float s = sinf(phase);
        float c = cosf(phase);
        h  += w.amp * s;
        float d = w.amp * w.k * c;
        dx += d * w.dir_x;
        dz += d * w.dir_z;
    }
    height_out = h;
    normal_out = glm::normalize(Vec3(-dx, 1.0f, -dz));
}

// Raymarch the primary ray against the height field. Uses a monotonically
// increasing step size so distant waves are still sampled cheaply, then
// linearly interpolates the sign change to a sub-step hit.
HD inline bool ocean_raymarch(const Ray& ray, float t,
                              float& t_hit_out, Vec3& hit_pos_out, Vec3& normal_out) {
    const int   MAX_STEPS = 96;
    const float T_MIN     = 0.05f;
    const float T_MAX     = 220.0f;

    float t_prev = T_MIN;
    Vec3  p_prev = ray.at(t_prev);
    float h_prev;
    Vec3  n_ignored;
    ocean_eval_surface(p_prev.x, p_prev.z, t, h_prev, n_ignored);
    float dy_prev = p_prev.y - h_prev;

    float step = 0.12f;
    float t_cur = t_prev;

    for (int i = 0; i < MAX_STEPS; ++i) {
        t_cur = t_prev + step;
        if (t_cur > T_MAX) return false;

        Vec3  p_cur = ray.at(t_cur);
        float h_cur;
        ocean_eval_surface(p_cur.x, p_cur.z, t, h_cur, n_ignored);
        float dy_cur = p_cur.y - h_cur;

        if (dy_prev > 0.0f && dy_cur <= 0.0f) {
            float denom = (dy_prev - dy_cur);
            float alpha = denom > 1e-6f ? (dy_prev / denom) : 0.5f;
            float t_cross = t_prev + alpha * (t_cur - t_prev);
            Vec3  p_cross = ray.at(t_cross);
            float h_cross;
            ocean_eval_surface(p_cross.x, p_cross.z, t, h_cross, normal_out);
            p_cross.y = h_cross;
            t_hit_out   = t_cross;
            hit_pos_out = p_cross;
            return true;
        }

        t_prev  = t_cur;
        dy_prev = dy_cur;
        step   *= 1.035f;   // grow steps with distance
    }
    return false;
}

// Sun direction as a function of time. Elevation sweeps from near-horizon
// (dawn) up past zenith and back down (dusk) as t grows, and azimuth drifts
// slowly so the solar disc moves across the sky rather than sitting still.
HD inline Vec3 ocean_sun_direction(float t) {
    float elev = 0.35f + 0.40f * sinf(0.08f * t - 1.2f);   // radians above horizon
    float azim = 0.20f + 0.015f * t;                        // radians from +z toward +x
    float ce = cosf(elev);
    return glm::normalize(Vec3(ce * sinf(azim), sinf(elev), -ce * cosf(azim)));
}

// Approximate sky color combining a Rayleigh-like zenith gradient, a warm
// Mie-like horizon lift, a broad sun halo, and a tight solar disc.
HD inline Vec3 ocean_sky_color(const Vec3& dir, const Vec3& sun_dir) {
    float up = fmaxf(0.0f, dir.y);

    Vec3 zenith (0.18f, 0.38f, 0.78f);      // Rayleigh blue
    Vec3 horizon(1.15f, 0.78f, 0.42f);      // Mie warm tint

    // Bias toward horizon at low elevations (pow steepens the transition).
    float m = powf(1.0f - up, 4.0f);
    Vec3 base = horizon * m + zenith * (1.0f - m);

    // Mie-ish forward scattering lobe around the sun.
    float mu  = fmaxf(0.0f, glm::dot(dir, sun_dir));
    float mie = powf(mu, 8.0f);
    base += Vec3(1.0f, 0.65f, 0.30f) * mie * 0.55f;

    // Tight sun glow + bright disc.
    float glow = powf(mu, 384.0f);
    base += Vec3(2.5f, 1.8f, 1.0f) * glow;
    float disc_edge0 = 0.99965f;
    float disc_edge1 = 0.99995f;
    float ds = glm::clamp((mu - disc_edge0) / (disc_edge1 - disc_edge0), 0.0f, 1.0f);
    float disc = ds * ds * (3.0f - 2.0f * ds);
    base += Vec3(14.0f, 11.0f, 7.5f) * disc;

    // Atmospheric softening below the horizon (reflections pointing down).
    if (dir.y < 0.0f) {
        float fade = glm::clamp(1.0f + dir.y * 3.5f, 0.0f, 1.0f);
        base = glm::mix(horizon * 0.55f, base, fade);
    }
    return base;
}

// Narkowicz 2015 ACES approximation. Operates per-channel on HDR input.
HD inline Vec3 ocean_aces_tonemap(const Vec3& x) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    Vec3 num = x * (a * x + Vec3(b));
    Vec3 den = x * (c * x + Vec3(d)) + Vec3(e);
    return glm::clamp(num / den, 0.0f, 1.0f);
}

// Shade a single primary ray against the animated ocean. Returns tone-mapped
// LDR color in [0, 1]. The `t` parameter is the per-frame time.
HD inline Vec3 shade_ocean_pixel(const Ray& ray, float t) {
    Vec3 dir = glm::normalize(ray.direction);
    Vec3 sun = ocean_sun_direction(t);

    float t_hit;
    Vec3  hit_pos;
    Vec3  normal;
    if (ocean_raymarch(ray, t, t_hit, hit_pos, normal)) {
        Vec3 view = -dir;
        float cos_theta = fmaxf(0.0f, glm::dot(normal, view));

        // Schlick Fresnel for water (n=1.333, R0 ≈ 0.02).
        const float R0 = 0.02f;
        float fresnel = R0 + (1.0f - R0) * powf(1.0f - cos_theta, 5.0f);

        // Mirror reflection of the sky (perturbed slightly by normal).
        Vec3 reflected = glm::reflect(dir, normal);
        Vec3 sky_refl  = ocean_sky_color(reflected, sun);

        // Subsurface / deep-water color. Simulates absorption by mixing a
        // deep navy base with a lighter teal shoulder along wave crests.
        float crest = glm::clamp(normal.y, 0.0f, 1.0f);
        Vec3 deep   (0.015f, 0.09f, 0.16f);
        Vec3 shallow(0.05f,  0.30f, 0.38f);
        Vec3 subsurf = glm::mix(deep, shallow, crest);

        // Soft sun-side tint transmitted through the wave crest.
        float sun_dot = fmaxf(0.0f, glm::dot(normal, sun));
        subsurf += Vec3(0.18f, 0.12f, 0.06f) * sun_dot;

        // Phong-style specular lobe for the sun pin on the water.
        Vec3 half = glm::normalize(sun + view);
        float spec = powf(fmaxf(0.0f, glm::dot(normal, half)), 180.0f);
        Vec3 sun_col = Vec3(6.0f, 4.5f, 2.5f);

        Vec3 color = glm::mix(subsurf, sky_refl, fresnel);
        color += sun_col * spec * fresnel;

        // Atmospheric fog toward the horizon.
        float fog = 1.0f - expf(-t_hit * 0.010f);
        Vec3 horizon_col = ocean_sky_color(Vec3(dir.x, 0.02f, dir.z), sun);
        color = glm::mix(color, horizon_col, fog);

        return ocean_aces_tonemap(color);
    }

    // No water hit → pure sky.
    return ocean_aces_tonemap(ocean_sky_color(dir, sun));
}
