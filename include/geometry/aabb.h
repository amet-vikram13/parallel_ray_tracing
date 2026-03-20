#pragma once

#include "core/common.h"
#include "geometry/ray.h"
#include <algorithm>

struct AABB {
    Vec3 min_bound;
    Vec3 max_bound;

    HD AABB() : min_bound(INF), max_bound(-INF) {}
    HD AABB(const Vec3& mn, const Vec3& mx) : min_bound(mn), max_bound(mx) {}

    // Ray-AABB intersection using the slab method
    HD bool intersect(const Ray& ray, float t_min, float t_max) const {
        for (int a = 0; a < 3; ++a) {
            float inv_d = 1.0f / ray.direction[a];
            float t0 = (min_bound[a] - ray.origin[a]) * inv_d;
            float t1 = (max_bound[a] - ray.origin[a]) * inv_d;
            if (inv_d < 0.0f) {
                float tmp = t0; t0 = t1; t1 = tmp;
            }
            t_min = t0 > t_min ? t0 : t_min;
            t_max = t1 < t_max ? t1 : t_max;
            if (t_max <= t_min) return false;
        }
        return true;
    }

    HD Vec3 centroid() const {
        return 0.5f * (min_bound + max_bound);
    }

    HD float surface_area() const {
        Vec3 d = max_bound - min_bound;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }

    HD static AABB merge(const AABB& a, const AABB& b) {
        return AABB(
            Vec3(fminf(a.min_bound.x, b.min_bound.x),
                 fminf(a.min_bound.y, b.min_bound.y),
                 fminf(a.min_bound.z, b.min_bound.z)),
            Vec3(fmaxf(a.max_bound.x, b.max_bound.x),
                 fmaxf(a.max_bound.y, b.max_bound.y),
                 fmaxf(a.max_bound.z, b.max_bound.z))
        );
    }

    HD int longest_axis() const {
        Vec3 d = max_bound - min_bound;
        if (d.x > d.y && d.x > d.z) return 0;
        if (d.y > d.z) return 1;
        return 2;
    }
};
