#include "core/scene.h"
#include "camera/camera.h"
#include "renderer/cpu_renderer.h"
#include "renderer/gpu_renderer.h"
#include "renderer/render_config.h"
#include "geometry/sah_bvh.h"
#include "geometry/lbvh.h"
#include "utils/image.h"
#include "utils/timer.h"
#include <cstdio>
#include <cstring>
#include <cuda_runtime.h>

void print_usage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  --width  <int>    Image width  (default: 800)\n");
    printf("  --height <int>    Image height (default: 600)\n");
    printf("  --spp    <int>    Samples per pixel (default: 64)\n");
    printf("  --depth  <int>    Max bounce depth  (default: 10)\n");
    printf("  --cpu             Use CPU renderer (OpenMP)\n");
    printf("  --gpu             Use GPU renderer (CUDA)\n");
    printf("  --phong           Phase 1: Phong shading only\n");
    printf("  --scene  <name>   Scene: 'test' or 'cornell' (default: cornell)\n");
    printf("  --output <file>   Output PPM file (default: output.ppm)\n");
    printf("  --bvh    <type>   BVH type: 'none', 'sah', 'lbvh' (default: none)\n");
}

int main(int argc, char** argv) {
    RenderConfig config;
    bool use_gpu = true;
    bool phong_mode = false;
    const char* scene_name = "cornell";
    const char* output_file = "output.ppm";
    const char* bvh_name = "none";

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--width") && i + 1 < argc)
            config.width = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc)
            config.height = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--spp") && i + 1 < argc)
            config.samples_per_pixel = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--depth") && i + 1 < argc)
            config.max_depth = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cpu"))
            use_gpu = false;
        else if (!strcmp(argv[i], "--gpu"))
            use_gpu = true;
        else if (!strcmp(argv[i], "--phong"))
            phong_mode = true;
        else if (!strcmp(argv[i], "--scene") && i + 1 < argc)
            scene_name = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc)
            output_file = argv[++i];
        else if (!strcmp(argv[i], "--bvh") && i + 1 < argc) {
            bvh_name = argv[++i];
            if (!strcmp(bvh_name, "sah"))
                config.bvh_type = BVHType::SAH;
            else if (!strcmp(bvh_name, "lbvh"))
                config.bvh_type = BVHType::LBVH;
            else
                config.bvh_type = BVHType::NONE;
        }
        else if (!strcmp(argv[i], "--help")) {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Build scene
    Scene scene;
    if (!strcmp(scene_name, "cornell"))
        scene = Scene::create_cornell_box();
    else
        scene = Scene::create_test_scene();

    int total_prims = static_cast<int>(scene.spheres.size() + scene.triangles.size());
    printf("Scene: %s (%zu spheres, %zu triangles, %zu materials, %zu lights)\n",
           scene_name, scene.spheres.size(), scene.triangles.size(),
           scene.materials.size(), scene.lights.size());
    printf("Resolution: %d x %d | SPP: %d | Depth: %d | Mode: %s | Device: %s | BVH: %s\n",
           config.width, config.height, config.samples_per_pixel, config.max_depth,
           phong_mode ? "Phong" : "Path Tracing", use_gpu ? "GPU" : "CPU", bvh_name);

    // Setup camera
    Camera cam;
    if (!strcmp(scene_name, "cornell"))
        cam.init(Vec3(278, 278, -800), Vec3(278, 278, 0), Vec3(0, 1, 0),
                 40.0f, config.width, config.height);
    else
        cam.init(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0),
                 90.0f, config.width, config.height);

    // Phase 3: Build BVH acceleration structure
    std::vector<BVHNode> bvh_nodes;
    std::vector<PrimitiveRef> bvh_prim_refs;
    int bvh_root = 0;

    if (config.bvh_type == BVHType::SAH) {
        printf("\n=== Building CPU SAH-BVH ===\n");
        SAHBVHBuilder sah_builder;
        sah_builder.build(scene.spheres, scene.triangles);

        bvh_nodes = sah_builder.get_nodes();
        bvh_prim_refs = sah_builder.get_prim_refs();
        bvh_root = sah_builder.get_root();

        printf("  Primitives: %d | BVH Nodes: %zu | Root: %d\n",
               total_prims, bvh_nodes.size(), bvh_root);
        printf("  Build time: %.3f ms\n", sah_builder.build_time_ms);

    } else if (config.bvh_type == BVHType::LBVH) {
        printf("\n=== Building GPU LBVH (Morton codes + Karras algorithm) ===\n");
        LBVHBuilder lbvh_builder;
        lbvh_builder.build(scene.spheres, scene.triangles);

        bvh_nodes = lbvh_builder.get_nodes();
        bvh_prim_refs = lbvh_builder.get_prim_refs();
        bvh_root = lbvh_builder.get_root();

        printf("  Primitives: %d | BVH Nodes: %zu | Root: %d\n",
               total_prims, bvh_nodes.size(), bvh_root);
        printf("  Morton code computation: %.3f ms\n", lbvh_builder.morton_time_ms);
        printf("  Radix sort:              %.3f ms\n", lbvh_builder.sort_time_ms);
        printf("  Tree construction:       %.3f ms\n", lbvh_builder.tree_time_ms);
        printf("  AABB computation:        %.3f ms\n", lbvh_builder.bbox_time_ms);
        printf("  Total build time:        %.3f ms\n", lbvh_builder.build_time_ms);
    }

    Image output(config.width, config.height);
    Timer timer;

    if (use_gpu) {
        // GPU path
        GPURenderer gpu;
        gpu.upload_scene(scene);

        // Upload BVH if built
        if (config.bvh_type != BVHType::NONE && !bvh_nodes.empty()) {
            gpu.upload_bvh(bvh_nodes, bvh_prim_refs, bvh_root);
        }

        int num_pixels = config.width * config.height;
        float* d_framebuffer = nullptr;
        cudaMalloc(&d_framebuffer, num_pixels * 3 * sizeof(float));

        printf("\n=== Rendering ===\n");
        timer.start();
        if (phong_mode)
            gpu.render_phong(cam, d_framebuffer, config);
        else
            gpu.render_pathtraced(cam, d_framebuffer, config);
        timer.stop();

        gpu.download_framebuffer(d_framebuffer, output.pixels.data(), num_pixels);
        cudaFree(d_framebuffer);
        gpu.cleanup();
    } else {
        // CPU path
        CPURenderer cpu;

        // Set BVH if built
        if (config.bvh_type != BVHType::NONE && !bvh_nodes.empty()) {
            cpu.set_bvh(bvh_nodes, bvh_prim_refs, bvh_root);
        }

        printf("\n=== Rendering ===\n");
        timer.start();
        if (phong_mode)
            cpu.render_phong(scene, cam, output, config);
        else
            cpu.render_pathtraced(scene, cam, output, config);
        timer.stop();
    }

    timer.print("Render time");

    // Compute throughput metrics
    int num_pixels = config.width * config.height;
    double render_ms = timer.elapsed_ms();
    double mrays_per_sec = 0.0;
    if (!phong_mode) {
        double total_primary_rays = static_cast<double>(num_pixels) * config.samples_per_pixel;
        mrays_per_sec = total_primary_rays / (render_ms * 1000.0);
        printf("Throughput: %.2f Mrays/sec (primary rays only)\n", mrays_per_sec);
    }
    double fps = 1000.0 / render_ms;
    printf("Frames/sec: %.2f\n", fps);

    output.write_ppm(output_file);
    printf("Output written to: %s\n", output_file);

    return 0;
}
