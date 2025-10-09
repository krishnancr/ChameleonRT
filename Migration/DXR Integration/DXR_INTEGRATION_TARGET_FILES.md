# DXR Backend Shader Compilation Integration - Target Files

**Date:** October 9, 2025  
**Phase:** 1, Part 2 - Identify Integration Points  
**Purpose:** Document all DXR backend files involved in shader compilation for Slang integration

---

## Executive Summary

The DXR backend uses a **build-time shader compilation** approach:
- Shaders are compiled **during CMake build** using DXC (DirectX Shader Compiler)
- Compiled DXIL bytecode is **embedded as a C header file** at build time
- The embedded bytecode is then **directly loaded** into the pipeline at runtime
- **No runtime shader compilation** occurs in the current implementation

**Key Finding:** Unlike typical DXC integration, there's **no runtime IDxcCompiler usage**. The shader compilation happens entirely in the CMake build process via the `add_dxil_embed_library()` function.

---

## Part 1: Main Backend Files

### 1.1 Backend Class Definition

**File:** `backends/dxr/render_dxr.h`
- **Lines:** 1-100 (main class definition)
- **Backend Class Name:** `RenderDXR`
- **Base Class:** `RenderBackend`
- **Key Members:**
  - `dxr::RTPipeline rt_pipeline` - Ray tracing pipeline object
  - `Microsoft::WRL::ComPtr<ID3D12Device5> device` - D3D12 device
  - Various descriptor heaps and buffers

**Integration Point:**
```cpp
struct RenderDXR : RenderBackend {
    // Add Slang compiler instance here:
#ifdef USE_SLANG_COMPILER
    chameleonrt::SlangShaderCompiler slangCompiler;
#endif
```

### 1.2 Main Backend Implementation

**File:** `backends/dxr/render_dxr.cpp`
- **Total Lines:** 945
- **Key Functions:**
  - `build_raytracing_pipeline()` - Lines 590-645 (CRITICAL)
  - `set_scene()` - Scene setup and resource uploads
  - `render()` - Main rendering loop
  - `create_device_objects()` - D3D12 device setup

---

## Part 2: Shader Compilation System

### 2.1 Current Build-Time Compilation

**CMake Function:** `add_dxil_embed_library()`
- **Location:** `backends/dxr/cmake/FindD3D12.cmake`, lines 58-117
- **Purpose:** Compile HLSL → DXIL at build time, embed as C header

**How it works:**
```cmake
add_dxil_embed_library(dxr_shaders render_dxr.hlsl util.hlsl
    disney_bsdf.hlsl lcg_rng.hlsl
    COMPILE_OPTIONS -O3
    COMPILE_DEFINITIONS ${HLSL_COMPILE_DEFNS}
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR} ${PROJECT_SOURCE_DIR})
```

**Generates:**
- Output file: `build/backends/dxr/render_dxr_embedded_dxil.h`
- Contains: `unsigned char render_dxr_dxil[]` array with compiled bytecode

**DXC Command (from CMake):**
```powershell
dxc render_dxr.hlsl -T lib_6_3 -Fh render_dxr_embedded_dxil.h -Vn render_dxr_dxil
```

### 2.2 Shader Loading at Runtime

**File:** `backends/dxr/render_dxr.cpp`
**Function:** `RenderDXR::build_raytracing_pipeline()`
**Lines:** 590-645

**Current Implementation:**
```cpp
void RenderDXR::build_raytracing_pipeline()
{
    // Loads pre-compiled embedded DXIL
    dxr::ShaderLibrary shader_library(render_dxr_dxil,        // Embedded bytecode array
                                      sizeof(render_dxr_dxil),  // Size
                                      {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"});

    // ... root signature setup ...

    // Build RT pipeline with pre-loaded shader library
    rt_pipeline = rt_pipeline_builder.create(device.Get());
}
```

**Key Details:**
- `render_dxr_dxil` comes from `#include "render_dxr_embedded_dxil.h"` (line 9)
- Shader library expects **already-compiled DXIL bytecode**
- No runtime compilation needed

---

## Part 3: Shader Files

### 3.1 Main Shader

**File:** `backends/dxr/render_dxr.hlsl`
- **Lines:** 329 total
- **Shader Stages:**
  - Ray generation shader: `RayGen` (entry point around line 150)
  - Miss shader: `Miss` (around line 200)
  - Closest hit shader: `ClosestHit` (around line 250)
  - Shadow miss shader: `ShadowMiss` (around line 300)

**Includes:**
```hlsl
#include "util.hlsl"
#include "lcg_rng.hlsl"
#include "disney_bsdf.hlsl"
#include "lights.hlsl"
#include "util/texture_channel_mask.h"
```

### 3.2 Shader Dependencies

**Files in `backends/dxr/`:**
- `util.hlsl` - Utility functions
- `lcg_rng.hlsl` - Random number generation
- `disney_bsdf.hlsl` - Disney BRDF implementation
- `lights.hlsl` - Lighting functions

**All these files are compiled together as a single shader library** (DXR library model, `-T lib_6_3`)

---

## Part 4: Pipeline Creation Flow

### 4.1 Shader Library Class

**File:** `backends/dxr/dxr_utils.h` or `dxr_utils.cpp` (likely)
**Class:** `dxr::ShaderLibrary`

**Constructor signature (inferred):**
```cpp
ShaderLibrary(const void* dxil_bytecode, 
              size_t bytecode_size, 
              std::vector<std::wstring> export_names);
```

**Purpose:**
- Wraps DXIL bytecode for D3D12 state object creation
- Manages shader exports (entry points)

### 4.2 Pipeline Builder

**Class:** `dxr::RTPipelineBuilder`
**Usage in `build_raytracing_pipeline()`:**

```cpp
dxr::RTPipelineBuilder()
    .set_global_root_sig(global_root_sig)
    .add_shader_library(shader_library)           // Adds the DXIL library
    .set_ray_gen(L"RayGen")                       // Specifies ray gen entry point
    .add_miss_shader(L"Miss")                     // Miss shader entry point
    .add_miss_shader(L"ShadowMiss")               // Shadow miss entry point
    .set_shader_root_sig({L"RayGen"}, raygen_root_sig)
    .configure_shader_payload(...)
    .set_max_recursion(1)
    .add_hit_group(...)                           // Hit groups for each geometry
    .create(device.Get());                        // Creates D3D12 state object
```

---

## Part 5: Integration Strategy

### 5.1 Current Build Flow

```
HLSL Source Files
    ↓
CMake add_dxil_embed_library()
    ↓
DXC Compiler (build time)
    ↓
DXIL Bytecode → Embedded Header
    ↓
Compiled into C++ Binary
    ↓
Runtime: Load from embedded array
    ↓
Create D3D12 State Object
```

### 5.2 Proposed Slang Integration Flow

**Option A: Replace Build-Time Compilation (Recommended)**

```
HLSL/Slang Source Files
    ↓
CMake Custom Command with Slang
    ↓
Slang Compiler (build time) → DXIL
    ↓
DXIL Bytecode → Embedded Header
    ↓
Compiled into C++ Binary
    ↓
Runtime: Load from embedded array (unchanged)
    ↓
Create D3D12 State Object (unchanged)
```

**Why this approach:**
- ✅ Minimal code changes (only CMake)
- ✅ No runtime dependencies on Slang DLLs
- ✅ Fast runtime startup (no compilation)
- ✅ Easy to compare DXC vs Slang output
- ✅ Follows existing architecture

**Option B: Add Runtime Compilation Path (Alternative)**

```
HLSL/Slang Source Files (in source tree)
    ↓
Runtime: Load source files
    ↓
Slang Compiler (runtime) → DXIL
    ↓
DXIL Bytecode (in memory)
    ↓
Create D3D12 State Object
```

**Why this might be useful:**
- ✅ Faster iteration (no rebuild needed)
- ✅ Shader hot-reload possible
- ❌ Requires Slang DLL deployment
- ❌ Slower startup time
- ❌ More code changes needed

### 5.3 Recommended Approach: Hybrid

**Phase 1: CMake Integration (Build-time)**
- Replace `dxc` calls with Slang in `add_dxil_embed_library()`
- Keep embedded header approach
- No C++ code changes needed
- Easy rollback if issues

**Phase 2: Optional Runtime Path (Future)**
- Add `#ifdef USE_SLANG_RUNTIME` path
- Allow shader reload during development
- Fall back to embedded for release builds

---

## Part 6: Key Files for Integration

### 6.1 Files to Modify (Phase 1 - CMake Only)

1. **`backends/dxr/cmake/FindD3D12.cmake`**
   - Function: `add_dxil_embed_library()`
   - Lines: 58-117
   - Change: Replace `dxc` command with Slang compilation
   - **Impact:** Build system only, no C++ changes

2. **`backends/dxr/CMakeLists.txt`**
   - Lines: 25-32 (shader library definition)
   - Change: Add Slang options if `ENABLE_DXR_SLANG` is set
   - **Impact:** Build configuration

### 6.2 Files to Modify (Phase 2 - Runtime Compilation)

3. **`backends/dxr/render_dxr.h`**
   - Lines: 10-50 (class members)
   - Change: Add `SlangShaderCompiler` member
   - **Impact:** Class definition

4. **`backends/dxr/render_dxr.cpp`**
   - Function: `build_raytracing_pipeline()`
   - Lines: 590-645
   - Change: Add conditional runtime compilation path
   - **Impact:** Pipeline creation

### 6.3 Files to Reference (Unchanged)

- `backends/dxr/render_dxr.hlsl` - Main shader (use as-is for pass-through)
- `backends/dxr/dxr_utils.h/cpp` - Pipeline utilities (no changes needed)
- `backends/dxr/dx12_utils.h/cpp` - D3D12 helpers (no changes needed)

---

## Part 7: Entry Points and Shader Stages

### 7.1 Ray Tracing Shaders (from render_dxr.hlsl)

| Shader Type | Entry Point | Export Name | Purpose |
|-------------|-------------|-------------|---------|
| Ray Generation | `RayGen` | `L"RayGen"` | Primary ray generation from camera |
| Miss (Primary) | `Miss` | `L"Miss"` | Background/environment for primary rays |
| Miss (Shadow) | `ShadowMiss` | `L"ShadowMiss"` | Shadow ray misses (always black) |
| Closest Hit | `ClosestHit` | `L"ClosestHit"` | Surface shading on ray intersection |

**Note:** No any-hit shader currently used (all geometry is opaque)

### 7.2 Root Signatures

**Global Root Signature:** Empty (no global parameters)

**RayGen Root Signature:**
- CBV: `SceneParams` (register b1)
- Descriptor Heap: SRVs/UAVs (output, accum buffer, scene BVH, etc.)
- Descriptor Heap: Samplers

**Hit Group Root Signature:**
- SRV: `vertex_buf` (register t0, space 1)
- SRV: `index_buf` (register t1, space 1)
- SRV: `normal_buf` (register t2, space 1)
- SRV: `uv_buf` (register t3, space 1)
- Constants: `MeshData` (register b0, space 1)

---

## Part 8: CMake Integration Details

### 8.1 Current DXC Compilation

**From `FindD3D12.cmake`, lines 95-102:**

```cmake
add_custom_command(OUTPUT ${DXIL_EMBED_FILE}
    COMMAND ${D3D12_SHADER_COMPILER} ${MAIN_SHADER}
    -T lib_6_3 -Fh ${DXIL_EMBED_FILE} -Vn ${FNAME}_dxil
    ${HLSL_INCLUDE_DIRS} ${HLSL_COMPILE_DEFNS} ${DXIL_COMPILE_OPTIONS}
    DEPENDS ${HLSL_SRCS}
    COMMENT "Compiling and embedding ${MAIN_SHADER} as ${FNAME}_dxil")
```

**Breakdown:**
- `${D3D12_SHADER_COMPILER}` = `dxc.exe` path
- `${MAIN_SHADER}` = `render_dxr.hlsl`
- `-T lib_6_3` = Shader model 6.3 library
- `-Fh` = Output as C header file
- `-Vn ${FNAME}_dxil` = Variable name in header
- Include/define flags passed through

### 8.2 Proposed Slang Replacement

**Option 1: Direct Slang Command**

```cmake
if(ENABLE_DXR_SLANG)
    add_custom_command(OUTPUT ${DXIL_EMBED_FILE}
        COMMAND ${SLANG_COMPILER} ${MAIN_SHADER}
        -target dxil -profile sm_6_3
        -o ${DXIL_EMBED_FILE} -embed-dxil -var-name ${FNAME}_dxil
        ${HLSL_INCLUDE_DIRS} ${HLSL_COMPILE_DEFNS} ${DXIL_COMPILE_OPTIONS}
        DEPENDS ${HLSL_SRCS}
        COMMENT "Compiling with Slang and embedding ${MAIN_SHADER}")
else()
    # Original DXC path
endif()
```

**Option 2: Use SlangShaderCompiler Utility**

Keep CMake invoking DXC, but add a Slang path later for runtime compilation.

---

## Part 9: Testing Strategy

### 9.1 Verification Points

1. **Build Verification:**
   - Embedded header file generated
   - Same size as DXC output (approximately)
   - Compiles without errors

2. **Runtime Verification:**
   - Pipeline state object created successfully
   - No D3D12 validation errors
   - Renders same output as DXC version

3. **Performance Verification:**
   - Render times within 5% of DXC version
   - No frame time spikes

### 9.2 Comparison Tools

**Binary Comparison:**
```powershell
# Disassemble DXC output
dxc -dumpbin render_dxr_dxil_dxc.dxil > dxc_asm.txt

# Disassemble Slang output
dxc -dumpbin render_dxr_dxil_slang.dxil > slang_asm.txt

# Compare
fc dxc_asm.txt slang_asm.txt
```

**Visual Comparison:**
- Render same scene with both versions
- Use image diff tool
- Should be pixel-perfect (deterministic)

---

## Part 10: Integration Checklist

### Phase 1: CMake Build-Time Integration

- [ ] Modify `FindD3D12.cmake` to detect Slang compiler
- [ ] Add `ENABLE_DXR_SLANG` CMake option to `backends/CMakeLists.txt`
- [ ] Update `add_dxil_embed_library()` to support Slang path
- [ ] Test compilation: `cmake --build build --target dxr_shaders`
- [ ] Verify embedded header is generated
- [ ] Build full `crt_dxr` target
- [ ] Run application and verify rendering
- [ ] Compare output image with DXC version
- [ ] Disassemble and compare DXIL bytecode
- [ ] Performance benchmark (render time comparison)

### Phase 2: Runtime Compilation (Optional)

- [ ] Create `SlangShaderCompiler` instance in `RenderDXR`
- [ ] Add `#ifdef USE_SLANG_RUNTIME` paths
- [ ] Load shader source files from disk
- [ ] Compile HLSL to DXIL at runtime
- [ ] Create `ShaderLibrary` from runtime-compiled bytecode
- [ ] Test hot-reload capability
- [ ] Measure startup time impact

### Phase 3: Native Slang Shaders (Future)

- [ ] Port `render_dxr.hlsl` to `.slang` syntax
- [ ] Update include paths
- [ ] Test cross-compilation to Vulkan SPIRV
- [ ] Verify both backends work from same source

---

## Part 11: Known Challenges

### 11.1 Shader Model Compatibility

**Current:** DXR requires shader model 6.3+ for ray tracing
**Slang Support:** Verify Slang properly targets `lib_6_3` profile
**Test:** Check for ray tracing intrinsics support

### 11.2 Include Paths

**Current:** HLSL uses `#include` with relative paths
**Slang:** May need to adjust include search paths
**Test:** Ensure all `#include` directives resolve correctly

### 11.3 Embedded Header Format

**DXC Output:**
```cpp
unsigned char render_dxr_dxil[] = { 0x44, 0x58, ... };
```

**Slang:** Must produce identical format for drop-in replacement
**Test:** Verify header file structure matches

### 11.4 Debug Information

**DXC:** Can embed debug info with `-Zi` flag
**Slang:** Check if debug info is supported/needed
**Impact:** PIX debugging capabilities

---

## Summary

### Critical Integration Points

1. **CMake Build System:**
   - `backends/dxr/cmake/FindD3D12.cmake` (function `add_dxil_embed_library`)
   - `backends/dxr/CMakeLists.txt` (shader library definition)

2. **Runtime Pipeline Creation:**
   - `backends/dxr/render_dxr.cpp` (function `build_raytracing_pipeline()`, lines 590-645)
   - Uses embedded bytecode from `render_dxr_embedded_dxil.h`

3. **Shader Source:**
   - `backends/dxr/render_dxr.hlsl` (main shader, 329 lines)
   - Entry points: RayGen, Miss, ClosestHit, ShadowMiss

### Recommended First Step

**Start with CMake-only integration:**
1. Modify `add_dxil_embed_library()` to call Slang instead of DXC
2. Keep runtime code completely unchanged
3. Verify output is identical
4. Benchmark performance

This approach:
- ✅ Lowest risk
- ✅ Easy to test
- ✅ Easy to rollback
- ✅ No runtime dependencies
- ✅ Follows existing architecture

---

**Next Actions:**
1. Review this document with integration guide
2. Create a test branch
3. Implement CMake changes
4. Test build and verify output
5. Document any differences found

---

*Document Created: October 9, 2025*  
*For: ChameleonRT Slang Integration - Phase 1, Part 2*
