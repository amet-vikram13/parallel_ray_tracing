#include "utils/image.h"
#include <fstream>
#include <algorithm>
#include <cmath>

Image::Image(int w, int h) : width(w), height(h), pixels(w * h, Vec3(0.0f)) {}

void Image::set_pixel(int x, int y, const Vec3& color) {
    if (x >= 0 && x < width && y >= 0 && y < height)
        pixels[y * width + x] = color;
}

Vec3 Image::get_pixel(int x, int y) const {
    if (x >= 0 && x < width && y >= 0 && y < height)
        return pixels[y * width + x];
    return Vec3(0.0f);
}

void Image::write_ppm(const std::string& filename, float gamma) const {
    std::ofstream out(filename, std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";

    float inv_gamma = 1.0f / gamma;
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            Vec3 c = pixels[y * width + x];
            // Tone map (clamp) and gamma correct
            auto to_byte = [inv_gamma](float v) -> uint8_t {
                v = std::clamp(v, 0.0f, 1.0f);
                return static_cast<uint8_t>(powf(v, inv_gamma) * 255.0f + 0.5f);
            };
            uint8_t r = to_byte(c.r);
            uint8_t g = to_byte(c.g);
            uint8_t b = to_byte(c.b);
            out.write(reinterpret_cast<char*>(&r), 1);
            out.write(reinterpret_cast<char*>(&g), 1);
            out.write(reinterpret_cast<char*>(&b), 1);
        }
    }
}

std::vector<uint8_t> Image::to_rgb8(float gamma) const {
    std::vector<uint8_t> buf(width * height * 3);
    float inv_gamma = 1.0f / gamma;
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            Vec3 c = pixels[y * width + x];
            int out_y = (height - 1 - y);
            int idx = (out_y * width + x) * 3;
            auto to_byte = [inv_gamma](float v) -> uint8_t {
                v = std::clamp(v, 0.0f, 1.0f);
                return static_cast<uint8_t>(powf(v, inv_gamma) * 255.0f + 0.5f);
            };
            buf[idx + 0] = to_byte(c.r);
            buf[idx + 1] = to_byte(c.g);
            buf[idx + 2] = to_byte(c.b);
        }
    }
    return buf;
}

void Image::clear(const Vec3& color) {
    std::fill(pixels.begin(), pixels.end(), color);
}
