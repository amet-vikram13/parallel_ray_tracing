#pragma once

#include "core/scene.h"
#include "camera/camera.h"
#include "utils/image.h"
#include "renderer/render_config.h"
#include "geometry/bvh_node.h"
#include <vector>

class CPURenderer {
public:
    // Phase 1: Phong shading with hard shadows
    void render_phong(const Scene& scene, const Camera& cam,
                      Image& output, const RenderConfig& config);

    // Phase 2: Monte Carlo path tracing (OpenMP)
    void render_pathtraced(const Scene& scene, const Camera& cam,
                           Image& output, const RenderConfig& config);

    // Phase 3: Set BVH for accelerated traversal
    void set_bvh(const std::vector<BVHNode>& nodes,
                 const std::vector<PrimitiveRef>& prim_refs, int root);

private:
    // Trace single ray against scene, return closest hit
    bool trace(const Scene& scene, const Ray& ray, float t_min, float t_max,
               HitRecord& rec) const;

    // BVH-accelerated trace using iterative stack-based traversal
    bool trace_bvh(const Scene& scene, const Ray& ray, float t_min, float t_max,
                   HitRecord& rec) const;

    // Phong shade a hit point
    Vec3 shade_phong(const Scene& scene, const Ray& ray,
                     const HitRecord& rec) const;

    // Recursive path trace
    Vec3 trace_path(const Scene& scene, const Ray& ray, int depth,
                    const RenderConfig& config, unsigned int& seed) const;

    // Simple hash-based RNG for CPU
    float random_float(unsigned int& seed) const;
    Vec3 random_hemisphere(const Vec3& normal, unsigned int& seed) const;

    // BVH data (Phase 3)
    const BVHNode* bvh_nodes = nullptr;
    const PrimitiveRef* bvh_prim_refs = nullptr;
    int bvh_root = -1;
    bool use_bvh = false;
};
