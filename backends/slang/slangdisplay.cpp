#include "slangdisplay.h"
#include <slang-gfx.h>
#include <SDL_syswm.h>
#include <imgui.h>
#include "util/display/imgui_impl_sdl.h"
#ifdef _WIN32
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>
#include "../dxr/imgui_impl_dx12.h"
// Add D3D12 descriptor heap at file scope
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imgui_desc_heap;
#endif
#include <memory>
#include <string>

using namespace Slang;
using namespace gfx;

SlangDisplay::SlangDisplay(SDL_Window* sdl_window) : window(sdl_window) {
    // 1. Extract native window handle from SDL_Window*
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(sdl_window, &wm_info);
    HWND hwnd = wm_info.info.win.window;

    // 2. Create device
    IDevice::Desc deviceDesc = {};
    deviceDesc.deviceType = DeviceType::Default;
    gfx::Result res = gfxCreateDevice(&deviceDesc, device.writeRef());
    if (SLANG_FAILED(res)) throw std::runtime_error("Failed to create GFX device");

    // 3. Create command queue
    ICommandQueue::Desc queueDesc = {};
    queueDesc.type = ICommandQueue::QueueType::Graphics;
    queue = device->createCommandQueue(queueDesc);

    // 4. Create framebuffer layout
    IFramebufferLayout::TargetLayout renderTargetLayout = {Format::R8G8B8A8_UNORM, 1};
    IFramebufferLayout::TargetLayout depthLayout = {Format::D32_FLOAT, 1};
    IFramebufferLayout::Desc framebufferLayoutDesc = {};
    framebufferLayoutDesc.renderTargetCount = 1;
    framebufferLayoutDesc.renderTargets = &renderTargetLayout;
    framebufferLayoutDesc.depthStencil = &depthLayout;
    device->createFramebufferLayout(framebufferLayoutDesc, framebufferLayout.writeRef());

    // 5. Create swapchain
    ISwapchain::Desc swapchainDesc = {};
    swapchainDesc.format = Format::R8G8B8A8_UNORM;
    swapchainDesc.width = 1280; // Default, should be window size
    swapchainDesc.height = 720;
    swapchainDesc.imageCount = 2;
    swapchainDesc.queue = queue;
    
#ifdef _WIN32
    // Create window handle for Windows using the static method FromHwnd
    gfx::WindowHandle windowHandle = gfx::WindowHandle::FromHwnd(hwnd);
#else
    // For non-Windows platforms
    gfx::WindowHandle windowHandle = {};
#endif
    
    swapchain = device->createSwapchain(swapchainDesc, windowHandle);

    // 6. Create framebuffers for swapchain images
    framebuffers.clear();
    for (uint32_t i = 0; i < 2; ++i) {
        ComPtr<ITextureResource> colorBuffer;
        swapchain->getImage(i, colorBuffer.writeRef());
        IResourceView::Desc colorBufferViewDesc = {};
        colorBufferViewDesc.format = Format::R8G8B8A8_UNORM;
        colorBufferViewDesc.renderTarget.shape = IResource::Type::Texture2D;
        colorBufferViewDesc.type = IResourceView::Type::RenderTarget;
        ComPtr<IResourceView> rtv = device->createTextureView(colorBuffer.get(), colorBufferViewDesc);

        // Depth buffer
        ITextureResource::Desc depthBufferDesc = {};
        depthBufferDesc.type = IResource::Type::Texture2D;
        depthBufferDesc.size.width = swapchainDesc.width;
        depthBufferDesc.size.height = swapchainDesc.height;
        depthBufferDesc.size.depth = 1;
        depthBufferDesc.format = Format::D32_FLOAT;
        depthBufferDesc.defaultState = ResourceState::DepthWrite;
        depthBufferDesc.allowedStates = ResourceStateSet(ResourceState::DepthWrite);
        ComPtr<ITextureResource> depthBuffer = device->createTextureResource(depthBufferDesc, nullptr);
        IResourceView::Desc depthBufferViewDesc = {};
        depthBufferViewDesc.format = Format::D32_FLOAT;
        depthBufferViewDesc.renderTarget.shape = IResource::Type::Texture2D;
        depthBufferViewDesc.type = IResourceView::Type::DepthStencil;
        ComPtr<IResourceView> dsv = device->createTextureView(depthBuffer.get(), depthBufferViewDesc);

        IFramebuffer::Desc framebufferDesc = {};
        framebufferDesc.renderTargetCount = 1;
        framebufferDesc.depthStencilView = dsv.get();
        framebufferDesc.renderTargetViews = rtv.readRef();
        framebufferDesc.layout = framebufferLayout;
        ComPtr<IFramebuffer> framebuffer = device->createFramebuffer(framebufferDesc);
        framebuffers.push_back(framebuffer);
    }

    // 7. Create transient heaps and command buffers
    transientHeaps.clear();
    commandBuffers.clear();
    for (uint32_t i = 0; i < 2; ++i) {
        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096 * 1024;
        auto transientHeap = device->createTransientResourceHeap(transientHeapDesc);
        transientHeaps.push_back(transientHeap);
        
        // Create command buffer for each frame
        Slang::ComPtr<ICommandBuffer> commandBuffer;
        transientHeap->createCommandBuffer(commandBuffer.writeRef());
        commandBuffers.push_back(commandBuffer);
    }

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
#endif
}

SlangDisplay::~SlangDisplay() {
#ifdef _WIN32
    // Make sure we wait for any pending GPU work to complete
    if (queue)
        queue->waitOnHost();
        
    // Clear our command buffers
    commandBuffers.clear();
    
    // Shutdown ImGui DX12 backend
    ImGui_ImplDX12_Shutdown();
#endif

    // Explicitly clear resources in a controlled order
    framebuffers.clear();
    transientHeaps.clear();
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
#ifdef _WIN32
    // Only call ImGui_ImplDX12_NewFrame()
    // ImGui_ImplSDL2_NewFrame() and ImGui::NewFrame() are called elsewhere
    ImGui_ImplDX12_NewFrame();
#endif
}

void SlangDisplay::display(RenderBackend* backend) {
    // Acquire next image
    uint32_t frameIndex = swapchain->acquireNextImage();
    size_t bufferIndex = frameIndex % commandBuffers.size();
    
#ifdef _WIN32
    // End the ImGui frame properly
    ImGui::Render();
    
    // Reset the transient heap to prepare for new commands
    transientHeaps[bufferIndex]->synchronizeAndReset();
    
    // Use our pre-allocated command buffer
    auto& commandBuffer = commandBuffers[bufferIndex];
    
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

    // Present the swapchain
    swapchain->present();
}

