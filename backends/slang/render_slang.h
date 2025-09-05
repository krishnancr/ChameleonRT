#pragma once

#include "render_backend.h"
#include <memory>
#include <vector>

class SlangDisplay;

class RenderSlang : public RenderBackend {
    SlangDisplay *display;
    
    // CPU fallback image
    std::vector<uint32_t> cpu_image;
    int fb_width = 0, fb_height = 0;

public:
    RenderSlang();
    ~RenderSlang();

    // RenderBackend implementation
    std::string name() override;
    void initialize(const int fb_width, const int fb_height) override;
    void set_scene(const Scene &scene) override;
    RenderStats render(const glm::vec3 &pos,
                      const glm::vec3 &dir,
                      const glm::vec3 &up,
                      const float fovy,
                      const bool camera_changed,
                      const bool readback_framebuffer) override;
    
    void set_display(SlangDisplay* disp) { display = disp; }
};
