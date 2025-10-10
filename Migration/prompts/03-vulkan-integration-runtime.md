# Vulkan Backend Integration Prompts - Phase 2: Runtime Compilation

## Overview

**Goal:** Integrate Slang shader compilation into Vulkan backend for GLSL → SPIRV runtime compilation

**Pattern:** Mirror DXR integration (`02-dxr-integration-runtime.md`) but for Vulkan

**Key Differences from DXR:**
- **Source Language:** GLSL instead of HLSL
- **Target Format:** SPIRV instead of DXIL
- **Shader Profile:** Vulkan 1.2/1.3 instead of `lib_6_6`
- **No getTargetCode() bug:** SPIRV multi-entry compilation works (unlike DXIL)
- **Otherwise Identical:** Same SlangShaderCompiler API, same search paths pattern

**Reference:** DXR integration (COMPLETE) in `02-dxr-integration-runtime.md`

---

## Prerequisites

✅ **Must Be Complete Before Starting:**
1. `util/slang_shader_compiler.h` exists with all API methods
2. `util/slang_shader_compiler.cpp` implements per-entry-point compilation
3. `slang_compiler_util` library builds successfully
4. DXR integration COMPLETE (provides working reference)

---

## Prompt 1: Integrate SlangShaderCompiler into Vulkan Backend

### Status

📍 **START HERE**

### Context

The `SlangShaderCompiler` utility is already implemented and tested with DXR. We need to integrate it into the Vulkan backend following the same pattern.

**Reference Implementation:** `backends/dxr/render_dxr.cpp` (completed in DXR Prompt 1)

### Requirements

**1. Update CMakeLists.txt**

File: `backends/vulkan/CMakeLists.txt`

Add Slang support section (similar to DXR):

```cmake
# Add Slang support if enabled
if(ENABLE_VULKAN_SLANG)
    target_link_libraries(crt_vulkan PRIVATE slang_compiler_util)
    target_compile_definitions(crt_vulkan PRIVATE USE_SLANG_COMPILER=1)
    
    # Note: Vulkan uses SPIRV, doesn't need DXC DLLs
    # Keep existing glslang/shaderc finding for fallback
endif()
```

**2. Add SlangShaderCompiler Member**

File: `backends/vulkan/render_vulkan.h` (or equivalent)

In the Vulkan render class (e.g., `RenderVulkan`):

```cpp
#ifdef USE_SLANG_COMPILER
#include "slang_shader_compiler.h"
#endif

class RenderVulkan {
private:
#ifdef USE_SLANG_COMPILER
    chameleonrt::SlangShaderCompiler slangCompiler;
#endif
    // ... existing members
};
```

**3. Initialize Slang Compiler**

File: `backends/vulkan/render_vulkan.cpp` (or equivalent)

In device initialization method (after Vulkan device is created):

```cpp
void RenderVulkan::create_device_objects() {  // Or equivalent method
    // ... existing Vulkan initialization

#ifdef USE_SLANG_COMPILER
    // Initialize Slang shader compiler
    if (!slangCompiler.isValid()) {
        throw std::runtime_error("Failed to initialize Slang shader compiler");
    }
#endif
}
```

### Testing

**Build:**
```powershell
.\build_with_slang.ps1 -EnableVulkanSlang
```

**Verify:**
1. `crt_vulkan.dll` (or `.so`) builds successfully
2. Application loads Vulkan backend
3. No initialization errors

**Expected:** Application builds and loads, ready for shader compilation test.

---

## Prompt 2: Test with Hardcoded GLSL Shader

### Status

⏸️ **After Prompt 1**

### Context

Prove Slang can compile GLSL → SPIRV at runtime. Similar to DXR Prompt 2 but with GLSL syntax.

**Reference:** `backends/dxr/render_dxr.cpp` - DXR Prompt 2 (hardcoded shader test)

### Requirements

**1. Create Minimal GLSL Raygen Shader**

In Vulkan pipeline creation function (e.g., `build_raytracing_pipeline()`):

```cpp
#ifdef USE_SLANG_COMPILER
    // Hardcoded minimal GLSL raygen shader for testing
    const std::string glsl_source = R"(
#version 460
#extension GL_EXT_ray_tracing : require

layout(binding = 0, set = 0) uniform accelerationStructureEXT scene;
layout(binding = 1, set = 0, rgba8) uniform image2D output_image;

layout(location = 0) rayPayloadEXT vec3 payload;

void main() {
    const vec2 pixel_center = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
    const vec2 in_uv = pixel_center / vec2(gl_LaunchSizeEXT.xy);
    const vec2 d = in_uv * 2.0 - 1.0;
    
    const vec3 origin = vec3(0, 0, -3);
    const vec3 direction = normalize(vec3(d.x, d.y, 1.0));
    
    traceRayEXT(scene, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0,
                origin, 0.001, direction, 1000.0, 0);
    
    imageStore(output_image, ivec2(gl_LaunchIDEXT.xy), vec4(payload, 1.0));
}
)";

    // Compile GLSL to SPIRV using Slang
    auto result = slangCompiler.compileGLSLToSPIRV(glsl_source, "main", ShaderStage::RayGen);
    
    if (!result) {
        std::string error = slangCompiler.getLastError();
        std::cerr << "[Slang] Compilation failed: " << error << std::endl;
        throw std::runtime_error("Slang shader compilation failed");
    }
    
    std::cout << "[Slang] SUCCESS! SPIRV size: " << result->bytecode.size() << " bytes" << std::endl;
    
    // Create Vulkan shader module from SPIRV
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = result->bytecode.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(result->bytecode.data());
    
    VkShaderModule shader_module;
    vkCreateShaderModule(device, &create_info, nullptr, &shader_module);
    
    // Use shader_module in pipeline creation...
#else
    // Original glslang/shaderc path
#endif
```

### Testing

**Build & Run:**
```powershell
cd build\Debug
.\chameleonrt.exe vulkan ..\..\test_cube.obj
```

**Expected Output:**
```
[Slang] SUCCESS! SPIRV size: 1234 bytes
Loading OBJ: ../../test_cube.obj
Scene loaded
```

**Success Criteria:**
- ✅ Slang compiles GLSL → SPIRV
- ✅ Vulkan accepts SPIRV bytecode
- ✅ Pipeline creation succeeds
- ✅ Application runs (may show black/garbled - minimal shader is OK)

---

## Prompt 3: Load Production Shader from File

### Status

⏸️ **After Prompt 2**

### Context

Load actual production GLSL shader from file, similar to DXR Prompt 3.

**Reference:** `backends/dxr/render_dxr.cpp` - DXR Prompt 3 (file loading, per-entry-point)

**Key Learning from DXR:** Use per-entry-point compilation with `getEntryPointCode()`

### Requirements

**1. DLL-Relative Path Helper (if not exists)**

Similar to DXR, add helper to get DLL/SO directory:

```cpp
#ifdef USE_SLANG_COMPILER
namespace {
    std::filesystem::path get_dll_directory() {
        #ifdef _WIN32
            char dll_path[MAX_PATH];
            HMODULE hModule = NULL;
            GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | 
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)&get_dll_directory,
                &hModule
            );
            if (hModule && GetModuleFileNameA(hModule, dll_path, MAX_PATH)) {
                return std::filesystem::path(dll_path).parent_path();
            }
        #else
            // Linux: use dladdr
            Dl_info dl_info;
            dladdr((void*)get_dll_directory, &dl_info);
            return std::filesystem::path(dl_info.dli_fname).parent_path();
        #endif
        throw std::runtime_error("Failed to get DLL directory");
    }
    
    std::string load_shader_source(const std::filesystem::path& shader_path) {
        std::ifstream file(shader_path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + shader_path.string());
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
}
#endif
```

**2. Load Production Shader**

```cpp
#ifdef USE_SLANG_COMPILER
    std::filesystem::path dll_dir = get_dll_directory();
    std::filesystem::path shader_path = dll_dir / "render_vulkan.rgen";  // Or appropriate name
    
    std::string glsl_source;
    try {
        glsl_source = load_shader_source(shader_path);
    } catch (const std::exception& e) {
        std::cerr << "[Slang] ERROR loading shader: " << e.what() << std::endl;
        throw;
    }
    
    // Compile using per-entry-point pattern
    // (SlangShaderCompiler may need compileGLSLToSPIRVLibrary method)
    auto result = slangCompiler.compileGLSLToSPIRV(glsl_source, "main", ShaderStage::RayGen);
    
    if (!result) {
        std::string error = slangCompiler.getLastError();
        std::cerr << "[Slang] Compilation failed: " << error << std::endl;
        throw std::runtime_error("Slang shader compilation failed");
    }
    
    // Create Vulkan shader modules...
#endif
```

**3. Update CMakeLists.txt to Deploy Shaders**

File: `backends/vulkan/CMakeLists.txt`

```cmake
if(ENABLE_VULKAN_SLANG)
    # Copy shader source files to output directory for runtime compilation
    set(SHADER_FILES
        render_vulkan.rgen
        render_vulkan.rmiss
        render_vulkan.rchit
        # Add other shader files
    )
    
    foreach(SHADER_FILE ${SHADER_FILES})
        add_custom_command(TARGET crt_vulkan POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}"
                "$<TARGET_FILE_DIR:crt_vulkan>/${SHADER_FILE}"
            COMMENT "Deploying ${SHADER_FILE} for runtime Slang compilation"
        )
    endforeach()
endif()
```

### Testing

**Build:**
```powershell
.\build_with_slang.ps1 -EnableVulkanSlang
```

**Verify Files:**
```powershell
Test-Path build\Debug\render_vulkan.rgen  # Should exist
Test-Path build\Debug\crt_vulkan.dll  # Windows
Test-Path build\Debug\crt_vulkan.so   # Linux
```

**Run:**
```powershell
cd build\Debug
.\chameleonrt.exe vulkan ..\..\test_cube.obj
```

**Expected:** Shader loads and compiles, application renders correctly.

---

## Prompt 4: Use Slang's Native Include Resolution

### Status

⏸️ **After Prompt 3**

### Context

Production Vulkan shaders have `#include` directives. Use Slang's native include resolution.

**Key Discovery from DXR:** Slang handles includes automatically via `SessionDesc.searchPaths`  
**Reference:** `backends/dxr/render_dxr.cpp` - DXR Prompt 4 (native includes)

### Requirements

**1. Update SlangShaderCompiler for SPIRV (if needed)**

May need to add search paths parameter to `compileGLSLToSPIRV()` if not already present:

```cpp
// In util/slang_shader_compiler.h
std::optional<ShaderBlob> compileGLSLToSPIRV(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    const std::vector<std::string>& searchPaths = {},
    const std::vector<std::string>& defines = {}
);
```

**2. Pass Search Paths**

```cpp
#ifdef USE_SLANG_COMPILER
    std::filesystem::path dll_dir = get_dll_directory();
    std::filesystem::path shader_path = dll_dir / "render_vulkan.rgen";
    
    std::string glsl_source = load_shader_source(shader_path);
    
    // Setup search paths for #include resolution
    std::vector<std::string> searchPaths = {
        dll_dir.string(),  // Shader directory
        (dll_dir.parent_path() / "util").string()  // Utility headers
    };
    
    auto result = slangCompiler.compileGLSLToSPIRV(
        glsl_source, "main", ShaderStage::RayGen, searchPaths);
    
    if (!result) {
        std::string error = slangCompiler.getLastError();
        std::cerr << "[Slang] Compilation failed: " << error << std::endl;
        throw std::runtime_error("Slang shader compilation failed");
    }
#endif
```

**3. Deploy Include Files**

Update `backends/vulkan/CMakeLists.txt`:

```cmake
if(ENABLE_VULKAN_SLANG)
    set(SHADER_FILES
        render_vulkan.rgen
        render_vulkan.rmiss
        render_vulkan.rchit
        util.glsl           # Common utilities
        disney_bsdf.glsl    # Material shading
        lcg_rng.glsl        # Random number generation
        lights.glsl         # Light sampling
    )
    
    foreach(SHADER_FILE ${SHADER_FILES})
        add_custom_command(TARGET crt_vulkan POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}"
                "$<TARGET_FILE_DIR:crt_vulkan>/${SHADER_FILE}"
            COMMENT "Deploying ${SHADER_FILE} for runtime Slang compilation"
        )
    endforeach()
    
    # Deploy utility headers if cross-directory includes exist
    add_custom_command(TARGET crt_vulkan POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:crt_vulkan>/util"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/util/texture_channel_mask.h"
            "$<TARGET_FILE_DIR:crt_vulkan>/util/texture_channel_mask.h"
        COMMENT "Deploying util headers for runtime Slang compilation"
    )
endif()
```

### Testing

**Clean Rebuild:**
```powershell
.\build_with_slang.ps1 -EnableVulkanSlang
```

**Test Simple Scene:**
```powershell
cd build\Debug
.\chameleonrt.exe vulkan ..\..\test_cube.obj
```

**Test Complex Scene:**
```powershell
cd build\Debug
.\chameleonrt.exe vulkan C:\Demo\Assets\sponza\sponza.obj
```

**Expected Results:**
- ✅ All includes resolved automatically
- ✅ No compilation errors
- ✅ Renders correctly with full Disney BRDF
- ✅ Complex scenes load (Sponza: 262K triangles, 25 materials, 24 textures)

**Success Criteria:**
- Test cube renders (simple validation)
- Sponza loads and renders (production validation)
- No manual include preprocessing needed
- Clean console output (no errors)

---

## Summary & Next Steps

### Vulkan Integration Checklist

After completing all 4 prompts:

- ✅ Prompt 1: SlangShaderCompiler integrated into Vulkan backend
- ✅ Prompt 2: Hardcoded GLSL test shader compiles → SPIRV
- ✅ Prompt 3: Production shader loads from file
- ✅ Prompt 4: Native include resolution for complex shaders

### Differences from DXR

**Easier:**
- No DXC dependency (SPIRV compiler built into Slang)
- No `getTargetCode()` bug workaround needed for SPIRV
- Cross-platform (Linux/Windows)

**Same:**
- Per-entry-point compilation pattern
- Native include resolution via `searchPaths`
- DLL-relative file loading
- Automated shader deployment via CMake

### Cleanup

After successful integration:
- Remove test shaders (if any)
- Remove debug logging
- Update documentation
- Tag integration milestone

---

## Reference Files

**DXR Implementation (COMPLETE):**
- `backends/dxr/render_dxr.cpp` - Complete working example
- `backends/dxr/CMakeLists.txt` - Build configuration
- `Migration/prompts/02-dxr-integration-runtime.md` - Full DXR prompts

**Vulkan Implementation (TODO):**
- `backends/vulkan/render_vulkan.cpp` - To be modified
- `backends/vulkan/CMakeLists.txt` - To be updated
- This file - Vulkan integration prompts

**Common Code:**
- `util/slang_shader_compiler.h` - API (already complete)
- `util/slang_shader_compiler.cpp` - Implementation (already complete)
- Both have SPIRV support already implemented

---

**Ready to start Vulkan integration following DXR's proven pattern!** 🚀
