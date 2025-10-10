# DXR vs Vulkan RT Pipeline Comparison

**Date:** October 10, 2025  
**Purpose:** Detailed analysis of pipeline differences before Slang shader unification

**Related:** See `FALCOR_BINDING_ANALYSIS.md` for how NVIDIA Falcor solves these differences

---

## 1. Resource Binding Comparison

### DXR (D3D12) Binding Model

```hlsl
// Global resources - no set concept, just register spaces
RWTexture2D<float4> output : register(u0);                    // UAV space 0
RWTexture2D<float4> accum_buffer : register(u1);              // UAV space 0

cbuffer ViewParams : register(b0) { ... }                      // CBV space 0
cbuffer SceneParams : register(b1) { ... }                     // CBV space 0

RaytracingAccelerationStructure scene : register(t0);         // SRV space 0
StructuredBuffer<MaterialParams> material_params : register(t1); // SRV space 0
StructuredBuffer<QuadLight> lights : register(t2);            // SRV space 0
Texture2D textures[] : register(t3);                          // Unbounded array, SRV space 0

SamplerState tex_sampler : register(s0);                      // Sampler

// Per-mesh resources (shader record / local root signature) - space 1
StructuredBuffer<float3> vertices : register(t0, space1);
StructuredBuffer<uint3> indices : register(t1, space1);
StructuredBuffer<float3> normals : register(t2, space1);
StructuredBuffer<float2> uvs : register(t3, space1);

cbuffer MeshData : register(b0, space1) {
    uint32_t num_normals;
    uint32_t num_uvs;
    uint32_t material_id;
}
```

**Key Features:**
- Register spaces: `space0` (global), `space1` (local/per-geometry)
- Register types: `u` (UAV), `b` (CBV), `t` (SRV), `s` (Sampler)
- Unbounded arrays: `Texture2D textures[]`
- Access: `NonUniformResourceIndex()` for dynamic indexing

---

### Vulkan Binding Model

```glsl
// Set 0 - Global resources
layout(binding = 0, set = 0) uniform accelerationStructureEXT scene;
layout(binding = 1, set = 0, rgba8) uniform writeonly image2D framebuffer;
layout(binding = 2, set = 0, rgba32f) uniform image2D accum_buffer;

layout(binding = 3, set = 0, std140) uniform ViewParams { ... };

layout(binding = 4, set = 0, scalar) buffer MaterialParamsBuffer {
    MaterialParams material_params[];
};

layout(binding = 5, set = 0, std430) buffer LightParamsBuffer {
    QuadLight lights[];
};

layout(binding = 6, set = 0, r16ui) uniform writeonly uimage2D ray_stats;

// Set 1 - Textures (separate descriptor set)
layout(binding = 0, set = 1) uniform sampler2D textures[];

// Shader record data (via shaderRecordEXT)
layout(shaderRecordEXT) buffer SBT {
    uint32_t num_lights;
};

// Per-mesh resources (push constants + attributes, NOT in shader currently)
// Note: Vulkan backend accesses vertex data differently than DXR
```

**Key Features:**
- Descriptor sets: `set = 0` (global), `set = 1` (textures)
- Explicit binding points within sets
- Combined image-samplers: `sampler2D textures[]`
- Buffer layouts: `std140` (uniform), `std430` (storage), `scalar` (extended)
- Access: `nonuniformEXT()` for dynamic indexing
- Shader record: `shaderRecordEXT` buffer (like DXR local root signature)

---

## 2. Key Differences Table

| Feature | DXR (HLSL) | Vulkan (GLSL) | Slang Unified? |
|---------|------------|---------------|----------------|
| **Resource Grouping** | Register spaces (0, 1) | Descriptor sets (0, 1) | ✅ Maps to both |
| **Binding Syntax** | `register(u0)` | `layout(binding=0, set=0)` | ✅ Slang uses semantic names |
| **UAV/Images** | `RWTexture2D<float4>` | `uniform image2D` + format | ✅ `RWTexture2D` in Slang |
| **Buffers** | `StructuredBuffer<T>` | `buffer { T[] }` | ✅ `StructuredBuffer` works |
| **Uniforms** | `cbuffer` | `uniform` block | ✅ `cbuffer` or `ConstantBuffer` |
| **TLAS** | `RaytracingAccelerationStructure` | `accelerationStructureEXT` | ✅ Same in Slang |
| **Unbounded Arrays** | `Texture2D[]` implicit | `sampler2D[]` explicit | ✅ Supported |
| **Sampler** | Separate `SamplerState` | Combined in `sampler2D` | ⚠️ **DIFFERENCE** |
| **Dynamic Indexing** | `NonUniformResourceIndex()` | `nonuniformEXT()` | ✅ Slang handles both |
| **Ray Payload** | Function parameter | `rayPayloadEXT` qualifier | ✅ `[raypayload]` attribute |
| **Shader Record** | Local root signature (space1) | `shaderRecordEXT` buffer | ✅ Both supported |
| **Per-Mesh Data** | `register(t, space1)` | Not currently in GLSL version | ⚠️ **STRUCTURAL DIFFERENCE** |

---

## 3. Critical Binding Differences

### 3.1 Texture Sampling

**DXR Approach:**
```hlsl
Texture2D textures[] : register(t3);     // Texture array
SamplerState tex_sampler : register(s0); // Separate sampler

// Usage
float4 color = textures[NonUniformResourceIndex(tex_id)]
    .SampleLevel(tex_sampler, uv, 0);
```

**Vulkan Approach:**
```glsl
layout(binding = 0, set = 1) uniform sampler2D textures[]; // Combined

// Usage
vec4 color = texture(textures[nonuniformEXT(tex_id)], uv);
```

**Slang Solution:**
```slang
// Slang can handle both patterns:
Texture2D textures[];
SamplerState sampler;
// Compiles to combined samplers for Vulkan, separate for D3D12
```

### 3.2 Per-Mesh Resources (Shader Record)

**DXR:** Uses local root signature (space 1) for per-geometry data
```hlsl
StructuredBuffer<float3> vertices : register(t0, space1);
StructuredBuffer<uint3> indices : register(t1, space1);
cbuffer MeshData : register(b0, space1) {
    uint32_t num_normals;
    uint32_t num_uvs;
    uint32_t material_id;
}
```

**Vulkan:** Uses shader record buffer (limited size)
```glsl
layout(shaderRecordEXT) buffer SBT {
    uint32_t num_lights;
};
// Vertex data accessed differently (not shown in current shader)
```

**Issue:** The Vulkan backend doesn't currently bind per-mesh vertex buffers in the shader like DXR does. This is a **structural difference** in how geometry data is accessed.

---

## 4. Ray Tracing Pipeline Structure

### DXR Shaders (Single File)

```hlsl
// All in one file: render_dxr.hlsl

[shader("raygeneration")]
void RayGen() { ... }

[shader("miss")]
void Miss(inout HitInfo payload) { ... }

[shader("miss")]
void ShadowMiss(inout OcclusionHitInfo payload) { ... }

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib) {
    // Access per-mesh data via space1
    uint3 idx = indices[PrimitiveIndex()];
    float3 va = vertices[idx.x];
    ...
}
```

### Vulkan Shaders (Separate Files)

```
raygen.rgen          - Ray generation
miss.rmiss           - Primary miss
occlusion_miss.rmiss - Shadow miss  
hit.rchit            - Closest hit
```

Each file has its own `#extension` and bindings.

**Slang Approach:**
```slang
// Can be single file like DXR, or multiple files
// Slang compiles each [shader("...")] to appropriate stage
```

---

## 5. Layout Qualifiers

### Buffer Layout Differences

**DXR:** Implicit packing (follows HLSL rules, generally std140-like)

**Vulkan:** Explicit layout required
```glsl
layout(binding = 3, set = 0, std140) uniform ViewParams { ... }
layout(binding = 4, set = 0, scalar) buffer MaterialParamsBuffer { ... }
layout(binding = 5, set = 0, std430) buffer LightParamsBuffer { ... }
```

- `std140`: Uniform buffer, strict alignment (16-byte for vec3)
- `std430`: Storage buffer, tighter packing
- `scalar`: VK_EXT_scalar_block_layout, most efficient

**Slang:** Uses `[[vk::binding(...)]]` or parameter blocks to control layout

---

## 6. Type Differences

| Concept | DXR/HLSL | Vulkan/GLSL | Slang |
|---------|----------|-------------|-------|
| Vector3 | `float3` | `vec3` | `float3` |
| Vector4 | `float4` | `vec4` | `float4` |
| Matrix | `float4x4` | `mat4` | `float4x4` |
| Integer | `uint32_t` | `uint` | `uint` |
| Bit cast | `asuint()` | `floatBitsToUint()` | `asuint()` |
| Texture sample | `.SampleLevel(sampler, uv, 0)` | `texture(sampler2D, uv)` | `.SampleLevel()` |

**Slang:** Mostly follows HLSL conventions, cross-compiles to GLSL equivalents

---

## 7. Major Alignment Challenges

### 7.1 **CRITICAL: Per-Geometry Data Access**

**Current State:**
- DXR: Accesses vertices/indices via space1 buffers ✅
- Vulkan: Does NOT access vertices in shader (uses built-in attributes?) ⚠️

This is a **fundamental difference** that needs investigation:
- How does Vulkan ClosestHit get vertex positions?
- Is it using `gl_HitTriangleVertexPositionsEXT`? (requires extension)
- Or different C++ side setup?

### 7.2 Sampler Model

**DXR:** Separate textures and samplers
**Vulkan:** Combined image-samplers

Slang can handle this, but requires careful descriptor set design.

### 7.3 Descriptor Set Organization

**Must decide:** How to map DXR's space0/space1 to Vulkan's set0/set1?

**Current mapping:**
- space0 → set0 (global resources)
- space1 → ??? (per-mesh data not in Vulkan shader currently)

---

## 8. C++ Side: Pipeline Setup Differences

### DXR Pipeline Creation
```cpp
// Create shader library from DXIL bytecode
auto library = createShaderLibrary(dxilBlob);

// Hit groups
D3D12_HIT_GROUP_DESC hitGroup = {};
hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
hitGroup.ClosestHitShaderImport = L"ClosestHit";
hitGroup.HitGroupExport = L"HitGroup";

// Shader config (payload size)
D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
shaderConfig.MaxPayloadSizeInBytes = sizeof(HitInfo);
shaderConfig.MaxAttributeSizeInBytes = sizeof(Attributes);

// Pipeline config
D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
pipelineConfig.MaxTraceRecursionDepth = MAX_PATH_DEPTH;

// Root signature associations
// Global root signature (space0)
// Local root signature (space1) per hit group
```

### Vulkan Pipeline Creation
```cpp
// Create shader modules from SPIRV
VkShaderModule raygen = createShaderModule(raygenSpirv);
VkShaderModule miss = createShaderModule(missSpirv);
VkShaderModule hit = createShaderModule(hitSpirv);

// Shader stages
VkPipelineShaderStageCreateInfo stages[] = {
    {.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR, .module = raygen, .pName = "main"},
    {.stage = VK_SHADER_STAGE_MISS_BIT_KHR, .module = miss, .pName = "main"},
    {.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, .module = hit, .pName = "main"},
};

// Shader groups
VkRayTracingShaderGroupCreateInfoKHR groups[] = {
    // Raygen group
    {.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, .generalShader = 0},
    // Miss groups
    {.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, .generalShader = 1},
    // Hit group
    {.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR, 
     .closestHitShader = 2},
};

// Pipeline layout (descriptor sets)
VkPipelineLayoutCreateInfo layoutInfo = {
    .setLayoutCount = 2,  // set 0, set 1
    .pSetLayouts = descriptorSetLayouts,
};
```

**Key Difference:** 
- DXR: Single shader library, multiple entry points
- Vulkan: Separate shader modules, entry point always "main"

---

## 9. Assessment: Can We Unify?

### Factors in Favor ✅

1. **Data structures are identical** - `MaterialParams`, `DisneyMaterial`, etc.
2. **Core algorithms are identical** - Same Disney BRDF, ray tracing logic
3. **Slang supports both binding models** - Can map to D3D12 and Vulkan
4. **Same includes work** - `disney_bsdf`, `lcg_rng`, `lights` are portable
5. **Unbounded arrays supported** - Slang handles `Texture2D[]` for both

### Challenges ⚠️

1. **Sampler model difference** - Separate vs combined (Slang can handle)
2. **Per-mesh data access** - DXR uses space1 buffers, Vulkan method unclear
3. **Entry point names** - DXR uses function names, Vulkan uses "main"
4. **File structure** - Single vs multiple files (both work in Slang)
5. **Descriptor set mapping** - Need careful planning

### Blockers 🛑

1. **MUST INVESTIGATE:** How does Vulkan hit shader access vertex data?
   - Check `hit.rchit` implementation
   - Check C++ binding setup
   - Determine if it uses extensions or different approach

2. **Binding strategy decision needed:**
   - How to map space0/space1 to sets?
   - Where do textures go? (Currently set 1 in Vulkan)
   - How to handle shader record data?

---

## 10. Recommended Investigation Steps

### Before proceeding with Slang unification:

1. ✅ **Analyze Vulkan hit shader** - How does it get vertex positions/normals?
2. ✅ **Check Falcor's approach** - How do they handle DXR vs Vulkan differences?
3. ✅ **Design descriptor set layout** - Map DXR spaces to Vulkan sets
4. ⏸️ **Prototype small shader** - Test Slang compilation to both targets
5. ⏸️ **Validate sampler handling** - Ensure separate samplers work in Vulkan

### Questions to Answer:

- Can we access per-mesh vertex buffers in Vulkan RT like DXR does?
- Or do we need to restructure to use built-in triangle attributes only?
- How does Slang's `[[vk::binding(...)]]` attribute work?
- What's the right descriptor set organization?

---

## 10. Recommended Path Forward (Updated)

### Discovery: Vulkan Per-Mesh Data Access

**Found:** Vulkan uses `buffer_reference` (VK_EXT_buffer_device_address) in shader record:

```glsl
layout(buffer_reference, scalar) buffer VertexBuffer { vec3 v[]; };
layout(shaderRecordEXT, std430) buffer SBT {
    VertexBuffer verts;    // GPU device address
    IndexBuffer indices;   // GPU device address
    // ...
};

void main() {
    const uvec3 idx = indices.i[gl_PrimitiveID];
    const vec3 va = verts.v[idx.x];  // Access via buffer_reference
}
```

This is analogous to DXR's `space1` local root signature approach.

### Discovery: Falcor's Solution

**Key Finding:** NVIDIA Falcor **eliminates per-mesh descriptors entirely** by:
1. Merging all mesh buffers into **global arrays** (vertices[], indices[])
2. Using **indirection** via `GeometryInstanceData` → `MeshDesc` → offsets
3. Abstracting access behind `getVertexData(instanceID, triangleID, barycentrics)`
4. Using **Slang's automatic binding translation** (`[root]` attribute, no manual bindings)

See `FALCOR_BINDING_ANALYSIS.md` for complete details.

### Recommendation: Adopt Falcor's Pattern

**Advantages:**
- ✅ **Truly unified shaders** - Same code for DXR/Vulkan/Metal
- ✅ **Proven in production** - NVIDIA Falcor uses this approach
- ✅ **Simpler pipeline setup** - No local root signature or shader records
- ✅ **Industry best practice** - Learn production rendering patterns
- ✅ **Better for project goals** - "Understanding APIs deeply"

**What Changes:**
- CPU: Merge mesh buffers into global arrays with indirection structs
- Shaders: Use `Scene` struct abstraction instead of direct buffer access
- DXR: Remove `space1` local root signature
- Vulkan: Remove shader record buffer address setup
- Both: Use Slang reflection for automatic binding discovery

**Implementation Plan:**
1. Phase 2A: Refactor CPU scene loading (merge buffers, create indirection)
2. Phase 2B: Create `Scene.slang` module (struct + helpers)
3. Phase 2C: Convert shaders to use Scene abstraction
4. Phase 2D: Update DXR C++ (remove local root sig, use reflection)
5. Phase 2E: Update Vulkan C++ (remove shader records, use reflection)
6. Phase 2F: Validate on both backends

---

## Conclusion

**Feasibility:** ✅ **ACHIEVABLE WITH SCENE ABSTRACTION PATTERN**

The shaders use **same core logic** but have **different resource binding models**. 

**Solution Found:** Follow Falcor's pattern:
- Global geometry buffers (no per-mesh descriptors)
- Scene struct abstraction
- Slang automatic binding translation
- Production-proven approach

**Next Steps:**
1. ✅ ~~Investigate Vulkan vertex data access~~ (DONE: uses buffer_reference)
2. ✅ ~~Study Falcor's multi-API approach~~ (DONE: see FALCOR_BINDING_ANALYSIS.md)
3. **Begin Phase 2: Scene abstraction implementation**

**DO NOT** jump into converting shaders until we understand how to bridge the per-mesh data access difference.
