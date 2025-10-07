# DXR Backend Integration Prompts - Phase 1, Part 2

## Overview

These prompts guide you through integrating SlangShaderCompiler into the DXR (DirectX Raytracing) backend.

**Phase:** 1 (Pass-Through Compilation)  
**Part:** 2 (DXR Backend)  
**Duration:** 4-6 hours  
**Reference:** `Migration/INTEGRATION_GUIDE.md` Parts 2-3

**Goal:** Replace DXC shader compilation with Slang's `compileHLSLToDXIL()` while keeping existing HLSL shaders unchanged.

---

## Prompt 1: Find DXR Backend Main Files

### Context Setup
**Files to open:**
1. `backends/dxr/` directory (file explorer)
2. `Migration/INTEGRATION_GUIDE.md`

### Prompt

```
Task: Identify main DXR backend files for shader compilation integration

Context:
- Phase 1, Part 2 of Slang integration
- Need to find where DXR backend compiles shaders
- Looking for DXC compilation calls
- Main backend class definition

Please help me find:

1. Main backend class file (likely render_dxr.h or similar)
   - Search for: class definitions, main rendering interface
   
2. Shader compilation location (likely render_dxr.cpp or shader_compilation.cpp)
   - Search for: "DXC", "IDxcCompiler", "DxcCreateInstance", "CompileShader"
   - Or: Functions that take HLSL source and produce DXIL
   
3. Where shaders are loaded/used:
   - Ray generation shader
   - Miss shader
   - Closest hit shader
   - Any hit shader (if present)

Please:
- List the main files in backends/dxr/
- Grep for DXC-related terms
- Show where shader compilation currently happens
- Identify the backend class name

This will help me target the right integration points.
```

### Expected Output

- List of key DXR backend files
- Location of shader compilation code
- Backend class name
- Current DXC usage patterns

### Next Step

Once files identified, use that info for Prompt 2.

---

## Prompt 2: Add SlangShaderCompiler to DXR Backend Class

### Context Setup
**Files to open:**
1. Main DXR backend file (from Prompt 1 results)
2. `Migration/util/slang_shader_compiler.h`
3. `Migration/reference/integration_notes.txt`

### Prompt

```
Task: Add SlangShaderCompiler member to DXR backend class

Context:
- Phase 1, Part 2, Step 2.1 of Migration/INTEGRATION_GUIDE.md
- Integrating into DXR backend class: [CLASS_NAME from Prompt 1]
- File: [FILENAME from Prompt 1]
- Need conditional compilation (only when USE_SLANG_COMPILER defined)

Current backend class structure:
[Paste the class definition, or tell me to find it]

Requirements:

1. Add include at top of file:
```cpp
#ifdef USE_SLANG_COMPILER
#include "slang_shader_compiler.h"
#endif
```

2. Add member variable to class (private section):
```cpp
#ifdef USE_SLANG_COMPILER
    chameleonrt::SlangShaderCompiler slangCompiler;
#endif
```

3. In constructor or init() method, verify initialization:
   - Check slangCompiler.isValid() after construction
   - Log error if initialization fails
   - Maybe throw or return error code

4. Follow pattern from Migration/reference/integration_notes.txt "Dual Compilation During Migration"

Please show:
- Where to add #include (use replace_string_in_file with context lines)
- Where to add member variable (with context)
- How to verify initialization (add to constructor/init)
- Complete error handling

Backend class file: [FILENAME]
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

## Prompt 3: Update DXR CMakeLists.txt to Link Slang Utility

### Context Setup
**Files to open:**
1. `backends/dxr/CMakeLists.txt`
2. `Migration/reference/slang_CMakeLists.txt`

### Prompt

```
Task: Link slang_compiler_util library to DXR backend

Context:
- Phase 1, Part 2, Step 2.2
- DXR backend now has SlangShaderCompiler member
- Need to link against slang_compiler_util library
- Should only link when ENABLE_SLANG or ENABLE_DXR_SLANG is ON

Current file: backends/dxr/CMakeLists.txt

Requirements:

1. After existing target_link_libraries(crt_dxr ...), add:
```cmake
# Add Slang compiler utility if enabled
if(ENABLE_SLANG OR ENABLE_DXR_SLANG)
    target_link_libraries(crt_dxr PRIVATE slang_compiler_util)
    target_compile_definitions(crt_dxr PRIVATE USE_SLANG_COMPILER=1)
endif()
```

2. Keep existing DXC linking (for fallback/comparison)

3. Verify link order:
   - Utility libraries first (util, display, etc.)
   - Then slang_compiler_util
   - Then platform libs (d3d12.lib, etc.)

Pattern reference: Migration/reference/slang_CMakeLists.txt (lines about linking)

Please show:
- Where to add the if(ENABLE_SLANG) block
- Use replace_string_in_file with context
- Verify no duplicate definitions
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

## Prompt 4: Identify Shader Compilation Functions

### Context Setup
**Files to open:**
1. DXR backend shader compilation file (from Prompt 1)
2. Search results for "DXC" or "CompileShader"

### Prompt

```
Task: Find all shader compilation call sites in DXR backend

Context:
- Phase 1, Part 2, Step 3.2
- Need to replace DXC calls with Slang calls
- First step: identify all compilation sites

Please search backends/dxr/ for:

1. Functions that compile shaders:
   - Look for: "IDxcCompiler", "DxcCompile", "CompileShader", etc.
   - Show me the function signatures

2. Where these functions are called:
   - Ray generation shader compilation
   - Miss shader compilation
   - Closest hit shader compilation
   - Any hit shader compilation (if present)

3. Current compilation pattern:
   - What inputs do compile functions take?
   - What outputs do they produce?
   - How is DXIL bytecode extracted?
   - How is it used in pipeline creation?

Please show:
- All shader compilation function definitions
- All call sites with surrounding code (5-10 lines context)
- Data types used (IDxcBlob? std::vector<uint8_t>?)

This will help me create the replacement pattern.
```

### Expected Output

- List of shader compilation functions
- Call sites for each shader type
- Current data flow pattern
- Input/output types

### Next Step

Use this info to create replacement code in Prompt 5.

---

## Prompt 5: Replace First Shader Compilation (Ray Generation)

### Context Setup
**Files to open:**
1. DXR shader compilation file
2. `Migration/util/slang_shader_compiler.h` (API reference)
3. `Migration/INTEGRATION_GUIDE.md` Part 3.3

### Prompt

```
Task: Replace DXC compilation with Slang for ray generation shader

Context:
- Phase 1, Part 2, Step 3.3
- Starting with ray generation shader (simplest test case)
- Using dual compilation (keep both paths)
- Pattern from Migration/INTEGRATION_GUIDE.md "Replace Shader Compilation"

Current ray generation shader compilation code:
[Paste the current DXC compilation function/code from Prompt 4]

Requirements:

1. Wrap existing code in #else block:
```cpp
std::vector<uint8_t> CompileRayGenShader(const std::string& hlslSource, const char* entryPoint) {
#ifdef USE_SLANG_COMPILER
    // Slang path (new)
    using namespace chameleonrt;
    auto result = slangCompiler.compileHLSLToDXIL(
        hlslSource,
        entryPoint,
        ShaderStage::RayGen
    );
    
    if (!result) {
        std::string error = "Slang ray gen compilation failed: " + slangCompiler.getLastError();
        std::cerr << error << std::endl;
        throw std::runtime_error(error);
        // OR: fall back to DXC (remove throw, proceed to #else)
    }
    
    return result->bytecode;
#else
    // Original DXC path (existing code)
    [EXISTING DXC CODE HERE]
#endif
}
```

2. Ensure bytecode format matches:
   - Both paths should return std::vector<uint8_t>
   - If current code returns IDxcBlob, extract bytes first
   
3. Add error handling:
   - Log error with getLastError()
   - Either throw or fallback to DXC (your choice for Phase 1)
   
4. Test compilation:
   - Add temporary log: "Compiled raygen via Slang: X bytes"

Please show:
- Complete function with #ifdef branches
- Proper error handling
- Type conversions if needed
- How to integrate with existing D3D12 pipeline creation code

File: [DXR shader compilation file]
Function: [name from Prompt 4]
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

## Prompt 6: Replace Remaining Shaders (Miss, Hit, etc.)

### Context Setup
**Files to open:**
1. DXR shader compilation file (with raygen already converted)
2. List of remaining shaders from Prompt 4

### Prompt

```
Task: Apply Slang compilation pattern to all remaining RT shaders

Context:
- Phase 1, Part 2, Step 3.3 (continued)
- Ray generation shader successfully converted
- Now converting: miss, closest hit, any hit shaders
- Same pattern for all

Remaining shaders to convert:
[List from Prompt 4: miss shader, closest hit, any hit, etc.]

For each shader compilation site:

1. Apply same #ifdef USE_SLANG_COMPILER pattern as raygen
2. Use appropriate ShaderStage enum:
   - ShaderStage::Miss for miss shader
   - ShaderStage::ClosestHit for closest hit
   - ShaderStage::AnyHit for any hit
3. Same error handling pattern
4. Same logging pattern

Please:
- Convert all shader compilation functions
- Use multi_replace_string_in_file for efficiency if multiple functions
- Keep consistent error handling
- Add logging to each: "Compiled [shader type] via Slang: X bytes"

Show me the changes for:
1. Miss shader compilation
2. Closest hit shader compilation
3. Any hit shader compilation (if present)

Use the raygen conversion as the template.
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

## Prompt 7: Test Complete DXR Integration

### Context Setup
**Files to open:**
1. Terminal/PowerShell
2. Test scene file (test_cube.obj or similar)

### Prompt

```
Task: Validate complete DXR backend with Slang shader compilation

Context:
- Phase 1, Part 2 complete (all steps done)
- All shaders compile through Slang
- Ready for end-to-end test

Test procedure:

1. Clean rebuild:
   cmake -B build_dxr_test --fresh -DENABLE_SLANG=ON -DENABLE_DXR_SLANG=ON -DSlang_ROOT=C:\dev\slang\build\Debug
   cmake --build build_dxr_test --config Debug

2. Run with test scene:
   cd build_dxr_test\Debug
   .\chameleonrt.exe dxr ..\..\..\test_cube.obj

3. What to check:
   - Application starts without crashing
   - All shaders compile (check logs)
   - Scene renders (visual output)
   - No D3D12 validation errors (if debug layer enabled)

4. Compare with non-Slang build:
   cmake -B build_dxr_noslang --fresh -DENABLE_DXR=ON
   cmake --build build_dxr_noslang --config Debug
   cd build_dxr_noslang\Debug
   .\chameleonrt.exe dxr ..\..\..\test_cube.obj
   
   - Visual output should be identical
   - Performance should be similar (compilation might be slightly different)

5. Use PIX (if available):
   - Capture frame
   - Inspect shader bytecode in pipeline state
   - Verify DXIL looks reasonable

Please guide me through validation and help diagnose any issues.

What specific success criteria should I check?
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
