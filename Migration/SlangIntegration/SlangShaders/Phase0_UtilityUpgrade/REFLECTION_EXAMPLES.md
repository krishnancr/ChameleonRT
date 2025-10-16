# Reflection Examples - Visual Reference

## Example 1: Simple DXR Ray Tracing Shader

### Input Shader (HLSL)
```hlsl
// dxr_rt.hlsl
RaytracingAccelerationStructure scene : register(t0, space0);
RWTexture2D<float4> output : register(u0, space0);

cbuffer SceneConstants : register(b0, space0) {
    float4x4 invViewProj;
    float3 cameraPos;
};

struct Payload {
    float3 color;
    uint depth;
};

[shader("raygeneration")]
void RayGen() {
    // ... ray gen code
}

[shader("miss")]
void Miss(inout Payload payload) {
    payload.color = float3(0.1, 0.2, 0.3);
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs) {
    payload.color = float3(1.0, 0.0, 0.0);
}
```

### Reflection Output (Phase B)

```
[Slang] Reflection for 'RayGen':
  Parameters: 3

  [0] scene
      Category: ShaderResource (t register)
      Binding:  t0
      Space:    space0
      Type:     RaytracingAccelerationStructure

  [1] output
      Category: UnorderedAccess (u register)
      Binding:  u0
      Space:    space0
      Type:     RWTexture2D<float4>

  [2] SceneConstants
      Category: ConstantBuffer (b register)
      Binding:  b0
      Space:    space0
      Type:     cbuffer
      Size:     80 bytes
```

### C++ Data Structure
```cpp
blob->bindings = {
    { name: "scene",           binding: 0, set: 0, category: "t" },
    { name: "output",          binding: 0, set: 0, category: "u" },
    { name: "SceneConstants",  binding: 0, set: 0, category: "b" }
};
```

---

## Example 2: Vulkan Ray Tracing Shader (Same Code, SPIRV Target)

### Reflection Output (Phase B)

```
[Slang] Reflection for 'RayGen':
  Parameters: 3

  [0] scene
      Category: ShaderResource
      Binding:  0
      Set:      0
      Type:     accelerationStructureEXT

  [1] output
      Category: UnorderedAccess (storage image)
      Binding:  1   // Note: Vulkan auto-assigns unique bindings
      Set:      0
      Type:     image2D

  [2] SceneConstants
      Category: ConstantBuffer (uniform buffer)
      Binding:  2
      Set:      0
      Type:     uniform block
      Size:     80 bytes
```

### Vulkan Descriptor Set Layout (How You'd Use This)
```cpp
// Use reflection data to create Vulkan descriptor set layout
VkDescriptorSetLayoutBinding bindings[] = {
    {
        .binding = blob->bindings[0].binding,  // 0
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
    },
    {
        .binding = blob->bindings[1].binding,  // 1
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
    },
    {
        .binding = blob->bindings[2].binding,  // 2
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
    }
};
```

---

## Example 3: Complex Material Shader (Multi-Space)

### Input Shader
```hlsl
// Per-frame data (space0)
cbuffer FrameConstants : register(b0, space0) {
    float4x4 viewProj;
    float time;
};

Texture2D<float4> blueNoise : register(t0, space0);
SamplerState linearSampler : register(s0, space0);

// Per-material data (space1)
Texture2D<float4> albedoMap : register(t0, space1);
Texture2D<float4> normalMap : register(t1, space1);
Texture2D<float4> roughnessMap : register(t2, space1);
SamplerState materialSampler : register(s0, space1);

cbuffer MaterialConstants : register(b0, space1) {
    float4 baseColor;
    float metallic;
    float roughness;
};

// Per-object data (space2)
cbuffer ObjectConstants : register(b0, space2) {
    float4x4 worldMatrix;
    uint objectID;
};

[shader("fragment")]
float4 main(float2 uv : TEXCOORD) : SV_Target {
    // ... shader code
}
```

### Reflection Output (Organized by Space)

```
[Slang] Reflection for 'main':
  Parameters: 10

  ===== Space 0 (Frame) =====
  [0] FrameConstants
      Category: ConstantBuffer (b)
      Binding:  b0, space0

  [1] blueNoise
      Category: ShaderResource (t)
      Binding:  t0, space0

  [2] linearSampler
      Category: SamplerState (s)
      Binding:  s0, space0

  ===== Space 1 (Material) =====
  [3] albedoMap
      Category: ShaderResource (t)
      Binding:  t0, space1

  [4] normalMap
      Category: ShaderResource (t)
      Binding:  t1, space1

  [5] roughnessMap
      Category: ShaderResource (t)
      Binding:  t2, space1

  [6] materialSampler
      Category: SamplerState (s)
      Binding:  s0, space1

  [7] MaterialConstants
      Category: ConstantBuffer (b)
      Binding:  b0, space1

  ===== Space 2 (Object) =====
  [8] ObjectConstants
      Category: ConstantBuffer (b)
      Binding:  b0, space2
```

### C++ Validation Code
```cpp
void validateMultiSpaceReflection(const ShaderBlob& blob) {
    // Count resources per space
    std::map<uint32_t, std::vector<std::string>> spaceResources;
    
    for (const auto& binding : blob.bindings) {
        spaceResources[binding.set].push_back(binding.name);
    }
    
    assert(spaceResources[0].size() == 3); // Frame resources
    assert(spaceResources[1].size() == 5); // Material resources
    assert(spaceResources[2].size() == 1); // Object resources
    
    // Verify specific bindings
    auto findBinding = [&](const char* name) {
        return std::find_if(blob.bindings.begin(), blob.bindings.end(),
            [name](const auto& b) { return b.name == name; });
    };
    
    auto albedo = findBinding("albedoMap");
    assert(albedo->binding == 0);
    assert(albedo->set == 1);
    assert(albedo->category == "t");
    
    auto normal = findBinding("normalMap");
    assert(normal->binding == 1);
    assert(normal->set == 1);
    
    std::cout << "✅ Multi-space reflection validated!\n";
}
```

---

## Example 4: Array Resources

### Input Shader
```hlsl
Texture2D<float4> textureArray[8] : register(t0, space0);
SamplerState samplers[4] : register(s0, space0);

[shader("fragment")]
float4 main(uint texIndex : TEXINDEX) : SV_Target {
    return textureArray[texIndex].Sample(samplers[0], float2(0.5, 0.5));
}
```

### Reflection Output (Future Enhancement)
```
[Slang] Reflection for 'main':
  Parameters: 2

  [0] textureArray
      Category: ShaderResource (t)
      Binding:  t0, space0
      Count:    8  ⬅️ Array size (future enhancement)
      Type:     Texture2D[8]

  [1] samplers
      Category: SamplerState (s)
      Binding:  s0, space0
      Count:    4  ⬅️ Array size
      Type:     SamplerState[4]
```

**Note:** Array count extraction requires additional logic to query array size from `TypeLayoutReflection::getElementCount()`. Can be added in Phase 1 if needed.

---

## What We're NOT Extracting (Phase 0)

### Nested Struct Fields
```hlsl
struct Material {
    float3 albedo;
    float roughness;
};

cbuffer MaterialData : register(b0) {
    Material material;  // ⬅️ We extract "MaterialData", not "material.albedo"
    float3 emissive;
};
```

**Reflection (Phase 0):**
```
[0] MaterialData @ b0, space0  ✅ Top-level only
```

**NOT extracted (unless we implement Phase C):**
```
❌ material.albedo @ offset 0 in MaterialData
❌ material.roughness @ offset 12 in MaterialData
❌ emissive @ offset 16 in MaterialData
```

### Entry Point Parameters (Varyings)
```hlsl
struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
};

[shader("vertex")]
VSOutput main(float3 pos : POSITION) {
    // ...
}
```

**We DO extract:** Global shader parameters (textures, buffers, cbuffers)  
**We DON'T extract:** Vertex attributes, interpolators, semantics (these use a different reflection path)

---

## How to Use Reflection in ChameleonRT

### DXR Backend (Example)
```cpp
// backends/dxr/dxr_renderer.cpp

auto rtShaders = compiler.compileHLSLToDXILLibrary(hlslSource);
if (!rtShaders) {
    std::cerr << "Shader compilation failed\n";
    return false;
}

// For each shader (RayGen, Miss, ClosestHit, etc.)
for (const auto& shader : *rtShaders) {
    std::cout << "\n[" << shader.entryPoint << "] bindings:\n";
    
    for (const auto& binding : shader.bindings) {
        std::cout << "  " << binding.name 
                  << " @ " << binding.category << binding.binding
                  << ", space" << binding.set << "\n";
    }
    
    // Validate against expected layout
    // (useful for catching Slang compilation changes)
    if (shader.entryPoint == "RayGen") {
        assert(findBinding(shader, "scene")->binding == 0);
        assert(findBinding(shader, "output")->binding == 0);
    }
}
```

### Vulkan Backend (Future Use)
```cpp
// backends/vulkan/vulkan_renderer.cpp

auto spirvShaders = compiler.compileToSPIRVLibrary(slangSource);

// Auto-generate descriptor set layouts from reflection
VkDescriptorSetLayout createDescriptorSet(const ShaderBlob& blob) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    
    for (const auto& b : blob.bindings) {
        VkDescriptorType type = categoryToVulkanDescriptorType(b.category);
        
        bindings.push_back({
            .binding = b.binding,
            .descriptorType = type,
            .descriptorCount = b.count,
            .stageFlags = stageToVulkanShaderStage(blob.stage)
        });
    }
    
    // Create layout...
}
```

---

## Summary

### What Phase B Gives You
✅ **Resource names** - "scene", "output", "albedoMap"  
✅ **Binding indices** - t0, b0, u0, s0, etc.  
✅ **Spaces/Sets** - space0, space1, etc.  
✅ **Register types** - Differentiate t vs b vs u vs s  
✅ **Per-entry-point** - Different bindings for RayGen vs Miss vs ClosestHit  

### What Phase B Doesn't Give You (Can Add Later)
❌ Nested struct field offsets  
❌ Array element counts (easy to add)  
❌ Uniform buffer internal layout  
❌ Vertex attribute semantics  

### Perfect For ChameleonRT's Needs
- Debug shader compilation
- Validate descriptor layouts
- Auto-generate binding documentation
- Catch regression in shader changes
- Foundation for future descriptor set auto-generation
