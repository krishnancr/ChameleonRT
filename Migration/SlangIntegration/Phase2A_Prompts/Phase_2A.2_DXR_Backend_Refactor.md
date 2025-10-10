# Phase 2A.2: DXR Backend Refactor - Copilot Prompt

**Phase:** 2A.2 - DXR Backend Refactor  
**Goal:** Update DXR backend to use global buffers, validate identical rendering  
**Status:** Implementation

---

## Context

I'm working on ChameleonRT ray tracing renderer. Phase 2A.1 (CPU scene refactor) is complete - global buffers are now created during scene loading. Now we need to update the DXR backend to use these global buffers instead of per-geometry buffers.

**Critical Success Criteria:**
- Rendering must be IDENTICAL to Phase 2A.0 baseline screenshots
- Remove all space1 (local root signature) usage
- All resources bound in space0 (global root signature)
- Use native HLSL shaders (NO Slang in this phase)

---

## Prerequisites

Before starting this phase:
- ✅ Phase 2A.0 analysis complete
- ✅ Phase 2A.1 CPU refactor complete
- ✅ Global buffers (global_vertices, global_indices, mesh_descriptors) available in Scene
- ✅ Baseline screenshots for comparison

---

## Reference Documents

- `Migration/SlangIntegration/Phase2A_GlobalBuffers_Plan.md` - Section "Phase 2A.2"
- `Migration/SlangIntegration/Phase2A_Analysis/2A.0.2_DXR_Buffer_Analysis.md` - Current DXR architecture
- `.github/copilot-instructions.md` - Project conventions

---

## ⚠️ CRITICAL ANALYSIS POINT: BLAS Building Strategy

**BEFORE implementing code changes, we must decide how to build BLAS (Bottom-Level Acceleration Structures).**

### Analysis Required

Read `backends/dxr/render_dxr.cpp` and examine the BLAS building code (likely in `set_scene()` function):

1. How is `D3D12_RAYTRACING_GEOMETRY_DESC` currently filled?
   - Does it reference vertex/index buffers directly?
   - What buffer addresses are used?

2. What are our options for BLAS building with global buffers?
   - **Option A:** Create temporary per-geometry buffers ONLY for BLAS build, then discard them
   - **Option B:** Use global buffers with offset/stride parameters in geometry descriptor
   - **Option C:** Extract per-geometry data from global buffers into temporary memory

3. Which option is most compatible with D3D12 API?

### Decision Point

**PAUSE HERE and analyze the BLAS building code.** Document your findings:

Create a file: `Migration/SlangIntegration/Phase2A_Analysis/2A.2.0_BLAS_Strategy.md`

```markdown
# Phase 2A.2.0: BLAS Building Strategy for DXR

## Current BLAS Building Code Analysis

**Location:** `backends/dxr/render_dxr.cpp::set_scene()`

**Current Implementation:**
[Describe how BLAS is currently built]

**Geometry Descriptor Setup:**
```cpp
[Paste relevant D3D12_RAYTRACING_GEOMETRY_DESC setup code]
```

## Proposed Strategy

**Chosen Option:** [A / B / C]

**Rationale:**
[Explain why this option was chosen]

**Implementation Approach:**
[Describe how BLAS building will work with global buffers]

## Code Changes Required

[List specific changes needed for BLAS building]

## Validation Plan

[How will we verify BLAS is built correctly?]
```

**After completing this analysis and documenting the strategy, proceed with implementation below.**

---

## Task 2A.2.1: Update HLSL Shader

**Objective:** Modify DXR shader to use global buffers instead of space1 per-geometry buffers.

### File: `backends/dxr/render_dxr.hlsl`

#### Step 1: Add New Structs

At the top of the file (after any existing includes), add:

```hlsl
// Phase 2A.2: Global buffer structures
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
```

#### Step 2: Add Global Buffer Declarations (space0)

Find existing space0 resource declarations. After them (or at an appropriate location), add:

```hlsl
// Phase 2A.2: Global buffers for geometry data
StructuredBuffer<Vertex> globalVertices : register(t10, space0);
StructuredBuffer<uint> globalIndices : register(t11, space0);    // Note: uint not uint3
StructuredBuffer<MeshDesc> meshDescs : register(t12, space0);
```

**Important:** Use registers t10, t11, t12 to avoid conflicts with existing resources. If these are already used, choose different registers and document the choice.

#### Step 3: Remove space1 Declarations

Find and DELETE (or comment out) all space1 resource declarations. These typically look like:

```hlsl
// DELETE OR COMMENT OUT:
// StructuredBuffer<float3> vertices : register(t0, space1);
// StructuredBuffer<uint3> indices : register(t1, space1);
// StructuredBuffer<float3> normals : register(t2, space1);
// StructuredBuffer<float2> uvs : register(t3, space1);
// cbuffer MeshData : register(b0, space1) { ... }
```

**Action:** Comment these out with `// Phase 2A.2: Removed - using global buffers` for easy rollback if needed.

#### Step 4: Update ClosestHit Shader

Find the ClosestHit shader (likely named `ClosestHit` or similar). Replace the geometry data access logic:

**Old pattern (example - adapt based on actual code):**
```hlsl
// OLD CODE - per-geometry buffers in space1
uint3 tri = indices[PrimitiveIndex()];
float3 v0 = vertices[tri.x];
float3 v1 = vertices[tri.y];
float3 v2 = vertices[tri.z];
// ... normals, uvs accessed separately
```

**New pattern:**
```hlsl
[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    // Phase 2A.2: Use global buffers with indirection
    
    // Get which geometry we hit
    // NOTE: Verify that InstanceID() returns the correct mesh descriptor index
    // This may need adjustment based on how TLAS instances are set up
    uint meshID = InstanceID();
    MeshDesc mesh = meshDescs[meshID];
    
    // Fetch triangle indices (offset into global buffer)
    uint triIndex = mesh.ibOffset + PrimitiveIndex() * 3;
    uint idx0 = globalIndices[triIndex + 0];
    uint idx1 = globalIndices[triIndex + 1];
    uint idx2 = globalIndices[triIndex + 2];
    
    // Fetch vertices
    Vertex v0 = globalVertices[idx0];
    Vertex v1 = globalVertices[idx1];
    Vertex v2 = globalVertices[idx2];
    
    // Barycentric interpolation
    float3 barycentrics = float3(1.0 - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
    
    float3 position = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
    float3 normal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    float2 texCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;
    
    // Transform to world space
    float3 worldPos = mul(ObjectToWorld3x4(), float4(position, 1.0)).xyz;
    float3 worldNormal = normalize(mul((float3x3)ObjectToWorld3x4(), normal));
    
    // Material lookup
    uint materialID = mesh.materialID;
    
    // Rest of shading logic UNCHANGED from this point
    // ... (continue with existing shading code using worldPos, worldNormal, texCoord, materialID)
}
```

**Important:** Preserve all existing shading logic after the geometry fetch. Only replace the geometry data access portion.

### Actions Required

1. Read the current `render_dxr.hlsl` to understand its structure
2. Add Vertex and MeshDesc structs
3. Add global buffer declarations (t10, t11, t12 in space0)
4. Comment out all space1 declarations
5. Update ClosestHit shader with new geometry fetch logic
6. **DO NOT recompile yet** - shader will be compiled when C++ changes are done

### Validation

- ✅ Shader syntax looks correct
- ✅ No space1 resources remain uncommented
- ✅ Global buffers use space0
- ✅ Existing shading logic preserved

---

## Task 2A.2.2: Update DXR C++ - Add Global Buffer Member Variables

**Objective:** Add member variables to RenderDXR class for global buffers.

### File: `backends/dxr/render_dxr.h`

Find the `RenderDXR` class definition. Locate existing buffer member variables (likely near other D3D12 resources).

Add the following member variables:

```cpp
    // Phase 2A.2: Global buffers
    dxr::Buffer global_vertex_buffer;
    dxr::Buffer global_index_buffer;
    dxr::Buffer mesh_desc_buffer;
```

**Note:** These should be added alongside other buffer members, likely in the private section.

### Actions Required

1. Locate `RenderDXR` class in `render_dxr.h`
2. Add three global buffer member variables
3. Build to verify syntax (compilation will fail until set_scene is updated - expected)

### Validation

- ✅ Member variables added to class
- ✅ Syntax correct (same type as other buffers)

---

## Task 2A.2.3: Update DXR C++ - Create Global Buffers in set_scene()

**Objective:** Create and upload global buffers to GPU.

### File: `backends/dxr/render_dxr.cpp`

Find the `set_scene()` function. This is where per-geometry buffers are currently created.

#### Step 1: Create Global Buffers

At the beginning of `set_scene()`, AFTER `frame_id` and `samples_per_pixel` initialization, add:

```cpp
void RenderDXR::set_scene(const Scene &scene)
{
    frame_id = 0;
    samples_per_pixel = scene.samples_per_pixel;
    
    // Phase 2A.2: Create global buffers
    std::cout << "[Phase 2A.2 DXR] Creating global buffers...\n";
    std::cout << "  - Vertices: " << scene.global_vertices.size() << "\n";
    std::cout << "  - Indices: " << scene.global_indices.size() << "\n";
    std::cout << "  - MeshDescriptors: " << scene.mesh_descriptors.size() << "\n";
    
    // 1. Global Vertex Buffer
    if (!scene.global_vertices.empty()) {
        dxr::Buffer upload_vertices = dxr::Buffer::upload(
            device.Get(),
            scene.global_vertices.size() * sizeof(Vertex),
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        std::memcpy(upload_vertices.map(), scene.global_vertices.data(), 
                    scene.global_vertices.size() * sizeof(Vertex));
        upload_vertices.unmap();
        
        global_vertex_buffer = dxr::Buffer::device(
            device.Get(),
            scene.global_vertices.size() * sizeof(Vertex),
            D3D12_RESOURCE_STATE_COPY_DEST
        );
        
        // Will copy later via command list
    }
    
    // 2. Global Index Buffer
    if (!scene.global_indices.empty()) {
        dxr::Buffer upload_indices = dxr::Buffer::upload(
            device.Get(),
            scene.global_indices.size() * sizeof(uint32_t),
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        std::memcpy(upload_indices.map(), scene.global_indices.data(), 
                    scene.global_indices.size() * sizeof(uint32_t));
        upload_indices.unmap();
        
        global_index_buffer = dxr::Buffer::device(
            device.Get(),
            scene.global_indices.size() * sizeof(uint32_t),
            D3D12_RESOURCE_STATE_COPY_DEST
        );
    }
    
    // 3. MeshDesc Buffer
    if (!scene.mesh_descriptors.empty()) {
        dxr::Buffer upload_mesh_descs = dxr::Buffer::upload(
            device.Get(),
            scene.mesh_descriptors.size() * sizeof(MeshDesc),
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        std::memcpy(upload_mesh_descs.map(), scene.mesh_descriptors.data(), 
                    scene.mesh_descriptors.size() * sizeof(MeshDesc));
        upload_mesh_descs.unmap();
        
        mesh_desc_buffer = dxr::Buffer::device(
            device.Get(),
            scene.mesh_descriptors.size() * sizeof(MeshDesc),
            D3D12_RESOURCE_STATE_COPY_DEST
        );
    }
    
    // Find where command list is reset, or reset it here if needed
    // CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
    
    // Copy upload buffers to device buffers (add after command list reset)
    if (!scene.global_vertices.empty()) {
        cmd_list->CopyResource(global_vertex_buffer.get(), upload_vertices.get());
    }
    if (!scene.global_indices.empty()) {
        cmd_list->CopyResource(global_index_buffer.get(), upload_indices.get());
    }
    if (!scene.mesh_descriptors.empty()) {
        cmd_list->CopyResource(mesh_desc_buffer.get(), upload_mesh_descs.get());
    }
    
    // Transition resources to shader read state
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    if (!scene.global_vertices.empty()) {
        barriers.push_back(barrier_transition(global_vertex_buffer, 
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }
    if (!scene.global_indices.empty()) {
        barriers.push_back(barrier_transition(global_index_buffer, 
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }
    if (!scene.mesh_descriptors.empty()) {
        barriers.push_back(barrier_transition(mesh_desc_buffer, 
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }
    
    if (!barriers.empty()) {
        cmd_list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }
    
    std::cout << "[Phase 2A.2 DXR] Global buffers created and uploaded\n";
    
    // Continue with BLAS/TLAS building...
    // (existing code follows)
}
```

**Important:** 
- Upload buffers are local variables - they'll be destroyed when they go out of scope after copy
- Check if `barrier_transition` helper function exists; adapt based on actual DXR utilities
- Ensure this code is placed BEFORE BLAS building logic

#### Step 2: Update BLAS Building (Based on 2A.2.0 Strategy)

**THIS DEPENDS ON THE ANALYSIS FROM TASK 2A.2.0 BLAS STRATEGY**

Follow the strategy documented in `2A.2.0_BLAS_Strategy.md`. Common approaches:

**If using temporary buffers for BLAS:**
- Keep existing BLAS building code mostly unchanged
- Build BLAS using original `scene.meshes[].geometries[]` data
- Create temporary buffers just for BLAS input, discard after building

**If using global buffers for BLAS:**
- Modify geometry descriptors to use global buffers with offsets
- Use stride/offset parameters in D3D12_RAYTRACING_GEOMETRY_DESC

#### Step 3: Remove Per-Geometry Buffer Creation Loop

Find the loop that creates per-geometry buffers (identified in Phase 2A.0.2 analysis). This typically looks like:

```cpp
// OLD CODE - REMOVE OR COMMENT OUT
for (const auto& geom : mesh.geometries) {
    // Create vertex buffer
    auto vb = create_buffer(..., geom.vertices.data(), ...);
    // Create index buffer
    auto ib = create_buffer(..., geom.indices.data(), ...);
    // ... etc
}
```

**Action:** Comment out this loop with:
```cpp
// Phase 2A.2: Removed - using global buffers instead
// Old per-geometry buffer creation code
```

**Exception:** If BLAS building still needs temporary buffers (per 2A.2.0 strategy), keep minimal buffer creation for BLAS only.

### Actions Required

1. Add global buffer creation code at start of `set_scene()`
2. Add copy and barrier logic
3. Update BLAS building based on 2A.2.0 strategy
4. Remove (or comment out) per-geometry buffer creation loop
5. Add `#include <iostream>` if not present (for debug output)
6. Build the project - expect compilation errors in descriptor heap code (next task)

### Validation

- ✅ Global buffers created with correct sizes
- ✅ Upload and device buffers handled correctly
- ✅ Per-geometry buffer loop removed/commented
- ✅ Debug output shows buffer sizes

---

## Task 2A.2.4: Update DXR C++ - Descriptor Heap

**Objective:** Add SRVs (Shader Resource Views) for global buffers to descriptor heap.

### File: `backends/dxr/render_dxr.cpp`

Find the function that builds the descriptor heap (likely `build_descriptor_heap()` or similar, or within `set_scene()`).

#### Current Descriptor Heap Analysis

From Phase 2A.0.2 analysis, identify:
- What resources are currently in the descriptor heap?
- What are their descriptor indices?
- Where is the descriptor heap created?

#### Add SRVs for Global Buffers

Find where descriptors are created (likely creating SRVs for textures, materials, etc.).

Add SRV creation for global buffers at descriptors t10, t11, t12:

```cpp
// Phase 2A.2: Create SRVs for global buffers

// Assuming there's a descriptor handle increment pattern like:
// CD3DX12_CPU_DESCRIPTOR_HANDLE handle(heap->GetCPUDescriptorHandleForHeapStart());
// handle.Offset(index, descriptor_size);

// Global Vertices SRV (t10, space0)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(scene.global_vertices.size());
    srvDesc.Buffer.StructureByteStride = sizeof(Vertex);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(/* appropriate descriptor handle for t10 */);
    device->CreateShaderResourceView(global_vertex_buffer.get(), &srvDesc, handle);
}

// Global Indices SRV (t11, space0)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_UINT;  // uint format
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(scene.global_indices.size());
    srvDesc.Buffer.StructureByteStride = 0;  // Raw typed buffer
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(/* appropriate descriptor handle for t11 */);
    device->CreateShaderResourceView(global_index_buffer.get(), &srvDesc, handle);
}

// MeshDesc SRV (t12, space0)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(scene.mesh_descriptors.size());
    srvDesc.Buffer.StructureByteStride = sizeof(MeshDesc);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(/* appropriate descriptor handle for t12 */);
    device->CreateShaderResourceView(mesh_desc_buffer.get(), &srvDesc, handle);
}
```

**Important:** Adapt descriptor handle management based on actual code patterns in the project.

### Actions Required

1. Find descriptor heap creation code
2. Determine correct descriptor indices for t10, t11, t12
3. Add SRV creation for three global buffers
4. Ensure descriptor heap is large enough (may need to increase size)
5. Build and check for errors

### Validation

- ✅ Descriptor heap large enough for new SRVs
- ✅ SRVs created with correct formats
- ✅ Descriptor indices match shader register declarations

---

## Task 2A.2.5: Update DXR C++ - Root Signature

**Objective:** Remove local root signature (space1), ensure global root signature includes new SRVs.

### File: `backends/dxr/render_dxr.cpp`

Find root signature creation code (likely in initialization or `set_scene()`).

#### Step 1: Remove Local Root Signature

Find any code that creates or references a local root signature for space1. This might include:
- `D3D12_ROOT_SIGNATURE_DESC` with space1 parameters
- Hit group local root signature in shader table setup
- Shader record creation for per-geometry data

**Action:** Comment out or remove local root signature setup:
```cpp
// Phase 2A.2: Removed local root signature (space1)
// All resources now in global root signature (space0)
```

#### Step 2: Verify Global Root Signature

Ensure the global root signature descriptor table includes:
- Existing resources (whatever was in space0 before)
- New SRVs for global buffers (t10, t11, t12)

The root signature should have a descriptor range covering the new SRV indices.

Example (adapt based on actual code):
```cpp
CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 13, 0, 0);  // t0-t12 in space0

CD3DX12_ROOT_PARAMETER1 rootParams[1];
rootParams[0].InitAsDescriptorTable(1, ranges);

// ... rest of root signature creation
```

#### Step 3: Update Shader Table (SBT)

Find shader binding table (SBT) creation. With global buffers, hit group shader records should be simplified:
- No per-geometry shader record data
- All hit groups can use same record (or no local data)

**Action:** Simplify SBT creation - remove per-geometry shader record loop.

### Actions Required

1. Find and remove local root signature code
2. Verify global root signature covers t0-t12 (or appropriate range)
3. Simplify shader table - remove per-geometry records
4. Build the project

### Validation

- ✅ Local root signature removed
- ✅ Global root signature includes new SRVs
- ✅ Shader table simplified (no per-geometry records)
- ✅ Project compiles successfully

---

## Task 2A.2.6: Build and Test DXR Backend

**Objective:** Verify that DXR rendering is identical to baseline.

### Build

```powershell
cd C:\dev\ChameleonRT
cmake --build build --config Debug --target crt_dxr
```

**Fix any compilation errors before proceeding.**

### Test 1: test_cube.obj

Run the application:
```powershell
.\build\Debug\chameleonrt.exe dxr .\test_cube.obj
```

**Expected Console Output:**
```
[Phase 2A.1] Global buffers built:
  - Vertices: ...
  - Indices: ...
  - MeshDescriptors: ...
  - GeometryInstances: ...
  - TransformMatrices: ...
[Phase 2A.2 DXR] Creating global buffers...
  - Vertices: ...
  - Indices: ...
  - MeshDescriptors: ...
[Phase 2A.2 DXR] Global buffers created and uploaded
```

**Visual Validation:**
1. Take a screenshot of the rendered output
2. Compare with baseline: `Migration/SlangIntegration/Phase2A_Analysis/baseline_dxr_cube.png`
3. **CRITICAL:** Rendering should be IDENTICAL

**Manual Checks:**
- [ ] Cube renders correctly (geometry visible)
- [ ] Colors/shading look correct
- [ ] No black screen or crash
- [ ] Normals appear correct (shading on faces)
- [ ] No artifacts or corruption

### Test 2: Sponza (if available)

Run with Sponza:
```powershell
.\build\Debug\chameleonrt.exe dxr C:\Demo\Assets\sponza\sponza.obj
```

**Visual Validation:**
1. Take a screenshot
2. Compare with baseline: `Migration/SlangIntegration/Phase2A_Analysis/baseline_dxr_sponza.png`
3. Rendering should be IDENTICAL

**Manual Checks:**
- [ ] Scene loads without crash
- [ ] All geometry visible
- [ ] Textures/materials look correct
- [ ] No missing geometry
- [ ] Performance similar to baseline

### Deliverable

Create a file: `Migration/SlangIntegration/Phase2A_Analysis/2A.2.6_DXR_Test_Results.md`

```markdown
# Phase 2A.2.6: DXR Backend Test Results

**Date:** [Date]

## Build Status

- [x] Code compiles successfully
- [x] No linker errors
- [ ] Shader compiles successfully
- [ ] No D3D12 validation errors

**Build Console Output:**
```
[Paste any relevant build output or warnings]
```

## Test 1: test_cube.obj

**Console Output:**
```
[Paste console output]
```

**Visual Comparison:**
- [ ] Rendering IDENTICAL to baseline: [Yes/No]
- [ ] Geometry correct: [Yes/No]
- [ ] Shading correct: [Yes/No]
- [ ] No artifacts: [Yes/No]

**Screenshot:** [Attach or reference screenshot]

**Observations:**
[Any notes about differences, issues, or unexpected behavior]

## Test 2: Sponza

**Console Output:**
```
[Paste console output]
```

**Visual Comparison:**
- [ ] Rendering IDENTICAL to baseline: [Yes/No]
- [ ] All geometry present: [Yes/No]
- [ ] Materials/textures correct: [Yes/No]
- [ ] Performance similar: [Yes/No]

**Screenshot:** [Attach or reference screenshot]

**Observations:**
[Any notes]

## Issues Found

[List any issues, bugs, or rendering differences]

### Issue 1: [Description]
**Impact:** [Visual/Performance/Crash]
**Potential Cause:** [Analysis]
**Resolution:** [How it was fixed, or "Pending investigation"]

## Performance Comparison (Optional)

**Baseline:**
- test_cube FPS: [if available]
- Sponza FPS: [if available]

**After Refactor:**
- test_cube FPS: [measured]
- Sponza FPS: [measured]

**Analysis:**
[Is performance similar? Small overhead acceptable.]

## Status

- [ ] Phase 2A.2 DXR refactor complete
- [ ] Rendering identical to baseline
- [ ] Ready to proceed to Phase 2A.3 (Vulkan)

**If rendering is NOT identical, DO NOT proceed. Debug and fix issues first.**
```

---

## Common Issues and Debugging

### Issue: Black screen or no rendering

**Possible Causes:**
1. Descriptor heap not large enough
2. SRVs created incorrectly
3. Wrong descriptor indices in shader (t10, t11, t12 mismatch)
4. Barrier transitions incorrect
5. InstanceID() returning wrong mesh descriptor index

**Debug Steps:**
1. Enable D3D12 debug layer
2. Check for validation errors
3. Use PIX or RenderDoc to inspect resources
4. Verify descriptor heap size
5. Add debug output in shader (payload colors)

### Issue: Corrupted geometry

**Possible Causes:**
1. Index buffer offset calculation wrong
2. Vertex offset not applied correctly
3. MeshDesc offsets incorrect
4. InstanceID() mapping wrong

**Debug Steps:**
1. Print first few MeshDesc entries in C++
2. Verify offsets match expectations from Phase 2A.1
3. Check global_indices are properly adjusted
4. Verify InstanceID() < mesh_descriptors.size()

### Issue: Wrong materials/colors

**Possible Causes:**
1. materialID in MeshDesc incorrect
2. Material buffer access broken
3. UV coordinates wrong

**Debug Steps:**
1. Verify MeshDesc.materialID values
2. Check that material buffer is still accessible
3. Verify texCoord interpolation

### Issue: Crash during BLAS building

**Possible Causes:**
1. BLAS strategy (from 2A.2.0) not implemented correctly
2. Temporary buffers freed too early
3. Geometry descriptors malformed

**Debug Steps:**
1. Review 2A.2.0_BLAS_Strategy.md
2. Check buffer lifetimes
3. Validate geometry descriptor parameters

---

## Git Commit Recommendation

After successful completion of Phase 2A.2:

```powershell
git add backends/dxr/
git commit -m "Phase 2A.2: DXR backend refactor - global buffers

- Updated HLSL shader to use global buffers (space0)
- Removed space1 local root signature
- Added global vertex, index, and mesh descriptor buffers
- Updated descriptor heap with SRVs for global buffers
- Simplified shader binding table (no per-geometry records)
- BLAS building strategy: [describe chosen approach]

Validation:
- test_cube.obj: Rendering identical to baseline
- Sponza: Rendering identical to baseline
- No performance regression"
```

---

## Next Step

**Proceed to Phase 2A.3:** Vulkan Backend Refactor

Use prompt file: `Phase_2A.3_Vulkan_Backend_Refactor.md`
