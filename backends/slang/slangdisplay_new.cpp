#include "slangdisplay.h"
#include <slang.h>
#include <slang-gfx.h>
#include <SDL_syswm.h>
#include <imgui.h>
#include "util/display/imgui_impl_sdl.h"
#include <iostream>
#include <fstream>

using namespace Slang;
using namespace gfx;

SlangDisplay::SlangDisplay(SDL_Window* sdl_window) : window(sdl_window) {
    // Initialize like triangle example, but with SDL window integration
    
    // 1. Extract native window handle from SDL
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(sdl_window, &wm_info);

    // 2. Create device using triangle example pattern (unified for both APIs)
    gfx::IDevice::Desc deviceDesc = {};
#ifdef USE_VULKAN
    deviceDesc.deviceType = DeviceType::Vulkan;
#else
    deviceDesc.deviceType = DeviceType::DirectX12;
#endif
    
    gfx::Result res = gfxCreateDevice(&deviceDesc, device.writeRef());
    if (SLANG_FAILED(res)) {
        throw std::runtime_error("Failed to create GFX device");
    }

    // 3. Create command queue (like triangle example)
    gfx::ICommandQueue::Desc queueDesc = {};
    queueDesc.type = gfx::ICommandQueue::QueueType::Graphics;
    queue = device->createCommandQueue(queueDesc);

    // 4. Create swapchain (triangle example + SDL integration)
    gfx::ISwapchain::Desc swapchainDesc = {};
    swapchainDesc.format = gfx::Format::R8G8B8A8_UNORM;
    swapchainDesc.width = 1280;  // Will be updated with actual window size
    swapchainDesc.height = 720;
    swapchainDesc.imageCount = 2;
    swapchainDesc.queue = queue;
    
    // Extract SDL native window handle
    gfx::WindowHandle windowHandle = {};
#ifdef _WIN32
    windowHandle = gfx::WindowHandle::FromHwnd(wm_info.info.win.window);
#elif defined(__linux__)
    windowHandle = gfx::WindowHandle::FromXWindow(wm_info.info.x11.display, wm_info.info.x11.window);
#endif
    
    swapchain = device->createSwapchain(swapchainDesc, windowHandle);

    // 5. Create framebuffer layout (like triangle example - simplified, no depth)
    gfx::IFramebufferLayout::TargetLayout renderTargetLayout = {gfx::Format::R8G8B8A8_UNORM, 1};
    gfx::IFramebufferLayout::Desc framebufferLayoutDesc = {};
    framebufferLayoutDesc.renderTargetCount = 1;
    framebufferLayoutDesc.renderTargets = &renderTargetLayout;
    framebufferLayoutDesc.depthStencil = nullptr;  // No depth for simplicity
    device->createFramebufferLayout(framebufferLayoutDesc, framebufferLayout.writeRef());

    // 6. Create render pass layout (required for proper rendering)
    gfx::IRenderPassLayout::Desc renderPassDesc = {};
    renderPassDesc.framebufferLayout = framebufferLayout;
    renderPassDesc.renderTargetCount = 1;
    gfx::IRenderPassLayout::TargetAccessDesc renderTargetAccess = {};
    renderTargetAccess.loadOp = gfx::IRenderPassLayout::TargetLoadOp::Clear;
    renderTargetAccess.storeOp = gfx::IRenderPassLayout::TargetStoreOp::Store;
    renderTargetAccess.initialState = gfx::ResourceState::Undefined;
    renderTargetAccess.finalState = gfx::ResourceState::Present;
    renderPassDesc.renderTargetAccess = &renderTargetAccess;
    renderPassDesc.depthStencilAccess = nullptr;
    device->createRenderPassLayout(renderPassDesc, renderPassLayout.writeRef());

    // 7. Create framebuffers for swapchain images (like triangle example)
    createSwapchainFramebuffers();

    // 8. Create transient heaps (like triangle example)
    const uint32_t imageCount = 2;  // Fixed like triangle example
    for (uint32_t i = 0; i < imageCount; i++) {
        gfx::ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096 * 1024;
        auto transientHeap = device->createTransientResourceHeap(transientHeapDesc);
        transientHeaps.add(transientHeap);
    }

    // Initialize shader validation (triangle example pattern)
    initShaderValidation();
}

void SlangDisplay::createSwapchainFramebuffers() {
    // Create framebuffers like triangle example
    framebuffers.clear();
    
    const auto& swapchainDesc = swapchain->getDesc();
    for (uint32_t i = 0; i < swapchainDesc.imageCount; i++) {
        ComPtr<gfx::ITextureResource> colorBuffer;
        swapchain->getImage(i, colorBuffer.writeRef());
        
        gfx::IResourceView::Desc colorBufferViewDesc = {};
        colorBufferViewDesc.format = swapchainDesc.format;
        colorBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        colorBufferViewDesc.type = gfx::IResourceView::Type::RenderTarget;
        ComPtr<gfx::IResourceView> rtv = device->createTextureView(colorBuffer.get(), colorBufferViewDesc);

        gfx::IFramebuffer::Desc framebufferDesc = {};
        framebufferDesc.renderTargetCount = 1;
        framebufferDesc.depthStencilView = nullptr;
        framebufferDesc.renderTargetViews = rtv.readRef();
        framebufferDesc.layout = framebufferLayout;
        ComPtr<gfx::IFramebuffer> framebuffer = device->createFramebuffer(framebufferDesc);
        
        framebuffers.add(framebuffer);
    }
}

SlangDisplay::~SlangDisplay() {
    // Clean shutdown like triangle example
    if (queue) {
        queue->waitOnHost();
    }
    
    // Cleanup shader validation
    cleanupShaderValidation();
    
    // Clear resources
    transientHeaps.clear();
    framebuffers.clear();
    swapchain = nullptr;
    framebufferLayout = nullptr;
    renderPassLayout = nullptr;
    queue = nullptr;
    device = nullptr;
}

std::string SlangDisplay::gpu_brand() {
    if (device) {
        const gfx::DeviceInfo& info = device->getDeviceInfo();
        if (info.adapterName) {
            return std::string(info.adapterName);
        }
    }
    return "Slang GFX";
}

std::string SlangDisplay::name() {
    return "SlangDisplay (Triangle Example Pattern)";
}

void SlangDisplay::resize(const int width, const int height) {
    if (swapchain) {
        swapchain->resize(width, height);
        createSwapchainFramebuffers();
    }
}

void SlangDisplay::new_frame() {
    // Simple new frame like triangle example
    // ImGui setup is handled by main application
}

void SlangDisplay::display(RenderBackend* backend) {
    // Triangle example rendering pattern
    try {
        // Acquire next frame (like triangle example mainLoop)
        int frameBufferIndex = swapchain->acquireNextImage();
        m_currentFrameIndex = frameBufferIndex;
        
        // Reset transient heap and create command buffer (like triangle example)
        transientHeaps[frameBufferIndex]->synchronizeAndReset();
        ComPtr<ICommandBuffer> commandBuffer = transientHeaps[frameBufferIndex]->createCommandBuffer();
        
        // Encode render commands (like triangle example renderFrame)
        auto renderEncoder = commandBuffer->encodeRenderCommands(renderPassLayout, framebuffers[frameBufferIndex]);
        
        // Render our content (validation shaders or simple clearing)
        renderFrame(renderEncoder);
        
        renderEncoder->endEncoding();
        commandBuffer->close();
        
        // Execute and present (like triangle example)
        queue->executeCommandBuffer(commandBuffer);
        transientHeaps[frameBufferIndex]->finish();
        
        if (!isTestMode()) {
            swapchain->present();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Display error: " << e.what() << std::endl;
        throw;
    }
}

void SlangDisplay::renderFrame(gfx::IRenderCommandEncoder* renderEncoder) {
    // Triangle example rendering pattern
    
    // Set viewport like triangle example
    gfx::Viewport viewport = {};
    viewport.maxZ = 1.0f;
    viewport.extentX = 1280.0f;  // Should be actual window width
    viewport.extentY = 720.0f;   // Should be actual window height
    renderEncoder->setViewportAndScissor(viewport);
    
    // Run shader validation or simple clearing
    if (m_validation.validationActive && m_validation.shaderProgram) {
        renderWithShaderProgram(renderEncoder);
    } else {
        // Fallback: just clear (no drawing needed for basic validation)
        // Clear is automatic via render pass load op
    }
}

void SlangDisplay::renderWithShaderProgram(gfx::IRenderCommandEncoder* renderEncoder) {
    // Triangle example shader rendering pattern
    if (!m_validation.pipelineState) {
        return; // No pipeline to render with
    }
    
    // Bind pipeline like triangle example
    auto rootObject = renderEncoder->bindPipeline(m_validation.pipelineState);
    
    // Update time uniform (simple like triangle example)
    auto now = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(now - m_validation.startTime).count();
    
    // Set shader parameters (simplified)
    if (m_validation.timeBuffer) {
        // Map and update time buffer
        void* mappedData = nullptr;
        if (SLANG_SUCCEEDED(m_validation.timeBuffer->map(nullptr, &mappedData))) {
            *(float*)mappedData = time;
            m_validation.timeBuffer->unmap(nullptr);
        }
    }
    
    // Draw fullscreen triangle (like many rendering examples)
    renderEncoder->setPrimitiveTopology(PrimitiveTopology::TriangleList);
    renderEncoder->draw(3); // Fullscreen triangle
}

gfx::ICommandBuffer* SlangDisplay::getCurrentCommandBuffer() {
    // Simple accessor
    if (m_currentFrameIndex >= 0 && m_currentFrameIndex < (int)transientHeaps.getCount()) {
        return transientHeaps[m_currentFrameIndex]->createCommandBuffer().get();
    }
    return nullptr;
}

gfx::IFramebuffer* SlangDisplay::getCurrentFramebuffer() {
    if (m_currentFrameIndex >= 0 && m_currentFrameIndex < (int)framebuffers.getCount()) {
        return framebuffers[m_currentFrameIndex].get();
    }
    return nullptr;
}

bool SlangDisplay::isTestMode() {
    return false; // Simple implementation
}

// =============================================================================
// SHADER VALIDATION - Triangle Example Pattern
// =============================================================================

void SlangDisplay::initShaderValidation() {
    std::cout << "Loading Slang shader program..." << std::endl;
    
    // Initialize timing
    m_validation.startTime = std::chrono::high_resolution_clock::now();
    m_validation.validationActive = true;
    
    // Try to load shader program (triangle example pattern)
    if (loadShaderProgram()) {
        std::cout << "✅ Slang shader program loaded successfully" << std::endl;
    } else {
        std::cout << "⚠️ Shader loading failed, using fallback rendering" << std::endl;
    }
}

bool SlangDisplay::loadShaderProgram() {
    // Triangle example loadShaderProgram pattern
    
    // Get Slang session from device
    ComPtr<slang::ISession> slangSession = device->getSlangSession();
    if (!slangSession) {
        std::cerr << "Failed to get Slang session" << std::endl;
        return false;
    }
    
    // Load module (like triangle example)
    ComPtr<slang::IBlob> diagnosticsBlob;
    const char* shaderPath = "render_slang_flat.slang";  // Use existing shader
    slang::IModule* module = slangSession->loadModule(shaderPath, diagnosticsBlob.writeRef());
    
    if (diagnosticsBlob && diagnosticsBlob->getBufferSize() > 0) {
        std::cout << "Slang diagnostics: " << (const char*)diagnosticsBlob->getBufferPointer() << std::endl;
    }
    
    if (!module) {
        std::cerr << "Failed to load Slang module: " << shaderPath << std::endl;
        return false;
    }
    
    // Find entry points (triangle example pattern)
    ComPtr<slang::IEntryPoint> vertexEntryPoint;
    if (SLANG_FAILED(module->findEntryPointByName("vertexMain", vertexEntryPoint.writeRef()))) {
        std::cerr << "Failed to find vertex entry point" << std::endl;
        return false;
    }
    
    ComPtr<slang::IEntryPoint> fragmentEntryPoint;
    if (SLANG_FAILED(module->findEntryPointByName("fragmentMain", fragmentEntryPoint.writeRef()))) {
        std::cerr << "Failed to find fragment entry point" << std::endl;
        return false;
    }
    
    // Create composite (triangle example pattern)
    Slang::List<slang::IComponentType*> componentTypes;
    componentTypes.add(module);
    componentTypes.add(vertexEntryPoint);
    componentTypes.add(fragmentEntryPoint);
    
    ComPtr<slang::IComponentType> linkedProgram;
    SlangResult result = slangSession->createCompositeComponentType(
        componentTypes.getBuffer(),
        componentTypes.getCount(),
        linkedProgram.writeRef(),
        diagnosticsBlob.writeRef());
        
    if (SLANG_FAILED(result)) {
        std::cerr << "Failed to create composite component" << std::endl;
        return false;
    }
    
    // Create program (triangle example pattern)
    gfx::IShaderProgram::Desc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram;
    if (SLANG_FAILED(device->createProgram(programDesc, m_validation.shaderProgram.writeRef()))) {
        std::cerr << "Failed to create shader program" << std::endl;
        return false;
    }
    
    // Create pipeline state (triangle example pattern)
    if (!createPipelineState()) {
        std::cerr << "Failed to create pipeline state" << std::endl;
        return false;
    }
    
    // Create time buffer for animation
    createTimeBuffer();
    
    return true;
}

bool SlangDisplay::createPipelineState() {
    // Triangle example pipeline creation pattern
    
    // Create input layout (none needed for fullscreen triangle)
    auto inputLayout = device->createInputLayout(0, nullptr, 0);
    
    // Create graphics pipeline (like triangle example)
    GraphicsPipelineStateDesc desc = {};
    desc.inputLayout = inputLayout;
    desc.program = m_validation.shaderProgram;
    desc.framebufferLayout = framebufferLayout;
    
    auto pipelineState = device->createGraphicsPipelineState(desc);
    if (!pipelineState) {
        return false;
    }
    
    m_validation.pipelineState = pipelineState;
    return true;
}

void SlangDisplay::createTimeBuffer() {
    // Create simple time uniform buffer
    IBufferResource::Desc bufferDesc = {};
    bufferDesc.type = IResource::Type::Buffer;
    bufferDesc.sizeInBytes = sizeof(float);
    bufferDesc.defaultState = ResourceState::ConstantBuffer;
    bufferDesc.allowedStates = ResourceStateSet(ResourceState::ConstantBuffer);
    
    float initialTime = 0.0f;
    m_validation.timeBuffer = device->createBufferResource(bufferDesc, &initialTime);
}

void SlangDisplay::cleanupShaderValidation() {
    m_validation.shaderProgram = nullptr;
    m_validation.pipelineState = nullptr;
    m_validation.timeBuffer = nullptr;
    m_validation.validationActive = false;
}
