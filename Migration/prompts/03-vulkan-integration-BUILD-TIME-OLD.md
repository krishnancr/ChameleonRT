# Vulkan Backend Integration Prompts - Phase 1, Part 3

## Overview

These prompts guide you through integrating SlangShaderCompiler into the Vulkan backend.

**Phase:** 1 (Pass-Through Compilation)  
**Part:** 3 (Vulkan Backend)  
**Duration:** 4-6 hours  
**Reference:** `Migration/INTEGRATION_GUIDE.md` Part 4

**Goal:** Replace glslang shader compilation with Slang's `compileGLSLToSPIRV()` while keeping existing GLSL shaders unchanged.

**Note:** Very similar to DXR integration (02-dxr-integration.md), but GLSL→SPIRV instead of HLSL→DXIL.

---

## Prompt 1: Find Vulkan Backend Shader Compilation

### Prompt

```
Task: Identify Vulkan backend shader compilation locations

Context:
- Phase 1, Part 3 - Vulkan backend integration
- Need to find where GLSL is compiled to SPIRV
- Looking for glslang usage

Please search backends/vulkan/ for:

1. Shader compilation functions:
   - Look for: "glslang", "shaderc", "CompileGLSL", "SPIRV", "spv"
   - Show function signatures

2. Main backend class:
   - Vulkan render class name
   - Where shaders are compiled and loaded

3. Shader types compiled:
   - Ray generation, miss, closest hit, any hit
   - Or vertex/fragment if rasterization exists

Please show:
- Key Vulkan backend files
- Shader compilation function definitions
- Current GLSL→SPIRV pattern
- Data types used (std::vector<uint32_t>?)
```

---

## Prompt 2: Add SlangShaderCompiler to Vulkan Backend

### Prompt

```
Task: Add SlangShaderCompiler member to Vulkan backend class

Context:
- Phase 1, Part 3, Step 1
- Integrating into Vulkan backend class: [CLASS_NAME from Prompt 1]
- File: [FILENAME from Prompt 1]
- Same pattern as DXR integration

Requirements:

1. Add include with guard:
```cpp
#ifdef USE_SLANG_COMPILER
#include "slang_shader_compiler.h"
#endif
```

2. Add member variable:
```cpp
#ifdef USE_SLANG_COMPILER
    chameleonrt::SlangShaderCompiler slangCompiler;
#endif
```

3. Verify initialization in constructor/init:
   - Check isValid()
   - Log errors if initialization fails

4. Follow same pattern as DXR backend

Please show complete changes with context for replace_string_in_file.

File: [Vulkan backend file]
```

---

## Prompt 3: Update Vulkan CMakeLists.txt

### Prompt

```
Task: Link slang_compiler_util to Vulkan backend

Context:
- Phase 1, Part 3, Step 2
- backends/vulkan/CMakeLists.txt
- Same pattern as DXR

Requirements:

Add after existing target_link_libraries:
```cmake
# Add Slang compiler utility if enabled
if(ENABLE_SLANG OR ENABLE_VULKAN_SLANG)
    target_link_libraries(crt_vulkan PRIVATE slang_compiler_util)
    target_compile_definitions(crt_vulkan PRIVATE USE_SLANG_COMPILER=1)
endif()
```

Please show where to add with context.

File: backends/vulkan/CMakeLists.txt
```

---

## Prompt 4: Replace Vulkan Shader Compilation

### Prompt

```
Task: Replace glslang with Slang for GLSL→SPIRV compilation

Context:
- Phase 1, Part 3, Step 3
- Converting all Vulkan shaders to use compileGLSLToSPIRV()
- Dual compilation pattern (keep glslang fallback)

Current shader compilation code:
[Paste from Prompt 1]

Requirements:

1. For each shader compilation function:
```cpp
std::vector<uint32_t> CompileSPIRV(const std::string& glslSource, const char* entry, ShaderStage stage) {
#ifdef USE_SLANG_COMPILER
    using namespace chameleonrt;
    auto result = slangCompiler.compileGLSLToSPIRV(glslSource, entry, stage);
    
    if (!result) {
        std::string error = "Slang SPIRV compilation failed: " + slangCompiler.getLastError();
        std::cerr << error << std::endl;
        throw std::runtime_error(error);
    }
    
    // Convert uint8_t bytecode to uint32_t (SPIRV is uint32_t words)
    std::vector<uint32_t> spirv(result->bytecode.size() / 4);
    memcpy(spirv.data(), result->bytecode.data(), result->bytecode.size());
    
    std::cout << "Compiled " << entry << " via Slang: " << spirv.size() << " words" << std::endl;
    
    return spirv;
#else
    // Original glslang path
    [EXISTING CODE]
#endif
}
```

2. Convert all shaders (raygen, miss, hit, etc.)

3. Ensure bytecode format matches:
   - Vulkan expects std::vector<uint32_t>
   - Slang returns std::vector<uint8_t>
   - Convert: divide size by 4, memcpy

Please show conversion for all shader types.

Files: [Vulkan shader compilation files]
```

### Expected Output

- All Vulkan shaders using compileGLSLToSPIRV()
- Proper uint8_t → uint32_t conversion
- Error handling
- Logging

### Validation Checklist

- [ ] **Compiles:** Build crt_vulkan target

- [ ] **Shaders compile:** Check logs
  - "Compiled [shader] via Slang: N words"

- [ ] **SPIRV valid:** Run spirv-val on generated code (if possible)

- [ ] **Renders:** Run with Vulkan backend
  - `.\chameleonrt.exe vulkan test_cube.obj`
  - Check RenderDoc for shader modules

- [ ] **Validation layers happy:** No Vulkan validation errors

- [ ] **Matches non-Slang:** Visual output identical

---

## Prompt 5: Test Complete Vulkan Integration

### Prompt

```
Task: Validate complete Vulkan backend with Slang

Test procedure:

1. Build with Slang:
   cmake -B build_vk_test --fresh -DENABLE_SLANG=ON -DENABLE_VULKAN_SLANG=ON -DSlang_ROOT=C:\dev\slang\build\Debug
   cmake --build build_vk_test --config Debug

2. Run:
   cd build_vk_test\Debug
   .\chameleonrt.exe vulkan ..\..\..\test_cube.obj

3. Check:
   - All shaders compile via Slang
   - Scene renders correctly
   - No Vulkan validation errors
   - Performance acceptable

4. Compare with non-Slang build:
   - Visual output should match
   - SPIRV sizes should be similar

5. Use RenderDoc (if available):
   - Capture frame
   - Inspect shader modules
   - Verify SPIRV is valid

Please guide validation and help diagnose issues.
```

---

## Common Vulkan-Specific Issues

### Issue: "SPIRV validation failed"

**Solution:**
```cpp
// Check SPIRV target version in slang_shader_compiler.cpp:
targetDesc.profile = globalSession->findProfile("spirv_1_5");
// Try spirv_1_3 or spirv_1_0 if 1.5 causes issues
```

### Issue: "Bytecode size mismatch"

**Solution:**
```cpp
// Verify conversion:
if (result->bytecode.size() % 4 != 0) {
    throw std::runtime_error("SPIRV bytecode size not multiple of 4!");
}
std::vector<uint32_t> spirv(result->bytecode.size() / 4);
memcpy(spirv.data(), result->bytecode.data(), result->bytecode.size());
```

### Issue: "VkShaderModuleCreateInfo validation error"

**Solution:**
```cpp
// Verify createInfo setup:
VkShaderModuleCreateInfo createInfo = {};
createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
createInfo.codeSize = spirv.size() * sizeof(uint32_t);  // Bytes, not words!
createInfo.pCode = spirv.data();
```

---

## Next Steps

After Vulkan integration complete:

✅ **Phase 1, Part 3 done** - Vulkan backend using Slang for GLSL→SPIRV

✅ **Phase 1 complete** - Both DXR and Vulkan using Slang pass-through

**Next:** Phase 2 - First Native Slang Shader
- Open: `Migration/prompts/04-first-slang-shader.md`
- Write one shader in Slang language
- Compile to both DXIL and SPIRV

---

**Estimated time:** 4-6 hours  
**Success indicator:** `.\chameleonrt.exe vulkan test_cube.obj` renders with Slang-compiled GLSL
