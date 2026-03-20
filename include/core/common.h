#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <cstdint>

#ifdef __CUDACC__
#define HD __host__ __device__
#define DEVICE __device__
#define GLOBAL __global__
#else
#define HD
#define DEVICE
#define GLOBAL
#endif

using Vec3 = glm::vec3;
using Vec2 = glm::vec2;
using Mat4 = glm::mat4;

constexpr float INF = std::numeric_limits<float>::max();
constexpr float EPSILON = 1e-6f;
constexpr float PI = glm::pi<float>();
constexpr float INV_PI = 1.0f / PI;
