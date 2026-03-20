#pragma once

#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <cstdint>
#include <vector>

class GLDisplay {
public:
    GLDisplay(int width, int height, const char* title);
    ~GLDisplay();

    bool should_close() const;
    void poll_events();
    void update_texture(const std::vector<uint8_t>& rgb_data);
    void render();

private:
    GLFWwindow* window;
    GLuint texture_id;
    int width, height;
};
