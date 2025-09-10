# Plan A: Unified Slang GFX Backend Implementation

This plan implements ImGui rendering entirely through Slang GFX abstraction, providing a single codebase that works across Vulkan, D3D12, and future APIs.

## Overview

**Approach**: Pure Slang GFX implementation
**Benefits**: 
- Single codebase for all graphics APIs
- Architectural consistency with SlangDisplay
- Future-proof for new APIs (Metal, WebGPU)
- Educational value in graphics pipeline implementation

**Architecture**: Custom ImGui renderer using Slang GFX resources and command encoding

---

## Stage 0: Cleanup and Stabilization (Foundation) ✅ COMPLETED

**Objective**: Clean up SlangDisplay to have a stable, non-crashing foundation that displays a simple colored window before adding ImGui complexity.

**Implementation Status**: ✅ **COMPLETED**
**Commit SHA**: `41cdca57c90bf9161ee10b466fd109afac9f07ed`
**Completion Date**: `September 10, 2025`

**Tasks**:
1. ✅ Remove all scene rendering calls and backend->render() dependencies
2. ✅ Fix the render pass layout crash in encodeRenderCommands
3. ✅ Implement simple screen clearing to a solid color
4. ✅ Verify stable SDL window display without crashes
5. ✅ Clean up debug output and logging

**Implementation Details**:

### Clean Up SlangDisplay::display Method
```cpp
// filepath: c:\dev\ChameleonRT\backends\slang\slangdisplay.cpp
// Replace the entire display method with this cleaned-up version:
void SlangDisplay::display(RenderBackend* backend) {
    std::cout << "🖼️ SlangDisplay::display - Starting frame" << std::endl;
    
    // Acquire next swapchain image
    m_currentFrameIndex = swapchain->acquireNextImage();
    
    if (m_currentFrameIndex < 0 || m_currentFrameIndex >= (int)framebuffers.size()) {
        std::cerr << "❌ Invalid frame index: " << m_currentFrameIndex << std::endl;
        return;
    }
    
    // Use proper frame-based buffer index
    size_t bufferIndex = m_currentFrameIndex % transientHeaps.size();
    
    // Reset the transient heap to prepare for new commands
    transientHeaps[bufferIndex]->synchronizeAndReset();
    
    // Create a fresh command buffer
    Slang::ComPtr<gfx::ICommandBuffer> commandBuffer;
    transientHeaps[bufferIndex]->createCommandBuffer(commandBuffer.writeRef());
    
    // STEP 1: Simple screen clearing (no complex rendering yet)
    clearScreenToColor(commandBuffer.get(), 0.2f, 0.3f, 0.8f, 1.0f); // Nice blue color
    
    // STEP 2: End ImGui frame (even though we're not rendering it yet)
    ImGui::Render(); // This prevents ImGui from accumulating draw data
    
    // Execute command buffer
    commandBuffer->close();
    queue->executeCommandBuffers(1, commandBuffer.readRef(), nullptr, 0);
    
    // Finish and present
    transientHeaps[bufferIndex]->finish();
    swapchain->present();
    
    std::cout << "✅ Frame completed successfully" << std::endl;
}

// Add this new helper method to SlangDisplay class:
void SlangDisplay::clearScreenToColor(gfx::ICommandBuffer* commandBuffer, 
                                     float r, float g, float b, float a) {
    try {
        // Create render encoder with proper error handling
        auto renderEncoder = commandBuffer->encodeRenderCommands(
            renderPassLayout.get(), 
            framebuffers[m_currentFrameIndex].get()
        );
        
        // The clear happens automatically based on render pass layout
        // We just need to end encoding to trigger the clear
        renderEncoder->endEncoding();
        
        std::cout << "🎨 Screen cleared to color (" << r << ", " << g << ", " << b << ", " << a << ")" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error during screen clear: " << e.what() << std::endl;
        throw;
    }
}
```

### Fix Render Pass Layout Issues
```cpp
// filepath: c:\dev\ChameleonRT\backends\slang\slangdisplay.cpp
// Update the render pass creation to use proper load/store operations:

// In the constructor or initialization method, update render pass layout:
gfx::IRenderPassLayout::TargetAccessDesc renderTargetAccess = {};
renderTargetAccess.loadOp = gfx::IRenderPassLayout::TargetLoadOp::Clear;  // Clear on load
renderTargetAccess.storeOp = gfx::IRenderPassLayout::TargetStoreOp::Store; // Store result
renderTargetAccess.initialState = gfx::ResourceState::Undefined;           // First use
renderTargetAccess.finalState = gfx::ResourceState::Present;               // For presentation

gfx::IRenderPassLayout::Desc renderPassLayoutDesc = {};
renderPassLayoutDesc.renderTargetCount = 1;
renderPassLayoutDesc.renderTargets = &renderTargetAccess;
renderPassLayoutDesc.depthStencil = nullptr; // No depth buffer for now

// Set clear color to our blue color
renderTargetAccess.clearValue.color.floatValues[0] = 0.2f; // R
renderTargetAccess.clearValue.color.floatValues[1] = 0.3f; // G  
renderTargetAccess.clearValue.color.floatValues[2] = 0.8f; // B
renderTargetAccess.clearValue.color.floatValues[3] = 1.0f; // A

auto result = device->createRenderPassLayout(renderPassLayoutDesc, renderPassLayout.writeRef());
if (SLANG_FAILED(result)) {
    throw std::runtime_error("Failed to create render pass layout");
}
```

### Add Method Declaration to Header
```cpp
// filepath: c:\dev\ChameleonRT\backends\slang\slangdisplay.h  
// Add to SlangDisplay class private methods:
private:
    void clearScreenToColor(gfx::ICommandBuffer* commandBuffer, float r, float g, float b, float a);
```

**Checkpoints**: ✅ ALL COMPLETED
- [x] Application launches without crashing
- [x] SDL window displays with solid blue background
- [x] No render pass layout errors in console
- [x] Clean debug output without excessive logging  
- [x] Window can be resized and closed properly
- [x] Frame rate is stable (60+ FPS)

**Test Scenario**: ✅ Launch application and verify stable blue window for 30+ seconds.

**Test Commands**: 
```powershell
cd C:\dev\ChameleonRT\build_vulkan
cmake --build . --config Debug
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
```

**Final Acceptance Criteria**: ✅ ALL MET
- [x] No crashes during startup or shutdown
- [x] Consistent blue background color
- [x] Stable performance without memory leaks
- [x] Clean console output with informative messages
- [x] Window responds to system events properly
- [x] Ready foundation for ImGui integration

**Why This Stage Matters**:
This stage ensures we have a rock-solid foundation before adding ImGui complexity. It separates concerns by:
1. **✅ Eliminating crashes** from render pass issues
2. **✅ Removing scene rendering** dependencies that aren't working yet  
3. **✅ Validating Slang GFX** basic functionality works correctly
4. **✅ Providing clear success criteria** - a stable colored window
5. **✅ Creating clean baseline** for ImGui integration in Stage 1+

**Next Stage**: Ready to proceed to Stage 1 - Foundation Setup and Shader Creation

---

## Stage 1: Foundation Setup and Shader Creation

**Objective**: Create the basic ImGui rendering infrastructure with shaders and pipeline state.

**Tasks**:
1. Create `SlangImGuiRenderer` class structure
2. Implement ImGui vertex and fragment shaders in Slang
3. Create shader compilation and program creation
4. Setup basic pipeline state descriptor

**Implementation Details**:

### Create Header File
```cpp
// Create new file: c:\dev\ChameleonRT\backends\slang\slang_imgui_renderer.h
#pragma once
#include <slang-gfx.h>
#include <imgui.h>
#include <memory>
#include <vector>

using namespace Slang;
using namespace gfx;

class SlangImGuiRenderer {
private:
    ComPtr<gfx::IDevice> m_device;
    ComPtr<gfx::IShaderProgram> m_shaderProgram;
    ComPtr<gfx::IPipelineState> m_pipelineState;
    ComPtr<gfx::IBufferResource> m_vertexBuffer;
    ComPtr<gfx::IBufferResource> m_indexBuffer;
    ComPtr<gfx::ITextureResource> m_fontTexture;
    ComPtr<gfx::ISamplerState> m_fontSampler;
    ComPtr<gfx::IBufferResource> m_constantBuffer;
    
    size_t m_vertexBufferSize = 0;
    size_t m_indexBufferSize = 0;
    
public:
    bool initialize(gfx::IDevice* device);
    void render(gfx::ICommandBuffer* cmdBuffer, gfx::IFramebuffer* framebuffer, 
                gfx::IRenderPassLayout* renderPassLayout, ImDrawData* drawData);
    void shutdown();
    
private:
    bool createShaders();
    bool createPipelineState();
    bool createFontTexture();
    void updateBuffers(ImDrawData* drawData);
};
```

### Create Implementation File with Shaders
```cpp
// Create new file: c:\dev\ChameleonRT\backends\slang\slang_imgui_renderer.cpp
#include "slang_imgui_renderer.h"
#include <iostream>
#include <algorithm>

// ImGui shaders written in HLSL for Slang compilation
static const char* IMGUI_VERTEX_SHADER_HLSL = R"(
struct VertexData {
    float2 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR0;
};

cbuffer ProjectionData : register(b0) {
    float4x4 projectionMatrix;
}

VertexOutput vertexMain(VertexData input) {
    VertexOutput output;
    output.position = mul(projectionMatrix, float4(input.position, 0.0, 1.0));
    output.texCoord = input.texCoord;
    output.color = input.color;
    return output;
}
)";

static const char* IMGUI_FRAGMENT_SHADER_HLSL = R"(
struct FragmentInput {
    float2 texCoord : TEXCOORD0;
    float4 color : COLOR0;
};

Texture2D fontAtlas : register(t0);
SamplerState fontSampler : register(s0);

float4 fragmentMain(FragmentInput input) : SV_Target {
    return input.color * fontAtlas.Sample(fontSampler, input.texCoord);
}
)";

bool SlangImGuiRenderer::initialize(gfx::IDevice* device) {
    m_device = device;
    
    if (!createShaders()) {
        std::cerr << "❌ Failed to create ImGui shaders" << std::endl;
        return false;
    }
    
    if (!createPipelineState()) {
        std::cerr << "❌ Failed to create ImGui pipeline state" << std::endl;
        return false;
    }
    
    if (!createFontTexture()) {
        std::cerr << "❌ Failed to create ImGui font texture" << std::endl;
        return false;
    }
    
    std::cout << "✅ SlangImGuiRenderer initialized successfully" << std::endl;
    return true;
}
```

**Checkpoints**:
- [ ] `SlangImGuiRenderer` class compiles without errors
- [ ] Header file includes all necessary dependencies
- [ ] HLSL shaders are syntactically correct
- [ ] Basic structure follows Slang GFX patterns

**Test Scenario**: Compile the project and verify no syntax errors in the new files.

**Test Commands** (D3D12 Default):
```powershell
cd C:\dev\ChameleonRT\build_vulkan
cmake --build . --config Debug
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
```

**Note**: Cross-API testing not required until Stage 5. Using existing build_vulkan directory but with default D3D12 configuration (no USE_VULKAN flag set).

**Final Acceptance Criteria**:
_[To be filled manually]_

---

## Stage 2: Shader Compilation and Program Creation

**Objective**: Implement proper shader compilation through Slang and create shader program.

**Tasks**:
1. Add Slang session creation for shader compilation
2. Implement vertex and fragment shader compilation
3. Create shader program from compiled shaders
4. Add proper error handling for compilation failures

**Implementation Details**:

```cpp
// Add to slang_imgui_renderer.cpp
bool SlangImGuiRenderer::createShaders() {
    // Create shader program descriptor
    gfx::IShaderProgram::Desc programDesc = {};
    programDesc.pipelineType = gfx::PipelineType::Graphics;
    
    // Setup vertex shader
    gfx::IShaderProgram::ShaderDesc vertexShaderDesc = {};
    vertexShaderDesc.stage = gfx::SlangStage::Vertex;
    vertexShaderDesc.source = IMGUI_VERTEX_SHADER_HLSL;
    vertexShaderDesc.entryPointName = "vertexMain";
    
    // Setup fragment shader  
    gfx::IShaderProgram::ShaderDesc fragmentShaderDesc = {};
    fragmentShaderDesc.stage = gfx::SlangStage::Fragment;
    fragmentShaderDesc.source = IMGUI_FRAGMENT_SHADER_HLSL;
    fragmentShaderDesc.entryPointName = "fragmentMain";
    
    gfx::IShaderProgram::ShaderDesc shaders[] = {vertexShaderDesc, fragmentShaderDesc};
    programDesc.shaderCount = 2;
    programDesc.shaders = shaders;
    
    auto result = m_device->createShaderProgram(programDesc, m_shaderProgram.writeRef());
    if (SLANG_FAILED(result)) {
        std::cerr << "❌ Failed to create shader program, error code: " << result << std::endl;
        return false;
    }
    
    std::cout << "✅ ImGui shader program created successfully" << std::endl;
    return true;
}
```

**Checkpoints**:
- [ ] Shader compilation succeeds without errors
- [ ] Vertex shader compiles to correct stage
- [ ] Fragment shader compiles to correct stage
- [ ] Shader program creation returns success
- [ ] Error messages are informative for debugging

**Test Scenario**: Run shader compilation in isolation and verify success.

**Test Commands** (D3D12 Default):
```powershell
cd C:\dev\ChameleonRT\build_vulkan
cmake --build . --config Debug
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
```

**Note**: Cross-API testing not required until Stage 5. Shader compilation through Slang GFX is API-agnostic.

**Final Acceptance Criteria**:
_[To be filled manually]_

---

## Stage 3: Buffer Management System

**Objective**: Create dynamic vertex and index buffer management for ImGui geometry.

**Tasks**:
1. Implement buffer creation with proper usage flags
2. Add dynamic buffer resizing logic
3. Create buffer mapping/unmapping for data upload
4. Add vertex layout description for ImGui format

**Implementation Details**:

```cpp
// Add to slang_imgui_renderer.cpp
void SlangImGuiRenderer::updateBuffers(ImDrawData* drawData) {
    if (!drawData || drawData->TotalVtxCount == 0 || drawData->TotalIdxCount == 0) {
        return;
    }
    
    size_t requiredVertexBytes = drawData->TotalVtxCount * sizeof(ImDrawVert);
    size_t requiredIndexBytes = drawData->TotalIdxCount * sizeof(ImDrawIdx);
    
    // Create or resize vertex buffer
    if (!m_vertexBuffer || m_vertexBufferSize < requiredVertexBytes) {
        m_vertexBufferSize = std::max(requiredVertexBytes, m_vertexBufferSize * 2);
        
        gfx::IBufferResource::Desc bufferDesc = {};
        bufferDesc.type = gfx::IResource::Type::Buffer;
        bufferDesc.sizeInBytes = m_vertexBufferSize;
        bufferDesc.format = gfx::Format::Unknown;
        bufferDesc.elementSize = sizeof(ImDrawVert);
        bufferDesc.allowedStates = gfx::ResourceStateSet(
            gfx::ResourceState::VertexBuffer | gfx::ResourceState::CopyDestination);
        bufferDesc.defaultState = gfx::ResourceState::VertexBuffer;
        bufferDesc.memoryType = gfx::MemoryType::Upload;
        
        m_vertexBuffer = m_device->createBufferResource(bufferDesc, nullptr);
        if (!m_vertexBuffer) {
            std::cerr << "❌ Failed to create vertex buffer of size " << m_vertexBufferSize << std::endl;
            return;
        }
        
        std::cout << "📊 Created vertex buffer: " << m_vertexBufferSize << " bytes" << std::endl;
    }
    
    // Create or resize index buffer
    if (!m_indexBuffer || m_indexBufferSize < requiredIndexBytes) {
        m_indexBufferSize = std::max(requiredIndexBytes, m_indexBufferSize * 2);
        
        gfx::IBufferResource::Desc bufferDesc = {};
        bufferDesc.type = gfx::IResource::Type::Buffer;
        bufferDesc.sizeInBytes = m_indexBufferSize;
        bufferDesc.format = gfx::Format::Unknown;
        bufferDesc.elementSize = sizeof(ImDrawIdx);
        bufferDesc.allowedStates = gfx::ResourceStateSet(
            gfx::ResourceState::IndexBuffer | gfx::ResourceState::CopyDestination);
        bufferDesc.defaultState = gfx::ResourceState::IndexBuffer;
        bufferDesc.memoryType = gfx::MemoryType::Upload;
        
        m_indexBuffer = m_device->createBufferResource(bufferDesc, nullptr);
        if (!m_indexBuffer) {
            std::cerr << "❌ Failed to create index buffer of size " << m_indexBufferSize << std::endl;
            return;
        }
        
        std::cout << "📊 Created index buffer: " << m_indexBufferSize << " bytes" << std::endl;
    }
    
    // Upload vertex data
    void* vertexMappedData = nullptr;
    if (SLANG_SUCCEEDED(m_vertexBuffer->map(nullptr, &vertexMappedData))) {
        ImDrawVert* dstVertices = (ImDrawVert*)vertexMappedData;
        for (int n = 0; n < drawData->CmdListsCount; n++) {
            const ImDrawList* cmdList = drawData->CmdLists[n];
            memcpy(dstVertices, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
            dstVertices += cmdList->VtxBuffer.Size;
        }
        m_vertexBuffer->unmap(nullptr);
    } else {
        std::cerr << "❌ Failed to map vertex buffer for writing" << std::endl;
    }
    
    // Upload index data
    void* indexMappedData = nullptr;
    if (SLANG_SUCCEEDED(m_indexBuffer->map(nullptr, &indexMappedData))) {
        ImDrawIdx* dstIndices = (ImDrawIdx*)indexMappedData;
        for (int n = 0; n < drawData->CmdListsCount; n++) {
            const ImDrawList* cmdList = drawData->CmdLists[n];
            memcpy(dstIndices, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
            dstIndices += cmdList->IdxBuffer.Size;
        }
        m_indexBuffer->unmap(nullptr);
    } else {
        std::cerr << "❌ Failed to map index buffer for writing" << std::endl;
    }
}
```

**Checkpoints**:
- [ ] Buffer creation succeeds with correct usage flags
- [ ] Buffer resizing logic handles growth correctly
- [ ] Memory mapping operations succeed
- [ ] ImGui geometry data uploads without corruption
- [ ] Large UI scenes don't cause crashes

**Test Scenario**: Create UI with many elements and verify buffer management.

**Test Commands** (D3D12 Default):
```powershell
cd C:\dev\ChameleonRT\build_vulkan
cmake --build . --config Debug
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
```

**Note**: Cross-API testing not required until Stage 5. Buffer management through Slang GFX is API-agnostic.

**Final Acceptance Criteria**:
_[To be filled manually]_

---

## Stage 4: Font Texture and Pipeline State

**Objective**: Create font texture from ImGui atlas and complete graphics pipeline setup.

**Tasks**:
1. Extract font atlas data from ImGui
2. Create texture resource with proper format
3. Setup sampler state for font filtering
4. Complete pipeline state with blend and rasterizer settings

**Implementation Details**:

```cpp
// Add to slang_imgui_renderer.cpp
bool SlangImGuiRenderer::createFontTexture() {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* fontPixels;
    int fontWidth, fontHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    
    // Create font texture resource
    gfx::ITextureResource::Desc textureDesc = {};
    textureDesc.type = gfx::IResource::Type::Texture2D;
    textureDesc.size.width = fontWidth;
    textureDesc.size.height = fontHeight;
    textureDesc.size.depth = 1;
    textureDesc.format = gfx::Format::R8G8B8A8_UNORM;
    textureDesc.numMipLevels = 1;
    textureDesc.defaultState = gfx::ResourceState::ShaderResource;
    textureDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::ShaderResource | gfx::ResourceState::CopyDestination);
    
    gfx::ITextureResource::SubresourceData subresourceData = {};
    subresourceData.data = fontPixels;
    subresourceData.strideY = fontWidth * 4;
    subresourceData.strideZ = fontWidth * fontHeight * 4;
    
    m_fontTexture = m_device->createTextureResource(textureDesc, &subresourceData);
    if (!m_fontTexture) {
        std::cerr << "❌ Failed to create font texture resource" << std::endl;
        return false;
    }
    
    // Create font sampler state
    gfx::ISamplerState::Desc samplerDesc = {};
    samplerDesc.minFilter = gfx::TextureFilteringMode::Linear;
    samplerDesc.magFilter = gfx::TextureFilteringMode::Linear;
    samplerDesc.mipFilter = gfx::TextureFilteringMode::Point;
    samplerDesc.addressU = gfx::TextureAddressingMode::Clamp;
    samplerDesc.addressV = gfx::TextureAddressingMode::Clamp;
    samplerDesc.addressW = gfx::TextureAddressingMode::Clamp;
    
    m_fontSampler = m_device->createSamplerState(samplerDesc);
    if (!m_fontSampler) {
        std::cerr << "❌ Failed to create font sampler state" << std::endl;
        return false;
    }
    
    // Register texture with ImGui
    io.Fonts->SetTexID((ImTextureID)(intptr_t)m_fontTexture.get());
    
    std::cout << "✅ Font texture created: " << fontWidth << "x" << fontHeight << " pixels" << std::endl;
    return true;
}

bool SlangImGuiRenderer::createPipelineState() {
    if (!m_shaderProgram) {
        std::cerr << "❌ Shader program must be created before pipeline state" << std::endl;
        return false;
    }
    
    gfx::GraphicsPipelineStateDesc pipelineDesc = {};
    pipelineDesc.program = m_shaderProgram;
    
    // Define input layout matching ImDrawVert structure
    gfx::InputElementDesc inputElements[] = {
        {"POSITION", 0, gfx::Format::R32G32_FLOAT, 0, offsetof(ImDrawVert, pos)},
        {"TEXCOORD", 0, gfx::Format::R32G32_FLOAT, 0, offsetof(ImDrawVert, uv)},
        {"COLOR", 0, gfx::Format::R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col)}
    };
    
    gfx::InputLayoutDesc inputLayoutDesc = {};
    inputLayoutDesc.inputElementCount = 3;
    inputLayoutDesc.inputElements = inputElements;
    pipelineDesc.inputLayout = inputLayoutDesc;
    
    // Configure rasterizer state
    gfx::RasterizerStateDesc rasterizerDesc = {};
    rasterizerDesc.fillMode = gfx::FillMode::Solid;
    rasterizerDesc.cullMode = gfx::CullMode::None;
    rasterizerDesc.frontFace = gfx::FrontFaceMode::CounterClockwise;
    rasterizerDesc.depthBias = 0;
    rasterizerDesc.depthBiasClamp = 0.0f;
    rasterizerDesc.slopeScaledDepthBias = 0.0f;
    rasterizerDesc.depthClipEnable = true;
    rasterizerDesc.scissorEnable = true;
    rasterizerDesc.multisampleEnable = false;
    rasterizerDesc.antialiasedLineEnable = false;
    pipelineDesc.rasterizer = rasterizerDesc;
    
    // Setup blend state for alpha blending
    gfx::BlendStateDesc blendDesc = {};
    blendDesc.targetCount = 1;
    blendDesc.targets[0].blendEnable = true;
    blendDesc.targets[0].srcFactor = gfx::BlendFactor::SrcAlpha;
    blendDesc.targets[0].dstFactor = gfx::BlendFactor::InvSrcAlpha;
    blendDesc.targets[0].op = gfx::BlendOp::Add;
    blendDesc.targets[0].srcFactorAlpha = gfx::BlendFactor::One;
    blendDesc.targets[0].dstFactorAlpha = gfx::BlendFactor::InvSrcAlpha;
    blendDesc.targets[0].opAlpha = gfx::BlendOp::Add;
    blendDesc.targets[0].writeMask = gfx::RenderTargetWriteMask::EnableAll;
    pipelineDesc.blend = blendDesc;
    
    // Configure depth stencil state
    gfx::DepthStencilStateDesc depthStencilDesc = {};
    depthStencilDesc.depthTestEnable = false;
    depthStencilDesc.depthWriteEnable = false;
    depthStencilDesc.depthFunc = gfx::ComparisonFunc::Always;
    depthStencilDesc.stencilEnable = false;
    pipelineDesc.depthStencil = depthStencilDesc;
    
    // Set primitive topology
    pipelineDesc.primitiveType = gfx::PrimitiveType::Triangle;
    
    auto result = m_device->createGraphicsPipelineState(pipelineDesc, m_pipelineState.writeRef());
    if (SLANG_FAILED(result)) {
        std::cerr << "❌ Failed to create graphics pipeline state" << std::endl;
        return false;
    }
    
    std::cout << "✅ Graphics pipeline state created successfully" << std::endl;
    return true;
}
```

**Checkpoints**:
- [ ] Font atlas data extracts correctly from ImGui
- [ ] Texture creation succeeds with RGBA format
- [ ] Sampler state creates with linear filtering
- [ ] Pipeline state compiles with proper blend modes
- [ ] Alpha blending configuration is correct for UI transparency

**Test Scenario**: Initialize renderer and verify font texture loads with correct dimensions.

**Test Commands** (D3D12 Default):
```powershell
cd C:\dev\ChameleonRT\build_vulkan
cmake --build . --config Debug
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
```

**Note**: Cross-API testing not required until Stage 5. Pipeline state and texture creation through Slang GFX is API-agnostic.

**Final Acceptance Criteria**:
_[To be filled manually]_

---

## Stage 5: Render Command Implementation

**Objective**: Complete the render function that processes ImGui draw commands.

**Tasks**:
1. Implement projection matrix calculation for screen coordinates
2. Create constant buffer for shader uniforms
3. Process ImGui command lists and draw commands
4. Handle scissor rectangles for UI clipping

**Implementation Details**:

```cpp
// Add to slang_imgui_renderer.cpp
void SlangImGuiRenderer::render(gfx::ICommandBuffer* cmdBuffer, gfx::IFramebuffer* framebuffer,
                               gfx::IRenderPassLayout* renderPassLayout, ImDrawData* drawData) {
    if (!drawData || drawData->CmdListsCount == 0 || drawData->TotalVtxCount == 0) {
        return;
    }
    
    // Update vertex and index buffers with current frame data
    updateBuffers(drawData);
    
    // Calculate orthographic projection matrix for screen coordinates
    float displayLeft = drawData->DisplayPos.x;
    float displayRight = drawData->DisplayPos.x + drawData->DisplaySize.x;
    float displayTop = drawData->DisplayPos.y;
    float displayBottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
    
    float projectionMatrix[4][4] = {
        { 2.0f/(displayRight-displayLeft),   0.0f,                                  0.0f,   0.0f },
        { 0.0f,                             2.0f/(displayTop-displayBottom),       0.0f,   0.0f },
        { 0.0f,                             0.0f,                                 -1.0f,   0.0f },
        { (displayRight+displayLeft)/(displayLeft-displayRight),  (displayTop+displayBottom)/(displayBottom-displayTop), 0.0f, 1.0f },
    };
    
    // Create or update constant buffer for projection matrix
    if (!m_constantBuffer) {
        gfx::IBufferResource::Desc bufferDesc = {};
        bufferDesc.type = gfx::IResource::Type::Buffer;
        bufferDesc.sizeInBytes = sizeof(projectionMatrix);
        bufferDesc.format = gfx::Format::Unknown;
        bufferDesc.elementSize = 0;
        bufferDesc.allowedStates = gfx::ResourceStateSet(
            gfx::ResourceState::ConstantBuffer | gfx::ResourceState::CopyDestination);
        bufferDesc.defaultState = gfx::ResourceState::ConstantBuffer;
        bufferDesc.memoryType = gfx::MemoryType::Upload;
        
        m_constantBuffer = m_device->createBufferResource(bufferDesc, nullptr);
        if (!m_constantBuffer) {
            std::cerr << "❌ Failed to create constant buffer" << std::endl;
            return;
        }
    }
    
    // Upload projection matrix to constant buffer
    void* constantMappedData = nullptr;
    if (SLANG_SUCCEEDED(m_constantBuffer->map(nullptr, &constantMappedData))) {
        memcpy(constantMappedData, projectionMatrix, sizeof(projectionMatrix));
        m_constantBuffer->unmap(nullptr);
    } else {
        std::cerr << "❌ Failed to map constant buffer" << std::endl;
        return;
    }
    
    // Begin render pass encoding
    auto renderEncoder = cmdBuffer->encodeRenderCommands(renderPassLayout, framebuffer);
    
    // Bind graphics pipeline and resources
    renderEncoder->bindPipeline(m_pipelineState);
    renderEncoder->bindResource(0, m_constantBuffer); // Projection matrix
    renderEncoder->bindResource(1, m_fontTexture);    // Font atlas
    renderEncoder->bindResource(2, m_fontSampler);    // Font sampler
    
    // Set vertex and index buffers
    gfx::IBufferResource* vertexBuffers[] = {m_vertexBuffer};
    Offset vertexOffsets[] = {0};
    renderEncoder->setVertexBuffers(0, 1, vertexBuffers, vertexOffsets);
    renderEncoder->setIndexBuffer(m_indexBuffer, gfx::Format::R16_UINT, 0);
    
    // Configure viewport to match display size
    gfx::Viewport viewport = {};
    viewport.originX = 0.0f;
    viewport.originY = 0.0f;
    viewport.extentX = drawData->DisplaySize.x;
    viewport.extentY = drawData->DisplaySize.y;
    viewport.minZ = 0.0f;
    viewport.maxZ = 1.0f;
    renderEncoder->setViewports(1, &viewport);
    
    // Process all draw command lists
    ImVec2 clipOffset = drawData->DisplayPos;
    int globalIndexOffset = 0;
    int globalVertexOffset = 0;
    
    for (int listIdx = 0; listIdx < drawData->CmdListsCount; listIdx++) {
        const ImDrawList* cmdList = drawData->CmdLists[listIdx];
        
        for (int cmdIdx = 0; cmdIdx < cmdList->CmdBuffer.Size; cmdIdx++) {
            const ImDrawCmd* drawCmd = &cmdList->CmdBuffer[cmdIdx];
            
            if (drawCmd->UserCallback != nullptr) {
                // Skip user callbacks in this implementation
                continue;
            }
            
            // Calculate scissor rectangle for clipping
            gfx::ScissorRect scissorRect = {};
            scissorRect.minX = (int32_t)(drawCmd->ClipRect.x - clipOffset.x);
            scissorRect.minY = (int32_t)(drawCmd->ClipRect.y - clipOffset.y);
            scissorRect.maxX = (int32_t)(drawCmd->ClipRect.z - clipOffset.x);
            scissorRect.maxY = (int32_t)(drawCmd->ClipRect.w - clipOffset.y);
            
            // Clamp scissor rectangle to viewport bounds
            if (scissorRect.minX < 0) scissorRect.minX = 0;
            if (scissorRect.minY < 0) scissorRect.minY = 0;
            if (scissorRect.maxX > (int32_t)drawData->DisplaySize.x) scissorRect.maxX = (int32_t)drawData->DisplaySize.x;
            if (scissorRect.maxY > (int32_t)drawData->DisplaySize.y) scissorRect.maxY = (int32_t)drawData->DisplaySize.y;
            
            // Skip if scissor rectangle is invalid
            if (scissorRect.maxX <= scissorRect.minX || scissorRect.maxY <= scissorRect.minY) {
                continue;
            }
            
            renderEncoder->setScissorRects(1, &scissorRect);
            
            // Issue indexed draw call
            renderEncoder->drawIndexed(
                drawCmd->ElemCount,
                globalIndexOffset + drawCmd->IdxOffset,
                globalVertexOffset + drawCmd->VtxOffset
            );
        }
        
        globalIndexOffset += cmdList->IdxBuffer.Size;
        globalVertexOffset += cmdList->VtxBuffer.Size;
    }
    
    renderEncoder->endEncoding();
    
    std::cout << "🎨 Rendered " << drawData->CmdListsCount << " ImGui command lists with " 
              << drawData->TotalVtxCount << " vertices" << std::endl;
}
```

**Checkpoints**:
- [ ] Projection matrix calculations produce correct screen coordinates
- [ ] Constant buffer upload succeeds
- [ ] Render encoder creation and resource binding work
- [ ] Scissor rectangles properly clip UI elements
- [ ] Draw calls execute without validation errors
- [ ] Multiple command lists render correctly

**Test Scenario**: Render complex UI with overlapping elements and verify clipping.

**Cross-API Testing (Required from this stage)**:

Since Stage 5 first renders visible ImGui content, both Vulkan and D3D12 paths must be validated to ensure identical rendering behavior across graphics APIs.

**Vulkan Build & Test**:
```powershell
# Build with explicit Vulkan configuration
cd C:\dev\ChameleonRT
cmake -B build_vulkan -DUSE_VULKAN=ON
cmake --build build_vulkan --config Debug

# Test Vulkan rendering
cd build_vulkan
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
```

**D3D12 Build & Test** (Default):
```powershell
# Build with default D3D12 configuration (no special flags needed)
cd C:\dev\ChameleonRT  
cmake -B build_d3d12
cmake --build build_d3d12 --config Debug

# Test D3D12 rendering
cd build_d3d12
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
```

**Cross-API Validation Checklist**:
- [ ] Both APIs render identical ImGui widgets (buttons, text, sliders, checkboxes)
- [ ] Font rendering clarity and scaling consistent between APIs
- [ ] No coordinate system artifacts or Y-axis flipping issues
- [ ] UI elements respond to mouse input correctly on both APIs  
- [ ] Scissor rectangle clipping works identically
- [ ] Performance is comparable between APIs (within 10% frame time difference)
- [ ] No API-specific rendering artifacts or visual glitches

**Expected Results**: Both builds should produce visually identical ImGui rendering with responsive UI interaction.

**Final Acceptance Criteria**:
_[To be filled manually]_

---

## Stage 6: SlangDisplay Integration

**Objective**: Integrate the ImGui renderer into SlangDisplay and test end-to-end functionality.

**Tasks**:
1. Add ImGui renderer to SlangDisplay class
2. Update renderImGuiDrawData implementation
3. Add proper initialization and cleanup
4. Handle both Vulkan and D3D12 render pass layouts

**Implementation Details**:

### Update SlangDisplay Header
```cpp
// filepath: c:\dev\ChameleonRT\backends\slang\slangdisplay.h
// Add to SlangDisplay class includes:
#include "slang_imgui_renderer.h"

// Add to SlangDisplay class private members:
private:
    std::unique_ptr<SlangImGuiRenderer> m_imguiRenderer;
    bool m_imguiInitialized = false;

// Add to SlangDisplay class public methods:
public:
    bool initializeImGuiRenderer();
    void shutdownImGuiRenderer();
```

### Update SlangDisplay Implementation
```cpp
// filepath: c:\dev\ChameleonRT\backends\slang\slangdisplay.cpp
// Update renderImGuiDrawData method - replace the TODO placeholder:
void SlangDisplay::renderImGuiDrawData(gfx::ICommandBuffer* commandBuffer, gfx::IFramebuffer* framebuffer) {
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->CmdListsCount == 0) {
        return;
    }

    // Initialize ImGui renderer on first use
    if (!m_imguiInitialized) {
        if (!initializeImGuiRenderer()) {
            std::cerr << "❌ Failed to initialize ImGui renderer" << std::endl;
            return;
        }
        m_imguiInitialized = true;
    }

#ifdef USE_VULKAN
    // Use Vulkan render pass layout
    m_imguiRenderer->render(commandBuffer, framebuffer, renderPassLayout.get(), drawData);
#else
    // For D3D12, pass nullptr for render pass layout (handled internally)
    m_imguiRenderer->render(commandBuffer, framebuffer, nullptr, drawData);
#endif
}

bool SlangDisplay::initializeImGuiRenderer() {
    m_imguiRenderer = std::make_unique<SlangImGuiRenderer>();
    
    if (!m_imguiRenderer->initialize(device.get())) {
        std::cerr << "❌ Failed to initialize SlangImGuiRenderer" << std::endl;
        m_imguiRenderer.reset();
        return false;
    }
    
    std::cout << "✅ SlangImGuiRenderer initialized successfully" << std::endl;
    return true;
}

void SlangDisplay::shutdownImGuiRenderer() {
    if (m_imguiRenderer) {
        m_imguiRenderer->shutdown();
        m_imguiRenderer.reset();
    }
    m_imguiInitialized = false;
}

// Update destructor to include ImGui cleanup:
SlangDisplay::~SlangDisplay() {
    shutdownImGuiRenderer();
    
    // ... existing cleanup code remains the same
}
```

### Add Shutdown Method
```cpp
// Add shutdown method to slang_imgui_renderer.cpp:
void SlangImGuiRenderer::shutdown() {
    // Clean up resources in reverse order of creation
    m_constantBuffer = nullptr;
    m_fontSampler = nullptr;
    m_fontTexture = nullptr;
    m_indexBuffer = nullptr;
    m_vertexBuffer = nullptr;
    m_pipelineState = nullptr;
    m_shaderProgram = nullptr;
    m_device = nullptr;
    
    m_vertexBufferSize = 0;
    m_indexBufferSize = 0;
    
    std::cout << "✅ SlangImGuiRenderer shutdown complete" << std::endl;
}
```

**Checkpoints**:
- [ ] SlangDisplay compiles with new ImGui renderer integration
- [ ] Initialization occurs correctly on first render call
- [ ] Both Vulkan and D3D12 code paths work
- [ ] Resource cleanup prevents memory leaks
- [ ] Error handling gracefully handles initialization failures

**Test Scenario**: Launch ChameleonRT with Slang backend and verify UI renders on both graphics APIs.

**Vulkan Build & Test**:
```powershell
cd C:\dev\ChameleonRT
cmake -B build_vulkan -DUSE_VULKAN=ON
cmake --build build_vulkan --config Debug
cd build_vulkan
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
```

**D3D12 Build & Test** (Default):
```powershell
cd C:\dev\ChameleonRT
cmake -B build_d3d12  
cmake --build build_d3d12 --config Debug
cd build_d3d12
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
```

**Final Acceptance Criteria**:
- [ ] All ImGui widgets render correctly (buttons, text, sliders, checkboxes)
- [ ] UI responds properly to mouse input and interactions
- [ ] Text rendering is crisp and properly scaled
- [ ] Performance maintains 60+ FPS with typical UI load
- [ ] Works consistently on both Vulkan and D3D12 builds
- [ ] No memory leaks detected during extended usage
- [ ] Graceful error handling for all edge cases
- [ ] UI elements properly clip and don't render outside bounds

---

## Implementation Notes

### Architecture Benefits
- **Single Implementation**: One codebase works across all supported graphics APIs
- **Future-Proof**: Automatic support for new APIs added to Slang GFX
- **Consistent Performance**: Unified optimization strategies
- **Maintainable**: Reduced code duplication and API-specific bugs

### Performance Considerations
- Dynamic buffer resizing minimizes memory allocations
- Upload buffers provide efficient CPU-to-GPU data transfer
- Scissor rectangle optimization reduces overdraw
- Per-frame resource cycling prevents stalls

### Debugging Tips
- Enable Slang GFX validation layers during development
- Use debug output for buffer sizes and draw call counts
- Verify scissor rectangles with wireframe rendering
- Profile with graphics debuggers (RenderDoc, PIX)

### Common Pitfalls
- Ensure proper vertex layout alignment with ImDrawVert structure
- Handle empty draw data gracefully (no crash on zero elements)
- Validate projection matrix calculations for different display DPI
- Test with both single and multi-monitor setups
