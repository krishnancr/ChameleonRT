# First Slang Shader Prompts - Phase 2

## Overview

These prompts guide you through writing your first shader in native Slang language and compiling it to both DXIL and SPIRV.

**Phase:** 2 (Slang Language Migration)  
**Part:** 1 (First Shader)  
**Duration:** 2-3 hours  
**Reference:** `Migration/MIGRATION_PLAN.md` Phase 2

**Goal:** Prove end-to-end Slang language workflow by porting one simple shader that works on both D3D12 and Vulkan.

---

## Prompt 1: Choose Target Shader for Migration

### Prompt

```
Task: Identify simplest shader to port to Slang language

Context:
- Phase 2, Step 1
- Want easiest shader first (prove workflow)
- Ideally vertex+fragment or simple compute
- If only RT shaders exist, choose miss shader (simplest)

Please help me find:

1. Simplest shader in the project:
   - Vertex/fragment if rasterization exists
   - Otherwise: miss shader (just returns background color)
   - Avoid complex hit shaders for first attempt

2. Current shader code:
   - Show me the HLSL or GLSL source
   - What does it do?
   - How complex is it?

3. Where it's used:
   - Which backend(s)?
   - How is it loaded/compiled?
   - Entry point name?

Please show current shader source and suggest which to port first.
```

---

## Prompt 2: Write First Slang Shader

### Context Setup
**Files to have open:**
1. Current HLSL/GLSL shader source (from Prompt 1)
2. `Migration/MIGRATION_PLAN.md` Phase 2 example
3. Slang documentation (optional)

### Prompt

```
Task: Convert [SHADER_NAME] to Slang language

Context:
- Phase 2, Step 2
- Converting: [shader name from Prompt 1]
- Target: Both D3D12 (DXIL) and Vulkan (SPIRV)
- Slang should be similar to HLSL but with platform annotations

Current shader (HLSL or GLSL):
[Paste current shader source]

Requirements:

1. Create new file: shaders/[name].slang

2. Convert to Slang syntax:
   - Use HLSL-like syntax as base
   - Add [[vk::location(N)]] for Vulkan vertex inputs/outputs
   - Use [shader("stage")] attributes for entry points
   - Keep cbuffer for uniform data
   - Platform-independent types (float3, float4, etc.)

3. Example pattern:
```slang
// shaders/simple.slang

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normal : NORMAL;
};

cbuffer SceneData : register(b0) {
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
    // Simple shading
    float3 normal = normalize(input.normal);
    return float4(normal * 0.5 + 0.5, 1.0);
}
```

Please:
- Convert my shader to Slang
- Create the .slang file
- Explain any changes needed for cross-platform compatibility
- Note differences from HLSL/GLSL

Create file: shaders/[shader_name].slang
```

### Expected Output

- New `.slang` shader file created
- Slang-compatible syntax
- Platform annotations where needed
- Entry point attributes
- Ready to compile

---

## Prompt 3: Load and Compile Slang Shader in DXR Backend

### Prompt

```
Task: Load .slang file and compile to DXIL for D3D12

Context:
- Phase 2, Step 3a
- Have: shaders/[name].slang
- Need: Compile to DXIL using compileSlangToDXIL()
- Replace one shader compilation in DXR backend

Slang file: shaders/[name].slang
Entry point: [from shader]
Stage: [vertex/fragment/raygen/miss/etc.]

Requirements:

1. Load shader source from file:
```cpp
auto slangSource = chameleonrt::SlangShaderCompiler::loadShaderSource("shaders/[name].slang");
if (!slangSource) {
    throw std::runtime_error("Failed to load Slang shader");
}
```

2. Compile to DXIL:
```cpp
auto dxilBlob = slangCompiler.compileSlangToDXIL(
    *slangSource,
    "[entryPoint]",  // e.g., "vertexMain"
    ShaderStage::[Stage]
);

if (!dxilBlob) {
    throw std::runtime_error("Slang→DXIL failed: " + slangCompiler.getLastError());
}

std::cout << "Compiled Slang→DXIL: " << dxilBlob->bytecode.size() << " bytes" << std::endl;
```

3. Use bytecode in D3D12 pipeline:
   - Same as before: dxilBlob->bytecode
   - Should work with existing pipeline code

Please show:
- Where to add file loading code
- Where to replace shader compilation
- Complete error handling
- Integration with existing pipeline setup

File: [DXR backend shader compilation file]
```

---

## Prompt 4: Compile Same Slang Shader to SPIRV for Vulkan

### Prompt

```
Task: Compile .slang file to SPIRV for Vulkan

Context:
- Phase 2, Step 3b
- Same shader: shaders/[name].slang
- Now compile to SPIRV using compileSlangToSPIRV()
- Replace corresponding Vulkan shader

Slang file: shaders/[name].slang (same as DXR)
Entry point: [same as DXR]
Stage: [same as DXR]

Requirements:

1. Load shader (same as DXR):
```cpp
auto slangSource = chameleonrt::SlangShaderCompiler::loadShaderSource("shaders/[name].slang");
```

2. Compile to SPIRV:
```cpp
auto spirvBlob = slangCompiler.compileSlangToSPIRV(
    *slangSource,
    "[entryPoint]",
    ShaderStage::[Stage]
);

if (!spirvBlob) {
    throw std::runtime_error("Slang→SPIRV failed: " + slangCompiler.getLastError());
}

// Convert to uint32_t for Vulkan
std::vector<uint32_t> spirv(spirvBlob->bytecode.size() / 4);
memcpy(spirv.data(), spirvBlob->bytecode.data(), spirvBlob->bytecode.size());

std::cout << "Compiled Slang→SPIRV: " << spirv.size() << " words" << std::endl;
```

3. Use in Vulkan pipeline:
   - Same as before: spirv vector
   - VkShaderModuleCreateInfo as usual

Please show integration in Vulkan backend.

File: [Vulkan backend shader compilation file]
```

---

## Prompt 5: Test Cross-Compilation

### Prompt

```
Task: Validate single Slang source compiles to both APIs

Context:
- Phase 2, Step 4
- Single .slang file
- Compiles to DXIL for D3D12
- Compiles to SPIRV for Vulkan
- Both should render identically

Test procedure:

1. Build both backends:
   cmake -B build_slang_test --fresh -DENABLE_SLANG=ON -DENABLE_DXR_SLANG=ON -DENABLE_VULKAN_SLANG=ON
   cmake --build build_slang_test --config Debug

2. Test D3D12:
   .\build_slang_test\Debug\chameleonrt.exe dxr test_cube.obj
   - Check: "Compiled Slang→DXIL: N bytes"
   - Verify shader works correctly
   - Take screenshot or note visual output

3. Test Vulkan:
   .\build_slang_test\Debug\chameleonrt.exe vulkan test_cube.obj
   - Check: "Compiled Slang→SPIRV: N words"
   - Verify shader works correctly
   - Compare visual output to D3D12

4. Validation:
   - Both APIs render same scene
   - No shader compilation errors
   - No validation layer errors (PIX, Vulkan validation)
   - Shader outputs identical

Success criteria:
- ✅ Single .slang source
- ✅ Compiles to both targets
- ✅ Renders identically on both APIs
- ✅ No errors

What should I check if outputs don't match?
```

---

## Prompt 6: Document Learnings and Next Steps

### Prompt

```
Task: Reflect on Phase 2 completion and plan Phase 3

Context:
- First Slang shader working
- Proven cross-compilation works
- Ready to migrate more shaders

Questions:

1. What went well?
   - Was Slang syntax easy to work with?
   - Did cross-compilation work smoothly?
   - Any unexpected issues?

2. What needs improvement?
   - Shader differences between APIs?
   - Compilation time?
   - Error messages quality?

3. Ready for Phase 3 (migrate all RT shaders)?
   - Confidence level in Slang?
   - Any blockers?

4. Plan for ray tracing shader migration:
   - Order: Miss → RayGen → ClosestHit → AnyHit?
   - Or different order based on complexity?

Please help me assess and plan next steps.

Should we proceed to Phase 3 (all RT shaders) or address any issues first?
```

---

## Common Slang Language Issues

### Issue: "Syntax error in Slang shader"

**Solutions:**
- Check [shader("stage")] attribute format
- Verify [[vk::location(N)]] syntax
- Ensure struct/cbuffer syntax correct
- Compare with MIGRATION_PLAN.md example

### Issue: "Entry point not found in Slang module"

**Solution:**
```slang
// Entry point needs [shader("stage")] attribute:
[shader("vertex")]  // ← Required
VertexOutput vertexMain(VertexInput input) { ... }
```

### Issue: "Rendering different on D3D12 vs Vulkan"

**Solutions:**
1. Check coordinate system differences (clip space)
2. Verify [[vk::location(N)]] attributes set correctly
3. Check cbuffer/push constant differences
4. Review Slang documentation on platform differences

### Issue: "DXIL compiles but SPIRV fails (or vice versa)"

**Solutions:**
- Check target profiles (sm_6_5 vs spirv_1_5)
- Some features may not cross-compile perfectly
- Simplify shader to isolate issue
- Check Slang compiler diagnostics

---

## Next Steps

After first Slang shader complete:

✅ **Phase 2 done** - Native Slang language proven

**Decision Point:** Proceed to Phase 3?
- ✅ Yes → Migrate all RT shaders (`Migration/MIGRATION_PLAN.md` Phase 3)
- ⚠️ Issues → Address blockers first

**If proceeding:**
- Start with miss shaders (simplest RT shaders)
- Then raygen
- Then closest hit
- Finally any hit (if present)

**Alternative:**
- Stay in Phase 2, migrate more simple shaders first
- Build confidence before tackling complex RT shaders

---

**Estimated time:** 2-3 hours  
**Success indicator:** Single `.slang` file renders identically on D3D12 and Vulkan
