#pragma once
#include <SDL.h>
#include "display/display.h"
#include <slang-gfx.h>
#include <string>
#include <slang-com-ptr.h>
#include <vector>

#ifdef USE_VULKAN
#include <vulkan/vulkan.h>
#else  // DX12
#ifdef _WIN32
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>
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

    // Resource access methods for RenderSlang
    gfx::IDevice* getDevice() { return device.get(); }
    // REMOVED: getCurrentCommandBuffer() - create fresh each frame like triangle example
    gfx::IFramebuffer* getCurrentFramebuffer();
    gfx::IFramebufferLayout* getFramebufferLayout() { return framebufferLayout.get(); }
    uint32_t getCurrentFrameIndex() const { return m_currentFrameIndex; }

    Slang::ComPtr<gfx::IDevice> device;
    Slang::ComPtr<gfx::ICommandQueue> queue;
    Slang::ComPtr<gfx::ISwapchain> swapchain;
    Slang::ComPtr<gfx::IFramebufferLayout> framebufferLayout;
    std::vector<Slang::ComPtr<gfx::IFramebuffer>> framebuffers;
    std::vector<Slang::ComPtr<gfx::IResourceView>> renderTargetViews; // Store RTVs for clearing
    std::vector<Slang::ComPtr<gfx::ITransientResourceHeap>> transientHeaps;
    // REMOVED: commandBuffers storage - create fresh each frame like triangle example
    Slang::ComPtr<gfx::IRenderPassLayout> renderPassLayout; // Required for both Vulkan and D3D12

#ifndef USE_VULKAN
#ifdef _WIN32
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imgui_desc_heap; // D3D12 legacy - to be removed
#endif
#endif
    
private:
    SDL_Window* window; // Store the SDL window handle
    uint32_t m_currentFrameIndex = 0;  // Track current frame for resource access
    
    // Swapchain resize support following triangle example pattern
    void recreateSwapchainFramebuffers();
    
    // Unified framebuffer creation methods (refactored from duplicated code)
    void createSwapchainFramebuffers();
    void createSingleFramebuffer(uint32_t imageIndex, gfx::Format format, uint32_t width, uint32_t height);
    Slang::ComPtr<gfx::IResourceView> createRenderTargetView(gfx::ITextureResource* colorBuffer, gfx::Format format);
    Slang::ComPtr<gfx::IResourceView> createDepthStencilView(uint32_t width, uint32_t height);
    Slang::ComPtr<gfx::ITextureResource> createDepthBuffer(uint32_t width, uint32_t height);
    Slang::ComPtr<gfx::IFramebuffer> createFramebuffer(gfx::IResourceView* rtv, gfx::IResourceView* dsv);
    
    // Triangle rendering resources (Step 3: following triangle example pattern)
    struct TriangleRenderingData {
        Slang::ComPtr<gfx::IPipelineState> trianglePipeline;
        Slang::ComPtr<gfx::IBufferResource> triangleVertexBuffer;
        Slang::ComPtr<gfx::IBufferResource> triangleUniformBuffer;
        Slang::ComPtr<gfx::IInputLayout> triangleInputLayout;
        bool triangleInitialized = false;
    };
    
    TriangleRenderingData m_triangle;
    
    // Triangle rendering methods
    bool initTriangleRendering();
    void renderTriangle(gfx::IRenderCommandEncoder* renderEncoder);
    void cleanupTriangleRendering();
    
    // Shader validation methods (Phase S1.2: Real Slang Shader Compilation)
    struct ShaderValidationData {
        // Stage S1.2: Unified shader program using proper Slang API
        Slang::ComPtr<gfx::IShaderProgram> validationShaderProgram;
    };
    
    ShaderValidationData m_validation;
    
    void initShaderValidation();
    void cleanupShaderValidation();
    
    // Phase S1.3: Cross-platform Slang shader compilation
    bool loadSlangShader(gfx::DeviceType deviceType);
};
