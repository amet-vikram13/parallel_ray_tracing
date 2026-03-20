#include "geometry/sah_bvh.h"
#include "utils/timer.h"
#include <algorithm>
#include <numeric>

AABB SAHBVHBuilder::sphere_aabb(const Sphere& s) {
    return AABB(s.center - Vec3(s.radius), s.center + Vec3(s.radius));
}

AABB SAHBVHBuilder::triangle_aabb(const Triangle& t) {
    Vec3 mn = glm::min(glm::min(t.v0, t.v1), t.v2);
    Vec3 mx = glm::max(glm::max(t.v0, t.v1), t.v2);
    // Slight expansion to avoid zero-thickness
    return AABB(mn - Vec3(EPSILON), mx + Vec3(EPSILON));
}

void SAHBVHBuilder::build(const std::vector<Sphere>& spheres,
                          const std::vector<Triangle>& triangles) {
    Timer timer;
    timer.start();

    // Collect all primitive references
    int total_prims = static_cast<int>(spheres.size() + triangles.size());
    prim_refs.resize(total_prims);
    std::vector<BuildPrim> build_prims(total_prims);

    int idx = 0;
    for (int i = 0; i < static_cast<int>(spheres.size()); ++i, ++idx) {
        prim_refs[idx] = PrimitiveRef(PRIM_SPHERE, i);
        build_prims[idx].bounds = sphere_aabb(spheres[i]);
        build_prims[idx].centroid = build_prims[idx].bounds.centroid();
        build_prims[idx].ref_index = idx;
    }
    for (int i = 0; i < static_cast<int>(triangles.size()); ++i, ++idx) {
        prim_refs[idx] = PrimitiveRef(PRIM_TRIANGLE, i);
        build_prims[idx].bounds = triangle_aabb(triangles[i]);
        build_prims[idx].centroid = build_prims[idx].bounds.centroid();
        build_prims[idx].ref_index = idx;
    }

    nodes.clear();
    nodes.reserve(2 * total_prims);

    root_idx = build_recursive(build_prims, 0, total_prims);

    timer.stop();
    build_time_ms = timer.elapsed_ms();
}

int SAHBVHBuilder::build_recursive(std::vector<BuildPrim>& prims, int start, int end) {
    int node_idx = static_cast<int>(nodes.size());
    nodes.emplace_back();

    int count = end - start;

    // Compute bounds for all primitives in range
    AABB node_bounds;
    for (int i = start; i < end; ++i) {
        node_bounds = AABB::merge(node_bounds, prims[i].bounds);
    }

    if (count == 1) {
        // Leaf node
        nodes[node_idx].bounds = node_bounds;
        nodes[node_idx].prim_idx = prims[start].ref_index;
        nodes[node_idx].prim_count = 1;
        return node_idx;
    }

    if (count == 2) {
        // Create two leaf children
        nodes[node_idx].bounds = node_bounds;
        nodes[node_idx].left = build_recursive(prims, start, start + 1);
        nodes[node_idx].right = build_recursive(prims, start + 1, end);
        return node_idx;
    }

    // Compute centroid bounds for SAH binning
    AABB centroid_bounds;
    for (int i = start; i < end; ++i) {
        centroid_bounds = AABB::merge(centroid_bounds,
                                      AABB(prims[i].centroid, prims[i].centroid));
    }

    int best_axis = centroid_bounds.longest_axis();
    Vec3 extent = centroid_bounds.max_bound - centroid_bounds.min_bound;

    // Degenerate case: all centroids at same position — force midpoint split
    if (extent[best_axis] < EPSILON) {
        int mid = (start + end) / 2;
        nodes[node_idx].bounds = node_bounds;
        nodes[node_idx].left = build_recursive(prims, start, mid);
        nodes[node_idx].right = build_recursive(prims, mid, end);
        return node_idx;
    }

    // SAH binning
    struct Bin {
        AABB bounds;
        int count = 0;
    };

    float best_cost = INF;
    int best_split = -1;
    int best_split_axis = best_axis;

    // Try all 3 axes
    for (int axis = 0; axis < 3; ++axis) {
        if (extent[axis] < EPSILON) continue;

        Bin bins[NUM_BINS];

        float scale = static_cast<float>(NUM_BINS) /
                      (centroid_bounds.max_bound[axis] - centroid_bounds.min_bound[axis]);

        // Assign primitives to bins
        for (int i = start; i < end; ++i) {
            int b = static_cast<int>(
                (prims[i].centroid[axis] - centroid_bounds.min_bound[axis]) * scale);
            b = std::min(b, NUM_BINS - 1);
            bins[b].count++;
            bins[b].bounds = AABB::merge(bins[b].bounds, prims[i].bounds);
        }

        // Sweep from left to compute prefix areas and counts
        float left_area[NUM_BINS - 1];
        int left_count[NUM_BINS - 1];
        AABB left_box;
        int left_sum = 0;
        for (int i = 0; i < NUM_BINS - 1; ++i) {
            left_box = AABB::merge(left_box, bins[i].bounds);
            left_sum += bins[i].count;
            left_area[i] = left_box.surface_area();
            left_count[i] = left_sum;
        }

        // Sweep from right and compute SAH cost
        AABB right_box;
        int right_sum = 0;
        for (int i = NUM_BINS - 1; i >= 1; --i) {
            right_box = AABB::merge(right_box, bins[i].bounds);
            right_sum += bins[i].count;
            float cost = TRAVERSAL_COST +
                         INTERSECT_COST * (left_count[i - 1] * left_area[i - 1] +
                                           right_sum * right_box.surface_area()) /
                         node_bounds.surface_area();

            if (cost < best_cost && left_count[i - 1] > 0 && right_sum > 0) {
                best_cost = cost;
                best_split = i;
                best_split_axis = axis;
            }
        }
    }

    // If no valid split found, force midpoint split
    float leaf_cost = INTERSECT_COST * static_cast<float>(count);
    if (best_split == -1 || best_cost >= leaf_cost) {
        int mid = (start + end) / 2;
        nodes[node_idx].bounds = node_bounds;
        nodes[node_idx].left = build_recursive(prims, start, mid);
        nodes[node_idx].right = build_recursive(prims, mid, end);
        return node_idx;
    }

    // Partition primitives at the best split
    float split_scale = static_cast<float>(NUM_BINS) /
                        (centroid_bounds.max_bound[best_split_axis] -
                         centroid_bounds.min_bound[best_split_axis]);

    auto mid = std::partition(prims.begin() + start, prims.begin() + end,
        [&](const BuildPrim& p) {
            int b = static_cast<int>(
                (p.centroid[best_split_axis] -
                 centroid_bounds.min_bound[best_split_axis]) * split_scale);
            b = std::min(b, NUM_BINS - 1);
            return b < best_split;
        });

    int mid_idx = static_cast<int>(mid - prims.begin());

    // Safety: if partition didn't split, force a midpoint split
    if (mid_idx == start || mid_idx == end) {
        mid_idx = (start + end) / 2;
        std::nth_element(prims.begin() + start, prims.begin() + mid_idx,
                         prims.begin() + end,
                         [best_split_axis](const BuildPrim& a, const BuildPrim& b) {
                             return a.centroid[best_split_axis] < b.centroid[best_split_axis];
                         });
    }

    nodes[node_idx].bounds = node_bounds;
    nodes[node_idx].left = build_recursive(prims, start, mid_idx);
    nodes[node_idx].right = build_recursive(prims, mid_idx, end);

    return node_idx;
}
