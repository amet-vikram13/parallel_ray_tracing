// -----------------------------------------------------------------------------
// Scene label:    cornell_box
// Visual features: Standard Cornell Box with red/green side walls, white
//                  floor/ceiling/back, area light on the ceiling, and one
//                  mirror sphere plus one glass sphere inside the box.
// Expected time:   ~8–15 s on GPU @ 512×512, 64 spp (path-traced).
// Limitations:     Uses a single ceiling area light (no multiple light
//                  sampling). This is a direct port of the original
//                  Scene::create_cornell_box() factory.
// -----------------------------------------------------------------------------

#include "scenes/scene_registry.h"

void build_scene_cornell_box(Scene& scene, const RenderConfig& /*cfg*/) {
    const int mat_white  = scene.add_material(Material::lambertian(Vec3(0.73f)));
    const int mat_red    = scene.add_material(Material::lambertian(Vec3(0.65f, 0.05f, 0.05f)));
    const int mat_green  = scene.add_material(Material::lambertian(Vec3(0.12f, 0.45f, 0.15f)));
    const int mat_light  = scene.add_material(Material::emissive(Vec3(1.0f), Vec3(15.0f)));
    const int mat_mirror = scene.add_material(Material::specular(Vec3(0.99f)));
    const int mat_glass  = scene.add_material(Material::dielectric(1.5f));

    // Floor (white)
    scene.add_triangle(Vec3(0, 0, 0), Vec3(555, 0, 0), Vec3(555, 0, 555), mat_white);
    scene.add_triangle(Vec3(0, 0, 0), Vec3(555, 0, 555), Vec3(0, 0, 555), mat_white);

    // Ceiling (white)
    scene.add_triangle(Vec3(0, 555, 0), Vec3(0, 555, 555), Vec3(555, 555, 555), mat_white);
    scene.add_triangle(Vec3(0, 555, 0), Vec3(555, 555, 555), Vec3(555, 555, 0), mat_white);

    // Back wall (white)
    scene.add_triangle(Vec3(0, 0, 555), Vec3(555, 0, 555), Vec3(555, 555, 555), mat_white);
    scene.add_triangle(Vec3(0, 0, 555), Vec3(555, 555, 555), Vec3(0, 555, 555), mat_white);

    // Left wall (red)
    scene.add_triangle(Vec3(0, 0, 0), Vec3(0, 0, 555), Vec3(0, 555, 555), mat_red);
    scene.add_triangle(Vec3(0, 0, 0), Vec3(0, 555, 555), Vec3(0, 555, 0), mat_red);

    // Right wall (green)
    scene.add_triangle(Vec3(555, 0, 0), Vec3(555, 555, 0), Vec3(555, 555, 555), mat_green);
    scene.add_triangle(Vec3(555, 0, 0), Vec3(555, 555, 555), Vec3(555, 0, 555), mat_green);

    // Ceiling area light
    scene.add_triangle(Vec3(213, 554, 227), Vec3(343, 554, 227), Vec3(343, 554, 332), mat_light);
    scene.add_triangle(Vec3(213, 554, 227), Vec3(343, 554, 332), Vec3(213, 554, 332), mat_light);

    // Two spheres
    scene.add_sphere(Vec3(185, 90, 190), 90.0f, mat_mirror);
    scene.add_sphere(Vec3(370, 90, 350), 90.0f, mat_glass);

    scene.add_light(PointLight(Vec3(278, 540, 280), Vec3(1.0f), 20.0f));

    scene.cam_lookfrom = Vec3(278, 278, -800);
    scene.cam_lookat   = Vec3(278, 278, 0);
    scene.cam_vup      = Vec3(0, 1, 0);
    scene.cam_vfov_deg = 40.0f;
    scene.mode         = SceneMode::GEOMETRY;
}
