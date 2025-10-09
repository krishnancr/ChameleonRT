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

---

## Prompt 1: Analyze DXR Backend Architecture ✅ COMPLETED

**Status:** ✅ Analysis complete (see conversation summary above)

**Key Findings:**
- **Main class:** `RenderDXR` in `backends/dxr/render_dxr.h`
- **Integration point:** `build_raytracing_pipeline()` at line 591 in `render_dxr.cpp`
- **Current approach:** Build-time DXIL embedding (no runtime compilation)
- **Shaders:** RayGen, Miss, ShadowMiss, ClosestHit (no AnyHit)
- **Test shader ready:** `backends/dxr/shaders/simple_lambertian.hlsl`

**Skip to Prompt 2** - Analysis done!

---

## Prompt 2: Add SlangShaderCompiler to DXR Backend Class

### Prompt

```
Add SlangShaderCompiler member to the RenderDXR class with conditional compilation support.

Files to modify:
1. backends/dxr/render_dxr.h
2. backends/dxr/render_dxr.cpp

Tasks:

1. In render_dxr.h, add to the private section of RenderDXR class (around line 85-90):
```cpp
#ifdef USE_SLANG_COMPILER
    SlangShaderCompiler slangCompiler;
#endif
```

2. In render_dxr.cpp, add include near the top (after other includes):
```cpp
#ifdef USE_SLANG_COMPILER
#include "slang_shader_compiler.h"
#endif
```

3. In the RenderDXR constructor (the one without parameters), add validation logging:
```cpp
#ifdef USE_SLANG_COMPILER
    if (!slangCompiler.isValid()) {
        std::cerr << "[Slang] ERROR: Compiler initialization failed!" << std::endl;
        throw std::runtime_error("Slang compiler initialization failed");
    }
    std::cout << "[Slang] Compiler initialized successfully" << std::endl;
#endif
```

USE replace_string_in_file tool to make these changes with proper context.
```

### Expected Output

- #include added with #ifdef guard
- Member variable added to class
- Initialization code in constructor/init
- Error handling for failed init
- Compiles in both USE_SLANG_COMPILER=ON and OFF modes

### Validation Checklist

- [ ] **Builds with Slang:** `cmake --build build --target crt_dxr --config Debug -DUSE_SLANG_COMPILER=1`
  - Should compile without errors
  - May have linker warnings about unused slangCompiler, that's OK for now

- [ ] **Builds without Slang:** `cmake --build build_no_slang --target crt_dxr`
  - Should compile without errors
  - No references to Slang code

- [ ] **No warnings:** Check for "unused variable" warnings

- [ ] **Initializes:** Add temporary log: `std::cout << "Slang valid: " << slangCompiler.isValid() << std::endl;`
  - Should print "Slang valid: 1" when running with Slang enabled

---

## Prompt 3: Link Slang Utility Library in CMake

### Prompt

```
Update backends/dxr/CMakeLists.txt to link the slang_compiler_util library.

File: backends/dxr/CMakeLists.txt

After the existing target_link_libraries(crt_dxr PUBLIC ...) block (around line 56-60), add:

```cmake
# Link Slang compiler utility if enabled
if(ENABLE_SLANG OR ENABLE_DXR_SLANG)
    target_link_libraries(crt_dxr PRIVATE slang_compiler_util)
    target_compile_definitions(crt_dxr PRIVATE USE_SLANG_COMPILER=1)
endif()
```

USE replace_string_in_file to add this after the existing target_link_libraries block.
Include sufficient context (3-5 lines before/after).
```

### Expected Output

- CMake code linking slang_compiler_util
- USE_SLANG_COMPILER definition set
- Conditional on Slang being enabled
- Builds successfully

### Validation Checklist

- [ ] **Reconfigure CMake:** `cmake -B build -DENABLE_SLANG=ON -DENABLE_DXR_SLANG=ON`

- [ ] **Build DXR target:** `cmake --build build --target crt_dxr --config Debug`
  - Should link successfully
  - No "unresolved external symbol" errors for Slang functions

- [ ] **Check defines:** In build output, verify `-DUSE_SLANG_COMPILER=1` appears in compile commands

- [ ] **Run basic test:** `.\build\Debug\chameleonrt.exe dxr test_cube.obj`
  - May fail with other errors, but shouldn't crash on Slang init
  - If you added isValid() log, should print "Slang valid: 1"

---

## Prompt 4: Test with Hardcoded Minimal Shader (30 min)

### Prompt

```
Implement runtime HLSL compilation in build_raytracing_pipeline() using a hardcoded test shader.

File: backends/dxr/render_dxr.cpp
Function: build_raytracing_pipeline() (line 591)

Tasks:

1. Create a hardcoded minimal HLSL shader string at the start of build_raytracing_pipeline():

```cpp
#ifdef USE_SLANG_COMPILER
    // Minimal test shader
    const char* testHLSL = R"(
struct Payload { float3 color; };

[shader("raygeneration")]
void RayGen() {
    uint2 idx = DispatchRaysIndex().xy;
    output[idx] = float4(1, 0, 1, 1); // Magenta test output
}

[shader("miss")]
void Miss(inout Payload p) { p.color = float3(0, 0, 0); }

[shader("miss")]
void ShadowMiss(inout Payload p) { p.color = float3(1, 1, 1); }

[shader("closesthit")]
void ClosestHit(inout Payload p, BuiltInTriangleIntersectionAttributes attr) {
    p.color = float3(0.5, 0.5, 0.5);
}
)";

    // Compile with Slang
    auto result = slangCompiler.compileHLSLToDXIL(
        testHLSL,
        "",  // No entry point needed for library
        ShaderStage::Library
    );

    if (!result) {
        std::cerr << "[Slang] Compilation failed: " << slangCompiler.getLastError() << std::endl;
        throw std::runtime_error("Slang shader compilation failed");
    }

    std::cout << "[Slang] Compiled test shader: " << result->bytecode.size() << " bytes" << std::endl;

    // Use Slang-compiled bytecode
    dxr::ShaderLibrary shader_library(
        result->bytecode.data(),
        result->bytecode.size(),
        {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}
    );
#else
    // Original embedded DXIL path
    dxr::ShaderLibrary shader_library(render_dxr_dxil,
                                      sizeof(render_dxr_dxil),
                                      {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"});
#endif
```

2. Replace the existing ShaderLibrary construction (line 593-595) with the above code.

USE replace_string_in_file with proper context (5+ lines before/after the ShaderLibrary construction).

Expected result: Scene renders with magenta color when USE_SLANG_COMPILER=1 is defined.
```

---

## Prompt 5: Load Simple Lambertian Shader from File (1 hr)

### Prompt

```
Replace hardcoded test shader with file loading of simple_lambertian.hlsl.

File: backends/dxr/render_dxr.cpp
Function: build_raytracing_pipeline()

Tasks:

1. Add a helper function to load shader source (add before build_raytracing_pipeline()):

```cpp
#ifdef USE_SLANG_COMPILER
namespace {
std::string loadShaderSource(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
}
#endif
```

2. In build_raytracing_pipeline(), replace the hardcoded testHLSL with:

```cpp
#ifdef USE_SLANG_COMPILER
    // Load simple test shader from file
    std::string shaderPath = "backends/dxr/shaders/simple_lambertian.hlsl";
    std::string hlslSource = loadShaderSource(shaderPath);
    
    std::cout << "[Slang] Loading shader: " << shaderPath << std::endl;
    
    auto result = slangCompiler.compileHLSLToDXIL(
        hlslSource,
        "",
        ShaderStage::Library
    );

    if (!result) {
        std::cerr << "[Slang] Compilation failed: " << slangCompiler.getLastError() << std::endl;
        throw std::runtime_error("Slang shader compilation failed");
    }

    std::cout << "[Slang] Compiled successfully: " << result->bytecode.size() << " bytes" << std::endl;

    dxr::ShaderLibrary shader_library(
        result->bytecode.data(),
        result->bytecode.size(),
        {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}
    );
#else
    // ... existing embedded path ...
#endif
```

3. Add #include <fstream> and #include <sstream> at the top of render_dxr.cpp

USE replace_string_in_file for all modifications.

Expected result: Scene renders with simple Lambertian shading (gray/colored spheres).
```

### Expected Output

- Modified function with dual compilation paths
- Slang compilation code
- Error handling
- DXC fallback preserved
- Logging for verification

### Validation Checklist

- [ ] **Compiles:** Build crt_dxr target
  - Both USE_SLANG_COMPILER=ON and OFF modes work

- [ ] **Shader compiles:** Run with Slang enabled
  - Check log output: "Compiled raygen via Slang: [N] bytes"
  - Bytecode size should be reasonable (>100 bytes, <100KB typically)

- [ ] **Renders (or tries to):** Run chameleonrt with DXR backend
  - May not render correctly yet (other shaders not converted)
  - But should not crash in raygen compilation
  - Check PIX/debug layer for validation errors

- [ ] **Fallback works:** Build without USE_SLANG_COMPILER
  - Should still use DXC path
  - Should compile and run as before

---

## Prompt 6: Load Production Shader with Manual Include Resolution (2-3 hrs)

### Prompt

```
Implement manual include resolution and load the full render_dxr.hlsl shader.

File: backends/dxr/render_dxr.cpp

Tasks:

1. Enhance loadShaderSource() to handle #include directives recursively:

```cpp
#ifdef USE_SLANG_COMPILER
namespace {
std::string resolveIncludes(const std::string& source, const std::string& baseDir, int depth = 0) {
    if (depth > 10) {
        throw std::runtime_error("Include depth exceeded (circular include?)");
    }
    
    std::stringstream result;
    std::istringstream stream(source);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Check for #include "file.h"
        size_t includePos = line.find("#include");
        if (includePos != std::string::npos) {
            size_t quoteStart = line.find('"', includePos);
            size_t quoteEnd = line.find('"', quoteStart + 1);
            
            if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                std::string includePath = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                std::string fullPath = baseDir + "/" + includePath;
                
                // Load and recursively resolve the included file
                std::string includedSource = loadShaderSource(fullPath);
                std::string resolvedInclude = resolveIncludes(includedSource, baseDir, depth + 1);
                result << "// BEGIN INCLUDE: " << includePath << "\n";
                result << resolvedInclude;
                result << "// END INCLUDE: " << includePath << "\n";
                continue;
            }
        }
        result << line << "\n";
    }
    
    return result.str();
}

std::string loadShaderSource(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
}
#endif
```

2. Update build_raytracing_pipeline() to load production shader:

```cpp
#ifdef USE_SLANG_COMPILER
    std::string shaderPath = "backends/dxr/render_dxr.hlsl";
    std::string baseDir = "backends/dxr";
    
    std::cout << "[Slang] Loading production shader: " << shaderPath << std::endl;
    
    std::string hlslSource = loadShaderSource(shaderPath);
    std::string resolvedSource = resolveIncludes(hlslSource, baseDir);
    
    std::cout << "[Slang] Resolved shader: " << resolvedSource.length() << " characters" << std::endl;
    
    auto result = slangCompiler.compileHLSLToDXIL(
        resolvedSource,
        "",
        ShaderStage::Library
    );

    if (!result) {
        std::cerr << "[Slang] Compilation failed: " << slangCompiler.getLastError() << std::endl;
        // Optionally dump resolved source for debugging
        std::ofstream debug("slang_debug_output.hlsl");
        debug << resolvedSource;
        throw std::runtime_error("Slang shader compilation failed");
    }

    std::cout << "[Slang] Compiled production shader: " << result->bytecode.size() << " bytes" << std::endl;

    dxr::ShaderLibrary shader_library(
        result->bytecode.data(),
        result->bytecode.size(),
        {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}
    );
#else
    // ... existing embedded path ...
#endif
```

3. Add #include <sstream> if not already present

USE replace_string_in_file for all modifications.

Expected result: Full ray tracing with Disney BRDF, lighting, textures working correctly.
```

### Expected Output

- All shader compilation functions updated
- Consistent dual-compilation pattern
- Error handling and logging
- All shaders compile through Slang

### Validation Checklist

- [ ] **All functions compile:** Build crt_dxr

- [ ] **All shaders compile:** Run DXR backend
  - Check logs: "Compiled miss via Slang: ..."
  - Check logs: "Compiled closest hit via Slang: ..."
  - All shader types should compile successfully

- [ ] **Bytecode sizes reasonable:** 
  - Raygen: typically 1-5 KB
  - Miss: typically 0.5-2 KB
  - Hit: typically 2-10 KB
  - If sizes are 0 or extremely large, something's wrong

- [ ] **No compilation errors:** Check console for Slang diagnostics
  - Should be no "Slang compilation failed" messages

- [ ] **Ready for rendering test:** All shaders compile, ready to test pipeline

---

## Prompt 7: Add Shader Hot-Reload (Optional, 1-2 hrs)

### Prompt

```
Add file watcher for shader hot-reload during development.

File: backends/dxr/render_dxr.cpp

Tasks:

1. Add member variable to RenderDXR class header (render_dxr.h):

```cpp
#ifdef USE_SLANG_COMPILER
    std::string current_shader_path;
    std::filesystem::file_time_type shader_last_write_time;
#endif
```

2. Add reload check in render() method:

```cpp
#ifdef USE_SLANG_COMPILER
void RenderDXR::check_shader_reload() {
    if (current_shader_path.empty()) return;
    
    auto current_time = std::filesystem::last_write_time(current_shader_path);
    if (current_time != shader_last_write_time) {
        std::cout << "[Slang] Shader modified, reloading..." << std::endl;
        shader_last_write_time = current_time;
        
        try {
            build_raytracing_pipeline();
            build_shader_binding_table();
            std::cout << "[Slang] Hot-reload successful!" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[Slang] Hot-reload failed: " << e.what() << std::endl;
            // Keep using old pipeline
        }
    }
}
#endif
```

3. Call check_shader_reload() at the start of render() method

4. Store shader path when loading in build_raytracing_pipeline()

5. Add #include <filesystem> to render_dxr.cpp

USE replace_string_in_file for all modifications.

Expected result: Editing render_dxr.hlsl and saving will automatically recompile and update the render without restarting the application.
```

## Validation & Testing

After each prompt, validate your changes:

### After Prompt 2 & 3 (Build System):

Run in PowerShell:
```powershell
# Reconfigure with Slang enabled
cmake -B build -DENABLE_SLANG=ON -DENABLE_DXR_SLANG=ON -DSlang_ROOT=C:\dev\slang\build\Debug

# Build DXR backend
cmake --build build --target crt_dxr --config Debug

# Should see: "[Slang] Compiler initialized successfully" in build output
```

### After Prompt 4 (Hardcoded Test):

```powershell
cd build\Debug
.\chameleonrt.exe dxr ..\..\test_cube.obj
```

**Expected:** Magenta screen (test color from hardcoded shader)

### After Prompt 5 (Simple Lambertian):

```powershell
.\chameleonrt.exe dxr ..\..\test_cube.obj
```

**Expected:** Gray/simple shaded cube (Lambertian shading)

### After Prompt 6 (Production Shader):

```powershell
.\chameleonrt.exe dxr ..\..\test_cube.obj
```

**Expected:** Full rendering identical to non-Slang build

**Compare with build-time version:**
```powershell
# Build without Slang
cmake -B build_ref -DENABLE_DXR=ON
cmake --build build_ref --config Debug
cd build_ref\Debug
.\chameleonrt.exe dxr ..\..\test_cube.obj
```

Visual output should match exactly!

### After Prompt 7 (Hot-Reload):

```powershell
# Run the application
.\chameleonrt.exe dxr ..\..\test_cube.obj

# In another terminal/editor:
# Edit backends/dxr/render_dxr.hlsl
# Change Miss shader background color: float3(1, 0, 0) for red
# Save file

# Application should print:
# [Slang] Shader modified, reloading...
# [Slang] Hot-reload successful!

# Background should turn red WITHOUT restarting!
```
What common issues might I encounter?
```

### Expected Output

- Test procedure
- Success criteria
- Visual comparison method
- Performance baseline
- Common issues and fixes

### Validation Checklist

- [ ] **Builds successfully:** No compilation or linking errors

- [ ] **Runs without crashing:** Application launches and initializes

- [ ] **Shaders compile:** All "Compiled X via Slang" logs appear

- [ ] **Scene renders:** Visual output appears (even if not perfect)

- [ ] **Output matches:** Slang build looks same as non-Slang build

- [ ] **No validation errors:** D3D12 debug layer clean (if enabled)

- [ ] **Performance acceptable:** Frame time within 10% of non-Slang

- [ ] **DXIL valid:** PIX shows valid shader bytecode

---

## Common Issues & Solutions

### Issue: "Slang compilation failed: Entry point not found"

**Symptom:** Shader compilation fails with entry point error

**Solution:**
```cpp
// Check entry point name matches exactly
// In shader:
[shader("raygeneration")]
void RayGenMain() { ... }

// In C++:
compileHLSLToDXIL(source, "RayGenMain", ShaderStage::RayGen);
//                        ^^^^^^^^^^^ Must match
```

### Issue: "D3D12 ERROR: DXIL verification failed"

**Symptom:** Shader compiles but D3D12 rejects it

**Solutions:**
1. Check shader model version:
   ```cpp
   // In slang_shader_compiler.cpp
   targetDesc.profile = globalSession->findProfile("sm_6_5");
   // Try sm_6_5 instead of sm_6_6 for broader compatibility
   ```

2. Compare DXIL with DXC output:
   ```powershell
   # Save both to files, compare sizes
   # They should be similar
   ```

3. Use dxil-dis to disassemble and inspect

### Issue: "Bytecode size is 0"

**Symptom:** Compilation succeeds but bytecode empty

**Solution:**
```cpp
// Add check after compilation:
if (result->bytecode.empty()) {
    throw std::runtime_error("Shader compiled but bytecode is empty!");
}
```

### Issue: "Scene renders differently"

**Symptom:** Visual output doesn't match non-Slang build

**Solutions:**
1. Check all shaders compiled (not just some)
2. Verify shader binding table (SBT) entries correct
3. Use PIX to compare shader execution
4. Check for precision differences (shouldn't happen, but possible)

### Issue: "Crashes in Slang initialization"

**Symptom:** Application crashes when creating SlangShaderCompiler

**Solutions:**
1. Check slang.dll is in same directory as .exe
2. Verify Slang version matches (Debug vs Release)
3. Check slangCompiler.isValid() before use
4. Try creating global session manually first (debug)

---

## Next Steps

After DXR integration complete:

✅ **Phase 1, Part 2 done** - DXR backend using Slang for HLSL→DXIL

**Next:** Phase 1, Part 3 - Vulkan Backend Integration
- Open: `Migration/prompts/03-vulkan-integration.md`
- Similar process, but GLSL→SPIRV instead of HLSL→DXIL

**OR:** If DXR is sufficient for now, skip to:
- Phase 2: First native Slang shader (`04-first-slang-shader.md`)

---

**Estimated time for DXR integration:** 4-6 hours

**Success indicator:** `.\chameleonrt.exe dxr test_cube.obj` renders correctly with Slang-compiled shaders
