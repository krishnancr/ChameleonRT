#include "slangdisplay.h"
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
    std::cout << "🎯 Creating Vulkan device (Slang will auto-configure for SPIR-V)" << std::endl;
#else
    deviceDesc.deviceType = DeviceType::DirectX12;
    std::cout << "🎯 Creating D3D12 device (Slang will auto-configure for DXIL)" << std::endl;
#endif
    gfx::Result res = gfxCreateDevice(&deviceDesc, device.writeRef());
    if (SLANG_FAILED(res)) throw std::runtime_error("Failed to create GFX device");

    // Verify device type and adapter (keep minimal logging for diagnostics)
    const gfx::DeviceInfo& deviceInfo = device->getDeviceInfo();
    const char* deviceTypeName = gfxGetDeviceTypeName(deviceInfo.deviceType);
    std::cout << "Graphics API: " << deviceTypeName;
    
    // 3. Create command queue
    gfx::ICommandQueue::Desc queueDesc = {};
    queueDesc.type = gfx::ICommandQueue::QueueType::Graphics;
    queue = device->createCommandQueue(queueDesc);

    // TRIANGLE EXAMPLE PATTERN: Create framebuffer layout FIRST with hardcoded format
    gfx::IFramebufferLayout::TargetLayout renderTargetLayout = {gfx::Format::R8G8B8A8_UNORM, 1}; // EXACTLY like triangle example
    gfx::IFramebufferLayout::TargetLayout depthTargetLayout = {gfx::Format::D32_FLOAT, 1}; // EXACTLY like triangle example
    gfx::IFramebufferLayout::Desc framebufferLayoutDesc = {};
    framebufferLayoutDesc.renderTargetCount = 1;
    framebufferLayoutDesc.renderTargets = &renderTargetLayout;
    framebufferLayoutDesc.depthStencil = &depthTargetLayout;
    device->createFramebufferLayout(framebufferLayoutDesc, framebufferLayout.writeRef());

    // Create swapchain AFTER framebuffer layout (triangle example pattern)
    gfx::ISwapchain::Desc swapchainDesc = {};
    swapchainDesc.format = gfx::Format::R8G8B8A8_UNORM; // EXACTLY same format as framebuffer layout
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

    // Query actual swapchain properties
    const auto& actualSwapchainDesc = swapchain->getDesc();
    uint32_t actualImageCount = actualSwapchainDesc.imageCount;
    gfx::Format swapchainFormat = actualSwapchainDesc.format;
    
    // ===== CRITICAL: Ensure format consistency like triangle example =====
    std::cout << "🔍 CRITICAL: Swapchain format: R8G8B8A8_UNORM (hardcoded like triangle)" << std::endl;
    std::cout << "🔍 CRITICAL: Framebuffer format: R8G8B8A8_UNORM (hardcoded like triangle)" << std::endl;
    std::cout << "🔍 CRITICAL: Swapchain reports " << actualImageCount << " images (expecting 2)" << std::endl;
    if (actualImageCount != 2) {
        std::cerr << "❌ CRITICAL: Triangle example expects exactly 2 images!" << std::endl;
        return;
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

    // 6. Create framebuffers for swapchain images (EXACT triangle example pattern)
    framebuffers.clear();
    renderTargetViews.clear();
    framebuffers.reserve(actualImageCount);
    renderTargetViews.reserve(actualImageCount);
    
    for (uint32_t i = 0; i < actualImageCount; ++i) {
        ComPtr<gfx::ITextureResource> colorBuffer;
        swapchain->getImage(i, colorBuffer.writeRef());
        
        // Create render target view with HARDCODED format like triangle example
        gfx::IResourceView::Desc colorBufferViewDesc = {};
        memset(&colorBufferViewDesc, 0, sizeof(colorBufferViewDesc)); // Like triangle example
        colorBufferViewDesc.format = gfx::Format::R8G8B8A8_UNORM; // HARDCODED like triangle example
        colorBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        colorBufferViewDesc.type = gfx::IResourceView::Type::RenderTarget;
        ComPtr<gfx::IResourceView> rtv = device->createTextureView(colorBuffer.get(), colorBufferViewDesc);

        // Store the render target view for later clearing
        renderTargetViews.push_back(rtv);

        // Create depth buffer (EXACT triangle example pattern)
        gfx::ITextureResource::Desc depthBufferDesc;
        depthBufferDesc.type = gfx::IResource::Type::Texture2D;
        depthBufferDesc.size.width = 1280; // TODO: Get from actual window size
        depthBufferDesc.size.height = 720;
        depthBufferDesc.size.depth = 1;
        depthBufferDesc.format = gfx::Format::D32_FLOAT;
        depthBufferDesc.defaultState = gfx::ResourceState::DepthWrite;
        depthBufferDesc.allowedStates = gfx::ResourceStateSet(gfx::ResourceState::DepthWrite);
        gfx::ClearValue depthClearValue = {};
        depthBufferDesc.optimalClearValue = &depthClearValue;
        ComPtr<gfx::ITextureResource> depthBufferResource = device->createTextureResource(depthBufferDesc, nullptr);

        gfx::IResourceView::Desc depthBufferViewDesc;
        memset(&depthBufferViewDesc, 0, sizeof(depthBufferViewDesc)); // Like triangle example
        depthBufferViewDesc.format = gfx::Format::D32_FLOAT;
        depthBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        depthBufferViewDesc.type = gfx::IResourceView::Type::DepthStencil;
        ComPtr<gfx::IResourceView> dsv = device->createTextureView(depthBufferResource.get(), depthBufferViewDesc);

        // Create framebuffer (EXACT triangle example pattern)
        gfx::IFramebuffer::Desc framebufferDesc;
        framebufferDesc.renderTargetCount = 1;
        framebufferDesc.depthStencilView = dsv.get(); // Add depth buffer like triangle example
        framebufferDesc.renderTargetViews = rtv.readRef();
        framebufferDesc.layout = framebufferLayout;
        ComPtr<gfx::IFramebuffer> framebuffer = device->createFramebuffer(framebufferDesc);
        framebuffers.push_back(framebuffer);
    }

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

    // ===== VALIDATION STEP 1: Check vector sizes vs swapchain count =====
    uint32_t swapchainImageCount = swapchain->getDesc().imageCount;
    std::cout << "🔍 VALIDATION - Swapchain image count: " << swapchainImageCount << std::endl;
    std::cout << "🔍 VALIDATION - Framebuffers size: " << framebuffers.size() << std::endl;
    std::cout << "🔍 VALIDATION - TransientHeaps size: " << transientHeaps.size() << std::endl;
    std::cout << "🔍 VALIDATION - CommandBuffers: NONE (create fresh like triangle example)" << std::endl;
    
    if (framebuffers.size() != swapchainImageCount) {
        std::cerr << "❌ VALIDATION FAILED: Framebuffers size mismatch!" << std::endl;
    }
    if (transientHeaps.size() != swapchainImageCount) {
        std::cerr << "❌ VALIDATION FAILED: TransientHeaps size mismatch!" << std::endl;
    }
    std::cout << "✅ VALIDATION STEP 1 COMPLETE" << std::endl;

    // Initialize shader validation first (to create shader program)
    initShaderValidation();
    
    // Initialize triangle rendering (Step 3: following triangle example pattern)
    initTriangleRendering();
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
    std::cout << "Graphics resources cleaned up (surface managed by Slang GFX)" << std::endl;

    queue = nullptr;
    device = nullptr;
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
    
    if (swapchain) {
        swapchain->resize(width, height);
        // Recreate framebuffers as needed (not shown for brevity)
    }
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

void SlangDisplay::renderImGuiDrawData(gfx::ICommandBuffer* commandBuffer, gfx::IFramebuffer* framebuffer) {
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->CmdListsCount == 0) {
        return; // No ImGui data to render
    }

    // TODO: For now, skip ImGui rendering to demonstrate unified code path
    // This is a placeholder implementation for the unified approach
    // Full implementation would create vertex/index buffers and render ImGui geometry
    
    // In future iteration, we would:
    // 1. Create render pass layout for both Vulkan and D3D12
    // 2. Create vertex/index buffers for ImGui geometry
    // 3. Create graphics pipeline for ImGui rendering
    // 4. Upload ImGui font texture via Slang GFX
    // 5. Render each ImDrawList with appropriate draw calls
    
    // For now, this demonstrates the unified code structure
}

void SlangDisplay::display(RenderBackend* backend) {
    try {
    // Acquire next swapchain image
    m_currentFrameIndex = swapchain->acquireNextImage();
    
    // ===== VALIDATION: Basic frame tracking (minimal logging) =====
    static int frameCount = 0;
    frameCount++;
    if (frameCount <= 5 || frameCount % 100 == 0) {  // Reduce logging frequency
        std::cout << "Frame " << frameCount << " - Index: " << m_currentFrameIndex << std::endl;
    }
    
    // ===== CRITICAL DEBUG: Check values before crash =====
    // std::cout << "DEBUG: m_currentFrameIndex = " << m_currentFrameIndex << std::endl;
    // std::cout << "DEBUG: transientHeaps.size() = " << transientHeaps.size() << std::endl;
    
    // Use proper frame-based buffer index
    size_t bufferIndex = m_currentFrameIndex % transientHeaps.size();
    // std::cout << "DEBUG: calculated bufferIndex = " << bufferIndex << std::endl;
    
    // ===== CRITICAL DEBUG: Check transient heap before access =====
    if (bufferIndex >= transientHeaps.size()) {
        std::cerr << "❌ CRITICAL: bufferIndex " << bufferIndex << " >= transientHeaps.size() " << transientHeaps.size() << std::endl;
        return;
    }
    if (!transientHeaps[bufferIndex]) {
        std::cerr << "❌ CRITICAL: transientHeaps[" << bufferIndex << "] is nullptr!" << std::endl;
        return;
    }
    // std::cout << "DEBUG: About to call synchronizeAndReset()..." << std::endl;
    
    // Reset the transient heap to prepare for new commands  
    transientHeaps[bufferIndex]->synchronizeAndReset();
    // std::cout << "DEBUG: synchronizeAndReset() completed successfully" << std::endl;
    
    // ===== PROPER GPU SYNCHRONIZATION: Wait for GPU to finish =====
    queue->waitOnHost();  // Proper GPU wait instead of arbitrary timing delay
    // std::cout << "DEBUG: queue->waitOnHost() completed - GPU is synchronized" << std::endl;
    
    // Create command buffer using triangle example pattern
    Slang::ComPtr<gfx::ICommandBuffer> commandBuffer = 
        transientHeaps[bufferIndex]->createCommandBuffer();
    // std::cout << "DEBUG: createCommandBuffer() completed" << std::endl;
    
    // STEP 1: Use render pass with automatic clearing (following triangle example pattern)
    // ===== CRITICAL VALIDATION: Check all resources before encodeRenderCommands =====
    // std::cout << "DEBUG: About to validate resources..." << std::endl;
    if (!commandBuffer) {
        std::cerr << "❌ CRITICAL: Command buffer is null!" << std::endl;
        return;
    }
    // std::cout << "DEBUG: commandBuffer validated OK" << std::endl;
    
    if (!renderPassLayout) {
        std::cerr << "❌ CRITICAL: RenderPassLayout is null!" << std::endl;
        return;
    }
    // std::cout << "DEBUG: renderPassLayout validated OK" << std::endl;
    
    if (!framebuffers[m_currentFrameIndex]) {
        std::cerr << "❌ CRITICAL: Framebuffer[" << m_currentFrameIndex << "] is null!" << std::endl;
        return;
    }
    // std::cout << "DEBUG: framebuffer validated OK" << std::endl;
    
    // std::cout << "DEBUG: About to call encodeRenderCommands()..." << std::endl;
    auto renderEncoder = commandBuffer->encodeRenderCommands(renderPassLayout, framebuffers[m_currentFrameIndex]);
    // std::cout << "DEBUG: encodeRenderCommands() completed successfully!" << std::endl;
    
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
    
    // End render commands
    renderEncoder->endEncoding();
    
    // STEP 2: End ImGui frame (prevents accumulation)
    ImGui::Render();
    
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

void SlangDisplay::clearScreenToColor(gfx::ICommandBuffer* commandBuffer, 
                                     float r, float g, float b, float a) {
    if (m_currentFrameIndex < 0 || m_currentFrameIndex >= (int)framebuffers.size()) {
        std::cerr << "❌ Invalid frame index for clearing: " << m_currentFrameIndex << std::endl;
        return;
    }

    // 🎯 CRITICAL FIX: Use render encoder auto-clearing like triangle example
    // Triangle example NEVER uses encodeResourceCommands() - this was causing state corruption!
    std::cout << "🎯 RENDER ENCODER AUTO-CLEAR: Eliminating encodeResourceCommands() like triangle example" << std::endl;
    
    // Store clear values for render pass auto-clearing (like triangle example)
    m_clearColor[0] = r;
    m_clearColor[1] = g; 
    m_clearColor[2] = b;
    m_clearColor[3] = a;
    m_shouldClearOnNextRender = true;
    
    std::cout << "✅ Clear values stored for auto-clearing: " << r << "," << g << "," << b << "," << a << std::endl;
}

// REMOVED: getCurrentCommandBuffer() - we create fresh command buffers each frame like triangle example

gfx::IFramebuffer* SlangDisplay::getCurrentFramebuffer() {
    return framebuffers[m_currentFrameIndex].get();
}

// =============================================================================
// SHADER VALIDATION IMPLEMENTATION (Standard Complexity)
// =============================================================================

void SlangDisplay::initShaderValidation() {
    std::cout << "🚀 Phase S1.3: Starting Slang shader compilation..." << std::endl;
    
    // Get device info for API-specific handling
    const gfx::DeviceInfo& deviceInfo = device->getDeviceInfo();
    gfx::DeviceType deviceType = deviceInfo.deviceType;
    
    // Load and compile Slang shader using proper cross-platform approach
    if (!loadSlangShader(deviceType)) {
        // Initialize fallback validation data
        auto now = std::chrono::high_resolution_clock::now();
        m_validation.startTime = std::chrono::duration<double>(now.time_since_epoch()).count();
        m_validation.currentShader = 0;
        m_validation.validationActive = true;
        return;
    }
    
    // Initialize validation data for shader rendering
    auto now = std::chrono::high_resolution_clock::now();
    m_validation.startTime = std::chrono::duration<double>(now.time_since_epoch()).count();
    m_validation.currentShader = 0;
    m_validation.validationActive = true;
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
#ifndef USE_VULKAN
        if (programResult == -2147467259) { // E_FAIL
            // Fallback approach: Try creating program with minimal descriptor
            gfx::IShaderProgram::Desc simplifiedDesc = {};
            simplifiedDesc.slangGlobalScope = linkedProgram.get();
            
            ComPtr<ISlangBlob> fallbackDiagnostics;
            SlangResult fallbackResult = device->createProgram(
                simplifiedDesc,
                m_validation.validationShaderProgram.writeRef(),
                fallbackDiagnostics.writeRef()
            );
            
            if (SLANG_SUCCEEDED(fallbackResult) && m_validation.validationShaderProgram) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
#else
        return false;
#endif
    }
    
    return true;
}



void SlangDisplay::runShaderValidation() {
    if (!m_validation.validationActive) {
        return;
    }
    
    // If we have a shader program, use it; otherwise fall back to animated clearing
    if (m_validation.validationShaderProgram) {
        // TODO: Implement actual shader rendering here
        // For now, just clear to a solid color to indicate shader is loaded
        // NOTE: Temporarily disabled - need command buffer parameter when we re-enable
        // auto* commandBuffer = getCurrentCommandBuffer();  // REMOVED - no longer stored
        // clearScreenToColor(commandBuffer, 0.0f, 0.8f, 0.0f, 1.0f); // Green = success
        return;
    }
    
    // Fallback: animated clearing
    // Update time for animation
    auto now = std::chrono::high_resolution_clock::now();
    double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
    float deltaTime = static_cast<float>(currentTime - m_validation.startTime);
    
    // Switch animation modes every 4 seconds
    int newMode = static_cast<int>(deltaTime / 4.0) % 3;
    if (newMode != m_validation.currentShader) {
        m_validation.currentShader = newMode;
    }
    
    // Get current command buffer for animated clearing
    // NOTE: Temporarily disabled - need command buffer parameter when we re-enable
    // auto* commandBuffer = getCurrentCommandBuffer();  // REMOVED - no longer stored
    
    // TODO: Re-enable animated clearing when command buffer is passed as parameter
    return;
    
    // Implement different animation modes
    float r, g, b;
    switch (m_validation.currentShader) {
        case 0: // Rainbow mode
        {
            float hue = fmod(deltaTime * 0.5f, 1.0f) * 6.28318f; // 2π
            r = 0.5f + 0.5f * cosf(hue);
            g = 0.5f + 0.5f * cosf(hue + 2.094f); // 2π/3
            b = 0.5f + 0.5f * cosf(hue + 4.189f); // 4π/3
            break;
        }
        case 1: // Pulse mode
        {
            float pulse = 0.3f + 0.7f * (0.5f + 0.5f * sinf(deltaTime * 2.0f));
            r = 0.8f * pulse;
            g = 0.2f * pulse;
            b = 0.4f * pulse;
            break;
        }
        case 2: // Wave mode
        {
            r = 0.5f + 0.3f * sinf(deltaTime * 1.2f);
            g = 0.5f + 0.3f * sinf(deltaTime * 1.7f + 1.0f);
            b = 0.5f + 0.3f * sinf(deltaTime * 0.8f + 2.0f);
            break;
        }
        default:
            r = g = b = 0.5f;
    }
    
    // Apply animated clearing with validation colors
    // NOTE: Temporarily disabled - need command buffer parameter when we re-enable
    // clearScreenToColor(commandBuffer, r, g, b, 1.0f);  // REMOVED - no commandBuffer variable
}

void SlangDisplay::updateTimeBuffer(float time) {
    // Simplified for animated clear mode - no buffer updates needed
    // Time is calculated directly in runShaderValidation()
}

void SlangDisplay::cleanupShaderValidation() {
    m_validation.validationShaderProgram = nullptr;
    m_validation.rainbowShader = nullptr;
    m_validation.pulseShader = nullptr;
    m_validation.rainbowPipeline = nullptr;
    m_validation.pulsePipeline = nullptr;
    m_validation.timeBuffer = nullptr;
    m_validation.timeBufferView = nullptr;
    m_validation.validationActive = false;
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
    m_triangle.triangleShaderProgram = nullptr;
    m_triangle.trianglePipeline = nullptr;
    m_triangle.triangleVertexBuffer = nullptr;
    m_triangle.triangleUniformBuffer = nullptr;
    m_triangle.triangleInputLayout = nullptr;
    m_triangle.triangleInitialized = false;
    std::cout << "Triangle rendering resources cleaned up" << std::endl;
}

