# DXR Global Buffer Refactor - Quick Reference

**Status:** ✅ Complete | **Date:** 2025-10-14

---

## Quick Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                  DXR Global Buffer System                   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  CPU (Scene Building)          GPU (Ray Tracing)           │
│  ├─ global_vertices[]    →    ├─ globalVertices (t10)      │
│  ├─ global_indices[]     →    ├─ globalIndices (t11)       │
│  ├─ global_normals[]     →    ├─ globalNormals (t12)       │
│  ├─ global_uvs[]         →    ├─ globalUVs (t13)           │
│  └─ mesh_descriptors[]   →    └─ meshDescs (t14)           │
│                                                             │
│  SBT: meshDescIndex → meshDescs[index] → offsets           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Critical Code Patterns

### Shader Indexing (render_dxr.hlsl)

```hlsl
// Get geometry descriptor from SBT
const uint32_t meshID = meshDescIndex;  // From local root sig (b2)
MeshDesc mesh = meshDescs[meshID];      // Lookup descriptor

// Load triangle indices (ALREADY GLOBAL)
uint3 idx = globalIndices[mesh.ibOffset + PrimitiveIndex()];

// ✅ Vertices: Direct indexing (idx is global)
float3 va = globalVertices[idx.x];
float3 vb = globalVertices[idx.y];
float3 vc = globalVertices[idx.z];

// ✅ UVs: Convert to local, then offset
uint3 local_idx = idx - uint3(mesh.vbOffset, mesh.vbOffset, mesh.vbOffset);
float2 uva = globalUVs[mesh.uvOffset + local_idx.x];
float2 uvb = globalUVs[mesh.uvOffset + local_idx.y];
float2 uvc = globalUVs[mesh.uvOffset + local_idx.z];

// Same pattern for normals
float3 na = globalNormals[mesh.normalOffset + local_idx.x];
```

**Key Rule:** Indices are global, UVs/normals need local conversion.

---

## Buffer Layouts

### MeshDesc Structure
```cpp
struct MeshDesc {
    uint32_t vbOffset;      // Offset into globalVertices
    uint32_t ibOffset;      // Offset into globalIndices
    uint32_t normalOffset;  // Offset into globalNormals
    uint32_t uvOffset;      // Offset into globalUVs
    uint32_t num_normals;   // Count (0 if none)
    uint32_t num_uvs;       // Count (0 if none)
    uint32_t material_id;   // Material index
    uint32_t pad;           // Alignment
};  // 32 bytes total
```

### Global Buffers
| Buffer | Type | Register | Stride |
|--------|------|----------|--------|
| globalVertices | `StructuredBuffer<float3>` | t10, space0 | 12 bytes |
| globalIndices | `StructuredBuffer<uint3>` | t11, space0 | 12 bytes |
| globalNormals | `StructuredBuffer<float3>` | t12, space0 | 12 bytes |
| globalUVs | `StructuredBuffer<float2>` | t13, space0 | 8 bytes |
| meshDescs | `StructuredBuffer<MeshDesc>` | t14, space0 | 32 bytes |

---

## Root Signatures

### Global Root Signature
```cpp
dxr::RootSignature::global()
    .add_desc_heap("cbv_srv_uav_heap", raygen_desc_heap)  // All SRVs/UAVs
    .add_desc_heap("sampler_heap", raygen_sampler_heap)   // Samplers
    .create(device);
```

### Local Root Signatures
```cpp
// RayGen (b1, space0)
.add_constants("SceneParams", 1, 1, 0)  // num_lights

// HitGroup (b2, space0)
.add_constants("HitGroupData", 2, 1, 0)  // meshDescIndex
```

**Space0 Only:** ✅ Slang-compatible

---

## Build & Test Commands

### Build
```powershell
# CMake configure
cmake -B build -DENABLE_DXR=ON -DENABLE_SLANG=ON -DSlang_ROOT=C:\dev\slang\build\Debug

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --config Release
```

### Run Tests
```powershell
# Simple scene
.\build\Debug\chameleonrt.exe dxr "C:\Demo\Assets\cube\cube.obj"

# Complex scene (381 geometries)
.\build\Debug\chameleonrt.exe dxr "C:\Demo\Assets\sponza\sponza.obj"
```

**Expected:** No errors, correct rendering, console shows SBT mapping.

---

## Common Bugs & Fixes

| Bug | Symptom | Cause | Fix |
|-----|---------|-------|-----|
| **Double-offset (vertices)** | Black screen | `globalVertices[mesh.vbOffset + idx.x]` | Use `globalVertices[idx.x]` |
| **Double-offset (UVs)** | Missing textures | `globalUVs[mesh.uvOffset + idx.x]` | Convert to local: `idx.x - mesh.vbOffset` |
| **Slang compile fail** | Nullptr result | Missing search paths | Add shader dir + util dir |
| **SBT crash** | Access violation | meshDescIndex not written | Check `build_shader_binding_table()` |

---

## File Locations

### Key Files
```
backends/dxr/
├── render_dxr.hlsl       ← Shaders (ClosestHit, RayGen, Miss)
├── render_dxr.cpp        ← Pipeline, SBT, descriptors
├── render_dxr.h          ← Global buffer members
└── CMakeLists.txt        ← Shader compilation

util/
├── scene.cpp             ← build_global_buffers()
├── scene.h               ← Global buffer declarations
└── mesh.h                ← MeshDesc structure

Migration/SlangIntegration/GlobalBufferRefactor/DXR/
├── COMPLETION_REPORT.md  ← High-level summary
├── TECHNICAL_NOTES.md    ← Deep dive
└── QUICK_REFERENCE.md    ← This file
```

---

## Validation Checklist

- [x] Builds (Debug + Release)
- [x] Embedded DXIL works
- [x] Slang compilation works
- [x] Cube renders correctly
- [x] Sponza renders correctly
- [x] No console errors
- [x] GPU addresses valid (non-zero)
- [x] SBT mapping verified
- [x] Old code removed (no phase markers)

---

## Next Phase: Vulkan

Apply same pattern to Vulkan backend:
1. Create global buffers (VkBuffer)
2. Update descriptor sets (equivalent of space0)
3. Modify `ClosestHit` shader (same logic as DXR)
4. Update SBT (VkStridedDeviceAddressRegion)
5. Test with SPIR-V compilation via Slang

**Target:** Same pixel-perfect rendering as DXR.

---

## Contact & Support

- **Documentation:** `Migration/INTEGRATION_GUIDE.md`
- **Slang Docs:** `slang/docs/`
- **ChameleonRT:** `.github/copilot-instructions.md`

**For debugging:** Enable console output in `render_dxr.cpp` (already in place).

---

**Quick Start:**
1. Read `COMPLETION_REPORT.md` for overview
2. Reference `TECHNICAL_NOTES.md` for deep dive
3. Use this file for day-to-day lookups
4. Run test suite before committing changes

✅ **DXR Global Buffer Refactor Complete**
