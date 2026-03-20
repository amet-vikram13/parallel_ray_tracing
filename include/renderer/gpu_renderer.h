#pragma once

#include "core/scene.h"
#include "camera/camera.h"
#include "renderer/render_config.h"
#include "geometry/bvh_node.h"
#include <vector>

class GPURenderer {
public:
    GPURenderer();
    ~GPURenderer();

    // Allocate device memory for scene
    void upload_scene(const Scene& scene);

    // Phase 3: Upload BVH to GPU
    void upload_bvh(const std::vector<BVHNode>& nodes,
                    const std::vector<PrimitiveRef>& prim_refs, int root);

    // Phase 1: Phong shading on GPU
    void render_phong(const Camera& cam, float* d_output, const RenderConfig& config);

    // Phase 2: Path tracing on GPU
    void render_pathtraced(const Camera& cam, float* d_output, const RenderConfig& config);

    // Download framebuffer from GPU
    void download_framebuffer(float* d_output, Vec3* h_pixels, int num_pixels);

    // Free device memory
    void cleanup();

private:
    // Device scene buffers
    Sphere* d_spheres = nullptr;
    Triangle* d_triangles = nullptr;
    Material* d_materials = nullptr;
    PointLight* d_lights = nullptr;
    DeviceScene d_scene;

    // Phase 3: BVH device buffers
    BVHNode* d_bvh_nodes = nullptr;
    PrimitiveRef* d_bvh_prim_refs = nullptr;
    DeviceBVH d_bvh;
    bool bvh_uploaded = false;

    // RNG states
    void* d_rng_states = nullptr;
    bool rng_initialized = false;

    void init_rng(int width, int height);
};
