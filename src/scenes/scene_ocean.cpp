// -----------------------------------------------------------------------------
// Scene label:    ocean
// Visual features: Procedural, animated open ocean at golden hour. Per-pixel
//                  raymarched sum-of-sines height field with analytic normals,
//                  Schlick Fresnel water/sky blend, Rayleigh + Mie sky, sun
//                  disc with specular highlight, and ACES filmic tone mapping.
//                  Animates over time (sun arc + camera yaw).
// Expected time:   ~2–4 s per frame on CPU @ 512×512, 1 spp (fragment shader);
//                  ~80–150 ms per frame on GPU @ 512×512, 1 spp.
//                  Higher spp linearly multiplies that cost (used for AA only).
// Limitations:     This scene bypasses BVH/geometry intersection entirely --
//                  it is a fragment-shader-style scene. The `--no-accel` flag
//                  has no effect on the ocean surface itself. If the scene is
//                  extended with BVH-visible decorative geometry (e.g. a
//                  ground plane or floating objects) the flag will apply to
//                  that supplementary geometry only.
// -----------------------------------------------------------------------------

#include "scenes/ocean_shader.h"
#include "scenes/scene_registry.h"

#include <cmath>

// Advances per-frame ocean state. Rotates the look-at vector slowly around
// the camera to pan across the horizon (the cinematic "drifting ship deck"
// feel called out in the spec). The sun direction itself is a pure function
// of time inside the shader, so nothing else needs updating here.
static void animate_ocean(Scene& scene, float t) {
    const float yaw = 0.12f * t;     // slow pan
    const float dip = -0.06f;        // aim slightly below the horizon
    Vec3 look_dir = glm::normalize(Vec3(sinf(yaw), dip, -cosf(yaw)));
    scene.cam_lookat = scene.cam_lookfrom + look_dir * 10.0f;
}

void build_scene_ocean(Scene& scene, const RenderConfig& /*cfg*/) {
    scene.mode        = SceneMode::OCEAN;
    scene.shade_pixel = &shade_ocean_pixel;
    scene.animate     = &animate_ocean;

    scene.cam_lookfrom = Vec3(0.0f, 1.5f, 0.0f);
    scene.cam_lookat   = Vec3(0.0f, 1.44f, -10.0f);
    scene.cam_vup      = Vec3(0.0f, 1.0f, 0.0f);
    scene.cam_vfov_deg = 55.0f;

    // Ocean has no discrete geometry, no materials, no point lights.
    // The shade_pixel callback synthesizes the image end-to-end from the
    // primary ray + time.
}
