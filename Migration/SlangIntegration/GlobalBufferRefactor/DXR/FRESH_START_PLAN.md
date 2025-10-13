# DXR Global Buffer Implementation - Fresh Start Plan

**Date:** October 13, 2025  
**Status:** 🎯 PLANNING  
**Base Commit:** e89498e (CPU scene refactor COMPLETE)  
**Current Commit:** 02c90d6 (documentation cleanup)

---

## Context: Learning from Previous Attempt

### What We Learned (October 11-12, 2025)

**Previous attempt encountered persistent `CreateStateObject E_INVALIDARG` error:**
- Multiple bugs fixed (root signatures, SBT) but core error persisted
- 6+ hours debugging with no resolution
- Error was too opaque to diagnose without detailed D3D12 error messages
- Made too many changes simultaneously (6+ subsystems at once)

**Key Lessons:**
1. **Incremental changes are critical** - validate after each small step
2. **CreateStateObject errors are extremely hard to debug** - need better debugging strategy
3. **Test compilation early** - shader issues should be caught before C++ integration
4. **Validation gates needed** - don't proceed if previous step fails

---

## Fresh Start Strategy

### Core Principle: Minimal Changes, Maximum Validation

We will implement the DXR refactor with:
1. **Tiny incremental steps** (5-10 line changes max)
2. **Validation gate after each step** (must compile and run)
3. **Rollback tags** (git tag before each risky change)
4. **Shader testing in isolation** (before C++ integration)

### Implementation Approach: Option A Refined

**Strategy:** Temporary per-geometry buffers for BLAS, global buffers for shaders  
**Rationale:** Keep BLAS building unchanged (proven to work), only change shader access  
**Reference:** See [BLAS_STRATEGY.md](BLAS_STRATEGY.md) for detailed analysis

---

## Phase 1: Shader-Only Changes (No C++ Yet)

**Goal:** Update shader to use global buffers, validate compilation in isolation

### Step 1.1: Add Global Buffer Structs to Shader

**File:** `backends/dxr/render_dxr.hlsl`

**Add at top of file (after includes):**

```hlsl
// : Unified vertex structure
struct Vertex {
    float3 position;
    float3 normal;
    float2 texCoord;
};

// : Mesh descriptor for global buffer indirection
struct MeshDesc {
    uint vbOffset;      // Offset into global vertex buffer
    uint ibOffset;      // Offset into global index buffer  
    uint vertexCount;   // Number of vertices
    uint indexCount;    // Number of indices (triangles * 3)
    uint materialID;    // Material index
};
```

**Validation:**
- Shader compiles without errors ✅
- No runtime testing yet (C++ unchanged)

**Rollback:** `git tag phase2a2-step1.1-structs`

---

### Step 1.2: Add Global Buffer Declarations (space0)

**File:** `backends/dxr/render_dxr.hlsl`

**Add after existing space0 resources (around line 30-40):**

```hlsl
// : Global buffers (all geometry data concatenated)
StructuredBuffer<Vertex> globalVertices : register(t10, space0);
StructuredBuffer<uint> globalIndices : register(t11, space0);    // uint not uint3!
StructuredBuffer<MeshDesc> meshDescs : register(t12, space0);
```

**Important:** Keep existing space1 declarations for now (don't delete yet)

**Validation:**
- Shader compiles without errors ✅
- CMake rebuild succeeds ✅
- Application launches (using old space1 path still) ✅

**Rollback:** `git tag phase2a2-step1.2-declarations`

---

### Step 1.3: Create Debug ClosestHit Version (Parallel Implementation)

**File:** Create `backends/dxr/render_dxr.hlsl` (modify)

**Add NEW ClosestHit shader (don't replace old one yet):**

```hlsl
// : Debug version using global buffers
[shader("closesthit")]
void ClosestHit_GlobalBuffers(inout HitInfo payload, Attributes attrib)
{
    // Get which geometry we hit
    uint meshID = InstanceID();  // Built-in
    MeshDesc mesh = meshDescs[meshID];
    
    // Calculate triangle index with offset
    uint triIndex = mesh.ibOffset + PrimitiveIndex() * 3;
    
    // Fetch indices
    uint idx0 = globalIndices[triIndex + 0];
    uint idx1 = globalIndices[triIndex + 1];
    uint idx2 = globalIndices[triIndex + 2];
    
    // Fetch vertices
    Vertex v0 = globalVertices[idx0];
    Vertex v1 = globalVertices[idx1];
    Vertex v2 = globalVertices[idx2];
    
    // Barycentric interpolation
    float3 barycentrics = float3(1.0 - attrib.bary.x - attrib.bary.y, 
                                  attrib.bary.x, 
                                  attrib.bary.y);
    
    float3 position = v0.position * barycentrics.x + 
                     v1.position * barycentrics.y + 
                     v2.position * barycentrics.z;
    
    float3 normal = normalize(v0.normal * barycentrics.x + 
                             v1.normal * barycentrics.y + 
                             v2.normal * barycentrics.z);
    
    float2 texCoord = v0.texCoord * barycentrics.x + 
                     v1.texCoord * barycentrics.y + 
                     v2.texCoord * barycentrics.z;
    
    // Transform to world space
    float3 worldPos = mul(ObjectToWorld3x4(), float4(position, 1.0)).xyz;
    float3 worldNormal = normalize(mul((float3x3)ObjectToWorld3x4(), normal));
    
    // Use material ID from mesh descriptor
    uint materialID = mesh.materialID;
    
    // Continue with existing shading logic...
    // (Copy rest of shading from original ClosestHit)
}
```

**Keep original `ClosestHit` shader unchanged for now**

**Validation:**
- Shader compiles with BOTH entry points ✅
- Old ClosestHit still used (application works as before) ✅

**Rollback:** `git tag phase2a2-step1.3-parallel-shader`

---

### Step 1.4: Test Shader Compilation in Isolation

**Goal:** Verify shader compiles to valid DXIL before C++ integration

**Create test script:** `test_shader_compile.ps1`

```powershell
# Test compile render_dxr.hlsl to DXIL
$dxc = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\dxc.exe"

Write-Host "Testing shader compilation..."

# Compile RayGen
& $dxc -T lib_6_3 -Fo build\test_raygen.dxil backends\dxr\render_dxr.hlsl -E RayGen
if ($LASTEXITCODE -ne 0) { Write-Host "❌ RayGen failed"; exit 1 }

# Compile ClosestHit_GlobalBuffers
& $dxc -T lib_6_3 -Fo build\test_closesthit.dxil backends\dxr\render_dxr.hlsl -E ClosestHit_GlobalBuffers
if ($LASTEXITCODE -ne 0) { Write-Host "❌ ClosestHit_GlobalBuffers failed"; exit 1 }

# Compile Miss
& $dxc -T lib_6_3 -Fo build\test_miss.dxil backends\dxr\render_dxr.hlsl -E Miss
if ($LASTEXITCODE -ne 0) { Write-Host "❌ Miss failed"; exit 1 }

Write-Host "✅ All shaders compiled successfully"
```

**Run:** `.\test_shader_compile.ps1`

**Validation:**
- All shaders compile to DXIL ✅
- No errors or warnings ✅
- DXIL files created ✅

**Rollback:** `git tag phase2a2-step1.4-shader-validated`

---

## Phase 2: C++ Buffer Creation (No Shader Integration Yet)

**Goal:** Create global buffers in C++, validate creation, but DON'T use them yet

### Step 2.1: Add Global Buffer Member Variables

**File:** `backends/dxr/render_dxr.h`

**Add to `RenderDXR` class:**

```cpp
class RenderDXR {
    // ... existing members
    
    // : Global buffers (for shader access)
    dxr::Buffer global_vertex_buffer;
    dxr::Buffer global_index_buffer;
    dxr::Buffer mesh_desc_buffer;
};
```

**Validation:**
- Compiles ✅
- Application runs (buffers not used yet) ✅


---

### Step 2.2: Create Global Buffers (But Don't Bind Yet)

**File:** `backends/dxr/render_dxr.cpp`

**In `set_scene()` function, ADD at the end (after existing BLAS/TLAS build):**

```cpp
void RenderDXR::set_scene(const Scene &scene)
{
    // ... existing BLAS/TLAS code ...
    
    // : Create global buffers (for shader access)
    // NOTE: Still creating per-geometry buffers for BLAS above
    
    std::cout << "[] Creating global buffers...\n";
    
    // 1. Global Vertex Buffer
    dxr::Buffer upload_verts = dxr::Buffer::upload(
        device.Get(),
        scene.global_vertices.size() * sizeof(Vertex),
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    std::memcpy(upload_verts.map(), 
                scene.global_vertices.data(), 
                upload_verts.size());
    upload_verts.unmap();
    
    global_vertex_buffer = dxr::Buffer::device(
        device.Get(),
        upload_verts.size(),
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    
    // 2. Global Index Buffer
    dxr::Buffer upload_indices = dxr::Buffer::upload(
        device.Get(),
        scene.global_indices.size() * sizeof(uint32_t),
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    std::memcpy(upload_indices.map(), 
                scene.global_indices.data(), 
                upload_indices.size());
    upload_indices.unmap();
    
    global_index_buffer = dxr::Buffer::device(
        device.Get(),
        upload_indices.size(),
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    
    // 3. MeshDesc Buffer
    dxr::Buffer upload_descs = dxr::Buffer::upload(
        device.Get(),
        scene.mesh_descriptors.size() * sizeof(MeshDesc),
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    std::memcpy(upload_descs.map(), 
                scene.mesh_descriptors.data(), 
                upload_descs.size());
    upload_descs.unmap();
    
    mesh_desc_buffer = dxr::Buffer::device(
        device.Get(),
        upload_descs.size(),
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    
    // Upload to GPU
    CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
    
    cmd_list->CopyResource(global_vertex_buffer.get(), upload_verts.get());
    cmd_list->CopyResource(global_index_buffer.get(), upload_indices.get());
    cmd_list->CopyResource(mesh_desc_buffer.get(), upload_descs.get());
    
    // Transition to shader-readable
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {
        dxr::barrier_transition(global_vertex_buffer.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        dxr::barrier_transition(global_index_buffer.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        dxr::barrier_transition(mesh_desc_buffer.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    cmd_list->ResourceBarrier(barriers.size(), barriers.data());
    
    CHECK_ERR(cmd_list->Close());
    ID3D12CommandList *cmd_lists[] = {cmd_list.Get()};
    cmd_queue->ExecuteCommandLists(1, cmd_lists);
    dxr::flush_queue(cmd_queue.Get(), fence.Get(), fence_value);
    
    std::cout << "[] Global buffers created successfully\n";
    std::cout << "  - Vertices: " << scene.global_vertices.size() << "\n";
    std::cout << "  - Indices: " << scene.global_indices.size() << "\n";
    std::cout << "  - MeshDescs: " << scene.mesh_descriptors.size() << "\n";
}
```

**Validation:**
- Build succeeds ✅
- Run test_cube.obj ✅
- Console shows global buffers created ✅
- Application still renders (using old path) ✅
- No crashes ✅

**Rollback:** `git tag phase2a2-step2.2-buffers-created`

---

## Phase 3: Descriptor Heap Updates

**Goal:** Add global buffers to descriptor heap (but don't use in pipeline yet)

**Duration:** 30-45 minutes

---

### Step 3.1: Extend Descriptor Heap (15 min)

**File:** `backends/dxr/render_dxr.cpp`

**Location:** Find the function that builds the descriptor heap (search for `DescriptorHeapBuilder` or `raygen_desc_heap`)

**Expected location:** Around line 900-950 in `render_dxr.cpp`

**Change:**

```cpp
// BEFORE (Note: textures now at t30+ after Phase 1 register change):
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_uav_range(2, 0, 0)      // u0-u1 (output, accum_buffer)
    .add_srv_range(3, 0, 0)      // t0-t2 (TLAS, materials, lights)
    .add_cbv_range(1, 0, 0)      // b0 (ViewParams)
    .add_srv_range(textures.size(), 30, 0)  // t30+ (textures array - moved in Phase 1)
    .create(device.Get());

// AFTER (adding global buffers at t10-t14):
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_uav_range(2, 0, 0)      // u0-u1 (output, accum_buffer)
    .add_srv_range(3, 0, 0)      // t0-t2 (TLAS, materials, lights)
    .add_cbv_range(1, 0, 0)      // b0 (ViewParams)
    .add_srv_range(textures.size(), 30, 0)  // t30+ (textures array)
    .add_srv_range(5, 10, 0)     // t10-t14 (NEW: global buffers)
    .create(device.Get());
```

**Critical Notes:**
- **Phase 1 Change:** Textures moved from t3 to t30 to free up t10-t14 for global buffers
- The descriptor heap layout is now: `[u0-u1][t0-t2][b0][t30...tN][t10-t14]`
- **RayGen shader unchanged** - still uses u0-u1, t0-t2, b0, t30+
- New slots (t10-t14) exist but aren't populated yet
- Total heap size increased by 5 descriptors (for 5 global buffers)

**Validation:**
- Build succeeds ✅
- **Proof:** Console shows no errors during heap creation
- Application launches ✅
- **Proof:** Application still renders using old path
- No validation errors ✅
- **Proof:** D3D12 debug layer silent (enable with `D3D12GetDebugInterface`)

**Rollback:** `git tag phase2a2-step3.1-heap-extended`

**Debug if fails:**
```cpp
// Add before .create()
std::cout << "[DEBUG] Descriptor heap layout:\n";
std::cout << "  UAVs: 2 (u0-u1)\n";
std::cout << "  SRVs (scene): 3 (t0-t2)\n";
std::cout << "  CBVs: 1 (b0)\n";
std::cout << "  SRVs (textures): " << textures.size() << " (t30+)\n";
std::cout << "  SRVs (global): 5 (t10-t14)\n";
std::cout << "  Total descriptors: " << (2 + 3 + 1 + textures.size() + 5) << "\n";
```

---

### Step 3.2: Create SRVs for Global Buffers (30 min)

**File:** `backends/dxr/render_dxr.cpp`

**Location:** Immediately after descriptor heap creation (where existing SRVs are created)

**Expected location:** Around line 950-1000

**Add this code:**

```cpp
// Phase 3 : Create SRVs for global buffers
std::cout << "[Phase 3] Creating SRVs for global buffers...\n";

// Calculate descriptor offset to t10 position
// Layout: [u0-u1][t0-t2][b0][t30...tN][t10-t14]
//         [  2  ][ 3  ][1][textures.size()][HERE]
// Note: Textures moved to t30+ in Phase 1 to free up t10-t14
const UINT descriptor_increment = device->GetDescriptorHandleIncrementSize(
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

D3D12_CPU_DESCRIPTOR_HANDLE base_handle = raygen_desc_heap.cpu_desc_handle();
UINT offset_to_t10 = 2 + 3 + 1 + static_cast<UINT>(textures.size());

D3D12_CPU_DESCRIPTOR_HANDLE global_srv_handle = base_handle;
global_srv_handle.ptr += offset_to_t10 * descriptor_increment;

// ===== t10: globalVertices SRV =====
std::cout << "  Creating globalVertices SRV at t10 (offset " << offset_to_t10 << ")...\n";

D3D12_SHADER_RESOURCE_VIEW_DESC vertex_srv_desc = {};
vertex_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
vertex_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
vertex_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
vertex_srv_desc.Buffer.FirstElement = 0;
vertex_srv_desc.Buffer.NumElements = static_cast<UINT>(scene.global_vertices.size());
vertex_srv_desc.Buffer.StructureByteStride = sizeof(glm::vec3);  // 12 bytes
vertex_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

device->CreateShaderResourceView(
    global_vertex_buffer.get(),
    &vertex_srv_desc,
    global_srv_handle
);

std::cout << "    NumElements: " << vertex_srv_desc.Buffer.NumElements 
          << ", StructureByteStride: " << vertex_srv_desc.Buffer.StructureByteStride << "\n";

// ===== t11: globalIndices SRV =====
global_srv_handle.ptr += descriptor_increment;
std::cout << "  Creating globalIndices SRV at t11 (offset " << (offset_to_t10 + 1) << ")...\n";

D3D12_SHADER_RESOURCE_VIEW_DESC index_srv_desc = {};
index_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
index_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
index_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
index_srv_desc.Buffer.FirstElement = 0;
index_srv_desc.Buffer.NumElements = static_cast<UINT>(scene.global_indices.size());
index_srv_desc.Buffer.StructureByteStride = sizeof(glm::uvec3);  // 12 bytes
index_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

device->CreateShaderResourceView(
    global_index_buffer.get(),
    &index_srv_desc,
    global_srv_handle
);

std::cout << "    NumElements: " << index_srv_desc.Buffer.NumElements 
          << ", StructureByteStride: " << index_srv_desc.Buffer.StructureByteStride << "\n";

// ===== t12: globalNormals SRV (if present) =====
if (!scene.global_normals.empty()) {
    global_srv_handle.ptr += descriptor_increment;
    std::cout << "  Creating globalNormals SRV at t12 (offset " << (offset_to_t10 + 2) << ")...\n";
    
    D3D12_SHADER_RESOURCE_VIEW_DESC normal_srv_desc = {};
    normal_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    normal_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    normal_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    normal_srv_desc.Buffer.FirstElement = 0;
    normal_srv_desc.Buffer.NumElements = static_cast<UINT>(scene.global_normals.size());
    normal_srv_desc.Buffer.StructureByteStride = sizeof(glm::vec3);  // 12 bytes
    normal_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    
    device->CreateShaderResourceView(
        global_normal_buffer.get(),
        &normal_srv_desc,
        global_srv_handle
    );
    
    std::cout << "    NumElements: " << normal_srv_desc.Buffer.NumElements 
              << ", StructureByteStride: " << normal_srv_desc.Buffer.StructureByteStride << "\n";
}

// ===== t13: globalUVs SRV (if present) =====
if (!scene.global_uvs.empty()) {
    global_srv_handle.ptr += descriptor_increment;
    std::cout << "  Creating globalUVs SRV at t13 (offset " << (offset_to_t10 + 3) << ")...\n";
    
    D3D12_SHADER_RESOURCE_VIEW_DESC uv_srv_desc = {};
    uv_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    uv_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    uv_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    uv_srv_desc.Buffer.FirstElement = 0;
    uv_srv_desc.Buffer.NumElements = static_cast<UINT>(scene.global_uvs.size());
    uv_srv_desc.Buffer.StructureByteStride = sizeof(glm::vec2);  // 8 bytes
    uv_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    
    device->CreateShaderResourceView(
        global_uv_buffer.get(),
        &uv_srv_desc,
        global_srv_handle
    );
    
    std::cout << "    NumElements: " << uv_srv_desc.Buffer.NumElements 
              << ", StructureByteStride: " << uv_srv_desc.Buffer.StructureByteStride << "\n";
}

// ===== t14: meshDescs SRV =====
global_srv_handle.ptr += descriptor_increment;
std::cout << "  Creating meshDescs SRV at t14 (offset " << (offset_to_t10 + 4) << ")...\n";

D3D12_SHADER_RESOURCE_VIEW_DESC mesh_srv_desc = {};
mesh_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
mesh_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
mesh_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
mesh_srv_desc.Buffer.FirstElement = 0;
mesh_srv_desc.Buffer.NumElements = static_cast<UINT>(scene.mesh_descriptors.size());
mesh_srv_desc.Buffer.StructureByteStride = sizeof(MeshDesc);  // 32 bytes (from Phase 1)
mesh_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

device->CreateShaderResourceView(
    mesh_desc_buffer.get(),
    &mesh_srv_desc,
    global_srv_handle
);

std::cout << "    NumElements: " << mesh_srv_desc.Buffer.NumElements 
          << ", StructureByteStride: " << mesh_srv_desc.Buffer.StructureByteStride << "\n";

std::cout << "[Phase 3] SRVs created successfully at t10-t14\n";
```

**Expected Console Output (for test_cube_complete.obj with normals/UVs):**
```
[Phase 3] Creating SRVs for global buffers...
  Creating globalVertices SRV at t10 (offset 6)...
    NumElements: 24, StructureByteStride: 12
  Creating globalIndices SRV at t11 (offset 7)...
    NumElements: 12, StructureByteStride: 12
  Creating globalNormals SRV at t12 (offset 8)...
    NumElements: 24, StructureByteStride: 12
  Creating globalUVs SRV at t13 (offset 9)...
    NumElements: 24, StructureByteStride: 8
  Creating meshDescs SRV at t14 (offset 10)...
    NumElements: 1, StructureByteStride: 32
[Phase 3] SRVs created successfully at t10-t14
```

**Note:** For simple test_cube.obj (no normals/UVs), only t10, t11, and t14 will be created.

**Validation:**
- Build succeeds ✅
- **Proof:** Build log shows no errors
- SRVs created without errors ✅
- **Proof:** Console shows "Creating ... SRV" messages with correct counts
- **Proof:** Application still renders correctly (old ClosestHit used)
- Buffer addresses are valid ✅
- **Proof:** D3D12 debug layer doesn't report invalid resource

**Rollback:** `git tag phase2a2-step3.2-srvs-created`

**Validation with PIX (Optional but Recommended):**

1. **Capture frame** after this step
2. **Inspect descriptor heap**:
   - Navigate to the descriptor heap in PIX
   - Verify t10, t11, t12 slots exist
   - Check resource addresses (should be non-null)
   - Verify `NumElements` matches console output
3. **Check buffer contents** (download to CPU):
   - t10 should show vertex data (positions, normals, UVs)
   - t11 should show index data (0, 1, 2, ... for cube)
   - t12 should show mesh descriptor (vbOffset=0, ibOffset=0, materialID=0)

**Debug if fails:**

```cpp
// Add after each CreateShaderResourceView
if (global_vertex_buffer.get() == nullptr) {
    std::cerr << "[ERROR] global_vertex_buffer is null!\n";
    throw std::runtime_error("Buffer not created");
}

// Verify buffer GPU address
D3D12_GPU_VIRTUAL_ADDRESS gpu_va = global_vertex_buffer->GetGPUVirtualAddress();
std::cout << "[DEBUG] globalVertices GPU address: 0x" << std::hex << gpu_va << std::dec << "\n";
if (gpu_va == 0) {
    throw std::runtime_error("Invalid buffer GPU address");
}
```

**Common Errors:**

| Error | Symptom | Fix |
|-------|---------|-----|
| **Descriptor offset wrong** | Application crashes or black screen | Recalculate offset_to_t10 (should be 2+3+1+textures.size()) |
| **StructureByteStride mismatch** | D3D12 validation error | Verify sizeof(Vertex)=32, sizeof(uint32_t)=4, sizeof(MeshDesc)=20 |
| **Buffer not created** | CreateSRV fails | Check Phase 2.2 - buffers must exist before creating SRVs |
| **NumElements = 0** | SRV creation fails | Scene loading bug - global buffers empty |

---

## Phase 4: Pipeline Switch (The Critical Step)

**Goal:** Switch from old ClosestHit to ClosestHit_GlobalBuffers

**Duration:** 5 minutes (if successful) to 60 minutes (if debugging needed)

**⚠️ WARNING:** This is the ONLY step that can break rendering. All previous phases were safe.

---

### Step 4.1: Update Pipeline to Use New Shader (5 min)

**File:** `backends/dxr/render_dxr.cpp`

**Location:** Find the ray tracing pipeline creation code (search for `add_hit_group` or `RTPipelineBuilder`)

**Expected location:** Around line 700-800

**Change (ONE LINE):**

```cpp
// BEFORE:
rt_pipeline_builder.add_hit_group(
    dxr::HitGroup(hg_name, D3D12_HIT_GROUP_TYPE_TRIANGLES, L"ClosestHit")
);

// AFTER:
rt_pipeline_builder.add_hit_group(
    dxr::HitGroup(hg_name, D3D12_HIT_GROUP_TYPE_TRIANGLES, L"ClosestHit_GlobalBuffers")
);
```

**Alternative syntax (if your code looks different):**
```cpp
// BEFORE:
.add_hit_group(L"HitGroup", L"ClosestHit", nullptr)

// AFTER:
.add_hit_group(L"HitGroup", L"ClosestHit_GlobalBuffers", nullptr)
```

**What this does:**
- Tells D3D12 to invoke `ClosestHit_GlobalBuffers` instead of `ClosestHit` when rays hit geometry
- Old shader (`ClosestHit`) still exists in compiled shader library (not deleted yet)
- **This is the switch** from space1 (per-geometry) to space0 (global buffers)

**Build and Run:**
```powershell
cmake --build build --config Debug
.\build\Debug\chameleonrt.exe dxr assets\models\test_cube.obj
```

---

### Step 4.2: Validation - Four Possible Outcomes

#### **Outcome A: ✅ SUCCESS - Cube Renders Correctly**

**Symptoms:**
- Application launches normally
- Cube appears on screen with shading
- Visual appearance matches baseline screenshot
- No console errors
- Stable for 100+ frames

**Verification:**
```powershell
# Take screenshot and compare to baseline
# Should be pixel-identical or very close (floating point differences)
```

**If this happens:**
- 🎉 **Phase 4 COMPLETE!**
- Tag the commit: `git tag phase2a2-step4.1-pipeline-switched`
- **Proceed to Phase 5 (Cleanup)**

**Console Output (Expected):**
```
Loaded mesh: test_cube.obj
  Vertices: 24, Triangles: 12
[] Creating global buffers...
  - Vertices: 24
  - Indices: 36
  - MeshDescs: 1
[] Global buffers created successfully
[] Creating SRVs for global buffers...
  Creating globalVertices SRV at t10...
    NumElements: 24, StructureByteStride: 32
  Creating globalIndices SRV at t11...
    NumElements: 36, StructureByteStride: 4
  Creating meshDescs SRV at t12...
    NumElements: 1, StructureByteStride: 20
[] SRVs created successfully at t10-t12
Frame time: 16.7ms
```

---

#### **Outcome B: ❌ FAILURE - CreateStateObject Error**

**Symptoms:**
```
ERROR: CreateStateObject failed with HRESULT 0x80070057 (E_INVALIDARG)
```

**What this means:**
- Ray tracing pipeline creation failed
- Most likely: Descriptor binding mismatch
- Shader expects resources that aren't bound

**Immediate Action:**
```powershell
# Revert immediately
git reset --hard phase2a2-step3.2-srvs-created
```

**Debugging Checklist:**

1. **Enable D3D12 Debug Layer:**
   ```cpp
   // Add at start of main() or device creation
   ID3D12Debug* debug_controller;
   if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
       debug_controller->EnableDebugLayer();
       
       // Enable GPU-based validation (more detailed)
       ID3D12Debug1* debug_controller1;
       if (SUCCEEDED(debug_controller->QueryInterface(IID_PPV_ARGS(&debug_controller1)))) {
           debug_controller1->SetEnableGPUBasedValidation(TRUE);
       }
   }
   ```

2. **Check Shader Compilation:**
   ```powershell
   # Manually compile ClosestHit_GlobalBuffers
   dxc -T lib_6_3 backends\dxr\render_dxr.hlsl -E ClosestHit_GlobalBuffers -Fo build\test_closesthit.dxil
   # Should succeed with no errors
   ```

3. **Verify Root Signature Compatibility:**
   - Global root signature must include space0 registers t10-t12
   - Check root signature creation code
   - Look for `D3D12_ROOT_PARAMETER` or `RootSignature::Builder`

4. **Common Causes:**
   - **Root signature doesn't declare t10-t12**: Add to global root signature
   - **Shader compiled but not exported**: Check shader library export list
   - **Hit group name mismatch**: Verify "ClosestHit_GlobalBuffers" matches shader annotation

**Fix Strategy:**
```cpp
// Verify root signature includes global buffers
// Look for root signature creation (around line 600-700)
dxr::RootSignature global_root_sig = 
    dxr::RootSignatureBuilder::global()
        // ... existing parameters ...
        .add_srv_table(3, 10, 0)  // t10-t12, space0 (ADD THIS)
        .create(device.Get());
```

---

#### **Outcome C: ❌ FAILURE - Black Screen (Application Runs)**

**Symptoms:**
- Application launches normally
- Window appears, but renders solid black
- No console errors
- No crashes

**What this means:**
- Shader is executing but returning incorrect data
- Most likely: Buffer indexing bug or data corruption

**Immediate Action:**
```powershell
# Don't revert yet - we can debug this
# Take PIX capture for analysis
```

**Debugging with PIX:**

1. **Capture failing frame:**
   - Press F12 in PIX while application running
   - Or use: `PIX.exe -captureframes 1 -programTarget chameleonrt.exe`

2. **Inspect DispatchRays call:**
   - Find `DispatchRays` in event list
   - Check "Shader Table" view
   - Verify `ClosestHit_GlobalBuffers` is in the hit group

3. **Check ClosestHit execution:**
   - Navigate to ClosestHit_GlobalBuffers invocation
   - Inspect `meshID = InstanceID()`:
     - **Expected for test_cube.obj:** meshID = 0 (single mesh)
     - If meshID is garbage: InstanceID() not working
   - Inspect `meshDescs[meshID]`:
     - **Expected:** `{vbOffset=0, ibOffset=0, materialID=0}`
     - If garbage: meshDescs buffer not bound or corrupt
   - Inspect `globalIndices[triIndex]`:
     - **Expected:** Indices 0-35 (for 12 triangles × 3)
     - If garbage: globalIndices buffer not bound or corrupt

4. **Verify Buffer Bindings:**
   - Check descriptor heap at t10, t11, t12
   - Should show non-null GPU addresses
   - Download buffer contents to verify data

**Debug Code (Add to ClosestHit_GlobalBuffers):**

```hlsl
// Temporarily simplify shader to test execution
[shader("closesthit")]
void ClosestHit_GlobalBuffers(inout HitInfo payload, Attributes attrib)
{
    // TEST 1: Just return red (shader invoked?)
    payload.color_dist = float4(1, 0, 0, 1);  // Red
    payload.normal = float4(0, 0, 1, 0);
    return;  // If this shows red, shader is executing
    
    // TEST 2: Check InstanceID
    uint meshID = InstanceID();
    if (meshID == 0) {
        payload.color_dist = float4(0, 1, 0, 1);  // Green if meshID=0
    } else {
        payload.color_dist = float4(1, 0, 0, 1);  // Red if wrong
    }
    return;
    
    // TEST 3: Check buffer access
    MeshDesc mesh = meshDescs[0];  // Hard-code index 0
    if (mesh.materialID == 0) {
        payload.color_dist = float4(0, 0, 1, 1);  // Blue if buffer readable
    } else {
        payload.color_dist = float4(1, 0, 0, 1);  // Red if garbage
    }
    return;
    
    // ... rest of shader (test incrementally) ...
}
```

**Common Causes:**

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| **Solid black** | Shader returning (0,0,0,0) | Add debug colors (see above) |
| **Shader not executing** | Hit group not invoked | Check SBT, verify TraceRay parameters |
| **Wrong meshID** | InstanceID() returning garbage | BLAS/TLAS instance setup bug |
| **Buffer access crash** | Descriptors not bound | Check Phase 3.2 - SRVs created? |
| **Correct color, wrong geometry** | Index calculation wrong | Check `triIndex = mesh.ibOffset + PrimitiveIndex() * 3` |

---

#### **Outcome D: ❌ FAILURE - Application Crashes**

**Symptoms:**
- Application launches, then crashes (immediate or within seconds)
- Windows error: "chameleonrt.exe has stopped working"
- Or GPU fault: "DXGI_ERROR_DEVICE_REMOVED"

**What this means:**
- GPU fault - shader accessed invalid memory
- Most likely: Null pointer or out-of-bounds access

**Immediate Action:**
```powershell
# Revert immediately
git reset --hard phase2a2-step3.2-srvs-created
```

**Debugging with PIX (Device Removed):**

1. **Enable GPU crash dumps:**
   ```cpp
   // Add when creating device
   D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
   device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
   
   // Enable after-math (NVIDIA) or similar
   ```

2. **Check D3D12 debug output:**
   ```
   D3D12 ERROR: [ EXECUTION ERROR #1234: INVALID_BUFFER_ACCESS_UNALIGNED ]
   ```

3. **Capture with PIX:**
   - May not complete if crash is immediate
   - Look for last successfully executed command
   - Check descriptor heap state before crash

**Common Causes:**

| Error | Likely Cause | Fix |
|-------|--------------|-----|
| **Device Removed (0x887A0006)** | Invalid buffer access | Verify SRVs created correctly in Phase 3.2 |
| **Access Violation** | Null descriptor | Check buffer creation in Phase 2.2 - buffers exist? |
| **Out of bounds** | Wrong NumElements in SRV | Verify SRV NumElements matches actual buffer size |
| **Unaligned access** | Wrong StructureByteStride | Verify sizeof(Vertex)=32, sizeof(uint32_t)=4, sizeof(MeshDesc)=20 |

**Diagnostic Code (C++):**

```cpp
// Add after SRV creation in Phase 3.2
// Download buffer and verify first few elements
{
    std::vector<Vertex> cpu_vertices(scene.global_vertices.size());
    // ... download globalVertices buffer to cpu_vertices ...
    
    std::cout << "[DEBUG] First vertex: (" 
              << cpu_vertices[0].position.x << ", "
              << cpu_vertices[0].position.y << ", "
              << cpu_vertices[0].position.z << ")\n";
    
    // For test_cube.obj, should be something like (-1, -1, -1)
    // If garbage (NaN, huge numbers), buffer upload failed
}
```

---

### Step 4.3: Rollback Decision Point (10 min)

**If debugging exceeds 1 hour without progress:**

1. **Revert the change:**
   ```powershell
   git reset --hard phase2a2-step3.2-srvs-created
   ```

2. **Document the failure:**
   - Create `ISSUES_ENCOUNTERED.md`
   - Include exact error message
   - Attach PIX capture (if available)
   - List what was tried

3. **Seek help or pivot:**
   - Post on Discord/forum with PIX capture
   - Consider alternative approach (Option B for BLAS?)
   - Take a break, revisit with fresh perspective

**Example ISSUES_ENCOUNTERED.md:**
```markdown
# Phase 4.1 Failure Report

**Date:** October 13, 2025  
**Symptom:** Black screen when using ClosestHit_GlobalBuffers  
**Error:** None (application runs normally)

## PIX Findings:
- Shader is executing (invocation count > 0)
- `InstanceID()` returns 0 (correct for test_cube.obj)
- `meshDescs[0].vbOffset` = 0 (correct)
- `globalIndices[0]` = <garbage data>

## Hypothesis:
Global index buffer not uploaded correctly or descriptor wrong.

## Tried:
1. Verified SRV creation - NumElements=36, stride=4 (correct)
2. Checked buffer creation - upload buffer allocated
3. ??? Need to verify CopyResource actually happened

## Next Steps:
- Add sync point after CopyResource to ensure upload completes
- Download globalIndices buffer to CPU and inspect
```

---

### Step 4.4: Success Path - Final Validation (15 min)

**If Outcome A (Success) occurred:**

1. **Extended Stability Test:**
   ```powershell
   # Let application run for 5+ minutes
   # Check frame times, memory usage
   # Verify no gradual degradation
   ```

2. **Test Complex Scene (Sponza):**
   ```powershell
   .\build\Debug\chameleonrt.exe dxr assets\models\sponza\sponza.obj
   # Should render correctly with global buffers
   ```

3. **Screenshot Comparison:**
   ```powershell
   # Compare to baseline (from before global buffer refactor)
   # Should be pixel-identical or <1% difference
   ```

4. **Performance Check:**
   - Frame times should be similar (±10%)
   - Global buffer indirection adds ~1-2% overhead (acceptable)
   - If much slower: indexing bug (accessing wrong memory)

5. **D3D12 Validation:**
   - No warnings/errors in debug output
   - No resource leaks
   - No "live object" messages on exit

**Success Checklist:**

- ✅ test_cube.obj renders identically to baseline
- ✅ Sponza renders identically to baseline
- ✅ Stable for 1000+ frames
- ✅ Frame times within 10% of baseline
- ✅ No D3D12 validation errors
- ✅ No memory leaks (check Task Manager over time)
- ✅ Console shows correct buffer sizes

**If ALL checks pass:**
- Tag the commit: `git tag phase2a2-step4-COMPLETE`
- **Proceed to Phase 5 (Cleanup)**

---

## Phase 4 Summary

**Risk Level:** ⚠️ HIGH (only risky phase)

**Blast Radius:** Limited to ClosestHit shader execution

**Rollback:** One-line revert (`git reset --hard phase2a2-step3.2`)

**Debug Strategy:** PIX capture → shader execution analysis → buffer verification

**Success Rate:** High if Phases 1-3 completed correctly (all proof points passed)

**Time Investment:** 5-60 minutes depending on outcome

---

## Phase 5: Cleanup (Only After Success)

**Goal:** Remove old code paths

### Step 5.1: Remove Old ClosestHit Shader

**File:** `backends/dxr/render_dxr.hlsl`

- Delete original `ClosestHit` function
- Rename `ClosestHit_GlobalBuffers` to `ClosestHit`

### Step 5.2: Remove space1 Declarations

**File:** `backends/dxr/render_dxr.hlsl`

- Delete all `register(*, space1)` declarations

### Step 5.3: Remove Local Root Signatures

**File:** `backends/dxr/render_dxr.cpp`

- Remove hitgroup local root signature code
- Simplify shader binding table

---

## Emergency Rollback Procedure

**If ANY step fails:**

1. **Immediate revert:**
   ```powershell
   git reset --hard phase2a2-step<X>.<Y>-<name>
   ```

2. **Analyze error:**
   - What was the exact error message?
   - Which step introduced it?
   - Was it shader compilation or runtime?

3. **Document:**
   - Add to `GlobalBufferRefactor/DXR/ISSUES_ENCOUNTERED.md`
   - Include full error message, PIX capture if needed

4. **Decide:**
   - Fix and retry same step?
   - Change approach?
   - Seek different strategy?

---

## Success Criteria

**Phase 1 Success:**
- ✅ Shader compiles with global buffer declarations
- ✅ Shader compiles with both ClosestHit versions

**Phase 2 Success:**
- ✅ Global buffers created in C++
- ✅ No crashes during buffer creation
- ✅ Console confirms buffer sizes

**Phase 3 Success:**
- ✅ Descriptor heap extended
- ✅ SRVs created for global buffers
- ✅ No D3D12 validation errors

**Phase 4 Success:**
- ✅ Application renders test_cube.obj correctly
- ✅ Screenshot matches baseline
- ✅ No CreateStateObject errors
- ✅ No black screen
- ✅ Stable for 100+ frames

**Phase 5 Success:**
- ✅ Old code removed
- ✅ Clean implementation
- ✅ Still renders correctly

---

## Timeline Estimate

**Conservative estimate: 4-6 hours**

- Phase 1 (Shader): 30-45 min
- Phase 2 (Buffers): 45-60 min
- Phase 3 (Descriptors): 45-60 min  
- Phase 4 (Pipeline): 60-90 min (CRITICAL - may need debugging)
- Phase 5 (Cleanup): 30 min

**If Phase 4 fails:** Add 2-4 hours for debugging or strategy pivot

---

## Risk Mitigation

**Risk:** CreateStateObject fails again  
**Mitigation:** 
- Shader tested in isolation first (Phase 1.4)
- Parallel implementation (old path still works)
- Immediate rollback capability

**Risk:** Wrong descriptor heap offsets  
**Mitigation:**
- Print descriptor indices during creation
- Use D3D12 debug layer with GPU validation
- Verify with PIX capture

**Risk:** Buffer data corruption  
**Mitigation:**
- Print buffer sizes after creation
- Verify data matches Scene global buffers
- Test with simple cube first (8 vertices, 36 indices)

---

## Next Steps

Ready to begin? Let's start with Phase 1, Step 1.1:

**Command:** Add Vertex and MeshDesc structs to `backends/dxr/render_dxr.hlsl`

Shall we proceed?
