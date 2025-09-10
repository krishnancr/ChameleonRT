#include "slangdisplay.h"
#include <slang-gfx.h>
#include <SDL_syswm.h>
#include <imgui.h>
#include "util/display/imgui_impl_sdl.h"
#include <iostream>

#ifdef USE_VULKAN
// Vulkan includes are in the header
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
#include <chrono>
#include <thread>

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
    // Use Default and let Slang choose Vulkan based on availability
    // Explicit Vulkan selection can cause issues with device creation
    deviceDesc.deviceType = DeviceType::Vulkan;
#else
    // Keep the original working behavior for DX12
    deviceDesc.deviceType = DeviceType::Default;
#endif
    gfx::Result res = gfxCreateDevice(&deviceDesc, device.writeRef());
    if (SLANG_FAILED(res)) throw std::runtime_error("Failed to create GFX device");

    // **VERIFICATION**: Print actual device type and adapter information
    const gfx::DeviceInfo& deviceInfo = device->getDeviceInfo();
    std::cout << "========================================" << std::endl;
    std::cout << "🔍 GRAPHICS API VERIFICATION:" << std::endl;
    // Use proper Slang GFX API to get device type name
    const char* deviceTypeName = gfxGetDeviceTypeName(deviceInfo.deviceType);
    std::cout << "   Device Type: " << deviceTypeName;
    
#ifdef USE_VULKAN
    std::cout << " (Compiled for: VULKAN)" << std::endl;
#else
    std::cout << " (Compiled for: D3D12)" << std::endl;
#endif
    
    if (deviceInfo.adapterName) {
        std::cout << "   Adapter: " << deviceInfo.adapterName << std::endl;
    }
    std::cout << "========================================" << std::endl;

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
    
    // Platform-first window handle extraction (eliminates API-specific duplication)
    gfx::WindowHandle windowHandle = {};
    
#ifdef _WIN32
    // Windows: Extract HWND from SDL (works for both Vulkan and D3D12)
    HWND hwnd = wm_info.info.win.window;
    windowHandle = gfx::WindowHandle::FromHwnd(hwnd);
    
    #ifdef USE_VULKAN
    std::cout << "Using Windows HWND for Vulkan surface creation" << std::endl;
    #else
    std::cout << "Using Windows HWND for D3D12" << std::endl;
    #endif
    
#elif defined(__linux__)
    // Linux X11: Extract X11 display and window (Vulkan only for now)
    Display* x11Display = wm_info.info.x11.display;
    uint32_t x11Window = wm_info.info.x11.window;
    windowHandle = gfx::WindowHandle::FromXWindow(x11Display, x11Window);
    std::cout << "Using X11 window for Vulkan surface creation" << std::endl;
    
#else
    // Fallback for unsupported platforms
    windowHandle = {};
    std::cout << "Unsupported platform for window handle creation" << std::endl;
#endif
    
    swapchain = device->createSwapchain(swapchainDesc, windowHandle);

    // PHASE 2 FIX: Query actual swapchain properties for format compatibility
    const auto& actualSwapchainDesc = swapchain->getDesc();
    uint32_t actualImageCount = actualSwapchainDesc.imageCount;
    gfx::Format swapchainFormat = actualSwapchainDesc.format;
    
    std::cout << "✅ PHASE 2: Swapchain created with " << actualImageCount << " images, format: " << (int)swapchainFormat << std::endl;

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
    
    std::cout << "Render pass layout created (Stage 0)" << std::endl;

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
    
    std::cout << "Created " << actualImageCount << " transient heaps and command buffers" << std::endl;

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
    // std::cout << "🖼️ SlangDisplay::display - Starting frame " << frameCount << std::endl;
    
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
    
    // STEP 1: Simple screen clearing
    clearScreenToColor(commandBuffer.get(), 0.2f, 0.3f, 0.8f, 1.0f);
    
    // STEP 2: End ImGui frame (prevents accumulation)
    ImGui::Render();
    
    // Execute and present
    commandBuffer->close();
    queue->executeCommandBuffers(1, commandBuffer.readRef(), nullptr, 0);
    transientHeaps[bufferIndex]->finish();
    queue->waitOnHost();  // Stage 0 synchronization
    swapchain->present();
    
    // std::cout << "✅ Frame " << frameCount << " completed successfully" << std::endl;    
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
    // Stage 0: Minimal implementation - just log that we're clearing
    // TODO: Implement actual clearing in Stage 1+
}

gfx::ICommandBuffer* SlangDisplay::getCurrentCommandBuffer() {
    size_t bufferIndex = m_currentFrameIndex % commandBuffers.size();
    return commandBuffers[bufferIndex].get();
}

gfx::IFramebuffer* SlangDisplay::getCurrentFramebuffer() {
    return framebuffers[m_currentFrameIndex].get();
}

