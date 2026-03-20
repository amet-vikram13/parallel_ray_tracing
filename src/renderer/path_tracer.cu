#include "camera/camera.h"
#include "core/scene.h"
#include "renderer/render_config.h"
#include "renderer/stream_compaction.h"
#include "utils/cuda_rng.h"
#include "utils/cuda_utils.h"

#include "geometry/bvh_node.h"

extern __device__ bool trace_scene(const DeviceScene &scene, const DeviceBVH *bvh,
                                   const Ray &ray, float t_min, float t_max,
                                   HitRecord &rec);

// Generate initial rays for all pixels
__global__ void generate_rays_kernel(RayState *rays, Camera cam, int width,
                                     int height, int sample_idx,
                                     curandState *rng_states) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || y >= height)
    return;

  int idx = y * width + x;
  curandState rng = rng_states[idx];

  float u = curand_uniform(&rng);
  float v = curand_uniform(&rng);

  rays[idx].ray = cam.generate_ray(x, y, u, v);
  rays[idx].throughput = Vec3(1.0f);
  rays[idx].radiance = Vec3(0.0f);
  rays[idx].pixel_idx = idx;
  rays[idx].active = true;

  rng_states[idx] = rng;
}

// Single bounce kernel — intersect and scatter
__global__ void bounce_kernel(RayState *rays, int num_rays, DeviceScene scene,
                              int bounce, float rr_prob,
                              curandState *rng_states) {
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= num_rays || !rays[idx].active)
    return;

  RayState &rs = rays[idx];
  curandState rng = rng_states[rs.pixel_idx];
  HitRecord rec;

  if (!trace_scene(scene, nullptr, rs.ray, EPSILON, INF, rec)) {
    // Miss — accumulate sky color
    float t = 0.5f * (rs.ray.direction.y + 1.0f);
    Vec3 sky = (1.0f - t) * Vec3(1.0f) + t * Vec3(0.5f, 0.7f, 1.0f);
    rs.radiance += rs.throughput * sky;
    rs.active = false;
    rng_states[rs.pixel_idx] = rng;
    return;
  }

  Material mat = scene.materials[rec.material_id];
  rs.radiance += rs.throughput * mat.emission;

  // Russian Roulette
  if (bounce > 2) {
    if (curand_uniform(&rng) > rr_prob) {
      rs.active = false;
      rng_states[rs.pixel_idx] = rng;
      return;
    }
    rs.throughput /= rr_prob;
  }

  // Scatter based on material
  if (mat.type == MaterialType::LAMBERTIAN) {
    Vec3 dir = random_cosine_hemisphere(&rng, rec.normal);
    float cos_theta = fmaxf(0.0f, glm::dot(dir, rec.normal));
    rs.throughput *= mat.albedo * cos_theta * 2.0f;
    rs.ray = Ray(rec.point + rec.normal * EPSILON, dir);

  } else if (mat.type == MaterialType::SPECULAR) {
    Vec3 refl = glm::reflect(rs.ray.direction, rec.normal);
    if (mat.roughness > 0.0f)
      refl = glm::normalize(refl + mat.roughness * random_in_unit_sphere(&rng));
    if (glm::dot(refl, rec.normal) <= 0.0f) {
      rs.active = false;
    } else {
      rs.throughput *= mat.albedo;
      rs.ray = Ray(rec.point + rec.normal * EPSILON, refl);
    }

  } else if (mat.type == MaterialType::DIELECTRIC) {
    float ratio = rec.front_face ? (1.0f / mat.ior) : mat.ior;
    Vec3 udir = glm::normalize(rs.ray.direction);
    float ct = fminf(glm::dot(-udir, rec.normal), 1.0f);
    float st = sqrtf(1.0f - ct * ct);
    bool total_reflect = ratio * st > 1.0f;
    float r0 = (1.0f - ratio) / (1.0f + ratio);
    r0 *= r0;
    float refl_prob = r0 + (1.0f - r0) * powf(1.0f - ct, 5.0f);

    Vec3 dir, off;
    if (total_reflect || refl_prob > curand_uniform(&rng)) {
      dir = glm::reflect(udir, rec.normal);
      off = rec.normal * EPSILON;
    } else {
      dir = glm::refract(udir, rec.normal, ratio);
      off = -rec.normal * EPSILON;
    }
    rs.ray = Ray(rec.point + off, dir);

  } else {
    rs.active = false;
  }

  rng_states[rs.pixel_idx] = rng;
}

// Note: trace_scene_device is defined in gpu_renderer.cu as __device__
// This file would need to be compiled together or use a shared device function
// header. For modularity, the bounce_kernel references it via extern or
// link-time resolution.
