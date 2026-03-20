#pragma once

#include "core/common.h"
#include "geometry/ray.h"

struct Camera {
    Vec3 position;
    Vec3 lower_left;
    Vec3 horizontal;
    Vec3 vertical;
    Vec3 u, v, w; // orthonormal basis

    int width;
    int height;

    Camera() : width(800), height(600) {}

    // Setup with lookAt parameters
    void init(const Vec3& lookfrom, const Vec3& lookat, const Vec3& vup,
              float vfov_deg, int w, int h);

    // Generate ray for pixel (i, j) with optional sub-pixel offset
    HD Ray generate_ray(int px, int py, float offset_x = 0.5f, float offset_y = 0.5f) const {
        float s = (float(px) + offset_x) / float(width);
        float t = (float(py) + offset_y) / float(height);
        Vec3 dir = lower_left + s * horizontal + t * vertical - position;
        return Ray(position, dir);
    }
};
