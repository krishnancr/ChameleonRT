# Global Buffer Refactor

**Goal:** Refactor scene loading to use global buffers instead of per-geometry buffers  
**Status:** In Progress  
**Date Started:** October 10, 2025

---

## Overview

This refactor eliminates per-geometry buffers in favor of a single global buffer approach. This simplifies resource binding and prepares the codebase for eventual Slang shader integration.

**Key Changes:**
1. **CPU:** Merge all geometry data into global arrays during scene loading
2. **DXR:** Update to use global buffers (space0), remove local root signatures (space1)
3. **Vulkan:** Update to use global buffers (set0), remove shader record buffer addresses
4. **Validation:** Both backends render identically to baseline

---

## Success Criteria

✅ **CPU Scene Loading:**
- Single global vertex buffer (all geometries concatenated)
- Single global index buffer (all indices concatenated with adjusted offsets)
- MeshDesc array (metadata: `{ vbOffset, ibOffset, materialID }`)
- GeometryInstanceData array (metadata: `{ meshID, matrixID }`)

✅ **DXR Backend:**
- No local root signature (remove space1)
- All resources bound in space0
- Shader uses global buffers with offset indirection
- Renders identically to baseline (test_cube.obj, Sponza)

✅ **Vulkan Backend:**
- No shader record buffer addresses
- All resources bound in set0
- Shader uses global buffers with offset indirection
- Renders identically to baseline (test_cube.obj, Sponza)

---

## Status

### CPU Scene Refactor
- ✅ **COMPLETE** - Commits: 66f10b01, e89498ed
- See [CPU/](CPU/) directory for details

### DXR Backend
- 🔄 **IN PROGRESS** - Option A implementation (temporary per-geometry buffers for BLAS)
- See [DXR/](DXR/) directory for strategy and analysis

### Vulkan Backend
- ⏸️ **PENDING** - Waiting for DXR completion
- See [Vulkan/](Vulkan/) directory for planning

---

## Directory Structure

```
GlobalBufferRefactor/
├── README.md (this file)
├── baseline_*.png (validation screenshots)
├── CPU/
│   ├── Analysis.md - Original scene loading analysis
│   ├── COMPLETE.md - Implementation summary
│   ├── Test_Results.md - test_cube.obj validation
│   ├── Sponza_Validation.md - Sponza scene validation
│   └── Validation_Complete.md - Final CPU validation
├── DXR/
│   ├── BLAS_STRATEGY.md - BLAS build strategy decision (Option A)
│   └── Buffer_Analysis.md - DXR buffer architecture analysis
├── Vulkan/
│   └── Buffer_Analysis.md - Vulkan buffer architecture analysis
└── Concepts/
    └── D3D12_Resource_Binding.md - Educational guide to D3D12 resource binding
```

---

## Related Work

- **Phase 2B:** Unified Slang Shaders - See [SlangShaders/MASTER_PLAN.md](../SlangShaders/MASTER_PLAN.md)
- **Archive:** Previous attempts and outdated docs - See [Archive/Phase2A_FailedAttempts/](../Archive/Phase2A_FailedAttempts/)

---

## Git Commits

- **CPU Refactor Complete:** 66f10b01, e89498ed (October 11, 2025)
- **Documentation Cleanup:** [Current commit]

---

## Next Steps

1. Complete DXR Option A implementation
2. Validate DXR rendering against baseline
3. Implement Vulkan backend refactor
4. Validate Vulkan rendering against baseline
5. Final cross-backend validation
6. Begin Phase 2B (Slang shader unification)
