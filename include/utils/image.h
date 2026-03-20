#pragma once

#include "core/common.h"
#include <vector>
#include <string>

class Image {
public:
    int width, height;
    std::vector<Vec3> pixels; // HDR float buffer

    Image(int w, int h);

    void set_pixel(int x, int y, const Vec3& color);
    Vec3 get_pixel(int x, int y) const;

    // Gamma correct and write to PPM
    void write_ppm(const std::string& filename, float gamma = 2.2f) const;

    // Get raw byte buffer (for OpenGL texture upload)
    std::vector<uint8_t> to_rgb8(float gamma = 2.2f) const;

    void clear(const Vec3& color = Vec3(0.0f));
};
