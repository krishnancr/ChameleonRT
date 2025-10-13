# Phase 1 Quick Reference

**Status:** ✅ COMPLETE  
**Date:** October 13, 2025

---

## What Changed

### 1. Shader (render_dxr.hlsl)
- **Line 57:** Moved `Texture2D textures[]` from `register(t3)` to `register(t30)`
- **Lines 299-307:** Added global buffer declarations at `register(t10-t14, space0)`
- **Lines 364-414:** Added `ClosestHit_GlobalBuffers` shader (not used yet)

### 2. CPU (render_dxr.cpp)
- **Line 781:** Changed texture descriptor binding from base register 3 to 30

### 3. Scene Class (util/scene.h, util/scene.cpp)
- Changed from merged `Vertex` struct to 4 separate arrays:
  - `std::vector<glm::vec3> global_vertices`
  - `std::vector<glm::uvec3> global_indices`
  - `std::vector<glm::vec3> global_normals`
  - `std::vector<glm::vec2> global_uvs`
- Added `build_global_buffers()` with validation output

### 4. MeshDesc (util/mesh.h)
- Changed from 5 fields to 8 fields (32 bytes total)
- Added: `normalOffset`, `uvOffset`, `num_normals`, `num_uvs`, `pad`

---

## Register Layout (space0)

```
t0-t2:   scene, materials, lights (existing)
t10:     globalVertices (NEW)
t11:     globalIndices (NEW)
t12:     globalNormals (NEW)
t13:     globalUVs (NEW)
t14:     meshDescs (NEW)
t30+:    textures[] (moved from t3)
```

---

## Why This Design

1. **Moved textures to t30** - Unbounded array `textures[]` at t3 claimed t3→UINT_MAX, conflicting with t10-t14
2. **Global buffers in space0** - Clear intent (global resources), avoids space1 confusion
3. **Separate arrays** - Matches original DXR space1 layout (positions/normals/UVs separate)
4. **Offset-based indexing** - Slang-compatible (works in both DXR and Vulkan)

---

## Validation Output

```
[Phase 1] Building global buffers (separate arrays)...
[Phase 1] Global buffers created successfully:
  - Vertices (vec3):  8
  - Indices (uvec3):  12
  - Normals (vec3):   0
  - UVs (vec2):       0
  - sizeof(MeshDesc) = 32 bytes (expected: 32) ✅
```

---

## Next: Phase 2

Create D3D12 buffers and upload the global data to GPU.

See `PHASE1_CONSOLIDATED_COMPLETE.md` for full details.
