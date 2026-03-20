#pragma once

#include "core/common.h"
#include "geometry/ray.h"

// Ray state for active ray tracking
struct RayState {
    Ray ray;
    Vec3 throughput;
    Vec3 radiance;
    int pixel_idx;
    bool active;
};

// Compact active rays using Thrust scan + scatter
// Returns count of active rays remaining
int compact_rays(RayState* d_rays, RayState* d_compacted, int count);
