# Plan B: Falcor Approach (Native Backend Integration)

This plan uses Dear ImGui's official native backends with Slang GFX interoperability, similar to Falcor's approach. This leverages mature, battle-tested ImGui implementations while maintaining integration with the Slang GFX system.

## Overview

**Approach**: Native ImGui backends with Slang GFX interop
**Benefits**: 
- Mature and optimized implementations
- Full ImGui feature support out of the box
- Proven stability and performance
- Minimal custom code required

**Architecture**: Extract native API handles from Slang GFX and use official Dear ImGui backends

---

## Stage 1: Native Handle Extraction Infrastructure

**Objective**: Create infrastructure to extract native graphics API handles from Slang GFX for use with ImGui's native backends.

**Tasks**:
1. Create native handle extraction utilities
2. Setup API-specific initialization paths
3. Add proper error handling for interop operations
4. Create wrapper structures for native handles

**Implementation Details**:

### Create Interop Header
```cpp
// Create new file: c:\dev\ChameleonRT\backends\slang\slang_native_interop.h
#pragma once
#include <slang-gfx.h>

#ifdef USE_VULKAN
#include <vulkan/vulkan.h>
#else
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace SlangNativeInterop {
    
#ifdef USE_VULKAN
    struct VulkanHandles {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t queueFamilyIndex = 0;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkAllocationCallbacks* allocator = nullptr;
    };
    
    bool extractVulkanHandles(gfx::IDevice* device, gfx::ICommandQueue* queue, VulkanHandles& outHandles);
    VkCommandBuffer extractVulkanCommandBuffer(gfx::ICommandBuffer* commandBuffer);
    VkRenderPass createImGuiRenderPass(VkDevice device, VkFormat swapchainFormat);
    void destroyImGuiRenderPass(VkDevice device, VkRenderPass renderPass);
    
#else
    struct D3D12Handles {
        ID3D12Device* device = nullptr;
        ID3D12CommandQueue* commandQueue = nullptr;
        DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        uint32_t numFramesInFlight = 2;
        ID3D12DescriptorHeap* srvDescHeap = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE fontSrvCpuDescHandle = {};
        D3D12_GPU_DESCRIPTOR_HANDLE fontSrvGpuDescHandle = {};
    };
    
    bool extractD3D12Handles(gfx::IDevice* device, gfx::ICommandQueue* queue, D3D12Handles& outHandles);
    ID3D12GraphicsCommandList* extractD3D12CommandList(gfx::ICommandBuffer* commandBuffer);
    bool createD3D12DescriptorHeap(ID3D12Device* device, ID3D12DescriptorHeap** outHeap,
                                   D3D12_CPU_DESCRIPTOR_HANDLE& outCpuHandle,
                                   D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle);
    void destroyD3D12DescriptorHeap(ID3D12DescriptorHeap* heap);
#endif

    // Common utility functions
    bool validateNativeHandles();
    void logHandleInfo(const char* handleType, void* handle);
}
```

### Create Interop Implementation
```cpp
// Create new file: c:\dev\ChameleonRT\backends\slang\slang_native_interop.cpp
#include "slang_native_interop.h"
#include <iostream>
#include <cassert>

namespace SlangNativeInterop {

#ifdef USE_VULKAN
bool extractVulkanHandles(gfx::IDevice* device, gfx::ICommandQueue* queue, VulkanHandles& outHandles) {
    // Extract native handles using Slang GFX interop API
    gfx::IDevice::InteropHandles deviceHandles;
    if (SLANG_FAILED(device->getNativeDeviceHandles(&deviceHandles))) {
        std::cerr << "❌ Failed to extract native device handles" << std::endl;
        return false;
    }
    
    // Map Slang handles to Vulkan handles
    // Note: Actual mapping depends on Slang GFX implementation details
    outHandles.instance = (VkInstance)deviceHandles.handles[0].handleValue;
    outHandles.physicalDevice = (VkPhysicalDevice)deviceHandles.handles[1].handleValue;
    outHandles.device = (VkDevice)deviceHandles.handles[2].handleValue;
    
    // Extract queue handle
    gfx::ICommandQueue::NativeHandle queueHandle;
    if (SLANG_FAILED(queue->getNativeHandle(&queueHandle))) {
        std::cerr << "❌ Failed to extract native queue handle" << std::endl;
        return false;
    }
    
    outHandles.queue = (VkQueue)queueHandle.handleValue;
    outHandles.queueFamilyIndex = 0; // TODO: Extract actual queue family index
    
    logHandleInfo("VkInstance", outHandles.instance);
    logHandleInfo("VkPhysicalDevice", outHandles.physicalDevice);
    logHandleInfo("VkDevice", outHandles.device);
    logHandleInfo("VkQueue", outHandles.queue);
    
    return true;
}

VkCommandBuffer extractVulkanCommandBuffer(gfx::ICommandBuffer* commandBuffer) {
    gfx::ICommandBuffer::NativeHandle nativeHandle;
    if (SLANG_FAILED(commandBuffer->getNativeHandle(&nativeHandle))) {
        std::cerr << "❌ Failed to extract native command buffer handle" << std::endl;
        return VK_NULL_HANDLE;
    }
    
    VkCommandBuffer vkCmdBuffer = (VkCommandBuffer)nativeHandle.handleValue;
    logHandleInfo("VkCommandBuffer", vkCmdBuffer);
    return vkCmdBuffer;
}

VkRenderPass createImGuiRenderPass(VkDevice device, VkFormat swapchainFormat) {
    VkAttachmentDescription attachment = {};
    attachment.format = swapchainFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Load existing content
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentReference colorAttachment = {};
    colorAttachment.attachment = 0;
    colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachment;
    
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    VkRenderPass renderPass;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        std::cerr << "❌ Failed to create ImGui render pass" << std::endl;
        return VK_NULL_HANDLE;
    }
    
    std::cout << "✅ Created ImGui Vulkan render pass" << std::endl;
    return renderPass;
}

void destroyImGuiRenderPass(VkDevice device, VkRenderPass renderPass) {
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        std::cout << "🧹 Destroyed ImGui Vulkan render pass" << std::endl;
    }
}

#else // D3D12

bool extractD3D12Handles(gfx::IDevice* device, gfx::ICommandQueue* queue, D3D12Handles& outHandles) {
    // Extract native D3D12 handles using Slang GFX interop API
    gfx::IDevice::InteropHandles deviceHandles;
    if (SLANG_FAILED(device->getNativeDeviceHandles(&deviceHandles))) {
        std::cerr << "❌ Failed to extract native D3D12 device handles" << std::endl;
        return false;
    }
    
    outHandles.device = (ID3D12Device*)deviceHandles.handles[0].handleValue;
    
    gfx::ICommandQueue::NativeHandle queueHandle;
    if (SLANG_FAILED(queue->getNativeHandle(&queueHandle))) {
        std::cerr << "❌ Failed to extract native D3D12 command queue handle" << std::endl;
        return false;
    }
    
    outHandles.commandQueue = (ID3D12CommandQueue*)queueHandle.handleValue;
    outHandles.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // Default format
    outHandles.numFramesInFlight = 2; // Default double buffering
    
    logHandleInfo("ID3D12Device", outHandles.device);
    logHandleInfo("ID3D12CommandQueue", outHandles.commandQueue);
    
    return true;
}

ID3D12GraphicsCommandList* extractD3D12CommandList(gfx::ICommandBuffer* commandBuffer) {
    gfx::ICommandBuffer::NativeHandle nativeHandle;
    if (SLANG_FAILED(commandBuffer->getNativeHandle(&nativeHandle))) {
        std::cerr << "❌ Failed to extract native D3D12 command list handle" << std::endl;
        return nullptr;
    }
    
    ID3D12GraphicsCommandList* d3dCmdList = (ID3D12GraphicsCommandList*)nativeHandle.handleValue;
    logHandleInfo("ID3D12GraphicsCommandList", d3dCmdList);
    return d3dCmdList;
}

bool createD3D12DescriptorHeap(ID3D12Device* device, ID3D12DescriptorHeap** outHeap,
                               D3D12_CPU_DESCRIPTOR_HANDLE& outCpuHandle,
                               D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle) {
    // Create descriptor heap for ImGui font texture
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    
    if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(outHeap)))) {
        std::cerr << "❌ Failed to create D3D12 descriptor heap for ImGui" << std::endl;
        return false;
    }
    
    outCpuHandle = (*outHeap)->GetCPUDescriptorHandleForHeapStart();
    outGpuHandle = (*outHeap)->GetGPUDescriptorHandleForHeapStart();
    
    std::cout << "✅ Created D3D12 descriptor heap for ImGui" << std::endl;
    return true;
}

void destroyD3D12DescriptorHeap(ID3D12DescriptorHeap* heap) {
    if (heap) {
        heap->Release();
        std::cout << "🧹 Destroyed D3D12 descriptor heap" << std::endl;
    }
}

#endif

// Common utility functions
bool validateNativeHandles() {
    // Add validation logic based on current API
    std::cout << "🔍 Validating native API handles..." << std::endl;
    return true;
}

void logHandleInfo(const char* handleType, void* handle) {
    std::cout << "🔗 Extracted " << handleType << ": " << handle << std::endl;
}

} // namespace SlangNativeInterop
```

**Checkpoints**:
- [ ] Slang GFX interop API calls succeed
- [ ] Native handles extract correctly for both Vulkan and D3D12
- [ ] Handle validation functions work properly
- [ ] Error handling provides clear diagnostics
- [ ] Memory management is proper (no leaks)

**Test Scenario**: Extract handles and verify they're valid native API objects.

**Test Commands**: 
_[To be filled manually]_

**Final Acceptance Criteria**:
_[To be filled manually]_

---

## Stage 2: Native ImGui Backend Initialization

**Objective**: Initialize Dear ImGui's official backends using the extracted native handles.

**Tasks**:
1. Setup ImGui backend initialization for Vulkan and D3D12
2. Handle render pass creation for ImGui rendering
3. Configure descriptor heaps and resource binding
4. Create font texture upload mechanisms

**Implementation Details**:

### Create Native ImGui Manager
```cpp
// Create new file: c:\dev\ChameleonRT\backends\slang\slang_imgui_native.h
#pragma once
#include "slang_native_interop.h"
#include <imgui.h>

#ifdef USE_VULKAN
#include "backends/imgui_impl_vulkan.h"
#else
#include "backends/imgui_impl_dx12.h"
#endif

class SlangImGuiNative {
private:
#ifdef USE_VULKAN
    SlangNativeInterop::VulkanHandles m_vulkanHandles;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
#else
    SlangNativeInterop::D3D12Handles m_d3d12Handles;
#endif
    
    bool m_initialized = false;
    
public:
    bool initialize(gfx::IDevice* device, gfx::ICommandQueue* queue, gfx::Format swapchainFormat);
    void render(gfx::ICommandBuffer* commandBuffer, ImDrawData* drawData);
    void shutdown();
    
private:
#ifdef USE_VULKAN
    bool initializeVulkan();
    bool createVulkanDescriptorPool();
    void renderVulkan(VkCommandBuffer cmdBuffer, ImDrawData* drawData);
#else
    bool initializeD3D12();
    void renderD3D12(ID3D12GraphicsCommandList* cmdList, ImDrawData* drawData);
#endif
};
```

### Implement Native ImGui Manager
```cpp
// Create new file: c:\dev\ChameleonRT\backends\slang\slang_imgui_native.cpp
#include "slang_imgui_native.h"
#include <iostream>

bool SlangImGuiNative::initialize(gfx::IDevice* device, gfx::ICommandQueue* queue, gfx::Format swapchainFormat) {
    if (m_initialized) {
        std::cout << "⚠️ ImGui native backend already initialized" << std::endl;
        return true;
    }
    
#ifdef USE_VULKAN
    // Extract Vulkan handles
    if (!SlangNativeInterop::extractVulkanHandles(device, queue, m_vulkanHandles)) {
        std::cerr << "❌ Failed to extract Vulkan handles for ImGui" << std::endl;
        return false;
    }
    
    // Convert Slang format to Vulkan format
    VkFormat vkFormat = VK_FORMAT_B8G8R8A8_UNORM; // Default, should be derived from swapchainFormat
    
    // Create render pass for ImGui
    m_renderPass = SlangNativeInterop::createImGuiRenderPass(m_vulkanHandles.device, vkFormat);
    if (m_renderPass == VK_NULL_HANDLE) {
        return false;
    }
    
    if (!initializeVulkan()) {
        return false;
    }
    
#else
    // Extract D3D12 handles
    if (!SlangNativeInterop::extractD3D12Handles(device, queue, m_d3d12Handles)) {
        std::cerr << "❌ Failed to extract D3D12 handles for ImGui" << std::endl;
        return false;
    }
    
    if (!initializeD3D12()) {
        return false;
    }
#endif
    
    m_initialized = true;
    std::cout << "✅ ImGui native backend initialized successfully" << std::endl;
    return true;
}

#ifdef USE_VULKAN
bool SlangImGuiNative::initializeVulkan() {
    // Create descriptor pool for ImGui
    if (!createVulkanDescriptorPool()) {
        return false;
    }
    
    // Setup ImGui Vulkan init info
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = m_vulkanHandles.instance;
    initInfo.PhysicalDevice = m_vulkanHandles.physicalDevice;
    initInfo.Device = m_vulkanHandles.device;
    initInfo.QueueFamily = m_vulkanHandles.queueFamilyIndex;
    initInfo.Queue = m_vulkanHandles.queue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = 2;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = m_vulkanHandles.allocator;
    initInfo.CheckVkResultFn = nullptr; // Use default error handling
    
    if (!ImGui_ImplVulkan_Init(&initInfo, m_renderPass)) {
        std::cerr << "❌ Failed to initialize ImGui Vulkan backend" << std::endl;
        return false;
    }
    
    // Upload fonts texture
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // TODO: Get temp command buffer from Slang
    ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
    // TODO: Submit command buffer and wait
    ImGui_ImplVulkan_DestroyFontUploadObjects();
    
    std::cout << "✅ ImGui Vulkan backend initialized" << std::endl;
    return true;
}

bool SlangImGuiNative::createVulkanDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
    poolInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    
    if (vkCreateDescriptorPool(m_vulkanHandles.device, &poolInfo, m_vulkanHandles.allocator, &m_descriptorPool) != VK_SUCCESS) {
        std::cerr << "❌ Failed to create Vulkan descriptor pool for ImGui" << std::endl;
        return false;
    }
    
    std::cout << "✅ Created Vulkan descriptor pool for ImGui" << std::endl;
    return true;
}

void SlangImGuiNative::renderVulkan(VkCommandBuffer cmdBuffer, ImDrawData* drawData) {
    if (!drawData || drawData->CmdListsCount == 0) {
        return;
    }
    
    // Begin render pass if not already begun
    // Note: This assumes the render pass is already active
    
    // Render ImGui using official Vulkan backend
    ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuffer);
    
    std::cout << "🎨 Rendered ImGui via Vulkan backend" << std::endl;
}

#else // D3D12

bool SlangImGuiNative::initializeD3D12() {
    // Create descriptor heap for D3D12
    if (!SlangNativeInterop::createD3D12DescriptorHeap(
            m_d3d12Handles.device,
            &m_d3d12Handles.srvDescHeap,
            m_d3d12Handles.fontSrvCpuDescHandle,
            m_d3d12Handles.fontSrvGpuDescHandle)) {
        return false;
    }
    
    // Initialize ImGui D3D12 backend
    if (!ImGui_ImplDX12_Init(
            m_d3d12Handles.device,
            m_d3d12Handles.numFramesInFlight,
            m_d3d12Handles.rtvFormat,
            m_d3d12Handles.srvDescHeap,
            m_d3d12Handles.fontSrvCpuDescHandle,
            m_d3d12Handles.fontSrvGpuDescHandle)) {
        std::cerr << "❌ Failed to initialize ImGui D3D12 backend" << std::endl;
        return false;
    }
    
    std::cout << "✅ ImGui D3D12 backend initialized" << std::endl;
    return true;
}

void SlangImGuiNative::renderD3D12(ID3D12GraphicsCommandList* cmdList, ImDrawData* drawData) {
    if (!drawData || drawData->CmdListsCount == 0) {
        return;
    }
    
    // Set descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_d3d12Handles.srvDescHeap };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    
    // Render ImGui using official D3D12 backend
    ImGui_ImplDX12_RenderDrawData(drawData, cmdList);
    
    std::cout << "🎨 Rendered ImGui via D3D12 backend" << std::endl;
}

#endif

void SlangImGuiNative::render(gfx::ICommandBuffer* commandBuffer, ImDrawData* drawData) {
    if (!m_initialized) {
        std::cerr << "❌ ImGui native backend not initialized" << std::endl;
        return;
    }
    
#ifdef USE_VULKAN
    VkCommandBuffer vkCmdBuffer = SlangNativeInterop::extractVulkanCommandBuffer(commandBuffer);
    if (vkCmdBuffer != VK_NULL_HANDLE) {
        renderVulkan(vkCmdBuffer, drawData);
    }
#else
    ID3D12GraphicsCommandList* d3dCmdList = SlangNativeInterop::extractD3D12CommandList(commandBuffer);
    if (d3dCmdList != nullptr) {
        renderD3D12(d3dCmdList, drawData);
    }
#endif
}

void SlangImGuiNative::shutdown() {
    if (!m_initialized) {
        return;
    }
    
#ifdef USE_VULKAN
    ImGui_ImplVulkan_Shutdown();
    
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_vulkanHandles.device, m_descriptorPool, m_vulkanHandles.allocator);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    
    SlangNativeInterop::destroyImGuiRenderPass(m_vulkanHandles.device, m_renderPass);
    m_renderPass = VK_NULL_HANDLE;
    
#else
    ImGui_ImplDX12_Shutdown();
    SlangNativeInterop::destroyD3D12DescriptorHeap(m_d3d12Handles.srvDescHeap);
#endif
    
    m_initialized = false;
    std::cout << "🧹 ImGui native backend shutdown complete" << std::endl;
}
```

**Checkpoints**:
- [ ] Native backend initialization succeeds for both APIs
- [ ] Font texture uploads correctly
- [ ] Descriptor pools/heaps create successfully
- [ ] Render pass setup works for Vulkan
- [ ] Error handling provides clear feedback

**Test Scenario**: Initialize backends and verify ImGui renders basic elements.

**Test Commands**: 
_[To be filled manually]_

**Final Acceptance Criteria**:
_[To be filled manually]_

---

## Stage 3: Command Buffer Synchronization

**Objective**: Properly synchronize native ImGui rendering with Slang GFX command buffer lifecycle.

**Tasks**:
1. Handle command buffer state transitions
2. Manage render pass begin/end lifecycle
3. Implement proper resource barriers
4. Add frame synchronization handling

**Implementation Details**:

### Create Synchronization Manager
```cpp
// Add to slang_imgui_native.h
class CommandBufferSync {
private:
    bool m_renderPassActive = false;
    gfx::ICommandBuffer* m_currentCommandBuffer = nullptr;
    
#ifdef USE_VULKAN
    VkCommandBuffer m_currentVkCommandBuffer = VK_NULL_HANDLE;
#else
    ID3D12GraphicsCommandList* m_currentD3DCommandList = nullptr;
#endif

public:
    bool beginImGuiRenderPass(gfx::ICommandBuffer* commandBuffer, gfx::IFramebuffer* framebuffer);
    void endImGuiRenderPass();
    void renderImGui(ImDrawData* drawData);
    bool isRenderPassActive() const { return m_renderPassActive; }
    
private:
#ifdef USE_VULKAN
    bool beginVulkanRenderPass(VkCommandBuffer cmdBuffer, VkFramebuffer framebuffer);
    void endVulkanRenderPass(VkCommandBuffer cmdBuffer);
#else
    bool beginD3D12Rendering(ID3D12GraphicsCommandList* cmdList);
    void endD3D12Rendering(ID3D12GraphicsCommandList* cmdList);
#endif
};
```

### Implement Synchronization
```cpp
// Add to slang_imgui_native.cpp
bool CommandBufferSync::beginImGuiRenderPass(gfx::ICommandBuffer* commandBuffer, gfx::IFramebuffer* framebuffer) {
    if (m_renderPassActive) {
        std::cerr << "⚠️ ImGui render pass already active" << std::endl;
        return false;
    }
    
    m_currentCommandBuffer = commandBuffer;
    
#ifdef USE_VULKAN
    m_currentVkCommandBuffer = SlangNativeInterop::extractVulkanCommandBuffer(commandBuffer);
    if (m_currentVkCommandBuffer == VK_NULL_HANDLE) {
        return false;
    }
    
    // Extract native framebuffer from Slang framebuffer
    // Note: This requires additional Slang GFX interop API
    VkFramebuffer vkFramebuffer = VK_NULL_HANDLE; // TODO: Extract from framebuffer parameter
    
    if (!beginVulkanRenderPass(m_currentVkCommandBuffer, vkFramebuffer)) {
        return false;
    }
    
#else
    m_currentD3DCommandList = SlangNativeInterop::extractD3D12CommandList(commandBuffer);
    if (m_currentD3DCommandList == nullptr) {
        return false;
    }
    
    if (!beginD3D12Rendering(m_currentD3DCommandList)) {
        return false;
    }
#endif
    
    m_renderPassActive = true;
    return true;
}

void CommandBufferSync::endImGuiRenderPass() {
    if (!m_renderPassActive) {
        return;
    }
    
#ifdef USE_VULKAN
    endVulkanRenderPass(m_currentVkCommandBuffer);
    m_currentVkCommandBuffer = VK_NULL_HANDLE;
#else
    endD3D12Rendering(m_currentD3DCommandList);
    m_currentD3DCommandList = nullptr;
#endif
    
    m_currentCommandBuffer = nullptr;
    m_renderPassActive = false;
}

void CommandBufferSync::renderImGui(ImDrawData* drawData) {
    if (!m_renderPassActive) {
        std::cerr << "❌ Cannot render ImGui: render pass not active" << std::endl;
        return;
    }
    
#ifdef USE_VULKAN
    ImGui_ImplVulkan_RenderDrawData(drawData, m_currentVkCommandBuffer);
#else
    // Ensure descriptor heap is set
    ID3D12DescriptorHeap* heaps[] = { m_d3d12Handles.srvDescHeap };
    m_currentD3DCommandList->SetDescriptorHeaps(_countof(heaps), heaps);
    
    ImGui_ImplDX12_RenderDrawData(drawData, m_currentD3DCommandList);
#endif
}

#ifdef USE_VULKAN
bool CommandBufferSync::beginVulkanRenderPass(VkCommandBuffer cmdBuffer, VkFramebuffer framebuffer) {
    // Begin render pass for ImGui
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass; // From SlangImGuiNative
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {800, 600}; // TODO: Get actual size
    
    // No clear colors - we're rendering over existing content
    renderPassInfo.clearValueCount = 0;
    renderPassInfo.pClearValues = nullptr;
    
    vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    return true;
}

void CommandBufferSync::endVulkanRenderPass(VkCommandBuffer cmdBuffer) {
    vkCmdEndRenderPass(cmdBuffer);
}

#else

bool CommandBufferSync::beginD3D12Rendering(ID3D12GraphicsCommandList* cmdList) {
    // D3D12 doesn't need explicit render pass begin/end for ImGui
    // Just ensure proper render target is set
    return true;
}

void CommandBufferSync::endD3D12Rendering(ID3D12GraphicsCommandList* cmdList) {
    // No explicit end needed for D3D12 ImGui rendering
}

#endif
```

**Checkpoints**:
- [ ] Command buffer extraction works correctly
- [ ] Render pass lifecycle managed properly
- [ ] Resource barriers don't interfere with existing content
- [ ] Frame synchronization works across multiple frames
- [ ] No rendering artifacts or validation errors

**Test Scenario**: Render scene content followed by ImGui overlay.

**Test Commands**: 
_[To be filled manually]_

**Final Acceptance Criteria**:
_[To be filled manually]_

---

## Stage 4: SlangDisplay Integration

**Objective**: Integrate the native ImGui backend into SlangDisplay with proper resource management.

**Tasks**:
1. Update SlangDisplay to use native ImGui backend
2. Handle initialization and cleanup properly
3. Integrate with existing renderImGuiDrawData method
4. Add configuration options for different backends

**Implementation Details**:

### Update SlangDisplay Header
```cpp
// filepath: c:\dev\ChameleonRT\backends\slang\slangdisplay.h
// Add includes:
#include "slang_imgui_native.h"

// Add to SlangDisplay class private members:
private:
    std::unique_ptr<SlangImGuiNative> m_imguiNative;
    std::unique_ptr<CommandBufferSync> m_commandSync;
    bool m_imguiInitialized = false;

// Add to SlangDisplay class public methods:
public:
    bool initializeImGuiNative();
    void shutdownImGuiNative();
```

### Update SlangDisplay Implementation
```cpp
// filepath: c:\dev\ChameleonRT\backends\slang\slangdisplay.cpp
// Replace the renderImGuiDrawData method:
void SlangDisplay::renderImGuiDrawData(gfx::ICommandBuffer* commandBuffer, gfx::IFramebuffer* framebuffer) {
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->CmdListsCount == 0) {
        return;
    }

    // Initialize ImGui native backend on first use
    if (!m_imguiInitialized) {
        if (!initializeImGuiNative()) {
            std::cerr << "❌ Failed to initialize ImGui native backend" << std::endl;
            return;
        }
        m_imguiInitialized = true;
    }

    // Begin ImGui rendering with proper synchronization
    if (!m_commandSync->beginImGuiRenderPass(commandBuffer, framebuffer)) {
        std::cerr << "❌ Failed to begin ImGui render pass" << std::endl;
        return;
    }

    // Render ImGui through native backend
    m_commandSync->renderImGui(drawData);

    // End ImGui rendering
    m_commandSync->endImGuiRenderPass();
}

bool SlangDisplay::initializeImGuiNative() {
    m_imguiNative = std::make_unique<SlangImGuiNative>();
    m_commandSync = std::make_unique<CommandBufferSync>();
    
    // Determine swapchain format
    gfx::Format swapchainFormat = gfx::Format::R8G8B8A8_UNORM; // Default
    
    if (!m_imguiNative->initialize(device.get(), queue.get(), swapchainFormat)) {
        std::cerr << "❌ Failed to initialize SlangImGuiNative" << std::endl;
        m_imguiNative.reset();
        m_commandSync.reset();
        return false;
    }
    
    std::cout << "✅ SlangImGuiNative initialized successfully" << std::endl;
    return true;
}

void SlangDisplay::shutdownImGuiNative() {
    if (m_imguiNative) {
        m_imguiNative->shutdown();
        m_imguiNative.reset();
    }
    
    if (m_commandSync) {
        m_commandSync.reset();
    }
    
    m_imguiInitialized = false;
}

// Update destructor:
SlangDisplay::~SlangDisplay() {
    shutdownImGuiNative();
    
    // ... existing cleanup code remains the same
}
```

### Add CMake/Build Integration
```cmake
# Add to backends/slang/CMakeLists.txt or appropriate build file
if(USE_VULKAN)
    target_sources(slang_backend PRIVATE
        slang_native_interop.cpp
        slang_imgui_native.cpp
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    )
    target_link_libraries(slang_backend PRIVATE vulkan)
else()
    target_sources(slang_backend PRIVATE
        slang_native_interop.cpp
        slang_imgui_native.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp
    )
    target_link_libraries(slang_backend PRIVATE d3d12 dxgi)
endif()

target_include_directories(slang_backend PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
```

**Checkpoints**:
- [ ] SlangDisplay compiles with native ImGui integration
- [ ] Both Vulkan and D3D12 backends initialize correctly
- [ ] Resource management prevents leaks
- [ ] Error handling is comprehensive
- [ ] Performance is equivalent to VKDisplay

**Test Scenario**: Launch ChameleonRT with Slang backend and verify UI matches VKDisplay.

**Test Commands**: 
_[To be filled manually]_

**Final Acceptance Criteria**:
- [ ] All ImGui widgets render identically to VKDisplay
- [ ] UI responsiveness matches native backends
- [ ] Font rendering quality is identical
- [ ] Performance is within 5% of VKDisplay
- [ ] Works on both Vulkan and D3D12 configurations
- [ ] No memory leaks or resource issues
- [ ] Graceful error handling for all failure modes
- [ ] Proper cleanup on application shutdown

---

## Stage 5: Testing and Optimization

**Objective**: Comprehensive testing and performance optimization of the native backend integration.

**Tasks**:
1. Create comprehensive test scenarios
2. Performance benchmarking against VKDisplay
3. Memory leak detection and profiling
4. Edge case handling and error recovery

**Implementation Details**:

### Create Test Framework
```cpp
// Create new file: c:\dev\ChameleonRT\backends\slang\imgui_test_suite.h
#pragma once
#include <vector>
#include <string>
#include <functional>

class ImGuiTestSuite {
public:
    struct TestCase {
        std::string name;
        std::function<bool()> testFunction;
        std::string description;
    };
    
    void addTest(const std::string& name, std::function<bool()> test, const std::string& desc);
    bool runAllTests();
    bool runTest(const std::string& name);
    void reportResults();
    
private:
    std::vector<TestCase> m_tests;
    std::vector<std::string> m_passedTests;
    std::vector<std::string> m_failedTests;
    
    bool testBasicRendering();
    bool testLargeUI();
    bool testFontRendering();
    bool testInputHandling();
    bool testMemoryUsage();
    bool testPerformance();
};
```

### Implement Performance Monitoring
```cpp
// Add performance monitoring to slang_imgui_native.cpp
class PerformanceMonitor {
private:
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    std::vector<float> m_frameTimes;
    size_t m_frameIndex = 0;
    
public:
    void beginFrame() {
        m_lastFrameTime = std::chrono::high_resolution_clock::now();
    }
    
    void endFrame() {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - m_lastFrameTime);
        
        if (m_frameTimes.size() < 1000) {
            m_frameTimes.push_back(duration.count() / 1000.0f); // Convert to milliseconds
        } else {
            m_frameTimes[m_frameIndex % m_frameTimes.size()] = duration.count() / 1000.0f;
        }
        m_frameIndex++;
    }
    
    float getAverageFrameTime() const {
        if (m_frameTimes.empty()) return 0.0f;
        
        float sum = 0.0f;
        for (float time : m_frameTimes) {
            sum += time;
        }
        return sum / m_frameTimes.size();
    }
    
    float getMaxFrameTime() const {
        if (m_frameTimes.empty()) return 0.0f;
        return *std::max_element(m_frameTimes.begin(), m_frameTimes.end());
    }
};
```

### Add Memory Monitoring
```cpp
// Add to slang_imgui_native.cpp
class MemoryTracker {
private:
    size_t m_peakMemoryUsage = 0;
    size_t m_currentMemoryUsage = 0;
    
public:
    void recordAllocation(size_t size) {
        m_currentMemoryUsage += size;
        m_peakMemoryUsage = std::max(m_peakMemoryUsage, m_currentMemoryUsage);
    }
    
    void recordDeallocation(size_t size) {
        m_currentMemoryUsage = (size > m_currentMemoryUsage) ? 0 : m_currentMemoryUsage - size;
    }
    
    size_t getCurrentUsage() const { return m_currentMemoryUsage; }
    size_t getPeakUsage() const { return m_peakMemoryUsage; }
    
    void logMemoryStats() {
        std::cout << "💾 Memory Usage - Current: " << (m_currentMemoryUsage / 1024) << " KB, "
                  << "Peak: " << (m_peakMemoryUsage / 1024) << " KB" << std::endl;
    }
};
```

**Checkpoints**:
- [ ] All test cases pass consistently
- [ ] Performance within 5% of VKDisplay baseline
- [ ] Memory usage stable across extended runs
- [ ] No validation errors in debug builds
- [ ] Proper error recovery from all failure modes

**Test Scenario**: Run extended stress test with complex UI for 30+ minutes.

**Test Commands**: 
_[To be filled manually]_

**Final Acceptance Criteria**:
_[To be filled manually]_

---

## Implementation Notes

### Architecture Benefits
- **Proven Stability**: Uses battle-tested Dear ImGui backends
- **Feature Complete**: All ImGui features work immediately
- **Performance Optimized**: Leverages hand-tuned native implementations
- **Maintenance**: Easier debugging with well-documented backends

### Integration Challenges
- **API Interop**: Requires careful handle extraction from Slang GFX
- **Synchronization**: Must properly coordinate with Slang command recording
- **Resource Management**: Need to avoid conflicts between Slang and native resources
- **Version Compatibility**: Must maintain compatibility with ImGui backend updates

### Debugging Tips
- Use graphics debuggers (RenderDoc, PIX) to verify command streams
- Enable native API validation layers
- Monitor resource creation/destruction carefully
- Test handle extraction with simple cases first

### Performance Considerations
- Native backends are highly optimized for their respective APIs
- Minimal overhead from handle extraction
- Proper resource pooling prevents allocation stalls
- Command buffer recording should be efficient

### Common Pitfalls
- Ensure proper command buffer state when extracting handles
- Validate extracted handles before use
- Handle case where Slang GFX creates resources differently than expected
- Test with different driver versions and hardware configurations
