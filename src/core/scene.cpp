#include "core/scene.h"

int Scene::add_material(const Material& mat) {
    materials.push_back(mat);
    return static_cast<int>(materials.size()) - 1;
}

void Scene::add_sphere(const Vec3& center, float radius, int mat_id) {
    spheres.emplace_back(center, radius, mat_id);
}

void Scene::add_triangle(const Vec3& a, const Vec3& b, const Vec3& c, int mat_id) {
    triangles.emplace_back(a, b, c, mat_id);
}

void Scene::add_light(const PointLight& light) {
    lights.push_back(light);
}

Scene Scene::create_test_scene() {
    Scene scene;

    int mat_ground = scene.add_material(Material::lambertian(Vec3(0.5f, 0.5f, 0.5f)));
    int mat_center = scene.add_material(Material::lambertian(Vec3(0.7f, 0.3f, 0.3f)));
    int mat_left   = scene.add_material(Material::specular(Vec3(0.8f, 0.8f, 0.8f)));
    int mat_right  = scene.add_material(Material::dielectric(1.5f));

    // Ground
    scene.add_sphere(Vec3(0.0f, -100.5f, -1.0f), 100.0f, mat_ground);
    // Center
    scene.add_sphere(Vec3(0.0f, 0.0f, -1.0f), 0.5f, mat_center);
    // Left
    scene.add_sphere(Vec3(-1.0f, 0.0f, -1.0f), 0.5f, mat_left);
    // Right
    scene.add_sphere(Vec3(1.0f, 0.0f, -1.0f), 0.5f, mat_right);

    scene.add_light(PointLight(Vec3(0.0f, 5.0f, 0.0f), Vec3(1.0f), 1.0f));

    return scene;
}

Scene Scene::create_cornell_box() {
    Scene scene;

    // Materials
    int mat_white = scene.add_material(Material::lambertian(Vec3(0.73f)));
    int mat_red   = scene.add_material(Material::lambertian(Vec3(0.65f, 0.05f, 0.05f)));
    int mat_green = scene.add_material(Material::lambertian(Vec3(0.12f, 0.45f, 0.15f)));
    int mat_light = scene.add_material(Material::emissive(Vec3(1.0f), Vec3(15.0f)));
    int mat_mirror = scene.add_material(Material::specular(Vec3(0.99f)));
    int mat_glass  = scene.add_material(Material::dielectric(1.5f));

    // Cornell box walls as triangle pairs (quads)
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

    // Ceiling light
    scene.add_triangle(Vec3(213, 554, 227), Vec3(343, 554, 227), Vec3(343, 554, 332), mat_light);
    scene.add_triangle(Vec3(213, 554, 227), Vec3(343, 554, 332), Vec3(213, 554, 332), mat_light);

    // Two spheres inside the box
    scene.add_sphere(Vec3(185, 90, 190), 90.0f, mat_mirror);
    scene.add_sphere(Vec3(370, 90, 350), 90.0f, mat_glass);

    scene.add_light(PointLight(Vec3(278, 540, 280), Vec3(1.0f), 20.0f));

    return scene;
}
