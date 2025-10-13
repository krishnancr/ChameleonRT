# Phase 2A.2.0: BLAS Building Strategy for DXR

**Date:** October 10, 2025  
**Status:** ✅ APPROVED - Option A Selected  
**Implementation Date:** October 11, 2025  
**Decision:** Option A - Temporary per-geometry buffers for BLAS build

---

## Current BLAS Building Code Analysis

### Location
- **File:** `backends/dxr/render_dxr.cpp::set_scene()`
- **Lines:** ~115-240 (buffer creation and BLAS building)
- **Helper:** `backends/dxr/dxr_utils.cpp` - Geometry constructor and BottomLevelBVH::enqeue_build()

---

### Current Implementation Flow

#### 1. Per-Geometry Buffer Creation (render_dxr.cpp lines 115-210)

```cpp
for (const auto &mesh : scene.meshes) {
    std::vector<dxr::Geometry> geometries;
    for (const auto &geom : mesh.geometries) {
        // Create upload buffers (CPU-writable, staging)
        dxr::Buffer upload_verts = dxr::Buffer::upload(...);
        dxr::Buffer upload_indices = dxr::Buffer::upload(...);
        dxr::Buffer upload_uvs = dxr::Buffer::upload(...);  // if present
        dxr::Buffer upload_normals = dxr::Buffer::upload(...);  // if present
        
        // Copy CPU data to upload buffers
        std::memcpy(upload_verts.map(), geom.vertices.data(), ...);
        std::memcpy(upload_indices.map(), geom.indices.data(), ...);
        // ... uvs, normals
        
        // Create device buffers (GPU VRAM)
        dxr::Buffer vertex_buf = dxr::Buffer::device(...);
        dxr::Buffer index_buf = dxr::Buffer::device(...);
        dxr::Buffer uv_buf = dxr::Buffer::device(...);
        dxr::Buffer normal_buf = dxr::Buffer::device(...);
        
        // Copy upload → device via command list
        cmd_list->CopyResource(vertex_buf.get(), upload_verts.get());
        cmd_list->CopyResource(index_buf.get(), upload_indices.get());
        // ... uvs, normals
        
        // Transition buffers to shader read state
        cmd_list->ResourceBarrier(...);
        
        // Create Geometry object (stores buffers + creates desc)
        geometries.emplace_back(vertex_buf, index_buf, normal_buf, uv_buf);
        
        // Execute command list and sync
        cmd_list->Close();
        cmd_queue->ExecuteCommandLists(...);
        sync_gpu();
    }
    
    // Store all geometries for this mesh
    meshes.emplace_back(geometries);
    
    // Build BLAS for this mesh
    meshes.back().enqeue_build(device.Get(), cmd_list.Get());
    cmd_list->Close();
    cmd_queue->ExecuteCommandLists(...);
    sync_gpu();
    
    // Compact BLAS (optional optimization)
    meshes.back().enqueue_compaction(device.Get(), cmd_list.Get());
    cmd_list->Close();
    cmd_queue->ExecuteCommandLists(...);
    sync_gpu();
    
    meshes.back().finalize();
}
```

---

#### 2. Geometry Descriptor Setup (dxr_utils.cpp lines 881-899)

```cpp
Geometry::Geometry(Buffer verts,
                   Buffer indices,
                   Buffer normals,
                   Buffer uvs,
                   D3D12_RAYTRACING_GEOMETRY_FLAGS geom_flags)
    : vertex_buf(verts), index_buf(indices), normal_buf(normals), uv_buf(uvs)
{
    desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    
    // Vertex buffer configuration
    desc.Triangles.VertexBuffer.StartAddress = vertex_buf->GetGPUVirtualAddress();
    desc.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;  // vec3
    desc.Triangles.VertexCount = vertex_buf.size() / desc.Triangles.VertexBuffer.StrideInBytes;
    desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    
    // Index buffer configuration
    desc.Triangles.IndexBuffer = index_buf->GetGPUVirtualAddress();
    desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    desc.Triangles.IndexCount = index_buf.size() / sizeof(uint32_t);
    
    desc.Triangles.Transform3x4 = 0;  // No transform (identity)
    desc.Flags = geom_flags;
}
```

**Key Details:**
- Uses **GPU virtual addresses** directly (`GetGPUVirtualAddress()`)
- Assumes **contiguous buffer** (StartAddress + stride × count)
- No explicit offset parameter in `D3D12_RAYTRACING_GEOMETRY_DESC`
- Transform3x4 is for pre-transform (usually not used)

---

#### 3. BLAS Building (dxr_utils.cpp lines 901-970)

```cpp
BottomLevelBVH::BottomLevelBVH(std::vector<Geometry> &geoms,
                               D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS build_flags)
    : build_flags(build_flags), geometries(geoms)
{
    // Extract geometry descriptors from Geometry objects
    std::transform(geometries.begin(),
                   geometries.end(),
                   std::back_inserter(geom_descs),
                   [](const Geometry &g) { return g.desc; });
}

void BottomLevelBVH::enqeue_build(ID3D12Device5 *device, ID3D12GraphicsCommandList4 *cmd_list)
{
    // ... post_build_info buffer creation
    
    // Setup BLAS build inputs
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bvh_inputs = {0};
    bvh_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bvh_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bvh_inputs.NumDescs = geom_descs.size();
    bvh_inputs.pGeometryDescs = geom_descs.data();  // Array of geometry descriptors
    bvh_inputs.Flags = build_flags;
    
    // Query required memory sizes
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info = {0};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&bvh_inputs, &prebuild_info);
    
    // Allocate BVH and scratch buffers
    bvh = Buffer::device(device,
                         prebuild_info.ResultDataMaxSizeInBytes,
                         D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    scratch = Buffer::device(device,
                             prebuild_info.ScratchDataSizeInBytes,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    
    // Build BLAS
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc = {0};
    build_desc.Inputs = bvh_inputs;  // References geom_descs
    build_desc.DestAccelerationStructureData = bvh->GetGPUVirtualAddress();
    build_desc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    cmd_list->BuildRaytracingAccelerationStructure(&build_desc, 1, &post_build_info_desc);
    
    // UAV barrier to wait for build completion
    cmd_list->ResourceBarrier(...);
}
```

**Key Details:**
- BLAS build uses `geom_descs.data()` which contains **GPU virtual addresses** from Geometry::desc
- Build reads vertex/index data **directly from GPU buffers** during construction
- No CPU data access during build
- BLAS build is asynchronous (GPU-side operation)

---

## D3D12 API Constraints

### Can We Use Global Buffers with Offsets?

**Question:** Can `D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC` reference a **sub-range** of a larger buffer?

**Answer:** **YES, but with limitations.**

From D3D12 documentation:

```cpp
typedef struct D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC {
    D3D12_GPU_VIRTUAL_ADDRESS Transform3x4;
    DXGI_FORMAT IndexFormat;
    DXGI_FORMAT VertexFormat;
    UINT IndexCount;
    UINT VertexCount;
    D3D12_GPU_VIRTUAL_ADDRESS IndexBuffer;
    D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE VertexBuffer;
} D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC;

typedef struct D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE {
    D3D12_GPU_VIRTUAL_ADDRESS StartAddress;
    UINT64 StrideInBytes;
} D3D12_GPU_VIRTUAL_ADDRESS_AND_STRIDE;
```

**Key Observations:**
1. `VertexBuffer.StartAddress` can be **any GPU virtual address** (not required to be buffer start)
2. `IndexBuffer` can be **any GPU virtual address** (not required to be buffer start)
3. GPU virtual addresses are **linear** - you can offset them with `baseAddress + offset`

**Implication:** We CAN use global buffers by computing offset addresses!

**Example:**
```cpp
D3D12_GPU_VIRTUAL_ADDRESS global_vb_base = global_vertex_buffer->GetGPUVirtualAddress();
D3D12_GPU_VIRTUAL_ADDRESS geometry_vb_start = global_vb_base + (vbOffset * sizeof(Vertex));

desc.Triangles.VertexBuffer.StartAddress = geometry_vb_start;
desc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);  // 32 bytes (vec3+vec3+vec2)
desc.Triangles.VertexCount = vertexCount;
```

---

## Options Analysis

### Option A: Temporary Per-Geometry Buffers for BLAS Build

**Approach:**
1. Create global buffers for shader access (as planned)
2. During BLAS building, create **temporary** per-geometry buffers from scene data
3. Use temporary buffers for BLAS build (same as current code)
4. Discard temporary buffers after BLAS is built
5. Only global buffers remain for shader access

**Pros:**
- ✅ **Minimal code changes** - BLAS building code stays almost identical
- ✅ **Low risk** - Proven approach, no API uncertainties
- ✅ **Easy to debug** - Separate BLAS and shader refactoring
- ✅ **Incremental** - Can validate BLAS building independently of shader changes

**Cons:**
- ❌ **Temporary memory overhead** - Extra GPU allocations during scene load
- ❌ **Longer load time** - Duplicate uploads (temp buffers + global buffers)
- ❌ **Not fully optimized** - Still creating per-geometry buffers (temporarily)

**Implementation Complexity:** Low

---

### Option B: Global Buffers with Offset for BLAS Build

**Approach:**
1. Create global buffers
2. For BLAS building, compute GPU virtual addresses with offsets:
   ```cpp
   desc.Triangles.VertexBuffer.StartAddress = 
       global_vertex_buffer->GetGPUVirtualAddress() + (mesh_desc.vbOffset * sizeof(Vertex));
   desc.Triangles.IndexBuffer = 
       global_index_buffer->GetGPUVirtualAddress() + (mesh_desc.ibOffset * sizeof(uint32_t));
   ```
3. Use offset addresses in geometry descriptors
4. No temporary buffers needed

**Pros:**
- ✅ **Memory efficient** - No temporary allocations
- ✅ **Faster scene loading** - Single upload per buffer type
- ✅ **Cleaner architecture** - Single source of truth (global buffers)
- ✅ **Future-proof** - Aligns with global buffer strategy

**Cons:**
- ❌ **More complex** - Requires understanding GPU virtual address arithmetic
- ❌ **Stride mismatch** - Current code uses vec3 for vertices, global uses Vertex struct (larger stride)
- ❌ **Index format** - Current code uses uint3 indices, global uses flat uint array (needs offset/3 calculations)
- ❌ **Higher risk** - Untested approach, potential alignment issues

**Implementation Complexity:** Medium-High

**Critical Issue:** **Stride mismatch between BLAS and shader access!**
- BLAS expects: `VertexBuffer.StrideInBytes = 12` (sizeof(vec3))
- Shader uses: `sizeof(Vertex) = 32` (vec3 position + vec3 normal + vec2 uv)
- If we use global buffer for BLAS, stride would be 32, but BLAS only reads first 12 bytes (position)
- This MIGHT work (BLAS ignores extra data), but is **untested and risky**

---

### Option C: Extract Per-Geometry Data from Global Buffers (CPU-side)

**Approach:**
1. Create global buffers
2. For BLAS building, extract per-geometry vertex/index data from global buffers back to CPU
3. Create temporary upload buffers from extracted data
4. Build BLAS using temporary buffers
5. Discard temporary buffers

**Pros:**
- ✅ **Safe** - Uses proven BLAS building approach
- ✅ **Flexible** - Can transform data if needed (e.g., extract only positions)

**Cons:**
- ❌ **Inefficient** - Copy global → CPU → temp buffers → GPU
- ❌ **Complex** - More data manipulation
- ❌ **Slow** - Multiple copies, potential stalls
- ❌ **Redundant** - Already have data in scene.meshes

**Implementation Complexity:** Medium

**Verdict:** Worse than Option A (we still have original scene data, no need to extract)

---

## Proposed Strategy

### **Chosen Option: A - Temporary Per-Geometry Buffers for BLAS Build**

**Rationale:**

1. **Risk Mitigation:** Phase 2A focuses on validating global buffer architecture. Keeping BLAS building unchanged isolates risk to shader changes only.

2. **Incremental Approach:** Aligns with project philosophy (direct API understanding, not premature optimization).

3. **Stride Compatibility:** Avoids potential issues with vertex stride mismatch (BLAS expects vec3, global buffer uses Vertex struct).

4. **Index Format:** Current BLAS code expects `uint3` indices, global buffer uses flat `uint` array. Temporary buffers avoid reformatting complexity.

5. **Validation:** We can compare BLAS builds before/after refactor (should be byte-identical).

6. **Future Optimization:** Option B can be implemented in a future phase after Phase 2A is validated.

**Trade-off Acceptance:**
- Extra memory during scene load: **Acceptable** (temporary, only during `set_scene()`)
- Duplicate uploads: **Acceptable** (one-time cost, scene loading is not performance-critical)
- Not fully optimal: **Acceptable** (correctness first, optimization later)

---

## Implementation Approach

### High-Level Changes to `render_dxr.cpp::set_scene()`

```cpp
void RenderDXR::set_scene(const Scene &scene)
{
    frame_id = 0;
    samples_per_pixel = scene.samples_per_pixel;
    
    // PHASE 2A.2 STEP 1: Create global buffers (NEW)
    // - Create global_vertex_buffer, global_index_buffer, mesh_desc_buffer
    // - Upload scene.global_vertices, scene.global_indices, scene.mesh_descriptors
    // - Transition to NON_PIXEL_SHADER_RESOURCE state
    // [Code added here - Task 2A.2.3]
    
    // PHASE 2A.2 STEP 2: Build BLAS (UNCHANGED - uses temporary buffers)
    for (const auto &mesh : scene.meshes) {
        std::vector<dxr::Geometry> geometries;
        for (const auto &geom : mesh.geometries) {
            // Create temporary buffers (same as current code)
            dxr::Buffer upload_verts = dxr::Buffer::upload(...);
            dxr::Buffer upload_indices = dxr::Buffer::upload(...);
            // ... normals, UVs
            
            // Copy data to upload buffers
            std::memcpy(upload_verts.map(), geom.vertices.data(), ...);
            // ...
            
            // Create temporary device buffers
            dxr::Buffer vertex_buf = dxr::Buffer::device(...);
            dxr::Buffer index_buf = dxr::Buffer::device(...);
            // ... normals, UVs
            
            // Copy upload → device
            cmd_list->CopyResource(vertex_buf.get(), upload_verts.get());
            // ...
            
            // Create Geometry (stores buffers, creates desc)
            geometries.emplace_back(vertex_buf, index_buf, normal_buf, uv_buf);
            
            // Execute and sync
            cmd_list->Close();
            cmd_queue->ExecuteCommandLists(...);
            sync_gpu();
        }
        
        // Store geometries and build BLAS
        meshes.emplace_back(geometries);
        meshes.back().enqeue_build(device.Get(), cmd_list.Get());
        // ... compaction, finalize
    }
    
    // PHASE 2A.2 STEP 3: At this point, temporary buffers are discarded
    // - dxr::Buffer destructors release GPU memory
    // - Only BLAS (meshes[].bvh) and global buffers remain
    
    // Rest of set_scene (TLAS, materials, textures) - UNCHANGED
    // ...
}
```

---

### Buffer Lifecycle

**Before Refactor:**
- Per-geometry buffers created → used for BLAS → kept for shader access (space1)
- Total buffers: 4 × num_geometries (vertex, index, normal, UV)

**After Refactor (Option A):**
- **Temporary buffers:** Created → used for BLAS → **discarded** (scope ends)
- **Global buffers:** Created → used for shader access (space0) → kept
- Total persistent buffers: 3 global + 1 BLAS per mesh + 1 TLAS

**Memory Comparison:**
- Before: 400 buffers (100 geoms × 4) = ~5MB data + D3D12 overhead
- After: 3 global buffers (~5MB data) + minimal overhead
- **During load:** Temporary spike (both temp + global), then temp buffers released

---

## Code Changes Required

### 1. Add Global Buffer Creation (Beginning of `set_scene()`)

**Location:** `backends/dxr/render_dxr.cpp::set_scene()` - before BLAS loop

```cpp
// Phase 2A.2: Create global buffers
std::cout << "[Phase 2A.2 DXR] Creating global buffers...\n";

// Global Vertex Buffer
dxr::Buffer upload_vertices = dxr::Buffer::upload(...);
std::memcpy(upload_vertices.map(), scene.global_vertices.data(), ...);
upload_vertices.unmap();

global_vertex_buffer = dxr::Buffer::device(...);

// Global Index Buffer
dxr::Buffer upload_indices = dxr::Buffer::upload(...);
std::memcpy(upload_indices.map(), scene.global_indices.data(), ...);
upload_indices.unmap();

global_index_buffer = dxr::Buffer::device(...);

// MeshDesc Buffer
dxr::Buffer upload_mesh_descs = dxr::Buffer::upload(...);
std::memcpy(upload_mesh_descs.map(), scene.mesh_descriptors.data(), ...);
upload_mesh_descs.unmap();

mesh_desc_buffer = dxr::Buffer::device(...);

// Copy and transition
cmd_list->Reset(...);
cmd_list->CopyResource(global_vertex_buffer.get(), upload_vertices.get());
cmd_list->CopyResource(global_index_buffer.get(), upload_indices.get());
cmd_list->CopyResource(mesh_desc_buffer.get(), upload_mesh_descs.get());
cmd_list->ResourceBarrier(...);  // Transition to shader read state
cmd_list->Close();
cmd_queue->ExecuteCommandLists(...);
sync_gpu();

std::cout << "[Phase 2A.2 DXR] Global buffers uploaded\n";
```

### 2. Keep BLAS Building Loop UNCHANGED

**Location:** `backends/dxr/render_dxr.cpp::set_scene()` - existing BLAS loop

**Action:** NO CHANGES (comment to document strategy)

```cpp
// Phase 2A.2 Note: BLAS building uses temporary per-geometry buffers
// These buffers are created from scene.meshes[] data, used for BLAS build,
// then discarded when they go out of scope. This is acceptable for Phase 2A.
// Future optimization: Use global buffers with offset addresses (Option B).

for (const auto &mesh : scene.meshes) {
    // ... existing code unchanged
}
```

### 3. Remove Per-Geometry Shader Access

**Location:** `backends/dxr/render_dxr.cpp` - shader record building, descriptor heap

**Action:** Remove/comment out code that binds per-geometry buffers to space1

- Remove local root signature for hit groups (space1)
- Remove shader record writes for per-geometry buffer addresses
- Update descriptor heap to include global buffers (space0)

**Note:** This is covered in Tasks 2A.2.4 and 2A.2.5

---

## Validation Plan

### 1. BLAS Integrity Check

**Method:** Compare BLAS GPU virtual addresses before/after refactor

**Expected Result:** BLAS builds should be **functionally identical** (addresses will differ, but structure/size should match)

**Validation:**
```cpp
// After BLAS build
std::cout << "[BLAS Validation] Mesh " << i 
          << " BLAS size: " << meshes[i].bvh.size() 
          << " bytes, address: " << std::hex 
          << meshes[i]->GetGPUVirtualAddress() << std::dec << "\n";
```

Compare output before/after refactor - sizes should match exactly.

---

### 2. TLAS Integrity Check

**Method:** Verify TLAS instance buffer and build succeeds

**Expected Result:** TLAS builds successfully, no errors

**Validation:**
- Check `scene_bvh.bvh.size()` matches expected
- No D3D12 validation errors during TLAS build
- Instance count matches `scene.instances.size()`

---

### 3. Memory Usage Check

**Method:** Monitor GPU memory allocation during scene load

**Expected Result:** 
- Temporary spike during load (temp + global buffers)
- After load, memory drops to baseline (global buffers only)
- Final memory usage **lower** than before refactor

**Validation:** Use GPU profiler (PIX, Nsight) or D3D12 debug layer memory tracking

---

### 4. Rendering Validation

**Method:** Compare screenshots before/after refactor

**Expected Result:** **Pixel-perfect identical** rendering (BLAS hasn't changed)

**Validation:**
- Capture baseline screenshot before refactor
- Capture screenshot after Task 2A.2.5 (shader changes)
- Use image diff tool (ImageMagick, Python PIL) to verify identical

**Note:** BLAS changes alone won't affect rendering - validation occurs after shader updates.

---

## Risk Assessment

### Low Risk ✅
- BLAS building unchanged (proven code path)
- Temporary buffer approach is standard D3D12 pattern
- No API uncertainties

### Medium Risk ⚠️
- Global buffer creation - new code, must handle empty buffers correctly
- Descriptor heap updates - register conflicts possible
- Shader changes - must match C++ buffer layouts exactly

### Mitigation
- Add debug output for buffer sizes
- Test with simple scene (test_cube.obj) first
- Keep baseline screenshots for comparison
- Use D3D12 debug layer for validation errors

---

## Next Steps

After completing this analysis:

1. ✅ **Document strategy** (this file)
2. → **Proceed to Task 2A.2.1** (Update HLSL shader)
3. → **Continue through Tasks 2A.2.2-2A.2.5** (C++ changes)
4. → **Validate rendering** (Task 2A.2.5)

**This analysis satisfies the prerequisite for Phase 2A.2 implementation.**

---

## Future Optimization (Post-Phase 2A)

**Phase 2A.X (Future):** Optimize BLAS building with global buffers

**Approach:**
- Implement Option B (global buffers with offset addresses)
- Handle vertex stride mismatch (BLAS reads 12 bytes from 32-byte stride)
- Test thoroughly with various scenes
- Measure performance improvement (faster load times, less memory)

**Priority:** Low (correctness first, optimization later)

**Estimated Effort:** 1-2 days (after Phase 2A validation)

---

## References

- **D3D12 Ray Tracing Spec:** https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
- **Geometry Descriptor:** `D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC` documentation
- **Current Implementation:** `backends/dxr/dxr_utils.cpp` lines 881-970
- **Phase 2A Plan:** `Migration/SlangIntegration/Phase2A_GlobalBuffers_Plan.md`

---

**Analysis Complete - Ready for Implementation** ✅
