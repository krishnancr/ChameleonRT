# DXR Backend Architecture Diagram

## Current Build & Runtime Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                        BUILD TIME (CMake)                            │
└─────────────────────────────────────────────────────────────────────┘

    HLSL Source Files                    CMake Configuration
    ┌────────────────┐                   ┌──────────────────────┐
    │ render_dxr.hlsl│                   │ CMakeLists.txt       │
    │ util.hlsl      │                   │ ├─ ENABLE_DXR        │
    │ disney_bsdf.hlsl│ ────────────────▶│ └─ add_dxil_embed_   │
    │ lcg_rng.hlsl   │                   │    library()         │
    │ lights.hlsl    │                   └──────────┬───────────┘
    └────────────────┘                              │
           │                                        │
           │                    ┌───────────────────┴──────────────────┐
           │                    │ FindD3D12.cmake                      │
           │                    │ function: add_dxil_embed_library()   │
           └───────────────────▶│ ┌──────────────────────────────────┐ │
                                │ │ Custom Command:                  │ │
                                │ │   dxc render_dxr.hlsl           │ │
                                │ │   -T lib_6_3                     │ │
                                │ │   -Fh render_dxr_embedded_dxil.h│ │
                                │ │   -Vn render_dxr_dxil            │ │
                                │ └──────────────────────────────────┘ │
                                └──────────────┬───────────────────────┘
                                               │
                                               ▼
                           Generated C Header File
                        ┌────────────────────────────────┐
                        │ render_dxr_embedded_dxil.h     │
                        │ ┌────────────────────────────┐ │
                        │ │ unsigned char              │ │
                        │ │ render_dxr_dxil[] = {      │ │
                        │ │   0x44, 0x58, 0x42, 0x43,  │ │
                        │ │   // ... DXIL bytecode ... │ │
                        │ │ };                         │ │
                        │ └────────────────────────────┘ │
                        └────────────┬───────────────────┘
                                     │
                                     │ #include
                                     ▼
                            ┌────────────────────┐
                            │ render_dxr.cpp     │
                            │ (compiled to .obj) │
                            └─────────┬──────────┘
                                      │
                                      │ link
                                      ▼
                            ┌────────────────────┐
                            │ crt_dxr.dll        │
                            │ (contains bytecode)│
                            └────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                        RUNTIME (Application)                         │
└─────────────────────────────────────────────────────────────────────┘

    Application Startup
    ┌────────────────┐
    │ main.cpp       │
    │ loads crt_dxr  │
    └────────┬───────┘
             │
             ▼
    ┌────────────────────────────────┐
    │ RenderDXR::set_scene()         │
    │ ┌────────────────────────────┐ │
    │ │ Upload geometry            │ │
    │ │ Build BVH                  │ │
    │ │ Upload textures/materials  │ │
    │ └────────────────────────────┘ │
    └────────┬───────────────────────┘
             │
             ▼
    ┌────────────────────────────────────────────┐
    │ RenderDXR::build_raytracing_pipeline()    │
    │ ┌────────────────────────────────────────┐ │
    │ │ Line 593:                              │ │
    │ │   dxr::ShaderLibrary shader_library(   │ │
    │ │     render_dxr_dxil,          ◄────────┼─┼─ From embedded array
    │ │     sizeof(render_dxr_dxil),           │ │
    │ │     {L"RayGen", L"Miss", ...})         │ │
    │ └────────────────────────────────────────┘ │
    │                                            │
    │ ┌────────────────────────────────────────┐ │
    │ │ Create root signatures                 │ │
    │ │   - Global (empty)                     │ │
    │ │   - RayGen (scene params + heaps)      │ │
    │ │   - HitGroup (vertex/index buffers)    │ │
    │ └────────────────────────────────────────┘ │
    │                                            │
    │ ┌────────────────────────────────────────┐ │
    │ │ RTPipelineBuilder()                    │ │
    │ │   .add_shader_library(shader_library)  │ │
    │ │   .set_ray_gen(L"RayGen")              │ │
    │ │   .add_miss_shader(L"Miss")            │ │
    │ │   .add_hit_group(...)                  │ │
    │ │   .create(device)                      │ │
    │ └────────────────────────────────────────┘ │
    └────────┬───────────────────────────────────┘
             │
             ▼
    ┌────────────────────────────────┐
    │ D3D12 State Object             │
    │ ┌────────────────────────────┐ │
    │ │ DXIL Library               │ │
    │ │ ├─ Export: RayGen          │ │
    │ │ ├─ Export: Miss            │ │
    │ │ ├─ Export: ShadowMiss      │ │
    │ │ └─ Export: ClosestHit      │ │
    │ │                            │ │
    │ │ Pipeline Config            │ │
    │ │ ├─ Max Recursion: 1        │ │
    │ │ ├─ Payload Size: 32 bytes  │ │
    │ │ └─ Attributes: 8 bytes     │ │
    │ │                            │ │
    │ │ Shader Binding Table       │ │
    │ │ ├─ RayGen Group            │ │
    │ │ ├─ Miss Groups (x2)        │ │
    │ │ └─ HitGroups (per geom)    │ │
    │ └────────────────────────────┘ │
    └────────┬───────────────────────┘
             │
             ▼
    ┌────────────────────────────────┐
    │ RenderDXR::render()            │
    │ ┌────────────────────────────┐ │
    │ │ Update view parameters     │ │
    │ │ DispatchRays()             │ │
    │ │ Readback framebuffer       │ │
    │ └────────────────────────────┘ │
    └────────────────────────────────┘
```

---

## Shader Entry Point Flow

```
┌──────────────────────────────────────────────────────────────┐
│                     render_dxr.hlsl                           │
│                     (Single DXIL Library)                     │
└──────────────────────────────────────────────────────────────┘

    Includes:
    ┌──────────────┐
    │ util.hlsl    │ ───┐
    │ lcg_rng.hlsl │ ───┤
    │ disney_bsdf  │ ───┼──▶ Compiled together
    │ lights.hlsl  │ ───┤    as library
    └──────────────┘    │
                        │
    Entry Points:       │
    ┌──────────────────────────────────────┐
    │ [shader("raygeneration")]            │
    │ void RayGen(...)                     │ ─┐
    │   ├─ Generate camera ray             │  │
    │   ├─ TraceRay()                      │  │
    │   └─ Write to output UAV             │  │
    └──────────────────────────────────────┘  │
                                              │
    ┌──────────────────────────────────────┐  │
    │ [shader("miss")]                     │  │
    │ void Miss(inout Payload p)           │  │ Exported
    │   └─ Return background color         │  │ to D3D12
    └──────────────────────────────────────┘  │ State Object
                                              │
    ┌──────────────────────────────────────┐  │
    │ [shader("miss")]                     │  │
    │ void ShadowMiss(inout ShadowPayload) │  │
    │   └─ Set not shadowed                │  │
    └──────────────────────────────────────┘  │
                                              │
    ┌──────────────────────────────────────┐  │
    │ [shader("closesthit")]               │  │
    │ void ClosestHit(inout Payload p,     │  │
    │                 BuiltInAttribs attr) │ ─┘
    │   ├─ Load vertex data                │
    │   ├─ Interpolate attributes          │
    │   ├─ Evaluate Disney BRDF            │
    │   ├─ Sample direct lighting          │
    │   └─ Trace shadow rays               │
    └──────────────────────────────────────┘
```

---

## Resource Binding Layout

```
┌─────────────────────────────────────────────────────────────────┐
│                   RayGen Shader Bindings                         │
│                   (Root Signature: raygen_root_sig)              │
└─────────────────────────────────────────────────────────────────┘

Root Constants (b1):
┌──────────────────────┐
│ SceneParams          │
│ ├─ num_lights        │
└──────────────────────┘

Descriptor Heap (CBV/SRV/UAV):
┌──────────────────────────────────┐
│ u0: output (RWTexture2D)         │ ◄── Render target
│ u1: accum_buffer (RWTexture2D)   │ ◄── Accumulation
│ u2: ray_stats (RWTexture2D)      │ ◄── Debug stats (optional)
│ t0: scene (TLAS)                 │ ◄── Top-level BVH
│ t1: material_params (Buffer)     │ ◄── Material data
│ t2: lights (Buffer)              │ ◄── Light sources
│ t3+: textures[] (Texture2D)      │ ◄── Bindless textures
└──────────────────────────────────┘

Descriptor Heap (Samplers):
┌──────────────────────┐
│ s0: tex_sampler      │
└──────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                   Hit Group Bindings                             │
│                   (Root Signature: hitgroup_root_sig)            │
└─────────────────────────────────────────────────────────────────┘

Per-Instance SRVs (space 1):
┌──────────────────────┐
│ t0: vertex_buf       │ ◄── Vertex positions
│ t1: index_buf        │ ◄── Triangle indices
│ t2: normal_buf       │ ◄── Vertex normals
│ t3: uv_buf           │ ◄── Texture coordinates
└──────────────────────┘

Root Constants (b0, space 1):
┌──────────────────────┐
│ MeshData             │
│ ├─ material_id       │
│ ├─ has_normals       │
│ └─ has_uvs           │
└──────────────────────┘
```

---

## Slang Integration Points

```
┌─────────────────────────────────────────────────────────────────┐
│           OPTION A: CMake Build-Time Integration                │
│                    (Recommended First)                           │
└─────────────────────────────────────────────────────────────────┘

    Current:                              Slang:
    
    DXC Compiler                          Slang Compiler
    ┌──────────────┐                     ┌──────────────┐
    │ dxc.exe      │                     │ slangc.exe   │
    │ -T lib_6_3   │ ──────────▶         │ -target dxil │
    │ -Fh header.h │                     │ -profile sm_6_3
    └──────────────┘                     │ -embed-dxil  │
                                         └──────────────┘
    
    Change Location:
    └─▶ backends/dxr/cmake/FindD3D12.cmake
        Function: add_dxil_embed_library()
        Lines: 95-102

    Impact:
    ✓ No C++ code changes
    ✓ Same embedded header output
    ✓ Drop-in replacement
    ✗ Still build-time only

┌─────────────────────────────────────────────────────────────────┐
│           OPTION B: Runtime Compilation Integration             │
│                    (Optional Later)                              │
└─────────────────────────────────────────────────────────────────┘

    Add to RenderDXR class:
    ┌────────────────────────────────────────┐
    │ #ifdef USE_SLANG_RUNTIME               │
    │   SlangShaderCompiler slangCompiler;   │
    │ #endif                                 │
    └────────────────────────────────────────┘
    
    Modify build_raytracing_pipeline():
    ┌────────────────────────────────────────────────┐
    │ #ifdef USE_SLANG_RUNTIME                       │
    │   auto hlsl = loadFile("render_dxr.hlsl");    │
    │   auto result = slangCompiler.compileHLSL...  │
    │   ShaderLibrary lib(result->bytecode, ...);   │
    │ #else                                          │
    │   ShaderLibrary lib(render_dxr_dxil, ...);    │ ◄── Embedded
    │ #endif                                         │
    └────────────────────────────────────────────────┘
    
    Change Location:
    └─▶ backends/dxr/render_dxr.h (add member)
    └─▶ backends/dxr/render_dxr.cpp (build_raytracing_pipeline)
    
    Impact:
    ✓ Faster iteration (no rebuild)
    ✓ Shader hot-reload possible
    ✗ Runtime Slang dependency
    ✗ Slower startup
```

---

## File Dependencies Map

```
backends/dxr/
│
├─ render_dxr.h ────────────────────┐
│  └─ Class: RenderDXR              │
│     ├─ Member: rt_pipeline        │  Includes
│     ├─ Member: device             │  ────────────┐
│     └─ Method: build_*_pipeline() │              │
│                                    │              ▼
├─ render_dxr.cpp ◄─────────────────┘     ┌────────────────┐
│  ├─ #include render_dxr.h               │ render_backend.h│
│  ├─ #include render_dxr_embedded_dxil.h │ (interface)     │
│  └─ Implements: build_raytracing_*      └────────────────┘
│
├─ render_dxr.hlsl ──────────┐
│  ├─ Entry: RayGen          │
│  ├─ Entry: Miss            │  Compiled by
│  ├─ Entry: ShadowMiss      │  ────────────┐
│  └─ Entry: ClosestHit      │              │
│                             │              ▼
├─ util.hlsl ────────────────┤      ┌───────────────┐
├─ disney_bsdf.hlsl ─────────┤      │ DXC or Slang  │
├─ lcg_rng.hlsl ─────────────┤      │ Compiler      │
├─ lights.hlsl ──────────────┘      └───────┬───────┘
│                                            │
├─ dxr_utils.h/cpp                           │ Generates
│  ├─ Class: ShaderLibrary                   │
│  ├─ Class: RTPipelineBuilder               ▼
│  └─ Class: RootSignature         ┌──────────────────────────┐
│                                   │ render_dxr_embedded_dxil.h│
├─ dx12_utils.h/cpp                 │ unsigned char            │
│  └─ D3D12 helpers                 │ render_dxr_dxil[] = {...}│
│                                   └──────────────────────────┘
├─ CMakeLists.txt
│  ├─ add_library(crt_dxr ...)
│  └─ add_dxil_embed_library(dxr_shaders ...)
│                     │
└─ cmake/            │
   ├─ FindD3D12.cmake◄┘
   │  └─ function: add_dxil_embed_library()  ◄── KEY INTEGRATION POINT
   └─ FindWinPixEventRuntime.cmake
```

---

**See also:**
- `DXR_INTEGRATION_TARGET_FILES.md` - Detailed analysis
- `DXR_QUICK_REFERENCE.md` - Quick lookup guide
- `INTEGRATION_GUIDE.md` - General integration steps
