#pragma once

#include "core/common.h"

struct Ray {
    Vec3 origin;
    Vec3 direction;

    HD Ray() : origin(0.0f), direction(0.0f, 0.0f, -1.0f) {}
    HD Ray(const Vec3& o, const Vec3& d) : origin(o), direction(glm::normalize(d)) {}

    HD Vec3 at(float t) const { return origin + t * direction; }
};
