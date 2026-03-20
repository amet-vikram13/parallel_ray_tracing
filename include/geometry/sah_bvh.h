#pragma once

#include "geometry/bvh_node.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include <vector>

// CPU SAH-BVH builder using Surface Area Heuristic
class SAHBVHBuilder {
public:
    // Build BVH over all scene primitives (spheres + triangles)
    void build(const std::vector<Sphere>& spheres,
               const std::vector<Triangle>& triangles);

    const std::vector<BVHNode>& get_nodes() const { return nodes; }
    const std::vector<PrimitiveRef>& get_prim_refs() const { return prim_refs; }
    int get_root() const { return root_idx; }

    // Timing
    double build_time_ms = 0.0;

private:
    static constexpr int NUM_BINS = 12;
    static constexpr float TRAVERSAL_COST = 1.0f;
    static constexpr float INTERSECT_COST = 1.0f;

    struct BuildPrim {
        AABB bounds;
        Vec3 centroid;
        int ref_index;   // index into prim_refs
    };

    std::vector<BVHNode> nodes;
    std::vector<PrimitiveRef> prim_refs;
    int root_idx = 0;

    // Compute AABB for a sphere
    static AABB sphere_aabb(const Sphere& s);

    // Compute AABB for a triangle
    static AABB triangle_aabb(const Triangle& t);

    // Recursive build returning node index
    int build_recursive(std::vector<BuildPrim>& prims, int start, int end);
};
