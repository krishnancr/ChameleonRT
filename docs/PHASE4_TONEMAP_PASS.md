# Phase 4: Separate Tonemap Pass

## Overview

This phase extracts tonemapping from the raygen shader into a separate compute shader pass, matching AfraChameleonRT's architecture. This completes the OIDN integration by establishing the proper rendering pipeline:

```
Ray Trace → Accumulate (linear HDR) → OIDN Denoise → Tonemap (compute) → Display
```

### Prerequisites

- **Phase 1**: Buffer conversion complete (Texture2D → Buffer)
- **Phase 2**: Auxiliary buffers complete (AccumPixel struct with color/albedo/normal)
- **Phase 3**: OIDN integration complete (denoising writes to denoise_buffer)

### Goals

| Goal | Description |
|------|-------------|
| Clean separation | Raygen shader outputs linear HDR only |
| Proper OIDN workflow | Tonemap reads from `denoise_buffer` (or `accum_buffer` when OIDN disabled) |
| Unified shader | Single `tonemap.slang` works for DXR and Vulkan |
| No visual change | Output should be identical to Phase 3 |

---

## Task Summary

```
┌──────────────────────────────────────────────────────────────────┐
│ Task 4.1: Create tonemap.slang compute shader                    │
│   - Read from denoise_buffer (OIDN) or accum_buffer              │
│   - Apply linear_to_srgb conversion                              │
│   - Write to outputTexture (framebuffer)                         │
├──────────────────────────────────────────────────────────────────┤
│ Task 4.2: Remove inline tonemapping from unified_render.slang    │
│   - Raygen shader writes linear HDR to accum_buffer only         │
│   - Remove sRGB conversion and framebuffer write                 │
├──────────────────────────────────────────────────────────────────┤
│ Task 4.3: DXR Backend - Add compute pipeline for tonemap         │
│   - Create compute pipeline state                                │
│   - Add command list for tonemap dispatch                        │
│   - Execute tonemap after OIDN (or after raygen if no OIDN)      │
├──────────────────────────────────────────────────────────────────┤
│ Task 4.4: Vulkan Backend - Add compute pipeline for tonemap      │
│   - Create compute pipeline                                      │
│   - Add command buffer for tonemap dispatch                      │
│   - Execute tonemap after OIDN (or after raygen if no OIDN)      │
├──────────────────────────────────────────────────────────────────┤
│ Task 4.5: Build & Verify                                         │
│   - Verify identical output to Phase 3                           │
│   - Verify OIDN path still works                                 │
│   - Verify non-OIDN path still works                             │
└──────────────────────────────────────────────────────────────────┘
```

---

## Reference: AfraChameleonRT Implementation

### DXR Tonemap Shader (`backends/dxr/tonemap.hlsl`)

```hlsl
#include "util.hlsl"

RWTexture2D<float4> framebuffer : register(u0);

#ifdef ENABLE_OIDN
RWStructuredBuffer<float4> color_buffer : register(u2);  // denoise_buffer
#else
RWStructuredBuffer<float4> color_buffer : register(u1);  // accum_buffer
#endif

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
  const uint2 pixel = dispatchThreadId.xy;
  uint2 dims;
  framebuffer.GetDimensions(dims.x, dims.y);
  if (pixel.x >= dims.x || pixel.y >= dims.y)
    return;

  const uint pixel_index = dims.x * pixel.y + pixel.x;
  const float4 color = color_buffer[pixel_index];
  framebuffer[pixel] = float4(linear_to_srgb(color.r),
                              linear_to_srgb(color.g),
                              linear_to_srgb(color.b), 1.f);
}
```

### Vulkan Tonemap Shader (`backends/vulkan/tonemap.comp`)

```glsl
#version 460

#include "util.glsl"

layout(binding = 1, set = 0, rgba8) uniform writeonly image2D framebuffer;

#ifdef ENABLE_OIDN
layout(binding = 7, set = 0) buffer ColorBuffer { vec4 color_buffer[]; }; // denoise_buffer
#else
layout(binding = 2, set = 0) buffer ColorBuffer { vec4 color_buffer[]; }; // accum_buffer
#endif

layout(local_size_x = 16, local_size_y = 16) in;

void main() {
  const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
  const ivec2 dims = imageSize(framebuffer);
  if (pixel.x >= dims.x || pixel.y >= dims.y)
    return;

  const uint pixel_index = dims.x * pixel.y + pixel.x;
  vec4 color = color_buffer[pixel_index];
  color.xyz = vec3(linear_to_srgb(color.x), linear_to_srgb(color.y),
          linear_to_srgb(color.z));
  imageStore(framebuffer, pixel, vec4(color.xyz, 1.f));
}
```

### DXR Pipeline Setup (`render_dxr.cpp` lines 734-745)

```cpp
// Tonemap
tonemap_root_sig =
    dxr::RootSignatureBuilder::global()
        .add_desc_heap("cbv_srv_uav_heap", raygen_desc_heap)
        .create(device.Get());

D3D12_COMPUTE_PIPELINE_STATE_DESC tonemap_pso = {};
tonemap_pso.pRootSignature = tonemap_root_sig.get();
tonemap_pso.CS.pShaderBytecode = tonemap_dxil;
tonemap_pso.CS.BytecodeLength = sizeof(tonemap_dxil);
tonemap_pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
CHECK_ERR(device->CreateComputePipelineState(&tonemap_pso, IID_PPV_ARGS(&tonemap_ps)));
```

### DXR Dispatch (`render_dxr.cpp` lines 1040-1052)

```cpp
// Tonemap
CHECK_ERR(tonemap_cmd_list->Reset(render_cmd_allocator.Get(), nullptr));
tonemap_cmd_list->SetDescriptorHeaps(desc_heaps.size(), desc_heaps.data());
tonemap_cmd_list->SetPipelineState(tonemap_ps.Get());
tonemap_cmd_list->SetComputeRootSignature(tonemap_root_sig.get());
tonemap_cmd_list->SetComputeRootDescriptorTable(0, raygen_desc_heap.gpu_desc_handle());

glm::uvec2 dispatch_dim = render_target.dims();
glm::uvec2 workgroup_dim(16, 16);
dispatch_dim = (dispatch_dim + workgroup_dim - glm::uvec2(1)) / workgroup_dim;
tonemap_cmd_list->Dispatch(dispatch_dim.x, dispatch_dim.y, 1);

CHECK_ERR(tonemap_cmd_list->Close());
```

### Vulkan Pipeline Setup (`render_vulkan.cpp` lines 937-941)

```cpp
// Load the shader modules for our tonemap pipeline and build the pipeline
auto tonemap_shader =
    std::make_shared<vkrt::ShaderModule>(*device, tonemap_spv, sizeof(tonemap_spv));

tonemap_pipeline = vkrt::build_compute_pipeline(*device, pipeline_layout, tonemap_shader);
```

### Vulkan Dispatch (`render_vulkan.cpp` lines 1183-1207)

```cpp
// Tonemap
CHECK_VULKAN(vkBeginCommandBuffer(tonemap_cmd_buf, &begin_info));

vkCmdBindPipeline(
    tonemap_cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, tonemap_pipeline);

vkCmdBindDescriptorSets(tonemap_cmd_buf,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline_layout,
                        0,
                        descriptor_sets.size(),
                        descriptor_sets.data(),
                        0,
                        nullptr);

glm::uvec2 dispatch_dim = render_target->dims();
glm::uvec2 workgroup_dim(16, 16);
dispatch_dim = (dispatch_dim + workgroup_dim - glm::uvec2(1)) / workgroup_dim;

vkCmdDispatch(tonemap_cmd_buf,
              dispatch_dim.x,
              dispatch_dim.y,
              1);

CHECK_VULKAN(vkEndCommandBuffer(tonemap_cmd_buf));
```

---

## Task 4.1: Create Tonemap Slang Shader

### Create `shaders/tonemap.slang`

```slang
// Tonemap compute shader
// Reads linear HDR from color buffer, applies sRGB conversion, writes to framebuffer

import modules.util;

// Output framebuffer (UAV texture)
RWTexture2D<float4> framebuffer : register(u0);

// Input color buffer - reads from denoise_buffer when OIDN enabled, else accum_buffer
#ifdef ENABLE_OIDN
RWStructuredBuffer<float4> color_buffer : register(u2);  // denoise_buffer
#else
// When OIDN disabled, read from accum_buffer.color (first element of AccumPixel struct)
struct AccumPixel {
    float4 color;
    float4 albedo;
    float4 normal;
};
RWStructuredBuffer<AccumPixel> accum_buffer : register(u1);
#endif

[numthreads(16, 16, 1)]
[shader("compute")]
void TonemapMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    
    // Get framebuffer dimensions
    uint2 dims;
    framebuffer.GetDimensions(dims.x, dims.y);
    
    // Bounds check
    if (pixel.x >= dims.x || pixel.y >= dims.y)
        return;
    
    // Calculate pixel index
    uint pixel_index = dims.x * pixel.y + pixel.x;
    
    // Read linear HDR color
#ifdef ENABLE_OIDN
    float4 color = color_buffer[pixel_index];
#else
    float4 color = accum_buffer[pixel_index].color;
#endif
    
    // Apply sRGB conversion (tonemapping)
    float3 srgb_color = float3(
        linear_to_srgb(color.r),
        linear_to_srgb(color.g),
        linear_to_srgb(color.b)
    );
    
    // Write to framebuffer
    framebuffer[pixel] = float4(srgb_color, 1.0f);
}
```

### Notes

- Uses `import modules.util` for `linear_to_srgb()` function (already exists)
- Workgroup size 16×16 matches AfraChameleonRT
- Register bindings match existing descriptor heap layout

---

## Task 4.2: Remove Inline Tonemapping from Raygen Shader

### Modify `shaders/unified_render.slang`

**CURRENT (end of RayGen function):**
```slang
    // ===== PROGRESSIVE ACCUMULATION (Task 3.4.5) =====
    // Accumulate across frames for temporal convergence
    float4 accum_color = accum_buffer[pixel];
    accum_color = (float4(illum, 1.0) + frame_id * accum_color) / (frame_id + 1);
    accum_buffer[pixel] = accum_color;
    
    // ===== sRGB CONVERSION (Task 3.4.6) =====
    // Convert linear accumulated color to sRGB for display
    float3 srgb_color = float3(
        linear_to_srgb(accum_color.r),
        linear_to_srgb(accum_color.g),
        linear_to_srgb(accum_color.b)
    );
    
    // Write sRGB-corrected color to output
    outputTexture[pixel] = float4(srgb_color, 1.0f);
```

**CHANGE TO:**
```slang
    // ===== PROGRESSIVE ACCUMULATION (Task 3.4.5) =====
    // Accumulate across frames for temporal convergence
    // Note: After Phase 2, this writes to AccumPixel.color
    AccumPixel accum = accum_buffer[pixel_idx];
    accum.color = (float4(illum, 1.0) + frame_id * accum.color) / (frame_id + 1);
    
    // Store first-hit albedo and normal (Phase 2)
    accum.albedo = first_albedo;
    accum.normal = first_normal;
    
    accum_buffer[pixel_idx] = accum;
    
    // Tonemapping moved to separate compute pass (Phase 4)
    // The tonemap shader will read from denoise_buffer (OIDN) or accum_buffer
```

### Key Changes

1. **Remove** `outputTexture[pixel]` write from raygen
2. **Remove** sRGB conversion code
3. Raygen now only writes linear HDR to `accum_buffer`
4. Tonemap compute shader handles framebuffer write

---

## Task 4.3: DXR Backend - Add Compute Pipeline

### 4.3.1 Add Member Variables

**File:** `backends/dxr/render_dxr.h`

Add member variables:

```cpp
// Tonemap compute pipeline
ComPtr<ID3D12GraphicsCommandList4> tonemap_cmd_list;
dxr::RootSignature tonemap_root_sig;
ComPtr<ID3D12PipelineState> tonemap_ps;
```

### 4.3.2 Create Command List

**File:** `backends/dxr/render_dxr.cpp`

In initialization (after creating other command lists):

```cpp
// Create tonemap command list
CHECK_ERR(device->CreateCommandList(0,
                                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    render_cmd_allocator.Get(),
                                    nullptr,
                                    IID_PPV_ARGS(&tonemap_cmd_list)));
CHECK_ERR(tonemap_cmd_list->Close());
```

### 4.3.3 Build Compute Pipeline

**File:** `backends/dxr/render_dxr.cpp`

After building the RT pipeline, add:

```cpp
// Build tonemap compute pipeline
void RenderDXR::build_tonemap_pipeline()
{
    // Compile tonemap shader using Slang
    auto tonemap_shader = compile_slang_shader("shaders/tonemap.slang", "TonemapMain", "cs_6_3");
    
    // Create root signature (shares descriptor heap with raygen)
    tonemap_root_sig =
        dxr::RootSignatureBuilder::global()
            .add_desc_heap("cbv_srv_uav_heap", raygen_desc_heap)
            .create(device.Get());
    
    // Create compute pipeline state
    D3D12_COMPUTE_PIPELINE_STATE_DESC tonemap_pso = {};
    tonemap_pso.pRootSignature = tonemap_root_sig.get();
    tonemap_pso.CS.pShaderBytecode = tonemap_shader.data();
    tonemap_pso.CS.BytecodeLength = tonemap_shader.size();
    tonemap_pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    
    CHECK_ERR(device->CreateComputePipelineState(&tonemap_pso, IID_PPV_ARGS(&tonemap_ps)));
}
```

### 4.3.4 Execute Tonemap Pass

**File:** `backends/dxr/render_dxr.cpp`

In `render()` function, after OIDN execution (or after raygen if no OIDN):

```cpp
#ifdef ENABLE_OIDN
    // Execute OIDN denoising
    oidn_filter.execute();
    oidn_device.sync();
#endif

    // Execute tonemap compute pass
    CHECK_ERR(tonemap_cmd_list->Reset(render_cmd_allocator.Get(), nullptr));
    
    // Set descriptor heaps
    ID3D12DescriptorHeap* desc_heaps[] = { raygen_desc_heap.heap() };
    tonemap_cmd_list->SetDescriptorHeaps(1, desc_heaps);
    
    // Bind pipeline
    tonemap_cmd_list->SetPipelineState(tonemap_ps.Get());
    tonemap_cmd_list->SetComputeRootSignature(tonemap_root_sig.get());
    tonemap_cmd_list->SetComputeRootDescriptorTable(0, raygen_desc_heap.gpu_desc_handle());
    
    // Dispatch (16x16 workgroups)
    glm::uvec2 dispatch_dim = render_target.dims();
    glm::uvec2 workgroup_dim(16, 16);
    dispatch_dim = (dispatch_dim + workgroup_dim - glm::uvec2(1)) / workgroup_dim;
    tonemap_cmd_list->Dispatch(dispatch_dim.x, dispatch_dim.y, 1);
    
    CHECK_ERR(tonemap_cmd_list->Close());
    
    // Submit tonemap command list
    ID3D12CommandList* tonemap_cmds = tonemap_cmd_list.Get();
    cmd_queue->ExecuteCommandLists(1, &tonemap_cmds);
```

### 4.3.5 Update Render Order

The render loop should now be:

```cpp
// 1. Execute ray tracing (writes linear HDR to accum_buffer)
ID3D12CommandList* render_cmds = render_cmd_list.Get();
cmd_queue->ExecuteCommandLists(1, &render_cmds);

// 2. Execute OIDN (reads accum_buffer, writes to denoise_buffer)
#ifdef ENABLE_OIDN
oidn_filter.execute();
oidn_device.sync();
#endif

// 3. Execute tonemap (reads denoise_buffer/accum_buffer, writes to framebuffer)
ID3D12CommandList* tonemap_cmds = tonemap_cmd_list.Get();
cmd_queue->ExecuteCommandLists(1, &tonemap_cmds);

// 4. Readback framebuffer for display
// ... existing readback code ...
```

---

## Task 4.4: Vulkan Backend - Add Compute Pipeline

### 4.4.1 Add Member Variables

**File:** `backends/vulkan/render_vulkan.h`

Add member variables:

```cpp
// Tonemap compute pipeline
VkCommandBuffer tonemap_cmd_buf = VK_NULL_HANDLE;
VkPipeline tonemap_pipeline = VK_NULL_HANDLE;
```

### 4.4.2 Create Command Buffer

**File:** `backends/vulkan/render_vulkan.cpp`

In initialization:

```cpp
// Allocate tonemap command buffer
VkCommandBufferAllocateInfo alloc_info = {};
alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
alloc_info.commandPool = command_pool;
alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
alloc_info.commandBufferCount = 1;
CHECK_VULKAN(vkAllocateCommandBuffers(device->logical_device(), &alloc_info, &tonemap_cmd_buf));
```

### 4.4.3 Build Compute Pipeline

**File:** `backends/vulkan/render_vulkan.cpp`

```cpp
void RenderVulkan::build_tonemap_pipeline()
{
    // Compile tonemap shader using Slang for SPIR-V
    auto tonemap_spirv = compile_slang_shader_spirv("shaders/tonemap.slang", "TonemapMain");
    
    auto tonemap_shader = std::make_shared<vkrt::ShaderModule>(*device, 
                                                               tonemap_spirv.data(), 
                                                               tonemap_spirv.size());
    
    tonemap_pipeline = vkrt::build_compute_pipeline(*device, pipeline_layout, tonemap_shader);
}
```

### 4.4.4 Execute Tonemap Pass

**File:** `backends/vulkan/render_vulkan.cpp`

In `render()` function:

```cpp
#ifdef ENABLE_OIDN
    // Execute OIDN denoising
    oidn_filter.execute();
    oidn_device.sync();
#endif

    // Execute tonemap compute pass
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    CHECK_VULKAN(vkBeginCommandBuffer(tonemap_cmd_buf, &begin_info));
    
    vkCmdBindPipeline(tonemap_cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, tonemap_pipeline);
    
    vkCmdBindDescriptorSets(tonemap_cmd_buf,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_layout,
                            0,
                            descriptor_sets.size(),
                            descriptor_sets.data(),
                            0,
                            nullptr);
    
    // Dispatch (16x16 workgroups)
    glm::uvec2 dispatch_dim = render_target->dims();
    glm::uvec2 workgroup_dim(16, 16);
    dispatch_dim = (dispatch_dim + workgroup_dim - glm::uvec2(1)) / workgroup_dim;
    
    vkCmdDispatch(tonemap_cmd_buf, dispatch_dim.x, dispatch_dim.y, 1);
    
    CHECK_VULKAN(vkEndCommandBuffer(tonemap_cmd_buf));
    
    // Submit tonemap command buffer
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &tonemap_cmd_buf;
    CHECK_VULKAN(vkQueueSubmit(cmd_queue, 1, &submit_info, VK_NULL_HANDLE));
```

### 4.4.5 Cleanup

**File:** `backends/vulkan/render_vulkan.cpp`

In destructor:

```cpp
if (tonemap_pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device->logical_device(), tonemap_pipeline, nullptr);
}
```

---

## Task 4.5: Build & Verify

### Build Commands

```powershell
cd C:\Users\kchunang\dev\ChameleonRT\build

# Configure (with or without OIDN)
cmake .. -DENABLE_OIDN=ON   # or OFF to test non-OIDN path

# Build
cmake --build . --config Release
```

### Verification Steps

1. **Test with OIDN enabled:**
   ```powershell
   .\Release\chameleonrt.exe dxr path\to\scene.gltf
   ```
   - Should show denoised output
   - Output should match Phase 3

2. **Test with OIDN disabled:**
   ```powershell
   # Rebuild without OIDN
   cmake .. -DENABLE_OIDN=OFF
   cmake --build . --config Release
   
   .\Release\chameleonrt.exe dxr path\to\scene.gltf
   ```
   - Should show noisy output (same as Phase 2)
   - Tonemap pass still runs, reading from accum_buffer

3. **Test Vulkan backend:**
   ```powershell
   .\Release\chameleonrt.exe vulkan path\to\scene.gltf
   ```

### Success Criteria

- [ ] Application runs without crashes
- [ ] OIDN path produces same output as Phase 3
- [ ] Non-OIDN path produces same output as Phase 2
- [ ] No visual artifacts from pipeline change
- [ ] Performance is acceptable (compute dispatch overhead minimal)

---

## Pipeline Comparison

### Before Phase 4 (Inline Tonemapping)

```
┌─────────────────────────────────────────────────────────────┐
│                     Raygen Shader                           │
│  1. Path trace → linear HDR                                 │
│  2. Accumulate to accum_buffer                              │
│  3. (OIDN) Denoise → denoise_buffer                         │
│  4. Read from denoise_buffer/accum_buffer                   │
│  5. Apply sRGB conversion                                   │
│  6. Write to framebuffer                                    │
└─────────────────────────────────────────────────────────────┘
```

### After Phase 4 (Separate Tonemap Pass)

```
┌─────────────────────────────────────────────────────────────┐
│                     Raygen Shader                           │
│  1. Path trace → linear HDR                                 │
│  2. Accumulate to accum_buffer (color, albedo, normal)      │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                     OIDN (if enabled)                       │
│  Input: accum_buffer                                        │
│  Output: denoise_buffer                                     │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                     Tonemap Shader                          │
│  1. Read from denoise_buffer (OIDN) or accum_buffer         │
│  2. Apply sRGB conversion                                   │
│  3. Write to framebuffer                                    │
└─────────────────────────────────────────────────────────────┘
```

---

## Summary of Changes

| File | Change |
|------|--------|
| `shaders/tonemap.slang` | **New file** - Tonemap compute shader |
| `shaders/unified_render.slang` | Remove sRGB conversion and framebuffer write |
| `backends/dxr/render_dxr.h` | Add tonemap pipeline member variables |
| `backends/dxr/render_dxr.cpp` | Create tonemap pipeline, execute dispatch |
| `backends/vulkan/render_vulkan.h` | Add tonemap pipeline member variables |
| `backends/vulkan/render_vulkan.cpp` | Create tonemap pipeline, execute dispatch |

---

## Benefits of Separate Tonemap Pass

1. **Clean Architecture**: Each shader has single responsibility
2. **Flexibility**: Easy to swap tonemap operators (ACES, Reinhard, etc.)
3. **Proper OIDN Workflow**: Denoising happens on linear HDR data
4. **Future Extensions**: Can add exposure control, bloom, etc. as post-process
5. **Debugging**: Can visualize linear HDR buffer separately

---

## Future Enhancements

1. **Advanced Tonemapping**: Add ACES, Reinhard, or filmic tonemap operators
2. **Exposure Control**: Add UI slider for exposure adjustment
3. **HDR Output**: Option to output HDR to HDR displays
4. **Post-Process Stack**: Add bloom, vignette, chromatic aberration
