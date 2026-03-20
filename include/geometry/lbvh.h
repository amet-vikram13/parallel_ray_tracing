#pragma once

#include "geometry/bvh_node.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include <vector>

// GPU LBVH builder using Morton code sorting and the Karras (2012) algorithm
class LBVHBuilder {
public:
    // Build LBVH on GPU. Scene data must already be on host.
    void build(const std::vector<Sphere>& spheres,
               const std::vector<Triangle>& triangles);

    // Download built BVH from GPU to host vectors
    const std::vector<BVHNode>& get_nodes() const { return h_nodes; }
    const std::vector<PrimitiveRef>& get_prim_refs() const { return h_prim_refs; }
    int get_root() const { return 0; }  // root is always internal node 0

    // Timing
    double build_time_ms = 0.0;
    double morton_time_ms = 0.0;
    double sort_time_ms = 0.0;
    double tree_time_ms = 0.0;
    double bbox_time_ms = 0.0;

private:
    std::vector<BVHNode> h_nodes;
    std::vector<PrimitiveRef> h_prim_refs;
};

// CUDA kernel launch wrappers (defined in lbvh.cu)
void lbvh_compute_morton_codes(const Sphere* d_spheres, int num_spheres,
                               const Triangle* d_triangles, int num_triangles,
                               unsigned int* d_morton_codes, int* d_indices,
                               AABB scene_bounds, int total_prims);

void lbvh_build_tree(unsigned int* d_sorted_morton, int* d_sorted_indices,
                     BVHNode* d_nodes, int num_leaves);
