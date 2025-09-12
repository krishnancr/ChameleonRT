#include "slangdisplay.h"
#include <slang.h>
#include <slang-gfx.h>
#include <SDL_syswm.h>
#include <imgui.h>
#include "util/display/imgui_impl_sdl.h"
#include <iostream>
#include <fstream>

// Note: Not using internal gfx-util/shader-cursor.h - implementing uniform binding with public API

#ifdef USE_VULKAN
// Vulkan includes for direct command access
#include <vulkan/vulkan.h>
#ifdef __linux__
#include <X11/Xlib.h>
#endif
#else  // DX12
#ifdef _WIN32
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>
// Note: No longer including imgui_impl_dx12.h - using unified rendering
#endif
#endif

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

    // We need to know the swapchain format before creating framebuffer layout
    gfx::ISwapchain::Desc swapchainDesc = {};
    swapchainDesc.format = gfx::Format::R8G8B8A8_UNORM;
    swapchainDesc.width = 1280; // Default, should be window size
    swapchainDesc.height = 720;
    swapchainDesc.imageCount = 2;
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

    // 5. Create framebuffer layout using actual swapchain format (Stage 0: no depth)
    gfx::IFramebufferLayout::TargetLayout renderTargetLayout = {swapchainFormat, 1}; // Use actual format
    gfx::IFramebufferLayout::Desc framebufferLayoutDesc = {};
    framebufferLayoutDesc.renderTargetCount = 1;
    framebufferLayoutDesc.renderTargets = &renderTargetLayout;
    framebufferLayoutDesc.depthStencil = nullptr; // Stage 0: No depth buffer
    device->createFramebufferLayout(framebufferLayoutDesc, framebufferLayout.writeRef());

    // 5.1. Create render pass layout for Vulkan (required for proper command buffer encoding)
    gfx::IRenderPassLayout::Desc renderPassLayoutDesc = {};
    renderPassLayoutDesc.framebufferLayout = framebufferLayout;
    renderPassLayoutDesc.renderTargetCount = 1;
    gfx::IRenderPassLayout::TargetAccessDesc renderTargetAccess = {};
    renderTargetAccess.loadOp = gfx::IRenderPassLayout::TargetLoadOp::Clear;  // Clear on load
    renderTargetAccess.storeOp = gfx::IRenderPassLayout::TargetStoreOp::Store; // Store result
    renderTargetAccess.initialState = gfx::ResourceState::Undefined;           // First use
    renderTargetAccess.finalState = gfx::ResourceState::Present;               // For presentation
    
    renderPassLayoutDesc.renderTargetAccess = &renderTargetAccess;
    renderPassLayoutDesc.depthStencilAccess = nullptr; // No depth buffer for Stage 0
    
    auto result = device->createRenderPassLayout(renderPassLayoutDesc, renderPassLayout.writeRef());
    if (SLANG_FAILED(result)) {
        throw std::runtime_error("Failed to create render pass layout");
    }

    // 6. Create framebuffers for swapchain images
    framebuffers.clear();
    renderTargetViews.clear();
    framebuffers.reserve(actualImageCount);
    renderTargetViews.reserve(actualImageCount);
    
    for (uint32_t i = 0; i < actualImageCount; ++i) {
        ComPtr<gfx::ITextureResource> colorBuffer;
        swapchain->getImage(i, colorBuffer.writeRef());
        gfx::IResourceView::Desc colorBufferViewDesc = {};
        colorBufferViewDesc.format = swapchainFormat;  // PHASE 2 FIX: Use actual swapchain format
        colorBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        colorBufferViewDesc.type = gfx::IResourceView::Type::RenderTarget;
        ComPtr<gfx::IResourceView> rtv = device->createTextureView(colorBuffer.get(), colorBufferViewDesc);

        // Store the render target view for later clearing
        renderTargetViews.push_back(rtv);

        // Stage 0: No depth buffer for simplified setup
        gfx::IFramebuffer::Desc framebufferDesc = {};
        framebufferDesc.renderTargetCount = 1;
        framebufferDesc.depthStencilView = nullptr; // No depth buffer for Stage 0
        framebufferDesc.renderTargetViews = rtv.readRef();
        framebufferDesc.layout = framebufferLayout;
        ComPtr<gfx::IFramebuffer> framebuffer = device->createFramebuffer(framebufferDesc);
        framebuffers.push_back(framebuffer);
    }

    // 7. Create transient heaps and command buffers
    transientHeaps.clear();
    commandBuffers.clear();
    transientHeaps.reserve(actualImageCount);
    commandBuffers.reserve(actualImageCount);
    
    // PHASE 2 FIX: Create one transient heap per swapchain image
    for (uint32_t i = 0; i < actualImageCount; ++i) {
        gfx::ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096 * 1024;
        auto transientHeap = device->createTransientResourceHeap(transientHeapDesc);
        transientHeaps.push_back(transientHeap);
        
        // Create command buffer for each frame
        Slang::ComPtr<gfx::ICommandBuffer> commandBuffer;
        transientHeap->createCommandBuffer(commandBuffer.writeRef());
        commandBuffers.push_back(commandBuffer);
    }
    


    // 8. Unified ImGui setup - build font atlas for both backends
    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->IsBuilt()) {
        if (io.Fonts->ConfigData.Size == 0) {
            io.Fonts->AddFontDefault();
        }
        io.Fonts->Build();
    }

#ifndef USE_VULKAN
    // Keep D3D12 descriptor heap for now (will be removed in future iteration)
#ifdef _WIN32
    // Create a descriptor heap for ImGui (D3D12 legacy - to be removed)
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = 1;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12Device* d3d12Device = nullptr;
    
    // Get the native D3D12 device using the proper Slang GFX API
    gfx::IDevice::InteropHandles handles;
    if (SLANG_SUCCEEDED(device->getNativeDeviceHandles(&handles)))
    {
        // Check if the handle is a D3D12 handle
        if (handles.handles[0].api == gfx::InteropHandleAPI::D3D12)
        {
            d3d12Device = (ID3D12Device*)(void*)handles.handles[0].handleValue;
        }
    }
    
    if (d3d12Device)
    {
        HRESULT hr = d3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imgui_desc_heap));
        // TODO: Remove this when full Slang GFX ImGui implementation is complete
    }
#endif
#endif

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
    
    // Clear our command buffers and transient heaps
    commandBuffers.clear();
    transientHeaps.clear();

    // Cleanup triangle rendering resources
    cleanupTriangleRendering();

    // Cleanup shader validation resources
    cleanupShaderValidation();

#ifndef USE_VULKAN
#ifdef _WIN32
    // Clean up D3D12 descriptor heap (legacy - will be removed)
    if (imgui_desc_heap) {
        imgui_desc_heap.Reset();
    }
#endif
#endif

    // Explicitly clear resources in a controlled order
    framebuffers.clear();
    swapchain = nullptr;
    framebufferLayout = nullptr;
    renderPassLayout = nullptr;
    std::cout << "Vulkan resources cleaned up (surface managed by Slang GFX)" << std::endl;

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
    static int frameCount = 0;
    frameCount++;
    
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
    
    // STEP 1: Use render pass with automatic clearing (following triangle example pattern)
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
    
    // End render commands
    renderEncoder->endEncoding();
    
    // STEP 2: End ImGui frame (prevents accumulation)
    ImGui::Render();
    
    // Execute and present
    commandBuffer->close();
    queue->executeCommandBuffers(1, commandBuffer.readRef(), nullptr, 0);
    transientHeaps[bufferIndex]->finish();
    queue->waitOnHost();  // Stage 0 synchronization
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
    

    
#ifdef USE_VULKAN
    // Pure Direct Vulkan clearing using image transition and vkCmdClearColorImage
    if (device->getDeviceInfo().deviceType == gfx::DeviceType::Vulkan) {
        // Get native Vulkan handles
        gfx::IDevice::InteropHandles deviceHandles;
        if (SLANG_SUCCEEDED(device->getNativeDeviceHandles(&deviceHandles)) && 
            deviceHandles.handles[0].api == gfx::InteropHandleAPI::Vulkan) {
            
            VkDevice vkDevice = (VkDevice)deviceHandles.handles[0].handleValue;
            
            gfx::InteropHandle cmdBufHandle;
            if (SLANG_SUCCEEDED(commandBuffer->getNativeHandle(&cmdBufHandle)) &&
                cmdBufHandle.api == gfx::InteropHandleAPI::Vulkan) {
                
                VkCommandBuffer vkCmdBuf = (VkCommandBuffer)cmdBufHandle.handleValue;
                
                // Get the swapchain image texture resource
                ComPtr<gfx::ITextureResource> colorBuffer;
                swapchain->getImage(m_currentFrameIndex, colorBuffer.writeRef());
                
                gfx::InteropHandle imageHandle;
                if (SLANG_SUCCEEDED(colorBuffer->getNativeResourceHandle(&imageHandle)) &&
                    imageHandle.api == gfx::InteropHandleAPI::Vulkan) {
                    
                    VkImage vkImage = (VkImage)imageHandle.handleValue;
                    
                    // Transition image to TRANSFER_DST_OPTIMAL layout for clearing
                    VkImageMemoryBarrier barrier = {};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = vkImage;
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.levelCount = 1;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = 1;
                    barrier.srcAccessMask = 0;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    
                    vkCmdPipelineBarrier(vkCmdBuf,
                                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       0, 0, nullptr, 0, nullptr, 1, &barrier);
                    
                    // Clear the image
                    VkClearColorValue clearColor = {};
                    clearColor.float32[0] = r;
                    clearColor.float32[1] = g;
                    clearColor.float32[2] = b;
                    clearColor.float32[3] = a;
                    
                    VkImageSubresourceRange range = {};
                    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    range.baseMipLevel = 0;
                    range.levelCount = 1;
                    range.baseArrayLayer = 0;
                    range.layerCount = 1;
                    
                    vkCmdClearColorImage(vkCmdBuf, vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       &clearColor, 1, &range);
                    
                    // Transition back to PRESENT_SRC_KHR for presentation
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = 0;
                    
                    vkCmdPipelineBarrier(vkCmdBuf,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                       0, 0, nullptr, 0, nullptr, 1, &barrier);
                    
                    return;
                }
            }
        }
    }
#endif
    
    // D3D12 path: Use resource encoder for clearing
    auto* resourceEncoder = commandBuffer->encodeResourceCommands();
    if (!resourceEncoder) {
        std::cerr << "❌ Failed to create resource encoder for clearing" << std::endl;
        return;
    }
    
    // Set up clear values
    gfx::ClearValue clearValue = {};
    clearValue.color.floatValues[0] = r;
    clearValue.color.floatValues[1] = g;
    clearValue.color.floatValues[2] = b;
    clearValue.color.floatValues[3] = a;
    
    // Clear the render target view
    resourceEncoder->clearResourceView(
        renderTargetViews[m_currentFrameIndex], 
        &clearValue, 
        gfx::ClearResourceViewFlags::FloatClearValues);
    
    // Note: endEncoding() is handled by the command buffer lifecycle
}

gfx::ICommandBuffer* SlangDisplay::getCurrentCommandBuffer() {
    size_t bufferIndex = m_currentFrameIndex % commandBuffers.size();
    return commandBuffers[bufferIndex].get();
}

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
        auto* commandBuffer = getCurrentCommandBuffer();
        clearScreenToColor(commandBuffer, 0.0f, 0.8f, 0.0f, 1.0f); // Green = success
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
    auto* commandBuffer = getCurrentCommandBuffer();
    
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
    clearScreenToColor(commandBuffer, r, g, b, 1.0f);
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

