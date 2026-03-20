#pragma once

#include "core/common.h"
#include "geometry/ray.h"
#include "geometry/hit_record.h"

enum class MaterialType : int {
    LAMBERTIAN = 0,
    SPECULAR = 1,
    DIELECTRIC = 2,
    PHONG = 3  // Phase 1 shading
};

struct Material {
    MaterialType type;
    Vec3 albedo;
    Vec3 emission;
    float roughness;    // for specular
    float ior;          // index of refraction for dielectric
    float shininess;    // for Phong

    HD Material()
        : type(MaterialType::LAMBERTIAN), albedo(0.8f), emission(0.0f),
          roughness(0.0f), ior(1.5f), shininess(32.0f) {}

    HD static Material lambertian(const Vec3& color) {
        Material m;
        m.type = MaterialType::LAMBERTIAN;
        m.albedo = color;
        return m;
    }

    HD static Material specular(const Vec3& color, float rough = 0.0f) {
        Material m;
        m.type = MaterialType::SPECULAR;
        m.albedo = color;
        m.roughness = rough;
        return m;
    }

    HD static Material dielectric(float ior_val) {
        Material m;
        m.type = MaterialType::DIELECTRIC;
        m.albedo = Vec3(1.0f);
        m.ior = ior_val;
        return m;
    }

    HD static Material phong(const Vec3& color, float shine = 32.0f) {
        Material m;
        m.type = MaterialType::PHONG;
        m.albedo = color;
        m.shininess = shine;
        return m;
    }

    HD static Material emissive(const Vec3& color, const Vec3& emit) {
        Material m;
        m.type = MaterialType::LAMBERTIAN;
        m.albedo = color;
        m.emission = emit;
        return m;
    }
};
