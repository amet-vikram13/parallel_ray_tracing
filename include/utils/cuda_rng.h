#pragma once

#include "core/common.h"
#include <curand_kernel.h>

// Per-thread RNG state
struct RNGState {
    curandState state;
};



// Random float in [0, 1)
DEVICE inline float random_float(curandState* state) {
    return curand_uniform(state);
}

// Random float in [min, max)
DEVICE inline float random_float(curandState* state, float min, float max) {
    return min + (max - min) * curand_uniform(state);
}

// Random unit vector on hemisphere (cosine-weighted sampling)
DEVICE inline Vec3 random_cosine_hemisphere(curandState* state, const Vec3& normal) {
    float r1 = curand_uniform(state);
    float r2 = curand_uniform(state);
    float phi = 2.0f * PI * r1;
    float cos_theta = sqrtf(1.0f - r2);
    float sin_theta = sqrtf(r2);

    // Build ONB from normal
    Vec3 w = normal;
    Vec3 a = (fabsf(w.x) > 0.9f) ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 u = glm::normalize(glm::cross(a, w));
    Vec3 v = glm::cross(w, u);

    return glm::normalize(u * cosf(phi) * sin_theta + v * sinf(phi) * sin_theta + w * cos_theta);
}

// Random point in unit sphere (rejection sampling)
DEVICE inline Vec3 random_in_unit_sphere(curandState* state) {
    Vec3 p;
    do {
        p = Vec3(random_float(state, -1, 1), random_float(state, -1, 1), random_float(state, -1, 1));
    } while (glm::dot(p, p) >= 1.0f);
    return p;
}
