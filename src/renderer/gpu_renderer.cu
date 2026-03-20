#include "renderer/gpu_renderer.h"
#include "utils/cuda_utils.h"
#include "utils/cuda_rng.h"
#include "geometry/ray.h"
#include "geometry/hit_record.h"
#include "geometry/aabb.h"

// Initialize RNG for each pixel thread
__global__ void init_rng_kernel(curandState* states, int width, int height, unsigned long long seed) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    int idx = y * width + x;
    curand_init(seed, idx, 0, &states[idx]);
}

// --- Device scene intersection (brute-force) ---
__device__ bool trace_scene_brute(const DeviceScene& scene, const Ray& ray,
                                   float t_min, float t_max, HitRecord& rec) {
    bool hit = false;
    float closest = t_max;

    for (int i = 0; i < scene.num_spheres; ++i) {
        HitRecord tmp;
        if (scene.spheres[i].intersect(ray, t_min, closest, tmp)) {
            hit = true;
            closest = tmp.t;
            rec = tmp;
        }
    }

    for (int i = 0; i < scene.num_triangles; ++i) {
        HitRecord tmp;
        if (scene.triangles[i].intersect(ray, t_min, closest, tmp)) {
            hit = true;
            closest = tmp.t;
            rec = tmp;
        }
    }

    return hit;
}

// --- Phase 3: Iterative stack-based BVH traversal on GPU ---
__device__ bool trace_scene_bvh(const DeviceScene& scene, const DeviceBVH& bvh,
                                 const Ray& ray, float t_min, float t_max,
                                 HitRecord& rec) {
    constexpr int STACK_SIZE = 64;
    int stack[STACK_SIZE];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;  // root node

    bool hit = false;
    float closest = t_max;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const BVHNode& node = bvh.nodes[node_idx];

        if (!node.bounds.intersect(ray, t_min, closest))
            continue;

        if (node.is_leaf()) {
            int prim_global_idx = node.prim_idx;
            HitRecord tmp;
            bool prim_hit = false;

            // For LBVH: prim_idx is global index (spheres first, then triangles)
            if (bvh.prim_refs != nullptr) {
                const PrimitiveRef& ref = bvh.prim_refs[prim_global_idx];
                if (ref.type == PRIM_SPHERE) {
                    prim_hit = scene.spheres[ref.index].intersect(ray, t_min, closest, tmp);
                } else {
                    prim_hit = scene.triangles[ref.index].intersect(ray, t_min, closest, tmp);
                }
            } else {
                if (prim_global_idx < scene.num_spheres) {
                    prim_hit = scene.spheres[prim_global_idx].intersect(ray, t_min, closest, tmp);
                } else {
                    int tri_idx = prim_global_idx - scene.num_spheres;
                    prim_hit = scene.triangles[tri_idx].intersect(ray, t_min, closest, tmp);
                }
            }

            if (prim_hit) {
                hit = true;
                closest = tmp.t;
                rec = tmp;
            }
        } else {
            // Push children onto stack
            if (node.left >= 0) stack[stack_ptr++] = node.left;
            if (node.right >= 0) stack[stack_ptr++] = node.right;
        }
    }

    return hit;
}

// Unified trace dispatcher
__device__ bool trace_scene(const DeviceScene& scene, const DeviceBVH* bvh,
                             const Ray& ray, float t_min, float t_max,
                             HitRecord& rec) {
    if (bvh != nullptr && bvh->nodes != nullptr) {
        return trace_scene_bvh(scene, *bvh, ray, t_min, t_max, rec);
    }
    return trace_scene_brute(scene, ray, t_min, t_max, rec);
}

// --- Phase 1: Phong kernel (with optional BVH) ---
__global__ void phong_kernel(DeviceScene scene, DeviceBVH bvh, bool use_bvh,
                             Camera cam, float* output, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const DeviceBVH* bvh_ptr = use_bvh ? &bvh : nullptr;

    Ray ray = cam.generate_ray(x, y);
    HitRecord rec;
    Vec3 color(0.0f);

    if (trace_scene(scene, bvh_ptr, ray, EPSILON, INF, rec)) {
        Material mat = scene.materials[rec.material_id];
        Vec3 ambient = 0.1f * mat.albedo;
        color = ambient;

        for (int l = 0; l < scene.num_lights; ++l) {
            PointLight light = scene.lights[l];
            Vec3 light_dir = glm::normalize(light.position - rec.point);
            float light_dist = glm::length(light.position - rec.point);

            // Shadow test
            Ray shadow_ray(rec.point + rec.normal * EPSILON, light_dir);
            HitRecord shadow_rec;
            if (trace_scene(scene, bvh_ptr, shadow_ray, EPSILON, light_dist, shadow_rec))
                continue;

            // Diffuse
            float diff = fmaxf(0.0f, glm::dot(rec.normal, light_dir));
            color += diff * mat.albedo * light.color * light.intensity;

            // Specular (Blinn-Phong)
            Vec3 view_dir = glm::normalize(-ray.direction);
            Vec3 half_dir = glm::normalize(light_dir + view_dir);
            float spec = powf(fmaxf(0.0f, glm::dot(rec.normal, half_dir)), mat.shininess);
            color += spec * light.color * light.intensity;
        }
    } else {
        float t = 0.5f * (ray.direction.y + 1.0f);
        color = (1.0f - t) * Vec3(1.0f) + t * Vec3(0.5f, 0.7f, 1.0f);
    }

    int idx = (y * width + x) * 3;
    output[idx + 0] = color.r;
    output[idx + 1] = color.g;
    output[idx + 2] = color.b;
}

// --- Phase 2: Path tracing kernel (with optional BVH) ---
__device__ Vec3 trace_path_gpu(const DeviceScene& scene, const DeviceBVH* bvh,
                               Ray ray, int max_depth,
                               float rr_prob, curandState* rng) {
    Vec3 throughput(1.0f);
    Vec3 radiance(0.0f);

    for (int depth = 0; depth < max_depth; ++depth) {
        HitRecord rec;
        if (!trace_scene(scene, bvh, ray, EPSILON, INF, rec)) {
            // Sky
            float t = 0.5f * (ray.direction.y + 1.0f);
            Vec3 sky = (1.0f - t) * Vec3(1.0f) + t * Vec3(0.5f, 0.7f, 1.0f);
            radiance += throughput * sky;
            break;
        }

        Material mat = scene.materials[rec.material_id];
        radiance += throughput * mat.emission;

        // Russian Roulette
        if (depth > 2) {
            if (curand_uniform(rng) > rr_prob) break;
            throughput /= rr_prob;
        }

        // Material scattering
        if (mat.type == MaterialType::LAMBERTIAN) {
            Vec3 scatter_dir = random_cosine_hemisphere(rng, rec.normal);
            float cos_theta = fmaxf(0.0f, glm::dot(scatter_dir, rec.normal));
            throughput *= mat.albedo * cos_theta * 2.0f;
            ray = Ray(rec.point + rec.normal * EPSILON, scatter_dir);

        } else if (mat.type == MaterialType::SPECULAR) {
            Vec3 reflected = glm::reflect(ray.direction, rec.normal);
            if (mat.roughness > 0.0f) {
                Vec3 fuzz = mat.roughness * random_in_unit_sphere(rng);
                reflected = glm::normalize(reflected + fuzz);
            }
            if (glm::dot(reflected, rec.normal) <= 0.0f) break;
            throughput *= mat.albedo;
            ray = Ray(rec.point + rec.normal * EPSILON, reflected);

        } else if (mat.type == MaterialType::DIELECTRIC) {
            float etai_over_etat = rec.front_face ? (1.0f / mat.ior) : mat.ior;
            Vec3 unit_dir = glm::normalize(ray.direction);
            float cos_theta_i = fminf(glm::dot(-unit_dir, rec.normal), 1.0f);
            float sin_theta_i = sqrtf(1.0f - cos_theta_i * cos_theta_i);

            bool cannot_refract = etai_over_etat * sin_theta_i > 1.0f;

            float r0 = (1.0f - etai_over_etat) / (1.0f + etai_over_etat);
            r0 = r0 * r0;
            float reflectance = r0 + (1.0f - r0) * powf(1.0f - cos_theta_i, 5.0f);

            Vec3 direction;
            Vec3 offset;
            if (cannot_refract || reflectance > curand_uniform(rng)) {
                direction = glm::reflect(unit_dir, rec.normal);
                offset = rec.normal * EPSILON;
            } else {
                direction = glm::refract(unit_dir, rec.normal, etai_over_etat);
                offset = -rec.normal * EPSILON;
            }
            ray = Ray(rec.point + offset, direction);

        } else {
            break;
        }
    }

    return radiance;
}

__global__ void pathtracing_kernel(DeviceScene scene, DeviceBVH bvh, bool use_bvh,
                                   Camera cam, float* output,
                                   int width, int height, int spp, int max_depth,
                                   float rr_prob, curandState* rng_states) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    const DeviceBVH* bvh_ptr = use_bvh ? &bvh : nullptr;

    int pixel_idx = y * width + x;
    curandState local_rng = rng_states[pixel_idx];

    Vec3 color(0.0f);
    for (int s = 0; s < spp; ++s) {
        float u_off = curand_uniform(&local_rng);
        float v_off = curand_uniform(&local_rng);
        Ray ray = cam.generate_ray(x, y, u_off, v_off);
        color += trace_path_gpu(scene, bvh_ptr, ray, max_depth, rr_prob, &local_rng);
    }

    color /= float(spp);

    // Write back RNG state
    rng_states[pixel_idx] = local_rng;

    int out_idx = pixel_idx * 3;
    output[out_idx + 0] = color.r;
    output[out_idx + 1] = color.g;
    output[out_idx + 2] = color.b;
}

// --- GPURenderer implementation ---

GPURenderer::GPURenderer() {
    d_bvh.nodes = nullptr;
    d_bvh.num_nodes = 0;
    d_bvh.prim_refs = nullptr;
    d_bvh.num_prims = 0;
}

GPURenderer::~GPURenderer() {
    cleanup();
}

void GPURenderer::upload_bvh(const std::vector<BVHNode>& nodes,
                              const std::vector<PrimitiveRef>& prim_refs, int root) {
    if (!nodes.empty()) {
        CUDA_CHECK(cudaMalloc(&d_bvh_nodes, nodes.size() * sizeof(BVHNode)));
        CUDA_CHECK(cudaMemcpy(d_bvh_nodes, nodes.data(),
                              nodes.size() * sizeof(BVHNode), cudaMemcpyHostToDevice));
    }
    if (!prim_refs.empty()) {
        CUDA_CHECK(cudaMalloc(&d_bvh_prim_refs, prim_refs.size() * sizeof(PrimitiveRef)));
        CUDA_CHECK(cudaMemcpy(d_bvh_prim_refs, prim_refs.data(),
                              prim_refs.size() * sizeof(PrimitiveRef), cudaMemcpyHostToDevice));
    }
    d_bvh.nodes = d_bvh_nodes;
    d_bvh.num_nodes = static_cast<int>(nodes.size());
    d_bvh.prim_refs = d_bvh_prim_refs;
    d_bvh.num_prims = static_cast<int>(prim_refs.size());
    bvh_uploaded = true;
}

void GPURenderer::init_rng(int width, int height) {
    if (rng_initialized) return;
    int num_pixels = width * height;
    CUDA_CHECK(cudaMalloc(&d_rng_states, num_pixels * sizeof(curandState)));

    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    init_rng_kernel<<<grid, block>>>(static_cast<curandState*>(d_rng_states),
                                      width, height, 42ULL);
    CUDA_SYNC_CHECK();
    rng_initialized = true;
}

void GPURenderer::upload_scene(const Scene& scene) {
    // Spheres
    if (!scene.spheres.empty()) {
        CUDA_CHECK(cudaMalloc(&d_spheres, scene.spheres.size() * sizeof(Sphere)));
        CUDA_CHECK(cudaMemcpy(d_spheres, scene.spheres.data(),
                              scene.spheres.size() * sizeof(Sphere), cudaMemcpyHostToDevice));
    }

    // Triangles
    if (!scene.triangles.empty()) {
        CUDA_CHECK(cudaMalloc(&d_triangles, scene.triangles.size() * sizeof(Triangle)));
        CUDA_CHECK(cudaMemcpy(d_triangles, scene.triangles.data(),
                              scene.triangles.size() * sizeof(Triangle), cudaMemcpyHostToDevice));
    }

    // Materials
    CUDA_CHECK(cudaMalloc(&d_materials, scene.materials.size() * sizeof(Material)));
    CUDA_CHECK(cudaMemcpy(d_materials, scene.materials.data(),
                          scene.materials.size() * sizeof(Material), cudaMemcpyHostToDevice));

    // Lights
    if (!scene.lights.empty()) {
        CUDA_CHECK(cudaMalloc(&d_lights, scene.lights.size() * sizeof(PointLight)));
        CUDA_CHECK(cudaMemcpy(d_lights, scene.lights.data(),
                              scene.lights.size() * sizeof(PointLight), cudaMemcpyHostToDevice));
    }

    d_scene.spheres = d_spheres;
    d_scene.num_spheres = static_cast<int>(scene.spheres.size());
    d_scene.triangles = d_triangles;
    d_scene.num_triangles = static_cast<int>(scene.triangles.size());
    d_scene.materials = d_materials;
    d_scene.num_materials = static_cast<int>(scene.materials.size());
    d_scene.lights = d_lights;
    d_scene.num_lights = static_cast<int>(scene.lights.size());
}

void GPURenderer::render_phong(const Camera& cam, float* d_output,
                               const RenderConfig& config) {
    dim3 block(16, 16);
    dim3 grid((config.width + block.x - 1) / block.x,
              (config.height + block.y - 1) / block.y);

    phong_kernel<<<grid, block>>>(d_scene, d_bvh, bvh_uploaded,
                                   cam, d_output, config.width, config.height);
    CUDA_SYNC_CHECK();
}

void GPURenderer::render_pathtraced(const Camera& cam, float* d_output,
                                    const RenderConfig& config) {
    init_rng(config.width, config.height);

    dim3 block(16, 16);
    dim3 grid((config.width + block.x - 1) / block.x,
              (config.height + block.y - 1) / block.y);

    pathtracing_kernel<<<grid, block>>>(d_scene, d_bvh, bvh_uploaded,
                                         cam, d_output,
                                         config.width, config.height,
                                         config.samples_per_pixel, config.max_depth,
                                         config.russian_roulette_prob,
                                         static_cast<curandState*>(d_rng_states));
    CUDA_SYNC_CHECK();
}

void GPURenderer::download_framebuffer(float* d_output, Vec3* h_pixels, int num_pixels) {
    std::vector<float> buf(num_pixels * 3);
    CUDA_CHECK(cudaMemcpy(buf.data(), d_output, num_pixels * 3 * sizeof(float),
                          cudaMemcpyDeviceToHost));

    for (int i = 0; i < num_pixels; ++i) {
        h_pixels[i] = Vec3(buf[i * 3 + 0], buf[i * 3 + 1], buf[i * 3 + 2]);
    }
}

void GPURenderer::cleanup() {
    if (d_spheres) { cudaFree(d_spheres); d_spheres = nullptr; }
    if (d_triangles) { cudaFree(d_triangles); d_triangles = nullptr; }
    if (d_materials) { cudaFree(d_materials); d_materials = nullptr; }
    if (d_lights) { cudaFree(d_lights); d_lights = nullptr; }
    if (d_rng_states) { cudaFree(d_rng_states); d_rng_states = nullptr; }
    if (d_bvh_nodes) { cudaFree(d_bvh_nodes); d_bvh_nodes = nullptr; }
    if (d_bvh_prim_refs) { cudaFree(d_bvh_prim_refs); d_bvh_prim_refs = nullptr; }
    rng_initialized = false;
    bvh_uploaded = false;
}
