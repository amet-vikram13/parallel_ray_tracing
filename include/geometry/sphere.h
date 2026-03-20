#pragma once

#include "core/common.h"
#include "geometry/ray.h"
#include "geometry/hit_record.h"

struct Sphere {
    Vec3 center;
    float radius;
    int material_id;

    HD Sphere() : center(0.0f), radius(1.0f), material_id(0) {}
    HD Sphere(const Vec3& c, float r, int mat_id)
        : center(c), radius(r), material_id(mat_id) {}

    // Ray-sphere intersection using quadratic formula
    HD bool intersect(const Ray& ray, float t_min, float t_max, HitRecord& rec) const {
        Vec3 oc = ray.origin - center;
        float a = glm::dot(ray.direction, ray.direction);
        float half_b = glm::dot(oc, ray.direction);
        float c = glm::dot(oc, oc) - radius * radius;
        float discriminant = half_b * half_b - a * c;

        if (discriminant < 0.0f) return false;

        float sqrtd = sqrtf(discriminant);
        float root = (-half_b - sqrtd) / a;
        if (root < t_min || root > t_max) {
            root = (-half_b + sqrtd) / a;
            if (root < t_min || root > t_max) return false;
        }

        rec.t = root;
        rec.point = ray.at(root);
        Vec3 outward_normal = (rec.point - center) / radius;
        rec.set_face_normal(ray.direction, outward_normal);
        rec.material_id = material_id;
        return true;
    }
};
