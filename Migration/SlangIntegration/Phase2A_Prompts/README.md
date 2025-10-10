# Phase 2A Prompts - Usage Guide

This directory contains detailed Copilot prompts for executing Phase 2A: Global Buffer Refactor.

---

## Overview

Phase 2A refactors ChameleonRT from per-geometry buffers to global buffers. This is a prerequisite for eventual Slang shader unification, but **uses only native HLSL/GLSL shaders** (NO Slang in this phase).

**Goal:** Both DXR and Vulkan backends should render identically using global buffers.

---

## Prompt Files

### 1. Phase_2A.0_Preparation_Analysis.md
**Phase:** Analysis and Documentation  
**Duration:** ~2-3 hours  
**Code Changes:** NONE (analysis only)

**Purpose:**
- Understand current architecture
- Document scene loading, DXR buffers, Vulkan buffers
- Capture baseline screenshots
- Create list of files to modify

**Deliverables:**
- Analysis documents in `Migration/SlangIntegration/Phase2A_Analysis/`
- Baseline screenshots
- Files_To_Modify.md

**Use when:** Starting Phase 2A from scratch

---

### 2. Phase_2A.1_CPU_Scene_Refactor.md
**Phase:** CPU-side implementation  
**Duration:** ~2-4 hours  
**Code Changes:** `util/mesh.h`, `util/scene.h`, `util/scene.cpp`

**Purpose:**
- Add new data structures (Vertex, MeshDesc, GeometryInstanceData)
- Implement build_global_buffers() function
- Concatenate all geometry into global arrays
- Test with debug output

**Deliverables:**
- Global buffers created during scene loading
- CPU test results document
- No GPU code touched

**Use when:** Phase 2A.0 complete, ready to modify CPU code

---

### 3. Phase_2A.2_DXR_Backend_Refactor.md
**Phase:** DXR backend implementation  
**Duration:** ~4-6 hours  
**Code Changes:** `backends/dxr/*`

**Purpose:**
- **CRITICAL:** Analyze BLAS building strategy first
- Update DXR shaders to use global buffers
- Update C++ to create global buffers
- Remove space1 local root signature
- Update descriptor heap and root signature

**Deliverables:**
- DXR renders identically to baseline
- BLAS strategy documented
- DXR test results

**Use when:** Phase 2A.1 complete, CPU global buffers working

**⚠️ IMPORTANT:** Contains PAUSE POINT for BLAS strategy analysis

---

### 4. Phase_2A.3_Vulkan_Backend_Refactor.md
**Phase:** Vulkan backend implementation  
**Duration:** ~4-6 hours  
**Code Changes:** `backends/vulkan/*`

**Purpose:**
- **CRITICAL:** Analyze Vulkan BLAS building strategy first
- Update Vulkan shaders to use global buffers
- Update C++ to create global buffers
- Remove buffer_reference and shaderRecordEXT usage
- Update descriptor sets

**Deliverables:**
- Vulkan renders identically to baseline and DXR
- Vulkan BLAS strategy documented
- Vulkan test results

**Use when:** Phase 2A.2 complete, DXR validated

**⚠️ IMPORTANT:** Contains PAUSE POINT for Vulkan BLAS strategy analysis

---

### 5. Phase_2A.4_Final_Validation.md
**Phase:** Comprehensive validation and documentation  
**Duration:** ~2-3 hours  
**Code Changes:** NONE (testing and documentation only)

**Purpose:**
- Cross-backend visual comparison (DXR vs Vulkan)
- Performance validation (no regression)
- Memory validation (reduced usage)
- Final documentation and summary

**Deliverables:**
- Complete Phase 2A summary
- Validation results for all tests
- Updated STATUS.md
- Ready for Phase 2B

**Use when:** Phase 2A.3 complete, both backends working

---

## How to Use These Prompts

### Step 1: Sequential Execution
Execute prompts in order (2A.0 → 2A.1 → 2A.2 → 2A.3 → 2A.4). Each phase builds on the previous.

### Step 2: Copy and Paste
1. Open the prompt file in your editor
2. Read through it to understand the phase
3. Copy the ENTIRE prompt content
4. Paste into GitHub Copilot Chat
5. Follow Copilot's guidance step-by-step

### Step 3: Deliverables
Each prompt specifies deliverables (files to create, tests to run). Complete all deliverables before moving to next phase.

### Step 4: Validation
Each phase has a validation checklist at the end. Ensure all items are checked before proceeding.

### Step 5: Git Commits
Each prompt includes recommended git commit messages. Commit after each phase for easy rollback if needed.

---

## Critical Pause Points

### 🛑 Phase 2A.2 - Task 2A.2.0: BLAS Building Strategy

**STOP and analyze** how DXR currently builds BLAS before implementing code changes.

**Decision needed:** How will BLAS be built with global buffers?
- Option A: Temporary per-geometry buffers (just for BLAS, then discard)
- Option B: Global buffers with offset parameters
- Option C: Extract from global buffers into temporary memory

**Document your decision** in `2A.2.0_BLAS_Strategy.md` before continuing.

### 🛑 Phase 2A.3 - Task 2A.3.0: Vulkan BLAS Building Strategy

**STOP and analyze** how Vulkan currently builds BLAS.

**Decision needed:** Can we reuse DXR strategy? Vulkan-specific approach?

**Document your decision** in `2A.3.0_Vulkan_BLAS_Strategy.md` before continuing.

---

## Expected Timeline

| Phase | Duration | Cumulative |
|-------|----------|------------|
| 2A.0  | 2-3 hours | 2-3 hours |
| 2A.1  | 2-4 hours | 4-7 hours |
| 2A.2  | 4-6 hours | 8-13 hours |
| 2A.3  | 4-6 hours | 12-19 hours |
| 2A.4  | 2-3 hours | 14-22 hours |

**Total:** ~14-22 hours of work (depending on issues encountered)

**Recommendation:** Break into multiple sessions, commit after each phase.

---

## Testing Strategy

### Per-Phase Testing

Each implementation phase (2A.1, 2A.2, 2A.3) includes testing:
- **2A.1:** CPU debug output validation
- **2A.2:** DXR rendering validation (vs baseline)
- **2A.3:** Vulkan rendering validation (vs baseline and DXR)

### Final Testing (2A.4)

Comprehensive cross-backend validation:
- Visual comparison (DXR vs Vulkan vs Baseline)
- Performance comparison
- Memory usage comparison

### Test Scenes

Primary: `test_cube.obj` (simple, fast iteration)  
Secondary: `Sponza.obj` (complex, validates real-world usage)

---

## Directory Structure After Completion

```
Migration/SlangIntegration/
├── Phase2A_GlobalBuffers_Plan.md          # Original plan
├── Phase2A_Complete_Summary.md            # Final summary (created in 2A.4)
├── Phase2A_Prompts/                       # This directory
│   ├── README.md                          # This file
│   ├── Phase_2A.0_Preparation_Analysis.md
│   ├── Phase_2A.1_CPU_Scene_Refactor.md
│   ├── Phase_2A.2_DXR_Backend_Refactor.md
│   ├── Phase_2A.3_Vulkan_Backend_Refactor.md
│   └── Phase_2A.4_Final_Validation.md
└── Phase2A_Analysis/                      # Created during execution
    ├── Phase_2A.0_Summary.md
    ├── 2A.0.1_Scene_Loading_Analysis.md
    ├── 2A.0.2_DXR_Buffer_Analysis.md
    ├── 2A.0.3_Vulkan_Buffer_Analysis.md
    ├── Files_To_Modify.md
    ├── baseline_dxr_cube.png
    ├── baseline_dxr_sponza.png
    ├── baseline_vulkan_cube.png
    ├── baseline_vulkan_sponza.png
    ├── 2A.1.4_CPU_Test_Results.md
    ├── 2A.2.0_BLAS_Strategy.md
    ├── 2A.2.6_DXR_Test_Results.md
    ├── 2A.3.0_Vulkan_BLAS_Strategy.md
    ├── 2A.3.6_Vulkan_Test_Results.md
    ├── 2A.4.1_Cross_Backend_Comparison.md
    ├── 2A.4.2_Performance_Validation.md
    ├── 2A.4.3_Memory_Validation.md
    ├── final_dxr_cube.png
    ├── final_dxr_sponza.png
    ├── final_vulkan_cube.png
    └── final_vulkan_sponza.png
```

---

## Troubleshooting

### Issue: Copilot not following prompt structure

**Solution:** Break the prompt into smaller sections, paste one section at a time.

### Issue: Code doesn't compile after implementing changes

**Solution:** Review the validation checklist in the prompt. Each prompt includes common issues and debugging steps.

### Issue: Rendering doesn't match baseline

**Solution:** DO NOT proceed to next phase. Use the debugging section in the prompt to investigate. Common causes:
- Offset calculation errors
- Descriptor binding issues
- InstanceID() mapping incorrect

### Issue: Not sure what BLAS strategy to use

**Solution:** At the PAUSE POINT, ask Copilot to help analyze the current BLAS code and suggest a strategy. Document the decision before implementing.

---

## Tips for Success

1. **Read the entire prompt first** before starting execution
2. **Complete all deliverables** - don't skip documentation
3. **Capture baseline screenshots** before any code changes (Phase 2A.0)
4. **Commit after each phase** - makes rollback easy if issues arise
5. **Pause at critical analysis points** - don't rush BLAS strategy decisions
6. **Validate thoroughly** - rendering must be IDENTICAL before proceeding
7. **Document issues** - if you encounter problems, document them and solutions

---

## Success Criteria

Phase 2A is complete when:
- ✅ All 5 prompt phases executed
- ✅ CPU global buffers created during scene loading
- ✅ DXR backend uses global buffers
- ✅ Vulkan backend uses global buffers
- ✅ Both backends render identically to baseline
- ✅ No significant performance regression
- ✅ All documentation created
- ✅ All code committed to git

---

## What's Next?

After Phase 2A completion:
- **Phase 2B:** Convert native shaders (HLSL/GLSL) to Slang language
- **Phase 2C:** Unified Slang shader for both backends

**Note:** Phase 2B prompts will be created separately.

---

## Questions or Issues?

If you encounter issues or have questions:
1. Check the troubleshooting section in the specific prompt
2. Review the Phase2A_GlobalBuffers_Plan.md for context
3. Consult the BLAS strategy documents if rendering is broken
4. Review git history to see what changed

---

## Credits

**Created:** October 10, 2025  
**For:** ChameleonRT Global Buffer Refactor  
**Based on:** Phase2A_GlobalBuffers_Plan.md  

**Note:** These prompts are designed to work with GitHub Copilot. They include detailed context, step-by-step instructions, and validation criteria to guide you through the refactoring process.
