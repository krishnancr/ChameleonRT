#pragma once
#include "render_backend.h"
#include <slang-gfx.h>
#include <string>

class RenderSlang : public RenderBackend {
public:
    RenderSlang(gfx::IDevice* device);
    ~RenderSlang();

    std::string name() override;
    void initialize(const int fb_width, const int fb_height) override;
    void set_scene(const Scene& scene) override;
    RenderStats render(const glm::vec3& pos,
                      const glm::vec3& dir,
                      const glm::vec3& up,
                      float fovy,
                      bool camera_changed,
                      bool readback_framebuffer) override;
private:
    gfx::IDevice* m_device = nullptr;
};
