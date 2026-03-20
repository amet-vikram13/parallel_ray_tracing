#pragma once

#include "core/common.h"
#include "geometry/aabb.h"

// Reference to a scene primitive (sphere or triangle)
enum PrimitiveType : int {
    PRIM_SPHERE   = 0,
    PRIM_TRIANGLE = 1
};

struct PrimitiveRef {
    int type;       // PrimitiveType
    int index;      // index into scene.spheres or scene.triangles

    HD PrimitiveRef() : type(0), index(0) {}
    HD PrimitiveRef(int t, int i) : type(t), index(i) {}
};

// Flat BVH node suitable for both CPU and GPU traversal
struct BVHNode {
    AABB bounds;
    int left;         // left child index (-1 for leaf)
    int right;        // right child index (-1 for leaf)
    int prim_idx;     // index into primitive refs array (leaf only)
    int prim_count;   // 0 = internal node, >0 = leaf node

    HD BVHNode() : left(-1), right(-1), prim_idx(-1), prim_count(0) {}

    HD bool is_leaf() const { return prim_count > 0; }
};

// GPU-friendly BVH data
struct DeviceBVH {
    BVHNode* nodes;
    int num_nodes;
    PrimitiveRef* prim_refs;
    int num_prims;
};
