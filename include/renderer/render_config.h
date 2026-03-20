#pragma once

enum class BVHType : int {
    NONE = 0,     // brute-force (no BVH)
    SAH  = 1,     // CPU SAH-BVH
    LBVH = 2      // GPU LBVH (Morton codes)
};

struct RenderConfig {
    int width = 800;
    int height = 600;
    int samples_per_pixel = 64;
    int max_depth = 10;            // max bounce depth
    float russian_roulette_prob = 0.8f;
    bool use_gpu = true;
    bool show_preview = false;
    bool phong_mode = false;       // Phase 1 mode
    BVHType bvh_type = BVHType::NONE;  // Phase 3: acceleration structure
};
