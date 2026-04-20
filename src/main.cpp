#include "core/scene.h"
#include "camera/camera.h"
#include "renderer/cpu_renderer.h"
#include "renderer/gpu_renderer.h"
#include "renderer/render_config.h"
#include "geometry/sah_bvh.h"
#include "geometry/lbvh.h"
#include "utils/image.h"
#include "utils/timer.h"
#include "scenes/scene_registry.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cuda_runtime.h>
#include <string>
#include <vector>

static void print_usage(const char* prog) {
    printf("Usage: %s --scene <label> [options]\n", prog);
    printf("  --scene   <label>   Required. Selects the scene to render.\n");
    printf("  --no-accel          Disables BVH; uses brute-force ray-scene intersection.\n");
    printf("  --bvh     <type>    BVH type: 'sah' | 'lbvh' | 'none' (default: sah).\n");
    printf("  --spp     <N>       Samples per pixel (default: 64).\n");
    printf("  --width   <W>       Output width  (default: 800).\n");
    printf("  --height  <H>       Output height (default: 600).\n");
    printf("  --frames  <F>       Number of animation frames (default: 1).\n");
    printf("  --depth   <N>       Max bounce depth for path tracing (default: 10).\n");
    printf("  --cpu               Use CPU renderer (OpenMP).\n");
    printf("  --gpu               Use GPU renderer (CUDA) [default].\n");
    printf("  --phong             Use Phong shading (Phase 1) for geometry scenes.\n");
    printf("  --output  <prefix>  Output filename prefix (default: frame).\n");
    printf("  --help              Print this help message.\n");
}

static void print_scene_labels() {
    const auto labels = SceneRegistry::instance().labels();
    printf("Available scenes:\n");
    for (const auto& l : labels) printf("  - %s\n", l.c_str());
}

static const char* bvh_mode_label(BVHType t) {
    switch (t) {
        case BVHType::SAH:  return "sah";
        case BVHType::LBVH: return "lbvh";
        default:            return "brute";
    }
}

int main(int argc, char** argv) {
    register_all_scenes();

    RenderConfig config;
    bool        use_gpu       = true;
    bool        phong_mode    = false;
    std::string scene_name;
    std::string output_prefix = "frame";
    bool        explicit_bvh  = false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "--width")  && i + 1 < argc) config.width  = atoi(argv[++i]);
        else if (!strcmp(a, "--height") && i + 1 < argc) config.height = atoi(argv[++i]);
        else if (!strcmp(a, "--spp")    && i + 1 < argc) config.samples_per_pixel = atoi(argv[++i]);
        else if (!strcmp(a, "--depth")  && i + 1 < argc) config.max_depth = atoi(argv[++i]);
        else if (!strcmp(a, "--frames") && i + 1 < argc) config.frames = atoi(argv[++i]);
        else if (!strcmp(a, "--cpu"))    use_gpu = false;
        else if (!strcmp(a, "--gpu"))    use_gpu = true;
        else if (!strcmp(a, "--phong"))  phong_mode = true;
        else if (!strcmp(a, "--no-accel")) config.use_accel = false;
        else if (!strcmp(a, "--scene")  && i + 1 < argc) scene_name = argv[++i];
        else if (!strcmp(a, "--output") && i + 1 < argc) output_prefix = argv[++i];
        else if (!strcmp(a, "--bvh")    && i + 1 < argc) {
            const char* bvh_name = argv[++i];
            explicit_bvh = true;
            if (!strcmp(bvh_name, "sah"))       config.bvh_type = BVHType::SAH;
            else if (!strcmp(bvh_name, "lbvh")) config.bvh_type = BVHType::LBVH;
            else                                 config.bvh_type = BVHType::NONE;
        }
        else if (!strcmp(a, "--help")) { print_usage(argv[0]); print_scene_labels(); return 0; }
        else { fprintf(stderr, "Unknown argument: %s\n", a); print_usage(argv[0]); return 2; }
    }

    if (scene_name.empty()) {
        fprintf(stderr, "error: --scene <label> is required.\n");
        print_usage(argv[0]);
        print_scene_labels();
        return 1;
    }
    if (!SceneRegistry::instance().has(scene_name)) {
        fprintf(stderr, "error: unknown scene label '%s'.\n", scene_name.c_str());
        print_scene_labels();
        return 1;
    }

    // Build scene via registry.
    Scene scene;
    SceneRegistry::instance().build(scene_name, scene, config);

    // Resolve effective acceleration mode. `--no-accel` always wins; otherwise
    // a default SAH BVH is used unless the user asked for something specific.
    if (!config.use_accel) {
        config.bvh_type = BVHType::NONE;
    } else if (!explicit_bvh) {
        config.bvh_type = BVHType::SAH;
    }
    const bool is_shader_scene = (scene.mode == SceneMode::OCEAN);

    const int total_prims =
        static_cast<int>(scene.spheres.size() + scene.triangles.size());

    printf("Scene: %s (%zu spheres, %zu triangles, %zu materials, %zu lights)\n",
           scene_name.c_str(), scene.spheres.size(), scene.triangles.size(),
           scene.materials.size(), scene.lights.size());
    printf("Resolution: %d x %d | SPP: %d | Depth: %d | Mode: %s | Device: %s | Accel: %s | Frames: %d\n",
           config.width, config.height, config.samples_per_pixel, config.max_depth,
           is_shader_scene ? "Shader"
               : (phong_mode ? "Phong" : "Path Tracing"),
           use_gpu ? "GPU" : "CPU",
           bvh_mode_label(config.bvh_type),
           config.frames);

    // Camera is rebuilt each frame from scene.cam_* in case animate() moves it.
    Camera cam;
    cam.init(scene.cam_lookfrom, scene.cam_lookat, scene.cam_vup,
             scene.cam_vfov_deg, config.width, config.height);

    // Build BVH only for geometry scenes that actually have primitives.
    std::vector<BVHNode>     bvh_nodes;
    std::vector<PrimitiveRef> bvh_prim_refs;
    int bvh_root = 0;
    const bool build_bvh = (!is_shader_scene) &&
                           (config.bvh_type != BVHType::NONE) &&
                           (total_prims > 0);

    if (build_bvh && config.bvh_type == BVHType::SAH) {
        printf("\n=== Building CPU SAH-BVH ===\n");
        SAHBVHBuilder sah_builder;
        sah_builder.build(scene.spheres, scene.triangles);
        bvh_nodes    = sah_builder.get_nodes();
        bvh_prim_refs = sah_builder.get_prim_refs();
        bvh_root     = sah_builder.get_root();
        printf("  Primitives: %d | BVH Nodes: %zu | Root: %d\n",
               total_prims, bvh_nodes.size(), bvh_root);
        printf("  Build time: %.3f ms\n", sah_builder.build_time_ms);
    } else if (build_bvh && config.bvh_type == BVHType::LBVH) {
        printf("\n=== Building GPU LBVH ===\n");
        LBVHBuilder lbvh_builder;
        lbvh_builder.build(scene.spheres, scene.triangles);
        bvh_nodes    = lbvh_builder.get_nodes();
        bvh_prim_refs = lbvh_builder.get_prim_refs();
        bvh_root     = lbvh_builder.get_root();
        printf("  Primitives: %d | BVH Nodes: %zu | Root: %d\n",
               total_prims, bvh_nodes.size(), bvh_root);
        printf("  Total build time: %.3f ms\n", lbvh_builder.build_time_ms);
    }

    // GPU setup (only for geometry scenes; ocean bypasses scene upload).
    GPURenderer gpu;
    float*      d_framebuffer = nullptr;
    const int   num_pixels    = config.width * config.height;

    if (use_gpu) {
        if (!is_shader_scene) {
            gpu.upload_scene(scene);
            if (!bvh_nodes.empty()) {
                gpu.upload_bvh(bvh_nodes, bvh_prim_refs, bvh_root);
            }
        }
        cudaMalloc(&d_framebuffer, num_pixels * 3 * sizeof(float));
    }

    CPURenderer cpu;
    if (!use_gpu && !is_shader_scene && !bvh_nodes.empty()) {
        cpu.set_bvh(bvh_nodes, bvh_prim_refs, bvh_root);
    }

    // Timing log (written whenever --frames > 1, per the spec).
    FILE* csv = nullptr;
    if (config.frames > 1) {
        csv = fopen("timing.csv", "w");
        if (csv) {
            fprintf(csv, "frame,accel_mode,render_time_ms,rays_cast\n");
        } else {
            fprintf(stderr, "warning: could not open timing.csv for writing.\n");
        }
    }

    Image output(config.width, config.height);

    printf("\n=== Rendering %d frame%s ===\n",
           config.frames, config.frames == 1 ? "" : "s");

    for (int f = 0; f < config.frames; ++f) {
        const float t = static_cast<float>(f);

        if (scene.animate) scene.animate(scene, t);
        cam.init(scene.cam_lookfrom, scene.cam_lookat, scene.cam_vup,
                 scene.cam_vfov_deg, config.width, config.height);

        Timer timer;
        timer.start();

        if (use_gpu) {
            if (is_shader_scene) {
                gpu.render_shader_ocean(cam, t, d_framebuffer, config);
            } else if (phong_mode) {
                gpu.render_phong(cam, d_framebuffer, config);
            } else {
                gpu.render_pathtraced(cam, d_framebuffer, config);
            }
            gpu.download_framebuffer(d_framebuffer, output.pixels.data(), num_pixels);
        } else {
            if (is_shader_scene) {
                cpu.render_shader(scene, cam, output, config, t);
            } else if (phong_mode) {
                cpu.render_phong(scene, cam, output, config);
            } else {
                cpu.render_pathtraced(scene, cam, output, config);
            }
        }

        timer.stop();

        // Write frame image. Single-frame runs use the prefix verbatim as
        // <prefix>.ppm; multi-frame runs follow the frame_XXXX.ppm convention
        // from the spec.
        char path[512];
        if (config.frames == 1) {
            snprintf(path, sizeof(path), "%s.ppm", output_prefix.c_str());
        } else {
            snprintf(path, sizeof(path), "%s_%04d.ppm", output_prefix.c_str(), f);
        }
        output.write_ppm(path);

        const long long rays_cast =
            static_cast<long long>(config.width) *
            static_cast<long long>(config.height) *
            static_cast<long long>(std::max(1, config.samples_per_pixel));

        printf("  frame %4d  t=%.3f  %6.2f ms  -> %s\n",
               f, t, timer.elapsed_ms(), path);

        if (csv) {
            fprintf(csv, "%d,%s,%.3f,%lld\n",
                    f, bvh_mode_label(config.bvh_type),
                    timer.elapsed_ms(),
                    rays_cast);
        }
    }

    if (csv) {
        fclose(csv);
        printf("Timing log written to: timing.csv\n");
    }

    if (use_gpu) {
        if (d_framebuffer) cudaFree(d_framebuffer);
        gpu.cleanup();
    }

    return 0;
}
