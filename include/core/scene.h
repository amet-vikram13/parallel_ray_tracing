#pragma once

#include "core/common.h"
#include "core/light.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "material/material.h"
#include <vector>

struct Scene {
  std::vector<Sphere> spheres;
  std::vector<Triangle> triangles;
  std::vector<Material> materials;
  std::vector<PointLight> lights;

  int add_material(const Material &mat);
  void add_sphere(const Vec3 &center, float radius, int mat_id);
  void add_triangle(const Vec3 &a, const Vec3 &b, const Vec3 &c, int mat_id);
  void add_light(const PointLight &light);

  // Build a Cornell Box scene
  static Scene create_cornell_box();

  // Simple sphere scene for testing
  static Scene create_test_scene();
};

// GPU-friendly flattened scene data (SOA layout)
struct DeviceScene {
  Sphere *spheres;
  int num_spheres;
  Triangle *triangles;
  int num_triangles;
  Material *materials;
  int num_materials;
  PointLight *lights;
  int num_lights;
};
