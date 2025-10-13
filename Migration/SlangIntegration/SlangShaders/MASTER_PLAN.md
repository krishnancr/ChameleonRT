# Slang Integration: Phase 2B - Unified Slang Shaders

**Date:** October 10, 2025  
**Status:** Planning  
**Goal:** Convert native HLSL/GLSL shaders to unified Slang shaders  
**Prerequisites:** Phase 2A must be complete (global buffers working with native shaders)

---

## Overview

This phase converts the native HLSL (DXR) and GLSL (Vulkan) shaders to a unified Slang shader that compiles to both DXIL and SPIRV. This completes the Slang integration by achieving true shader unification.

**Key Changes:**
1. Create Scene.slang module (data structures and helpers)
2. Create unified raytracing.slang shader (replaces HLSL and GLSL)
3. Update DXR C++ to compile Slang → DXIL
4. Update Vulkan C++ to compile Slang → SPIRV
5. Validate identical rendering output

**C++ changes are minimal** - Phase 2A already prepared the backends for global buffers.

---

## Success Criteria

✅ **Scene.slang Module:**
- Defines Scene struct with all resources
- Implements getVertexData() helper function
- Compiles to DXIL without errors
- Compiles to SPIRV without errors

✅ **Unified raytracing.slang:**
- Single shader file with RayGen, ClosestHit, Miss entry points
- Uses Scene module for data access
- Compiles to DXIL (for DXR)
- Compiles to SPIRV (for Vulkan)

✅ **DXR Backend:**
- Compiles raytracing.slang → DXIL at runtime
- test_cube.obj renders identically to Phase 2A (HLSL version)
- Sponza renders identically to Phase 2A

✅ **Vulkan Backend:**
- Compiles raytracing.slang → SPIRV at runtime
- test_cube.obj renders identically to Phase 2A (GLSL version)
- Sponza renders identically to Phase 2A

✅ **Cross-Backend Validation:**
- DXR and Vulkan produce identical screenshots (from same shader!)

---

## Phase 2B.0: Preparation

**Goal:** Verify Phase 2A is complete and understand shader structure

### Tasks

**2B.0.1: Verify Phase 2A Completion**
- DXR renders correctly with HLSL and global buffers
- Vulkan renders correctly with GLSL and global buffers
- Both backends have baseline screenshots for comparison

**2B.0.2: Analyze Current Shader Structure**
- Read `backends/dxr/render_dxr.hlsl` (Phase 2A version)
- Identify shader entry points: RayGen, ClosestHit, Miss
- Document shader logic flow
- Note resource bindings (space0 for DXR)

**2B.0.3: Analyze Vulkan Shader Structure**
- Read `backends/vulkan/*.rgen`, `*.rchit`, `*.rmiss` (Phase 2A versions)
- Identify shader entry points
- Note resource bindings (set0 for Vulkan)
- Compare with DXR logic (should be nearly identical)

**2B.0.4: Study Slang Syntax**
- Review Slang examples in `c:\dev\slang\examples\ray-tracing-pipeline\`
- Understand Slang shader stage attributes: `[shader("raygeneration")]`, etc.
- Understand ParameterBlock usage
- Note syntax differences from HLSL/GLSL

**Deliverables:**
- Understanding of current shader structure (both backends)
- Confidence that Phase 2A is working correctly
- Knowledge of Slang syntax for ray tracing

---

## Phase 2B.1: Create Scene Module

**Goal:** Define reusable Scene abstraction in Slang

### File Structure

```
util/
├─ SceneTypes.slang (NEW) - Basic type definitions
└─ Scene.slang (NEW)      - Scene struct and helpers
```

### SceneTypes.slang

**File:** `util/SceneTypes.slang`

```slang
#pragma once

// Unified vertex structure (position, normal, texCoord)
struct Vertex
{
    float3 position;
    float3 normal;
    float2 texCoord;
};

// Mesh descriptor - metadata for accessing global buffers
struct MeshDesc
{
    uint vbOffset;      // Offset into global vertex buffer
    uint ibOffset;      // Offset into global index buffer
    uint vertexCount;   // Number of vertices in this mesh
    uint indexCount;    // Number of indices (triangles * 3)
    uint materialID;    // Material index
};

// Interpolated vertex data (returned by getVertexData)
struct VertexData
{
    float3 position;    // Interpolated position (local space)
    float3 normal;      // Interpolated normal (local space)
    float2 texCoord;    // Interpolated texture coordinate
    uint materialID;    // Material ID for this geometry
};

// Material parameters (match your C++ DisneyMaterial or similar)
struct MaterialParams
{
    float3 baseColor;
    float metallic;
    float roughness;
    uint albedoTexID;
    uint metallicRoughnessTexID;
    uint normalMapTexID;
    // Add other fields as needed
};

// Simple quad light structure (match your C++ QuadLight)
struct QuadLight
{
    float3 position;
    float3 v1, v2;      // Edge vectors
    float3 emission;
    // Add other fields as needed
};
```

### Scene.slang

**File:** `util/Scene.slang`

```slang
import SceneTypes;

// Scene structure - holds all scene resources
struct Scene
{
    // Global geometry buffers (all meshes concatenated)
    [root] StructuredBuffer<Vertex> vertices;
    [root] StructuredBuffer<uint> indices;
    [root] StructuredBuffer<MeshDesc> meshDescs;
    
    // Acceleration structure
    [root] RaytracingAccelerationStructure tlas;
    
    // Materials and textures
    StructuredBuffer<MaterialParams> materials;
    Texture2D textures[];           // Unbounded array
    SamplerState linearSampler;
    
    // Lights
    StructuredBuffer<QuadLight> lights;
    uint numLights;
    
    // Helper function: Get interpolated vertex data
    // This abstracts the offset calculation and interpolation
    VertexData getVertexData(uint instanceID, uint primitiveID, float3 barycentrics)
    {
        // Get mesh descriptor (contains offsets)
        MeshDesc mesh = meshDescs[instanceID];
        
        // Calculate triangle index with offset
        uint triIndex = mesh.ibOffset + primitiveID * 3;
        
        // Fetch vertex indices
        uint idx0 = indices[triIndex + 0];
        uint idx1 = indices[triIndex + 1];
        uint idx2 = indices[triIndex + 2];
        
        // Fetch vertices
        Vertex v0 = vertices[idx0];
        Vertex v1 = vertices[idx1];
        Vertex v2 = vertices[idx2];
        
        // Interpolate vertex attributes
        VertexData result;
        result.position = v0.position * barycentrics.x + 
                         v1.position * barycentrics.y + 
                         v2.position * barycentrics.z;
        
        result.normal = normalize(v0.normal * barycentrics.x + 
                                 v1.normal * barycentrics.y + 
                                 v2.normal * barycentrics.z);
        
        result.texCoord = v0.texCoord * barycentrics.x + 
                         v1.texCoord * barycentrics.y + 
                         v2.texCoord * barycentrics.z;
        
        result.materialID = mesh.materialID;
        
        return result;
    }
    
    // Helper: Sample texture with material
    float3 sampleAlbedo(uint materialID, float2 texCoord)
    {
        MaterialParams mat = materials[materialID];
        if (mat.albedoTexID != 0xFFFFFFFF)  // Has texture
        {
            return textures[mat.albedoTexID].Sample(linearSampler, texCoord).rgb;
        }
        return mat.baseColor;  // Use solid color
    }
};

// Global scene parameter block
ParameterBlock<Scene> gScene;
```

### Tasks

**2B.1.1: Create SceneTypes.slang**
- Define Vertex, MeshDesc, VertexData structs
- Match existing C++ struct layouts (from Phase 2A)
- Define MaterialParams and QuadLight (match your scene)

**2B.1.2: Create Scene.slang**
- Define Scene struct with all resources
- Use `[root]` attribute for frequently accessed buffers
- Implement getVertexData() helper
- Optionally add sampleAlbedo() or other helpers

**2B.1.3: Test Compilation**
- Compile Scene.slang to DXIL using SlangShaderCompiler
- Compile Scene.slang to SPIRV using SlangShaderCompiler
- Verify no compilation errors
- Note: This won't link/run yet (no entry points), just testing syntax

**2B.1.4: Add to CMake**
- Add util/SceneTypes.slang and util/Scene.slang to CMakeLists.txt
- Mark as SLANG source files
- Ensure they're copied to build directory if needed

**Validation:**
- Scene.slang compiles to DXIL ✅
- Scene.slang compiles to SPIRV ✅
- No errors or warnings

---

## Phase 2B.2: Create Unified Ray Tracing Shader

**Goal:** Single shader file for both DXR and Vulkan

### File Structure

```
backends/shaders/
└─ raytracing.slang (NEW) - Unified shader with all entry points
```

### raytracing.slang

**File:** `backends/shaders/raytracing.slang`

```slang
import Scene;

// Constant buffer (camera and frame data)
cbuffer ViewParams : register(b0, space0)
{
    float4x4 viewProj;
    float4x4 invViewProj;
    float3 cameraPos;
    float pad0;
    float3 cameraDir;
    float pad1;
    uint frameCount;
};

// Output texture
RWTexture2D<float4> outputColor : register(u0, space0);

// Ray payload
struct Payload
{
    float3 color;
    uint depth;
    bool hit;
};

// === RAY GENERATION SHADER ===
[shader("raygeneration")]
void RayGen()
{
    // Get pixel coordinates
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim = DispatchRaysDimensions().xy;
    
    // Calculate normalized device coordinates
    float2 pixelCenter = float2(launchIndex) + float2(0.5, 0.5);
    float2 ndc = (pixelCenter / float2(launchDim)) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y for screen space
    
    // Generate camera ray
    float4 target = mul(invViewProj, float4(ndc, 1.0, 1.0));
    target /= target.w;
    
    RayDesc ray;
    ray.Origin = cameraPos;
    ray.Direction = normalize(target.xyz - cameraPos);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    
    // Initialize payload
    Payload payload;
    payload.color = float3(0, 0, 0);
    payload.depth = 0;
    payload.hit = false;
    
    // Trace primary ray
    TraceRay(
        gScene.tlas,                    // Acceleration structure
        RAY_FLAG_NONE,                  // Ray flags
        0xFF,                           // Instance inclusion mask
        0,                              // Ray contribution to hit group index
        1,                              // Multiplier for geometry contribution
        0,                              // Miss shader index
        ray,
        payload
    );
    
    // Write result
    outputColor[launchIndex] = float4(payload.color, 1.0);
}

// === CLOSEST HIT SHADER ===
[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    // Calculate barycentric coordinates
    float3 barycentrics = float3(
        1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
        attribs.barycentrics.x,
        attribs.barycentrics.y
    );
    
    // Get interpolated vertex data using Scene helper
    VertexData v = gScene.getVertexData(
        InstanceIndex(),      // Which geometry instance
        PrimitiveIndex(),     // Which triangle
        barycentrics          // Interpolation weights
    );
    
    // Transform to world space
    float3 worldPos = mul(ObjectToWorld3x4(), float4(v.position, 1.0)).xyz;
    float3 worldNormal = normalize(mul((float3x3)ObjectToWorld3x4(), v.normal));
    
    // Sample material
    float3 albedo = gScene.sampleAlbedo(v.materialID, v.texCoord);
    
    // Simple diffuse lighting (replace with your shading)
    float3 lightDir = normalize(float3(1, 1, 1));
    float ndotl = max(0.0, dot(worldNormal, lightDir));
    
    payload.color = albedo * (0.2 + 0.8 * ndotl);  // Ambient + diffuse
    payload.hit = true;
}

// === MISS SHADER ===
[shader("miss")]
void Miss(inout Payload payload)
{
    // Sky gradient (simple)
    float3 rayDir = WorldRayDirection();
    float t = 0.5 * (rayDir.y + 1.0);
    payload.color = lerp(float3(1.0, 1.0, 1.0), float3(0.5, 0.7, 1.0), t);
    payload.hit = false;
}
```

### Tasks

**2B.2.1: Create raytracing.slang**
- Import Scene module
- Define ViewParams cbuffer (match your camera data)
- Define Payload struct
- Port RayGen shader from HLSL (Phase 2A version)

**2B.2.2: Port ClosestHit Shader**
- Port logic from HLSL closesthit
- Replace manual offset calculation with `gScene.getVertexData()`
- Port shading logic (lighting, texture sampling, etc.)
- Use Slang built-ins: `InstanceIndex()`, `PrimitiveIndex()`, `ObjectToWorld3x4()`

**2B.2.3: Port Miss Shader**
- Port logic from HLSL miss shader
- Use Slang built-in: `WorldRayDirection()`

**2B.2.4: Test Compilation**
- Compile raytracing.slang → DXIL (for DXR)
  - Entry points: RayGen, ClosestHit, Miss
  - Target: DXIL (SM 6.3 or higher)
- Compile raytracing.slang → SPIRV (for Vulkan)
  - Entry points: RayGen, ClosestHit, Miss
  - Target: SPIRV (Vulkan 1.2)
- Verify no compilation errors

**Validation:**
- raytracing.slang compiles to DXIL ✅
- raytracing.slang compiles to SPIRV ✅
- All entry points found ✅
- No errors or warnings

---

## Phase 2B.3: Update DXR Backend to Use Slang

**Goal:** Compile Slang → DXIL, validate rendering

### C++ Changes

**File:** `backends/dxr/render_dxr.cpp`

**Modify shader compilation (minimal change from Phase 2A):**

```cpp
// In initialize() or wherever shaders are compiled:

// OLD (Phase 2A - HLSL):
// auto raygenBlob = compileHLSL("backends/dxr/render_dxr.hlsl", "RayGen", "lib_6_3");

// NEW (Phase 2B - Slang):
SlangShaderCompiler compiler;

// Compile RayGen
auto raygenBlob = compiler.compileFromFile(
    "backends/shaders/raytracing.slang",
    "RayGen",
    SlangCompileTarget::DXIL,
    ShaderStage::RayGeneration
);

// Compile ClosestHit
auto closestHitBlob = compiler.compileFromFile(
    "backends/shaders/raytracing.slang",
    "ClosestHit",
    SlangCompileTarget::DXIL,
    ShaderStage::ClosestHit
);

// Compile Miss
auto missBlob = compiler.compileFromFile(
    "backends/shaders/raytracing.slang",
    "Miss",
    SlangCompileTarget::DXIL,
    ShaderStage::Miss
);

// Rest of pipeline creation unchanged (Phase 2A already set up global buffers)
```

**Update descriptor heap binding (should already be correct from Phase 2A):**
- Ensure Scene resources are bound to space0
- Verify binding indices match Slang compilation reflection

### Tasks

**2B.3.1: Update Shader Compilation Code**
- Replace HLSL compilation with Slang compilation
- Compile all three entry points (RayGen, ClosestHit, Miss)
- Store resulting DXIL blobs

**2B.3.2: Verify Descriptor Bindings**
- Use Slang reflection to get resource binding info
- Verify matches Phase 2A descriptor heap layout
- Update if needed (should be minimal)

**2B.3.3: Build and Test**
- Build DXR backend
- Run with test_cube.obj
- Compare screenshot to Phase 2A baseline (HLSL version)
- Should be pixel-identical

**2B.3.4: Test with Sponza**
- Run with Sponza scene
- Compare screenshot to Phase 2A baseline
- Should be pixel-identical

**Validation:**
- DXR renders with Slang shader ✅
- test_cube.obj matches Phase 2A (HLSL) ✅
- Sponza matches Phase 2A (HLSL) ✅
- No crashes, validation errors, or visual differences

---

## Phase 2B.4: Update Vulkan Backend to Use Slang

**Goal:** Compile Slang → SPIRV, validate rendering

### C++ Changes

**File:** `backends/vulkan/render_vulkan.cpp`

**Modify shader compilation (minimal change from Phase 2A):**

```cpp
// In initialize() or wherever shaders are compiled:

// OLD (Phase 2A - GLSL):
// auto raygenSpirv = compileGLSL("backends/vulkan/raygen.rgen", shaderc_raygen_shader);

// NEW (Phase 2B - Slang):
SlangShaderCompiler compiler;

// Compile RayGen
auto raygenSpirv = compiler.compileFromFile(
    "backends/shaders/raytracing.slang",
    "RayGen",
    SlangCompileTarget::SPIRV,
    ShaderStage::RayGeneration
);

// Compile ClosestHit
auto closestHitSpirv = compiler.compileFromFile(
    "backends/shaders/raytracing.slang",
    "ClosestHit",
    SlangCompileTarget::SPIRV,
    ShaderStage::ClosestHit
);

// Compile Miss
auto missSpirv = compiler.compileFromFile(
    "backends/shaders/raytracing.slang",
    "Miss",
    SlangCompileTarget::SPIRV,
    ShaderStage::Miss
);

// Rest of pipeline creation unchanged (Phase 2A already set up global buffers)
```

**Update descriptor set bindings (should already be correct from Phase 2A):**
- Ensure Scene resources are bound to set0
- Verify binding indices match Slang compilation reflection

### Tasks

**2B.4.1: Update Shader Compilation Code**
- Replace GLSL compilation with Slang compilation
- Compile all three entry points (RayGen, ClosestHit, Miss)
- Store resulting SPIRV blobs

**2B.4.2: Verify Descriptor Bindings**
- Use Slang reflection to get resource binding info
- Verify matches Phase 2A descriptor set layout
- Update if needed (should be minimal)

**2B.4.3: Build and Test**
- Build Vulkan backend
- Run with test_cube.obj
- Compare screenshot to Phase 2A baseline (GLSL version)
- Should be pixel-identical

**2B.4.4: Test with Sponza**
- Run with Sponza scene
- Compare screenshot to Phase 2A baseline
- Should be pixel-identical

**Validation:**
- Vulkan renders with Slang shader ✅
- test_cube.obj matches Phase 2A (GLSL) ✅
- Sponza matches Phase 2A (GLSL) ✅
- No crashes, validation errors, or visual differences

---

## Phase 2B.5: Cross-Backend Validation

**Goal:** Verify both backends produce identical results from unified shader

### Tasks

**2B.5.1: DXR vs Vulkan Comparison (test_cube.obj)**
- Render test_cube.obj with DXR (Slang)
- Render test_cube.obj with Vulkan (Slang)
- Save screenshots
- Compare pixel-by-pixel (should be identical)

**2B.5.2: DXR vs Vulkan Comparison (Sponza)**
- Render Sponza with DXR (Slang)
- Render Sponza with Vulkan (Slang)
- Save screenshots
- Compare visually (should be identical)

**2B.5.3: Verify Unified Shader Usage**
- Confirm both backends compile from `backends/shaders/raytracing.slang`
- Confirm no backend-specific shader code remains
- Delete old HLSL and GLSL shader files (or archive them)

**2B.5.4: Performance Check**
- Measure frame times (DXR vs Vulkan)
- Should be similar to Phase 2A performance
- Small differences acceptable (Slang compilation overhead is one-time)

**2B.5.5: Documentation**
- Update STATUS.md - mark Phase 2B complete
- Document Slang integration completion
- Create before/after comparison showing unified architecture

**Validation:**
- DXR and Vulkan produce identical screenshots ✅
- Single shader file (`raytracing.slang`) used by both ✅
- No performance regression ✅
- Both backends stable and working ✅

---

## Deliverables

Upon completion of Phase 2B:

1. ✅ **Scene.slang Module** - Reusable scene data abstraction
2. ✅ **raytracing.slang** - Unified shader for both backends
3. ✅ **DXR using Slang** - Compiles Slang → DXIL, renders identically
4. ✅ **Vulkan using Slang** - Compiles Slang → SPIRV, renders identically
5. ✅ **Cross-backend validation** - Both produce identical output
6. ✅ **Slang integration complete** - Goal of project achieved

---

## Risk Mitigation

**Risk:** Slang syntax errors  
**Mitigation:** Test compile Scene.slang first (2B.1), then raytracing.slang (2B.2), before backend integration

**Risk:** Resource binding mismatches  
**Mitigation:** Phase 2A already set up bindings correctly, minimal changes needed

**Risk:** Shader logic differences causing visual bugs  
**Mitigation:** Port shader logic carefully, test incrementally (DXR first, then Vulkan)

**Risk:** Performance issues  
**Mitigation:** Slang compilation is one-time cost, runtime performance identical to native

---

## Notes

- Phase 2A must be complete before starting 2B
- C++ changes are minimal (just shader compilation calls)
- Most work is porting shader logic to Slang syntax
- Slang syntax is very similar to HLSL (easier to port)
- Test compilation early and often
- Keep Phase 2A baseline screenshots for comparison

---

## Project Completion

After Phase 2B, the Slang integration project is **COMPLETE**:

✅ **Global buffer architecture** (Phase 2A)  
✅ **Unified Slang shaders** (Phase 2B)  
✅ **DXR and Vulkan using same shader** (Goal achieved)  
✅ **Production-quality architecture** (Falcor pattern)

**Next steps (optional future work):**
- Add more complex shading (PBR, ray cones, etc.)
- Add shadow rays, reflections, etc.
- Extend to other backends (Metal, WGSL)
- All using the same unified Slang shader!

---

## Success Metrics

**Before Slang Integration:**
- Separate HLSL shaders for DXR
- Separate GLSL shaders for Vulkan
- Manual synchronization of logic
- Per-geometry buffer complexity

**After Phase 2B (Complete):**
- Single `raytracing.slang` for both backends
- Automatic logic synchronization
- Global buffer simplicity
- Industry-standard architecture
