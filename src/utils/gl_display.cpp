#include "utils/gl_display.h"
#include <cstdio>
#include <cstdlib>

GLDisplay::GLDisplay(int w, int h, const char* title) : width(w), height(h) {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        exit(1);
    }

    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        exit(1);
    }

    glfwMakeContextCurrent(window);

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

GLDisplay::~GLDisplay() {
    glDeleteTextures(1, &texture_id);
    glfwDestroyWindow(window);
    glfwTerminate();
}

bool GLDisplay::should_close() const {
    return glfwWindowShouldClose(window);
}

void GLDisplay::poll_events() {
    glfwPollEvents();
}

void GLDisplay::update_texture(const std::vector<uint8_t>& rgb_data) {
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, rgb_data.data());
}

void GLDisplay::render() {
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    // Full-screen quad
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f( 1, -1);
    glTexCoord2f(1, 1); glVertex2f( 1,  1);
    glTexCoord2f(0, 1); glVertex2f(-1,  1);
    glEnd();

    glfwSwapBuffers(window);
}
