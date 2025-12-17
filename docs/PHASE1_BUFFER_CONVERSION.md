# Phase 1: Buffer Conversion for OIDN Integration

## Overview

**Goal:** Convert `accum_buffer` from `Texture2D` to `Buffer` while maintaining identical rendering output.

**Why:** OIDN requires linear buffer memory (not tiled texture memory) for GPU-based denoising with shared memory handles.

**Expected Result:** After Phase 1, rendering Cornell Box (or any scene) produces **identical visual output** as before. The only change is internal - where accumulation data is stored.

---

## Task Execution Order

```
┌─────────────────────────────────────────────────────────────────┐
│ Task 1.1: Slang Shader Change (unified_render.slang)            │
│   - Change accum_buffer declaration                             │
│   - Update read/write indexing                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 1.2: DXR Backend Header (render_dxr.h)                     │
│   - Change accum_buffer type declaration                        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 1.3: DXR Backend Implementation (render_dxr.cpp)           │
│   - Change buffer creation                                      │
│   - Update descriptor heap binding (UAV for buffer)             │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 1.4: Vulkan Backend Header (render_vulkan.h)               │
│   - Change accum_buffer type declaration                        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 1.5: Vulkan Backend Implementation (render_vulkan.cpp)     │
│   - Change buffer creation                                      │
│   - Update descriptor layout (STORAGE_BUFFER vs STORAGE_IMAGE)  │
│   - Update descriptor set writes                                │
│   - Remove image layout transition for accum_buffer             │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 1.6: Build & Verify                                        │
│   - Compile both backends                                       │
│   - Run and verify identical output                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## Task 1.1: Slang Shader Change

**File:** `shaders/unified_render.slang`

### 1.1.1 Change Vulkan Declaration (Lines 16-17)

**CURRENT:**
```slang
[[vk::binding(1, 0)]] RWTexture2D<float4> outputTexture;
[[vk::binding(2, 0)]] RWTexture2D<float4> accum_buffer;
```

**CHANGE TO:**
```slang
[[vk::binding(1, 0)]] RWTexture2D<float4> outputTexture;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> accum_buffer;
```

### 1.1.2 Change DXR Declaration (Lines 43-44)

**CURRENT:**
```slang
RWTexture2D<float4> outputTexture : register(u0);
RWTexture2D<float4> accum_buffer : register(u1);
```

**CHANGE TO:**
```slang
RWTexture2D<float4> outputTexture : register(u0);
RWStructuredBuffer<float4> accum_buffer : register(u1);
```

### 1.1.3 Update Accumulation Read/Write (Lines 431-436)

**CURRENT:**
```slang
    // ===== PROGRESSIVE ACCUMULATION (Task 3.4.5) =====
    // Accumulate across frames for temporal convergence
    float4 accum_color = accum_buffer[pixel];
    accum_color = (float4(illum, 1.0) + frame_id * accum_color) / (frame_id + 1);
    accum_buffer[pixel] = accum_color;
```

**CHANGE TO:**
```slang
    // ===== PROGRESSIVE ACCUMULATION (Task 3.4.5) =====
    // Accumulate across frames for temporal convergence
    uint pixel_idx = pixel.y * uint(dims.x) + pixel.x;
    float4 accum_color = accum_buffer[pixel_idx];
    accum_color = (float4(illum, 1.0) + frame_id * accum_color) / (frame_id + 1);
    accum_buffer[pixel_idx] = accum_color;
```

### Success Criteria
- [ ] Shader compiles with Slang compiler for DXR target
- [ ] Shader compiles with Slang compiler for Vulkan/SPIRV target

---

## Task 1.2: DXR Backend Header

**File:** `backends/dxr/render_dxr.h`

### 1.2.1 Change accum_buffer Type Declaration (Line 42)

**CURRENT:**
```cpp
    dxr::Texture2D render_target, accum_buffer, ray_stats;
```

**CHANGE TO:**
```cpp
    dxr::Texture2D render_target, ray_stats;
    dxr::Buffer accum_buffer;
```

### Success Criteria
- [ ] Header compiles without errors

---

## Task 1.3: DXR Backend Implementation

**File:** `backends/dxr/render_dxr.cpp`

### 1.3.1 Change Buffer Creation in initialize() (Lines 84-88)

**CURRENT:**
```cpp
    accum_buffer = dxr::Texture2D::device(device.Get(),
                                          glm::uvec2(fb_width, fb_height),
                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                          DXGI_FORMAT_R32G32B32A32_FLOAT,
                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
```

**CHANGE TO:**
```cpp
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
```

**Reference (AfraChameleonRT `render_dxr.cpp` lines 111-114):**
```cpp
     accum_buffer = dxr::Buffer::device(device.Get(),
                                        sizeof(glm::vec4) * fb_width * fb_height,
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
```

### 1.3.2 Update Descriptor Heap Binding in build_descriptor_heap()

Find the accum_buffer UAV creation. 

**CURRENT:**
```cpp
    // Accum buffer
    device->CreateUnorderedAccessView(accum_buffer.get(), nullptr, &uav_desc, heap_handle);
    heap_handle.ptr +=
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
```

**CHANGE TO:**
```cpp
    // Accum buffer (structured buffer UAV)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC buffer_uav_desc{};
        buffer_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        buffer_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        buffer_uav_desc.Buffer.FirstElement = 0;
        buffer_uav_desc.Buffer.StructureByteStride = sizeof(glm::vec4);
        buffer_uav_desc.Buffer.NumElements = accum_buffer.size() / sizeof(glm::vec4);
        buffer_uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(accum_buffer.get(), nullptr, &buffer_uav_desc, heap_handle);
        heap_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
```

**Reference (AfraChameleonRT `render_dxr.cpp` lines 894-906):**
```cpp
    // Accum buffer
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.StructureByteStride = sizeof(glm::vec4);
        uav_desc.Buffer.NumElements = accum_buffer.size() / uav_desc.Buffer.StructureByteStride;
        uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(accum_buffer.get(), nullptr, &uav_desc, heap_handle);
        heap_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
```

### Success Criteria
- [ ] DXR backend compiles without errors
- [ ] DXR backend links without errors

---

## Task 1.4: Vulkan Backend Header

**File:** `backends/vulkan/render_vulkan.h`

### 1.4.1 Change accum_buffer Type Declaration (Line 37)

**CURRENT:**
```cpp
    std::shared_ptr<vkrt::Texture2D> render_target, accum_buffer;
```

**CHANGE TO:**
```cpp
    std::shared_ptr<vkrt::Texture2D> render_target;
    std::shared_ptr<vkrt::Buffer> accum_buffer;
```

### Success Criteria
- [ ] Header compiles without errors

---

## Task 1.5: Vulkan Backend Implementation

**File:** `backends/vulkan/render_vulkan.cpp`

### 1.5.1 Change Buffer Creation in initialize() (Lines 109-112)

**CURRENT:**
```cpp
    accum_buffer = vkrt::Texture2D::device(*device,
                                           glm::uvec2(fb_width, fb_height),
                                           VK_FORMAT_R32G32B32A32_SFLOAT,
                                           VK_IMAGE_USAGE_STORAGE_BIT);
```

**CHANGE TO:**
```cpp
    accum_buffer = vkrt::Buffer::device(*device,
                                        sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
```

**Reference (AfraChameleonRT `render_vulkan.cpp` lines 149-151):**
```cpp
    accum_buffer = vkrt::Buffer::device(*device,
                                        sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
```

### 1.5.2 Update Image Layout Transition (Lines 137-155)

Remove `accum_buffer` from image barriers since it's now a buffer, not an image.

**CURRENT:**
```cpp
#ifndef REPORT_RAY_STATS
        std::array<VkImageMemoryBarrier, 2> barriers = {};
#else
        std::array<VkImageMemoryBarrier, 3> barriers = {};
#endif
        // ... barrier setup ...
        barriers[0].image = render_target->image_handle();
        barriers[1].image = accum_buffer->image_handle();
#ifdef REPORT_RAY_STATS
        barriers[2].image = ray_stats->image_handle();
#endif
```

**CHANGE TO:**
```cpp
#ifndef REPORT_RAY_STATS
        std::array<VkImageMemoryBarrier, 1> barriers = {};
#else
        std::array<VkImageMemoryBarrier, 2> barriers = {};
#endif
        // ... barrier setup ...
        barriers[0].image = render_target->image_handle();
#ifdef REPORT_RAY_STATS
        barriers[1].image = ray_stats->image_handle();
#endif
```

### 1.5.3 Update Descriptor Layout Builder

Find the descriptor layout builder and change binding 2 from `STORAGE_IMAGE` to `STORAGE_BUFFER`.

**CURRENT:**
```cpp
            .add_binding(
                2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
```

**CHANGE TO:**
```cpp
            .add_binding(
                2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
```

### 1.5.4 Update Descriptor Set Writes (Two Locations)

**Location 1 - In initialize() resize handler:**

**CURRENT:**
```cpp
        vkrt::DescriptorSetUpdater()
            .write_storage_image(desc_set, 1, render_target)
            .write_storage_image(desc_set, 2, accum_buffer)
```

**CHANGE TO:**
```cpp
        vkrt::DescriptorSetUpdater()
            .write_storage_image(desc_set, 1, render_target)
            .write_ssbo(desc_set, 2, accum_buffer)
```

**Location 2 - In set_scene() or wherever descriptors are written:**

**CURRENT:**
```cpp
                       .write_storage_image(desc_set, 1, render_target)
                       .write_storage_image(desc_set, 2, accum_buffer)
```

**CHANGE TO:**
```cpp
                       .write_storage_image(desc_set, 1, render_target)
                       .write_ssbo(desc_set, 2, accum_buffer)
```

**Reference (AfraChameleonRT `render_vulkan.cpp` lines 233-234):**
```cpp
            .write_storage_image(desc_set, 1, render_target)
            .write_ssbo(desc_set, 2, accum_buffer)
```

### Success Criteria
- [ ] Vulkan backend compiles without errors
- [ ] Vulkan backend links without errors

---

## Task 1.6: Build & Verify

### Build Commands

```powershell
# Navigate to build directory
cd C:\Users\kchunang\dev\ChameleonRT\build

# Build DXR backend
cmake --build . --target crt_dxr --config Release

# Build Vulkan backend  
cmake --build . --target crt_vulkan --config Release
```

### Verification Steps

1. **Run with DXR backend:**
   ```powershell
   .\Release\chameleonrt.exe dxr path\to\cornell_box.gltf
   ```

2. **Run with Vulkan backend:**
   ```powershell
   .\Release\chameleonrt.exe vulkan path\to\cornell_box.gltf
   ```

### Success Criteria
- [ ] DXR backend renders scene without crashes
- [ ] Vulkan backend renders scene without crashes
- [ ] Progressive accumulation works (image converges over frames)
- [ ] No visual artifacts or corruption
- [ ] Output is visually identical to pre-change (can screenshot compare)
- [ ] Frame rate is similar to pre-change (no major performance regression)

---

## Rollback Plan

If any task fails:
1. `git stash` or `git checkout -- .` to revert all changes
2. Each task can be committed separately for granular rollback:
   - Commit after Task 1.1 (shader)
   - Commit after Tasks 1.2-1.3 (DXR complete)
   - Commit after Tasks 1.4-1.5 (Vulkan complete)

---

## Code Snippets for Automated Implementation

### All-in-One Implementation Script

For Claude Code or similar tools, here are the exact changes:

```
FILES TO MODIFY:
1. shaders/unified_render.slang
2. backends/dxr/render_dxr.h
3. backends/dxr/render_dxr.cpp
4. backends/vulkan/render_vulkan.h
5. backends/vulkan/render_vulkan.cpp

CHANGES SUMMARY:
- unified_render.slang: RWTexture2D<float4> accum_buffer → RWStructuredBuffer<float4> accum_buffer (2 places)
- unified_render.slang: accum_buffer[pixel] → accum_buffer[pixel.y * uint(dims.x) + pixel.x] (2 places)
- render_dxr.h: dxr::Texture2D accum_buffer → dxr::Buffer accum_buffer
- render_dxr.cpp: Texture2D::device() → Buffer::device() for accum_buffer
- render_dxr.cpp: Texture UAV → Buffer UAV in build_descriptor_heap()
- render_vulkan.h: shared_ptr<vkrt::Texture2D> accum_buffer → shared_ptr<vkrt::Buffer> accum_buffer
- render_vulkan.cpp: Texture2D::device() → Buffer::device() for accum_buffer
- render_vulkan.cpp: Remove accum_buffer from image barriers
- render_vulkan.cpp: VK_DESCRIPTOR_TYPE_STORAGE_IMAGE → VK_DESCRIPTOR_TYPE_STORAGE_BUFFER for binding 2
- render_vulkan.cpp: write_storage_image → write_ssbo for accum_buffer (2 places)
```

---

## Next Phase Preview

After Phase 1 is complete and verified:

**Phase 2: OIDN Integration**
- Add OIDN CMake configuration
- Add denoise_buffer
- Expand accum_buffer to 3x size (color + albedo + normal)
- Add OIDN device/filter initialization
- Execute OIDN filter after render

**Phase 3: Tonemap Shader**
- Create tonemap.slang compute shader
- Add tonemap pipeline
- Move sRGB conversion from raygen to tonemap
