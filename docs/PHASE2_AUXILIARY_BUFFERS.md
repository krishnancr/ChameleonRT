# Phase 2: Auxiliary Buffers for OIDN (Color, Albedo, Normal)

## Overview

**Goal:** Expand `accum_buffer` to hold color, albedo, and normal data using a struct-based layout matching AfraChameleonRT.

**Why:** OIDN uses auxiliary "albedo" and "normal" data to improve denoising quality:
- **Albedo**: Surface color without lighting ("what color is the object")
- **Normal**: First-hit surface normal (geometry normal, helps preserve edges)

**Expected Result:** **Identical visual output** - we write albedo/normal but only display color.

**Prerequisites:** Phase 1 complete (accum_buffer is already a Buffer, not Texture2D)

---

## What Happens at End of Phase 2?

```
                         PHASE 2 (No OIDN yet)
                         =====================

                        Ray Tracing (Slang)
                              │
                              ▼
                    ┌─────────────────────────┐
                    │ accum_buffer            │
                    │ ┌─────────────────────┐ │
                    │ │ AccumPixel {        │ │
                    │ │   float4 color;  ◄──┼─┼── Used for display
                    │ │   float4 albedo;   │ │   (written, NOT read yet)
                    │ │   float4 normal;   │ │   (written, NOT read yet)
                    │ │ }                   │ │
                    │ └─────────────────────┘ │
                    └─────────────────────────┘
                              │
                              │ (inline tonemap reads color)
                              ▼
                    ┌─────────────────────────┐
                    │ sRGB Conversion         │
                    │ (linear_to_srgb)        │
                    └─────────────────────────┘
                              │
                              ▼
                    ┌─────────────────────────┐
                    │ outputTexture           │
                    │ (display)               │
                    └─────────────────────────┘
```

**Key Point:** Albedo and normal are **written but not read** until Phase 3 (OIDN).

---

## Architecture Change

### Before (Phase 1)
```
accum_buffer: [pixel_count × float4]
              └── color (RGB + alpha) per pixel
```

### After (Phase 2) - Struct-Based Layout
```
accum_buffer: [pixel_count × AccumPixel]

struct AccumPixel {
    float4 color;   // RGB illumination + alpha
    float4 albedo;  // RGB first-hit surface color + alpha  
    float4 normal;  // XYZ first-hit normal + alpha
};

Memory Layout (per pixel = 48 bytes = 3 × float4):
┌──────────────────────────────────────────────────────────┐
│ pixel[0]                │ pixel[1]                │ ...  │
│ {color,albedo,normal}   │ {color,albedo,normal}   │      │
└──────────────────────────────────────────────────────────┘
```

---

## Reference: AfraChameleonRT HLSL Implementation

### AccumPixel Struct (`backends/dxr/util.hlsl` lines 28-34)
```hlsl
struct AccumPixel {
    float4 color;
#ifdef ENABLE_OIDN
    float4 albedo;
    float4 normal;
#endif
};
```

### Variable Declaration (`backends/dxr/render_dxr.hlsl` lines 189-192)
```hlsl
#ifdef ENABLE_OIDN
    float3 first_albedo = float3(0, 0, 0);
    float3 first_normal = float3(0, 0, 0);
#endif
```

### Capture on Miss (`backends/dxr/render_dxr.hlsl` lines 204-211)
```hlsl
        // If we hit nothing, include the scene background color from the miss shader
        if (payload.color_dist.w <= 0) {
            illum += path_throughput * payload.color_dist.rgb;
#ifdef ENABLE_OIDN
            if (bounce == 0) {
                first_albedo = payload.color_dist.rgb;
            }
#endif
            break;
        }
```

### Capture on Hit (`backends/dxr/render_dxr.hlsl` lines 225-230)
```hlsl
#ifdef ENABLE_OIDN
        if (bounce == 0) {
            first_albedo = mat.base_color;
            first_normal = v_z;
        }
#endif
```

### Accumulation (`backends/dxr/render_dxr.hlsl` lines 259-268)
```hlsl
    const uint pixel_index = dims.x * pixel.y + pixel.x;

    const float4 accum_color = (float4(illum, 1.0) + frame_id * accum_buffer[pixel_index].color) / (frame_id + 1);
    accum_buffer[pixel_index].color = accum_color;
#ifdef ENABLE_OIDN
    const float4 accum_albedo = (float4(first_albedo, 1.0) + frame_id * accum_buffer[pixel_index].albedo) / (frame_id + 1);
    accum_buffer[pixel_index].albedo = accum_albedo;
    const float4 accum_normal = (float4(first_normal, 1.0) + frame_id * accum_buffer[pixel_index].normal) / (frame_id + 1);
    accum_buffer[pixel_index].normal = accum_normal;
#endif
```

### Buffer Declaration (`backends/dxr/render_dxr.hlsl` line 27)
```hlsl
RWStructuredBuffer<AccumPixel> accum_buffer : register(u1);
```

---

## Task Execution Order

```
┌─────────────────────────────────────────────────────────────────┐
│ Task 2.1: Slang Shader - Add AccumPixel Struct & Bindings       │
│   - Define AccumPixel struct                                    │
│   - Change accum_buffer to RWStructuredBuffer<AccumPixel>       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 2.2: Slang Shader - Add Albedo/Normal Capture              │
│   - Add first_albedo, first_normal variables                    │
│   - Capture on bounce 0 (hit or miss)                           │
│   - Accumulate & write to accum_buffer[pixel_idx].albedo/normal │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 2.3: DXR Backend - Triple Buffer Size                      │
│   - Multiply accum_buffer allocation by 3                       │
│   - Update UAV descriptor StructureByteStride                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 2.4: Vulkan Backend - Triple Buffer Size                   │
│   - Multiply accum_buffer allocation by 3                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 2.5: Build & Verify                                        │
│   - Identical visual output                                     │
│   - Optional: Debug visualization of albedo/normal              │
└─────────────────────────────────────────────────────────────────┘
```

---

## Task 2.1: Slang Shader - Add AccumPixel Struct & Bindings

**File:** `shaders/unified_render.slang`

### 2.1.1 Add AccumPixel Struct Definition

Add this struct near the top of the file, after the existing struct definitions (after `MaterialParams`):

```slang
// Accumulation buffer pixel structure for progressive rendering and OIDN
struct AccumPixel {
    float4 color;   // RGB illumination + alpha
    float4 albedo;  // RGB first-hit surface color + alpha
    float4 normal;  // XYZ first-hit normal + alpha
};
```

**Reference (AfraChameleonRT util.hlsl):**
```hlsl
struct AccumPixel {
    float4 color;
#ifdef ENABLE_OIDN
    float4 albedo;
    float4 normal;
#endif
};
```

> **Note:** We're NOT using `#ifdef ENABLE_OIDN` in ChameleonRT - we always include albedo/normal to keep the shader simpler. The overhead is minimal.

### 2.1.2 Change accum_buffer Declaration (Vulkan)

**CURRENT (after Phase 1):**
```slang
[[vk::binding(1, 0)]] RWTexture2D<float4> outputTexture;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> accum_buffer;
```

**CHANGE TO:**
```slang
[[vk::binding(1, 0)]] RWTexture2D<float4> outputTexture;
[[vk::binding(2, 0)]] RWStructuredBuffer<AccumPixel> accum_buffer;
```

### 2.1.3 Change accum_buffer Declaration (DXR)

**CURRENT (after Phase 1):**
```slang
RWTexture2D<float4> outputTexture : register(u0);
RWStructuredBuffer<float4> accum_buffer : register(u1);
```

**CHANGE TO:**
```slang
RWTexture2D<float4> outputTexture : register(u0);
RWStructuredBuffer<AccumPixel> accum_buffer : register(u1);
```

**Reference (AfraChameleonRT render_dxr.hlsl line 27):**
```hlsl
RWStructuredBuffer<AccumPixel> accum_buffer : register(u1);
```

---

## Task 2.2: Slang Shader - Add Albedo/Normal Capture

**File:** `shaders/unified_render.slang`

### 2.2.1 Add first_albedo and first_normal Variables

Find the multi-sample setup (around line 195-198). Add variables **outside** the SPP loop:

**CURRENT:**
```slang
    // ===== MULTI-SAMPLE ANTI-ALIASING  =====
    float3 illum = float3(0.0f);
    
    for (uint s = 0; s < samples_per_pixel; ++s) {
```

**CHANGE TO:**
```slang
    // ===== MULTI-SAMPLE ANTI-ALIASING  =====
    float3 illum = float3(0.0f);
    float3 first_albedo = float3(0.0f);  // First-hit albedo for OIDN
    float3 first_normal = float3(0.0f);  // First-hit normal for OIDN
    
    for (uint s = 0; s < samples_per_pixel; ++s) {
```

**Reference (AfraChameleonRT render_dxr.hlsl lines 189-192):**
```hlsl
#ifdef ENABLE_OIDN
    float3 first_albedo = float3(0, 0, 0);
    float3 first_normal = float3(0, 0, 0);
#endif
```

### 2.2.2 Capture Albedo/Normal on Miss (First Bounce)

Find the miss handling (around line 245-248):

**CURRENT:**
```slang
        // Miss? Add sky color and terminate
        if (payload.color_dist.w <= 0.0f) {
            sample_illum += path_throughput * payload.color_dist.rgb;  // Sky color from Miss shader
            break;
        }
```

**CHANGE TO:**
```slang
        // Miss? Add sky color and terminate
        if (payload.color_dist.w <= 0.0f) {
            sample_illum += path_throughput * payload.color_dist.rgb;  // Sky color from Miss shader
            // Capture first-hit data for OIDN (sky color as albedo, no normal for miss)
            if (bounce == 0) {
                first_albedo += payload.color_dist.rgb;
                // first_normal stays 0 for sky miss
            }
            break;
        }
```

**Reference (AfraChameleonRT render_dxr.hlsl lines 204-211):**
```hlsl
        if (payload.color_dist.w <= 0) {
            illum += path_throughput * payload.color_dist.rgb;
#ifdef ENABLE_OIDN
            if (bounce == 0) {
                first_albedo = payload.color_dist.rgb;
            }
#endif
            break;
        }
```

### 2.2.3 Capture Albedo/Normal on First Hit

Find where the tangent frame is built (after `ortho_basis`). This is around line 280-285:

**CURRENT:**
```slang
        // Build tangent frame
        float3 v_x, v_y;
        float3 v_z = normal;
        ortho_basis(v_x, v_y, v_z);
        
        // ===== DIRECT LIGHTING with MIS
```

**CHANGE TO:**
```slang
        // Build tangent frame
        float3 v_x, v_y;
        float3 v_z = normal;
        ortho_basis(v_x, v_y, v_z);
        
        // Capture first-hit albedo and normal for OIDN
        if (bounce == 0) {
            first_albedo += mat.base_color;
            first_normal += v_z;
        }
        
        // ===== DIRECT LIGHTING with MIS
```

**Reference (AfraChameleonRT render_dxr.hlsl lines 225-230):**
```hlsl
#ifdef ENABLE_OIDN
        if (bounce == 0) {
            first_albedo = mat.base_color;
            first_normal = v_z;
        }
#endif
```

> **Note:** We use `+=` instead of `=` because we have an SPP loop. AfraChameleonRT doesn't have SPP loop so they use `=`.

### 2.2.4 Average Albedo/Normal After SPP Loop

Find the end of the SPP loop:

**CURRENT:**
```slang
    }  // End of SPP loop
    
    // Average all samples
    illum = illum / float(samples_per_pixel);
```

**CHANGE TO:**
```slang
    }  // End of SPP loop
    
    // Average all samples
    illum = illum / float(samples_per_pixel);
    first_albedo = first_albedo / float(samples_per_pixel);
    first_normal = first_normal / float(samples_per_pixel);
```

### 2.2.5 Update Accumulation Section

Find the progressive accumulation section:

**CURRENT (after Phase 1):**
```slang
    // ===== PROGRESSIVE ACCUMULATION (Task 3.4.5) =====
    // Accumulate across frames for temporal convergence
    uint pixel_idx = pixel.y * uint(dims.x) + pixel.x;
    float4 accum_color = accum_buffer[pixel_idx];
    accum_color = (float4(illum, 1.0) + frame_id * accum_color) / (frame_id + 1);
    accum_buffer[pixel_idx] = accum_color;
```

**CHANGE TO:**
```slang
    // ===== PROGRESSIVE ACCUMULATION (Task 3.4.5) =====
    // Accumulate across frames for temporal convergence
    uint pixel_idx = pixel.y * uint(dims.x) + pixel.x;
    
    // Accumulate color
    float4 accum_color = accum_buffer[pixel_idx].color;
    accum_color = (float4(illum, 1.0) + frame_id * accum_color) / (frame_id + 1);
    accum_buffer[pixel_idx].color = accum_color;
    
    // Accumulate albedo
    float4 accum_albedo = accum_buffer[pixel_idx].albedo;
    accum_albedo = (float4(first_albedo, 1.0) + frame_id * accum_albedo) / (frame_id + 1);
    accum_buffer[pixel_idx].albedo = accum_albedo;
    
    // Accumulate normal
    float4 accum_normal = accum_buffer[pixel_idx].normal;
    accum_normal = (float4(first_normal, 1.0) + frame_id * accum_normal) / (frame_id + 1);
    accum_buffer[pixel_idx].normal = accum_normal;
```

**Reference (AfraChameleonRT render_dxr.hlsl lines 259-268):**
```hlsl
    const uint pixel_index = dims.x * pixel.y + pixel.x;

    const float4 accum_color = (float4(illum, 1.0) + frame_id * accum_buffer[pixel_index].color) / (frame_id + 1);
    accum_buffer[pixel_index].color = accum_color;
#ifdef ENABLE_OIDN
    const float4 accum_albedo = (float4(first_albedo, 1.0) + frame_id * accum_buffer[pixel_index].albedo) / (frame_id + 1);
    accum_buffer[pixel_index].albedo = accum_albedo;
    const float4 accum_normal = (float4(first_normal, 1.0) + frame_id * accum_buffer[pixel_index].normal) / (frame_id + 1);
    accum_buffer[pixel_index].normal = accum_normal;
#endif
```

### 2.2.6 sRGB Conversion (No Change Needed)

The sRGB conversion section continues to use `accum_color` which is already loaded. No changes needed:

```slang
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

---

## Task 2.3: DXR Backend Changes

**File:** `backends/dxr/render_dxr.cpp`

### 2.3.1 Triple Buffer Allocation

**CURRENT (after Phase 1):**
```cpp
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
```

**CHANGE TO:**
```cpp
    // Allocate 3x size for AccumPixel struct: color + albedo + normal
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       3 * sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
```

**Reference (AfraChameleonRT render_dxr.cpp lines 108-114):**
```cpp
#ifdef ENABLE_OIDN
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       3 * sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_HEAP_FLAG_SHARED);
```

> **Note:** We don't add `D3D12_HEAP_FLAG_SHARED` yet - that's for Phase 3 (OIDN).

### 2.3.2 Update UAV Descriptor StructureByteStride

Find the accum_buffer UAV creation in `build_descriptor_heap()`:

**CURRENT (after Phase 1):**
```cpp
    // Accum buffer (structured buffer UAV)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC buffer_uav_desc{};
        buffer_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        buffer_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        buffer_uav_desc.Buffer.FirstElement = 0;
        buffer_uav_desc.Buffer.StructureByteStride = sizeof(glm::vec4);
        buffer_uav_desc.Buffer.NumElements = accum_buffer.size() / sizeof(glm::vec4);
```

**CHANGE TO:**
```cpp
    // Accum buffer (structured buffer UAV) - AccumPixel struct: color + albedo + normal
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC buffer_uav_desc{};
        buffer_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        buffer_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        buffer_uav_desc.Buffer.FirstElement = 0;
        buffer_uav_desc.Buffer.StructureByteStride = 3 * sizeof(glm::vec4);  // AccumPixel = 3 x float4
        buffer_uav_desc.Buffer.NumElements = accum_buffer.size() / (3 * sizeof(glm::vec4));
```

**Reference (AfraChameleonRT render_dxr.cpp lines 894-899):**
```cpp
    #ifdef ENABLE_OIDN
         uav_desc.Buffer.StructureByteStride = 3 * sizeof(glm::vec4);
    #else
         uav_desc.Buffer.StructureByteStride = sizeof(glm::vec4);
    #endif
        uav_desc.Buffer.NumElements = accum_buffer.size() / uav_desc.Buffer.StructureByteStride;
```

---

## Task 2.4: Vulkan Backend Changes

**File:** `backends/vulkan/render_vulkan.cpp`

### 2.4.1 Triple Buffer Allocation

**CURRENT (after Phase 1):**
```cpp
    accum_buffer = vkrt::Buffer::device(*device,
                                        sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
```

**CHANGE TO:**
```cpp
    // Allocate 3x size for AccumPixel struct: color + albedo + normal
    accum_buffer = vkrt::Buffer::device(*device,
                                        3 * sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
```

**Reference (AfraChameleonRT render_vulkan.cpp lines 146-151):**
```cpp
#ifdef ENABLE_OIDN
    accum_buffer = vkrt::Buffer::device(*device,
                                        3 * sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        0,
                                        external_mem_type);
```

> **Note:** We don't add external memory support yet - that's for Phase 3 (OIDN).

---

## Task 2.5: Build & Verify

### Build Commands

```powershell
cd C:\Users\kchunang\dev\ChameleonRT\build

# Build DXR backend
cmake --build . --target crt_dxr --config Release

# Build Vulkan backend  
cmake --build . --target crt_vulkan --config Release
```

### Verification Steps

1. **Run with DXR backend (1 SPP for testing):**
   ```powershell
   .\Release\chameleonrt.exe dxr path\to\cornell_box.gltf
   ```

2. **Run with Vulkan backend (1 SPP for testing):**
   ```powershell
   .\Release\chameleonrt.exe vulkan path\to\cornell_box.gltf
   ```

### Success Criteria

- [ ] DXR backend renders scene without crashes
- [ ] Vulkan backend renders scene without crashes
- [ ] No Vulkan validation layer errors
- [ ] Progressive accumulation still works (image converges over frames)
- [ ] **Visual output is identical to Phase 1** (we're writing albedo/normal but not using them yet)
- [ ] Memory usage increased by ~3x for accum_buffer (3 × float4 per pixel)

---

## Debug Visualization (Optional)

To verify albedo and normal are being captured correctly, temporarily modify the output:

### View Albedo
```slang
    // DEBUG: Output albedo instead of color
    float3 srgb_color = float3(
        linear_to_srgb(accum_albedo.r),
        linear_to_srgb(accum_albedo.g),
        linear_to_srgb(accum_albedo.b)
    );
```

**Expected:** "Flat" rendering - surface colors without lighting effects.

### View Normal
```slang
    // DEBUG: Output normal as color (remap from [-1,1] to [0,1])
    float3 normal_viz = accum_normal.xyz * 0.5 + 0.5;
    outputTexture[pixel] = float4(normal_viz, 1.0f);
```

**Expected:** RGB visualization of surface normals (similar to a normal map texture).

---

## Troubleshooting

### If rendering looks wrong:

1. **Black screen**: Check buffer allocation size is 3× (not 2×)
2. **Garbage output**: Check struct byte stride in UAV descriptor
3. **Albedo all black**: Verify `first_albedo +=` is inside the SPP loop and `if (bounce == 0)`
4. **Normal all zero**: Verify capture happens after `ortho_basis` call

### If crashes occur:

1. **Buffer overrun**: Check `pixel_idx` calculation matches Phase 1
2. **Struct mismatch**: Verify AccumPixel struct in Slang matches C++ layout expectations
3. **Descriptor mismatch**: Verify `StructureByteStride = 3 * sizeof(glm::vec4)`

---

## Summary of Changes

| File | Change |
|------|--------|
| `shaders/unified_render.slang` | Add `AccumPixel` struct, change `accum_buffer` type, add `first_albedo`/`first_normal` capture and accumulation |
| `backends/dxr/render_dxr.cpp` | Buffer size × 3, UAV stride = `3 * sizeof(glm::vec4)` |
| `backends/vulkan/render_vulkan.cpp` | Buffer size × 3 |

---

## Comparison: Phase 1 vs Phase 2

| Aspect | Phase 1 | Phase 2 |
|--------|---------|---------|
| Buffer type | `RWStructuredBuffer<float4>` | `RWStructuredBuffer<AccumPixel>` |
| Buffer size | `N × sizeof(float4)` | `N × 3 × sizeof(float4)` |
| Data stored | color only | color + albedo + normal |
| Data displayed | color | color (albedo/normal ignored) |
| Visual output | Reference image | **Identical to Phase 1** |

---

## Next Phase Preview

**Phase 3: OIDN Integration**
- Add OIDN CMake configuration and library linking
- Add `denoise_buffer` (same size as color portion: `N × sizeof(float4)`)
- Create shared memory handles for GPU buffer sharing
- Initialize OIDN device and filter
- Configure filter:
  - Input: `accum_buffer` (color at offset 0, albedo at offset 1, normal at offset 2)
  - Output: `denoise_buffer`
- Execute denoising after render, before display
- Read denoised color from `denoise_buffer` for sRGB conversion
