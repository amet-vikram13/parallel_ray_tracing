#pragma once

#include "core/common.h"

struct HitRecord {
    Vec3 point;
    Vec3 normal;
    float t;
    int material_id;
    bool front_face;

    HD HitRecord() : t(INF), material_id(-1), front_face(true) {}

    // Ensure normal always faces against the ray
    HD void set_face_normal(const Vec3& ray_dir, const Vec3& outward_normal) {
        front_face = glm::dot(ray_dir, outward_normal) < 0.0f;
        normal = front_face ? outward_normal : -outward_normal;
    }
};
