# Slang Integration Guide

## Overview

Step-by-step instructions for integrating Slang shader compilation into ChameleonRT backends.

**Target Audience:** Yourself (in the new branch, following these instructions)  
**Prerequisites:** Migration/ directory copied, basic understanding of backend structure

---

## Quick Reference

**Files to modify:**
- `CMakeLists.txt` (root)
- `backends/CMakeLists.txt`
- `backends/dxr/CMakeLists.txt`
- `backends/vulkan/CMakeLists.txt`
- Backend shader loading code

**Files to add:**
- `util/slang_shader_compiler.h`
- `util/slang_shader_compiler.cpp`
- `cmake/FindSlang.cmake` (already exists, may need updates)

---

## Part 1: CMake Setup

### Step 1.1: Root CMakeLists.txt

Add Slang option and utility library:

```cmake
# Near top, with other options
option(ENABLE_SLANG "Use Slang for shader compilation" OFF)

# After finding other packages
if(ENABLE_SLANG)
    # Slang_ROOT can be set via env variable or -DSlang_ROOT=...
    if(DEFINED ENV{SLANG_PATH})
        set(Slang_ROOT $ENV{SLANG_PATH})
    endif()
    
    find_package(Slang REQUIRED)
    
    # Create utility library
    add_library(slang_compiler_util STATIC
        util/slang_shader_compiler.h
        util/slang_shader_compiler.cpp
    )
    
    target_link_libraries(slang_compiler_util
        PUBLIC Slang::Slang
    )
    
    target_include_directories(slang_compiler_util
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/util
    )
    
    target_compile_definitions(slang_compiler_util
        PUBLIC CHAMELEONRT_USE_SLANG=1
    )
endif()
```

**Why:** 
- Creates optional Slang support
- Builds utility library when enabled
- Makes `CHAMELEONRT_USE_SLANG` available to all code

### Step 1.2: backends/CMakeLists.txt

Update backend selection logic:

```cmake
# Current structure (probably):
# add_subdirectory(dxr)
# add_subdirectory(vulkan)
# ...

# New structure with Slang option:
cmake_dependent_option(ENABLE_DXR_SLANG 
    "Build DXR backend with Slang compilation" 
    OFF 
    "ENABLE_SLANG;ENABLE_DXR" 
    OFF
)

cmake_dependent_option(ENABLE_VULKAN_SLANG 
    "Build Vulkan backend with Slang compilation" 
    OFF 
    "ENABLE_SLANG;ENABLE_VULKAN" 
    OFF
)

# Build backends
if(ENABLE_DXR OR ENABLE_DXR_SLANG)
    add_subdirectory(dxr)
endif()

if(ENABLE_VULKAN OR ENABLE_VULKAN_SLANG)
    add_subdirectory(vulkan)
endif()

# ... other backends
```

**Why:**
- Allows gradual migration per backend
- Can have both Slang and non-Slang builds simultaneously
- Clear CMake variable names

### Step 1.3: Test CMake Configuration

```powershell
cd c:\dev\ChameleonRT
cmake -B build_slang_test -DENABLE_SLANG=ON -DENABLE_DXR=ON -DENABLE_DXR_SLANG=ON

# Should see:
# -- Found Slang: ...
# -- Configuring done
# No errors about missing Slang
```

**If it fails:**
- Check `$env:SLANG_PATH` is set
- Or pass `-DSlang_ROOT=C:\dev\slang\build\Debug`
- Verify `FindSlang.cmake` is in `cmake/` directory

---

## Part 2: Backend Integration

### Step 2.1: DXR Backend CMakeLists.txt

```cmake
# In backends/dxr/CMakeLists.txt

# ... existing find_package calls for D3D12, etc.

# Add Slang support if enabled
if(ENABLE_DXR_SLANG)
    target_link_libraries(crt_dxr PRIVATE slang_compiler_util)
    target_compile_definitions(crt_dxr PRIVATE USE_SLANG_COMPILER=1)
    
    # May still need DXC for comparison/fallback
    # Keep existing DXC finding logic
endif()
```

**Why:**
- Links Slang utility into DXR backend
- Defines `USE_SLANG_COMPILER` for conditional compilation
- Keeps DXC available for testing

### Step 2.2: Vulkan Backend CMakeLists.txt

```cmake
# In backends/vulkan/CMakeLists.txt

# ... existing Vulkan, glslang setup

if(ENABLE_VULKAN_SLANG)
    target_link_libraries(crt_vulkan PRIVATE slang_compiler_util)
    target_compile_definitions(crt_vulkan PRIVATE USE_SLANG_COMPILER=1)
    
    # May still need glslang for fallback
    # Keep existing glslang finding
endif()
```

### Step 2.3: Test Build

```powershell
cmake --build build_slang_test --target crt_dxr

# Should compile without errors
# Linking should succeed
```

**If linking fails:**
- Check Slang DLLs are findable
- May need to copy `slang.dll` to output directory
- See reference/integration_notes.txt for DLL deployment

---

## Part 3: Code Integration

### Step 3.1: Include Header in Backend

In your DXR backend shader compilation code:

```cpp
// At top of file (e.g., render_dxr.cpp or wherever shaders are compiled)
#ifdef USE_SLANG_COMPILER
#include "slang_shader_compiler.h"
#endif
```

### Step 3.2: Create Compiler Instance

```cpp
// In your backend class (e.g., RenderDXR)
class RenderDXR {
private:
#ifdef USE_SLANG_COMPILER
    chameleonrt::SlangShaderCompiler slangCompiler;
#endif
    // ... other members
};
```

### Step 3.3: Replace Shader Compilation

**Find existing code like:**

```cpp
// Old DXC compilation
ComPtr<IDxcBlob> CompileShader(const std::string& source, const char* entryPoint) {
    // DXC compilation logic
    // ...
    return compiledBlob;
}
```

**Replace with:**

```cpp
std::vector<uint8_t> CompileShader(const std::string& source, const char* entryPoint, ShaderStage stage) {
#ifdef USE_SLANG_COMPILER
    auto result = slangCompiler.compileHLSLToDXIL(source, entryPoint, stage);
    if (!result) {
        throw std::runtime_error("Slang compilation failed: " + slangCompiler.getLastError());
    }
    return result->bytecode;
#else
    // Original DXC path
    auto blob = CompileWithDXC(source, entryPoint);
    std::vector<uint8_t> bytecode(blob->GetBufferSize());
    memcpy(bytecode.data(), blob->GetBufferPointer(), blob->GetBufferSize());
    return bytecode;
#endif
}
```

**Why:**
- Keeps both paths available during migration
- Easy to compare outputs
- Can disable Slang if issues arise

### Step 3.4: Update D3D12 Pipeline Creation

```cpp
// When creating pipeline state object
auto shaderBytecode = CompileShader(shaderSource, "main", ShaderStage::RayGen);

D3D12_DXIL_LIBRARY_DESC libDesc = {};
libDesc.DXILLibrary.pShaderBytecode = shaderBytecode.data();
libDesc.DXILLibrary.BytecodeLength = shaderBytecode.size();

// Rest of pipeline creation unchanged
```

---

## Part 4: Vulkan Integration

Similar pattern for Vulkan:

### Step 4.1: Replace GLSL Compilation

```cpp
std::vector<uint32_t> CompileShaderToSPIRV(const std::string& glslSource, const char* entryPoint, ShaderStage stage) {
#ifdef USE_SLANG_COMPILER
    auto result = slangCompiler.compileGLSLToSPIRV(glslSource, entryPoint, stage);
    if (!result) {
        throw std::runtime_error("Slang SPIRV compilation failed: " + slangCompiler.getLastError());
    }
    
    // Convert uint8_t vector to uint32_t vector
    std::vector<uint32_t> spirv(result->bytecode.size() / 4);
    memcpy(spirv.data(), result->bytecode.data(), result->bytecode.size());
    return spirv;
#else
    // Original glslang path
    return CompileWithGlslang(glslSource, entryPoint, stage);
#endif
}
```

### Step 4.2: Create Shader Module

```cpp
auto spirv = CompileShaderToSPIRV(shaderSource, "main", ShaderStage::RayGen);

VkShaderModuleCreateInfo createInfo = {};
createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
createInfo.codeSize = spirv.size() * sizeof(uint32_t);
createInfo.pCode = spirv.data();

VkShaderModule shaderModule;
vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);

// Rest of pipeline creation unchanged
```

---

## Part 5: Testing & Validation

### Step 5.1: Build with Slang Enabled

```powershell
cmake -B build -DENABLE_SLANG=ON -DENABLE_DXR=ON -DENABLE_DXR_SLANG=ON
cmake --build build --config Debug
```

### Step 5.2: Run Application

```powershell
cd build\Debug
.\chameleonrt.exe dxr ..\..\..\test_cube.obj
```

**Expected:**
- Application starts without errors
- Scene renders correctly
- No shader compilation errors in console

**If it crashes:**
- Check Slang DLLs are in same directory as .exe
- Enable verbose logging in SlangShaderCompiler
- Compare DXIL output with previous DXC output

### Step 5.3: Validation Tools

**D3D12 / PIX:**
```powershell
# Open PIX, attach to chameleonrt.exe
# Capture frame
# Check shader bytecode in pipeline state
# Verify no D3D12 validation errors
```

**Vulkan / RenderDoc:**
```powershell
# Launch through RenderDoc
# Capture frame
# Verify SPIRV in shader modules
# Check Vulkan validation layers
```

### Step 5.4: Performance Comparison

```cpp
// Add timing around compilation
auto start = std::chrono::high_resolution_clock::now();
auto shader = CompileShader(source, "main", ShaderStage::RayGen);
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

std::cout << "Shader compilation took: " << duration.count() << "ms\n";
```

**Acceptable:**
- First compile: May be slower (Slang initialization)
- Subsequent compiles: Should be comparable to DXC/glslang
- If >2x slower, investigate

---

## Part 6: Migrating to Slang Language

Once pass-through works, migrate shaders to `.slang` files:

### Step 6.1: Create Slang Shader File

```slang
// shaders/simple.slang

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal : NORMAL;
};

cbuffer SceneData : register(b0) {
    float4x4 viewProj;
    float4x4 model;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.position = mul(viewProj, worldPos);
    output.normal = mul((float3x3)model, input.normal);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return float4(normalize(input.normal) * 0.5 + 0.5, 1.0);
}
```

### Step 6.2: Load and Compile

```cpp
// Load from file
auto source = SlangShaderCompiler::loadShaderSource("shaders/simple.slang");
if (!source) {
    throw std::runtime_error("Failed to load shader file");
}

// Compile to target
auto dxilShader = slangCompiler.compileSlangToDXIL(*source, "vertexMain", ShaderStage::Vertex);
auto spirvShader = slangCompiler.compileSlangToSPIRV(*source, "vertexMain", ShaderStage::Vertex);

// Use in pipelines as before
```

### Step 6.3: Test Cross-Compilation

Run same `.slang` file on both D3D12 and Vulkan:

```powershell
# D3D12
.\chameleonrt.exe dxr test_cube.obj

# Vulkan
.\chameleonrt.exe vulkan test_cube.obj

# Should render identically
```

---

## Troubleshooting

### Issue: Slang not found

```
CMake Error: Could not find Slang
```

**Solution:**
```powershell
$env:SLANG_PATH = "C:\dev\slang\build\Debug"
# Or pass to CMake:
cmake -B build -DSlang_ROOT=C:\dev\slang\build\Debug
```

### Issue: Missing slang.dll at runtime

```
The code execution cannot proceed because slang.dll was not found
```

**Solution:**
Add DLL deployment to CMakeLists.txt:
```cmake
if(WIN32 AND ENABLE_SLANG)
    add_custom_command(TARGET chameleonrt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${Slang_ROOT}/bin/slang.dll"
            "$<TARGET_FILE_DIR:chameleonrt>"
    )
endif()
```

### Issue: Missing DXC DLLs for HLSL compilation

```
Slang compilation failed:
failed to load downstream compiler 'dxc'
could not find a suitable pass-through compiler for 'dxc'
```

**Cause:** Slang requires Microsoft's DXC compiler (`dxcompiler.dll` and `dxil.dll`) for HLSL→DXIL compilation.

**Solution (Automated - Already Implemented):**
The DXR backend CMakeLists.txt automatically deploys DXC DLLs:
```cmake
# In backends/dxr/CMakeLists.txt
if(ENABLE_DXR_SLANG)
    # Automatically finds Windows SDK and copies DXC DLLs
    # Searches versions: 10.0.26100.0, 10.0.22621.0, etc.
    add_custom_command(TARGET crt_dxr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WINSDK_BIN_DIR}/dxcompiler.dll"
            "$<TARGET_FILE_DIR:crt_dxr>"
        # ... also copies dxil.dll
    )
endif()
```

**Manual Solution (if needed):**
Copy DLLs from Windows SDK to output directory:
```powershell
Copy-Item "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxcompiler.dll" build\Debug\
Copy-Item "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxil.dll" build\Debug\
```

### Issue: Compilation errors

```
Slang compilation failed: error: undeclared identifier 'float3'
```

**Solution:**
- Check Slang syntax (may differ slightly from HLSL)
- Verify shader stage is correct
- Check Slang version compatibility
- Review Slang documentation

### Issue: DXIL validation fails

```
D3D12: DXIL verification failed
```

**Solution:**
- Compare with DXC-generated DXIL
- Check shader model version (sm_6_0 vs sm_6_6)
- Verify entry point signatures match
- Use `dxil-dis` to disassemble and compare

### Issue: SPIRV validation fails

```
spirv-val: error: Invalid SPIR-V binary
```

**Solution:**
```powershell
# Save SPIRV to file
# Validate manually
spirv-val shader.spv

# Check Slang SPIRV target settings
# May need specific Vulkan version target
```

---

## Best Practices

### 1. Start Simple
- Port simplest shader first (solid color fragment shader)
- Verify that works before complex shaders
- Build confidence incrementally

### 2. Keep Dual Paths Initially
- Don't remove DXC/glslang immediately
- Easy to compare outputs
- Safety net for issues

### 3. Version Control Discipline
- Commit after each working phase
- Tag milestones (e.g., `slang-passthrough-working`)
- Easy to rollback if needed

### 4. Test on Both APIs Frequently
- Don't get too far ahead on one API
- Cross-platform issues harder to fix later
- Validates Slang's cross-compilation

### 5. Performance Baseline Early
- Measure before Slang integration
- Track throughout migration
- Identify regressions quickly

### 6. Document Gotchas
- Slang syntax differences from HLSL/GLSL
- Platform-specific attributes needed
- Workarounds for bugs
- Add to this file or separate doc

---

## Success Checklist

**DXR Backend Integration Status (Updated: Current Session)**

### Phase 1: Build System & Initialization ✅ COMPLETE
- [x] CMake finds Slang correctly
- [x] Utility library builds without errors (`slang_compiler_util.lib`)
- [x] Slang DLLs deploy to output directory (`slang.dll` in `build/Debug/`)
- [x] DXR backend builds with Slang support (`crt_dxr.dll` created)
- [x] Backend plugin loads at runtime (verified with `chameleonrt.exe dxr test_cube.obj`)
- [x] SlangShaderCompiler member initialized successfully
- [x] Console output: `[Slang] Compiler initialized successfully` ✅
- [x] Can build with `-DENABLE_SLANG=OFF` (fallback works - traditional DXR build)
- [x] Can build with `-DENABLE_DXR_SLANG=ON` (Slang-enabled DXR build)

**Key Learnings:**
- Plugin architecture requires `crt_dxr.dll` to be built (not just a library)
- `ENABLE_DXR_SLANG` must be independent of `ENABLE_DXR` (allow separate builds)
- C++17 required for Slang headers (`std::optional`, inline variables)
- Header includes must be `#ifdef USE_SLANG_COMPILER` guarded

### Phase 2: Runtime Compilation ✅ COMPLETE
- [x] HLSL→DXIL pass-through works (D3D12) ✅
  - [x] Hardcoded shader test (Prompt 2) ✅
  - [x] File-based shader loading (Prompt 3) ✅
  - [x] Per-entry-point compilation working ✅
  - [ ] Production shaders with includes (Prompt 4) - **NEXT**
- [x] D3D12 pipeline accepts Slang-compiled DXIL ✅
- [x] Rendering produces correct visual output ✅
- [x] No D3D12 validation layer errors ✅

**Implementation Details:**
- API: `getEntryPointCode(entryPointIndex, targetIndex, ...)` per shader
- Returns: `std::vector<ShaderBlob>` (one blob per entry point)
- Creates 4 separate `dxr::ShaderLibrary` objects
- All libraries added to RT pipeline builder
- Compiled sizes: RayGen=7832, Miss=3132, ShadowMiss=2488, ClosestHit=7112 bytes

### Phase 3: Vulkan & Cross-Platform (Future)
- [ ] GLSL→SPIRV pass-through works (Vulkan)
- [ ] First `.slang` shader compiles
- [ ] Slang shader works on D3D12
- [ ] Same Slang shader works on Vulkan
- [ ] No visual regressions
- [ ] Performance acceptable (<5% overhead)
- [ ] Validation layers happy

**Current Step:** Prompt 2 in `Migration/prompts/02-dxr-integration-runtime.md`  
**Test:** Hardcoded HLSL shader compilation at runtime

---

## Next Steps After Integration

Once basics working:

1. **Port all shaders incrementally**
   - Start with raster (if you have it)
   - Then ray tracing (raygen, miss, hit)
   
2. **Remove dual paths**
   - After 2 weeks of stability
   - Clean up `#ifdef USE_SLANG_COMPILER`
   
3. **Explore Slang features**
   - Interfaces
   - Generics
   - Parameter blocks
   
4. **Add Metal backend** (future)
   - Should be easier with Slang foundation
   - Use `compileSlangToMetal()`

---

## Reference Files

- **Migration/reference/slang_CMakeLists.txt** - Working CMakeLists.txt from slang backend
- **Migration/reference/integration_notes.txt** - Quick tips
- **Migration/MIGRATION_PLAN.md** - Overall strategy
- **Migration/LEARNINGS.md** - Why we're doing this

---

*"Integration is a marathon, not a sprint. Test early, test often, commit frequently."*
