#include "slangdisplay.h"
#if CRT_ENABLE_SLANG_IMGUI_RENDERER
#include "SlangImGuiRenderer.h"
#endif
#include <slang.h>
#include <slang-gfx.h>
#include <SDL_syswm.h>
#include <imgui.h>
#include "util/display/imgui_impl_sdl.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>

using namespace Slang;
using namespace gfx;

SlangDisplay::SlangDisplay(SDL_Window* sdl_window) : window(sdl_window) {
    // 1. Extract native window handle from SDL_Window*
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(sdl_window, &wm_info);

    // 2. Create device with explicit API selection  
    gfx::IDevice::Desc deviceDesc = {};
#ifdef USE_VULKAN
    deviceDesc.deviceType = DeviceType::Vulkan;
#else
    deviceDesc.deviceType = DeviceType::DirectX12;
#endif
    gfx::Result res = gfxCreateDevice(&deviceDesc, device.writeRef());
    if (SLANG_FAILED(res)) throw std::runtime_error("Failed to create GFX device");

    // Verify device type and adapter (keep minimal logging for diagnostics)
    const gfx::DeviceInfo& deviceInfo = device->getDeviceInfo();
    const char* deviceTypeName = gfxGetDeviceTypeName(deviceInfo.deviceType);
    // Graphics API determined: deviceTypeName
    
    // 3. Create command queue
    gfx::ICommandQueue::Desc queueDesc = {};
    queueDesc.type = gfx::ICommandQueue::QueueType::Graphics;
    queue = device->createCommandQueue(queueDesc);

    // TRIANGLE EXAMPLE PATTERN: Create swapchain FIRST, then query actual format
    gfx::ISwapchain::Desc swapchainDesc = {};
    swapchainDesc.format = gfx::Format::R8G8B8A8_UNORM; // Request preferred format
    swapchainDesc.width = 1280; // Default, should be window size
    swapchainDesc.height = 720;
    swapchainDesc.imageCount = 2; // EXACTLY like triangle example (kSwapchainImageCount = 2)
    swapchainDesc.queue = queue;
    
    // Extract native window handle from SDL
    gfx::WindowHandle windowHandle = {};
    
#ifdef _WIN32
    HWND hwnd = wm_info.info.win.window;
    windowHandle = gfx::WindowHandle::FromHwnd(hwnd);
#elif defined(__linux__)
    Display* x11Display = wm_info.info.x11.display;
    uint32_t x11Window = wm_info.info.x11.window;
    windowHandle = gfx::WindowHandle::FromXWindow(x11Display, x11Window);
#else
    windowHandle = {};
#endif
    
    swapchain = device->createSwapchain(swapchainDesc, windowHandle);

    // Query actual swapchain properties and format (CRITICAL FIX: following triangle example)
    const auto& actualSwapchainDesc = swapchain->getDesc();
    uint32_t actualImageCount = actualSwapchainDesc.imageCount;
    gfx::Format actualFormat = actualSwapchainDesc.format;  // This might be B8G8R8A8_UNORM!
    
    // Now create framebuffer layout with ACTUAL format (triangle example pattern)
    gfx::IFramebufferLayout::TargetLayout renderTargetLayout = {actualFormat, 1}; // Use actual format!
    gfx::IFramebufferLayout::TargetLayout depthTargetLayout = {gfx::Format::D32_FLOAT, 1};
    gfx::IFramebufferLayout::Desc framebufferLayoutDesc = {};
    framebufferLayoutDesc.renderTargetCount = 1;
    framebufferLayoutDesc.renderTargets = &renderTargetLayout;
    framebufferLayoutDesc.depthStencil = &depthTargetLayout;
    device->createFramebufferLayout(framebufferLayoutDesc, framebufferLayout.writeRef());
    if (actualImageCount != 2) {
        throw std::runtime_error("Expected exactly 2 swapchain images");
    }

    // 5.1. Create render pass layout (following triangle example pattern exactly)
    gfx::IRenderPassLayout::Desc renderPassLayoutDesc = {};
    renderPassLayoutDesc.framebufferLayout = framebufferLayout;
    renderPassLayoutDesc.renderTargetCount = 1;
    gfx::IRenderPassLayout::TargetAccessDesc renderTargetAccess = {};
    gfx::IRenderPassLayout::TargetAccessDesc depthStencilAccess = {};
    renderTargetAccess.loadOp = gfx::IRenderPassLayout::TargetLoadOp::Clear;
    renderTargetAccess.storeOp = gfx::IRenderPassLayout::TargetStoreOp::Store;
    renderTargetAccess.initialState = gfx::ResourceState::Undefined;
    renderTargetAccess.finalState = gfx::ResourceState::Present;
    depthStencilAccess.loadOp = gfx::IRenderPassLayout::TargetLoadOp::Clear;
    depthStencilAccess.storeOp = gfx::IRenderPassLayout::TargetStoreOp::Store;
    depthStencilAccess.initialState = gfx::ResourceState::DepthWrite;
    depthStencilAccess.finalState = gfx::ResourceState::DepthWrite;
    renderPassLayoutDesc.renderTargetAccess = &renderTargetAccess;
    renderPassLayoutDesc.depthStencilAccess = &depthStencilAccess;
    
    auto result = device->createRenderPassLayout(renderPassLayoutDesc, renderPassLayout.writeRef());
    if (SLANG_FAILED(result)) {
        throw std::runtime_error("Failed to create render pass layout");
    }

    // 6. Create framebuffers for swapchain images (UNIFIED METHOD)
    createSwapchainFramebuffers();

    // 7. Create transient heaps (EXACT triangle example pattern)
    transientHeaps.clear();
    transientHeaps.reserve(actualImageCount);
    
    // Triangle example creates one transient heap per swapchain image
    for (uint32_t i = 0; i < actualImageCount; ++i) {
        gfx::ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096 * 1024; // EXACTLY like triangle example
        auto transientHeap = device->createTransientResourceHeap(transientHeapDesc);
        transientHeaps.push_back(transientHeap);
    }

    // 8. Unified ImGui setup - build font atlas for both backends
    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->IsBuilt()) {
        if (io.Fonts->ConfigData.Size == 0) {
            io.Fonts->AddFontDefault();
        }
        io.Fonts->Build();
    }

    // Validate swapchain setup
    uint32_t swapchainImageCount = swapchain->getDesc().imageCount;
    if (framebuffers.size() != swapchainImageCount) {
        throw std::runtime_error("Framebuffers size mismatch");
    }
    if (transientHeaps.size() != swapchainImageCount) {
        throw std::runtime_error("TransientHeaps size mismatch");
    }

    // Initialize shader validation first (to create shader program)
    initShaderValidation();
    
    // Initialize triangle rendering (Step 3: following triangle example pattern)
    initTriangleRendering();

#if CRT_ENABLE_SLANG_IMGUI_RENDERER
    {
        SlangImGuiRenderer::InitializeDesc desc;
        desc.device = device.get();
        desc.framebufferLayout = framebufferLayout.get();
        desc.renderPassLayout = renderPassLayout.get();

        m_imguiRenderer = std::make_unique<SlangImGuiRenderer>();
        m_imguiRendererInitialized = m_imguiRenderer->initialize(desc);
        if (!m_imguiRendererInitialized) {
            std::cerr << "[SlangDisplay] WARNING: ImGui renderer initialization failed" << std::endl;
        }
    }
#endif
}

SlangDisplay::~SlangDisplay() {
    // Make sure we wait for any pending GPU work to complete
    if (queue)
    {
        queue->waitOnHost();
#ifdef _WIN32
        Sleep(100);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
    }

#if CRT_ENABLE_SLANG_IMGUI_RENDERER
    if (m_imguiRenderer) {
        m_imguiRenderer->shutdown();
        m_imguiRenderer.reset();
    }
    m_imguiRendererInitialized = false;
#endif
    
    // Clear our transient heaps (no command buffers to clear - created fresh each frame)
    transientHeaps.clear();

    // Cleanup triangle rendering resources
    cleanupTriangleRendering();

    // Cleanup shader validation resources
    cleanupShaderValidation();

    // Explicitly clear resources in a controlled order
    framebuffers.clear();
    swapchain = nullptr;
    framebufferLayout = nullptr;
    renderPassLayout = nullptr;
    queue = nullptr;
    device = nullptr;
    std::cout << "Graphics resources cleaned up" << std::endl;
}

std::string SlangDisplay::gpu_brand() {
    if (device) {
        const gfx::DeviceInfo& info = device->getDeviceInfo();
        // Print API verification information
        std::cout << "=== GRAPHICS API VERIFICATION ===" << std::endl;
        std::cout << "Device Type Enum: " << (int)info.deviceType << std::endl;
        std::cout << "API Name: " << (info.apiName ? info.apiName : "Unknown") << std::endl;
        std::cout << "Adapter Name: " << (info.adapterName ? info.adapterName : "Unknown") << std::endl;

        // Additional compile-time verification
#ifdef USE_VULKAN
        std::cout << "Compile-time API: VULKAN (USE_VULKAN defined)" << std::endl;
#else
        std::cout << "Compile-time API: D3D12 (USE_VULKAN not defined)" << std::endl;
#endif
        
        std::cout << "=================================" << std::endl;
        
        if (info.adapterName)
            return std::string(info.adapterName);
    }
    return "gfx";
}

std::string SlangDisplay::name() {
    return "SlangDisplay (gfx)";
}

void SlangDisplay::resize(const int width, const int height) {
    if (!swapchain || width <= 0 || height <= 0) {
        std::cerr << "❌ RESIZE: Invalid parameters - swapchain=" << (swapchain ? "valid" : "null") 
                  << " size=" << width << "x" << height << std::endl;
        return;
    }

    queue->waitOnHost();
    framebuffers.clear();
    renderTargetViews.clear();
    
    if (swapchain->resize(width, height) != SLANG_OK) {
        throw std::runtime_error("Swapchain resize failed");
    }
    recreateSwapchainFramebuffers();
}

// New method following triangle example createSwapchainFramebuffers() pattern
void SlangDisplay::recreateSwapchainFramebuffers() {
    // Use unified framebuffer creation method
    createSwapchainFramebuffers();
}

void SlangDisplay::new_frame() {
    // Unified ImGui new frame - ensure font atlas is built
    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->IsBuilt()) {
        if (io.Fonts->ConfigData.Size == 0) {
            io.Fonts->AddFontDefault();
        }
        io.Fonts->Build();
    }
    // No backend-specific calls needed - ImGui_ImplSDL2_NewFrame() and ImGui::NewFrame() 
    // are called by the main application
}

void SlangDisplay::display(RenderBackend* backend) {
    try {
    // CRITICAL: Validate swapchain state before use (triangle example pattern)
    if (!swapchain || framebuffers.empty()) {
        return;
    }
    
    // Acquire next swapchain image
    m_currentFrameIndex = swapchain->acquireNextImage();
    
    // Additional validation: Check frame index is valid
    if (m_currentFrameIndex < 0 || m_currentFrameIndex >= static_cast<int>(framebuffers.size())) {
        return;
    }
    
    // ===== VALIDATION: Basic frame tracking (minimal logging) =====
    static int frameCount = 0;
    frameCount++;
    if (frameCount <= 5 || frameCount % 100 == 0) {  // Reduce logging frequency
        std::cout << "Frame " << frameCount << " - Index: " << m_currentFrameIndex << std::endl;
    }

    // Use proper frame-based buffer index
    size_t bufferIndex = m_currentFrameIndex % transientHeaps.size();

    if (bufferIndex >= transientHeaps.size() || !transientHeaps[bufferIndex]) {
        return;
    }

    // Reset the transient heap to prepare for new commands  
    transientHeaps[bufferIndex]->synchronizeAndReset();

    // ===== PROPER GPU SYNCHRONIZATION: Wait for GPU to finish =====
    queue->waitOnHost();  // Proper GPU wait instead of arbitrary timing delay
    
    // Create command buffer using triangle example pattern
    Slang::ComPtr<gfx::ICommandBuffer> commandBuffer = transientHeaps[bufferIndex]->createCommandBuffer();

    if (!commandBuffer) {
        return;
    }
    
    if (!renderPassLayout) {
        return;
    }
    
    if (!framebuffers[m_currentFrameIndex]) {
        std::cerr << "❌ CRITICAL: Framebuffer[" << m_currentFrameIndex << "] is null!" << std::endl;
        return;
    }

    auto renderEncoder = commandBuffer->encodeRenderCommands(renderPassLayout, framebuffers[m_currentFrameIndex]);

    // Set viewport (following triangle example)
    gfx::Viewport viewport = {};
    viewport.maxZ = 1.0f;
    viewport.extentX = (float)1280; // TODO: Get from actual window size
    viewport.extentY = (float)720;
    renderEncoder->setViewportAndScissor(viewport);
    
    // STEP 3: Render triangle geometry (following triangle example pattern)
    if (m_triangle.triangleInitialized) {
        renderTriangle(renderEncoder);
    }
    
    ImGui::Render();

#if CRT_ENABLE_SLANG_IMGUI_RENDERER
    if (m_imguiRenderer) {
        m_imguiRenderer->render(ImGui::GetDrawData(), renderEncoder);
    }
#endif
    
    // End render commands
    renderEncoder->endEncoding();

    // Execute and present
    commandBuffer->close();
    queue->executeCommandBuffer(commandBuffer);  // Single command buffer like triangle example
    
    // ===== CRITICAL FIX: Add missing finish() call like triangle example =====
    transientHeaps[bufferIndex]->finish();
    
    swapchain->present();
    
    } 
    catch (const std::exception& e) {
        std::cerr << "Exception in display(): " << e.what() << std::endl;
        throw;
    } catch (...) {
        std::cerr << " Unknown exception in display()" << std::endl;
        throw;
    }
}

gfx::IFramebuffer* SlangDisplay::getCurrentFramebuffer() {
    return framebuffers[m_currentFrameIndex].get();
}

// =============================================================================
// UNIFIED FRAMEBUFFER CREATION HELPERS (Refactored from duplicated code)
// =============================================================================

// Create render target view with unified descriptor setup
Slang::ComPtr<gfx::IResourceView> SlangDisplay::createRenderTargetView(gfx::ITextureResource* colorBuffer, gfx::Format format) {
    gfx::IResourceView::Desc colorBufferViewDesc = {};
    memset(&colorBufferViewDesc, 0, sizeof(colorBufferViewDesc)); // Like triangle example
    colorBufferViewDesc.format = format; // Use actual format, not hardcoded!
    colorBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
    colorBufferViewDesc.type = gfx::IResourceView::Type::RenderTarget;
    return device->createTextureView(colorBuffer, colorBufferViewDesc);
}

// Create depth buffer with unified configuration
Slang::ComPtr<gfx::ITextureResource> SlangDisplay::createDepthBuffer(uint32_t width, uint32_t height) {
    gfx::ITextureResource::Desc depthBufferDesc;
    depthBufferDesc.type = gfx::IResource::Type::Texture2D;
    depthBufferDesc.size.width = width;
    depthBufferDesc.size.height = height;
    depthBufferDesc.size.depth = 1;
    depthBufferDesc.format = gfx::Format::D32_FLOAT;
    depthBufferDesc.defaultState = gfx::ResourceState::DepthWrite;
    depthBufferDesc.allowedStates = gfx::ResourceStateSet(gfx::ResourceState::DepthWrite);
    gfx::ClearValue depthClearValue = {};
    depthBufferDesc.optimalClearValue = &depthClearValue;
    return device->createTextureResource(depthBufferDesc, nullptr);
}

// Create depth stencil view with unified configuration
Slang::ComPtr<gfx::IResourceView> SlangDisplay::createDepthStencilView(uint32_t width, uint32_t height) {
    auto depthBuffer = createDepthBuffer(width, height);
    
    gfx::IResourceView::Desc depthBufferViewDesc;
    memset(&depthBufferViewDesc, 0, sizeof(depthBufferViewDesc)); // Like triangle example
    depthBufferViewDesc.format = gfx::Format::D32_FLOAT;
    depthBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
    depthBufferViewDesc.type = gfx::IResourceView::Type::DepthStencil;
    return device->createTextureView(depthBuffer.get(), depthBufferViewDesc);
}

// Create framebuffer with unified descriptor
Slang::ComPtr<gfx::IFramebuffer> SlangDisplay::createFramebuffer(gfx::IResourceView* rtv, gfx::IResourceView* dsv) {
    gfx::IFramebuffer::Desc framebufferDesc;
    framebufferDesc.renderTargetCount = 1;
    framebufferDesc.depthStencilView = dsv;
    framebufferDesc.renderTargetViews = &rtv;
    framebufferDesc.layout = framebufferLayout;
    return device->createFramebuffer(framebufferDesc);
}

// Create single framebuffer for given image index
void SlangDisplay::createSingleFramebuffer(uint32_t imageIndex, gfx::Format format, uint32_t width, uint32_t height) {
    // Get swapchain image
    ComPtr<gfx::ITextureResource> colorBuffer;
    swapchain->getImage(imageIndex, colorBuffer.writeRef());
    
    // Create render target view with ACTUAL format (CRITICAL FIX)
    auto rtv = createRenderTargetView(colorBuffer.get(), format);
    renderTargetViews.push_back(rtv);
    
    // Create depth stencil view with unified configuration
    auto dsv = createDepthStencilView(width, height);
    
    // Create framebuffer with unified descriptor
    auto framebuffer = createFramebuffer(rtv.get(), dsv.get());
    framebuffers.push_back(framebuffer);
}

// Unified framebuffer creation method that both constructor and resize can call
void SlangDisplay::createSwapchainFramebuffers() {
    const auto& swapchainDesc = swapchain->getDesc();
    uint32_t imageCount = swapchainDesc.imageCount;
    gfx::Format format = swapchainDesc.format;
    
    // Clear and reserve (common pattern)
    framebuffers.clear();
    renderTargetViews.clear();
    framebuffers.reserve(imageCount);
    renderTargetViews.reserve(imageCount);
    
    // Unified framebuffer creation loop
    for (uint32_t i = 0; i < imageCount; ++i) {
        createSingleFramebuffer(i, format, swapchainDesc.width, swapchainDesc.height);
    }
}

// =============================================================================
// SHADER VALIDATION IMPLEMENTATION (Standard Complexity)
// =============================================================================

void SlangDisplay::initShaderValidation() {

    // Get device info for API-specific handling
    const gfx::DeviceInfo& deviceInfo = device->getDeviceInfo();
    gfx::DeviceType deviceType = deviceInfo.deviceType;
    
    // Load and compile Slang shader using proper cross-platform approach
    if (!loadSlangShader(deviceType)) {
        // Initialize fallback validation data
        return;
    }
}

bool SlangDisplay::loadSlangShader(gfx::DeviceType deviceType) {
    // Step 1: Get device's Slang session
    ComPtr<slang::ISession> slangSession;
    slangSession = device->getSlangSession();
    if (!slangSession) {
        return false;
    }
    
    // Step 2: Load module
    std::string shaderPath = "C:/dev/ChameleonRT/backends/slang/shaders/triangle_shader.slang";
    
    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderPath.c_str(), diagnosticsBlob.writeRef());
    
    if (!module) {
        return false;
    }
    
    // Step 3: Find entry points
    ComPtr<slang::IEntryPoint> vertexEntryPoint;
    SlangResult vertexResult = module->findEntryPointByName("vertexMain", vertexEntryPoint.writeRef());
    if (SLANG_FAILED(vertexResult) || !vertexEntryPoint) {
        return false;
    }
    
    ComPtr<slang::IEntryPoint> fragmentEntryPoint;
    SlangResult fragResult = module->findEntryPointByName("fragmentMain", fragmentEntryPoint.writeRef());
    if (SLANG_FAILED(fragResult) || !fragmentEntryPoint) {
        return false;
    }
    
    // Step 4: Create composite component
    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);
    
    // Record entry point indices
    int entryPointCount = 0;
    int vertexEntryPointIndex = entryPointCount++;
    componentTypes.push_back(vertexEntryPoint);
    
    int fragmentEntryPointIndex = entryPointCount++;
    componentTypes.push_back(fragmentEntryPoint);
    
    // Create composite
    ComPtr<slang::IComponentType> linkedProgram;
    SlangResult result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        (SlangInt)componentTypes.size(),
        linkedProgram.writeRef(),
        diagnosticsBlob.writeRef());
        
    if (SLANG_FAILED(result) || !linkedProgram) {
        return false;
    }
    
    // Step 5: Extract compiled bytecode directly from Slang
    SlangInt targetIndex = 0;
    
    // Extract vertex shader bytecode
    ComPtr<ISlangBlob> vertexShaderBlob;
    SlangResult vertexBytecodeResult = linkedProgram->getEntryPointCode(
        vertexEntryPointIndex,
        targetIndex,
        vertexShaderBlob.writeRef(),
        diagnosticsBlob.writeRef()
    );
    
    if (SLANG_FAILED(vertexBytecodeResult) || !vertexShaderBlob) {
        return false;
    }
    
    // Extract fragment shader bytecode
    ComPtr<ISlangBlob> fragmentShaderBlob;
    SlangResult fragmentBytecodeResult = linkedProgram->getEntryPointCode(
        fragmentEntryPointIndex,
        targetIndex,
        fragmentShaderBlob.writeRef(),
        diagnosticsBlob.writeRef()
    );
    
    if (SLANG_FAILED(fragmentBytecodeResult) || !fragmentShaderBlob) {
        return false;
    }
    
    // Create GFX shader program from extracted bytecode
    gfx::IShaderProgram::Desc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram.get();
    
    ComPtr<ISlangBlob> programDiagnosticsBlob;
    SlangResult programResult = device->createProgram(
        programDesc, 
        m_validation.validationShaderProgram.writeRef(),
        programDiagnosticsBlob.writeRef()
    );
    
    if (SLANG_FAILED(programResult) || !m_validation.validationShaderProgram) {
        // Detailed error analysis for D3D12
// #ifndef USE_VULKAN
//         if (programResult == -2147467259) { // E_FAIL
//             // Fallback approach: Try creating program with minimal descriptor
//             gfx::IShaderProgram::Desc simplifiedDesc = {};
//             simplifiedDesc.slangGlobalScope = linkedProgram.get();
            
//             ComPtr<ISlangBlob> fallbackDiagnostics;
//             SlangResult fallbackResult = device->createProgram(
//                 simplifiedDesc,
//                 m_validation.validationShaderProgram.writeRef(),
//                 fallbackDiagnostics.writeRef()
//             );
            
//             if (SLANG_SUCCEEDED(fallbackResult) && m_validation.validationShaderProgram) {
//                 return true;
//             } else {
//                 return false;
//             }
//         } else {
//             return false;
//         }
// #else
//         return false;
// #endif
    }
    
    return true;
}

void SlangDisplay::cleanupShaderValidation() {

}

// =============================================================================
// TRIANGLE RENDERING IMPLEMENTATION (Step 3: Following triangle example pattern)
// =============================================================================

// Triangle vertex data (EXACT triangle example coordinates)
struct Vertex {
    float position[3];
    float color[3];
};

static const int kVertexCount = 3;
static const Vertex kVertexData[kVertexCount] = {
    {{-0.8f, -0.8f, 0.0f}, {1, 0, 0}},   // Large red triangle - bottom left
    {{ 0.0f,  0.8f, 0.0f}, {0, 1, 0}},   // Large green triangle - top center
    {{ 0.8f, -0.8f, 0.0f}, {0, 0, 1}},   // Large blue triangle - bottom right
};

bool SlangDisplay::initTriangleRendering() {
    // Check if shader program is available
    if (!m_validation.validationShaderProgram) {
        return false;
    }
    
    // Step 1: Create input layout
    gfx::InputElementDesc inputElements[] = {
        {"POSITION", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, position)},
        {"COLOR", 0, gfx::Format::R32G32B32_FLOAT, offsetof(Vertex, color)},
    };
    
    m_triangle.triangleInputLayout = device->createInputLayout(sizeof(Vertex), &inputElements[0], 2);
    if (!m_triangle.triangleInputLayout) {
        return false;
    }
    
    // Step 2: Create vertex buffer
    gfx::IBufferResource::Desc vertexBufferDesc = {};
    vertexBufferDesc.type = gfx::IResource::Type::Buffer;
    vertexBufferDesc.sizeInBytes = kVertexCount * sizeof(Vertex);
    vertexBufferDesc.defaultState = gfx::ResourceState::VertexBuffer;
    
    m_triangle.triangleVertexBuffer = device->createBufferResource(vertexBufferDesc, &kVertexData[0]);
    if (!m_triangle.triangleVertexBuffer) {
        return false;
    }
    
    // Step 3: Create uniform buffer for MVP matrix
    gfx::IBufferResource::Desc uniformBufferDesc = {};
    uniformBufferDesc.type = gfx::IResource::Type::Buffer;
    uniformBufferDesc.sizeInBytes = 16 * sizeof(float); // 4x4 matrix
    uniformBufferDesc.defaultState = gfx::ResourceState::ConstantBuffer;
    
    // Identity matrix for now
    float identityMatrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    
    m_triangle.triangleUniformBuffer = device->createBufferResource(uniformBufferDesc, &identityMatrix[0]);
    if (!m_triangle.triangleUniformBuffer) {
        return false;
    }
    
    // Step 4: Create graphics pipeline state
    gfx::GraphicsPipelineStateDesc pipelineDesc = {};
    pipelineDesc.inputLayout = m_triangle.triangleInputLayout;
    pipelineDesc.program = m_validation.validationShaderProgram;
    pipelineDesc.framebufferLayout = framebufferLayout;
    
    // Add explicit render state
    pipelineDesc.depthStencil.depthTestEnable = false;
    pipelineDesc.depthStencil.depthWriteEnable = false;
    pipelineDesc.rasterizer.cullMode = gfx::CullMode::None;
    pipelineDesc.rasterizer.fillMode = gfx::FillMode::Solid;
    pipelineDesc.rasterizer.frontFace = gfx::FrontFaceMode::CounterClockwise;
    
    m_triangle.trianglePipeline = device->createGraphicsPipelineState(pipelineDesc);
    if (!m_triangle.trianglePipeline) {
        return false;
    }
    
    m_triangle.triangleInitialized = true;
    return true;
}

void SlangDisplay::renderTriangle(gfx::IRenderCommandEncoder* renderEncoder) {
    if (!m_triangle.triangleInitialized) {
        return;
    }
    
    // Step 1: Bind pipeline and get root shader object
    auto rootObject = renderEncoder->bindPipeline(m_triangle.trianglePipeline);
    
    // Step 2: Bind uniform data
    if (rootObject) {
        const gfx::DeviceInfo& deviceInfo = device->getDeviceInfo();
        
        // Bind matrix uniform
        gfx::ShaderOffset uniformsOffset = {};
        uniformsOffset.bindingRangeIndex = 0;
        uniformsOffset.bindingArrayIndex = 0;  
        uniformsOffset.uniformOffset = 0;
        
        rootObject->setData(uniformsOffset, deviceInfo.identityProjectionMatrix, 16 * sizeof(float));
    }
    
    // Step 3: Set vertex buffer and draw
    renderEncoder->setVertexBuffer(0, m_triangle.triangleVertexBuffer);
    renderEncoder->setPrimitiveTopology(gfx::PrimitiveTopology::TriangleList);
    
    // Step 4: Draw the triangle
    renderEncoder->draw(3);
}

void SlangDisplay::cleanupTriangleRendering() {
    m_triangle.trianglePipeline = nullptr;
    m_triangle.triangleVertexBuffer = nullptr;
    m_triangle.triangleUniformBuffer = nullptr;
    m_triangle.triangleInputLayout = nullptr;
    m_triangle.triangleInitialized = false;
    std::cout << "Triangle rendering resources cleaned up" << std::endl;
}

