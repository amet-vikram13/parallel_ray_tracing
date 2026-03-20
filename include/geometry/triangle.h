#pragma once

#include "core/common.h"
#include "geometry/ray.h"
#include "geometry/hit_record.h"

struct Triangle {
    Vec3 v0, v1, v2;
    Vec3 n0, n1, n2; // per-vertex normals (for smooth shading)
    int material_id;

    HD Triangle() : material_id(0) {}
    HD Triangle(const Vec3& a, const Vec3& b, const Vec3& c, int mat_id)
        : v0(a), v1(b), v2(c), material_id(mat_id) {
        Vec3 face_normal = glm::normalize(glm::cross(b - a, c - a));
        n0 = n1 = n2 = face_normal;
    }

    // Möller–Trumbore intersection
    HD bool intersect(const Ray& ray, float t_min, float t_max, HitRecord& rec) const {
        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        Vec3 h = glm::cross(ray.direction, edge2);
        float a = glm::dot(edge1, h);

        if (fabsf(a) < EPSILON) return false;

        float f = 1.0f / a;
        Vec3 s = ray.origin - v0;
        float u = f * glm::dot(s, h);
        if (u < 0.0f || u > 1.0f) return false;

        Vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(ray.direction, q);
        if (v < 0.0f || u + v > 1.0f) return false;

        float t = f * glm::dot(edge2, q);
        if (t < t_min || t > t_max) return false;

        rec.t = t;
        rec.point = ray.at(t);
        // Interpolate vertex normals using barycentric coords
        float w = 1.0f - u - v;
        Vec3 interp_normal = glm::normalize(w * n0 + u * n1 + v * n2);
        rec.set_face_normal(ray.direction, interp_normal);
        rec.material_id = material_id;
        return true;
    }
};
