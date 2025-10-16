# Task 0.3: Reflection Extraction - Detailed Proposal

## Overview
Implement basic reflection extraction to capture shader parameter bindings (registers, descriptor sets, etc.) from compiled Slang programs.

## Reference Material

### Slang Documentation
- **User Guide:** `slang/docs/user-guide/09-reflection.md`
- **Example Code:** `slang/examples/reflection-api/main.cpp`
- **API Header:** `slang/include/slang.h`

### Key Slang Types
```cpp
// From slang.h (C++ wrapper interface)
struct ShaderReflection {           // aka ProgramLayout
    unsigned getParameterCount();
    VariableLayoutReflection* getParameterByIndex(unsigned index);
    EntryPointReflection* getEntryPointByIndex(SlangUInt index);
};

struct VariableLayoutReflection {
    const char* getName();
    TypeLayoutReflection* getTypeLayout();
    
    // Binding information
    unsigned getBindingIndex();      // t10, b0, u5, etc.
    unsigned getBindingSpace();      // space0, space1, etc.
    
    // Multi-dimensional offsets (bytes, registers, etc.)
    size_t getOffset(ParameterCategory category);
    size_t getBindingSpace(ParameterCategory category);
    
    // Enumerate used categories
    unsigned getCategoryCount();
    ParameterCategory getCategoryByIndex(unsigned index);
};

struct TypeLayoutReflection {
    TypeReflection::Kind getKind();  // Struct, Array, Resource, etc.
    const char* getName();
    
    // For struct types
    unsigned getFieldCount();
    VariableLayoutReflection* getFieldByIndex(unsigned index);
};

enum class ParameterCategory {
    Uniform,            // cbuffer data
    ConstantBuffer,     // b registers
    ShaderResource,     // t registers (SRV)
    UnorderedAccess,    // u registers (UAV)
    SamplerState,       // s registers
    // ... more
};
```

## Implementation Strategy

### Phase A: Basic Parameter Extraction (Minimal)
**Goal:** Extract top-level shader parameters with their bindings

```cpp
bool SlangShaderCompiler::extractReflection(
    slang::IComponentType* program, 
    ShaderBlob& blob
) {
    // Get reflection interface
    slang::ProgramLayout* layout = program->getLayout();
    if (!layout) {
        lastError = "No layout available for reflection";
        return false;
    }
    
    // Iterate global parameters
    unsigned paramCount = layout->getParameterCount();
    
    for (unsigned i = 0; i < paramCount; ++i) {
        slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
        
        // Get basic info
        const char* name = param->getName();
        if (!name) name = "<unnamed>";
        
        // Get binding for common categories
        unsigned binding = param->getBindingIndex();
        unsigned space = param->getBindingSpace();
        
        // Store in blob
        ShaderBlob::Binding bindingInfo;
        bindingInfo.name = name;
        bindingInfo.binding = binding;
        bindingInfo.set = space;
        bindingInfo.count = 1;  // TODO: handle arrays
        
        blob.bindings.push_back(bindingInfo);
    }
    
    return true;
}
```

### Phase B: Category-Aware Extraction (Better)
**Goal:** Distinguish between different register types (t, b, u, s)

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
    
    unsigned paramCount = layout->getParameterCount();
    
    for (unsigned i = 0; i < paramCount; ++i) {
        slang::VariableLayoutReflection* param = layout->getParameterByIndex(i);
        
        const char* name = param->getName();
        if (!name) name = "<unnamed>";
        
        // Iterate through all categories this parameter uses
        unsigned categoryCount = param->getCategoryCount();
        
        for (unsigned c = 0; c < categoryCount; ++c) {
            slang::ParameterCategory category = param->getCategoryByIndex(c);
            
            // Only extract binding-relevant categories
            switch (category) {
                case slang::ParameterCategory::ConstantBuffer:
                case slang::ParameterCategory::ShaderResource:
                case slang::ParameterCategory::UnorderedAccess:
                case slang::ParameterCategory::SamplerState:
                {
                    size_t binding = param->getOffset(category);
                    size_t space = param->getBindingSpace(category);
                    
                    ShaderBlob::Binding bindingInfo;
                    bindingInfo.name = name;
                    bindingInfo.binding = (uint32_t)binding;
                    bindingInfo.set = (uint32_t)space;
                    bindingInfo.count = 1;
                    bindingInfo.category = categoryToString(category);  // NEW FIELD
                    
                    blob.bindings.push_back(bindingInfo);
                    break;
                }
                default:
                    // Skip non-binding categories (like Uniform for byte offsets)
                    break;
            }
        }
    }
    
    return true;
}

const char* categoryToString(slang::ParameterCategory category) {
    switch (category) {
        case slang::ParameterCategory::ConstantBuffer: return "b";
        case slang::ParameterCategory::ShaderResource: return "t";
        case slang::ParameterCategory::UnorderedAccess: return "u";
        case slang::ParameterCategory::SamplerState: return "s";
        default: return "unknown";
    }
}
```

### Phase C: Recursive Struct Fields (Optional - Phase 1+)
**Goal:** Extract nested struct fields for detailed analysis

```cpp
// For Phase 1 and beyond - NOT implementing now
void extractTypeLayoutRecursive(
    slang::TypeLayoutReflection* typeLayout,
    const std::string& parentName,
    std::vector<ShaderBlob::Binding>& bindings
) {
    if (typeLayout->getKind() == slang::TypeReflection::Kind::Struct) {
        unsigned fieldCount = typeLayout->getFieldCount();
        for (unsigned i = 0; i < fieldCount; ++i) {
            auto field = typeLayout->getFieldByIndex(i);
            std::string fieldName = parentName + "." + field->getName();
            
            // Recurse or extract binding
            extractTypeLayoutRecursive(field->getTypeLayout(), fieldName, bindings);
        }
    }
    // ... handle arrays, resources, etc.
}
```

## Updated ShaderBlob Structure

### Current Definition
```cpp
struct ShaderBlob {
    std::vector<uint8_t> bytecode;
    std::string entryPoint;
    ShaderStage stage;
    ShaderTarget target;
    
    struct Binding {
        std::string name;
        uint32_t binding;
        uint32_t set;
        uint32_t count;
    };
    std::vector<Binding> bindings;
};
```

### Proposed Enhancement (Optional)
```cpp
struct Binding {
    std::string name;
    uint32_t binding;       // Register index (e.g., 10 for t10)
    uint32_t set;           // Space/set (e.g., 0 for space0)
    uint32_t count;         // Array size (1 for non-arrays)
    std::string category;   // "t", "b", "u", "s" (OPTIONAL - for debugging)
};
```

## Example Usage

### Test Shader (HLSL)
```hlsl
// test_reflection.hlsl
StructuredBuffer<float3> vertices : register(t10, space0);
Texture2D<float4> albedoMap : register(t11, space0);
SamplerState texSampler : register(s0, space0);

cbuffer Camera : register(b0, space0) {
    float4x4 viewProj;
    float3 cameraPos;
};

RWStructuredBuffer<float4> output : register(u0, space1);

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    // ... shader code
}
```

### Expected Reflection Output
```cpp
auto blob = compiler.compileHLSLToDXIL(source, "main", ShaderStage::Compute);

// blob->bindings should contain:
// [0] name="vertices",   binding=10, set=0, category="t"
// [1] name="albedoMap",  binding=11, set=0, category="t"
// [2] name="texSampler", binding=0,  set=0, category="s"
// [3] name="Camera",     binding=0,  set=0, category="b"
// [4] name="output",     binding=0,  set=1, category="u"
```

### Validation Code
```cpp
// In test_slang_compiler.cpp
void test_reflection() {
    std::cout << "\n=== Test: Reflection ===\n";
    
    SlangShaderCompiler compiler;
    assert(compiler.isValid());
    
    std::string shader = R"(
        StructuredBuffer<float3> vertices : register(t10, space0);
        cbuffer Camera : register(b0, space0) {
            float4x4 viewProj;
        };
        
        [shader("compute")]
        [numthreads(64, 1, 1)]
        void main(uint3 dtid : SV_DispatchThreadID) { }
    )";
    
    auto blob = compiler.compileHLSLToDXIL(
        shader, "main", ShaderStage::Compute
    );
    
    assert(blob.has_value());
    assert(blob->bindings.size() == 2);
    
    // Find vertices binding
    auto it = std::find_if(blob->bindings.begin(), blob->bindings.end(),
        [](const auto& b) { return b.name == "vertices"; });
    
    assert(it != blob->bindings.end());
    assert(it->binding == 10);
    assert(it->set == 0);
    
    // Find Camera binding
    it = std::find_if(blob->bindings.begin(), blob->bindings.end(),
        [](const auto& b) { return b.name == "Camera"; });
    
    assert(it != blob->bindings.end());
    assert(it->binding == 0);
    assert(it->set == 0);
    
    std::cout << "✅ Reflection extracted " << blob->bindings.size() << " bindings\n";
    for (const auto& b : blob->bindings) {
        std::cout << "  - " << b.name 
                  << " @ binding=" << b.binding 
                  << ", set=" << b.set << "\n";
    }
    
    std::cout << "PASS\n";
}
```

## Recommendation: Which Phase to Implement?

### **✅ Recommend: Phase B (Category-Aware)**

**Rationale:**
1. **Minimal vs Phase A:** Phase A only gets binding/space, missing which register type (t vs b vs u vs s)
2. **Practical utility:** Knowing "t10" vs "b10" is essential for debugging D3D12/Vulkan descriptor layouts
3. **Low complexity:** ~50 lines of additional code over Phase A
4. **Future-proof:** Sets up proper categorization for Phase 1 work
5. **No overhead:** Information is already in Slang's reflection, just need to query it

### Skip Phase C for now
- **Recursive struct fields** are useful for detailed introspection
- NOT needed for ChameleonRT's current use case
- Can add in Phase 1+ if needed for descriptor set layout generation

## Implementation Checklist

- [ ] Add `category` field to `ShaderBlob::Binding` (optional but recommended)
- [ ] Implement `extractReflection()` using Phase B approach
- [ ] Add `categoryToString()` helper function
- [ ] Handle edge cases (unnamed parameters, no layout, etc.)
- [ ] Add debug output (optional, controlled by flag)
- [ ] Write test case in `test_slang_compiler.cpp`
- [ ] Validate against HLSL shader with multiple binding types
- [ ] Document in `RESULTS.md`

## Expected Output Example

```
[Slang] Reflection for 'main':
  Parameters: 4
  [0] vertices @ binding=10, space=0, category=t (ShaderResource)
  [1] Camera @ binding=0, space=0, category=b (ConstantBuffer)
  [2] albedoMap @ binding=11, space=0, category=t (ShaderResource)
  [3] output @ binding=0, space=1, category=u (UnorderedAccess)
```

## Time Estimate

- **Phase A (Basic):** 30 minutes
- **Phase B (Category-Aware):** 45 minutes ⭐ **RECOMMENDED**
- **Phase C (Recursive):** 2+ hours (defer to later phase)

## Next Steps

1. Review this proposal
2. Confirm Phase B is the right approach
3. Implement `extractReflection()` with Phase B strategy
4. Add test case
5. Validate with real DXR/Vulkan shader
