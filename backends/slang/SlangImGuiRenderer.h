#pragma once

#include <slang-com-ptr.h>
#include <slang-gfx.h>
#include <array>
#include <cstddef>

struct ImDrawData;

class SlangImGuiRenderer {
public:
    struct InitializeDesc {
        gfx::IDevice* device = nullptr;
        gfx::IFramebufferLayout* framebufferLayout = nullptr;
        gfx::IRenderPassLayout* renderPassLayout = nullptr;
    };

    SlangImGuiRenderer();
    ~SlangImGuiRenderer();

    bool initialize(const InitializeDesc& desc);
    void shutdown();
    void render(const ImDrawData* drawData, gfx::IRenderCommandEncoder* renderEncoder);

private:
    bool m_initialized = false;
    bool m_renderWarned = false;
    bool m_loggedDrawStats = false;
    bool m_loggedBindingLayout = false;
    bool m_loggedBindingResults = false;
    bool m_loggedVertexBounds = false;
    bool m_loggedVertexSamples = false;
    bool m_loggedCommandDetails = false;
    bool m_loggedProjectionSample = false;
    bool m_loggedLayoutInfo = false;
    bool m_loggedSourceIndexSamples = false;
    bool m_loggedProjectionMatrix = false;
    bool m_loggedProjectionLayout = false;

    Slang::ComPtr<gfx::IDevice> m_device;
    Slang::ComPtr<gfx::IFramebufferLayout> m_framebufferLayout;
    Slang::ComPtr<gfx::IRenderPassLayout> m_renderPassLayout;

    Slang::ComPtr<gfx::IShaderProgram> m_shaderProgram;
    Slang::ComPtr<gfx::IPipelineState> m_pipelineState;
    Slang::ComPtr<gfx::IInputLayout> m_inputLayout;

    Slang::ComPtr<gfx::IBufferResource> m_vertexBuffer;
    Slang::ComPtr<gfx::IBufferResource> m_indexBuffer;
    Slang::ComPtr<gfx::IBufferResource> m_constantBuffer;

    Slang::ComPtr<gfx::ITextureResource> m_fontTexture;
    Slang::ComPtr<gfx::IResourceView> m_fontTextureView;
    Slang::ComPtr<gfx::ISamplerState> m_fontSampler;

    std::array<float, 16> m_projectionMatrix{};
    std::array<float, 16> m_projectionMatrixGpu{};

    size_t m_vertexBufferSize = 0;
    size_t m_indexBufferSize = 0;
    size_t m_constantBufferSize = 0;

    bool createShaderProgram();
    bool createInputLayout();
    bool createPipelineState();
    bool ensureVertexBufferCapacity(size_t requiredBytes);
    bool ensureIndexBufferCapacity(size_t requiredBytes);
    bool uploadDrawData(const ImDrawData* drawData);
    bool initializeConstantBuffer();
    bool createFontResources();
    bool updateProjectionConstants(const ImDrawData* drawData);
};
