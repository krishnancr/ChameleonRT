# Phase 2: Textures - Detailed Plan

**Status:** 📋 Planning  
**Duration:** 2-3 days  
**Risk Level:** LOW

---

## Objectives

Add texture sampling and material system to Slang shaders. Enable diffuse color rendering from textures without BRDF evaluation. This validates resource binding (texture arrays, samplers) and material data flow.

---

## Prerequisites

- ✅ Phase 1 completed (global buffers working)
- ✅ Test scenes with textured meshes available
- ✅ Current implementation renders textures correctly

---

## Phase 2.1: UV Access and Single Texture

**Duration:** 1 day  
**Goal:** Sample one test texture using interpolated UVs

### Shader Design: `single_texture.slang`

```slang
// Extend Phase 1 shader with UV buffer and textures

// Global geometry buffers (from Phase 1)
StructuredBuffer<float3> globalVertices : register(t10, space0);
StructuredBuffer<uint3> globalIndices : register(t11, space0);
StructuredBuffer<float2> globalUVs : register(t12, space0);      // NEW
StructuredBuffer<MeshDesc> meshDescs : register(t14, space0);

// Textures and sampler
Texture2D textures[] : register(t30, space0);  // Bindless texture array
SamplerState linearSampler : register(s0, space0);

// Camera and scene (same as Phase 1)
RaytracingAccelerationStructure scene : register(t0, space0);
RWTexture2D<float4> outputTexture : register(u0, space0);

cbuffer CameraParams : register(b0, space0) {
    float4x4 invViewProj;
    float3 cameraPosition;
    uint frameCount;
};

cbuffer HitGroupData : register(b2, space0) {
    uint meshDescIndex;
};

// Payload structures (same as Phase 1)
struct Payload {
    float3 color;
};

struct ShadowPayload {
    bool hit;
};

// ========== Helper Functions ==========

float2 interpolateUV(float2 uv0, float2 uv1, float2 uv2, float2 bary) {
    return uv0 * (1.0 - bary.x - bary.y) + uv1 * bary.x + uv2 * bary.y;
}

// ========== Ray Generation ==========

[shader("raygeneration")]
void RayGen() {
    // Same as Phase 1
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;
    
    float2 screenPos = (float2(pixel) + 0.5) / float2(dims);
    screenPos = screenPos * 2.0 - 1.0;
    screenPos.y = -screenPos.y;
    
    float4 target = mul(invViewProj, float4(screenPos, 1.0, 1.0));
    target /= target.w;
    
    float3 rayOrigin = cameraPosition;
    float3 rayDir = normalize(target.xyz - cameraPosition);
    
    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    
    Payload payload;
    payload.color = float3(0, 0, 0);
    
    TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    
    outputTexture[pixel] = float4(payload.color, 1.0);
}

// ========== Miss Shader ==========

[shader("miss")]
void Miss(inout Payload payload) {
    payload.color = float3(0.1, 0.1, 0.1);
}

// ========== Closest Hit ==========

[shader("closesthit")]
void ClosestHit(
    inout Payload payload,
    in BuiltInTriangleIntersectionAttributes attrib)
{
    // Get mesh info
    MeshDesc mesh = meshDescs[meshDescIndex];
    uint primitiveID = PrimitiveIndex();
    uint3 indices = globalIndices[mesh.ibOffset + primitiveID];
    
    // Get UVs
    float2 uv0 = globalUVs[mesh.uvOffset + indices.x];
    float2 uv1 = globalUVs[mesh.uvOffset + indices.y];
    float2 uv2 = globalUVs[mesh.uvOffset + indices.z];
    
    // Interpolate UV
    float2 uv = interpolateUV(uv0, uv1, uv2, attrib.barycentrics);
    
    // Sample first texture (test texture at index 0)
    float3 textureColor = textures[0].SampleLevel(linearSampler, uv, 0).rgb;
    
    payload.color = textureColor;
}

// ========== Shadow Miss ==========

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload) {
    payload.hit = false;
}
```

### Tasks

#### Task 2.1.1: Update MeshDesc Structure (30 min)

**File:** `backends/dxr/mesh_data.h` or similar

Ensure `MeshDesc` includes UV offset:
```cpp
struct MeshDesc {
    uint32_t vbOffset;
    uint32_t ibOffset;
    uint32_t uvOffset;  // NEW - offset into global UV buffer
    uint32_t materialID;
};
```

Update both DXR and Vulkan to populate this field.

#### Task 2.1.2: Write Shader (1 hour)

Copy above shader to `Phase2_Textures/single_texture.slang`.

#### Task 2.1.3: Compile and Verify Reflection (30 min)

```cpp
auto blobs = compiler.compileToSPIRVLibrary(*source, SLANG_SOURCE_LANGUAGE_SLANG);

// Check reflection for new bindings
// Expected: globalUVs at t12, textures at t30, sampler at s0
```

#### Task 2.1.4: Bind UV Buffer (DXR) (1 hour)

**File:** `backends/dxr/render_dxr.cpp`

```cpp
// Create global UV buffer (similar to vertices/indices)
D3D12_RESOURCE_DESC uvBufferDesc = {};
uvBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
uvBufferDesc.Width = totalUVs * sizeof(float) * 2;
uvBufferDesc.Height = 1;
uvBufferDesc.DepthOrArraySize = 1;
uvBufferDesc.MipLevels = 1;
uvBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
uvBufferDesc.SampleDesc.Count = 1;
uvBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
uvBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

device->CreateCommittedResource(
    &defaultHeapProps,
    D3D12_HEAP_FLAG_NONE,
    &uvBufferDesc,
    D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr,
    IID_PPV_ARGS(&globalUVBuffer)
);

// Upload UVs (via staging buffer)
// ...

// Create SRV in descriptor heap
D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
srvDesc.Format = DXGI_FORMAT_UNKNOWN;
srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
srvDesc.Buffer.FirstElement = 0;
srvDesc.Buffer.NumElements = totalUVs;
srvDesc.Buffer.StructureByteStride = sizeof(float) * 2;

device->CreateShaderResourceView(
    globalUVBuffer.Get(),
    &srvDesc,
    descriptorHeap.GetCPUDescriptorHandle(12) // t12
);
```

#### Task 2.1.5: Bind Test Texture (DXR) (1 hour)

```cpp
// Create test texture (checkerboard or UV gradient)
// ... create D3D12 texture resource

// Create SRV at t30
D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
texSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
texSrvDesc.Texture2D.MipLevels = 1;

device->CreateShaderResourceView(
    testTexture.Get(),
    &texSrvDesc,
    descriptorHeap.GetCPUDescriptorHandle(30) // t30
);

// Create sampler at s0
D3D12_SAMPLER_DESC samplerDesc = {};
samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
samplerDesc.MaxAnisotropy = 1;
samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
samplerDesc.MinLOD = 0;
samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

device->CreateSampler(
    &samplerDesc,
    samplerHeap.GetCPUDescriptorHandle(0) // s0
);
```

#### Task 2.1.6: Bind UV Buffer (Vulkan) (1 hour)

Similar to DXR but using Vulkan APIs:
- Create `VkBuffer` for UVs
- Update descriptor set binding 12 with UV buffer
- Create texture and sampler
- Update descriptor set binding 30 with texture
- Update descriptor set binding 0 (sampler set) with sampler

#### Task 2.1.7: Test Rendering (30 min)

Render test scene with single texture:
- Should see checkerboard or UV gradient on mesh
- UVs should be continuous across triangles
- No seams or artifacts

### Deliverables (Phase 2.1)
- [ ] `single_texture.slang`
- [ ] UV buffer binding (DXR and Vulkan)
- [ ] Test texture creation and binding
- [ ] Screenshots showing textured mesh

### Success Criteria
- ✅ Texture samples correctly
- ✅ UVs interpolate smoothly
- ✅ No texture coordinate seams
- ✅ Works on both DXR and Vulkan

---

## Phase 2.2: Material System

**Duration:** 1-2 days  
**Goal:** Use material data to select correct texture per mesh

### Shader Design: `textured_with_materials.slang`

```slang
// Add material buffer

struct MaterialParams {
    uint baseColorTexIdx;
    uint normalTexIdx;
    uint metallicRoughnessTexIdx;
    float metallicFactor;
    float roughnessFactor;
    uint pad[3];
};

StructuredBuffer<MaterialParams> materials : register(t1, space0);

[shader("closesthit")]
void ClosestHit(
    inout Payload payload,
    in BuiltInTriangleIntersectionAttributes attrib)
{
    // Get mesh and material
    MeshDesc mesh = meshDescs[meshDescIndex];
    MaterialParams mat = materials[mesh.materialID];
    
    // Get UVs
    uint primitiveID = PrimitiveIndex();
    uint3 indices = globalIndices[mesh.ibOffset + primitiveID];
    float2 uv0 = globalUVs[mesh.uvOffset + indices.x];
    float2 uv1 = globalUVs[mesh.uvOffset + indices.y];
    float2 uv2 = globalUVs[mesh.uvOffset + indices.z];
    float2 uv = interpolateUV(uv0, uv1, uv2, attrib.barycentrics);
    
    // Sample base color from material's texture
    Texture2D baseColorTex = textures[mat.baseColorTexIdx];
    float3 baseColor = baseColorTex.SampleLevel(linearSampler, uv, 0).rgb;
    
    // Just return diffuse color (no lighting yet)
    payload.color = baseColor;
}
```

### Tasks

#### Task 2.2.1: Define Material Structure (30 min)

Match your current material structure. Ensure alignment matches between CPU and GPU.

#### Task 2.2.2: Update Shader (30 min)

Add material buffer and update ClosestHit to use it.

#### Task 2.2.3: Upload Material Data (DXR) (1 hour)

```cpp
// Create material buffer
std::vector<MaterialParams> materialsData;
for (auto& mat : scene.materials) {
    MaterialParams mp;
    mp.baseColorTexIdx = mat.baseColorTextureIndex + 30; // Offset to t30+
    mp.normalTexIdx = mat.normalTextureIndex + 30;
    mp.metallicRoughnessTexIdx = mat.metallicRoughnessIndex + 30;
    mp.metallicFactor = mat.metallicFactor;
    mp.roughnessFactor = mat.roughnessFactor;
    materialsData.push_back(mp);
}

// Create buffer and upload
// ... similar to other buffers

// Create SRV at t1
device->CreateShaderResourceView(
    materialBuffer.Get(),
    &srvDesc,
    descriptorHeap.GetCPUDescriptorHandle(1)
);
```

#### Task 2.2.4: Load All Scene Textures (DXR) (2 hours)

```cpp
// For each texture in scene
for (size_t i = 0; i < scene.textures.size(); ++i) {
    // Load image data
    auto imageData = loadImage(scene.textures[i].path);
    
    // Create D3D12 texture
    auto texture = createTexture(imageData);
    
    // Create SRV at t30 + i
    device->CreateShaderResourceView(
        texture.Get(),
        &texSrvDesc,
        descriptorHeap.GetCPUDescriptorHandle(30 + i)
    );
    
    sceneTextures.push_back(texture);
}
```

**Note:** Ensure descriptor heap is large enough for all textures.

#### Task 2.2.5: Upload Material Data (Vulkan) (1 hour)

Similar to DXR but using Vulkan buffer and descriptor set updates.

#### Task 2.2.6: Load All Scene Textures (Vulkan) (2 hours)

Create `VkImage` for each texture, update descriptor set array binding.

#### Task 2.2.7: Test Multi-Material Scene (1 hour)

Use scene with multiple materials (e.g., Sponza, San Miguel):
- Each mesh should show correct texture
- Material transitions should be correct
- No texture swapping artifacts

### Deliverables (Phase 2.2)
- [ ] `textured_with_materials.slang`
- [ ] Material buffer implementation
- [ ] Full scene texture loading
- [ ] Screenshots of multi-material scenes

### Success Criteria
- ✅ All materials render with correct textures
- ✅ Material assignments match current implementation
- ✅ Texture array indexing works correctly
- ✅ Output identical to current textured mode (no lighting)

---

## Validation Strategy

### Visual Tests

1. **UV Continuity Test** (Phase 2.1)
   - Render cube with UV gradient texture
   - Check for seams at triangle boundaries
   - Should be smooth color gradient

2. **Multi-Texture Test** (Phase 2.2)
   - Render scene with 10+ different materials
   - Verify each mesh has correct texture
   - Compare with current implementation screenshot

3. **Texture Filtering Test** (Phase 2.1)
   - Render at different camera distances
   - Check mipmapping works (if enabled)
   - No aliasing artifacts

### Performance Tests

```powershell
# Benchmark textured scene
.\chameleonrt.exe dxr sponza.obj --slang --benchmark
# Should be within 1-2% of Phase 1 (buffer access overhead minimal)
```

### Debug Validation

Use RenderDoc/Nsight to inspect:
- Descriptor heap contents (DXR)
- Descriptor set bindings (Vulkan)
- Texture sampling (check correct texture is accessed)
- UV values (should be in [0,1] range typically)

---

## Known Issues / Risks

### Risk 1: Bindless Texture Array Support
**Issue:** Not all GPUs support large unbounded arrays  
**Mitigation:** 
- Start with small array (64 textures)
- Test on target hardware
- Fallback: Use descriptor indexing if needed

### Risk 2: Texture Format Mismatches
**Issue:** Slang might emit different format assumptions  
**Mitigation:**
- Explicitly specify DXGI_FORMAT / VkFormat
- Validate with reflection API

### Risk 3: Sampler State Differences
**Issue:** Slang default sampler vs. explicit sampler  
**Mitigation:**
- Always use explicit SamplerState declaration
- Match filtering mode with current implementation

---

## Integration with Phase 1

All Phase 1 features remain:
- Global buffer access still works
- Normal computation available (can toggle for debugging)
- Can render with/without textures

Build flags can control texture path:
```cpp
#ifdef ENABLE_TEXTURES
    // Use textured_with_materials.slang
#else
    // Use flat_shading_with_buffers.slang
#endif
```

---

## Deliverables Checklist

- [ ] `single_texture.slang` shader
- [ ] `textured_with_materials.slang` shader
- [ ] UV buffer implementation (DXR + Vulkan)
- [ ] Material buffer implementation (DXR + Vulkan)
- [ ] Texture loading and binding (DXR + Vulkan)
- [ ] `RESULTS.md` with screenshots
- [ ] Performance comparison data

---

## Success Criteria (Phase 2 Complete)

### Visual
- ✅ Single texture sampling works correctly
- ✅ Multi-material scenes render correctly
- ✅ Output matches current textured implementation
- ✅ DXR and Vulkan produce identical results

### Performance
- ✅ Frame time within 1-2% of Phase 1
- ✅ Texture sampling overhead minimal

### Technical
- ✅ Bindless texture array works
- ✅ Material data flows correctly
- ✅ UV interpolation correct
- ✅ No validation errors

**Estimated Time:** 2-3 days

**Next Phase:** Phase 3 (Full BRDF)
