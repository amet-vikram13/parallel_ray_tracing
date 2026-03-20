#include "camera/camera.h"
#include <cmath>

void Camera::init(const Vec3& lookfrom, const Vec3& lookat, const Vec3& vup,
                  float vfov_deg, int w, int h) {
    width = w;
    height = h;
    position = lookfrom;

    float theta = vfov_deg * PI / 180.0f;
    float half_height = tanf(theta / 2.0f);
    float aspect = float(w) / float(h);
    float half_width = aspect * half_height;

    this->w = glm::normalize(lookfrom - lookat);
    this->u = glm::normalize(glm::cross(vup, this->w));
    this->v = glm::cross(this->w, this->u);

    lower_left = position - half_width * this->u - half_height * this->v - this->w;
    horizontal = 2.0f * half_width * this->u;
    vertical = 2.0f * half_height * this->v;
}
