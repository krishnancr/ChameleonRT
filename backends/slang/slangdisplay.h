#pragma once
#include <SDL.h>
#include "display/display.h"
#include <slang-gfx.h>
#include <string>
#include <slang-com-ptr.h>
#include <vector>

#ifdef USE_VULKAN
#include <vulkan/vulkan.h>
#include "../vulkan/imgui_impl_vulkan.h"
#else  // DX12
#ifdef _WIN32
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>
#include "../dxr/imgui_impl_dx12.h"
#endif
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

#ifdef USE_VULKAN
    Slang::ComPtr<gfx::IRenderPassLayout> renderPassLayout; // Required for proper Vulkan command buffer encoding
#else  // DX12
#ifdef _WIN32
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imgui_desc_heap;
#endif
#endif
    
private:
    SDL_Window* window; // Store the SDL window handle
};
