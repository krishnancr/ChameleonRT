#pragma once
#include <SDL.h>
#include "display/display.h"
#include <slang-gfx.h>
#include <string>
#include <slang-com-ptr.h>
#include <vector>
#ifdef _WIN32
#include <d3d12.h>
#endif

class SlangDisplay : public Display {
public:
    SlangDisplay(SDL_Window* window);
    ~SlangDisplay();

    std::string gpu_brand() override;
    std::string name() override;
    void resize(const int width, const int height) override;
    void new_frame() override;
    void display(RenderBackend* backend) override;

    Slang::ComPtr<gfx::IDevice> device;
    Slang::ComPtr<gfx::ICommandQueue> queue;
    Slang::ComPtr<gfx::ISwapchain> swapchain;
    Slang::ComPtr<gfx::IFramebufferLayout> framebufferLayout;
    std::vector<Slang::ComPtr<gfx::IFramebuffer>> framebuffers;
    std::vector<Slang::ComPtr<gfx::ITransientResourceHeap>> transientHeaps;
    std::vector<Slang::ComPtr<gfx::ICommandBuffer>> commandBuffers; // Added command buffer storage
    
private:
    SDL_Window* window; // Store the SDL window handle
};
