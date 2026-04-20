#pragma once

#include "core/common.h"
#include "core/light.h"
#include "geometry/ray.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "material/material.h"
#include <string>
#include <vector>

// Selects the rendering dispatch used for a scene. GEOMETRY scenes are
// rasterized via the usual ray-vs-primitive intersection path (optionally
// accelerated by a BVH). OCEAN scenes bypass geometry entirely and are
// shaded per pixel via a fragment-shader-style callback.
enum class SceneMode : int {
    GEOMETRY = 0,
    OCEAN    = 1
};

struct Scene;

// Per-frame animation callback: updates scene state (e.g. camera, object
// positions) as a function of a scalar time parameter `t`.
using AnimateFn = void (*)(Scene& scene, float t);

// Fragment-shader callback (host side): returns HDR radiance for a primary
// ray at time `t`. Set only for fragment-shader scenes like `ocean`.
using ShadePixelFn = Vec3 (*)(const Ray& ray, float t);

struct Scene {
    std::string name;

    std::vector<Sphere>     spheres;
    std::vector<Triangle>   triangles;
    std::vector<Material>   materials;
    std::vector<PointLight> lights;

    // Scene-provided camera defaults. Renderer copies these into a Camera
    // at the start of each frame (so animate callbacks can pan the view).
    Vec3  cam_lookfrom = Vec3(0.0f, 0.0f, 0.0f);
    Vec3  cam_lookat   = Vec3(0.0f, 0.0f, -1.0f);
    Vec3  cam_vup      = Vec3(0.0f, 1.0f, 0.0f);
    float cam_vfov_deg = 60.0f;

    SceneMode        mode        = SceneMode::GEOMETRY;
    AnimateFn        animate     = nullptr;
    ShadePixelFn     shade_pixel = nullptr;

    int  add_material(const Material& mat);
    void add_sphere(const Vec3& center, float radius, int mat_id);
    void add_triangle(const Vec3& a, const Vec3& b, const Vec3& c, int mat_id);
    void add_light(const PointLight& light);
};

// GPU-friendly flattened scene data (SOA layout)
struct DeviceScene {
    Sphere*     spheres;
    int         num_spheres;
    Triangle*   triangles;
    int         num_triangles;
    Material*   materials;
    int         num_materials;
    PointLight* lights;
    int         num_lights;
};
