#include "render_slang.h"
#include "slangdisplay.h"
#include <iostream>
#include <cstring>

RenderSlang::RenderSlang() : display(nullptr) {
}

RenderSlang::~RenderSlang() {
    cpu_image.clear();
    fb_width = fb_height = 0;
}

std::string RenderSlang::name() {
    return "Slang";
}

void RenderSlang::initialize(const int fb_width, const int fb_height) {
    this->fb_width = fb_width;
    this->fb_height = fb_height;
    
    // Resize the base class img buffer
    img.resize(fb_width * fb_height);
    
    // Fill with red color for testing
    uint32_t red_color = 0xFF0000FF; // RGBA: Red = FF, Green = 00, Blue = 00, Alpha = FF
    for (auto& pixel : img) {
        pixel = red_color;
    }
}

void RenderSlang::set_scene(const Scene &scene) {
    // Scene data will be used when ray tracing is implemented
}

RenderStats RenderSlang::render(const glm::vec3 &pos,
                               const glm::vec3 &dir,
                               const glm::vec3 &up,
                               const float fovy,
                               const bool camera_changed,
                               const bool readback_framebuffer) {
    // Fill the base class img buffer with red for testing
    if (readback_framebuffer && img.size() == fb_width * fb_height) {
        uint32_t red_color = 0xFF0000FF;
        for (auto& pixel : img) {
            pixel = red_color;
        }
    }
    
    // Return basic stats for a flat red image
    RenderStats stats;
    stats.render_time = 1.0f; // 1ms fake render time
    stats.rays_per_second = 1000000; // 1M fake rays/sec
    
    return stats;
}
