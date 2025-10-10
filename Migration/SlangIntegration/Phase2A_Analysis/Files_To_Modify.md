# Phase 2A: Files That Will Be Modified

**Date Created:** 2025-10-10  
**Purpose:** Comprehensive list of files requiring modification for global buffer refactor  
**Status:** Planning

---

## Overview

This document lists all files that will be modified during Phase 2A (Global Buffer Refactor). The refactor eliminates per-geometry buffers in favor of global buffers, which is a prerequisite for eventual Slang shader unification.

**Total Estimated Files:** 13-15 files (9 core + 4-6 utility/build files)

---

## Phase 2A.1: CPU Scene Refactor

**Goal:** Create global buffers during scene loading (CPU-side only, no GPU changes)

### Core Files (3 required)

- [ ] **`util/mesh.h`**
  - **Changes:** Add new structs
    - `Vertex` - Interleaved vertex structure (position, normal, texCoord)
    - `MeshDesc` - Geometry metadata (vbOffset, ibOffset, vertexCount, indexCount, materialID)
    - `GeometryInstanceData` - Instance metadata (meshID, matrixID, flags)
  - **Lines affected:** ~50 lines added after existing structs
  - **Complexity:** Low - just struct definitions

- [ ] **`util/scene.h`**
  - **Changes:** Add global buffer fields to `Scene` struct
    - `std::vector<Vertex> global_vertices`
    - `std::vector<uint32_t> global_indices`
    - `std::vector<MeshDesc> mesh_descriptors`
    - `std::vector<GeometryInstanceData> geometry_instances`
    - `std::vector<glm::mat4> transform_matrices`
    - `void build_global_buffers()` - Helper function declaration
  - **Lines affected:** ~10 lines added to Scene struct
  - **Complexity:** Low - just member variable additions

- [ ] **`util/scene.cpp`**
  - **Changes:** Implement `build_global_buffers()` function
    - Iterate through all meshes/geometries
    - Concatenate vertices/indices into global arrays
    - Track offsets for each geometry
    - Create MeshDesc entries
    - Build geometry instance data
    - Handle missing normals/UVs gracefully
  - **Lines affected:** ~150-200 lines of new code
  - **Complexity:** Medium - complex offset tracking and edge case handling

### Integration Decision (1 file - TBD)

- [ ] **EITHER `util/scene.cpp` (Scene constructor) OR `main.cpp`**
  - **Decision Point:** Where to call `build_global_buffers()`?
  - **Option A:** Call in `Scene::Scene()` constructor after file loading completes
    - **Pros:** Automatic, encapsulated
    - **Cons:** Scene constructor becomes heavier
  - **Option B:** Call explicitly in `main.cpp` after `Scene` creation
    - **Pros:** Explicit control, easier debugging
    - **Cons:** Must remember to call
  - **Recommendation:** Option A (constructor) for encapsulation
  - **Lines affected:** 1-2 lines

### Validation Files (0-1 files)

- [ ] **`util/scene.cpp` (Optional)**
  - **Changes:** Add debug output in `build_global_buffers()`
    - Print global buffer sizes
    - Print mesh descriptor count
    - Verify offset calculations
  - **Purpose:** Validate CPU-side refactor before GPU work
  - **Lines affected:** ~10 lines (debug output)
  - **Complexity:** Low

---

## Phase 2A.2: DXR Backend Refactor

**Goal:** Update DXR backend to use global buffers, remove space1 local root signature

### Core Files (3 required)

- [ ] **`backends/dxr/render_dxr.h`**
  - **Changes:** Add global buffer member variables
    - `dxr::Buffer global_vertex_buffer`
    - `dxr::Buffer global_index_buffer`
    - `dxr::Buffer mesh_desc_buffer`
  - **Lines affected:** ~3 lines added to class members
  - **Complexity:** Low

- [ ] **`backends/dxr/render_dxr.cpp`**
  - **Changes (Major):**
    - **`set_scene()` function (~lines 115-420):**
      - Remove per-geometry buffer creation loop
      - Add global buffer creation (3 buffers total)
      - Upload global buffers to GPU
      - **CRITICAL:** BLAS building strategy decision
        - Keep temporary per-geometry buffers for BLAS build? OR
        - Use global buffers with offsets?
        - **PAUSE POINT** - Analyze before implementing
    - **`build_raytracing_pipeline()` function (~lines 646-780):**
      - Remove local root signature (`hitgroup_root_sig`)
      - Remove `.add_srv()` calls for space1 resources
      - Remove `.add_constants("MeshData", ...)` call
      - Remove `.set_shader_root_sig(hg_names, hitgroup_root_sig)`
      - Simplify hit group creation (single hit group vs. per-geometry)
    - **`build_shader_resource_heap()` function (~lines 782+):**
      - Add 3 new SRV entries for global buffers
      - Update descriptor heap builder with new bindings
    - **`build_shader_binding_table()` function (~lines 788-870):**
      - Remove loop over parameterized_meshes/geometries
      - Remove per-geometry shader record population
      - Simplify to single hit group record (or minimal records)
    - **`build_descriptor_heap()` function:**
      - Update descriptor count if needed
  - **Lines affected:** ~300-400 lines modified/removed
  - **Complexity:** High - complex refactor with BLAS decision point

- [ ] **`backends/dxr/render_dxr.hlsl`**
  - **Changes:**
    - **Add global buffer declarations (space0):**
      ```hlsl
      struct Vertex {
          float3 position;
          float3 normal;
          float2 texCoord;
      };
      
      struct MeshDesc {
          uint vbOffset;
          uint ibOffset;
          uint vertexCount;
          uint indexCount;
          uint materialID;
      };
      
      StructuredBuffer<Vertex> globalVertices : register(t10, space0);
      StructuredBuffer<uint> globalIndices : register(t11, space0);
      StructuredBuffer<MeshDesc> meshDescs : register(t12, space0);
      ```
    - **Remove space1 declarations (~lines 280-289):**
      - Delete `StructuredBuffer<float3> vertices : register(t0, space1);`
      - Delete `StructuredBuffer<uint3> indices : register(t1, space1);`
      - Delete `StructuredBuffer<float3> normals : register(t2, space1);`
      - Delete `StructuredBuffer<float2> uvs : register(t3, space1);`
      - Delete `cbuffer MeshData : register(b0, space1) { ... }`
    - **Update ClosestHit shader (~lines 292-329):**
      - Replace direct vertex/index access with indirection
      - Add `uint meshID = InstanceID();` or `GeometryIndex()`
      - Fetch `MeshDesc mesh = meshDescs[meshID];`
      - Compute global indices: `globalIndices[mesh.ibOffset + PrimitiveIndex() * 3]`
      - Fetch vertices via global buffer + offsets
      - Use `mesh.materialID` instead of space1 constant
  - **Lines affected:** ~80 lines modified
  - **Complexity:** Medium - shader indirection logic

### Utility Files (1-2 files)

- [ ] **`backends/dxr/dxr_utils.h` (Possibly)**
  - **Changes:** May need to update `Geometry` struct or add helper functions
  - **Uncertainty:** Depends on BLAS building strategy
  - **Lines affected:** TBD

- [ ] **`backends/dxr/dxr_utils.cpp` (Possibly)**
  - **Changes:** May need to update BLAS building code if using global buffers
  - **Lines affected:** TBD

---

## Phase 2A.3: Vulkan Backend Refactor

**Goal:** Update Vulkan backend to use global buffers, simplify shader records

### Core Files (3 required)

- [ ] **`backends/vulkan/render_vulkan.h`**
  - **Changes:** Add global buffer member variables
    - `std::shared_ptr<vkrt::Buffer> global_vertex_buffer`
    - `std::shared_ptr<vkrt::Buffer> global_index_buffer`
    - `std::shared_ptr<vkrt::Buffer> mesh_desc_buffer`
  - **Optional:** Update `HitGroupParams` struct if keeping shader records
    - Simplify to just global buffer addresses (16 bytes) OR
    - Remove entirely if using descriptor set approach
  - **Lines affected:** ~3-10 lines
  - **Complexity:** Low

- [ ] **`backends/vulkan/render_vulkan.cpp`**
  - **Changes (Major):**
    - **`set_scene()` function (~lines 200-460):**
      - Remove per-geometry buffer creation loop
      - Add global buffer creation (3 buffers total)
      - Upload global buffers to GPU
      - **CRITICAL:** BLAS building strategy decision (same as DXR)
        - **PAUSE POINT** - Analyze before implementing
    - **`build_raytracing_pipeline()` function (~lines 755-850):**
      - **Descriptor Set Layout Decision:**
        - **Option A:** Add global buffers to descriptor set 0 (bindings 7-9)
        - **Option B:** Use buffer_reference with shaderRecordEXT
      - If Option A: Add 3 new bindings to `desc_layout`
      - Update pipeline layout if needed
    - **`build_shader_descriptor_table()` function (~lines 933-990):**
      - If Option A: Add descriptor writes for global buffers
      - Update descriptor pool sizes
    - **`build_shader_binding_table()` function (~lines 993-1073):**
      - If Option A: Remove or minimize shader record population
      - If Option B: Update HitGroupParams to use global addresses
      - Remove loop over parameterized_meshes/geometries OR
      - Simplify to single set of buffer addresses for all geometries
  - **Lines affected:** ~300-400 lines modified
  - **Complexity:** High - complex refactor with descriptor vs. shader record decision

- [ ] **`backends/vulkan/hit.rchit`**
  - **Changes:**
    - **Option A (Descriptor Set Approach):**
      - Remove `buffer_reference` declarations (~lines 9-23)
      - Remove `shaderRecordEXT` declaration (~lines 26-34)
      - Add descriptor set bindings:
        ```glsl
        layout(set=0, binding=7, scalar) buffer GlobalVertices {
            Vertex vertices[];
        };
        layout(set=0, binding=8, scalar) buffer GlobalIndices {
            uint indices[];
        };
        layout(set=0, binding=9, scalar) buffer MeshDescriptors {
            MeshDesc descs[];
        };
        ```
    - **Option B (buffer_reference Approach):**
      - Keep `buffer_reference` declarations
      - Update to use global buffer types (Vertex, not vec3)
      - Simplify `shaderRecordEXT` to just global addresses
    - **Update main() function (~lines 36-60):**
      - Add `uint meshID = gl_GeometryIndexEXT;`
      - Fetch `MeshDesc desc = descs[meshID];`
      - Compute global indices with offsets
      - Fetch vertices from global buffer
      - Use `desc.materialID`
  - **Lines affected:** ~60 lines modified
  - **Complexity:** Medium - shader indirection logic

### Additional Shader Files (2-4 files - if needed)

- [ ] **`backends/vulkan/raygen.rgen` (Check if it accesses geometry data)**
  - **Changes:** Unlikely to need changes (only casts rays)
  - **Verify:** Does raygen need to know about mesh descriptors?
  - **Complexity:** Low (probably no changes)

- [ ] **`backends/vulkan/miss.rmiss` (Check if it accesses geometry data)**
  - **Changes:** Unlikely to need changes (handles missed rays)
  - **Complexity:** Low (probably no changes)

- [ ] **`backends/vulkan/occlusion_miss.rmiss` (Check if it accesses geometry data)**
  - **Changes:** Unlikely to need changes (handles shadow rays)
  - **Complexity:** Low (probably no changes)

- [ ] **`backends/vulkan/util.glsl` (Common utilities)**
  - **Changes:** May need to add Vertex, MeshDesc struct definitions if shared
  - **Lines affected:** ~20 lines (struct definitions)
  - **Complexity:** Low

### Utility Files (1-2 files - possibly)

- [ ] **`backends/vulkan/vulkanrt_utils.h` (Possibly)**
  - **Changes:** May need to update `Geometry` struct
  - **Uncertainty:** Depends on BLAS building strategy
  - **Lines affected:** TBD

- [ ] **`backends/vulkan/vulkanrt_utils.cpp` (Possibly)**
  - **Changes:** May need to update BLAS building code
  - **Lines affected:** TBD

---

## Phase 2A.4: Final Validation

**Goal:** Ensure both backends work correctly, no visual regressions

### Test Files (0 files - manual testing)

- No code files modified, but create test documentation:
  - `Migration/SlangIntegration/Phase2A_Analysis/Validation_Results.md`
  - Include screenshots, frame times, memory usage comparisons

---

## Build System (if needed)

### CMake Files (0-1 files)

- [ ] **`CMakeLists.txt` (Root - unlikely to need changes)**
  - **Possible changes:** None expected
  - **Note:** Global buffer refactor is source-only, no new dependencies
  - **Complexity:** N/A (no changes)

- [ ] **`backends/CMakeLists.txt` (Unlikely to need changes)**
  - **Possible changes:** None expected
  - **Complexity:** N/A (no changes)

---

## Summary Statistics

### Files by Phase

| Phase | Core Files | Utility Files | Test/Doc Files | Total |
|-------|-----------|---------------|----------------|-------|
| 2A.1  | 3         | 0-1           | 0              | 3-4   |
| 2A.2  | 3         | 0-2           | 0              | 3-5   |
| 2A.3  | 3         | 1-6           | 0              | 4-9   |
| 2A.4  | 0         | 0             | 1 (doc)        | 0-1   |
| **Total** | **9** | **1-9**      | **1**         | **11-19** |

### Files by Type

| File Type | Count | Complexity |
|-----------|-------|------------|
| C++ Headers (.h) | 3 | Low |
| C++ Source (.cpp) | 3 | High |
| HLSL Shaders (.hlsl) | 1 | Medium |
| GLSL Shaders (.rchit, .rgen, .rmiss, .glsl) | 2-5 | Low-Medium |
| CMake (.txt) | 0 | N/A |
| **Total Code Files** | **9-12** | **Mixed** |

### Lines of Code Estimate

| Phase | Lines Added | Lines Modified | Lines Deleted | Total Changed |
|-------|-------------|----------------|---------------|---------------|
| 2A.1  | 200-250     | 10-20          | 0             | 210-270       |
| 2A.2  | 100-150     | 300-400        | 200-300       | 600-850       |
| 2A.3  | 100-150     | 300-400        | 150-250       | 550-800       |
| **Total** | **400-550** | **610-820** | **350-550** | **1360-1920** |

---

## Critical Decision Points (PAUSE POINTS)

### Decision 1: BLAS Building Strategy (Phase 2A.2 & 2A.3)

**Files Affected:** 
- `backends/dxr/render_dxr.cpp::set_scene()`
- `backends/vulkan/render_vulkan.cpp::set_scene()`

**Decision:** How to provide geometry data to BLAS building?

**Option A: Temporary Per-Geometry Buffers**
- Keep current BLAS build code
- Create temporary per-geometry buffers during `set_scene()`
- Use these for BLAS input (D3D12_RAYTRACING_GEOMETRY_DESC / VkAccelerationStructureGeometryKHR)
- Discard temporary buffers after BLAS build completes
- **Pros:** Simple, minimal changes to BLAS code
- **Cons:** Extra memory allocations, duplicate data temporarily

**Option B: Global Buffers with Offsets**
- Use global buffers for BLAS input
- DXR: Set `desc.Triangles.VertexBuffer.StartAddress = global_vb_address + offset`
- Vulkan: Set `geometry.triangles.vertexData.deviceAddress = global_vb_address + offset`
- **Pros:** No duplicate data, cleaner architecture
- **Cons:** More complex, need to verify offset support in BLAS APIs

**Recommendation:** Option A for Phase 2A (simpler), Option B for future optimization

**PAUSE:** Before implementing Phase 2A.2 or 2A.3, research BLAS API support for vertex/index buffer offsets.

---

### Decision 2: Vulkan Global Buffer Access (Phase 2A.3)

**Files Affected:**
- `backends/vulkan/render_vulkan.cpp::build_raytracing_pipeline()`
- `backends/vulkan/render_vulkan.cpp::build_shader_descriptor_table()`
- `backends/vulkan/hit.rchit`

**Decision:** How to pass global buffers to shaders?

**Option A: Descriptor Set Bindings (Traditional)**
- Add global buffers to descriptor set 0 (bindings 7, 8, 9)
- Update descriptor set layout and writes
- Remove or minimize shader records
- Access in shader via: `layout(set=0, binding=7) buffer GlobalVertices { ... }`
- **Pros:** Traditional Vulkan approach, familiar pattern
- **Cons:** Requires descriptor set updates, less flexible

**Option B: buffer_reference + shaderRecordEXT (Modern)**
- Keep buffer_reference extension
- Simplify shader record to just global buffer addresses (16 bytes)
- All geometries share same addresses
- Access in shader via: `shaderRecordEXT` → `buffer_reference` → global buffers
- **Pros:** More flexible, minimal descriptor set changes
- **Cons:** Requires VK_KHR_buffer_device_address (already used)

**Recommendation:** Option B (buffer_reference) - already using device addresses, cleaner for per-geometry indirection

**PAUSE:** Before implementing Phase 2A.3, decide on descriptor set vs. shader record approach.

---

## Risk Assessment

### High Risk Files (Require Extra Care)

1. **`backends/dxr/render_dxr.cpp::set_scene()`** - Complex BLAS building logic
2. **`backends/vulkan/render_vulkan.cpp::set_scene()`** - Complex BLAS building logic
3. **`backends/dxr/render_dxr.hlsl::ClosestHit()`** - Shader indirection correctness
4. **`backends/vulkan/hit.rchit::main()`** - Shader indirection correctness
5. **`util/scene.cpp::build_global_buffers()`** - Offset calculation correctness

### Medium Risk Files

1. **`backends/dxr/render_dxr.cpp::build_raytracing_pipeline()`** - Root signature changes
2. **`backends/vulkan/render_vulkan.cpp::build_shader_binding_table()`** - SBT restructure

### Low Risk Files

1. **`util/mesh.h`** - Just struct definitions
2. **`util/scene.h`** - Just member additions
3. **`backends/dxr/render_dxr.h`** - Just member additions
4. **`backends/vulkan/render_vulkan.h`** - Just member additions

---

## Testing Strategy

### After Each Phase

**Phase 2A.1 (CPU Refactor):**
- Verify `global_vertices.size()` matches sum of all geometry vertex counts
- Verify `global_indices.size()` matches sum of all geometry index counts × 3
- Verify `mesh_descriptors.size()` equals total geometry count
- Print offsets and verify no overlaps

**Phase 2A.2 (DXR Refactor):**
- Render `test_cube.obj` with DXR
- Compare to baseline screenshot (pixel-perfect match expected)
- Render Sponza (if available)
- Verify no crashes, no visual artifacts

**Phase 2A.3 (Vulkan Refactor):**
- Render `test_cube.obj` with Vulkan
- Compare to baseline screenshot (pixel-perfect match expected)
- Compare to DXR output (should match)
- Verify no validation errors

**Phase 2A.4 (Final Validation):**
- Cross-backend comparison (DXR vs. Vulkan)
- Performance measurement (frame times)
- Memory usage comparison (should decrease)

---

## Rollback Plan

If issues arise, roll back in reverse order:
1. **Phase 2A.4** → Just validation, no code changes, N/A
2. **Phase 2A.3** → Revert Vulkan backend changes (3-9 files)
3. **Phase 2A.2** → Revert DXR backend changes (3-5 files)
4. **Phase 2A.1** → Revert CPU changes (3-4 files)

**Recommendation:** Git commit after each phase completes successfully.

---

## Notes

- **NO Slang in Phase 2A:** All shaders remain native HLSL/GLSL
- **Validation is Critical:** Must compare screenshots after each backend refactor
- **PAUSE POINTS:** Do NOT skip the critical decision analysis tasks
- **Keep Baseline:** Don't delete baseline screenshots until Phase 2A is complete and validated

---

**Document Version:** 1.0  
**Last Updated:** 2025-10-10  
**Next Review:** After Phase 2A.1 completion
