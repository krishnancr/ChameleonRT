# Phase 0: Utility Upgrade - Detailed Plan

**Status:** 📋 Planning  
**Duration:** 2-3 hours  
**Risk Level:** LOW

---

## Objectives

Enhance `SlangShaderCompiler` utility class with features needed for shader development phases.

---

## Prerequisites

- ✅ Existing `SlangShaderCompiler` class (validated for HLSL→DXIL)
- ✅ Slang SDK installed and linked
- ✅ CMake build system configured

---

## Tasks Breakdown

### Task 0.1: Add Search Paths to Single-Entry Functions
**Time:** 30 minutes  
**Difficulty:** Easy

#### Changes Required

**File:** `util/slang_shader_compiler.h`

Add `searchPaths` parameter to:
- `compileHLSLToDXIL()`
- `compileGLSLToSPIRV()`
- `compileSlangToDXIL()`
- `compileSlangToSPIRV()`
- `compileSlangToMetal()`

**File:** `util/slang_shader_compiler.cpp`

Update `compileInternal()` to:
1. Accept `searchPaths` parameter
2. Configure `SessionDesc` with search paths (like `compileHLSLToDXILLibrary` does)

#### Validation
```cpp
// Test with include directive
std::string shader = R"(
    #include "test.hlsl"
    [shader("compute")]
    void main() { }
)";

auto blob = compiler.compileHLSLToDXIL(
    shader, "main", ShaderStage::Compute,
    {"./test_shaders"}, // Search path
    {}
);
assert(blob.has_value());
```

---

### Task 0.2: Implement SPIRV Library Compilation
**Time:** 45 minutes  
**Difficulty:** Easy (clone existing DXR function)

#### Implementation Strategy

**File:** `util/slang_shader_compiler.h`

Add new function:
```cpp
std::optional<std::vector<ShaderBlob>> compileToSPIRVLibrary(
    const std::string& source,
    SlangSourceLanguage sourceLanguage,  // HLSL or Slang
    const std::vector<std::string>& searchPaths = {},
    const std::vector<std::string>& defines = {}
);
```

**File:** `util/slang_shader_compiler.cpp`

Clone `compileHLSLToDXILLibrary()` implementation:
1. Change `targetDesc.format = SLANG_SPIRV`
2. Change `targetDesc.profile = globalSession->findProfile("spirv_1_5")`
3. Same entry point extraction logic
4. Set `shaderBlob.target = ShaderTarget::SPIRV`

#### Validation
```cpp
auto blobs = compiler.compileToSPIRVLibrary(hlslSource, SLANG_SOURCE_LANGUAGE_HLSL);
assert(blobs.has_value());
assert(blobs->size() == 4); // RayGen, Miss, ClosestHit, ShadowMiss
for (auto& blob : *blobs) {
    assert(blob.target == ShaderTarget::SPIRV);
    assert(!blob.bytecode.empty());
}
```

---

### Task 0.3: Implement Basic Reflection Extraction
**Time:** 60 minutes  
**Difficulty:** Medium

#### Implementation Strategy

**File:** `util/slang_shader_compiler.cpp`

Implement `extractReflection()` body:

```cpp
bool SlangShaderCompiler::extractReflection(
    slang::IComponentType* program, 
    ShaderBlob& blob
) {
    slang::ProgramLayout* layout = program->getLayout();
    if (!layout) {
        lastError = "No layout available for reflection";
        return false;
    }
    
    SlangInt paramCount = layout->getParameterCount();
    
    std::cout << "[Slang] Reflection for '" << blob.entryPoint << "':\n";
    std::cout << "  Parameters: " << paramCount << "\n";
    
    for (SlangInt i = 0; i < paramCount; ++i) {
        slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
        
        const char* name = param->getName();
        if (!name) name = "<unnamed>";
        
        SlangInt space = param->getBindingSpace();
        SlangInt binding = param->getBindingIndex();
        
        std::cout << "  [" << i << "] " << name 
                  << " @ binding=" << binding 
                  << ", space=" << space << "\n";
        
        // Populate blob.bindings
        ShaderBlob::Binding bindingInfo;
        bindingInfo.name = name;
        bindingInfo.binding = (uint32_t)binding;
        bindingInfo.set = (uint32_t)space;
        bindingInfo.count = 1; // TODO: detect array size
        blob.bindings.push_back(bindingInfo);
    }
    
    return true;
}
```

#### Validation
```cpp
// Shader with known bindings
std::string shader = R"(
    StructuredBuffer<float3> vertices : register(t10, space0);
    cbuffer Camera : register(b0, space0) {
        float4x4 viewProj;
    };
    
    [shader("compute")]
    void main() { }
)";

auto blob = compiler.compileHLSLToDXIL(shader, "main", ShaderStage::Compute);
assert(blob->bindings.size() == 2);

// Find vertices binding
auto it = std::find_if(blob->bindings.begin(), blob->bindings.end(),
    [](const auto& b) { return b.name == "vertices"; });
assert(it != blob->bindings.end());
assert(it->binding == 10);
assert(it->set == 0);
```

---

### Task 0.4: Create Test Harness
**Time:** 30 minutes  
**Difficulty:** Easy

#### Test Program Structure

**File:** `util/test_slang_compiler.cpp`

```cpp
#include "slang_shader_compiler.h"
#include <iostream>
#include <cassert>

using namespace chameleonrt;

void test_search_paths() {
    std::cout << "\n=== Test: Search Paths ===\n";
    // TODO: Implement test
    std::cout << "PASS\n";
}

void test_spirv_library() {
    std::cout << "\n=== Test: SPIRV Library ===\n";
    // TODO: Implement test
    std::cout << "PASS\n";
}

void test_reflection() {
    std::cout << "\n=== Test: Reflection ===\n";
    // TODO: Implement test
    std::cout << "PASS\n";
}

int main() {
    std::cout << "SlangShaderCompiler Test Suite\n";
    std::cout << "==============================\n";
    
    test_search_paths();
    test_spirv_library();
    test_reflection();
    
    std::cout << "\nAll tests passed! ✅\n";
    return 0;
}
```

**File:** `util/CMakeLists.txt`

Add test executable:
```cmake
# Test executable (optional)
if(ENABLE_SLANG)
    add_executable(test_slang_compiler test_slang_compiler.cpp slang_shader_compiler.cpp)
    target_link_libraries(test_slang_compiler PRIVATE slang::slang)
    target_include_directories(test_slang_compiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
```

---

## Deliverables Checklist

- [ ] `util/slang_shader_compiler.h` - Updated signatures
- [ ] `util/slang_shader_compiler.cpp` - Implementations
- [ ] `util/test_slang_compiler.cpp` - Test harness
- [ ] `util/CMakeLists.txt` - Test build config
- [ ] `Phase0_UtilityUpgrade/RESULTS.md` - Test output and validation

---

## Validation Criteria

### Functional Tests
- ✅ Compile shader with `#include` directive using search paths
- ✅ Compile multi-entry-point shader to SPIRV library
- ✅ Extract and verify reflection data (bindings, spaces)
- ✅ All tests pass in `test_slang_compiler`

### Code Quality
- ✅ No compiler warnings
- ✅ Consistent with existing code style
- ✅ Error handling for edge cases (no layout, invalid paths, etc.)
- ✅ Memory safe (no leaks, use ComPtr correctly)

### Documentation
- ✅ Function comments updated
- ✅ Test output documented in RESULTS.md

---

## Known Issues / Risks

### Risk 1: Search Path Handling
**Issue:** Slang requires paths to use specific separators  
**Mitigation:** Test on Windows with backslashes and forward slashes

### Risk 2: Reflection API Changes
**Issue:** Slang reflection API might differ between versions  
**Mitigation:** Check Slang version, add version guards if needed

### Risk 3: SPIRV Profile
**Issue:** Wrong profile (spirv_1_5 vs spirv_1_6) might cause issues  
**Mitigation:** Start with spirv_1_5 (widely supported), upgrade if needed

---

## Integration Points

### With Phase 1
Phase 1 will use:
- `compileToSPIRVLibrary()` for Vulkan RT shaders
- `searchPaths` for `#include "util.hlsl"`
- Reflection data to verify bindings match expectations

### With Existing Code
All changes are **additive** - existing functionality unchanged:
- `compileHLSLToDXIL()` still works without search paths (default empty)
- `compileHLSLToDXILLibrary()` unchanged
- Backward compatible

---

## Completion Criteria

Phase 0 is **COMPLETE** when:

1. All tests pass
2. Reflection output shows correct bindings for test shader
3. SPIRV library compilation produces 4 entry point blobs
4. No regressions in existing HLSL→DXIL path
5. Code reviewed and committed to feature branch

**Estimated Time to Complete:** 2-3 hours

**Next Phase:** Phase 1 (Flat Shading)
