# OIDN Integration Plan for ChameleonRT

## Executive Summary

This document details how Intel Open Image Denoise (OIDN) is integrated into AfraChameleonRT and provides a comprehensive plan for replicating this integration in ChameleonRT.

---

## ⚠️ Implementation Approach: Phased Rollout

Due to architectural differences between ChameleonRT and AfraChameleonRT, we are implementing OIDN in **three phases**:

| Phase | Goal | Visual Change | Document |
|-------|------|---------------|----------|
| **Phase 1** | Convert `accum_buffer` from Texture2D to Buffer | None (identical output) | [docs/PHASE1_BUFFER_CONVERSION.md](docs/PHASE1_BUFFER_CONVERSION.md) |
| **Phase 2** | Add OIDN denoising pipeline | Denoised output | (TBD) |
| **Phase 3** | Separate tonemap pass (Slang compute shader) | None (identical output) | (TBD) |

### Why Phased?

ChameleonRT differs from AfraChameleonRT in key ways:
- **Slang unified shaders** vs separate HLSL/GLSL (benefit: one shader change covers DXR + Vulkan)
- **Inline tonemapping** in raygen shader vs separate tonemap compute pass
- **Texture2D** for accum_buffer vs Buffer (OIDN requires linear buffer memory)

By breaking into phases, we:
1. Reduce risk - each phase can be verified independently
2. Enable rollback - if a phase fails, we don't lose prior work
3. Isolate issues - easier to debug which change caused problems

---

## Assessment: How Hard is This Integration?

### Difficulty Rating: **MODERATE** (3/5)

**Reasons:**

1. **Well-Structured Existing Code**: The AfraChameleonRT integration follows a clean, consistent pattern across all backends (DXR, Vulkan, Metal). This makes it easy to understand and replicate.

2. **Compile-Time Conditional**: Everything is wrapped in `#ifdef ENABLE_OIDN` guards, making it a completely optional feature that doesn't affect existing functionality.

3. **Buffer Memory Sharing Complexity**: The trickiest part is setting up shared memory between the graphics API and OIDN, which differs per backend:
   - **DXR**: Uses Win32 shared handles (`D3D12_HEAP_FLAG_SHARED`)
   - **Vulkan**: Uses external memory extensions (varies by platform: OpaqueFD, DMABuf, OpaqueWin32)
   - **Metal**: Native MTLBuffer sharing (simplest)

4. **Shader Modifications Required**: Shaders must be modified to output albedo and normal auxiliary buffers alongside the color buffer.

5. **CMake Integration**: Straightforward - just find and link the OpenImageDenoise package.

---

## Understanding the AfraChameleonRT Integration

### Architecture Overview

The OIDN integration follows this pattern across all backends:

```
┌─────────────────────────────────────────────────────────────────┐
│                     Render Pass (GPU)                           │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Path Tracing Kernel                                     │   │
│  │  - Outputs: color, first-hit albedo, first-hit normal   │   │
│  │  - All accumulated progressively                         │   │
│  └─────────────────────────────────────────────────────────┘   │
│                           │                                     │
│                           ▼                                     │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  accum_buffer (3x vec4 per pixel with OIDN)             │   │
│  │  Layout: [color.rgb, alpha][albedo.rgb, _][normal.rgb,_]│   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                           │
                           ▼ (Shared Memory)
┌─────────────────────────────────────────────────────────────────┐
│                     OIDN Denoiser (CPU/GPU)                     │
│  - Input: color, albedo, normal from accum_buffer               │
│  - Output: denoised color to denoise_buffer                     │
│  - HDR mode enabled                                              │
│  - Quality: Balanced                                             │
└─────────────────────────────────────────────────────────────────┘
                           │
                           ▼ (Shared Memory)
┌─────────────────────────────────────────────────────────────────┐
│                     Tonemap Pass (GPU)                          │
│  - Reads from denoise_buffer (instead of accum_buffer)          │
│  - Applies sRGB conversion                                       │
│  - Outputs to render_target texture                              │
└─────────────────────────────────────────────────────────────────┘
```

### Key Components Modified

| Component | Files Modified | Purpose |
|-----------|---------------|---------|
| CMake Build System | `util/CMakeLists.txt`, `backends/*/CMakeLists.txt` | Find OIDN, pass compile definitions |
| Utility Shaders | `util.hlsl`, `util.glsl`, `util.metal` | Extended `AccumPixel` struct |
| Render Shaders | `render_dxr.hlsl`, `raygen.rgen`, `render_metal.metal` | Capture first-hit albedo/normal |
| Tonemap Shaders | `tonemap.hlsl`, `tonemap.comp` | Read from denoise_buffer |
| Backend Headers | `render_dxr.h`, `render_vulkan.h`, `render_metal.h` | OIDN device/filter refs, denoise_buffer |
| Backend Implementations | `render_dxr.cpp`, `render_vulkan.cpp`, `render_metal.mm` | Initialize OIDN, execute filter |
| Vulkan Utils | `vulkan_utils.cpp`, `vulkan_utils.h` | External memory support |
| Main Application | `main.cpp` | UI indicator for OIDN |

---

## Detailed Implementation Stages

### Stage 1: CMake Configuration

#### 1.1 Add OIDN Option to `util/CMakeLists.txt`

Add at the end of the file (after line ~60):

```cmake
option(ENABLE_OIDN "Build with Intel Open Image Denoise support." OFF)
if(ENABLE_OIDN)
    find_package(OpenImageDenoise REQUIRED)
    target_compile_definitions(util PUBLIC ENABLE_OIDN)
    target_link_libraries(util PUBLIC OpenImageDenoise)
endif()
```

#### 1.2 Update Backend CMakeLists.txt Files

For each backend, add OIDN to shader compile definitions:

**DXR (`backends/dxr/CMakeLists.txt`):**
```cmake
set(HLSL_COMPILE_DEFNS "")
if (REPORT_RAY_STATS)
    set(HLSL_COMPILE_DEFNS "REPORT_RAY_STATS=1")
endif()
if(ENABLE_OIDN)
    list(APPEND HLSL_COMPILE_DEFNS "ENABLE_OIDN=1")
endif()
```

**Vulkan (`backends/vulkan/CMakeLists.txt`):**
```cmake
set(GLSL_COMPILE_DEFNS "")
if (REPORT_RAY_STATS)
    set(GLSL_COMPILE_DEFNS "REPORT_RAY_STATS=1")
endif()
if(ENABLE_OIDN)
    list(APPEND GLSL_COMPILE_DEFNS "ENABLE_OIDN=1")
endif()
```

**Metal (`backends/metal/CMakeLists.txt`):**
```cmake
set(METAL_COMPILE_DEFNS "")
if (REPORT_RAY_STATS)
    set(METAL_COMPILE_DEFNS "REPORT_RAY_STATS=1")
endif()
if (ENABLE_OIDN)
    set(METAL_COMPILE_DEFNS "${METAL_COMPILE_DEFNS} ENABLE_OIDN=1")
endif()
```

---

### Stage 2: Shader Modifications

#### 2.1 Extend AccumPixel Structure

In each backend's utility header/shader:

**HLSL (`backends/dxr/util.hlsl`):**
```hlsl
struct AccumPixel {
    float4 color;
#ifdef ENABLE_OIDN
    float4 albedo;
    float4 normal;
#endif
};
```

**GLSL (`backends/vulkan/util.glsl`):**
```glsl
struct AccumPixel {
    vec4 color;
#ifdef ENABLE_OIDN
    vec4 albedo;
    vec4 normal;
#endif
};
```

**Metal (`backends/metal/util.metal`):**
```metal
struct AccumPixel {
    float4 color;
#ifdef ENABLE_OIDN
    float4 albedo;
    float4 normal;
#endif
};
```

#### 2.2 Modify Path Tracing Shaders to Capture Auxiliary Data

In the raygen/main rendering shader, add logic to capture first-hit albedo and normal:

```hlsl
// Declare variables at path tracing start
#ifdef ENABLE_OIDN
    float3 first_albedo = float3(0, 0, 0);
    float3 first_normal = float3(0, 0, 0);
#endif

// In the path tracing loop, on first bounce or miss:
#ifdef ENABLE_OIDN
    if (bounce == 0) {
        first_albedo = mat.base_color;  // or miss color for misses
        first_normal = surface_normal;   // world-space normal
    }
#endif

// After path tracing loop, store to accum_buffer:
#ifdef ENABLE_OIDN
    const float4 accum_albedo = (float4(first_albedo, 1.0) + frame_id * accum_buffer[pixel_index].albedo) / (frame_id + 1);
    accum_buffer[pixel_index].albedo = accum_albedo;
    const float4 accum_normal = (float4(first_normal, 1.0) + frame_id * accum_buffer[pixel_index].normal) / (frame_id + 1);
    accum_buffer[pixel_index].normal = accum_normal;
#endif
```

#### 2.3 Modify Tonemap Shader

Change the tonemap shader to read from `denoise_buffer` when OIDN is enabled:

```hlsl
#ifdef ENABLE_OIDN
RWStructuredBuffer<float4> color_buffer : register(u2);  // denoise_buffer
#else
RWStructuredBuffer<float4> color_buffer : register(u1);  // accum_buffer
#endif
```

---

### Stage 3: Backend C++ Implementation

Each backend needs to:
1. Include OIDN header
2. Declare OIDN device, filter, and denoise buffer
3. Initialize OIDN device (matching GPU device)
4. Create shared-memory buffers
5. Configure OIDN filter
6. Execute filter after render, before tonemap

#### 3.1 DXR Backend

**`render_dxr.h` additions:**
```cpp
#pragma once
// ... existing includes ...
#ifdef ENABLE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

struct RenderDXR : RenderBackend {
    // ... existing members ...
    
#ifdef ENABLE_OIDN
    dxr::Buffer denoise_buffer;
    oidn::DeviceRef oidn_device;
    oidn::FilterRef oidn_filter;
#endif
};
```

**`render_dxr.cpp` - initialize() additions:**
```cpp
void RenderDXR::initialize(const int fb_width, const int fb_height)
{
#ifdef ENABLE_OIDN
    // Get the LUID of the adapter
    LUID luid = device->GetAdapterLuid();

    // Initialize the denoiser device using adapter LUID
    oidn_device = oidn::newDevice(oidn::LUID{luid.LowPart, luid.HighPart});
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to create OIDN device.");
    oidn_device.commit();
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to commit OIDN device.");

    // Verify external memory support
    const auto oidn_external_mem_types = oidn_device.get<oidn::ExternalMemoryTypeFlags>("externalMemoryTypes");
    if (!(oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::OpaqueWin32))
        throw std::runtime_error("failed to find compatible external memory type");
#endif

    // ... existing initialization ...
    
#ifdef ENABLE_OIDN
    // Create accum_buffer with 3x size (color + albedo + normal) and shared heap
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       3 * sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_HEAP_FLAG_SHARED);

    // Create denoise output buffer with shared heap
    denoise_buffer = dxr::Buffer::device(device.Get(),
                                         sizeof(glm::vec4) * fb_width * fb_height,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_HEAP_FLAG_SHARED);
#else
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
#endif

    // ... after other initialization ...

#ifdef ENABLE_OIDN
    {
        // Initialize the denoiser filter
        oidn_filter = oidn_device.newFilter("RT");
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to create OIDN filter.");

        // Create shared handles for buffers
        HANDLE accum_buffer_handle = nullptr;
        CHECK_ERR(device->CreateSharedHandle(
                    accum_buffer.get(), nullptr, GENERIC_ALL, nullptr, &accum_buffer_handle));
        auto input_buffer = oidn_device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32,
                                                  accum_buffer_handle, nullptr, accum_buffer.size());

        HANDLE denoise_buffer_handle = nullptr;
        CHECK_ERR(device->CreateSharedHandle(
                    denoise_buffer.get(), nullptr, GENERIC_ALL, nullptr, &denoise_buffer_handle));
        auto output_buffer = oidn_device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32,
                                                   denoise_buffer_handle, nullptr, denoise_buffer.size());

        // Configure filter with interleaved layout
        // Input buffer layout: [color0, albedo0, normal0, color1, albedo1, normal1, ...]
        oidn_filter.setImage("color",  input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                             0 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));  // offset, stride
        oidn_filter.setImage("albedo", input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                             1 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
        oidn_filter.setImage("normal", input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                             2 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));

        oidn_filter.setImage("output", output_buffer, oidn::Format::Float3, fb_width, fb_height,
                             0, sizeof(glm::vec4));

        oidn_filter.set("hdr", true);               // HDR input
        oidn_filter.set("quality", oidn::Quality::Balanced);
        
        oidn_filter.commit();
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to commit OIDN filter.");
    }
#endif
}
```

**`render_dxr.cpp` - render() additions:**
```cpp
RenderStats RenderDXR::render(...)
{
    // ... existing render dispatch ...
    
    // After GPU render completes, before tonemap:
#ifdef ENABLE_OIDN
    // Denoise the frame
    oidn_filter.execute();
#endif

    // Tonemap the frame
    {
        ID3D12CommandList *tonemap_cmds = tonemap_cmd_list.Get();
        cmd_queue->ExecuteCommandLists(1, &tonemap_cmds);
    }
    
    // ... rest of function ...
}
```

#### 3.2 Vulkan Backend

The Vulkan backend requires additional external memory extension handling.

**`vulkan_utils.h` additions:**
```cpp
class Buffer {
    // ... existing ...
    
#ifdef _WIN32
    HANDLE external_mem_handle(VkExternalMemoryHandleTypeFlagBits handle_type);
#else
    int external_mem_handle(VkExternalMemoryHandleTypeFlagBits handle_type);
#endif
};
```

**`vulkan_utils.cpp` additions:**

1. Load external memory extension functions
2. Add external memory parameters to buffer creation
3. Implement `external_mem_handle()` method

**`render_vulkan.h` additions:**
```cpp
#ifdef ENABLE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

struct RenderVulkan : RenderBackend {
    // ... existing ...
    
#ifdef ENABLE_OIDN
    std::shared_ptr<vkrt::Buffer> denoise_buffer;
    oidn::DeviceRef oidn_device;
    oidn::FilterRef oidn_filter;
#endif
};
```

**`render_vulkan.cpp` additions:**

Similar pattern to DXR, but uses Vulkan UUID for device matching and platform-specific external memory types:
- Windows: `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT`
- Linux: `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT` or `VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT`

#### 3.3 Metal Backend

Metal is simplest as MTLBuffers can be directly wrapped by OIDN.

**`render_metal.h` additions:**
```cpp
#ifdef ENABLE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

struct RenderMetal : RenderBackend {
    // ... existing ...
    
#ifdef ENABLE_OIDN
    std::shared_ptr<metal::Buffer> denoise_buffer;
    oidn::DeviceRef oidn_device;
    oidn::FilterRef oidn_filter;
#endif
};
```

**`render_metal.mm` additions:**
```objc
void RenderMetal::initialize(const int fb_width, const int fb_height)
{
#ifdef ENABLE_OIDN
    // Initialize the denoiser device with Metal command queue
    oidn_device = oidn::newMetalDevice(context->command_queue);
    oidn_device.commit();
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to initialize OIDN device.");
#endif

    // ... buffer creation with 3x size for OIDN ...

#ifdef ENABLE_OIDN
    {
        oidn_filter = oidn_device.newFilter("RT");

        // Wrap Metal buffers directly
        auto input_buffer  = oidn_device.newBuffer(accum_buffer->buffer);
        auto output_buffer = oidn_device.newBuffer(denoise_buffer->buffer);

        oidn_filter.setImage("color",  input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                             0 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
        oidn_filter.setImage("albedo", input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                             1 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
        oidn_filter.setImage("normal", input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                             2 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));

        oidn_filter.setImage("output", output_buffer, oidn::Format::Float3, fb_width, fb_height,
                             0, sizeof(glm::vec4));

        oidn_filter.set("hdr", true);
        oidn_filter.set("quality", oidn::Quality::Balanced);
        oidn_filter.commit();
    }
#endif
}
```

For Metal, use `executeAsync()` for better performance:
```objc
#ifdef ENABLE_OIDN
    oidn_filter.executeAsync();
#endif
```

---

### Stage 4: Descriptor/Resource Binding Updates

Each backend needs to update its descriptor heap/set bindings to include the denoise_buffer.

**DXR:** Add UAV for denoise_buffer in `build_descriptor_heap()`
**Vulkan:** Add binding 7 for denoise_buffer in descriptor layout and updates
**Metal:** Pass denoise_buffer to tonemap shader

---

### Stage 5: UI Update

**`main.cpp` additions:**
```cpp
#ifdef ENABLE_OIDN
    ImGui::Text("Denoiser: Intel(R) Open Image Denoise");
#endif
```

---

## Files to Modify (Complete List)

### Core/CMake
| File | Changes |
|------|---------|
| `util/CMakeLists.txt` | Add OIDN option and linking |
| `backends/dxr/CMakeLists.txt` | Add ENABLE_OIDN to shader compile definitions |
| `backends/vulkan/CMakeLists.txt` | Add ENABLE_OIDN to shader compile definitions |
| `backends/metal/CMakeLists.txt` | Add ENABLE_OIDN to shader compile definitions |

### DXR Backend (Windows only)
| File | Changes |
|------|---------|
| `backends/dxr/render_dxr.h` | Add OIDN includes, device/filter/buffer members |
| `backends/dxr/render_dxr.cpp` | Initialize OIDN, create shared buffers, execute filter |
| `backends/dxr/util.hlsl` | Extend AccumPixel struct |
| `backends/dxr/render_dxr.hlsl` | Capture first-hit albedo/normal, write to accum_buffer |
| `backends/dxr/tonemap.hlsl` | Read from denoise_buffer when OIDN enabled |

### Vulkan Backend
| File | Changes |
|------|---------|
| `backends/vulkan/render_vulkan.h` | Add OIDN includes, device/filter/buffer members |
| `backends/vulkan/render_vulkan.cpp` | Initialize OIDN, create external memory buffers, execute filter |
| `backends/vulkan/vulkan_utils.h` | Add external_mem_handle method |
| `backends/vulkan/vulkan_utils.cpp` | Implement external memory support |
| `backends/vulkan/util.glsl` | Extend AccumPixel struct |
| `backends/vulkan/raygen.rgen` | Capture first-hit albedo/normal |
| `backends/vulkan/tonemap.comp` | Read from denoise_buffer |

### Metal Backend (macOS only)
| File | Changes |
|------|---------|
| `backends/metal/render_metal.h` | Add OIDN includes, device/filter/buffer members |
| `backends/metal/render_metal.mm` | Initialize OIDN, execute filter |
| `backends/metal/util.metal` | Extend AccumPixel struct |
| `backends/metal/render_metal.metal` | Capture first-hit albedo/normal |

### Main Application
| File | Changes |
|------|---------|
| `main.cpp` | Add UI indicator for OIDN |

---

## Build Instructions

To build with OIDN support:

```bash
cmake -B build -DENABLE_OIDN=ON -DOpenImageDenoise_DIR=/path/to/oidn/lib/cmake/OpenImageDenoise ..
cmake --build build
```

**Prerequisites:**
- Intel Open Image Denoise 2.x (for GPU denoising support)
- For Vulkan: External memory extensions support in GPU driver

---

## Testing Checklist

- [ ] Build without OIDN (verify no regressions)
- [ ] Build with OIDN on Windows (DXR backend)
- [ ] Build with OIDN on Windows (Vulkan backend)
- [ ] Build with OIDN on Linux (Vulkan backend)
- [ ] Build with OIDN on macOS (Metal backend)
- [ ] Verify denoising quality on noisy renders
- [ ] Verify performance impact is acceptable
- [ ] Test window resize handling
- [ ] Test scene switching

---

## Notes and Considerations

1. **OIDN Version**: This integration uses OIDN 2.x features (GPU denoising, external memory). OIDN 1.x would require host-side memory copies.

2. **Buffer Layout**: The interleaved layout `[color, albedo, normal]` per pixel is more cache-friendly than separate buffers.

3. **Quality Setting**: `oidn::Quality::Balanced` is used for real-time performance. `oidn::Quality::High` can be used for final renders.

4. **HDR Mode**: HDR is enabled since path tracing produces HDR radiance values. Tonemapping happens after denoising.

5. **Async Execution**: Metal uses `executeAsync()` for better performance. DXR/Vulkan use synchronous `execute()`.

6. **Error Handling**: OIDN errors should be checked after each operation to catch configuration issues early.
