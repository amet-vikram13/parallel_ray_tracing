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
