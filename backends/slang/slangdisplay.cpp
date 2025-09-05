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
#include "../dxr/imgui_impl_dx12.h"
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

#ifdef USE_VULKAN
    // For Vulkan, we'll handle window surface creation differently
#else  // DX12
    HWND hwnd = wm_info.info.win.window;
#endif

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

    // 3. Create command queue
    gfx::ICommandQueue::Desc queueDesc = {};
    queueDesc.type = gfx::ICommandQueue::QueueType::Graphics;
    queue = device->createCommandQueue(queueDesc);

    // 4. Create framebuffer layout
    gfx::IFramebufferLayout::TargetLayout renderTargetLayout = {gfx::Format::R8G8B8A8_UNORM, 1};
    gfx::IFramebufferLayout::TargetLayout depthLayout = {gfx::Format::D32_FLOAT, 1};
    gfx::IFramebufferLayout::Desc framebufferLayoutDesc = {};
    framebufferLayoutDesc.renderTargetCount = 1;
    framebufferLayoutDesc.renderTargets = &renderTargetLayout;
    framebufferLayoutDesc.depthStencil = &depthLayout;
    device->createFramebufferLayout(framebufferLayoutDesc, framebufferLayout.writeRef());

#ifdef USE_VULKAN
    // 4.1. Create render pass layout for Vulkan (required for proper command buffer encoding)
    gfx::IRenderPassLayout::Desc renderPassLayoutDesc = {};
    renderPassLayoutDesc.framebufferLayout = framebufferLayout;
    renderPassLayoutDesc.renderTargetCount = 1;
    gfx::IRenderPassLayout::TargetAccessDesc renderTargetAccess = {};
    renderTargetAccess.loadOp = gfx::IRenderPassLayout::TargetLoadOp::Clear;  // Clear for fresh frame
    renderTargetAccess.storeOp = gfx::IRenderPassLayout::TargetStoreOp::Store;
    renderTargetAccess.initialState = gfx::ResourceState::Undefined;  // Start from undefined (common for swapchain)
    renderTargetAccess.finalState = gfx::ResourceState::Present;   // End in present state
    renderPassLayoutDesc.renderTargetAccess = &renderTargetAccess;
    
    gfx::IRenderPassLayout::TargetAccessDesc depthAccess = {};
    depthAccess.loadOp = gfx::IRenderPassLayout::TargetLoadOp::Clear;  // Clear depth
    depthAccess.storeOp = gfx::IRenderPassLayout::TargetStoreOp::Store;
    depthAccess.initialState = gfx::ResourceState::Undefined;  // Start from undefined
    depthAccess.finalState = gfx::ResourceState::DepthWrite;   // End in depth write state
    renderPassLayoutDesc.depthStencilAccess = &depthAccess;
    
    device->createRenderPassLayout(renderPassLayoutDesc, renderPassLayout.writeRef());
#endif

    // 5. Create swapchain
    gfx::ISwapchain::Desc swapchainDesc = {};
    swapchainDesc.format = gfx::Format::R8G8B8A8_UNORM;
    swapchainDesc.width = 1280; // Default, should be window size
    swapchainDesc.height = 720;
    swapchainDesc.imageCount = 2;
    swapchainDesc.queue = queue;
    
#ifdef USE_VULKAN
    // For Vulkan, create window handle through SDL
    // Extract platform-specific window handle
    #ifdef _WIN32
    HWND hwnd = wm_info.info.win.window;
    gfx::WindowHandle windowHandle = gfx::WindowHandle::FromHwnd(hwnd);
    #elif defined(__linux__)
    // For Linux X11
    Display* x11Display = wm_info.info.x11.display;
    Window x11Window = wm_info.info.x11.window;
    gfx::WindowHandle windowHandle = gfx::WindowHandle::FromXlib(x11Display, x11Window);
    #else
    gfx::WindowHandle windowHandle = {};
    #endif
#else  // DX12
#ifdef _WIN32
    // Create window handle for Windows using the static method FromHwnd
    gfx::WindowHandle windowHandle = gfx::WindowHandle::FromHwnd(hwnd);
#else
    // For non-Windows platforms
    gfx::WindowHandle windowHandle = {};
#endif
#endif
    
    swapchain = device->createSwapchain(swapchainDesc, windowHandle);

    // 6. Create framebuffers for swapchain images
    framebuffers.clear();
    for (uint32_t i = 0; i < 2; ++i) {
        ComPtr<gfx::ITextureResource> colorBuffer;
        swapchain->getImage(i, colorBuffer.writeRef());
        gfx::IResourceView::Desc colorBufferViewDesc = {};
        colorBufferViewDesc.format = gfx::Format::R8G8B8A8_UNORM;
        colorBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        colorBufferViewDesc.type = gfx::IResourceView::Type::RenderTarget;
        ComPtr<gfx::IResourceView> rtv = device->createTextureView(colorBuffer.get(), colorBufferViewDesc);

        // Depth buffer
        gfx::ITextureResource::Desc depthBufferDesc = {};
        depthBufferDesc.type = gfx::IResource::Type::Texture2D;
        depthBufferDesc.size.width = swapchainDesc.width;
        depthBufferDesc.size.height = swapchainDesc.height;
        depthBufferDesc.size.depth = 1;
        depthBufferDesc.format = gfx::Format::D32_FLOAT;
        depthBufferDesc.defaultState = gfx::ResourceState::DepthWrite;
        depthBufferDesc.allowedStates = gfx::ResourceStateSet(gfx::ResourceState::DepthWrite);
        ComPtr<gfx::ITextureResource> depthBuffer = device->createTextureResource(depthBufferDesc, nullptr);
        gfx::IResourceView::Desc depthBufferViewDesc = {};
        depthBufferViewDesc.format = gfx::Format::D32_FLOAT;
        depthBufferViewDesc.renderTarget.shape = gfx::IResource::Type::Texture2D;
        depthBufferViewDesc.type = gfx::IResourceView::Type::DepthStencil;
        ComPtr<gfx::IResourceView> dsv = device->createTextureView(depthBuffer.get(), depthBufferViewDesc);

        gfx::IFramebuffer::Desc framebufferDesc = {};
        framebufferDesc.renderTargetCount = 1;
        framebufferDesc.depthStencilView = dsv.get();
        framebufferDesc.renderTargetViews = rtv.readRef();
        framebufferDesc.layout = framebufferLayout;
        ComPtr<gfx::IFramebuffer> framebuffer = device->createFramebuffer(framebufferDesc);
        framebuffers.push_back(framebuffer);
    }

    // 7. Create transient heaps and command buffers
    transientHeaps.clear();
    commandBuffers.clear();
    for (uint32_t i = 0; i < 2; ++i) {
        gfx::ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096 * 1024;
        auto transientHeap = device->createTransientResourceHeap(transientHeapDesc);
        transientHeaps.push_back(transientHeap);
        
        // Create command buffer for each frame
        Slang::ComPtr<gfx::ICommandBuffer> commandBuffer;
        transientHeap->createCommandBuffer(commandBuffer.writeRef());
        commandBuffers.push_back(commandBuffer);
    }

#ifdef USE_VULKAN
    // 8. Simplified ImGui setup for now - just build font atlas
    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->IsBuilt()) {
        if (io.Fonts->ConfigData.Size == 0) {
            io.Fonts->AddFontDefault();
        }
        io.Fonts->Build();
    }

#else  // DX12
#ifdef _WIN32
    // 8. ImGui native backend initialization (DX12 example)
    // 8.1 Create a descriptor heap for ImGui
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
    
    if (!d3d12Device)
    {
        throw std::runtime_error("Failed to get D3D12 device from GFX device");
    }
    
    HRESULT hr = d3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imgui_desc_heap));

    // Initialize ImGui for DX12 only (SDL is initialized in main.cpp)
    ImGui_ImplDX12_Init(
        d3d12Device,
        2,            // num frames in flight
        DXGI_FORMAT_R8G8B8A8_UNORM,
        imgui_desc_heap.Get(),
        imgui_desc_heap->GetCPUDescriptorHandleForHeapStart(),
        imgui_desc_heap->GetGPUDescriptorHandleForHeapStart());

    // Create and upload font atlas for DX12
    // We need to execute a command to upload the fonts
    // For now, we'll let ImGui handle this automatically on first render
    // The font atlas will be created during the first NewFrame call
#endif
#endif
}

SlangDisplay::~SlangDisplay() {
#ifdef USE_VULKAN
    // Make sure we wait for any pending GPU work to complete
    if (queue)
    {
        queue->waitOnHost();
        // Sleep equivalent for cross-platform
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Clear our command buffers
    commandBuffers.clear();
    transientHeaps.clear();

#else  // DX12
#ifdef _WIN32
    // Make sure we wait for any pending GPU work to complete
    if (queue)
    {
        queue->waitOnHost();
        Sleep(100);
    }
        
        
    // Clear our command buffers
    commandBuffers.clear();
    transientHeaps.clear();

    // Shutdown ImGui DX12 backend
    ImGui_ImplDX12_Shutdown();

    imgui_desc_heap.Reset();
#endif
#endif

    // Explicitly clear resources in a controlled order
    framebuffers.clear();
    swapchain = nullptr;
    framebufferLayout = nullptr;
    queue = nullptr;
    device = nullptr;
}

std::string SlangDisplay::gpu_brand() {
    if (device) {
        const gfx::DeviceInfo& info = device->getDeviceInfo();
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
#ifdef USE_VULKAN
    // Simple font atlas check (no specific backend NewFrame needed)
    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->IsBuilt()) {
        if (io.Fonts->ConfigData.Size == 0) {
            io.Fonts->AddFontDefault();
        }
        io.Fonts->Build();
    }
#else  // DX12
#ifdef _WIN32
    // Only call ImGui_ImplDX12_NewFrame()
    // ImGui_ImplSDL2_NewFrame() and ImGui::NewFrame() are called elsewhere
    ImGui_ImplDX12_NewFrame();
#endif
#endif
}

void SlangDisplay::display(RenderBackend* backend) {
    // Acquire next image
    uint32_t frameIndex = swapchain->acquireNextImage();
    
    // Use proper frame-based buffer index for better resource utilization
    size_t bufferIndex = frameIndex % transientHeaps.size();
    
    // End the ImGui frame properly
    ImGui::Render();
    
    // Reset the transient heap to prepare for new commands
    // With latest Slang improvements, this handles synchronization better
    transientHeaps[bufferIndex]->synchronizeAndReset();
    
    // Create a fresh command buffer
    Slang::ComPtr<gfx::ICommandBuffer> commandBuffer;
    transientHeaps[bufferIndex]->createCommandBuffer(commandBuffer.writeRef());

#ifdef USE_VULKAN
    // Simple empty command buffer for Vulkan test
    commandBuffer->close();
    queue->executeCommandBuffers(1, commandBuffer.readRef(), nullptr, 0);
    
    // Essential synchronization for Vulkan - ensures commands complete before heap reset
    // Latest Slang has improved fence management, but this waitOnHost() ensures compatibility
    queue->waitOnHost();
    
    // Call finish() for consistency (no-op for Vulkan, proper sync for D3D12)
    transientHeaps[bufferIndex]->finish();

#else  // DX12
#ifdef _WIN32
    // Get native D3D12 command list
    gfx::InteropHandle nativeHandle = {};
    if (SLANG_SUCCEEDED(commandBuffer->getNativeHandle(&nativeHandle)) && 
        nativeHandle.api == gfx::InteropHandleAPI::D3D12)
    {
        // Cast the handle to a D3D12 command list
        ID3D12GraphicsCommandList* cmdList = 
            reinterpret_cast<ID3D12GraphicsCommandList*>(nativeHandle.handleValue);
            
        // Set descriptor heap for ImGui
        ID3D12DescriptorHeap* heaps[] = { imgui_desc_heap.Get() };
        cmdList->SetDescriptorHeaps(1, heaps);
        
        // Render ImGui draw data to the command list
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
        
        // Close the command buffer
        commandBuffer->close();
        
        // Execute the command buffer
        queue->executeCommandBuffers(1, commandBuffer.readRef(), nullptr, 0);
    }
#endif
#endif

    // Present the swapchain
    swapchain->present();
}

