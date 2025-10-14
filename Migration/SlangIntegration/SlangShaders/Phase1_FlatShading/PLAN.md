# Phase 1: Flat Shading - Detailed Plan

**Status:** 📋 Planning  
**Duration:** 3-5 days  
**Risk Level:** MEDIUM

---

## Objectives

Create minimal Slang RT shaders and validate end-to-end RT pipeline creation for both DXR and Vulkan. Achieve normal-shaded rendering (flat colors based on geometric normals) using unified Slang source.

---

## Prerequisites

- ✅ Phase 0 completed (`SlangShaderCompiler` enhanced)
- ✅ Current DXR implementation rendering correctly
- ✅ Current Vulkan RT implementation rendering correctly
- ✅ Test scene available (simple cube or sphere)

---

## Detailed Sub-Phase Plans

---

## Phase 1.1: Compilation Infrastructure Test

**Duration:** Half day  
**Goal:** Validate Slang compilation without RT pipeline complexity

### Shader Design: `test_compilation_only.slang`

```slang
// Simple compute shader - no RT, just test compilation
RWTexture2D<float4> outputTexture : register(u0, space0);

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    uint2 dims;
    outputTexture.GetDimensions(dims.x, dims.y);
    
    // Checkerboard pattern
    bool checker = ((pixel.x / 32) + (pixel.y / 32)) & 1;
    float3 color = checker ? float3(1, 0, 0) : float3(0, 0, 1);
    
    outputTexture[pixel] = float4(color, 1.0);
}
```

### Tasks

1. **Write shader** (15 min)
   - Copy above into `Phase1_FlatShading/test_compilation_only.slang`

2. **Compile to DXIL** (15 min)
   ```cpp
   SlangShaderCompiler compiler;
   auto source = SlangShaderCompiler::loadShaderSource("test_compilation_only.slang");
   auto dxilBlob = compiler.compileSlangToDXIL(
       *source, "computeMain", ShaderStage::Compute
   );
   assert(dxilBlob.has_value());
   std::cout << "DXIL size: " << dxilBlob->bytecode.size() << " bytes\n";
   ```

3. **Compile to SPIRV** (15 min)
   ```cpp
   auto spirvBlob = compiler.compileSlangToSPIRV(
       *source, "computeMain", ShaderStage::Compute
   );
   assert(spirvBlob.has_value());
   std::cout << "SPIRV size: " << spirvBlob->bytecode.size() << " bytes\n";
   ```

4. **Compare with native compilers** (30 min)
   - Compile same shader with DXC: `dxc -T cs_6_5 -E computeMain test.hlsl`
   - Compile with glslangValidator
   - Compare bytecode sizes (should be similar, within 10%)

5. **Check reflection** (15 min)
   - Verify `outputTexture` appears in bindings
   - Should be: `u0, space0` (UAV register 0)

### Deliverables
- `test_compilation_only.slang`
- Compilation test results in `RESULTS.md`

### Success Criteria
- ✅ Compiles to DXIL and SPIRV without errors
- ✅ Bytecode size reasonable (< 10KB for this simple shader)
- ✅ Reflection shows outputTexture at u0, space0
- ✅ No Slang diagnostics/warnings

---

## Phase 1.2: Minimal RT Shader

**Duration:** 1 day  
**Goal:** Simplest possible RT shader with barycentric coloring

### Shader Design: `minimal_rt.slang`

```slang
// Ray tracing acceleration structure
RaytracingAccelerationStructure scene : register(t0, space0);

// Output texture
RWTexture2D<float4> outputTexture : register(u0, space0);

// Camera parameters
cbuffer CameraParams : register(b0, space0)
{
    float4x4 invViewProj;
    float3 cameraPosition;
    uint frameCount;
};

// Primary ray payload
struct Payload
{
    float3 color;
};

// Shadow ray payload
struct ShadowPayload
{
    bool hit;
};

// ========== Ray Generation Shader ==========

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;
    
    // Calculate normalized screen coordinates [-1, 1]
    float2 screenPos = (float2(pixel) + 0.5) / float2(dims);
    screenPos = screenPos * 2.0 - 1.0;
    screenPos.y = -screenPos.y; // Flip Y for screen space
    
    // Calculate ray direction from camera
    float4 target = mul(invViewProj, float4(screenPos, 1.0, 1.0));
    target /= target.w;
    
    float3 rayOrigin = cameraPosition;
    float3 rayDir = normalize(target.xyz - cameraPosition);
    
    // Setup ray
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    
    // Trace ray
    Payload payload;
    payload.color = float3(0, 0, 0);
    
    TraceRay(
        scene,              // Acceleration structure
        RAY_FLAG_NONE,      // Ray flags
        0xFF,               // Instance inclusion mask
        0,                  // Ray contribution to hit group index
        1,                  // Multiplier for geometry contribution to hit group index
        0,                  // Miss shader index
        ray,
        payload
    );
    
    // Write result
    outputTexture[pixel] = float4(payload.color, 1.0);
}

// ========== Miss Shader ==========

[shader("miss")]
void Miss(inout Payload payload)
{
    // Gray background
    payload.color = float3(0.1, 0.1, 0.1);
}

// ========== Closest Hit Shader ==========

[shader("closesthit")]
void ClosestHit(
    inout Payload payload,
    in BuiltInTriangleIntersectionAttributes attrib)
{
    // Color based on barycentric coordinates
    float3 barycentrics = float3(
        1.0 - attrib.barycentrics.x - attrib.barycentrics.y,
        attrib.barycentrics.x,
        attrib.barycentrics.y
    );
    
    payload.color = barycentrics;
}

// ========== Shadow Miss Shader ==========

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.hit = false;
}
```

### Tasks

1. **Write shader** (1 hour)
   - Type/adapt above shader
   - Match your current camera parameter structure
   - Ensure payload structures match your current implementation

2. **Compile to DXIL library** (30 min)
   ```cpp
   auto source = SlangShaderCompiler::loadShaderSource("minimal_rt.slang");
   auto dxilBlobs = compiler.compileHLSLToDXILLibrary(*source);
   
   assert(dxilBlobs.has_value());
   assert(dxilBlobs->size() == 4); // RayGen, Miss, ClosestHit, ShadowMiss
   
   for (auto& blob : *dxilBlobs) {
       std::cout << "Entry point: " << blob.entryPoint 
                 << " Size: " << blob.bytecode.size() << " bytes\n";
   }
   ```

3. **Compile to SPIRV library** (30 min)
   ```cpp
   auto spirvBlobs = compiler.compileToSPIRVLibrary(
       *source, SLANG_SOURCE_LANGUAGE_SLANG
   );
   
   assert(spirvBlobs.has_value());
   assert(spirvBlobs->size() == 4);
   ```

4. **Verify reflection** (30 min)
   - Check that all expected bindings appear:
     - `scene` at t0, space0
     - `outputTexture` at u0, space0
     - `CameraParams` at b0, space0

### Deliverables
- `minimal_rt.slang`
- Compilation test results

### Success Criteria
- ✅ All 4 entry points compile successfully
- ✅ DXIL and SPIRV blobs generated
- ✅ Reflection shows correct bindings
- ✅ No compilation errors or warnings

---

## Phase 1.3: DXR Pipeline Integration

**Duration:** 1-2 days  
**Goal:** Create D3D12 RT pipeline using Slang shaders, render barycentric-colored mesh

### Architecture

**File Structure:**
```
backends/dxr/
├── render_dxr.h
├── render_dxr.cpp (existing)
└── render_dxr_slang.cpp (new - Slang path)
```

**Conditional Compilation:**
```cpp
#ifdef ENABLE_DXR_SLANG
    #include "render_dxr_slang.cpp"
#else
    // Use existing HLSL→DXC path
#endif
```

### Implementation Plan

#### Task 1.3.1: Load Compiled Shaders (2 hours)

**File:** `backends/dxr/render_dxr_slang.cpp`

```cpp
#include "../../util/slang_shader_compiler.h"

namespace chameleonrt {

struct SlangCompiledShaders {
    std::vector<uint8_t> rayGenBlob;
    std::vector<uint8_t> missBlob;
    std::vector<uint8_t> closestHitBlob;
    std::vector<uint8_t> shadowMissBlob;
};

SlangCompiledShaders loadSlangShaders() {
    SlangShaderCompiler compiler;
    
    // Load shader source
    auto source = SlangShaderCompiler::loadShaderSource(
        "backends/shaders/minimal_rt.slang"
    );
    if (!source) {
        throw std::runtime_error("Failed to load shader source");
    }
    
    // Compile to DXIL library
    auto blobs = compiler.compileHLSLToDXILLibrary(*source);
    if (!blobs) {
        throw std::runtime_error("Shader compilation failed: " + 
                                compiler.getLastError());
    }
    
    // Extract individual entry points
    SlangCompiledShaders result;
    for (auto& blob : *blobs) {
        if (blob.entryPoint == "RayGen") {
            result.rayGenBlob = std::move(blob.bytecode);
        } else if (blob.entryPoint == "Miss") {
            result.missBlob = std::move(blob.bytecode);
        } else if (blob.entryPoint == "ClosestHit") {
            result.closestHitBlob = std::move(blob.bytecode);
        } else if (blob.entryPoint == "ShadowMiss") {
            result.shadowMissBlob = std::move(blob.bytecode);
        }
    }
    
    return result;
}

} // namespace chameleonrt
```

**Validation:**
- Print blob sizes
- Verify all 4 blobs are non-empty

#### Task 1.3.2: Create RT Pipeline State Object (4 hours)

```cpp
void RenderDXR::createRTPipeline_Slang() {
    auto shaders = loadSlangShaders();
    
    // Create DXIL library subobjects
    CD3DX12_STATE_OBJECT_DESC raytracingPipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};
    
    // RayGen library
    auto rayGenLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE rayGenBytecode = {
        shaders.rayGenBlob.data(),
        shaders.rayGenBlob.size()
    };
    rayGenLib->SetDXILLibrary(&rayGenBytecode);
    rayGenLib->DefineExport(L"RayGen");
    
    // Miss library
    auto missLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE missBytecode = {
        shaders.missBlob.data(),
        shaders.missBlob.size()
    };
    missLib->SetDXILLibrary(&missBytecode);
    missLib->DefineExport(L"Miss");
    
    // ClosestHit library
    auto hitLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE hitBytecode = {
        shaders.closestHitBlob.data(),
        shaders.closestHitBlob.size()
    };
    hitLib->SetDXILLibrary(&hitBytecode);
    hitLib->DefineExport(L"ClosestHit");
    
    // ShadowMiss library
    auto shadowMissLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE shadowMissBytecode = {
        shaders.shadowMissBlob.data(),
        shaders.shadowMissBlob.size()
    };
    shadowMissLib->SetDXILLibrary(&shadowMissBytecode);
    shadowMissLib->DefineExport(L"ShadowMiss");
    
    // Hit group (ClosestHit only, no AnyHit or Intersection)
    auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitGroup->SetClosestHitShaderImport(L"ClosestHit");
    hitGroup->SetHitGroupExport(L"HitGroup");
    hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    
    // Shader config (payload sizes)
    auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payloadSize = sizeof(float) * 3;  // Payload.color
    UINT attribSize = sizeof(float) * 2;   // Built-in triangle attributes
    shaderConfig->Config(payloadSize, attribSize);
    
    // Pipeline config (max recursion depth)
    auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    pipelineConfig->Config(1); // No recursion yet
    
    // Global root signature
    auto globalRootSig = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSig->SetRootSignature(globalRootSignature.Get());
    
    // Create state object
    CHECK_ERR(device->CreateStateObject(
        raytracingPipeline,
        IID_PPV_ARGS(&rtpso)
    ));
}
```

**Validation:**
- State object creation succeeds
- No D3D12 debug layer errors
- Can query shader identifiers from state object

#### Task 1.3.3: Build Shader Binding Table (2 hours)

```cpp
void RenderDXR::buildShaderBindingTable_Slang() {
    ComPtr<ID3D12StateObjectProperties> rtpsoProps;
    rtpso.As(&rtpsoProps);
    
    // Get shader identifiers
    void* rayGenID = rtpsoProps->GetShaderIdentifier(L"RayGen");
    void* missID = rtpsoProps->GetShaderIdentifier(L"Miss");
    void* shadowMissID = rtpsoProps->GetShaderIdentifier(L"ShadowMiss");
    void* hitGroupID = rtpsoProps->GetShaderIdentifier(L"HitGroup");
    
    // Build SBT (same as current implementation, just different IDs)
    // ... rest of SBT building code
}
```

#### Task 1.3.4: Dispatch Rays (1 hour)

```cpp
void RenderDXR::render_Slang() {
    // Same dispatch code as current implementation
    cmdList->SetComputeRootSignature(globalRootSignature.Get());
    cmdList->SetPipelineState1(rtpso.Get());
    
    // Bind resources (scene, output texture, camera CB)
    // ... same as current code
    
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    // ... fill in SBT addresses
    
    cmdList->DispatchRays(&dispatchDesc);
}
```

### Testing Strategy

1. **Start simple:** Single triangle/cube scene
2. **Enable D3D12 debug layer** - catch any errors early
3. **Compare with native:** Render same scene with current HLSL path
4. **Visual validation:** Should see barycentric-colored mesh

### Deliverables
- `backends/dxr/render_dxr_slang.cpp` (or integrated into render_dxr.cpp)
- Screenshots in `RESULTS.md`
- Performance comparison

### Success Criteria
- ✅ DXR pipeline creates successfully
- ✅ No D3D12 errors or warnings
- ✅ Renders barycentric-colored mesh
- ✅ Output matches expected (rainbow triangles)

---

## Phase 1.4: Vulkan RT Pipeline Integration

**Duration:** 1-2 days  
**Goal:** Create Vulkan RT pipeline using Slang-compiled SPIRV

### Implementation Plan

#### Task 1.4.1: Load Compiled Shaders (1 hour)

```cpp
// Similar to DXR, but load SPIRV blobs
auto spirvBlobs = compiler.compileToSPIRVLibrary(
    *source, SLANG_SOURCE_LANGUAGE_SLANG
);
```

#### Task 1.4.2: Create Shader Modules (1 hour)

```cpp
std::vector<VkShaderModule> shaderModules;

for (auto& blob : *spirvBlobs) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = blob.bytecode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(blob.bytecode.data());
    
    VkShaderModule shaderModule;
    vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    shaderModules.push_back(shaderModule);
}
```

#### Task 1.4.3: Create RT Pipeline (3 hours)

```cpp
// Shader stages
std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
// ... create stage for each entry point

// Shader groups
std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
// RayGen group
// Miss group
// Hit group
// Shadow miss group

// Create pipeline
VkRayTracingPipelineCreateInfoKHR pipelineInfo = {};
pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
pipelineInfo.stageCount = shaderStages.size();
pipelineInfo.pStages = shaderStages.data();
pipelineInfo.groupCount = shaderGroups.size();
pipelineInfo.pGroups = shaderGroups.data();
pipelineInfo.maxPipelineRayRecursionDepth = 1;
pipelineInfo.layout = pipelineLayout;

vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 
                               1, &pipelineInfo, nullptr, &rtPipeline);
```

#### Task 1.4.4: Build SBT and Trace (2 hours)

```cpp
// Get shader handles
// Build SBT buffer
// vkCmdTraceRaysKHR(...)
```

### Deliverables
- Vulkan RT integration code
- Screenshots comparing with DXR output

### Success Criteria
- ✅ Vulkan pipeline creates successfully
- ✅ No validation layer errors
- ✅ Renders identical output to DXR
- ✅ Pixel-perfect match (or within float precision)

---

## Phase 1.5: Global Buffer Access

**Duration:** 1-2 days  
**Goal:** Access unified global geometry buffers, compute normals

### Shader Update: `flat_shading_with_buffers.slang`

```slang
// Global geometry buffers (unified architecture)
StructuredBuffer<float3> globalVertices : register(t10, space0);
StructuredBuffer<uint3> globalIndices : register(t11, space0);

// Mesh descriptors
struct MeshDesc {
    uint vbOffset;
    uint ibOffset;
    uint materialID;
    uint pad;
};
StructuredBuffer<MeshDesc> meshDescs : register(t14, space0);

// Per-hit-group data (mesh index)
cbuffer HitGroupData : register(b2, space0) {
    uint meshDescIndex;
};

[shader("closesthit")]
void ClosestHit(
    inout Payload payload,
    in BuiltInTriangleIntersectionAttributes attrib)
{
    // Get mesh descriptor
    MeshDesc mesh = meshDescs[meshDescIndex];
    
    // Get triangle indices
    uint primitiveID = PrimitiveIndex();
    uint3 indices = globalIndices[mesh.ibOffset + primitiveID];
    
    // Get vertex positions
    float3 v0 = globalVertices[mesh.vbOffset + indices.x];
    float3 v1 = globalVertices[mesh.vbOffset + indices.y];
    float3 v2 = globalVertices[mesh.vbOffset + indices.z];
    
    // Compute geometric normal
    float3 e1 = v1 - v0;
    float3 e2 = v2 - v0;
    float3 geometricNormal = normalize(cross(e1, e2));
    
    // Shade based on normal (map [-1,1] to [0,1] for visualization)
    payload.color = geometricNormal * 0.5 + 0.5;
}
```

### Tasks

1. **Update shader** (30 min)
2. **Verify reflection** (30 min)
   - Check bindings: t10, t11, t14, b2 all in space0
3. **Update DXR resource binding** (2 hours)
   - Bind global buffers to descriptor heap
   - Populate hit group local root parameters with meshDescIndex
4. **Update Vulkan resource binding** (2 hours)
   - Bind global buffers to descriptor sets
   - Update SBT with per-geometry data
5. **Test rendering** (1 hour)
   - Should see normal-shaded mesh
   - Compare with current normal-shaded output

### Deliverables
- `flat_shading_with_buffers.slang`
- Screenshots of normal-shaded rendering
- Comparison with current implementation

### Success Criteria
- ✅ Normals computed correctly (facing correct direction)
- ✅ Output identical to current normal-shaded mode
- ✅ Reflection shows all buffers bound correctly
- ✅ No buffer access errors

---

## Integration Strategy

### CMake Configuration

**File:** `CMakeLists.txt`

```cmake
option(ENABLE_DXR_SLANG "Use Slang for DXR shaders (experimental)" OFF)
option(ENABLE_VULKAN_SLANG "Use Slang for Vulkan shaders (experimental)" OFF)

if(ENABLE_DXR_SLANG OR ENABLE_VULKAN_SLANG)
    find_package(Slang REQUIRED)
endif()

if(ENABLE_DXR_SLANG)
    target_compile_definitions(chameleonrt PRIVATE ENABLE_DXR_SLANG)
    message(STATUS "DXR will use Slang shaders")
endif()

if(ENABLE_VULKAN_SLANG)
    target_compile_definitions(chameleonrt PRIVATE ENABLE_VULKAN_SLANG)
    message(STATUS "Vulkan will use Slang shaders")
endif()
```

### Build Commands

```powershell
# Test Slang path
cmake -B build -DENABLE_DXR=ON -DENABLE_DXR_SLANG=ON -DSlang_ROOT=C:\dev\slang\build\Debug
cmake --build build --config Debug

# Test native path (default)
cmake -B build -DENABLE_DXR=ON
cmake --build build --config Debug
```

---

## Validation Strategy

### Visual Validation

For each sub-phase:
1. Render test scene
2. Take screenshot
3. Compare with reference (native path)
4. Verify pixel-perfect match (or understand differences)

### Performance Validation

```powershell
# Benchmark mode
.\build\Debug\chameleonrt.exe dxr cube.obj --slang --benchmark --frames 100
# Record: Average FPS, frame time, ray count

.\build\Debug\chameleonrt.exe dxr cube.obj --native --benchmark --frames 100
# Compare metrics
```

**Acceptance:** Frame time within 1% of native

### Debug Validation

- Enable D3D12 debug layer: No errors/warnings
- Enable Vulkan validation layers: No errors/warnings
- Use RenderDoc to inspect pipeline state, resources, shader execution

---

## Deliverables Checklist

- [ ] `test_compilation_only.slang`
- [ ] `minimal_rt.slang`
- [ ] `flat_shading_with_buffers.slang`
- [ ] DXR integration code
- [ ] Vulkan integration code
- [ ] CMake build configuration
- [ ] `RESULTS.md` with screenshots and benchmarks
- [ ] `ISSUES.md` with known problems/workarounds

---

## Success Criteria (Phase 1 Complete)

### Visual
- ✅ Barycentric coloring works (Phase 1.2-1.4)
- ✅ Normal shading works (Phase 1.5)
- ✅ Output identical between DXR and Vulkan
- ✅ Output identical between Slang and native paths

### Performance
- ✅ Frame time within 1% of native implementation
- ✅ Ray count matches native implementation

### Technical
- ✅ No validation errors
- ✅ Reflection data correct
- ✅ Can toggle Slang/native at build time
- ✅ Code reviewed and committed

**Estimated Time:** 3-5 days

**Next Phase:** Phase 2 (Textures)
