# Phase 3: OIDN Denoising Integration

## Overview

**Goal:** Integrate Intel Open Image Denoise (OIDN) to denoise the rendered image using color + albedo + normal auxiliary data.

**Why:** Path tracing produces noisy images at low sample counts. OIDN dramatically reduces noise using ML-based denoising.

**Expected Result:** **Visibly denoised output** - the image should appear much cleaner, especially at low sample counts.

**Prerequisites:** 
- Phase 1 complete (accum_buffer is a Buffer)
- Phase 2 complete (color, albedo, normal captured in accum_buffer)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Render Pass (GPU)                           │
│  Path Tracing → accum_buffer [color | albedo | normal]          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ (Shared GPU Memory)
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     OIDN Denoiser (GPU)                         │
│  Input:  accum_buffer (color + albedo + normal)                 │
│  Output: denoise_buffer                                         │
│  Mode:   HDR, Balanced quality                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ (Shared GPU Memory)
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Tonemap / Display                           │
│  Read from denoise_buffer instead of accum_buffer               │
└─────────────────────────────────────────────────────────────────┘
```

---

## Task Execution Order

```
┌─────────────────────────────────────────────────────────────────┐
│ Task 3.1: CMake Configuration & OIDN Download                   │
│   - Create cmake/oidn.cmake to auto-download OIDN binaries      │
│   - Add ENABLE_OIDN option to util/CMakeLists.txt               │
│   - Configure install rules for OIDN DLLs                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 3.2: DXR Backend - OIDN Integration                        │
│   - Add OIDN headers and member variables                       │
│   - Create shared buffers (D3D12_HEAP_FLAG_SHARED)              │
│   - Initialize OIDN device and filter                           │
│   - Execute filter after render                                 │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 3.3: Vulkan Backend - OIDN Integration                     │
│   - Add OIDN headers and member variables                       │
│   - Create shared buffers (external memory)                     │
│   - Initialize OIDN device and filter                           │
│   - Execute filter after render                                 │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 3.4: Slang Shader - Read from denoise_buffer               │
│   - Add denoise_buffer binding                                  │
│   - Read denoised color for tonemapping                         │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ Task 3.5: Build & Verify                                        │
│   - Enable OIDN in CMake                                        │
│   - Verify denoised output                                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## Task 3.1: CMake Configuration & OIDN Download

### Overview

We will:
1. Create `cmake/oidn.cmake` - Downloads OIDN binaries from GitHub
2. Modify `util/CMakeLists.txt` - Links OIDN when enabled
3. Configure install rules - Copies OIDN DLLs to output directory

### OIDN Version & Download URLs

**Version:** 2.3.3 (Latest as of December 2024)

| Platform | URL |
|----------|-----|
| Windows x64 | `https://github.com/RenderKit/oidn/releases/download/v2.3.3/oidn-2.3.3.x64.windows.zip` |
| Linux x86_64 | `https://github.com/RenderKit/oidn/releases/download/v2.3.3/oidn-2.3.3.x86_64.linux.tar.gz` |
| macOS x86_64 | `https://github.com/RenderKit/oidn/releases/download/v2.3.3/oidn-2.3.3.x86_64.macos.tar.gz` |
| macOS ARM64 | `https://github.com/RenderKit/oidn/releases/download/v2.3.3/oidn-2.3.3.arm64.macos.tar.gz` |

### 3.1.1 Create `cmake/oidn.cmake`

**File:** `cmake/oidn.cmake` (NEW FILE)

```cmake
# cmake/oidn.cmake
# Auto-download Intel Open Image Denoise (OIDN) from GitHub releases

include(ExternalProject)

set(OIDN_VERSION "2.3.3")

# Determine platform-specific download URL and archive format
if(WIN32)
    set(OIDN_PLATFORM "x64.windows")
    set(OIDN_ARCHIVE_EXT "zip")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(OIDN_PLATFORM "arm64.macos")
    else()
        set(OIDN_PLATFORM "x86_64.macos")
    endif()
    set(OIDN_ARCHIVE_EXT "tar.gz")
else()
    # Linux
    set(OIDN_PLATFORM "x86_64.linux")
    set(OIDN_ARCHIVE_EXT "tar.gz")
endif()

set(OIDN_ARCHIVE_NAME "oidn-${OIDN_VERSION}.${OIDN_PLATFORM}.${OIDN_ARCHIVE_EXT}")
set(OIDN_DOWNLOAD_URL "https://github.com/RenderKit/oidn/releases/download/v${OIDN_VERSION}/${OIDN_ARCHIVE_NAME}")
set(OIDN_EXTRACTED_DIR "oidn-${OIDN_VERSION}.${OIDN_PLATFORM}")

message(STATUS "OIDN: Will download from ${OIDN_DOWNLOAD_URL}")

# Download and extract OIDN
ExternalProject_Add(oidn_ext
    PREFIX oidn
    DOWNLOAD_DIR oidn
    STAMP_DIR oidn/stamp
    SOURCE_DIR oidn/src
    BINARY_DIR oidn
    URL "${OIDN_DOWNLOAD_URL}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_ALWAYS OFF
)

# Set paths to OIDN after extraction
set(OIDN_ROOT ${CMAKE_CURRENT_BINARY_DIR}/oidn/src/${OIDN_EXTRACTED_DIR})
set(OIDN_INCLUDE_DIR ${OIDN_ROOT}/include)
set(OIDN_LIB_DIR ${OIDN_ROOT}/lib)
set(OIDN_BIN_DIR ${OIDN_ROOT}/bin)

# Create imported target for OIDN
add_library(OpenImageDenoise SHARED IMPORTED GLOBAL)
add_dependencies(OpenImageDenoise oidn_ext)

if(WIN32)
    set_target_properties(OpenImageDenoise PROPERTIES
        IMPORTED_LOCATION "${OIDN_BIN_DIR}/OpenImageDenoise.dll"
        IMPORTED_IMPLIB "${OIDN_LIB_DIR}/OpenImageDenoise.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${OIDN_INCLUDE_DIR}"
    )
    
    # List of OIDN DLLs that need to be copied to output directory
    set(OIDN_DLLS
        "${OIDN_BIN_DIR}/OpenImageDenoise.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_core.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_device_cpu.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_device_cuda.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_device_hip.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_device_sycl.dll"
        CACHE INTERNAL "OIDN DLLs to copy"
    )
    
    # TBB DLLs (OIDN dependency)
    list(APPEND OIDN_DLLS "${OIDN_BIN_DIR}/tbb12.dll")
    
elseif(APPLE)
    set_target_properties(OpenImageDenoise PROPERTIES
        IMPORTED_LOCATION "${OIDN_LIB_DIR}/libOpenImageDenoise.dylib"
        INTERFACE_INCLUDE_DIRECTORIES "${OIDN_INCLUDE_DIR}"
    )
    
    # List of OIDN dylibs
    file(GLOB OIDN_DYLIBS "${OIDN_LIB_DIR}/*.dylib")
    set(OIDN_DLLS ${OIDN_DYLIBS} CACHE INTERNAL "OIDN dylibs to copy")
    
else()
    # Linux
    set_target_properties(OpenImageDenoise PROPERTIES
        IMPORTED_LOCATION "${OIDN_LIB_DIR}/libOpenImageDenoise.so"
        INTERFACE_INCLUDE_DIRECTORIES "${OIDN_INCLUDE_DIR}"
    )
    
    # List of OIDN shared libraries
    file(GLOB OIDN_SOS "${OIDN_LIB_DIR}/*.so*")
    set(OIDN_DLLS ${OIDN_SOS} CACHE INTERNAL "OIDN shared libs to copy")
endif()

# Function to copy OIDN DLLs to target output directory
function(copy_oidn_dlls TARGET_NAME)
    if(WIN32)
        foreach(DLL ${OIDN_DLLS})
            if(EXISTS "${DLL}")
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL}"
                        "$<TARGET_FILE_DIR:${TARGET_NAME}>"
                    COMMENT "Copying ${DLL} to output directory"
                )
            endif()
        endforeach()
    elseif(APPLE)
        foreach(DYLIB ${OIDN_DLLS})
            if(EXISTS "${DYLIB}")
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DYLIB}"
                        "$<TARGET_FILE_DIR:${TARGET_NAME}>"
                )
            endif()
        endforeach()
    else()
        # Linux - copy to lib directory next to executable
        foreach(SO ${OIDN_DLLS})
            if(EXISTS "${SO}")
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${SO}"
                        "$<TARGET_FILE_DIR:${TARGET_NAME}>"
                )
            endif()
        endforeach()
    endif()
endfunction()

message(STATUS "OIDN: Include dir will be ${OIDN_INCLUDE_DIR}")
message(STATUS "OIDN: Lib dir will be ${OIDN_LIB_DIR}")
```

### 3.1.2 Modify `util/CMakeLists.txt`

**File:** `util/CMakeLists.txt`

Add at the end of the file:

```cmake
# Intel Open Image Denoise (OIDN) support
option(ENABLE_OIDN "Build with Intel Open Image Denoise support." OFF)
if(ENABLE_OIDN)
    # Include our OIDN download/setup module
    include(${CMAKE_SOURCE_DIR}/cmake/oidn.cmake)
    
    # Link OIDN to util library
    target_compile_definitions(util PUBLIC ENABLE_OIDN)
    target_link_libraries(util PUBLIC OpenImageDenoise)
    
    message(STATUS "OIDN: Enabled - will download OIDN ${OIDN_VERSION}")
endif()
```

### 3.1.3 Modify Backend CMakeLists.txt to Copy DLLs

For each backend that uses OIDN, add DLL copy commands.

**File:** `backends/dxr/CMakeLists.txt`

Add at the end:

```cmake
# Copy OIDN DLLs to output directory
if(ENABLE_OIDN)
    copy_oidn_dlls(crt_dxr)
endif()
```

**File:** `backends/vulkan/CMakeLists.txt`

Add at the end:

```cmake
# Copy OIDN DLLs to output directory  
if(ENABLE_OIDN)
    copy_oidn_dlls(crt_vulkan)
endif()
```

### 3.1.4 Alternative: Manual OIDN Installation

If you prefer to install OIDN manually instead of auto-download:

1. Download from: https://github.com/RenderKit/oidn/releases/tag/v2.3.3
2. Extract to a known location (e.g., `C:\oidn` or `/opt/oidn`)
3. Set CMake variable: `-DOpenImageDenoise_DIR=C:\oidn\lib\cmake\OpenImageDenoise`

In this case, modify `util/CMakeLists.txt` to use `find_package`:

```cmake
option(ENABLE_OIDN "Build with Intel Open Image Denoise support." OFF)
if(ENABLE_OIDN)
    find_package(OpenImageDenoise REQUIRED)
    target_compile_definitions(util PUBLIC ENABLE_OIDN)
    target_link_libraries(util PUBLIC OpenImageDenoise)
endif()
```

### 3.1.5 Verification

After CMake configuration, you should see:

```
-- OIDN: Will download from https://github.com/RenderKit/oidn/releases/download/v2.3.3/oidn-2.3.3.x64.windows.zip
-- OIDN: Include dir will be .../build/oidn/src/oidn-2.3.3.x64.windows/include
-- OIDN: Lib dir will be .../build/oidn/src/oidn-2.3.3.x64.windows/lib
-- OIDN: Enabled - will download OIDN 2.3.3
```

After build, the output directory should contain:
- `OpenImageDenoise.dll`
- `OpenImageDenoise_core.dll`
- `OpenImageDenoise_device_cpu.dll`
- `OpenImageDenoise_device_cuda.dll` (if CUDA available)
- `tbb12.dll`

---

## Reference: AfraChameleonRT DXR Implementation

Before diving into the implementation, here's the reference code from AfraChameleonRT:

### OIDN Device Initialization (`render_dxr.cpp` lines 88-104)
```cpp
#ifdef ENABLE_OIDN
    // Get the LUID of the adapter
    LUID luid = device->GetAdapterLuid();

    // Initialize the denoiser device
    oidn_device = oidn::newDevice(oidn::LUID{luid.LowPart, luid.HighPart});
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to create OIDN device.");
    oidn_device.commit();
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to commit OIDN device.");

    // Find a compatible external memory handle type
    const auto oidn_external_mem_types = oidn_device.get<oidn::ExternalMemoryTypeFlags>("externalMemoryTypes");
    if (!(oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::OpaqueWin32))
        throw std::runtime_error("failed to find compatible external memory type");
#endif
```

### Buffer Creation with Shared Heap (`render_dxr.cpp` lines 108-122)
```cpp
#ifdef ENABLE_OIDN
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       3 * sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_HEAP_FLAG_SHARED);

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
```

### OIDN Filter Setup (`render_dxr.cpp` lines 165-207)
```cpp
#ifdef ENABLE_OIDN
    {
        // Initialize the denoiser filter
        oidn_filter = oidn_device.newFilter("RT");
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to create OIDN filter.");

        HANDLE accum_buffer_handle = nullptr;
        CHECK_ERR(device->CreateSharedHandle(
                    accum_buffer.get(),
                    nullptr,
                    GENERIC_ALL,
                    nullptr,
                    &accum_buffer_handle));
        auto input_buffer = oidn_device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32,
                                                  accum_buffer_handle, nullptr, accum_buffer.size());

        HANDLE denoise_buffer_handle = nullptr;
        CHECK_ERR(device->CreateSharedHandle(
                    denoise_buffer.get(),
                    nullptr,
                    GENERIC_ALL,
                    nullptr,
                    &denoise_buffer_handle));
        auto output_buffer = oidn_device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32,
                                                   denoise_buffer_handle, nullptr, denoise_buffer.size());

        // AccumPixel struct layout: {color, albedo, normal} = 3 x float4 per pixel
        // Stride between pixels = 3 * sizeof(glm::vec4) = 48 bytes
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
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to commit OIDN filter.");
    }
#endif
```

### Execute Denoising (`render_dxr.cpp` line 252)
```cpp
#ifdef ENABLE_OIDN
    // Denoise the frame
    oidn_filter.execute();
#endif
```

---

## Task 3.2: DXR Backend - OIDN Integration

### 3.2.1 Add Headers and Member Variables

**File:** `backends/dxr/render_dxr.h`

**Add include (near top, with other includes):**
```cpp
#ifdef ENABLE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif
```

**Add member variables (in private section):**
```cpp
#ifdef ENABLE_OIDN
    dxr::Buffer denoise_buffer;
    oidn::DeviceRef oidn_device;
    oidn::FilterRef oidn_filter;
#endif
```

**Reference (AfraChameleonRT render_dxr.h):**
```cpp
#ifdef ENABLE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif
...
#ifdef ENABLE_OIDN
    dxr::Buffer denoise_buffer;
    oidn::DeviceRef oidn_device;
    oidn::FilterRef oidn_filter;
#endif
```

### 3.2.2 Modify Buffer Creation for Shared Memory

**File:** `backends/dxr/render_dxr.cpp`

In `initialize()`, update the accum_buffer creation to add shared heap flag:

**CURRENT (after Phase 2):**
```cpp
    // Allocate 3x size for AccumPixel struct: color + albedo + normal
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       3 * sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
```

**CHANGE TO:**
```cpp
#ifdef ENABLE_OIDN
    // Allocate 3x size for AccumPixel struct (shared for OIDN access)
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       3 * sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_HEAP_FLAG_SHARED);

    denoise_buffer = dxr::Buffer::device(device.Get(),
                                         sizeof(glm::vec4) * fb_width * fb_height,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_HEAP_FLAG_SHARED);
#else
    // Allocate 3x size for AccumPixel struct: color + albedo + normal
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       3 * sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
#endif
```

### 3.2.3 Initialize OIDN Device and Filter

**File:** `backends/dxr/render_dxr.cpp`

Add at the beginning of `initialize()`:

```cpp
#ifdef ENABLE_OIDN
    // Get the LUID of the adapter
    LUID luid = device->GetAdapterLuid();

    // Initialize the denoiser device
    oidn_device = oidn::newDevice(oidn::LUID{luid.LowPart, luid.HighPart});
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to create OIDN device.");
    oidn_device.commit();
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to commit OIDN device.");

    // Verify compatible external memory type
    const auto oidn_external_mem_types = oidn_device.get<oidn::ExternalMemoryTypeFlags>("externalMemoryTypes");
    if (!(oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::OpaqueWin32))
        throw std::runtime_error("Failed to find compatible external memory type");
#endif
```

After buffer creation, add filter setup:

```cpp
#ifdef ENABLE_OIDN
    {
        // Initialize the denoiser filter
        oidn_filter = oidn_device.newFilter("RT");
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to create OIDN filter.");

        // Create shared handle for accum_buffer
        HANDLE accum_buffer_handle = nullptr;
        CHECK_ERR(device->CreateSharedHandle(
                    accum_buffer.get(),
                    nullptr,
                    GENERIC_ALL,
                    nullptr,
                    &accum_buffer_handle));
        auto input_buffer = oidn_device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32,
                                                  accum_buffer_handle, nullptr, accum_buffer.size());

        // Create shared handle for denoise_buffer
        HANDLE denoise_buffer_handle = nullptr;
        CHECK_ERR(device->CreateSharedHandle(
                    denoise_buffer.get(),
                    nullptr,
                    GENERIC_ALL,
                    nullptr,
                    &denoise_buffer_handle));
        auto output_buffer = oidn_device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32,
                                                   denoise_buffer_handle, nullptr, denoise_buffer.size());

        // Configure filter inputs from AccumPixel struct layout:
        // struct AccumPixel { float4 color; float4 albedo; float4 normal; }
        // Stride between pixels = 3 * sizeof(glm::vec4) = 48 bytes
        // Color at offset 0, Albedo at offset 16, Normal at offset 32
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
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to commit OIDN filter.");
    }
#endif
```

### 3.2.4 Execute Denoising After Render

**File:** `backends/dxr/render_dxr.cpp`

In `render()`, after the ray tracing execution and before tonemapping:

```cpp
#ifdef ENABLE_OIDN
    // Denoise the frame
    oidn_filter.execute();
#endif
```

### 3.2.5 Add denoise_buffer to Descriptor Heap

In `build_descriptor_heap()`, add UAV for denoise_buffer:

```cpp
#ifdef ENABLE_OIDN
    // Denoise buffer UAV
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.StructureByteStride = sizeof(glm::vec4);
        uav_desc.Buffer.NumElements = denoise_buffer.size() / sizeof(glm::vec4);
        uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(denoise_buffer.get(), nullptr, &uav_desc, heap_handle);
        heap_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
#endif
```

---

## Task 3.3: Vulkan Backend - OIDN Integration

### 3.3.1 Add Headers and Member Variables

**File:** `backends/vulkan/render_vulkan.h`

**Add include:**
```cpp
#ifdef ENABLE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif
```

**Add member variables:**
```cpp
#ifdef ENABLE_OIDN
    std::shared_ptr<vkrt::Buffer> denoise_buffer;
    oidn::DeviceRef oidn_device;
    oidn::FilterRef oidn_filter;
#endif
```

### 3.3.2 Initialize OIDN and Create Shared Buffers

**File:** `backends/vulkan/render_vulkan.cpp`

At the beginning of `initialize()`:

```cpp
#ifdef ENABLE_OIDN
    // Query the UUID of the Vulkan physical device
    VkPhysicalDeviceIDProperties id_properties{};
    id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &id_properties;
    vkGetPhysicalDeviceProperties2(device->physical_device(), &properties);

    oidn::UUID uuid;
    std::memcpy(uuid.bytes, id_properties.deviceUUID, sizeof(uuid.bytes));

    // Initialize the denoiser device
    oidn_device = oidn::newDevice(uuid);
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to create OIDN device.");
    oidn_device.commit();
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to commit OIDN device.");

    // Find a compatible external memory handle type
    const auto oidn_external_mem_types = oidn_device.get<oidn::ExternalMemoryTypeFlags>("externalMemoryTypes");
    oidn::ExternalMemoryTypeFlag oidn_external_mem_type;
    VkExternalMemoryHandleTypeFlagBits external_mem_type;

    if (oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::OpaqueWin32) {
        oidn_external_mem_type = oidn::ExternalMemoryTypeFlag::OpaqueWin32;
        external_mem_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    } else if (oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::OpaqueFD) {
        oidn_external_mem_type = oidn::ExternalMemoryTypeFlag::OpaqueFD;
        external_mem_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    } else if (oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::DMABuf) {
        oidn_external_mem_type = oidn::ExternalMemoryTypeFlag::DMABuf;
        external_mem_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    } else {
        throw std::runtime_error("Failed to find compatible external memory type");
    }
#endif
```

Modify buffer creation:

```cpp
#ifdef ENABLE_OIDN
    // Allocate 3x size for AccumPixel struct with external memory for OIDN sharing
    accum_buffer = vkrt::Buffer::device(*device,
                                        3 * sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        0,
                                        external_mem_type);

    denoise_buffer = vkrt::Buffer::device(*device,
                                          sizeof(glm::vec4) * fb_width * fb_height,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                          0,
                                          external_mem_type);
#else
    // Allocate 3x size for AccumPixel struct: color + albedo + normal
    accum_buffer = vkrt::Buffer::device(*device,
                                        3 * sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
#endif
```

Add filter setup:

```cpp
#ifdef ENABLE_OIDN
    {
        // Initialize the denoiser filter
        oidn_filter = oidn_device.newFilter("RT");
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to create OIDN filter.");

        auto input_buffer = oidn_device.newBuffer(oidn_external_mem_type,
                                                  accum_buffer->external_mem_handle(external_mem_type),
#ifdef _WIN32
                                                  nullptr,
#endif
                                                  accum_buffer->size());

        auto output_buffer = oidn_device.newBuffer(oidn_external_mem_type,
                                                   denoise_buffer->external_mem_handle(external_mem_type),
#ifdef _WIN32
                                                   nullptr,
#endif
                                                   denoise_buffer->size());

        // Configure filter inputs from AccumPixel struct layout:
        // struct AccumPixel { float4 color; float4 albedo; float4 normal; }
        // Stride between pixels = 3 * sizeof(glm::vec4) = 48 bytes
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
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to commit OIDN filter.");
    }
#endif
```

**Reference (AfraChameleonRT render_vulkan.cpp lines 146-219):**
```cpp
#ifdef ENABLE_OIDN
    accum_buffer = vkrt::Buffer::device(*device,
                                        3 * sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        0,
                                        external_mem_type);

    denoise_buffer = vkrt::Buffer::device(*device,
                                          sizeof(glm::vec4) * fb_width * fb_height,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                          0,
                                          external_mem_type);
// ... filter setup with struct-based layout ...
    oidn_filter.setImage("color",  input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                         0 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
    oidn_filter.setImage("albedo", input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                         1 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
    oidn_filter.setImage("normal", input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                         2 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
#endif
```

### 3.3.3 Execute Denoising After Render

In `render()`:

```cpp
#ifdef ENABLE_OIDN
    // Denoise the frame
    oidn_filter.execute();
#endif
```

### 3.3.4 Add denoise_buffer Descriptor Binding

In the descriptor layout builder, add binding for denoise_buffer:

```cpp
#ifdef ENABLE_OIDN
            .add_binding(
                DENOISE_BUFFER_BINDING, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
#endif
```

And in descriptor set updates:

```cpp
#ifdef ENABLE_OIDN
    updater.write_ssbo(desc_set, DENOISE_BUFFER_BINDING, denoise_buffer);
#endif
```

---

## Task 3.4: Slang Shader - Read from denoise_buffer

**File:** `shaders/unified_render.slang`

### 3.4.1 Add denoise_buffer Binding

**Vulkan binding (after accum_buffer):**
```slang
#ifdef ENABLE_OIDN
[[vk::binding(DENOISE_BUFFER_BINDING, 0)]] RWStructuredBuffer<float4> denoise_buffer;
#endif
```

**DXR binding:**
```slang
#ifdef ENABLE_OIDN
RWStructuredBuffer<float4> denoise_buffer : register(u2);
#endif
```

### 3.4.2 Read Denoised Color for Display

Modify the tonemapping section to read from denoise_buffer when OIDN is enabled:

**CURRENT:**
```slang
    // ===== sRGB CONVERSION (Task 3.4.6) =====
    // Convert linear accumulated color to sRGB for display
    float3 srgb_color = float3(
        linear_to_srgb(accum_color.r),
        linear_to_srgb(accum_color.g),
        linear_to_srgb(accum_color.b)
    );
```

**CHANGE TO:**
```slang
    // ===== sRGB CONVERSION (Task 3.4.6) =====
    // Convert linear color to sRGB for display
#ifdef ENABLE_OIDN
    float4 display_color = denoise_buffer[pixel_idx];
#else
    float4 display_color = accum_color;
#endif
    float3 srgb_color = float3(
        linear_to_srgb(display_color.r),
        linear_to_srgb(display_color.g),
        linear_to_srgb(display_color.b)
    );
```

---

## Task 3.5: Build & Verify

### Build Commands

```powershell
cd C:\Users\kchunang\dev\ChameleonRT\build

# Configure with OIDN enabled
cmake .. -DENABLE_OIDN=ON

# Build
cmake --build . --config Release
```

### Prerequisites

- Install OIDN: Download from https://www.openimagedenoise.org/downloads.html
- Set `OpenImageDenoise_DIR` or add to PATH

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

- [ ] Application starts without crashes
- [ ] No OIDN errors in console
- [ ] **Visible denoising effect** - image should be smoother
- [ ] Progressive refinement still works
- [ ] Performance is acceptable (slight overhead from denoising)

### Expected Visual Difference

At low sample counts (1-16 SPP):
- **Without OIDN:** Grainy, noisy image
- **With OIDN:** Smooth, clean image with preserved detail

---

## Notes on Buffer Layout

### Struct-Based Layout (Phase 2)

We use a struct-based layout matching AfraChameleonRT:

```slang
struct AccumPixel {
    float4 color;
    float4 albedo;
    float4 normal;
};

RWStructuredBuffer<AccumPixel> accum_buffer;
```

Memory layout:
```
accum_buffer: [color0|albedo0|normal0 | color1|albedo1|normal1 | ...]
              └─ AccumPixel struct (3 × vec4 = 48 bytes)
```

### OIDN Configuration with Struct Layout

OIDN reads from this struct-based buffer using byte offsets and stride:

```cpp
// Stride = sizeof(AccumPixel) = 3 * sizeof(glm::vec4) = 48 bytes
// color  offset = 0 * sizeof(glm::vec4) = 0 bytes
// albedo offset = 1 * sizeof(glm::vec4) = 16 bytes
// normal offset = 2 * sizeof(glm::vec4) = 32 bytes

oidn_filter.setImage("color",  input_buffer, oidn::Format::Float3, fb_width, fb_height,
                     0 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
oidn_filter.setImage("albedo", input_buffer, oidn::Format::Float3, fb_width, fb_height,
                     1 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
oidn_filter.setImage("normal", input_buffer, oidn::Format::Float3, fb_width, fb_height,
                     2 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
```

This approach:
- Matches AfraChameleonRT exactly
- Single buffer allocation
- Interleaved data improves cache locality during ray tracing
- OIDN handles the strided read efficiently

---

## Summary of Changes

| File | Change |
|------|--------|
| `util/CMakeLists.txt` | Add ENABLE_OIDN option, link OpenImageDenoise |
| `backends/dxr/render_dxr.h` | Add OIDN includes, denoise_buffer, oidn_device, oidn_filter |
| `backends/dxr/render_dxr.cpp` | Init OIDN, shared buffers, filter setup, execute denoising |
| `backends/vulkan/render_vulkan.h` | Add OIDN includes, denoise_buffer, oidn_device, oidn_filter |
| `backends/vulkan/render_vulkan.cpp` | Init OIDN, external memory buffers, filter setup, execute denoising |
| `shaders/unified_render.slang` | Add denoise_buffer binding, read denoised color for display |

---

## Future Enhancements (Phase 4)

1. **Separate Tonemap Pass**: Move tonemapping from shader to a separate compute pass (required for proper OIDN workflow where denoised output goes to tonemap)
2. **Quality Presets**: Add UI control for OIDN quality levels (Fast, Balanced, High)
3. **Temporal Stability**: Consider OIDN's temporal filtering for animations
4. **Metal Backend**: Add OIDN support for Metal backend (macOS)
2. **Quality Toggle**: UI option to switch between Balanced/High quality
3. **Denoise Toggle**: UI option to enable/disable denoising at runtime
4. **Temporal Stability**: Use OIDN's temporal mode for animation
