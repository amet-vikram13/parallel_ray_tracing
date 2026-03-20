#include "renderer/cpu_renderer.h"
#include <omp.h>
#include <cmath>
#include <algorithm>

// Phase 3: Set BVH data for accelerated traversal
void CPURenderer::set_bvh(const std::vector<BVHNode>& nodes,
                           const std::vector<PrimitiveRef>& prim_refs, int root) {
    bvh_nodes = nodes.data();
    bvh_prim_refs = prim_refs.data();
    bvh_root = root;
    use_bvh = true;
}

// Phase 3: Iterative stack-based BVH traversal
bool CPURenderer::trace_bvh(const Scene& scene, const Ray& ray,
                             float t_min, float t_max, HitRecord& rec) const {
    constexpr int STACK_SIZE = 64;
    int stack[STACK_SIZE];
    int stack_ptr = 0;
    stack[stack_ptr++] = bvh_root;

    bool hit = false;
    float closest = t_max;

    while (stack_ptr > 0) {
        int node_idx = stack[--stack_ptr];
        const BVHNode& node = bvh_nodes[node_idx];

        if (!node.bounds.intersect(ray, t_min, closest))
            continue;

        if (node.is_leaf()) {
            // Test all primitives in this leaf
            for (int i = 0; i < node.prim_count; ++i) {
                int prim_global_idx = node.prim_idx;
                // For SAH-BVH, prim_idx is an index into prim_refs
                // For LBVH, prim_idx is the global primitive index
                HitRecord tmp;
                bool prim_hit = false;

                if (bvh_prim_refs != nullptr) {
                    const PrimitiveRef& ref = bvh_prim_refs[prim_global_idx + i];
                    if (ref.type == PRIM_SPHERE) {
                        prim_hit = scene.spheres[ref.index].intersect(ray, t_min, closest, tmp);
                    } else {
                        prim_hit = scene.triangles[ref.index].intersect(ray, t_min, closest, tmp);
                    }
                } else {
                    // Direct global index (LBVH path)
                    int num_spheres = static_cast<int>(scene.spheres.size());
                    if (prim_global_idx < num_spheres) {
                        prim_hit = scene.spheres[prim_global_idx].intersect(ray, t_min, closest, tmp);
                    } else {
                        int tri_idx = prim_global_idx - num_spheres;
                        prim_hit = scene.triangles[tri_idx].intersect(ray, t_min, closest, tmp);
                    }
                }

                if (prim_hit) {
                    hit = true;
                    closest = tmp.t;
                    rec = tmp;
                }
            }
        } else {
            // Push children (push far child first so near is processed first)
            if (node.left >= 0) stack[stack_ptr++] = node.left;
            if (node.right >= 0) stack[stack_ptr++] = node.right;
        }
    }

    return hit;
}

// PCG-style hash RNG
float CPURenderer::random_float(unsigned int& seed) const {
    seed = seed * 747796405u + 2891336453u;
    unsigned int result = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    result = (result >> 22u) ^ result;
    return static_cast<float>(result) / 4294967295.0f;
}

Vec3 CPURenderer::random_hemisphere(const Vec3& normal, unsigned int& seed) const {
    float r1 = random_float(seed);
    float r2 = random_float(seed);
    float phi = 2.0f * PI * r1;
    float cos_theta = sqrtf(1.0f - r2);
    float sin_theta = sqrtf(r2);

    // Build ONB from normal
    Vec3 w = normal;
    Vec3 a = (fabsf(w.x) > 0.9f) ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 u = glm::normalize(glm::cross(a, w));
    Vec3 v = glm::cross(w, u);

    return glm::normalize(u * cosf(phi) * sin_theta + v * sinf(phi) * sin_theta + w * cos_theta);
}

bool CPURenderer::trace(const Scene& scene, const Ray& ray, float t_min, float t_max,
                        HitRecord& rec) const {
    // Use BVH-accelerated traversal if available
    if (use_bvh) {
        return trace_bvh(scene, ray, t_min, t_max, rec);
    }

    // Brute-force: test all primitives
    bool hit = false;
    float closest = t_max;

    for (const auto& sphere : scene.spheres) {
        HitRecord tmp;
        if (sphere.intersect(ray, t_min, closest, tmp)) {
            hit = true;
            closest = tmp.t;
            rec = tmp;
        }
    }

    for (const auto& tri : scene.triangles) {
        HitRecord tmp;
        if (tri.intersect(ray, t_min, closest, tmp)) {
            hit = true;
            closest = tmp.t;
            rec = tmp;
        }
    }

    return hit;
}

// Phase 1: Phong shading with hard shadows
Vec3 CPURenderer::shade_phong(const Scene& scene, const Ray& ray,
                              const HitRecord& rec) const {
    const Material& mat = scene.materials[rec.material_id];
    Vec3 color(0.0f);
    Vec3 ambient = 0.1f * mat.albedo;
    color += ambient;

    for (const auto& light : scene.lights) {
        Vec3 light_dir = glm::normalize(light.position - rec.point);
        float light_dist = glm::length(light.position - rec.point);

        // Shadow ray
        Ray shadow_ray(rec.point + rec.normal * EPSILON, light_dir);
        HitRecord shadow_hit;
        if (trace(scene, shadow_ray, EPSILON, light_dist, shadow_hit))
            continue; // in shadow

        // Diffuse
        float diff = std::max(0.0f, glm::dot(rec.normal, light_dir));
        color += diff * mat.albedo * light.color * light.intensity;

        // Specular (Blinn-Phong)
        Vec3 view_dir = glm::normalize(-ray.direction);
        Vec3 half_dir = glm::normalize(light_dir + view_dir);
        float spec = powf(std::max(0.0f, glm::dot(rec.normal, half_dir)), mat.shininess);
        color += spec * light.color * light.intensity;
    }

    return color;
}

void CPURenderer::render_phong(const Scene& scene, const Camera& cam,
                               Image& output, const RenderConfig& config) {
    #pragma omp parallel for schedule(dynamic, 16) collapse(2)
    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            Ray ray = cam.generate_ray(x, y);
            HitRecord rec;
            Vec3 color(0.0f);

            if (trace(scene, ray, EPSILON, INF, rec)) {
                color = shade_phong(scene, ray, rec);
            } else {
                // Sky gradient
                float t = 0.5f * (ray.direction.y + 1.0f);
                color = (1.0f - t) * Vec3(1.0f) + t * Vec3(0.5f, 0.7f, 1.0f);
            }

            output.set_pixel(x, y, color);
        }
    }
}

// Phase 2: Recursive path tracing with Russian Roulette
Vec3 CPURenderer::trace_path(const Scene& scene, const Ray& ray, int depth,
                             const RenderConfig& config, unsigned int& seed) const {
    if (depth <= 0) return Vec3(0.0f);

    HitRecord rec;
    if (!trace(scene, ray, EPSILON, INF, rec)) {
        // Sky
        float t = 0.5f * (ray.direction.y + 1.0f);
        return (1.0f - t) * Vec3(1.0f) + t * Vec3(0.5f, 0.7f, 1.0f);
    }

    const Material& mat = scene.materials[rec.material_id];

    // Emission
    Vec3 emitted = mat.emission;

    // Russian Roulette termination
    if (depth < config.max_depth - 2) {
        if (random_float(seed) > config.russian_roulette_prob)
            return emitted;
    }

    float rr_factor = (depth < config.max_depth - 2) ? (1.0f / config.russian_roulette_prob) : 1.0f;

    switch (mat.type) {
        case MaterialType::LAMBERTIAN: {
            Vec3 scatter_dir = random_hemisphere(rec.normal, seed);
            Ray scattered(rec.point + rec.normal * EPSILON, scatter_dir);
            float cos_theta = std::max(0.0f, glm::dot(scatter_dir, rec.normal));
            Vec3 incoming = trace_path(scene, scattered, depth - 1, config, seed);
            return emitted + mat.albedo * incoming * cos_theta * 2.0f * rr_factor;
        }

        case MaterialType::SPECULAR: {
            Vec3 reflected = glm::reflect(ray.direction, rec.normal);
            // Add roughness perturbation
            if (mat.roughness > 0.0f) {
                Vec3 fuzz = mat.roughness * random_hemisphere(rec.normal, seed);
                reflected = glm::normalize(reflected + fuzz);
            }
            if (glm::dot(reflected, rec.normal) <= 0.0f) return emitted;
            Ray scattered(rec.point + rec.normal * EPSILON, reflected);
            Vec3 incoming = trace_path(scene, scattered, depth - 1, config, seed);
            return emitted + mat.albedo * incoming * rr_factor;
        }

        case MaterialType::DIELECTRIC: {
            float etai_over_etat = rec.front_face ? (1.0f / mat.ior) : mat.ior;
            Vec3 unit_dir = glm::normalize(ray.direction);
            float cos_theta_i = std::min(glm::dot(-unit_dir, rec.normal), 1.0f);
            float sin_theta_i = sqrtf(1.0f - cos_theta_i * cos_theta_i);

            bool cannot_refract = etai_over_etat * sin_theta_i > 1.0f;

            // Schlick approximation
            float r0 = (1.0f - etai_over_etat) / (1.0f + etai_over_etat);
            r0 = r0 * r0;
            float reflectance = r0 + (1.0f - r0) * powf(1.0f - cos_theta_i, 5.0f);

            Vec3 direction;
            Vec3 offset;
            if (cannot_refract || reflectance > random_float(seed)) {
                direction = glm::reflect(unit_dir, rec.normal);
                offset = rec.normal * EPSILON;
            } else {
                direction = glm::refract(unit_dir, rec.normal, etai_over_etat);
                offset = -rec.normal * EPSILON;
            }

            Ray scattered(rec.point + offset, direction);
            Vec3 incoming = trace_path(scene, scattered, depth - 1, config, seed);
            return emitted + incoming * rr_factor;
        }

        default:
            return emitted;
    }
}

void CPURenderer::render_pathtraced(const Scene& scene, const Camera& cam,
                                    Image& output, const RenderConfig& config) {
    #pragma omp parallel for schedule(dynamic, 16) collapse(2)
    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            unsigned int seed = static_cast<unsigned int>(y * config.width + x + 42);
            Vec3 color(0.0f);

            for (int s = 0; s < config.samples_per_pixel; ++s) {
                float u_off = random_float(seed);
                float v_off = random_float(seed);
                Ray ray = cam.generate_ray(x, y, u_off, v_off);
                color += trace_path(scene, ray, config.max_depth, config, seed);
            }

            color /= static_cast<float>(config.samples_per_pixel);
            output.set_pixel(x, y, color);
        }
    }
}
