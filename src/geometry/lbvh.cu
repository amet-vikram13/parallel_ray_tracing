#include "geometry/lbvh.h"
#include "utils/cuda_utils.h"
#include <thrust/sort.h>
#include <thrust/device_vector.h>
#include <cuda_runtime.h>

// ========== Morton Code Utilities ==========

// Expand a 10-bit integer into 30 bits by inserting 2 zeros between each bit
__device__ __host__ unsigned int expand_bits(unsigned int v) {
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

// Compute 30-bit Morton code for a 3D point in [0,1]^3
__device__ __host__ unsigned int morton_3d(float x, float y, float z) {
    x = fminf(fmaxf(x * 1024.0f, 0.0f), 1023.0f);
    y = fminf(fmaxf(y * 1024.0f, 0.0f), 1023.0f);
    z = fminf(fmaxf(z * 1024.0f, 0.0f), 1023.0f);
    unsigned int xx = expand_bits(static_cast<unsigned int>(x));
    unsigned int yy = expand_bits(static_cast<unsigned int>(y));
    unsigned int zz = expand_bits(static_cast<unsigned int>(z));
    return xx * 4 + yy * 2 + zz;
}

// ========== Kernel: Compute Morton Codes ==========

__global__ void compute_morton_kernel(
    const Sphere* spheres, int num_spheres,
    const Triangle* triangles, int num_triangles,
    unsigned int* morton_codes, int* indices,
    Vec3 scene_min, Vec3 scene_extent, int total_prims)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= total_prims) return;

    Vec3 centroid;
    if (tid < num_spheres) {
        centroid = spheres[tid].center;
    } else {
        int tri_idx = tid - num_spheres;
        const Triangle& t = triangles[tri_idx];
        centroid = (t.v0 + t.v1 + t.v2) / 3.0f;
    }

    // Normalize to [0, 1]
    Vec3 normalized = (centroid - scene_min) / scene_extent;
    normalized.x = fminf(fmaxf(normalized.x, 0.0f), 1.0f);
    normalized.y = fminf(fmaxf(normalized.y, 0.0f), 1.0f);
    normalized.z = fminf(fmaxf(normalized.z, 0.0f), 1.0f);

    morton_codes[tid] = morton_3d(normalized.x, normalized.y, normalized.z);
    indices[tid] = tid;
}

void lbvh_compute_morton_codes(const Sphere* d_spheres, int num_spheres,
                               const Triangle* d_triangles, int num_triangles,
                               unsigned int* d_morton_codes, int* d_indices,
                               AABB scene_bounds, int total_prims)
{
    Vec3 extent = scene_bounds.max_bound - scene_bounds.min_bound;
    // Prevent division by zero
    extent.x = fmaxf(extent.x, EPSILON);
    extent.y = fmaxf(extent.y, EPSILON);
    extent.z = fmaxf(extent.z, EPSILON);

    int block_size = 256;
    int grid_size = (total_prims + block_size - 1) / block_size;
    compute_morton_kernel<<<grid_size, block_size>>>(
        d_spheres, num_spheres, d_triangles, num_triangles,
        d_morton_codes, d_indices,
        scene_bounds.min_bound, extent, total_prims);
    CUDA_SYNC_CHECK();
}

// ========== Karras (2012) Radix Tree Construction ==========

// Longest common prefix between keys[i] and keys[j]
__device__ int delta(const unsigned int* sorted_morton, int num_leaves, int i, int j) {
    if (j < 0 || j >= num_leaves) return -1;
    if (sorted_morton[i] == sorted_morton[j]) {
        // Tie-break using index
        return 32 + __clz(i ^ j);
    }
    return __clz(sorted_morton[i] ^ sorted_morton[j]);
}

__global__ void build_tree_kernel(
    const unsigned int* sorted_morton, const int* sorted_indices,
    BVHNode* nodes, int num_leaves)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= num_leaves - 1) return;  // N-1 internal nodes

    // Determine direction of the range
    int d_left  = delta(sorted_morton, num_leaves, i, i - 1);
    int d_right = delta(sorted_morton, num_leaves, i, i + 1);
    int d = (d_right - d_left >= 0) ? 1 : -1;

    // Compute upper bound for the range length
    int delta_min = delta(sorted_morton, num_leaves, i, i - d);
    int l_max = 2;
    while (delta(sorted_morton, num_leaves, i, i + l_max * d) > delta_min)
        l_max *= 2;

    // Binary search for the actual range length
    int l = 0;
    for (int t = l_max / 2; t >= 1; t /= 2) {
        if (delta(sorted_morton, num_leaves, i, i + (l + t) * d) > delta_min)
            l += t;
    }
    int j = i + l * d;  // other end of the range

    // Find the split position using binary search
    int delta_node = delta(sorted_morton, num_leaves, i, j);
    int s = 0;
    int t = l;
    do {
        t = (t + 1) / 2;
        if (delta(sorted_morton, num_leaves, i, i + (s + t) * d) > delta_node)
            s += t;
    } while (t > 1);
    int split = i + s * d + min(d, 0);

    // Assign children
    // Internal nodes: [0, num_leaves-2], leaf nodes: [num_leaves-1, 2*num_leaves-2]
    int left_child, right_child;
    if (min(i, j) == split) {
        // Left child is a leaf
        left_child = num_leaves - 1 + split;
        nodes[left_child].prim_idx = sorted_indices[split];
        nodes[left_child].prim_count = 1;
    } else {
        left_child = split;
    }

    if (max(i, j) == split + 1) {
        // Right child is a leaf
        right_child = num_leaves - 1 + split + 1;
        nodes[right_child].prim_idx = sorted_indices[split + 1];
        nodes[right_child].prim_count = 1;
    } else {
        right_child = split + 1;
    }

    nodes[i].left = left_child;
    nodes[i].right = right_child;
    nodes[i].prim_count = 0;  // internal node
}

void lbvh_build_tree(unsigned int* d_sorted_morton, int* d_sorted_indices,
                     BVHNode* d_nodes, int num_leaves)
{
    int block_size = 256;
    int grid_size = (num_leaves - 1 + block_size - 1) / block_size;
    build_tree_kernel<<<grid_size, block_size>>>(
        d_sorted_morton, d_sorted_indices, d_nodes, num_leaves);
    CUDA_SYNC_CHECK();
}

// ========== Bottom-up AABB Computation ==========

__device__ AABB compute_leaf_aabb(int prim_global_idx, int num_spheres,
                                   const Sphere* spheres, const Triangle* triangles) {
    if (prim_global_idx < num_spheres) {
        const Sphere& s = spheres[prim_global_idx];
        return AABB(s.center - Vec3(s.radius), s.center + Vec3(s.radius));
    } else {
        int tri_idx = prim_global_idx - num_spheres;
        const Triangle& t = triangles[tri_idx];
        Vec3 mn = glm::min(glm::min(t.v0, t.v1), t.v2) - Vec3(EPSILON);
        Vec3 mx = glm::max(glm::max(t.v0, t.v1), t.v2) + Vec3(EPSILON);
        return AABB(mn, mx);
    }
}

// Find parent pointers from the tree structure
__global__ void find_parents_kernel(BVHNode* nodes, int* parents, int num_internal) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= num_internal) return;

    int left = nodes[i].left;
    int right = nodes[i].right;
    if (left >= 0) parents[left] = i;
    if (right >= 0) parents[right] = i;
}

// Bottom-up AABB computation: each leaf walks to root using atomic counters
static __global__ void bbox_bottom_up_full_kernel(
    BVHNode* nodes, int* parents, int* atom_counters,
    const Sphere* spheres, int num_spheres,
    const Triangle* triangles,
    int num_leaves)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= num_leaves) return;

    int leaf_idx = num_leaves - 1 + tid;
    int prim_global_idx = nodes[leaf_idx].prim_idx;

    nodes[leaf_idx].bounds = compute_leaf_aabb(prim_global_idx, num_spheres,
                                                spheres, triangles);

    int current = parents[leaf_idx];
    while (current >= 0) {
        int count = atomicAdd(&atom_counters[current], 1);
        if (count == 0) return;
        __threadfence();
        int left = nodes[current].left;
        int right = nodes[current].right;
        nodes[current].bounds = AABB::merge(nodes[left].bounds, nodes[right].bounds);
        __threadfence();
        current = parents[current];
    }
}

// ========== LBVHBuilder Implementation ==========

void LBVHBuilder::build(const std::vector<Sphere>& spheres,
                        const std::vector<Triangle>& triangles) {
    int num_spheres = static_cast<int>(spheres.size());
    int num_triangles = static_cast<int>(triangles.size());
    int total_prims = num_spheres + num_triangles;
    if (total_prims == 0) return;

    int num_internal = total_prims - 1;
    int total_nodes = 2 * total_prims - 1;

    // Build primitive refs (host side)
    h_prim_refs.resize(total_prims);
    for (int i = 0; i < num_spheres; ++i)
        h_prim_refs[i] = PrimitiveRef(PRIM_SPHERE, i);
    for (int i = 0; i < num_triangles; ++i)
        h_prim_refs[num_spheres + i] = PrimitiveRef(PRIM_TRIANGLE, i);

    // Compute scene bounds on CPU for Morton code normalization
    AABB scene_bounds;
    for (const auto& s : spheres) {
        scene_bounds = AABB::merge(scene_bounds,
            AABB(s.center - Vec3(s.radius), s.center + Vec3(s.radius)));
    }
    for (const auto& t : triangles) {
        Vec3 mn = glm::min(glm::min(t.v0, t.v1), t.v2);
        Vec3 mx = glm::max(glm::max(t.v0, t.v1), t.v2);
        scene_bounds = AABB::merge(scene_bounds, AABB(mn, mx));
    }

    // Upload scene to GPU
    Sphere* d_spheres_ptr = nullptr;
    Triangle* d_triangles_ptr = nullptr;
    if (num_spheres > 0) {
        CUDA_CHECK(cudaMalloc(&d_spheres_ptr, num_spheres * sizeof(Sphere)));
        CUDA_CHECK(cudaMemcpy(d_spheres_ptr, spheres.data(),
                              num_spheres * sizeof(Sphere), cudaMemcpyHostToDevice));
    }
    if (num_triangles > 0) {
        CUDA_CHECK(cudaMalloc(&d_triangles_ptr, num_triangles * sizeof(Triangle)));
        CUDA_CHECK(cudaMemcpy(d_triangles_ptr, triangles.data(),
                              num_triangles * sizeof(Triangle), cudaMemcpyHostToDevice));
    }

    // Allocate GPU buffers
    unsigned int* d_morton_codes;
    int* d_indices;
    BVHNode* d_nodes;
    int* d_atom_counters;
    int* d_parents;
    PrimitiveRef* d_prim_refs_gpu;

    CUDA_CHECK(cudaMalloc(&d_morton_codes, total_prims * sizeof(unsigned int)));
    CUDA_CHECK(cudaMalloc(&d_indices, total_prims * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_nodes, total_nodes * sizeof(BVHNode)));
    CUDA_CHECK(cudaMalloc(&d_atom_counters, num_internal * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_parents, total_nodes * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_prim_refs_gpu, total_prims * sizeof(PrimitiveRef)));

    CUDA_CHECK(cudaMemset(d_nodes, 0, total_nodes * sizeof(BVHNode)));
    CUDA_CHECK(cudaMemset(d_atom_counters, 0, num_internal * sizeof(int)));
    CUDA_CHECK(cudaMemset(d_parents, 0xFF, total_nodes * sizeof(int)));

    CUDA_CHECK(cudaMemcpy(d_prim_refs_gpu, h_prim_refs.data(),
                          total_prims * sizeof(PrimitiveRef), cudaMemcpyHostToDevice));

    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    // --- Step 1: Compute Morton codes ---
    cudaEvent_t morton_start, morton_stop;
    cudaEventCreate(&morton_start);
    cudaEventCreate(&morton_stop);
    cudaEventRecord(morton_start);

    lbvh_compute_morton_codes(d_spheres_ptr, num_spheres, d_triangles_ptr, num_triangles,
                              d_morton_codes, d_indices, scene_bounds, total_prims);

    cudaEventRecord(morton_stop);
    cudaEventSynchronize(morton_stop);
    float morton_ms;
    cudaEventElapsedTime(&morton_ms, morton_start, morton_stop);
    morton_time_ms = morton_ms;

    // --- Step 2: Sort by Morton codes ---
    cudaEvent_t sort_start, sort_stop;
    cudaEventCreate(&sort_start);
    cudaEventCreate(&sort_stop);
    cudaEventRecord(sort_start);

    thrust::device_ptr<unsigned int> d_morton_ptr(d_morton_codes);
    thrust::device_ptr<int> d_indices_ptr(d_indices);
    thrust::sort_by_key(d_morton_ptr, d_morton_ptr + total_prims, d_indices_ptr);

    cudaEventRecord(sort_stop);
    cudaEventSynchronize(sort_stop);
    float sort_ms;
    cudaEventElapsedTime(&sort_ms, sort_start, sort_stop);
    sort_time_ms = sort_ms;

    // --- Step 3: Build tree (Karras algorithm) ---
    cudaEvent_t tree_start, tree_stop;
    cudaEventCreate(&tree_start);
    cudaEventCreate(&tree_stop);
    cudaEventRecord(tree_start);

    lbvh_build_tree(d_morton_codes, d_indices, d_nodes, total_prims);

    cudaEventRecord(tree_stop);
    cudaEventSynchronize(tree_stop);
    float tree_ms;
    cudaEventElapsedTime(&tree_ms, tree_start, tree_stop);
    tree_time_ms = tree_ms;

    // --- Step 4: Compute AABBs bottom-up ---
    cudaEvent_t bbox_start, bbox_stop;
    cudaEventCreate(&bbox_start);
    cudaEventCreate(&bbox_stop);
    cudaEventRecord(bbox_start);

    // Find parents
    int block_size = 256;
    int grid_internal = (num_internal + block_size - 1) / block_size;
    find_parents_kernel<<<grid_internal, block_size>>>(d_nodes, d_parents, num_internal);
    CUDA_SYNC_CHECK();

    // Bottom-up AABB computation
    int grid_leaves = (total_prims + block_size - 1) / block_size;
    bbox_bottom_up_full_kernel<<<grid_leaves, block_size>>>(
        d_nodes, d_parents, d_atom_counters,
        d_spheres_ptr, num_spheres, d_triangles_ptr, total_prims);
    CUDA_SYNC_CHECK();

    cudaEventRecord(bbox_stop);
    cudaEventSynchronize(bbox_stop);
    float bbox_ms;
    cudaEventElapsedTime(&bbox_ms, bbox_start, bbox_stop);
    bbox_time_ms = bbox_ms;

    build_time_ms = morton_time_ms + sort_time_ms + tree_time_ms + bbox_time_ms;

    // --- Download results to host ---
    h_nodes.resize(total_nodes);
    CUDA_CHECK(cudaMemcpy(h_nodes.data(), d_nodes,
                          total_nodes * sizeof(BVHNode), cudaMemcpyDeviceToHost));

    // Cleanup GPU allocations
    cudaFree(d_morton_codes);
    cudaFree(d_indices);
    cudaFree(d_nodes);
    cudaFree(d_atom_counters);
    cudaFree(d_parents);
    cudaFree(d_prim_refs_gpu);
    if (d_spheres_ptr) cudaFree(d_spheres_ptr);
    if (d_triangles_ptr) cudaFree(d_triangles_ptr);

    cudaEventDestroy(morton_start); cudaEventDestroy(morton_stop);
    cudaEventDestroy(sort_start); cudaEventDestroy(sort_stop);
    cudaEventDestroy(tree_start); cudaEventDestroy(tree_stop);
    cudaEventDestroy(bbox_start); cudaEventDestroy(bbox_stop);
    cudaEventDestroy(start); cudaEventDestroy(stop);
}
