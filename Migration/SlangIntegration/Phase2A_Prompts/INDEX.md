# Phase 2A Prompt Index

Quick reference guide for executing Phase 2A: Global Buffer Refactor

---

## Quick Start

1. **Start Here:** Read `README.md` in this directory
2. **Review Plan:** Read `../Phase2A_GlobalBuffers_Plan.md`
3. **Execute Sequentially:** Use prompts 2A.0 → 2A.1 → 2A.2 → 2A.3 → 2A.4

---

## Prompt Files (Execute in Order)

| # | File | Phase | Duration | Code Changes | Status |
|---|------|-------|----------|--------------|--------|
| 1 | [Phase_2A.0_Preparation_Analysis.md](Phase_2A.0_Preparation_Analysis.md) | Analysis | 2-3h | ❌ None | ⬜ Not Started |
| 2 | [Phase_2A.1_CPU_Scene_Refactor.md](Phase_2A.1_CPU_Scene_Refactor.md) | CPU | 2-4h | `util/*` | ⬜ Not Started |
| 3 | [Phase_2A.2_DXR_Backend_Refactor.md](Phase_2A.2_DXR_Backend_Refactor.md) | DXR | 4-6h | `backends/dxr/*` | ⬜ Not Started |
| 4 | [Phase_2A.3_Vulkan_Backend_Refactor.md](Phase_2A.3_Vulkan_Backend_Refactor.md) | Vulkan | 4-6h | `backends/vulkan/*` | ⬜ Not Started |
| 5 | [Phase_2A.4_Final_Validation.md](Phase_2A.4_Final_Validation.md) | Validation | 2-3h | ❌ None | ⬜ Not Started |

**Total Estimated Time:** 14-22 hours

---

## Phase Status Tracking

Update this as you complete each phase:

### Phase 2A.0: Preparation & Analysis
- [ ] Started: [Date]
- [ ] Analysis documents created
- [ ] Baseline screenshots captured
- [ ] Files_To_Modify.md created
- [ ] Completed: [Date]

### Phase 2A.1: CPU Scene Refactor
- [ ] Started: [Date]
- [ ] New structs added to mesh.h
- [ ] Global buffers added to scene.h
- [ ] build_global_buffers() implemented
- [ ] CPU tests passing
- [ ] Completed: [Date]
- [ ] Git commit: [hash]

### Phase 2A.2: DXR Backend Refactor
- [ ] Started: [Date]
- [ ] BLAS strategy analyzed and documented
- [ ] HLSL shader updated
- [ ] C++ global buffers created
- [ ] Descriptor heap updated
- [ ] Root signature updated
- [ ] test_cube.obj renders identically
- [ ] Sponza renders identically
- [ ] Completed: [Date]
- [ ] Git commit: [hash]

### Phase 2A.3: Vulkan Backend Refactor
- [ ] Started: [Date]
- [ ] Vulkan BLAS strategy analyzed and documented
- [ ] GLSL shaders updated
- [ ] C++ global buffers created
- [ ] Descriptor sets updated
- [ ] SBT simplified
- [ ] test_cube.obj renders identically
- [ ] Sponza renders identically
- [ ] Completed: [Date]
- [ ] Git commit: [hash]

### Phase 2A.4: Final Validation
- [ ] Started: [Date]
- [ ] Cross-backend comparison complete
- [ ] Performance validation complete
- [ ] Memory validation complete
- [ ] Final documentation complete
- [ ] All validation checklists passed
- [ ] Completed: [Date]
- [ ] Git commit: [hash]

---

## Critical Checkpoints

### ✋ PAUSE: Before 2A.2 Implementation
**Required:** Complete Task 2A.2.0 - BLAS Building Strategy Analysis

**Create:** `../Phase2A_Analysis/2A.2.0_BLAS_Strategy.md`

**Decision:** How will BLAS be built with global buffers?

**Do not proceed** until strategy is documented.

---

### ✋ PAUSE: Before 2A.3 Implementation
**Required:** Complete Task 2A.3.0 - Vulkan BLAS Building Strategy Analysis

**Create:** `../Phase2A_Analysis/2A.3.0_Vulkan_BLAS_Strategy.md`

**Decision:** How will Vulkan BLAS be built with global buffers?

**Do not proceed** until strategy is documented.

---

### ✋ VALIDATION: After Each Backend
**DXR (2A.2):** Rendering must be IDENTICAL to baseline before proceeding to 2A.3

**Vulkan (2A.3):** Rendering must be IDENTICAL to baseline AND DXR before proceeding to 2A.4

**Do not skip validation** - fixing issues later is much harder.

---

## Common Issues Reference

### Compilation Errors
- **Missing includes:** Check that all headers are included
- **Undefined symbols:** Ensure implementation matches declaration
- **Struct size mismatch:** Verify C++ and shader struct layouts match

### Rendering Errors
- **Black screen:** Check descriptor bindings, shader compilation
- **Corrupted geometry:** Verify offset calculations, index adjustment
- **Wrong colors:** Check material ID mapping, texture sampling

### Crashes
- **During BLAS build:** Review BLAS strategy, check buffer lifetimes
- **During rendering:** Check InstanceID() mapping, buffer bounds
- **Out of memory:** Check buffer sizes, verify no leaks

### Performance Issues
- **Slow rendering:** Expected small overhead from indirection, should be minimal
- **Slow loading:** build_global_buffers() adds processing time, should be acceptable

---

## Files You'll Modify

### Phase 2A.1 (CPU)
```
util/
├── mesh.h          ← Add Vertex, MeshDesc, GeometryInstanceData
├── scene.h         ← Add global buffer fields, build_global_buffers()
└── scene.cpp       ← Implement build_global_buffers()
```

### Phase 2A.2 (DXR)
```
backends/dxr/
├── render_dxr.h      ← Add global buffer members
├── render_dxr.cpp    ← Create buffers, update descriptors/SBT
└── render_dxr.hlsl   ← Add global buffers, update ClosestHit
```

### Phase 2A.3 (Vulkan)
```
backends/vulkan/
├── render_vulkan.h    ← Add global buffer members
├── render_vulkan.cpp  ← Create buffers, update descriptors/SBT
└── hit.rchit          ← Add global buffers, update main()
```

---

## Documentation You'll Create

### Analysis Documents (Phase 2A.0)
```
Migration/SlangIntegration/Phase2A_Analysis/
├── Phase_2A.0_Summary.md
├── 2A.0.1_Scene_Loading_Analysis.md
├── 2A.0.2_DXR_Buffer_Analysis.md
├── 2A.0.3_Vulkan_Buffer_Analysis.md
└── Files_To_Modify.md
```

### Strategy Documents (Phases 2A.2, 2A.3)
```
Migration/SlangIntegration/Phase2A_Analysis/
├── 2A.2.0_BLAS_Strategy.md        ← CRITICAL: Created in 2A.2
└── 2A.3.0_Vulkan_BLAS_Strategy.md ← CRITICAL: Created in 2A.3
```

### Test Results (Phases 2A.1, 2A.2, 2A.3, 2A.4)
```
Migration/SlangIntegration/Phase2A_Analysis/
├── 2A.1.4_CPU_Test_Results.md
├── 2A.2.6_DXR_Test_Results.md
├── 2A.3.6_Vulkan_Test_Results.md
├── 2A.4.1_Cross_Backend_Comparison.md
├── 2A.4.2_Performance_Validation.md
└── 2A.4.3_Memory_Validation.md
```

### Final Summary (Phase 2A.4)
```
Migration/SlangIntegration/
└── Phase2A_Complete_Summary.md  ← Comprehensive summary
```

### Screenshots
```
Migration/SlangIntegration/Phase2A_Analysis/
├── baseline_dxr_cube.png          (2A.0)
├── baseline_dxr_sponza.png        (2A.0)
├── baseline_vulkan_cube.png       (2A.0)
├── baseline_vulkan_sponza.png     (2A.0)
├── final_dxr_cube.png             (2A.4)
├── final_dxr_sponza.png           (2A.4)
├── final_vulkan_cube.png          (2A.4)
└── final_vulkan_sponza.png        (2A.4)
```

---

## Git Workflow

### Recommended Commit Strategy

```bash
# After Phase 2A.0 (optional, no code changes)
git add Migration/SlangIntegration/Phase2A_Analysis/
git commit -m "Phase 2A.0: Analysis and baseline documentation"

# After Phase 2A.1
git add util/
git commit -m "Phase 2A.1: CPU scene refactor - global buffer creation"

# After Phase 2A.2
git add backends/dxr/ Migration/SlangIntegration/Phase2A_Analysis/2A.2*
git commit -m "Phase 2A.2: DXR backend refactor - global buffers"

# After Phase 2A.3
git add backends/vulkan/ Migration/SlangIntegration/Phase2A_Analysis/2A.3*
git commit -m "Phase 2A.3: Vulkan backend refactor - global buffers"

# After Phase 2A.4
git add Migration/SlangIntegration/
git commit -m "Phase 2A.4: Final validation complete - Phase 2A COMPLETE"
```

### Branch Strategy (Optional)

```bash
# Create feature branch for Phase 2A
git checkout -b feature/phase2a-global-buffers

# Work through phases, committing after each

# When all complete and validated:
git checkout feature/slang-integration
git merge feature/phase2a-global-buffers
```

---

## Success Criteria Summary

✅ **Phase 2A is complete when ALL of the following are true:**

1. **Code Compiles:** No errors, minimal warnings
2. **Visual Validation:** DXR and Vulkan render identically to each other and baseline
3. **Performance:** No significant regression (small overhead acceptable)
4. **Memory:** Usage reduced or similar to baseline
5. **Documentation:** All analysis and test documents created
6. **Git:** All changes committed with meaningful messages
7. **Validation Checklists:** All items in each prompt checklist marked complete

---

## Next Phase Preview

**Phase 2B: Shader Conversion to Slang**

After Phase 2A, you'll convert the native HLSL/GLSL shaders to Slang language while maintaining the global buffer architecture and identical rendering.

**Prerequisites for Phase 2B:**
- ✅ Phase 2A complete and validated
- Slang compiler integration working
- Understanding of Slang shader language syntax

---

## Need Help?

1. **Read the prompt** - Each prompt has troubleshooting sections
2. **Check README.md** - This directory's README has detailed guidance
3. **Review the plan** - Phase2A_GlobalBuffers_Plan.md has full context
4. **Check analysis docs** - Your phase 2A.0 analysis documents have architecture details

---

**Last Updated:** October 10, 2025  
**For:** ChameleonRT Phase 2A Global Buffer Refactor  
**Status:** Ready for execution
