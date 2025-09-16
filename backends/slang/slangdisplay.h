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

    // Triangle Example Auto-Clear Support (eliminates encodeResourceCommands)
    float m_clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool m_shouldClearOnNextRender = false;

#ifndef USE_VULKAN
#ifdef _WIN32
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imgui_desc_heap; // D3D12 legacy - to be removed
#endif
#endif
    
private:
    SDL_Window* window; // Store the SDL window handle
    uint32_t m_currentFrameIndex = 0;  // Track current frame for resource access
    
    // Unified ImGui rendering method that works with both Vulkan and D3D12
    void renderImGuiDrawData(gfx::ICommandBuffer* commandBuffer, gfx::IFramebuffer* framebuffer);
    
    // Helper method for Stage 0 - simple screen clearing
    void clearScreenToColor(gfx::ICommandBuffer* commandBuffer, float r, float g, float b, float a);
    
    // Swapchain resize support following triangle example pattern
    void recreateSwapchainFramebuffers();
    
    // Triangle rendering resources (Step 3: following triangle example pattern)
    struct TriangleRenderingData {
        Slang::ComPtr<gfx::IShaderProgram> triangleShaderProgram;
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
    
    // NEW: Triangle example pattern renderFrame
    void renderFrameTriangleStyle(int frameBufferIndex);
    
    // Helper for triangle example compatibility
    bool isTestMode();
    
    // Shader validation methods (Phase S1.2: Real Slang Shader Compilation)
    struct ShaderValidationData {
        // Stage S1.2: Unified shader program using proper Slang API
        Slang::ComPtr<gfx::IShaderProgram> validationShaderProgram;
        Slang::ComPtr<gfx::IPipelineState> validationPipeline;
        Slang::ComPtr<gfx::IBufferResource> uniformBuffer;
        
        // Legacy resources (Stage 0 compatibility)
        Slang::ComPtr<gfx::IShaderProgram> rainbowShader;
        Slang::ComPtr<gfx::IShaderProgram> pulseShader;
        Slang::ComPtr<gfx::IPipelineState> rainbowPipeline;
        Slang::ComPtr<gfx::IPipelineState> pulsePipeline;
        Slang::ComPtr<gfx::IBufferResource> timeBuffer;
        Slang::ComPtr<gfx::IResourceView> timeBufferView;
        
        // Timing and state
        double startTime;
        int currentShader; // 0 = rainbow, 1 = pulse, 2 = wave
        bool validationActive;
        bool shaderModeActive; // true = use shaders, false = use clearing (fallback)
    };
    
    ShaderValidationData m_validation;
    
    void initShaderValidation();
    void runShaderValidation();
    void cleanupShaderValidation();
    void updateTimeBuffer(float deltaTime);
    
    // Phase S1.3: Cross-platform Slang shader compilation
    bool loadSlangShader(gfx::DeviceType deviceType);
    bool loadSlangShaderProgram();
    bool createValidationPipeline();
    void renderWithSlangShaders(gfx::ICommandBuffer* commandBuffer);
    std::string loadShaderFile(const char* filename);
};
