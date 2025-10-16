# Slang Integration - Task-Specific Instructions

## IMPORTANT: Documentation Policy

**DO NOT create documentation files unless explicitly requested.**

When responding to user questions:
- ✅ Answer directly with code, explanations, or analysis
- ✅ Provide inline examples and snippets
- ✅ Reference existing documentation
- ❌ Do NOT create new `.md` files, `README.md`, `PLAN.md`, etc. unless the user specifically asks for them
- ❌ Do NOT create "helpful" planning documents, summaries, or guides unprompted

**The user will ask if they want documentation.** Focus on solving the immediate problem, not documenting everything.

## Current Mission

Integrate **Slang shader compiler** into ChameleonRT's direct API backends (DXR and Vulkan) **without using slang-gfx abstraction layer**.

## Context: Why This Migration

We previously experimented with slang-gfx (Slang's graphics API abstraction layer) but determined it was counterproductive for our learning and benchmarking goals. See `Migration/LEARNINGS.md` for full details.

**Decision:** Use Slang for shader compilation ONLY. Keep all pipeline creation, resource binding, and synchronization native to each API.

## Migration Resources

All migration resources are in the `Migration/` directory:

- **`Migration/README.md`** - Overview and quick start
- **`Migration/LEARNINGS.md`** - Why we pivoted from slang-gfx
- **`Migration/MIGRATION_PLAN.md`** - 6-week strategy with decision gates
- **`Migration/INTEGRATION_GUIDE.md`** - **PRIMARY REFERENCE** - Step-by-step instructions
- **`Migration/util/slang_shader_compiler.h`** - Main utility class to use
- **`Migration/util/slang_shader_compiler.cpp`** - Implementation
- **`Migration/reference/`** - Working CMake examples and code snippets
- **`Migration/prompts/`** - Detailed prompt templates for each phase

## Utility Class: SlangShaderCompiler

**Location:** `Migration/util/slang_shader_compiler.{h,cpp}`

**Purpose:** Unified interface for compiling shaders through Slang

**Key Methods:**

### Phase 1: Pass-Through Compilation
```cpp
// Compile existing HLSL to DXIL (for D3D12)
std::optional<ShaderBlob> compileHLSLToDXIL(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    const std::vector<std::string>& defines = {}
);

// Compile existing GLSL to SPIRV (for Vulkan)
std::optional<ShaderBlob> compileGLSLToSPIRV(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    const std::vector<std::string>& defines = {}
);
```

### Phase 2: Slang Language
```cpp
// Compile Slang source to DXIL
std::optional<ShaderBlob> compileSlangToDXIL(...);

// Compile Slang source to SPIRV
std::optional<ShaderBlob> compileSlangToSPIRV(...);

// Compile Slang source to Metal
std::optional<ShaderBlob> compileSlangToMetal(...);
```

### Error Handling
```cpp
const std::string& getLastError() const;
bool isValid() const;
```

### ShaderBlob Structure
```cpp
struct ShaderBlob {
    std::vector<uint8_t> bytecode;  // Compiled shader bytecode
    std::string entryPoint;
    ShaderStage stage;
    ShaderTarget target;
    // ... reflection data (future)
};
```

## Migration Strategy (Phases)

### Phase 1: Pass-Through Compilation (Weeks 1-2)
**Goal:** Prove Slang can compile existing HLSL/GLSL without breaking anything

**Tasks:**
1. Update root `CMakeLists.txt` - Add `ENABLE_SLANG` option
2. Integrate into DXR backend - Replace DXC with `compileHLSLToDXIL()`
3. Integrate into Vulkan backend - Replace glslang with `compileGLSLToSPIRV()`
4. Validate - Same visual output, no errors

**Success Criteria:**
- All existing shaders compile through Slang
- Rendering output identical
- Can build with/without Slang (dual paths)
- Decision gate: If this fails, reconsider strategy

### Phase 2: First Slang Shader (Week 3)
**Goal:** Write one shader in Slang language, prove cross-compilation works

**Tasks:**
1. Port simple shader (vertex+fragment) to `.slang` file
2. Compile to both DXIL and SPIRV from same source
3. Test on both D3D12 and Vulkan

**Success Criteria:**
- Single `.slang` source works on both APIs
- Developer experience is good
- Decision gate: If Slang language causes issues, keep pass-through only

### Phase 3: Ray Tracing Shaders (Weeks 4-6)
**Goal:** Migrate all RT shaders to Slang

**Order:** Miss → RayGen → Closest Hit → Any Hit

### Phase 4: Advanced Features (Optional, Week 7+)
- Slang interfaces for materials
- Parameter blocks
- Auto-differentiation (if relevant)

## Key Principles

### 1. Dual Compilation Paths
During migration, keep both compilation methods working:

```cpp
#ifdef USE_SLANG_COMPILER
    auto blob = slangCompiler.compileHLSLToDXIL(source, "main", stage);
    if (!blob) {
        // Log error, maybe fall back to DXC
    }
    bytecode = blob->bytecode;
#else
    // Original DXC/glslang path
    bytecode = compileWithDXC(source, "main");
#endif
```

**Why:** Safety net, easy rollback, can compare outputs

### 2. CMake Options
```cmake
option(ENABLE_SLANG "Use Slang for shader compilation" OFF)

cmake_dependent_option(ENABLE_DXR_SLANG 
    "Build DXR with Slang" 
    OFF 
    "ENABLE_SLANG;ENABLE_DXR" 
    OFF
)
```

**Why:** Gradual adoption, per-backend control

### 3. Error Handling Pattern
```cpp
auto result = slangCompiler.compileHLSLToDXIL(source, entry, stage);
if (!result) {
    std::cerr << "Slang compilation failed: " 
              << slangCompiler.getLastError() << std::endl;
    // Handle error (throw, fallback, etc.)
}
// Use result->bytecode
```

### 4. DLL Deployment
Slang requires `slang.dll` (Windows) at runtime:

```cmake
if(WIN32 AND ENABLE_SLANG)
    add_custom_command(TARGET chameleonrt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${Slang_ROOT}/bin/slang.dll"
            "$<TARGET_FILE_DIR:chameleonrt>"
    )
endif()
```

## What NOT to Do

❌ **Don't use slang-gfx:**
```cpp
// NO - Don't do this:
#include <slang-gfx.h>
gfx::IDevice* device = gfxCreateDevice(...);
```

❌ **Don't abstract APIs:**
```cpp
// NO - Keep native API code:
// Don't replace D3D12CreateDevice with slang-gfx wrapper
```

❌ **Don't break existing builds:**
```cpp
// NO - Always keep #ifdef guards during migration:
auto shader = slangCompiler.compile(...); // What if ENABLE_SLANG=OFF?
```

## What TO Do

✅ **Use SlangShaderCompiler utility:**
```cpp
#ifdef USE_SLANG_COMPILER
#include "slang_shader_compiler.h"
#endif

class RenderDXR {
#ifdef USE_SLANG_COMPILER
    chameleonrt::SlangShaderCompiler slangCompiler;
#endif
};
```

✅ **Follow Migration Guide:**
- Reference `Migration/INTEGRATION_GUIDE.md` for each step
- Check `Migration/reference/integration_notes.txt` for gotchas
- Use prompt templates from `Migration/prompts/`

✅ **Validate Incrementally:**
- Build after each change
- Test rendering output
- Check validation layers (D3D12 debug, Vulkan validation)

## Common Patterns

### Pattern 1: Adding to Backend Class
```cpp
// In backend header or source:
#ifdef USE_SLANG_COMPILER
#include "slang_shader_compiler.h"
#endif

class MyBackend {
private:
#ifdef USE_SLANG_COMPILER
    chameleonrt::SlangShaderCompiler slangCompiler;
#endif
};
```

### Pattern 2: Shader Compilation Function
```cpp
std::vector<uint8_t> CompileShader(const std::string& source, 
                                    const char* entry,
                                    ShaderStage stage) {
#ifdef USE_SLANG_COMPILER
    auto result = slangCompiler.compileHLSLToDXIL(source, entry, stage);
    if (!result) {
        throw std::runtime_error("Slang: " + slangCompiler.getLastError());
    }
    return result->bytecode;
#else
    return CompileWithDXC(source, entry); // Original path
#endif
}
```

### Pattern 3: CMake Backend Integration
```cmake
# In backends/dxr/CMakeLists.txt:
if(ENABLE_SLANG OR ENABLE_DXR_SLANG)
    target_link_libraries(crt_dxr PRIVATE slang_compiler_util)
    target_compile_definitions(crt_dxr PRIVATE USE_SLANG_COMPILER=1)
endif()
```

## Decision Gates

### Gate 1: After Week 2 (Pass-Through Working?)
**Question:** Can Slang compile all existing shaders without breaking anything?
- ✅ Yes → Proceed to Phase 2
- ❌ No → Investigate issues, consider timeline adjustment

### Gate 2: After Week 3 (Slang Language Viable?)
**Question:** Does writing shaders in `.slang` provide value?
- ✅ Yes → Continue to RT shader migration
- ❌ No → Keep pass-through only, abandon native Slang language

### Gate 3: After Week 6 (Complete Migration?)
**Question:** Are all shaders ported, performance acceptable?
- ✅ Yes → Polish and optimize
- ❌ No → Assess blockers, adjust plan

## File References

When working on integration, reference these files:

**For CMake setup:**
- `Migration/INTEGRATION_GUIDE.md` Part 1
- `Migration/reference/root_CMakeLists.txt`
- `Migration/reference/backends_CMakeLists.txt`

**For backend integration:**
- `Migration/INTEGRATION_GUIDE.md` Parts 2-4
- `Migration/reference/integration_notes.txt`
- `Migration/util/slang_shader_compiler.h`

**For Slang language migration:**
- `Migration/MIGRATION_PLAN.md` Phase 2
- Slang documentation: https://shader-slang.com/

**For debugging:**
- `Migration/reference/integration_notes.txt` - Common errors
- `Migration/INTEGRATION_GUIDE.md` Troubleshooting section

## Current Branch

**Working branch:** `feature/slang-integration`
- Based on clean `master`
- Contains `Migration/` directory
- Ready for integration work

**Archive branch:** `SlangBackend`
- Contains slang-gfx experiment
- Reference only, not for active work

## When Generating Code

**Always:**
1. Check if `USE_SLANG_COMPILER` should be defined
2. Include proper error handling (`getLastError()`)
3. Use complete code (no `...existing code...` placeholders)
4. Follow existing code style in the file
5. Consider both Debug and Release builds
6. Think about DLL deployment needs

**Never:**
1. Break builds when `ENABLE_SLANG=OFF`
2. Suggest slang-gfx usage
3. Abstract away native API calls
4. Skip validation steps

## Success Indicators

✅ **CMake configuration succeeds** with and without `ENABLE_SLANG`  
✅ **Builds without warnings** in both modes  
✅ **Shaders compile** through Slang (pass-through or native)  
✅ **Rendering output matches** previous implementation  
✅ **No validation errors** (PIX for D3D12, RenderDoc for Vulkan)  
✅ **Performance acceptable** (within 5% of baseline)  
✅ **Error messages informative** when compilation fails  

---

**Remember:** This is incremental migration with clear rollback points. Validate at each step before proceeding. The goal is to enable learning about ray tracing APIs while using Slang for cleaner shader authoring.
