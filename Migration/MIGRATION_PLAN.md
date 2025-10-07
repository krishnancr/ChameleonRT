# Slang Migration Plan

## Overview

**Goal:** Integrate Slang shader compilation into ChameleonRT's direct API backends (DXR, Vulkan, Metal) without using slang-gfx abstraction layer.

**Strategy:** Incremental migration with validation gates. Start with pass-through compilation of existing shaders, then gradually adopt Slang language features.

**Timeline:** 6-8 weeks with clear decision points

---

## Phases

### Phase 1: Pass-Through Compilation (Weeks 1-2)

**Goal:** Prove Slang can compile existing HLSL/GLSL shaders without breaking functionality

#### Week 1: D3D12 Backend

**Objectives:**
- ✅ Integrate `SlangShaderCompiler` utility class
- ✅ Replace DXC calls with `compileHLSLToDXIL()`
- ✅ Verify all existing HLSL shaders compile correctly
- ✅ No visual or functional regressions

**Tasks:**
1. Copy `util/slang_shader_compiler.*` to project
2. Update `backends/dxr/CMakeLists.txt` to link against utility
3. Find shader compilation call sites in DXR backend
4. Replace DXC compilation with SlangShaderCompiler
5. Test with existing scenes/shaders

**Success Criteria:**
- All DXR shaders compile through Slang
- Rendering output identical to previous
- No errors in PIX/validation layer
- Compilation time within 10% of DXC

**Rollback Plan:**
- Keep DXC path via `#ifdef USE_SLANG_COMPILER`
- Can disable with CMake option if issues arise

#### Week 2: Vulkan Backend

**Objectives:**
- ✅ Integrate `SlangShaderCompiler` into Vulkan backend
- ✅ Replace glslang calls with `compileGLSLToSPIRV()`
- ✅ Verify SPIR-V output validates correctly
- ✅ No regressions in Vulkan rendering

**Tasks:**
1. Update `backends/vulkan/CMakeLists.txt`
2. Find glslang compilation sites
3. Replace with `compileGLSLToSPIRV()`
4. Validate SPIR-V with spirv-val
5. Test with RenderDoc

**Success Criteria:**
- All Vulkan shaders compile through Slang
- SPIR-V passes validation
- Rendering identical to previous
- Performance within 5%

**Decision Point:** 
🚦 **End of Week 2:** If pass-through fails or causes significant issues, reconsider strategy. If successful, proceed to Phase 2.

---

### Phase 2: First Slang Shader (Week 3)

**Goal:** Prove end-to-end Slang language workflow by porting one simple shader

#### Single Shader Migration

**Target:** Simple vertex + fragment shader (e.g., colored triangle, basic mesh)

**Tasks:**
1. Choose simplest shader pair (VS + PS)
2. Convert to Slang syntax (`.slang` file)
3. Compile to DXIL for D3D12
4. Compile to SPIRV for Vulkan
5. Test on both backends

**Example Shader:**
```slang
// simple_mesh.slang
struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 normal : NORMAL;
};

cbuffer SceneUniforms : register(b0) {
    float4x4 viewProj;
    float4x4 model;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.position = mul(viewProj, worldPos);
    output.normal = mul((float3x3)model, input.normal);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float3 lightDir = normalize(float3(1, 1, 1));
    float ndotl = max(dot(normalize(input.normal), lightDir), 0.0);
    return float4(ndotl.xxx, 1.0);
}
```

**Validation:**
- Compiles to DXIL without errors
- Compiles to SPIRV without errors
- D3D12 renders correctly
- Vulkan renders correctly
- Outputs match between APIs

**Success Criteria:**
- Single Slang source works on both APIs
- No platform-specific code needed
- Developer experience is smooth

**Decision Point:**
🚦 **End of Week 3:** If Slang language causes problems, assess whether syntax issues or tooling issues. Decide whether to continue or revert to HLSL/GLSL pass-through only.

---

### Phase 3: Ray Tracing Shaders (Weeks 4-6)

**Goal:** Port all ray tracing shaders to Slang language

#### Week 4: Miss & RayGen Shaders

**Rationale:** These are simplest RT shaders, good starting point

**Tasks:**
1. Port miss shader to Slang
   - Simple shader, usually just returns background color
   - Good test of ray tracing pipeline integration
   
2. Port ray generation shader
   - Slightly more complex
   - Tests camera setup, ray dispatch

3. Update shader binding table (SBT) creation
   - Ensure Slang-compiled shaders work in SBT
   - D3D12 and Vulkan have different SBT layouts

**D3D12 Integration:**
```cpp
// Example: Loading Slang-compiled RT shader
auto missBlob = compiler.compileSlangToDXIL(
    missShaderSource, "missMain", ShaderStage::Miss);

D3D12_DXIL_LIBRARY_DESC libDesc = {};
libDesc.DXILLibrary.pShaderBytecode = missBlob->bytecode.data();
libDesc.DXILLibrary.BytecodeLength = missBlob->bytecode.size();
// ... rest of SBT setup
```

**Vulkan Integration:**
```cpp
// Example: Loading Slang-compiled RT shader
auto missBlob = compiler.compileSlangToSPIRV(
    missShaderSource, "missMain", ShaderStage::Miss);

VkShaderModuleCreateInfo createInfo = {};
createInfo.codeSize = missBlob->bytecode.size();
createInfo.pCode = (uint32_t*)missBlob->bytecode.data();
vkCreateShaderModule(device, &createInfo, nullptr, &missShader);
// ... rest of pipeline setup
```

**Success Criteria:**
- Miss shader renders correctly on both APIs
- RayGen shader dispatches rays correctly
- No SBT issues
- Performance acceptable

#### Week 5: Closest Hit Shader

**Complexity:** Medium (shading logic, material evaluation)

**Tasks:**
1. Port closest hit shader to Slang
2. Handle material parameters
3. Test with various materials (diffuse, specular, etc.)
4. Validate shading output

**Challenges:**
- Material system integration
- Texture sampling
- Resource binding (different per API)

**Strategy:**
- Start with simplest material (solid color)
- Add texture sampling incrementally
- Test each material type individually

#### Week 6: Any Hit & Intersection Shaders (if needed)

**Tasks:**
1. Port any hit shaders (for transparency, etc.)
2. Port custom intersection shaders (if used)
3. Validate full ray tracing pipeline

**Success Criteria:**
- All RT shaders in Slang language
- Full scene renders correctly on D3D12
- Full scene renders correctly on Vulkan
- Performance within 5% of previous (after warmup)

**Decision Point:**
🚦 **End of Week 6:** Evaluate whether to continue with advanced Slang features or consider migration complete. Assess developer experience, performance, maintainability.

---

### Phase 4: Advanced Slang Features (Week 7+)

**Goal:** Leverage Slang-specific features to improve code quality and cross-platform consistency

#### Slang Interfaces for Materials

**Benefit:** Abstract material evaluation behind interface

```slang
interface IMaterial {
    float3 evaluate(float3 viewDir, float3 normal, float2 uv);
};

struct LambertianMaterial : IMaterial {
    Texture2D albedoMap;
    SamplerState sampler;
    
    float3 evaluate(float3 viewDir, float3 normal, float2 uv) {
        return albedoMap.Sample(sampler, uv).rgb;
    }
};

// In shader:
[shader("closesthit")]
void closestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs) {
    IMaterial material = getMaterial(InstanceID());
    payload.color = material.evaluate(viewDir, normal, uv);
}
```

**Tasks:**
1. Define material interface
2. Implement concrete material types
3. Update shaders to use interface
4. Test polymorphic dispatch

#### Parameter Blocks

**Benefit:** Cleaner resource binding, less boilerplate

```slang
struct SceneParameters {
    Texture2D envMap;
    SamplerState envSampler;
    float4x4 viewProj;
    float3 cameraPos;
};

ParameterBlock<SceneParameters> scene;

[shader("raygen")]
void rayGen() {
    float3 color = scene.envMap.Sample(scene.envSampler, uv).rgb;
    // ...
}
```

**Tasks:**
1. Organize parameters into logical blocks
2. Update shader code to use parameter blocks
3. Update binding code in C++ (per API)

#### Automatic Differentiation (Optional)

**Benefit:** If doing ML-related stuff, Slang supports autodiff

**Evaluation:** Assess if relevant for our use cases

---

## Rollback Strategy

At any phase, we can rollback if needed:

### CMake Option Guard

```cmake
option(ENABLE_SLANG "Use Slang for shader compilation" ON)

if(ENABLE_SLANG)
    # Use SlangShaderCompiler
else()
    # Use native DXC/glslang
endif()
```

### Dual Code Paths (Weeks 1-3)

Keep both compilation paths active:

```cpp
#ifdef USE_SLANG
    auto shader = slangCompiler.compileHLSLToDXIL(source, "main", ...);
#else
    auto shader = dxcCompileHLSL(source, "main", ...);
#endif
```

Remove after 2 weeks of stability.

### Branch Isolation

- Main work on `feature/slang-integration` branch
- Keep `master` stable
- Merge only after full validation

### Decision Gates

🚦 **Week 2:** Pass-through working?  
   - ✅ Continue to Phase 2
   - ❌ Reconsider entire Slang strategy

🚦 **Week 3:** First Slang shader working?  
   - ✅ Continue to Phase 3
   - ❌ Keep pass-through only, abandon Slang language

🚦 **Week 6:** All RT shaders ported?  
   - ✅ Polish and optimize
   - ❌ Identify blockers, decide next steps

---

## Success Metrics

### Functional
- ✅ All shaders compile through Slang
- ✅ Identical rendering output to previous
- ✅ No validation errors (PIX, RenderDoc)
- ✅ Works on both D3D12 and Vulkan

### Performance
- ✅ Compilation time ≤ 110% of previous
- ✅ Runtime performance ≤ 105% of previous
- ✅ No unexpected frame time spikes

### Developer Experience
- ✅ Simpler shader authoring (single source)
- ✅ Better error messages than before
- ✅ Faster iteration (no multiple shader versions)
- ✅ Easier to add new features

### Maintainability
- ✅ Less code duplication (HLSL vs GLSL)
- ✅ Clear organization (Slang files separate)
- ✅ Easy to add new backends (Metal ready)

---

## Risk Mitigation

### Risk 1: Slang Compilation Bugs

**Mitigation:**
- Start with simple shaders (low risk)
- Compare generated DXIL/SPIRV with previous
- Report issues to Slang team if found
- Fallback to native compilers if critical

### Risk 2: Performance Regression

**Mitigation:**
- Benchmark each phase
- Profile with PIX/RenderDoc/Nsight
- Identify bottlenecks early
- Optimize before proceeding

### Risk 3: API Incompatibilities

**Mitigation:**
- Use Slang's platform-specific attributes (`[[vk::...]]`)
- Test on both APIs frequently
- Keep API-specific code minimal
- Document workarounds

### Risk 4: Timeline Slippage

**Mitigation:**
- Decision gates at week 2, 3, 6
- Can stop at any phase if needed
- Dual paths reduce risk in early phases
- Clear success criteria prevent scope creep

---

## Metal Consideration

**Not in initial plan**, but made easier by Slang:

Once D3D12 + Vulkan working:
1. Add Metal backend skeleton
2. Use `compileSlangToMetal()`
3. Implement Metal-specific pipeline creation
4. Should be faster than starting from scratch

**Estimated effort:** 3-4 weeks after Phase 3 complete

---

## Tools & Resources

### Development
- **Visual Studio 2022** (D3D12 debugging)
- **PIX** (D3D12 profiling)
- **RenderDoc** (Vulkan debugging)
- **spirv-val** (SPIR-V validation)

### Documentation
- [Slang Language Guide](https://shader-slang.com/slang/user-guide/)
- [Slang API Reference](https://shader-slang.com/slang/api-reference/)
- DXR Specification
- Vulkan Ray Tracing Tutorial

### Validation Scripts
- `clean_rebuild.ps1` - Full rebuild
- `validate_shaders.ps1` - Compile all shaders (TBD)
- `compare_output.ps1` - Visual comparison (TBD)

---

## Weekly Checkpoints

### Week 1 Review
- [ ] D3D12 HLSL→DXIL pass-through working?
- [ ] Any compilation errors?
- [ ] Performance acceptable?
- [ ] Decision: Continue or investigate issues?

### Week 2 Review
- [ ] Vulkan GLSL→SPIRV pass-through working?
- [ ] Both backends functional?
- [ ] Ready for Slang language?
- [ ] Decision: Proceed to Phase 2?

### Week 3 Review
- [ ] First Slang shader compiling?
- [ ] Works on both APIs?
- [ ] Developer experience good?
- [ ] Decision: Proceed to RT shaders?

### Week 6 Review
- [ ] All RT shaders ported?
- [ ] Performance acceptable?
- [ ] Any blockers remaining?
- [ ] Decision: Polish vs. Advanced features?

---

## Completion Criteria

Migration considered **complete** when:

1. ✅ All shaders authored in Slang language
2. ✅ Compiles to DXIL (D3D12) and SPIRV (Vulkan)
3. ✅ No visual/functional regressions
4. ✅ Performance within 5% of baseline
5. ✅ Documentation updated
6. ✅ CI/CD passes (if applicable)
7. ✅ No `#ifdef USE_SLANG` guards needed
8. ✅ Team confident in maintaining Slang codebase

**Then:** Merge `feature/slang-integration` → `master`

---

*"Plan the flight, fly the plan, but be ready to land anywhere."*
