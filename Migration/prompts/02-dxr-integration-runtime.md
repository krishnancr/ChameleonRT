# DXR Backend Integration Prompts - Phase 2: Runtime Compilation

## Overview

These prompts guide you through integrating **runtime shader compilation** using Slang in the DXR (DirectX Raytracing) backend.

**Phase:** 2 (Runtime Compilation)  
**Approach:** Direct HLSL → DXIL compilation at runtime  
**Duration:** 6-8 hours  
**Reference:** `Migration/DXR Integration/DXR_ARCHITECTURE_DIAGRAM.md`

**Goal:** Compile HLSL shaders to DXIL at runtime using Slang, bypassing build-time embedding entirely.

**Why Runtime Compilation:**
- ✅ Shader hot-reload during development
- ✅ Faster iteration (no C++ rebuild needed)
- ✅ More valuable learning experience
- ✅ Real-world game engine pattern

**Progression:**
1. Hardcoded minimal HLSL string → Verify Slang works end-to-end
2. Load simple Lambertian shader from file → Test file I/O
3. Load full production shaders with includes → Complete integration

**Key Discovery from Analysis:**
- `ShaderLibrary` uses single DXIL blob with multiple exports (DXR library model)
- Constructor: `ShaderLibrary(const void* bytecode, size_t size, std::vector<std::wstring> exports)`
- All shaders compile together as one library with entry points

---

## ✅ Prompt 1: Add SlangShaderCompiler to RenderDXR Class [COMPLETED]

**Status:** ✅ **COMPLETED** - Compiler initialized, DLL builds and loads successfully

**Key Learnings:**
1. **Plugin architecture requires `crt_dxr.dll`** - Not just a library, it's a dynamically loaded backend
2. **CMake dependency fix:** `ENABLE_DXR_SLANG` must NOT depend on `ENABLE_DXR` (allow independent builds)
3. **Early return fix:** Changed `if (NOT ENABLE_DXR)` to `if (NOT ENABLE_DXR AND NOT ENABLE_DXR_SLANG)`
4. **C++17 required:** Slang headers need C++17, changed from `CXX_STANDARD 14` to `17`
5. **Header include required:** `render_dxr.h` needs `#ifdef USE_SLANG_COMPILER` guarded include

**Validation Results:**
- ✅ Builds successfully with `.\build_with_slang.ps1 -EnableDXRSlang`
- ✅ `crt_dxr.dll` created in `build/Debug/`
- ✅ Console output: `[Slang] Compiler initialized successfully`
- ✅ Application starts without errors: `chameleonrt.exe dxr test_cube.obj`
- ✅ Slang DLL loaded and linked at runtime

**Next:** Prompt 2 - Test actual compilation with hardcoded shader

### Context Setup
**Files to open:**
1. `backends/dxr/render_dxr.h`
2. `backends/dxr/render_dxr.cpp`
3. `util/slang_shader_compiler.h` (API reference)

### Original Prompt (for reference)

```
Task: Add SlangShaderCompiler member to RenderDXR backend class

Context:
- Phase 2: Runtime Compilation
- Integrating Slang for runtime HLSL → DXIL compilation
- Backend class: RenderDXR in backends/dxr/render_dxr.h
- Want to compile shaders at runtime in build_raytracing_pipeline()

Current class structure from Migration/DXR Integration/DXR_ARCHITECTURE_DIAGRAM.md:
- Device: Microsoft::WRL::ComPtr<ID3D12Device5> device
- Pipeline: dxr::RTPipeline rt_pipeline
- Constructor: RenderDXR() and RenderDXR(device)

Requirements:

1. Add include at top of render_dxr.cpp:
```cpp
#include "slang_shader_compiler.h"
```

2. Add member variable to RenderDXR class in render_dxr.h (private section):
```cpp
chameleonrt::SlangShaderCompiler slangCompiler;
```

3. In constructor (both overloads), initialize and verify:
```cpp
// After create_device_objects() or in create_device_objects()
if (!slangCompiler.isValid()) {
    throw std::runtime_error("Failed to initialize Slang shader compiler");
}
std::cout << "[Slang] Compiler initialized successfully" << std::endl;
```

4. Update backends/dxr/CMakeLists.txt to link slang_compiler_util:
```cmake
target_link_libraries(crt_dxr PUBLIC
    dxr_shaders  # Keep for now (fallback)
    util
    display
    slang_compiler_util  # NEW: Add Slang support
    ${D3D12_LIBRARIES})

target_compile_definitions(crt_dxr PRIVATE
    USE_SLANG_RUNTIME=1)
```

Please:
- Add include to render_dxr.cpp (use replace_string_in_file with context)
- Add member to render_dxr.h class definition (with context)
- Add initialization to constructor with validation
- Update CMakeLists.txt to link and define USE_SLANG_RUNTIME
```

### Expected Output

- `#include "slang_shader_compiler.h"` added to render_dxr.cpp
- `SlangShaderCompiler slangCompiler;` member in RenderDXR class
- Initialization code in constructor with error checking
- CMakeLists.txt updated with linking and definition
- Console output: "[Slang] Compiler initialized successfully"

**Implementation Details:**
```cmake
# backends/CMakeLists.txt - Fixed dependency
cmake_dependent_option(ENABLE_DXR_SLANG 
    "Build DXR backend with Slang shader compilation" 
    OFF 
    "ENABLE_SLANG"  # Only depends on ENABLE_SLANG (not ENABLE_DXR)
    OFF
)

# backends/dxr/CMakeLists.txt - Fixed early return and C++ standard
if (NOT ENABLE_DXR AND NOT ENABLE_DXR_SLANG)  # Check both flags
    return()
endif()

set_target_properties(crt_dxr PROPERTIES
    CXX_STANDARD 17  # Changed from 14 for Slang & std::optional
    CXX_STANDARD_REQUIRED ON)

if(ENABLE_DXR_SLANG)
    target_link_libraries(crt_dxr PRIVATE slang_compiler_util)
    target_compile_definitions(crt_dxr PRIVATE USE_SLANG_COMPILER=1)
endif()
```

```cpp
// render_dxr.h - Guarded include and member
#ifdef USE_SLANG_COMPILER
#include "slang_shader_compiler.h"
#endif

struct RenderDXR : RenderBackend {
    // ... other members ...
    
#ifdef USE_SLANG_COMPILER
    chameleonrt::SlangShaderCompiler slangCompiler;
#endif
};
```

```cpp
// render_dxr.cpp - Guarded initialization in create_device_objects()
#ifdef USE_SLANG_COMPILER
    if (!slangCompiler.isValid()) {
        throw std::runtime_error("Failed to initialize Slang shader compiler");
    }
    std::cout << "[Slang] Compiler initialized successfully" << std::endl;
#endif
```

---

## ✅ Prompt 2: Test with Hardcoded Minimal HLSL Shader [COMPLETED]

**Status:** ✅ **COMPILATION SUCCESSFUL** - Slang runtime compilation proven to work!

**Achievements:**
1. ✅ Slang successfully compiles HLSL → DXIL at runtime
2. ✅ Bytecode generated: 3148 bytes (reasonable DXIL size)
3. ✅ Runtime linking works - SlangShaderCompiler is callable from backend
4. ⚠️ Pipeline creation fails (expected - test shader doesn't match root signature)

**Key Discovery:**
- **DXC DLLs required:** Slang needs `dxcompiler.dll` and `dxil.dll` for HLSL→DXIL compilation
- **Location:** `C:\Program Files (x86)\Windows Kits\10\bin\<SDK_VERSION>\x64\`
- **Deployment:** Must be copied to output directory alongside `slang.dll`

**Console Output:**
```
[Slang] Compiler initialized successfully
[Slang] Compiling hardcoded test shader...
[Slang] SUCCESS! Bytecode size: 3148 bytes
```

**Why Pipeline Creation Failed:**
Our minimal test shader has basic resources (just output UAV), but the actual pipeline expects:
- Acceleration structure (t0)
- Material buffer (t1)
- Lights buffer (t2)
- Texture array (t3)
- Constant buffers (b0, b1)
- Multiple UAVs (u0, u1, u2 for ray stats)

**Next:** Use the REAL production shader from `render_dxr.hlsl` to get a working render

---

## 📍 Prompt 3: Compile Production Shader with Includes [CURRENT STEP]

**Goal:** Prove Slang can compile HLSL → DXIL and create a valid D3D12 pipeline (runtime linking test)

### Context Setup
**Files to open:**
1. `backends/dxr/render_dxr.cpp` - Navigate to `build_raytracing_pipeline()` function (line ~602)
2. `util/slang_shader_compiler.h` - Reference the API

**Current code location:** `render_dxr.cpp:602-606`
```cpp
void RenderDXR::build_raytracing_pipeline()
{
    dxr::ShaderLibrary shader_library(render_dxr_dxil,
                                      sizeof(render_dxr_dxil),
                                      {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"});
```

### Refined Prompt

```
Task: Test Slang runtime compilation with minimal hardcoded HLSL shader

Context:
- SlangShaderCompiler member initialized successfully (Prompt 1 ✅)
- Testing ACTUAL compilation: HLSL string → DXIL bytecode → D3D12 pipeline
- Using minimal hardcoded shader to isolate Slang from file I/O issues
- Location: backends/dxr/render_dxr.cpp, function build_raytracing_pipeline() at line 602

Current code creates ShaderLibrary from embedded DXIL array:
```cpp
dxr::ShaderLibrary shader_library(render_dxr_dxil,  // Embedded at build time
                                  sizeof(render_dxr_dxil),
                                  {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"});
```

Requirements:

1. **Add #ifdef guard** at start of build_raytracing_pipeline():
```cpp
void RenderDXR::build_raytracing_pipeline()
{
#ifdef USE_SLANG_COMPILER
    // Slang path will go here
#else
    // Keep original embedded DXIL path
    dxr::ShaderLibrary shader_library(render_dxr_dxil,
                                      sizeof(render_dxr_dxil),
                                      {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"});
#endif
```

2. **Create minimal test shader** (in #ifdef USE_SLANG_COMPILER block):
```cpp
#ifdef USE_SLANG_COMPILER
    // Minimal test shader - just to verify compilation works
    const char* test_hlsl = R"(
RWTexture2D<float4> output : register(u0);

struct Payload {
    float3 color;
};

[shader("raygeneration")]
void RayGen() {
    uint2 pixel = DispatchRaysIndex().xy;
    output[pixel] = float4(1.0, 0.0, 1.0, 1.0); // Magenta test
}

[shader("miss")]
void Miss(inout Payload payload) {
    payload.color = float3(0.0, 0.0, 0.0); // Black
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs) {
    payload.color = float3(0.0, 1.0, 0.0); // Green
}

[shader("miss")]
void ShadowMiss(inout Payload payload) {
    payload.color = float3(1.0, 1.0, 1.0); // White
}
)";

    std::cout << "[Slang] Compiling hardcoded test shader..." << std::endl;
```

3. **Compile with Slang** - Important: Use RayGen stage for DXR library compilation:
```cpp
    // Compile HLSL to DXIL using Slang
    auto result = slangCompiler.compileHLSLToDXIL(
        test_hlsl,
        "RayGen",  // Entry point doesn't matter for library - all exports included
        chameleonrt::ShaderStage::RayGen  // Use RayGen (library mode in compiler)
    );

    if (!result) {
        std::string error = slangCompiler.getLastError();
        std::cerr << "[Slang] Compilation FAILED:\n" << error << std::endl;
        throw std::runtime_error("Slang shader compilation failed");
    }

    std::cout << "[Slang] SUCCESS! Bytecode size: " 
              << result->bytecode.size() << " bytes" << std::endl;
```

4. **Create ShaderLibrary from Slang bytecode:**
```cpp
    // Create D3D12 shader library from Slang-compiled DXIL
    dxr::ShaderLibrary shader_library(
        result->bytecode.data(),
        result->bytecode.size(),
        {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}
    );
#else
    // Original embedded DXIL path...
```

Please:
- Use replace_string_in_file to replace the shader_library construction
- Add the #ifdef guard to keep both paths (easy rollback if needed)
- Include full context (5+ lines before/after) in replace_string_in_file
- Keep all logging for debugging
```

### Expected Output

Console when running `chameleonrt.exe dxr test_cube.obj`:
```
[Slang] Compiler initialized successfully
[Slang] Compiling hardcoded test shader...
[Slang] SUCCESS! Bytecode size: 2456 bytes
```

### Validation Checklist

### Validation Checklist

- [ ] **Builds:** `cmake --build build --config Debug --target crt_dxr`
  - No compile errors
  - Links successfully
  
- [ ] **Slang compilation succeeds:** Run `chameleonrt.exe dxr test_cube.obj`
  - Console: `[Slang] Compiler initialized successfully`
  - Console: `[Slang] Compiling hardcoded test shader...`
  - Console: `[Slang] SUCCESS! Bytecode size: XXXX bytes`
  - Bytecode size should be 1000-5000 bytes (reasonable DXIL size)

- [ ] **D3D12 pipeline creates:** No validation layer errors
  - State object builds successfully
  - No D3D12 debug layer errors about invalid bytecode
  - Check PIX if needed

- [ ] **Application runs:** No crashes
  - Window opens
  - May show magenta/black/garbled output (shader is minimal)
  - **Success = no crash, pipeline builds, Slang worked!**

**If successful:** We've proven Slang can compile at runtime and D3D12 accepts the bytecode!

**Next:** Prompt 3 - Load actual production shader from file

---

## ✅ Prompt 3: Load Production Shader from File [COMPLETED]

**Status:** ✅ **COMPLETE** - Per-entry-point compilation working, renders successfully!

**Major Achievement:** Avoided Slang compiler bug by implementing per-entry-point compilation

**Final Implementation:**
- **API:** `linkedProgram->getEntryPointCode(i, 0, ...)` for each entry point
- **Return Type:** `std::optional<std::vector<ShaderBlob>>` (4 blobs)
- **D3D12 Integration:** 4 separate `dxr::ShaderLibrary` objects
- **Compiled Sizes:**
  - RayGen: 7,832 bytes
  - Miss: 3,132 bytes
  - ShadowMiss: 2,488 bytes
  - ClosestHit: 7,112 bytes

**Key Discovery:**
- `getTargetCode()` crashes with `Slang::InternalError` for DXIL multi-entry libraries from HLSL
- Workaround: Use `getEntryPointCode()` per entry point (GFX layer pattern)
- D3D12 RT pipeline natively supports multiple DXIL library subobjects
- This is the production-proven approach from Slang's own GFX implementation

**Console Output (Clean):**
```
Loading OBJ: ../../test_cube.obj
Scene loaded successfully
# Unique Triangles: 12
# Meshes: 1
```

**Validation Results:**
- ✅ File loads from DLL-relative path (11,004 bytes)
- ✅ All 4 entry points compile successfully
- ✅ D3D12 pipeline creates without errors
- ✅ Application renders correctly
- ✅ No validation layer errors
- ✅ Debug output cleaned up (production-ready)

**Next:** Prompt 4 - Production shaders with `#include` directives (requires include handler)

---

## 📍 Prompt 3: Load Compatible Simple Lambertian Shader from File [HISTORICAL - SEE ABOVE FOR COMPLETION]

**Status:** ⏳ **READY TO IMPLEMENT** - Shader deployed, compatibility verified

**Goal:** Load `simple_lambertian.hlsl` from file and compile it with Slang, proving file I/O + complete pipeline integration

**Pre-requisites Completed:**
1. ✅ Shader deployed to `build/Debug/simple_lambertian.hlsl` via CMake POST_BUILD
2. ✅ Compatibility analysis complete - 100% match with pipeline expectations
3. ✅ DXC DLLs auto-deployed for Slang compilation
4. ✅ Hardcoded test shader proved Slang compilation works

### Context Setup

**Files to open:**
1. `backends/dxr/render_dxr.cpp` - Navigate to `build_raytracing_pipeline()` function (line ~605)
2. `backends/dxr/simple_lambertian.hlsl` - Review the shader source
3. `Migration/COMPATIBILITY_ANALYSIS.md` - Reference compatibility verification

**Current State:**
- `build/Debug/simple_lambertian.hlsl` exists (11,344 bytes) - auto-deployed by CMake
- Hardcoded test shader compiled successfully (3148 bytes DXIL)
- Pipeline expects all entry points: RayGen, Miss, ShadowMiss, ClosestHit
- simple_lambertian.hlsl matches pipeline 100% (verified in analysis)

**Why This Shader:**
- ✅ Self-contained (no #include dependencies)
- ✅ All 4 required entry points present
- ✅ Correct payload structures (HitInfo, OcclusionHitInfo)
- ✅ All resource bindings match pipeline expectations
- ✅ Produces visible output (Lambertian shading with checkerboard pattern)

### Implementation Strategy

**Three key components:**
1. **DLL-relative path resolution** - Find shader next to `crt_dxr.dll`
2. **File loading helper** - Read shader source from disk
3. **Replace hardcoded test** - Use file-loaded shader instead

**Why DLL-relative paths:**
- ✅ Works from any working directory
- ✅ Shader always found (deployed with DLL)
- ✅ Portable across machines
- ✅ No hardcoded absolute paths

### Detailed Prompt

```
Task: Load simple_lambertian.hlsl from file using DLL-relative path resolution

Context:
- Hardcoded test shader compiled successfully (Prompt 2 ✅)
- simple_lambertian.hlsl deployed to build/Debug/ alongside crt_dxr.dll
- Need to find shader relative to DLL location (not working directory)
- Shader is 100% compatible with pipeline (verified analysis)
- Self-contained shader (no includes to resolve)

Current code location: backends/dxr/render_dxr.cpp, line ~605-663 in build_raytracing_pipeline()
Current implementation uses hardcoded test HLSL string in #ifdef USE_SLANG_COMPILER block

Requirements:

1. **Add Windows API include** at top of render_dxr.cpp (for GetModuleHandleExA):
```cpp
#ifdef USE_SLANG_COMPILER
#include <fstream>
#include <sstream>
#include <filesystem>
#include <Windows.h>  // For GetModuleHandleExA, GetModuleFileNameA
#endif
```

2. **Add DLL-relative path helper** (in anonymous namespace before build_raytracing_pipeline):
```cpp
#ifdef USE_SLANG_COMPILER
namespace {
    // Get the directory where crt_dxr.dll is located
    std::filesystem::path get_dll_directory() {
        char dll_path[MAX_PATH];
        HMODULE hModule = NULL;
        
        // Get handle to this DLL (crt_dxr.dll)
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&get_dll_directory,  // Any function address in this DLL
            &hModule
        );
        
        if (hModule == NULL) {
            throw std::runtime_error("Failed to get DLL module handle");
        }
        
        // Get full path to DLL
        if (GetModuleFileNameA(hModule, dll_path, MAX_PATH) == 0) {
            throw std::runtime_error("Failed to get DLL path");
        }
        
        // Return parent directory
        std::filesystem::path dll_file_path(dll_path);
        return dll_file_path.parent_path();
    }
    
    // Load shader source from file
    std::string load_shader_source(const std::filesystem::path& shader_path) {
        std::ifstream file(shader_path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + shader_path.string());
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        std::cout << "[Slang] Loaded shader: " << shader_path.filename().string()
                  << " (" << content.size() << " bytes)" << std::endl;
        
        return content;
    }
}
#endif // USE_SLANG_COMPILER
```

3. **Replace hardcoded test shader** in build_raytracing_pipeline() #ifdef USE_SLANG_COMPILER block:

Find this section (around line 605-663):
```cpp
#ifdef USE_SLANG_COMPILER
    const char* test_hlsl = R"(/* hardcoded shader */)";
    
    std::cout << "[Slang] Compiling hardcoded test shader..." << std::endl;
    
    auto result = slangCompiler.compileHLSLToDXIL(test_hlsl, "RayGen", ShaderStage::RayGen);
    // ... rest of test code
#else
    dxr::ShaderLibrary shader_library(render_dxr_dxil, ...);
#endif
```

Replace with:
```cpp
#ifdef USE_SLANG_COMPILER
    // Get shader path relative to DLL location
    std::filesystem::path dll_dir = get_dll_directory();
    std::filesystem::path shader_path = dll_dir / "simple_lambertian.hlsl";
    
    std::cout << "[Slang] DLL directory: " << dll_dir.string() << std::endl;
    std::cout << "[Slang] Loading shader: " << shader_path.string() << std::endl;
    
    // Load shader source from file
    std::string hlsl_source;
    try {
        hlsl_source = load_shader_source(shader_path);
    } catch (const std::exception& e) {
        std::cerr << "[Slang] ERROR: " << e.what() << std::endl;
        throw;
    }
    
    std::cout << "[Slang] Compiling simple_lambertian.hlsl..." << std::endl;
    
    // Compile HLSL to DXIL using Slang
    auto result = slangCompiler.compileHLSLToDXIL(
        hlsl_source,
        "RayGen",  // Entry point (DXR library includes all exports)
        chameleonrt::ShaderStage::RayGen
    );
    
    if (!result) {
        std::string error = slangCompiler.getLastError();
        std::cerr << "[Slang] Compilation FAILED:" << std::endl;
        std::cerr << error << std::endl;
        throw std::runtime_error("Slang shader compilation failed");
    }
    
    std::cout << "[Slang] SUCCESS! Compiled DXIL bytecode: " 
              << result->bytecode.size() << " bytes" << std::endl;
    
    // Create D3D12 shader library from Slang-compiled DXIL
    dxr::ShaderLibrary shader_library(
        result->bytecode.data(),
        result->bytecode.size(),
        {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}  // All 4 entry points
    );
#else
    // Original embedded DXIL path (unchanged)
    dxr::ShaderLibrary shader_library(render_dxr_dxil,
                                      sizeof(render_dxr_dxil),
                                      {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"});
#endif
```

Please:
- Use replace_string_in_file with sufficient context (5-10 lines before/after)
- Add includes at top with #ifdef guard
- Add helper functions in anonymous namespace
- Replace hardcoded shader with file loading
- Keep all error handling and logging
- Maintain dual-path structure (#ifdef vs original)
```

### Expected Output

**Build:**
```
-- Found DXC in Windows SDK 10.0.26100.0
-- DXC DLLs will be deployed to output directory
-- Shader files will be deployed to output directory
-- Build files have been written to: C:/dev/ChameleonRT/build
...
Deploying DXC DLLs for Slang HLSL compilation Deploying simple_lambertian.hlsl for runtime Slang compilation
```

**Runtime Console:**
```
[Slang] Compiler initialized successfully
[Slang] DLL directory: C:\dev\ChameleonRT\build\Debug
[Slang] Loading shader: C:\dev\ChameleonRT\build\Debug\simple_lambertian.hlsl
[Slang] Loaded shader: simple_lambertian.hlsl (11344 bytes)
[Slang] Compiling simple_lambertian.hlsl...
[Slang] SUCCESS! Compiled DXIL bytecode: XXXXX bytes
```

**Visual Output:**
- Window opens showing rendered scene
- Geometry with Lambertian diffuse shading
- Checkerboard pattern from UV coordinates (alternating tan/blue)
- Simple procedural sky (checkered pattern matching production)
- Smooth or geometric normals (based on mesh)
- Progressive accumulation and anti-aliasing

### Validation Checklist

**Build-Time:**
- [ ] **CMake detects shader:** Message "Shader files will be deployed to output directory"
- [ ] **POST_BUILD copies shader:** Check `build/Debug/simple_lambertian.hlsl` exists
- [ ] **File size correct:** 11,344 bytes (matches source)
- [ ] **No compile errors:** Clean C++ compilation

**Runtime:**
- [ ] **DLL directory found:** Console shows correct path to build/Debug/
- [ ] **Shader file loads:** Console shows "Loaded shader: simple_lambertian.hlsl (11344 bytes)"
- [ ] **Compilation succeeds:** Console shows "SUCCESS! Compiled DXIL bytecode: N bytes"
  - Bytecode size should be 8,000-15,000 bytes (larger than test shader due to full implementation)
- [ ] **Pipeline creates:** No D3D12 errors about invalid state object
- [ ] **Window opens:** Application displays rendered scene
- [ ] **Rendering works:** Visible geometry with shading (not black screen)
- [ ] **No crashes:** Application runs to completion

**Visual Quality:**
- [ ] **Shading visible:** Geometry shows lighting (not flat color)
- [ ] **Checkerboard pattern:** If model has UVs, alternating tan/blue colors visible
- [ ] **Sky background:** Checkered pattern in background (matches production)
- [ ] **Normals correct:** Smooth or faceted shading based on mesh
- [ ] **No artifacts:** No obvious errors, black triangles, or corruption

**Compatibility Verification:**
- [ ] **All entry points work:** RayGen, Miss, ShadowMiss, ClosestHit all execute
- [ ] **Resource bindings valid:** No D3D12 debug layer errors about missing resources
- [ ] **Payload structures work:** No crashes from payload size mismatch
- [ ] **Descriptor heaps valid:** No errors about invalid heap access

### Troubleshooting

**Problem:** "Failed to open shader file: C:\dev\ChameleonRT\build\Debug\simple_lambertian.hlsl"  
**Solution:**  
1. Check file exists: `Test-Path C:\dev\ChameleonRT\build\Debug\simple_lambertian.hlsl`
2. Rebuild to trigger POST_BUILD: `cmake --build build --config Debug --target crt_dxr`
3. Check CMake message: Should see "Deploying simple_lambertian.hlsl..."

**Problem:** "Slang shader compilation failed" with DXC errors  
**Solution:**  
1. Check DXC DLLs present: `dxcompiler.dll` and `dxil.dll` in build/Debug/
2. Check Windows SDK installed: CMake should find 10.0.26100.0 or later
3. Read full error output - Slang provides detailed HLSL errors

**Problem:** Pipeline creation fails (E_INVALIDARG or similar)  
**Solution:**  
1. Check compatibility analysis: `Migration/COMPATIBILITY_ANALYSIS.md`
2. Verify all 4 entry points in export list: RayGen, Miss, ClosestHit, ShadowMiss
3. Check bytecode size is reasonable (>1000 bytes)
4. Enable D3D12 debug layer for detailed errors

**Problem:** Black screen or no rendering  
**Solution:**  
1. Check for D3D12 validation errors in console
2. Verify pipeline state object created successfully
3. Test with original embedded shader (disable USE_SLANG_COMPILER) to compare
4. Use PIX for Windows to capture frame and inspect shaders

**Problem:** Crashes on startup  
**Solution:**  
1. Check slang.dll loaded (should see "[Slang] Compiler initialized successfully")
2. Verify exception messages in console
3. Check file paths are correct (DLL directory resolution)
4. Run with debugger to catch exception point

### What Success Looks Like

**Console Output:**
```
[Slang] Compiler initialized successfully
[Slang] DLL directory: C:\dev\ChameleonRT\build\Debug
[Slang] Loading shader: C:\dev\ChameleonRT\build\Debug\simple_lambertian.hlsl
[Slang] Loaded shader: simple_lambertian.hlsl (11344 bytes)
[Slang] Compiling simple_lambertian.hlsl...
[Slang] SUCCESS! Compiled DXIL bytecode: 12453 bytes
Display: 1280x720, backend: dxr
Loaded 1 meshes, 8 geometries in 0.05s
```

**Visual:**
- ✅ Window opens with rendered cube
- ✅ Lambertian shading visible (diffuse lighting)
- ✅ Checkerboard texture pattern (tan/blue alternating)
- ✅ Checkered sky background
- ✅ Smooth camera movement
- ✅ Progressive refinement (image improves over time)

**Modified Shader Test:**
1. Edit `build/Debug/simple_lambertian.hlsl`
2. Change line ~175: `float3 albedo = float3(0.8f, 0.6f, 0.4f);` to `float3(1.0f, 0.0f, 0.0f);`
3. Restart application
4. ✅ Cube should now be red (proves shader loaded from file)

### Success Criteria

This prompt is complete when:
- ✅ Shader loads from file (DLL-relative path)
- ✅ Slang compiles shader successfully
- ✅ Pipeline creates with Slang-compiled DXIL
- ✅ Rendering produces visible output
- ✅ All 4 entry points execute correctly
- ✅ No D3D12 validation errors
- ✅ Can modify shader file and see changes (after restart)

**Next:** Prompt 4 - Load production shaders with #include resolution

---

## Prompt 4: Load Production Shaders with Manual Include Resolution

### Context Setup
**Files to open:**
1. `backends/dxr/render_dxr.cpp`
2. `backends/dxr/render_dxr.hlsl` (production shader)
3. `backends/dxr/util.hlsl`, `disney_bsdf.hlsl`, etc. (includes)

### Prompt

```
Task: Implement manual include resolution for production shaders

Context:
- Simple Lambertian shader works (Prompt 3)
- Now loading full render_dxr.hlsl with Disney BRDF, lighting, etc.
- Has #include directives that need manual resolution
- Include path: backends/dxr/ and util/

Production shader includes:
#include "util.hlsl"
#include "lcg_rng.hlsl"
#include "disney_bsdf.hlsl"
#include "lights.hlsl"
#include "util/texture_channel_mask.h"

Requirements:

1. Upgrade loadShaderSource to handle includes:
```cpp
namespace {
    std::string resolveIncludes(const std::string& source, 
                                const std::string& base_path,
                                std::set<std::string>& processed) {
        std::stringstream result;
        std::istringstream stream(source);
        std::string line;
        
        while (std::getline(stream, line)) {
            // Check for #include directive
            size_t include_pos = line.find("#include");
            if (include_pos != std::string::npos) {
                // Extract filename between quotes
                size_t quote1 = line.find('"', include_pos);
                size_t quote2 = line.find('"', quote1 + 1);
                
                if (quote1 != std::string::npos && quote2 != std::string::npos) {
                    std::string include_file = line.substr(quote1 + 1, quote2 - quote1 - 1);
                    std::string include_path = base_path + "/" + include_file;
                    
                    // Prevent circular includes
                    if (processed.find(include_path) == processed.end()) {
                        processed.insert(include_path);
                        
                        std::cout << "[Slang]   Including: " << include_file << std::endl;
                        
                        // Load and recursively resolve the include
                        std::string included_source = loadShaderSource(include_path);
                        std::string resolved = resolveIncludes(included_source, base_path, processed);
                        result << resolved << "\n";
                    }
                    continue;  // Skip the #include line itself
                }
            }
            
            result << line << "\n";
        }
        
        return result.str();
    }
    
    std::string loadShaderSource(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + filename);
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    
    std::string loadShaderSourceWithIncludes(const std::string& filename) {
        std::cout << "[Slang] Loading shader with includes: " << filename << std::endl;
        
        std::string source = loadShaderSource(filename);
        
        // Get base path
        size_t last_slash = filename.find_last_of("/\\");
        std::string base_path = (last_slash != std::string::npos) 
                              ? filename.substr(0, last_slash) 
                              : ".";
        
        std::set<std::string> processed;
        std::string resolved = resolveIncludes(source, base_path, processed);
        
        std::cout << "[Slang] Total source size after includes: " 
                  << resolved.size() << " bytes" << std::endl;
        
        return resolved;
    }
}
```

2. Add <set> include at top:
```cpp
#include <set>
```

3. In build_raytracing_pipeline(), load production shader:
```cpp
// Load production shader with all includes
std::string shader_path = "backends/dxr/render_dxr.hlsl";
std::string hlsl_source = loadShaderSourceWithIncludes(shader_path);

std::cout << "[Slang] Compiling production shader (Disney BRDF)..." << std::endl;

auto result = slangCompiler.compileHLSLToDXIL(
    hlsl_source,
    "RayGen",
    chameleonrt::ShaderStage::Library
);

if (!result) {
    std::cerr << "[Slang] Compilation failed:" << std::endl;
    std::cerr << slangCompiler.getLastError() << std::endl;
    throw std::runtime_error("Production shader compilation failed");
}

std::cout << "[Slang] Production shader compiled: " 
          << result->bytecode.size() << " bytes" << std::endl;

dxr::ShaderLibrary shader_library(
    result->bytecode.data(),
    result->bytecode.size(),
    {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}  // All 4 entry points back
);
```

4. Restore full pipeline config:
- Re-add `.add_miss_shader(L"ShadowMiss")`
- Ensure all hit groups configured

Please:
- Add include resolution functions
- Update to load production shader
- Add comprehensive error output (show Slang errors in full)
- Test with full Disney BRDF rendering
```

### Expected Output

- Include resolution functions added
- Production shader loads with all dependencies
- Console shows each include being loaded
- Full shader compiles successfully
- All 4 entry points exported

### Validation Checklist

- [ ] **All includes load:** Console shows each #include file

- [ ] **Large source:** After includes, source is ~2000+ bytes

- [ ] **Compiles:** Slang compiles full production shader

- [ ] **Renders correctly:** Application shows:
  - Proper Disney BRDF shading
  - Textures (if scene has them)
  - Shadows from ShadowMiss
  - Lighting

- [ ] **Matches embedded version:** Compare output with original (if you have it)
  - Visual quality should be identical
  - Performance may be slightly slower (first compile)

- [ ] **No compilation errors:** Clean Slang output

**Next:** Prompt 5 - Add shader hot-reload (optional)

---

## Prompt 5: Add Shader Hot-Reload (Optional - Quality of Life)

### Context Setup
**Files to open:**
1. `backends/dxr/render_dxr.h`
2. `backends/dxr/render_dxr.cpp`
3. `main.cpp` (if needed for input handling)

### Prompt

```
Task: Add shader hot-reload for development workflow

Context:
- Production shaders compile at runtime successfully
- Want to reload shaders without restarting application
- Watch for key press (e.g., F5) to trigger reload
- Rebuild pipeline with new shaders

Requirements:

1. Add flag to trigger reload (in main.cpp or render loop):
```cpp
// In main render loop or input handling
if (key_pressed == SDLK_F5) {
    std::cout << "[Slang] F5 pressed, reloading shaders..." << std::endl;
    renderer->reloadShaders();
}
```

2. Add reloadShaders() method to RenderDXR class (render_dxr.h):
```cpp
void reloadShaders();
```

3. Implement reloadShaders() in render_dxr.cpp:
```cpp
void RenderDXR::reloadShaders() {
    std::cout << "[Slang] Reloading shaders from disk..." << std::endl;
    
    // Rebuild the pipeline (will recompile shaders)
    build_raytracing_pipeline();
    build_shader_binding_table();
    
    // Rebuild descriptor heap (resource bindings)
    build_descriptor_heap();
    
    // Re-record command lists with new pipeline
    record_command_lists();
    
    std::cout << "[Slang] Shaders reloaded successfully!" << std::endl;
    
    // Reset frame counter to see fresh renders
    frame_id = 0;
}
```

4. Make shader path configurable (optional):
```cpp
// Add member variable
std::string shader_file_path = "backends/dxr/render_dxr.hlsl";

// Allow override from constructor or config
```

Please:
- Add F5 key handling in appropriate place
- Implement reloadShaders() method
- Test: edit shader, press F5, see changes immediately
- Add console feedback for reload events
```

### Expected Output

- F5 key triggers shader reload
- Shaders recompile from disk
- Pipeline rebuilds with new shaders
- Scene re-renders with updated shaders
- No application restart needed

### Validation Checklist

- [ ] **F5 triggers reload:** Console shows "[Slang] Reloading shaders..."

- [ ] **Shaders recompile:** Fresh compilation from disk files

- [ ] **Changes visible:** Edit shader color/lighting, press F5, see change

- [ ] **No crashes:** Reload works multiple times in a row

- [ ] **Performance acceptable:** Reload takes <2 seconds

**Workflow test:**
1. Run application with test scene
2. Edit render_dxr.hlsl (change a color or add debug output)
3. Press F5
4. See changes immediately!

---

## Summary & Next Steps

### What We Accomplished

✅ **Prompt 1:** SlangShaderCompiler integrated into RenderDXR class  
✅ **Prompt 2:** Hardcoded minimal shader compiled and rendered  
✅ **Prompt 3:** Simple Lambertian shader loaded from file  
✅ **Prompt 4:** Production shaders with manual include resolution  
✅ **Prompt 5:** Hot-reload for development workflow (optional)

### Integration Complete When:

- [ ] Production shaders (render_dxr.hlsl) compile via Slang
- [ ] All entry points work (RayGen, Miss, ShadowMiss, ClosestHit)
- [ ] Rendering output matches embedded DXC version
- [ ] No D3D12 validation errors
- [ ] Shader hot-reload functional (if implemented)
- [ ] Performance acceptable (<10% slower than embedded)

### Future Enhancements

1. **Proper Slang Include API:** Use Slang's file system API instead of manual resolution
2. **Shader Caching:** Cache compiled DXIL to disk for faster startup
3. **Native Slang Shaders:** Port to `.slang` syntax for advanced features
4. **Vulkan Backend:** Apply same pattern to Vulkan SPIRV compilation
5. **Shader Validation:** Add pre-compile validation/linting

### Troubleshooting Guide

**Problem:** Slang compilation fails with include errors  
**Solution:** Check include paths, verify manual resolution logic

**Problem:** Pipeline creation fails  
**Solution:** Check entry point names match export list exactly

**Problem:** Renders black/wrong**  
**Solution:** Compare DXIL disassembly with DXC version, check resource bindings

**Problem:** Hot-reload crashes**  
**Solution:** Ensure proper synchronization (sync_gpu before rebuild)

---

## Appendix: Key Code Patterns

### Slang Compilation Pattern
```cpp
auto result = slangCompiler.compileHLSLToDXIL(
    hlsl_source,
    "EntryPoint",
    ShaderStage::Library
);

if (!result) {
    std::cerr << slangCompiler.getLastError() << std::endl;
    throw std::runtime_error("Compilation failed");
}

// Use result->bytecode
```

### ShaderLibrary Creation
```cpp
dxr::ShaderLibrary shader_library(
    bytecode_data,
    bytecode_size,
    {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}
);
```

### Include Resolution Pattern
```cpp
// Recursively load and inline all #include directives
// Base path = directory containing the main shader file
std::string resolved = resolveIncludes(source, base_path, processed_set);
```

---

**Ready to start? Begin with Prompt 1!** 🚀
