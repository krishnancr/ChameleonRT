# Phase 2A.0: Preparation & Analysis - Copilot Prompt

**Phase:** 2A.0 - Preparation (Analysis)  
**Goal:** Understand current architecture before making any code changes  
**Status:** Analysis only - NO code modifications in this phase

---

## Context

I'm working on the ChameleonRT ray tracing renderer project. This is a multi-backend renderer that supports DirectX Raytracing (DXR) and Vulkan Ray Tracing. We're planning to refactor the scene buffer architecture from per-geometry buffers to a global buffer approach.

**Critical:** This phase is ANALYSIS ONLY. Do not modify any code. The goal is to understand the current architecture thoroughly before making changes.

---

## Reference Documents

Before starting, please review these files to understand the project structure:
- `Migration/SlangIntegration/Phase2A_GlobalBuffers_Plan.md` - Complete refactor plan
- `.github/copilot-instructions.md` - Project architecture and conventions

---

## Task 2A.0.1: Document Current Scene Loading

**Objective:** Understand how scene data flows from file to CPU data structures.

### Actions Required

1. **Read and analyze `util/scene.h`:**
   - Identify all member fields in the `Scene` struct
   - Document the relationship between `meshes`, `parameterized_meshes`, and `instances`
   - Note how materials and textures are stored

2. **Read and analyze `util/mesh.h`:**
   - Document the `Geometry` struct: what data does it hold? (vertices, normals, UVs, indices)
   - Document the `Mesh` struct: how does it relate to Geometry?
   - Document the `ParameterizedMesh` struct: what's the purpose of material_ids?
   - Document the `Instance` struct: how are instances positioned in the scene?

3. **Read and analyze `util/scene.cpp`:**
   - Find the `Scene` constructor - how are OBJ/GLTF files loaded?
   - Identify the `load_obj()` function - trace how vertex data is parsed
   - Identify the `load_gltf()` function - trace how geometry is constructed
   - Document the data flow: File → Geometry → Mesh → ParameterizedMesh → Instance

### Deliverable

Create a markdown file `Migration/SlangIntegration/Phase2A_Analysis/2A.0.1_Scene_Loading_Analysis.md` with:
- Summary of Scene struct fields and their purposes
- Diagram (in text/markdown) showing: File → Geometry → Mesh → ParameterizedMesh → Instance
- Notes on how vertex data, normals, UVs, and indices are currently stored
- Any edge cases found (e.g., geometries without normals/UVs)

---

## Task 2A.0.2: Document Current DXR Buffer Creation

**Objective:** Understand how DXR backend creates and uses per-geometry GPU buffers.

### Actions Required

1. **Read and analyze `backends/dxr/render_dxr.h`:**
   - Identify the `RenderDXR` class structure
   - Find all buffer-related member variables
   - Document current resource binding strategy

2. **Read and analyze `backends/dxr/render_dxr.cpp`:**
   - Find the `set_scene()` function (should be around line 115)
   - **CRITICAL ANALYSIS:** Identify where per-geometry buffers are created in a loop
   - Document what buffers are created per geometry:
     - Vertex buffer
     - Index buffer
     - Normal buffer
     - UV buffer
     - Any metadata/constant buffers
   - Trace BLAS (Bottom-Level Acceleration Structure) building:
     - What geometry data is used for BLAS input?
     - How are D3D12_RAYTRACING_GEOMETRY_DESC structures filled?
   - Trace TLAS (Top-Level Acceleration Structure) building:
     - How are instances created?
     - What data goes into each instance?

3. **Read and analyze `backends/dxr/render_dxr.hlsl`:**
   - Identify resources bound in `space0` (global resources)
   - Identify resources bound in `space1` (local/per-geometry resources)
   - Find the ClosestHit shader - how does it currently access vertex data?
   - Document the current local root signature setup

### Deliverable

Create a markdown file `Migration/SlangIntegration/Phase2A_Analysis/2A.0.2_DXR_Buffer_Analysis.md` with:
- List of all per-geometry buffers currently created (with sizes/types)
- Pseudocode showing current buffer creation loop
- Documentation of space0 vs space1 resource binding
- BLAS/TLAS building strategy - what geometry data is referenced
- Notes on current shader record structure (if any)
- Diagram showing: Scene data → GPU buffers → BLAS → TLAS → Shader access

---

## Task 2A.0.3: Document Current Vulkan Buffer Creation

**Objective:** Understand how Vulkan backend creates and uses per-geometry GPU buffers.

### Actions Required

1. **Read and analyze `backends/vulkan/render_vulkan.h`:**
   - Identify the `RenderVulkan` class structure
   - Find all buffer-related member variables
   - Document current descriptor set layout

2. **Read and analyze `backends/vulkan/render_vulkan.cpp`:**
   - Find the `set_scene()` function
   - **CRITICAL ANALYSIS:** Identify where per-geometry buffers are created
   - Document what buffers are created per geometry:
     - Vertex buffer
     - Index buffer
     - Normal buffer
     - UV buffer
   - Trace buffer device address usage:
     - Are `vkGetBufferDeviceAddressKHR` calls used?
     - Where are these addresses stored (shader records)?
   - Trace BLAS building:
     - What geometry data is used for VkAccelerationStructureGeometryKHR?
   - Trace TLAS building:
     - How are VkAccelerationStructureInstanceKHR created?
   - Document descriptor set layout:
     - What bindings are used in set 0?
     - Are there multiple descriptor sets?

3. **Read and analyze `backends/vulkan/hit.rchit` (closest hit shader):**
   - Identify how vertex data is currently accessed
   - Are `buffer_reference` pointers used?
   - Are `shaderRecordEXT` declarations used?
   - Document current data access pattern

### Deliverable

Create a markdown file `Migration/SlangIntegration/Phase2A_Analysis/2A.0.3_Vulkan_Buffer_Analysis.md` with:
- List of all per-geometry buffers currently created
- Documentation of buffer device address usage
- Shader record structure (if applicable)
- Descriptor set layout (bindings in set 0)
- BLAS/TLAS building strategy
- Diagram showing: Scene data → GPU buffers → BLAS → TLAS → Shader access

---

## Task 2A.0.4: Create Baseline Screenshots

**Objective:** Capture reference screenshots for comparison after refactoring.

### Actions Required

**NOTE:** This is a manual task for the user to perform.

1. **Build current codebase:**
   ```powershell
   cd C:\dev\ChameleonRT
   cmake --build build --config Debug
   ```

2. **Capture DXR screenshots:**
   - Run: `.\build\Debug\chameleonrt.exe dxr .\test_cube.obj`
   - Take screenshot, save as `Migration/SlangIntegration/Phase2A_Analysis/baseline_dxr_cube.png`
   - Run: `.\build\Debug\chameleonrt.exe dxr C:\Demo\Assets\sponza\sponza.obj` (if available)
   - Take screenshot, save as `Migration/SlangIntegration/Phase2A_Analysis/baseline_dxr_sponza.png`

3. **Capture Vulkan screenshots (if available):**
   - Run: `.\build\Debug\chameleonrt.exe vulkan .\test_cube.obj`
   - Take screenshot, save as `Migration/SlangIntegration/Phase2A_Analysis/baseline_vulkan_cube.png`
   - Run: `.\build\Debug\chameleonrt.exe vulkan C:\Demo\Assets\sponza\sponza.obj` (if available)
   - Take screenshot, save as `Migration/SlangIntegration/Phase2A_Analysis/baseline_vulkan_sponza.png`

### Deliverable

- Baseline screenshots saved in `Migration/SlangIntegration/Phase2A_Analysis/`
- Note in analysis summary that baselines have been captured

---

## Task 2A.0.5: Create Files to Modify List

**Objective:** Document exactly which files will need modification in subsequent phases.

### Actions Required

Based on the analysis from tasks 2A.0.1, 2A.0.2, and 2A.0.3, create a comprehensive list.

### Deliverable

Create a markdown file `Migration/SlangIntegration/Phase2A_Analysis/Files_To_Modify.md` with:

```markdown
# Phase 2A: Files That Will Be Modified

## Phase 2A.1: CPU Scene Refactor
- [ ] `util/mesh.h` - Add new structs: Vertex, MeshDesc, GeometryInstanceData
- [ ] `util/scene.h` - Add global buffer fields to Scene struct
- [ ] `util/scene.cpp` - Implement build_global_buffers() function
- [ ] (Determine: Scene constructor or main.cpp for calling build_global_buffers())

## Phase 2A.2: DXR Backend Refactor
- [ ] `backends/dxr/render_dxr.h` - Add global buffer member variables
- [ ] `backends/dxr/render_dxr.cpp` - Modify set_scene(), descriptor heap, root signature
- [ ] `backends/dxr/render_dxr.hlsl` - Add global buffers, remove space1, update ClosestHit

## Phase 2A.3: Vulkan Backend Refactor
- [ ] `backends/vulkan/render_vulkan.h` - Add global buffer member variables
- [ ] `backends/vulkan/render_vulkan.cpp` - Modify set_scene(), descriptor sets, SBT
- [ ] `backends/vulkan/hit.rchit` - Add global buffers, remove buffer_reference, update main()
- [ ] (List any other Vulkan shader files that need updates)

## Build System (if needed)
- [ ] CMakeLists.txt - (Note if any changes are needed)
```

---

## Final Deliverable for Phase 2A.0

Create a comprehensive summary document:

**File:** `Migration/SlangIntegration/Phase2A_Analysis/Phase_2A.0_Summary.md`

### Contents:

```markdown
# Phase 2A.0: Preparation & Analysis - Summary

**Date Completed:** [Date]  
**Status:** ✅ Complete

## Overview

This phase analyzed the current ChameleonRT architecture to understand how scene data flows from files to GPU buffers in both DXR and Vulkan backends.

## Key Findings

### Scene Loading (Task 2A.0.1)
- [Summary of scene loading architecture]
- [Link to detailed analysis: 2A.0.1_Scene_Loading_Analysis.md]

### DXR Backend (Task 2A.0.2)
- [Summary of DXR buffer creation]
- [Key points about BLAS/TLAS building]
- [Link to detailed analysis: 2A.0.2_DXR_Buffer_Analysis.md]

### Vulkan Backend (Task 2A.0.3)
- [Summary of Vulkan buffer creation]
- [Key points about shader records and descriptor sets]
- [Link to detailed analysis: 2A.0.3_Vulkan_Buffer_Analysis.md]

## Baseline Screenshots

- ✅ DXR test_cube.obj: baseline_dxr_cube.png
- ✅ DXR Sponza: baseline_dxr_sponza.png
- ✅ Vulkan test_cube.obj: baseline_vulkan_cube.png (if available)
- ✅ Vulkan Sponza: baseline_vulkan_sponza.png (if available)

## Files to Modify

See: Files_To_Modify.md

Total files to modify: [count]
- CPU/Scene: [count]
- DXR: [count]
- Vulkan: [count]

## Important Notes for Next Phases

- [Any critical observations that will impact implementation]
- [Edge cases discovered]
- [Potential risks identified]

## Ready for Phase 2A.1

- ✅ Current architecture understood
- ✅ Baseline screenshots captured
- ✅ File modification list created
- ✅ No code changes made in this phase

**Next Step:** Proceed to Phase 2A.1 - CPU Scene Refactor
```

---

## Validation Checklist

Before proceeding to Phase 2A.1, ensure:

- ✅ All analysis documents created in `Migration/SlangIntegration/Phase2A_Analysis/`
- ✅ Baseline screenshots captured
- ✅ Files_To_Modify.md created with comprehensive list
- ✅ Phase_2A.0_Summary.md created
- ✅ **NO CODE MODIFICATIONS MADE** (this was analysis only)
- ✅ User has reviewed all analysis documents

---

## How to Use This Prompt

1. Copy this entire prompt
2. Paste into Copilot chat
3. Copilot will guide you through each task
4. Review each analysis document as it's created
5. Manually capture baseline screenshots (Task 2A.0.4)
6. Review the final summary
7. Confirm readiness to proceed to Phase 2A.1
