// -----------------------------------------------------------------------------
// Scene label:    spheres
// Visual features: Four spheres (ground + Lambertian + specular + dielectric)
//                  over a soft sky gradient. Useful as a quick sanity-check
//                  scene for material shading.
// Expected time:   ~1–3 s on GPU @ 512×512, 64 spp (path-traced).
// Limitations:     Simple ground sphere approximates an infinite plane; no
//                  area lighting. Direct port of Scene::create_test_scene().
// -----------------------------------------------------------------------------

#include "scenes/scene_registry.h"

void build_scene_spheres(Scene& scene, const RenderConfig& /*cfg*/) {
    const int mat_ground = scene.add_material(Material::lambertian(Vec3(0.5f, 0.5f, 0.5f)));
    const int mat_center = scene.add_material(Material::lambertian(Vec3(0.7f, 0.3f, 0.3f)));
    const int mat_left   = scene.add_material(Material::specular(Vec3(0.8f, 0.8f, 0.8f)));
    const int mat_right  = scene.add_material(Material::dielectric(1.5f));

    scene.add_sphere(Vec3( 0.0f, -100.5f, -1.0f), 100.0f, mat_ground);
    scene.add_sphere(Vec3( 0.0f,    0.0f, -1.0f),   0.5f, mat_center);
    scene.add_sphere(Vec3(-1.0f,    0.0f, -1.0f),   0.5f, mat_left);
    scene.add_sphere(Vec3( 1.0f,    0.0f, -1.0f),   0.5f, mat_right);

    scene.add_light(PointLight(Vec3(0.0f, 5.0f, 0.0f), Vec3(1.0f), 1.0f));

    scene.cam_lookfrom = Vec3(0, 0, 0);
    scene.cam_lookat   = Vec3(0, 0, -1);
    scene.cam_vup      = Vec3(0, 1, 0);
    scene.cam_vfov_deg = 90.0f;
    scene.mode         = SceneMode::GEOMETRY;
}
