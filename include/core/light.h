#pragma once

#include "core/common.h"

struct PointLight {
    Vec3 position;
    Vec3 color;
    float intensity;

    HD PointLight() : position(0.0f, 5.0f, 0.0f), color(1.0f), intensity(1.0f) {}
    HD PointLight(const Vec3& pos, const Vec3& col, float intens)
        : position(pos), color(col), intensity(intens) {}
};
