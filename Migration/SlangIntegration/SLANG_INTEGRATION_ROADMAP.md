# Slang Integration Roadmap for ChameleonRT

**Goal:** Migrate ChameleonRT's ray tracing shaders from native HLSL/GLSL to unified Slang shaders that compile to both DXR (DXIL) and Vulkan RT (SPIRV).

**Strategy:** Incremental validation at each step. Every phase must produce identical visual output to current implementation.

**Start Date:** October 14, 2025  
**Estimated Duration:** 2-3 weeks

---

## Phase 0: Utility Upgrade ⚙️

**Directory:** `Migration/SlangIntegration/SlangShaders/Phase0_UtilityUpgrade/`

**Duration:** 2-3 hours

**Goal:** Enhance `SlangShaderCompiler` with missing features needed for shader development

### Objectives
1. Add `searchPaths` parameter to all single-entry compilation functions
2. Implement `compileToSPIRVLibrary()` for Vulkan RT
3. Implement basic reflection data extraction
4. Create validation test harness

### Deliverables
- [ ] Updated `util/slang_shader_compiler.h`
- [ ] Updated `util/slang_shader_compiler.cpp`
- [ ] Test program: `test_slang_compiler.cpp`
- [ ] Validation: Compile test shader to DXIL and SPIRV, verify reflection output

### Success Criteria
- ✅ Can compile HLSL→DXIL with include paths
- ✅ Can compile HLSL→SPIRV library with multiple entry points
- ✅ Reflection output shows correct bindings (t10, space0, etc.)
- ✅ No compilation warnings or errors

### Dependencies
- None (foundation phase)

### Risk Level
**LOW** - Incremental improvements to existing validated code

---

## Phase 1: Flat Shading 🎨

**Directory:** `Migration/SlangIntegration/SlangShaders/Phase1_FlatShading/`

**Duration:** 3-5 days

**Goal:** Create minimal Slang RT shaders and validate end-to-end pipeline creation for both DXR and Vulkan

### Sub-Phases

#### Phase 1.1: Compilation Infrastructure Test (Day 1)
**Goal:** Validate Slang compilation without RT pipeline complexity

**Tasks:**
- [ ] Write minimal test shader (checkerboard pattern, no ray tracing)
- [ ] Compile to DXIL and SPIRV via `SlangShaderCompiler`
- [ ] Compare output sizes with DXC/glslang reference
- [ ] Verify no validation errors

**Deliverable:** `Phase1_FlatShading/test_compilation_only.slang`

#### Phase 1.2: Minimal RT Shader (Day 1-2)
**Goal:** Simplest possible RT shader with hardcoded colors

**Shader Structure:**
```slang
[shader("raygeneration")]
void RayGen() {
    // Calculate pixel coordinates
    // Trace ray
    // Write output texture
}

[shader("miss")]
void Miss(inout Payload payload) {
    payload.color = float3(0.1, 0.1, 0.1); // Gray background
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attrib) {
    // Return barycentric coordinates as color (classic debug view)
    payload.color = float3(barycentrics, ...);
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload) {
    payload.hit = false;
}
```

**Tasks:**
- [ ] Write shader with 4 entry points (RayGen, Miss, ClosestHit, ShadowMiss)
- [ ] Define payload structures (primary + shadow)
- [ ] Compile to DXIL library and SPIRV library
- [ ] Verify all entry points extracted correctly

**Deliverable:** `Phase1_FlatShading/minimal_rt.slang`

#### Phase 1.3: DXR Pipeline Integration (Day 2-3)
**Goal:** Create DXR RT pipeline state object using Slang-compiled shaders

**Tasks:**
- [ ] Load compiled DXIL blobs from `SlangShaderCompiler`
- [ ] Create `D3D12_STATE_OBJECT_DESC` with Slang shaders
- [ ] Set up shader identifier table (SBT) with per-geometry hit groups
- [ ] Bind acceleration structure and output texture
- [ ] Dispatch rays

**Expected Output:** Barycentric-colored mesh (rainbow triangle colors)

**Integration Points:**
- Modify `backends/dxr/render_dxr.cpp::render_dxr::initialize()` to use Slang path
- Add `#ifdef ENABLE_DXR_SLANG` guards (keep existing path working)

**Deliverable:** DXR rendering with Slang shaders

#### Phase 1.4: Vulkan RT Pipeline Integration (Day 3-4)
**Goal:** Create Vulkan RT pipeline using Slang-compiled SPIRV

**Tasks:**
- [ ] Load compiled SPIRV blobs from `SlangShaderCompiler`
- [ ] Create `VkShaderModule` for each entry point
- [ ] Set up `VkRayTracingShaderGroupCreateInfoKHR` (raygen, miss, hit, shadow)
- [ ] Create `VkRayTracingPipelineKHR`
- [ ] Build shader binding table (SBT)
- [ ] Bind descriptor sets and trace rays

**Expected Output:** Identical barycentric-colored mesh to DXR

**Integration Points:**
- Modify `backends/vulkan/render_vulkan.cpp::render_vulkan::initialize()` to use Slang path
- Add `#ifdef ENABLE_VULKAN_SLANG` guards

**Deliverable:** Vulkan RT rendering with Slang shaders

#### Phase 1.5: Global Buffer Access (Day 4-5)
**Goal:** Access global geometry buffers, compute geometric normals

**Shader Additions:**
```slang
// Global buffers matching your unified architecture
StructuredBuffer<float3> globalVertices : register(t10, space0);
StructuredBuffer<uint3> globalIndices : register(t11, space0);
StructuredBuffer<MeshDesc> meshDescs : register(t14, space0);

cbuffer HitGroupData : register(b2, space0) {
    uint meshDescIndex;
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attrib) {
    MeshDesc mesh = meshDescs[meshDescIndex];
    uint3 idx = globalIndices[mesh.ibOffset + PrimitiveIndex()];
    float3 v0 = globalVertices[idx.x];
    float3 v1 = globalVertices[idx.y];
    float3 v2 = globalVertices[idx.z];
    float3 normal = normalize(cross(v1 - v0, v2 - v0));
    payload.color = normal * 0.5 + 0.5; // Map to [0,1] for visualization
}
```

**Tasks:**
- [ ] Update shader with global buffer declarations
- [ ] Verify reflection shows correct bindings (t10, t11, t14, b2, space0)
- [ ] Test DXR: Bind global buffers to descriptor heap
- [ ] Test Vulkan: Bind global buffers to descriptor sets
- [ ] Validate normal-shaded output

**Expected Output:** Normal-shaded mesh (like current implementation)

**Deliverable:** `Phase1_FlatShading/flat_shading_with_buffers.slang`

### Deliverables (Phase 1 Complete)
- [ ] `minimal_rt.slang` - Basic RT shader
- [ ] `flat_shading_with_buffers.slang` - With global buffer access
- [ ] DXR integration code (conditional compilation)
- [ ] Vulkan integration code (conditional compilation)
- [ ] Screenshot comparisons (Slang vs. current implementation)
- [ ] Performance benchmark (frame time should be identical)

### Success Criteria
- ✅ Identical visual output: DXR Slang, Vulkan Slang, DXR native, Vulkan native
- ✅ No validation layer errors (D3D12 debug layer, Vulkan validation)
- ✅ Frame time within 1% of native path
- ✅ Reflection data matches expected bindings
- ✅ Can toggle between Slang and native paths at build time

### Dependencies
- Phase 0 completed (utility upgrade)

### Risk Level
**MEDIUM** - RT pipeline creation is complex, but isolated from shading logic

---

## Phase 2: Textures 🖼️

**Directory:** `Migration/SlangIntegration/SlangShaders/Phase2_Textures/`

**Duration:** 2-3 days

**Goal:** Add texture sampling and material system (diffuse only, no BRDF)

### Sub-Phases

#### Phase 2.1: UV Access and Single Texture (Day 1)
**Goal:** Sample one test texture using interpolated UVs

**Shader Additions:**
```slang
StructuredBuffer<float2> globalUVs : register(t12, space0);

Texture2D textures[] : register(t30, space0);  // Bindless texture array
SamplerState sampler : register(s0, space0);

[shader("closesthit")]
void ClosestHit(inout Payload payload, ...) {
    // Interpolate UVs
    MeshDesc mesh = meshDescs[meshDescIndex];
    uint3 idx = globalIndices[mesh.ibOffset + PrimitiveIndex()];
    float2 uv0 = globalUVs[mesh.uvOffset + idx.x];
    float2 uv1 = globalUVs[mesh.uvOffset + idx.y];
    float2 uv2 = globalUVs[mesh.uvOffset + idx.z];
    float2 uv = interpolate(uv0, uv1, uv2, attrib.barycentrics);
    
    // Sample first texture
    payload.color = textures[0].SampleLevel(sampler, uv, 0).rgb;
}
```

**Tasks:**
- [ ] Add UV buffer and texture array to shader
- [ ] Bind test texture to DXR descriptor heap
- [ ] Bind test texture to Vulkan descriptor set
- [ ] Verify texture sampling works

**Expected Output:** Test texture (checkerboard or UV gradient) displayed on mesh

**Deliverable:** `Phase2_Textures/single_texture.slang`

#### Phase 2.2: Material System (Day 2-3)
**Goal:** Use material data to select correct texture per mesh

**Shader Additions:**
```slang
struct MaterialParams {
    uint baseColorTexIdx;
    float metallicValue;
    float roughnessValue;
    // ... match your current material structure
};

StructuredBuffer<MaterialParams> materials : register(t1, space0);

[shader("closesthit")]
void ClosestHit(inout Payload payload, ...) {
    MeshDesc mesh = meshDescs[meshDescIndex];
    MaterialParams mat = materials[mesh.materialID];
    
    float2 uv = interpolateUV(...);
    
    // Sample base color from material's texture
    Texture2D baseColorTex = textures[mat.baseColorTexIdx];
    float3 baseColor = baseColorTex.SampleLevel(sampler, uv, 0).rgb;
    
    payload.color = baseColor; // Just diffuse color, no lighting yet
}
```

**Tasks:**
- [ ] Add material buffer to shader
- [ ] Update both backends to bind material data
- [ ] Test with multi-material scenes
- [ ] Verify correct texture per mesh

**Expected Output:** Full scene with correct textures (no lighting, just albedo)

**Deliverable:** `Phase2_Textures/textured_with_materials.slang`

### Deliverables (Phase 2 Complete)
- [ ] `single_texture.slang` - Basic texture sampling
- [ ] `textured_with_materials.slang` - Full material system
- [ ] Updated DXR/Vulkan backends with texture binding
- [ ] Screenshot comparisons with current textured output

### Success Criteria
- ✅ Textures display correctly on all meshes
- ✅ Material assignments match current implementation
- ✅ UV coordinates interpolate correctly
- ✅ Bindless texture array works on both backends
- ✅ Performance within 1% of Phase 1

### Dependencies
- Phase 1 completed (buffer access working)

### Risk Level
**LOW** - Texture sampling is well-understood, mainly testing binding setup

---

## Phase 3: Full BRDF 🌟

**Directory:** `Migration/SlangIntegration/SlangShaders/Phase3_FullBRDF/`

**Duration:** 3-5 days

**Goal:** Implement complete Disney BRDF path tracing matching current implementation

### Sub-Phases

#### Phase 3.1: Import Falcor's Disney BRDF Module (Day 1)
**Goal:** Create reusable Slang module for BRDF evaluation

**Tasks:**
- [ ] Extract Disney BRDF from Falcor's `Source/Falcor/Rendering/Materials/`
- [ ] Create `disney_bsdf.slang` module
- [ ] Create `lights.slang` module for light sampling
- [ ] Create `util.slang` module for shared utilities (random numbers, etc.)
- [ ] Test compilation as standalone modules

**Reference Files:**
- Falcor: `Source/Falcor/Rendering/Materials/BxDF.slang`
- Falcor: `Source/Falcor/Rendering/Materials/PBRT/PBRTDiffuseBRDF.slang`
- Your existing: `backends/dxr/disney_bsdf.hlsl`, `lcg_rng.hlsl`, `util.hlsl`

**Deliverable:** 
- `Phase3_FullBRDF/modules/disney_bsdf.slang`
- `Phase3_FullBRDF/modules/lights.slang`
- `Phase3_FullBRDF/modules/util.slang`
- `Phase3_FullBRDF/modules/lcg_rng.slang`

#### Phase 3.2: Direct Lighting (Day 2-3)
**Goal:** Evaluate BRDF with direct light sampling (no recursion yet)

**Shader Structure:**
```slang
import disney_bsdf;
import lights;

[shader("closesthit")]
void ClosestHit(inout Payload payload, ...) {
    // Get material parameters
    MaterialParams mat = materials[mesh.materialID];
    float3 baseColor = textures[mat.baseColorTexIdx].SampleLevel(...);
    
    // Sample lights
    float3 directLight = float3(0);
    for (uint i = 0; i < numLights; i++) {
        LightSample ls = sampleLight(lights[i], ...);
        
        // Trace shadow ray
        ShadowPayload shadow;
        TraceRay(..., shadow);
        
        if (!shadow.hit) {
            // Evaluate BRDF
            DisneyMaterial disneyMat = createDisneyMaterial(baseColor, mat.metallic, mat.roughness);
            float3 brdf = evalDisneyBRDF(disneyMat, wo, ls.wi, normal);
            directLight += brdf * ls.radiance * dot(normal, ls.wi);
        }
    }
    
    payload.color = directLight;
}
```

**Tasks:**
- [ ] Implement light sampling
- [ ] Implement shadow ray tracing
- [ ] Integrate Disney BRDF evaluation
- [ ] Test with single light source
- [ ] Compare output with current direct lighting

**Expected Output:** Direct-lit scene with proper shadows and BRDF response

**Deliverable:** `Phase3_FullBRDF/direct_lighting.slang`

#### Phase 3.3: Path Tracing (Recursive Rays) (Day 3-4)
**Goal:** Full recursive path tracing matching current implementation

**Shader Additions:**
```slang
[shader("closesthit")]
void ClosestHit(inout Payload payload, ...) {
    // Direct lighting (from Phase 3.2)
    float3 directLight = evaluateDirectLighting(...);
    
    // Indirect lighting (recursive ray)
    if (payload.depth < maxDepth) {
        // Sample BRDF for next direction
        float3 wi = sampleDisneyBRDF(material, wo, normal, random);
        
        // Trace indirect ray
        Payload indirectPayload;
        indirectPayload.depth = payload.depth + 1;
        TraceRay(..., indirectPayload);
        
        float3 indirectLight = indirectPayload.color * brdfPdf(...);
        
        payload.color = directLight + indirectLight;
    } else {
        payload.color = directLight;
    }
}
```

**Tasks:**
- [ ] Implement BRDF importance sampling
- [ ] Implement recursive ray tracing
- [ ] Implement Russian roulette termination
- [ ] Test with varying max depths (1, 2, 4, 8)
- [ ] Compare convergence with current implementation

**Expected Output:** Full path-traced scene matching current quality

**Deliverable:** `Phase3_FullBRDF/path_tracing.slang`

#### Phase 3.4: Optimization and Polish (Day 4-5)
**Goal:** Match or exceed current performance

**Tasks:**
- [ ] Profile hot paths
- [ ] Optimize material parameter fetches
- [ ] Optimize texture sampling
- [ ] Verify inline-ability of BRDF functions
- [ ] Compare frame times with current implementation
- [ ] Fix any performance regressions

**Expected Output:** Performance parity or better

### Deliverables (Phase 3 Complete)
- [ ] Complete BRDF modules (disney_bsdf, lights, util)
- [ ] `direct_lighting.slang` - Direct illumination
- [ ] `path_tracing.slang` - Full path tracer
- [ ] Performance analysis document
- [ ] Visual quality comparisons (screenshots + metrics)

### Success Criteria
- ✅ Pixel-perfect match with current implementation (or justifiable differences)
- ✅ Frame time within 5% of current implementation
- ✅ Converges to same solution (compare after N samples)
- ✅ All materials render correctly (metal, dielectric, rough, smooth)
- ✅ Proper energy conservation (no brightness gain/loss)
- ✅ Can switch between Slang and native at build time

### Dependencies
- Phase 2 completed (material system working)

### Risk Level
**MEDIUM-HIGH** - BRDF correctness is critical, path tracing is complex

---

## Cross-Cutting Concerns

### Validation Strategy (All Phases)

For **every** phase, verify:

1. **Visual Correctness:**
   ```powershell
   # Render test scene with both paths
   .\build\Debug\chameleonrt.exe dxr test_scene.obj --slang
   .\build\Debug\chameleonrt.exe dxr test_scene.obj --native
   # Take screenshots, compare pixel-by-pixel
   ```

2. **Performance Baseline:**
   ```powershell
   # Benchmark mode (100 frames average)
   .\build\Debug\chameleonrt.exe dxr test_scene.obj --slang --benchmark
   .\build\Debug\chameleonrt.exe dxr test_scene.obj --native --benchmark
   ```

3. **Validation Layers:**
   - Enable D3D12 debug layer: Check for shader mismatches, SBT errors
   - Enable Vulkan validation: Check for descriptor set issues, pipeline errors

4. **Cross-Backend Verification:**
   - Render same scene with DXR and Vulkan
   - Screenshots should be pixel-identical (within float precision)
   - Frame stats should match (ray count, triangle intersections)

### Build Configuration

Add CMake options:
```cmake
option(ENABLE_DXR_SLANG "Use Slang for DXR shaders" OFF)
option(ENABLE_VULKAN_SLANG "Use Slang for Vulkan shaders" OFF)
option(SLANG_SHADER_DEBUG "Enable Slang shader debug info" OFF)
```

During transition, both paths (native and Slang) should compile and work.

### Test Scenes

Use these test cases for each phase:

1. **Phase 1:** Simple cube or sphere (test geometry normals)
2. **Phase 2:** Textured cube with checkerboard (test UVs)
3. **Phase 3:** Full Cornell box or Sponza (test BRDF variety)

### Documentation

Each phase directory should contain:
- `README.md` - Phase objectives and instructions
- `shader.slang` - The shader(s) for this phase
- `RESULTS.md` - Screenshots, performance data, notes
- `ISSUES.md` - Known bugs, workarounds, TODOs

---

## Timeline Summary

| Phase | Duration | Cumulative | Risk |
|-------|----------|------------|------|
| Phase 0: Utility Upgrade | 2-3 hours | 0.5 days | LOW |
| Phase 1: Flat Shading | 3-5 days | 5.5 days | MEDIUM |
| Phase 2: Textures | 2-3 days | 8.5 days | LOW |
| Phase 3: Full BRDF | 3-5 days | 13.5 days | MEDIUM-HIGH |
| **TOTAL** | **~2-3 weeks** | | |

**Buffer:** Add 20% for unexpected issues = **3-4 weeks total**

---

## Success Metrics

### Phase 1 Success:
- Normal-shaded mesh renders identically on DXR and Vulkan
- Slang path vs. native path produce identical output

### Phase 2 Success:
- All textures load and display correctly
- Multi-material scenes work properly

### Phase 3 Success:
- Full path tracer produces correct lighting
- Performance within 5% of native
- Can be primary rendering path (not experimental)

### Overall Success:
- **Single shader source** compiles to DXR and Vulkan
- **Zero visual differences** between Slang and native
- **Production ready** - ship with Slang path enabled by default
- **Documentation complete** - others can add shaders easily

---

## Rollback Plan

Each phase has a working fallback:
- CMake flags can disable Slang path
- Native HLSL/GLSL shaders remain in repo
- If Phase N fails, Phase N-1 is still usable

---

## Next Steps

1. ✅ **Create phase directories** (DONE)
2. 📝 **Detailed planning for Phase 0** (create PLAN.md in Phase0_UtilityUpgrade/)
3. 📝 **Detailed planning for Phase 1** (create PLAN.md in Phase1_FlatShading/)
4. 📝 **Detailed planning for Phase 2** (create PLAN.md in Phase2_Textures/)
5. 📝 **Detailed planning for Phase 3** (create PLAN.md in Phase3_FullBRDF/)
6. 🔨 **Execute Phase 0**
7. ✅ **Validate Phase 0**
8. 🔨 **Execute Phase 1** ... and so on

**READY TO BEGIN DETAILED PLANNING? 🚀**
