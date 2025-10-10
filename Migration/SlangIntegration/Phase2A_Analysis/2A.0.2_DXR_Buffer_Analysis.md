# Task 2A.0.2: DXR Buffer Creation Analysis

**Date:** 2025-10-10  
**Status:** Complete  
**Deliverable:** Analysis of DXR backend per-geometry buffer architecture

---

## Executive Summary

The DXR backend uses a **per-geometry buffer architecture** with a **local root signature (space1)** to pass geometry-specific buffer addresses via shader records. Each geometry gets 4 separate GPU buffers (vertex, index, normal, UV) which are bound per-hit-group through the Shader Binding Table (SBT).

**Critical Findings:**
1. **Per-Geometry Buffers:** Each `Geometry` object creates 4 separate D3D12 buffers on the GPU
2. **Local Root Signature (space1):** Geometry buffers are bound via local root signature using GPU virtual addresses
3. **Shader Records:** Each hit group has a shader record containing buffer addresses + metadata
4. **BLAS Building:** Uses the same per-geometry buffers (vertex_buf, index_buf) for acceleration structure input
5. **Hit Group Explosion:** One hit group per geometry (not per mesh!) → `parameterized_meshes.size() * geometries_per_mesh` total hit groups

---

## RenderDXR Class Structure

### Member Variables (render_dxr.h)

**Buffer-Related Members:**
```cpp
class RenderDXR {
    // Scene-wide buffers (NOT per-geometry)
    dxr::Buffer view_param_buf;       // Camera parameters (space0, b0)
    dxr::Buffer instance_buf;         // D3D12_RAYTRACING_INSTANCE_DESC array (for TLAS)
    dxr::Buffer material_param_buf;   // Materials array (space0, t1)
    dxr::Buffer light_buf;            // Lights array (space0, t2)
    dxr::Buffer img_readback_buf;     // CPU readback buffer
    
    // Acceleration structures
    std::vector<dxr::BottomLevelBVH> meshes;  // One BLAS per Mesh (contains geometries)
    dxr::TopLevelBVH scene_bvh;               // TLAS (references BLAS instances)
    
    // Textures
    std::vector<dxr::Texture2D> textures;     // Texture array (space0, t3+)
    
    // Scene metadata (CPU-side)
    std::vector<ParameterizedMesh> parameterized_meshes;  // Mesh + material assignments
};
```

**Key Observation:** There are NO per-geometry buffer member variables in `RenderDXR`. The per-geometry buffers are stored inside each `dxr::Geometry` object within `dxr::BottomLevelBVH::geometries`.

---

## Per-Geometry Buffer Creation

### DXR Geometry Structure (dxr_utils.h)

```cpp
struct Geometry {
    Buffer vertex_buf;   // D3D12 buffer for vertex positions (vec3)
    Buffer index_buf;    // D3D12 buffer for triangle indices (uvec3)
    Buffer normal_buf;   // D3D12 buffer for vertex normals (vec3) - may be empty
    Buffer uv_buf;       // D3D12 buffer for texture coordinates (vec2) - may be empty
    D3D12_RAYTRACING_GEOMETRY_DESC desc;  // Geometry descriptor for BLAS building
};
```

**Each Geometry has 4 independent GPU buffers** stored as member variables.

---

## Buffer Creation Loop (render_dxr.cpp::set_scene)

### Pseudocode

```cpp
void RenderDXR::set_scene(const Scene &scene) {
    // For each Mesh in the scene
    for (const auto &mesh : scene.meshes) {
        std::vector<dxr::Geometry> geometries;
        
        // For each Geometry in the Mesh
        for (const auto &geom : mesh.geometries) {
            // ========== CREATE UPLOAD (STAGING) BUFFERS ==========
            // Upload buffers are in CPU-writable memory
            dxr::Buffer upload_verts = dxr::Buffer::upload(
                device.Get(),
                geom.vertices.size() * sizeof(glm::vec3),
                D3D12_RESOURCE_STATE_GENERIC_READ
            );
            dxr::Buffer upload_indices = dxr::Buffer::upload(
                device.Get(),
                geom.indices.size() * sizeof(glm::uvec3),
                D3D12_RESOURCE_STATE_GENERIC_READ
            );
            
            // Copy CPU data to upload buffers
            std::memcpy(upload_verts.map(), geom.vertices.data(), upload_verts.size());
            std::memcpy(upload_indices.map(), geom.indices.data(), upload_indices.size());
            upload_verts.unmap();
            upload_indices.unmap();
            
            // Optional: Upload normals if present
            dxr::Buffer upload_normals;
            if (!geom.normals.empty()) {
                upload_normals = dxr::Buffer::upload(...);
                std::memcpy(upload_normals.map(), geom.normals.data(), ...);
                upload_normals.unmap();
            }
            
            // Optional: Upload UVs if present
            dxr::Buffer upload_uvs;
            if (!geom.uvs.empty()) {
                upload_uvs = dxr::Buffer::upload(...);
                std::memcpy(upload_uvs.map(), geom.uvs.data(), ...);
                upload_uvs.unmap();
            }
            
            // ========== CREATE DEVICE (GPU) BUFFERS ==========
            // Device buffers are in GPU-local memory (VRAM)
            dxr::Buffer vertex_buf = dxr::Buffer::device(
                device.Get(),
                upload_verts.size(),
                D3D12_RESOURCE_STATE_COPY_DEST
            );
            dxr::Buffer index_buf = dxr::Buffer::device(
                device.Get(),
                upload_indices.size(),
                D3D12_RESOURCE_STATE_COPY_DEST
            );
            
            // Optional device buffers for normals/UVs
            dxr::Buffer normal_buf;
            if (!geom.normals.empty()) {
                normal_buf = dxr::Buffer::device(...);
            }
            dxr::Buffer uv_buf;
            if (!geom.uvs.empty()) {
                uv_buf = dxr::Buffer::device(...);
            }
            
            // ========== GPU COPY ==========
            // Copy from upload (staging) to device (VRAM)
            cmd_list->Reset(cmd_allocator.Get(), nullptr);
            cmd_list->CopyResource(vertex_buf.get(), upload_verts.get());
            cmd_list->CopyResource(index_buf.get(), upload_indices.get());
            if (!geom.uvs.empty()) {
                cmd_list->CopyResource(uv_buf.get(), upload_uvs.get());
            }
            if (!geom.normals.empty()) {
                cmd_list->CopyResource(normal_buf.get(), upload_normals.get());
            }
            
            // ========== RESOURCE BARRIERS ==========
            // Transition buffers to shader-readable state
            std::vector<D3D12_RESOURCE_BARRIER> barriers;
            barriers.push_back(barrier_transition(vertex_buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            barriers.push_back(barrier_transition(index_buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            if (!geom.uvs.empty()) {
                barriers.push_back(barrier_transition(uv_buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            }
            if (!geom.normals.empty()) {
                barriers.push_back(barrier_transition(normal_buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            }
            cmd_list->ResourceBarrier(barriers.size(), barriers.data());
            
            // ========== CREATE GEOMETRY OBJECT ==========
            // dxr::Geometry constructor stores buffer references and creates D3D12_RAYTRACING_GEOMETRY_DESC
            geometries.emplace_back(vertex_buf, index_buf, normal_buf, uv_buf);
            
            // Execute upload commands
            cmd_list->Close();
            cmd_queue->ExecuteCommandLists(1, &cmd_list);
            sync_gpu();  // Wait for upload to complete
        }
        
        // ========== BUILD BOTTOM-LEVEL BVH ==========
        // Create BottomLevelBVH from all geometries in this mesh
        meshes.emplace_back(geometries);  // Stores geometries with their buffers
        
        // Enqueue BLAS build
        cmd_list->Reset(cmd_allocator.Get(), nullptr);
        meshes.back().enqeue_build(device.Get(), cmd_list.Get());
        cmd_list->Close();
        cmd_queue->ExecuteCommandLists(1, &cmd_list);
        sync_gpu();
        
        // Enqueue BLAS compaction
        cmd_list->Reset(cmd_allocator.Get(), nullptr);
        meshes.back().enqueue_compaction(device.Get(), cmd_list.Get());
        cmd_list->Close();
        cmd_queue->ExecuteCommandLists(1, &cmd_list);
        sync_gpu();
        
        meshes.back().finalize();  // Release scratch buffers
    }
    
    // Continue with TLAS build, material/texture uploads...
}
```

---

## Per-Geometry Buffer Characteristics

### Buffer Sizes (Example: Simple Cube)

For a cube with 24 vertices (4 per face × 6 faces) and 12 triangles:

```
Geometry (Cube):
  vertex_buf:  24 vertices × 12 bytes (vec3) = 288 bytes
  index_buf:   12 triangles × 12 bytes (uvec3) = 144 bytes
  normal_buf:  24 normals × 12 bytes (vec3) = 288 bytes
  uv_buf:      24 UVs × 8 bytes (vec2) = 192 bytes
  
Total per geometry: ~912 bytes (+ D3D12 alignment/overhead)
```

**For Sponza (hypothetical):** 100 geometries × ~50KB average = **~5MB in per-geometry buffers**

---

## BLAS (Bottom-Level Acceleration Structure) Building

### BLAS Structure

```cpp
class BottomLevelBVH {
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geom_descs;  // Geometry descriptors for build
    std::vector<Geometry> geometries;  // Stores buffers (vertex_buf, index_buf, etc.)
    Buffer bvh;                        // Acceleration structure buffer
    Buffer scratch;                    // Temporary build scratch space
    // ... compaction info
};
```

**Key Point:** One `BottomLevelBVH` per `Mesh` (not per `Geometry`). Each BLAS contains multiple geometries.

### Geometry Descriptor Creation

When `dxr::Geometry` is constructed (dxr_utils.cpp, not shown but inferred):

```cpp
Geometry::Geometry(Buffer vertex_buf, Buffer index_buf, Buffer normal_buf, Buffer uv_buf, ...) {
    this->vertex_buf = vertex_buf;
    this->index_buf = index_buf;
    this->normal_buf = normal_buf;
    this->uv_buf = uv_buf;
    
    // Create D3D12_RAYTRACING_GEOMETRY_DESC for BLAS building
    desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    desc.Flags = geom_flags;  // e.g., D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
    
    // Vertex data (positions only, used by BLAS for spatial partitioning)
    desc.Triangles.VertexBuffer.StartAddress = vertex_buf->GetGPUVirtualAddress();
    desc.Triangles.VertexBuffer.StrideInBytes = sizeof(glm::vec3);  // 12 bytes
    desc.Triangles.VertexCount = vertex_buf.size() / sizeof(glm::vec3);
    desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    
    // Index data
    desc.Triangles.IndexBuffer = index_buf->GetGPUVirtualAddress();
    desc.Triangles.IndexCount = (index_buf.size() / sizeof(glm::uvec3)) * 3;
    desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    
    // Transform (identity for now)
    desc.Triangles.Transform3x4 = 0;
}
```

**BLAS Build Process:**
1. Collect `D3D12_RAYTRACING_GEOMETRY_DESC` from all geometries in the mesh
2. Call `ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure()` with:
   - Input: Array of geometry descriptors (references vertex_buf, index_buf GPU addresses)
   - Output: BLAS buffer (compact spatial hierarchy)
   - Scratch: Temporary buffer for build algorithm
3. Optionally compact BLAS to reduce memory usage
4. Finalize: Release scratch buffer, keep BLAS buffer

**Important:** BLAS building **reads vertex_buf and index_buf** to construct spatial hierarchy, but does NOT read normal_buf or uv_buf (those are for shading only).

---

## TLAS (Top-Level Acceleration Structure) Building

### Instance Buffer Creation (render_dxr.cpp lines 282-347)

```cpp
// Compute SBT offsets for each parameterized mesh
std::vector<uint32_t> parameterized_mesh_sbt_offsets;
uint32_t offset = 0;
for (const auto &pm : parameterized_meshes) {
    parameterized_mesh_sbt_offsets.push_back(offset);
    offset += meshes[pm.mesh_id].geometries.size();  // Increment by geometry count
}

// Create upload buffer for instance descriptors
auto upload_instance_buf = dxr::Buffer::upload(
    device.Get(),
    align_to(scene.instances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), ...),
    D3D12_RESOURCE_STATE_GENERIC_READ
);

// Fill instance buffer
D3D12_RAYTRACING_INSTANCE_DESC *buf = static_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(upload_instance_buf.map());

for (size_t i = 0; i < scene.instances.size(); ++i) {
    const auto &inst = scene.instances[i];
    
    buf[i].InstanceID = i;  // Used in shader as InstanceID()
    
    // CRITICAL: Maps instance to SBT hit group range
    buf[i].InstanceContributionToHitGroupIndex = parameterized_mesh_sbt_offsets[inst.parameterized_mesh_id];
    
    buf[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
    
    // Reference to BLAS (mesh)
    buf[i].AccelerationStructure = meshes[parameterized_meshes[inst.parameterized_mesh_id].mesh_id]->GetGPUVirtualAddress();
    
    buf[i].InstanceMask = 0xff;
    
    // Transform matrix (world transform)
    // Note: D3D matrices are row-major, glm is column-major
    const glm::mat4 m = glm::transpose(inst.transform);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            buf[i].Transform[r][c] = m[r][c];
        }
    }
}
upload_instance_buf.unmap();

// Copy to device buffer
instance_buf = dxr::Buffer::device(...);
cmd_list->CopyResource(instance_buf.get(), upload_instance_buf.get());
// Barrier...

// Build TLAS
scene_bvh = dxr::TopLevelBVH(instance_buf, scene.instances);
cmd_list->Reset(...);
scene_bvh.enqeue_build(device.Get(), cmd_list.Get());
cmd_list->Close();
cmd_queue->ExecuteCommandLists(...);
sync_gpu();
scene_bvh.finalize();
```

**TLAS Structure:**
- Input: `instance_buf` containing array of `D3D12_RAYTRACING_INSTANCE_DESC`
- Each instance references:
  - BLAS (via GPU address)
  - Transform matrix (3×4)
  - SBT hit group offset (for shader record lookup)
  - Instance ID (passed to shader)

**Critical Insight:** `InstanceContributionToHitGroupIndex` maps each instance to a **range of hit groups** in the SBT. For an instance using a mesh with N geometries, it maps to SBT entries `[offset, offset+N)`.

---

## Root Signature Architecture

### Global Root Signature (space0)

**File:** render_dxr.cpp lines 701-702

```cpp
dxr::RootSignature global_root_sig = 
    dxr::RootSignatureBuilder::global().create(device.Get());
```

**Note:** This is an **empty** global root signature. All scene-wide resources are bound via descriptor heaps in the **local** root signature for the ray gen shader.

**Why empty?** DXR allows resources to be bound via local root signatures (per shader), avoiding the need for a shared global signature.

---

### Local Root Signature: Ray Gen Shader (space0)

**File:** render_dxr.cpp lines 705-711

```cpp
dxr::RootSignature raygen_root_sig =
    dxr::RootSignatureBuilder::local()
        .add_constants("SceneParams", 1, 1, 0)  // b1, space0 (num_lights)
        .add_desc_heap("cbv_srv_uav_heap", raygen_desc_heap)  // UAVs, SRVs, CBVs
        .add_desc_heap("sampler_heap", raygen_sampler_heap)   // Samplers
        .create(device.Get());
```

**Ray Gen Descriptor Heap Contents (raygen_desc_heap):**

From `build_shader_resource_heap()` (lines 776+):
```cpp
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_uav_range(2, 0, 0)  // u0-u1: output, accum_buffer (or u0-u2 with ray_stats)
    .add_srv_range(3, 0, 0)  // t0-t2: scene (TLAS), material_params, lights
    .add_cbv_range(1, 0, 0)  // b0: ViewParams
    .add_srv_range(textures.size(), 3, 0)  // t3+: textures array
    .create(...);
```

**Ray Gen Resources (space0):**
- `u0` - output (render target UAV)
- `u1` - accum_buffer (accumulation UAV)
- `u2` - ray_stats (optional, if REPORT_RAY_STATS)
- `t0` - scene (TLAS SRV)
- `t1` - material_params (StructuredBuffer<MaterialParams>)
- `t2` - lights (StructuredBuffer<QuadLight>)
- `t3+` - textures[] (Texture2D array)
- `b0` - ViewParams (camera parameters)
- `b1` - SceneParams (num_lights)
- `s0` - tex_sampler

---

### Local Root Signature: Hit Group (space1)

**File:** render_dxr.cpp lines 713-720

```cpp
dxr::RootSignature hitgroup_root_sig = dxr::RootSignatureBuilder::local()
    .add_srv("vertex_buf", 0, 1)     // t0, space1
    .add_srv("index_buf", 1, 1)      // t1, space1
    .add_srv("normal_buf", 2, 1)     // t2, space1
    .add_srv("uv_buf", 3, 1)         // t3, space1
    .add_constants("MeshData", 0, 3, 1)  // b0, space1 (3 uint32_t values)
    .create(device.Get());
```

**Hit Group Resources (space1):**
- `t0, space1` - vertex_buf (StructuredBuffer<float3>)
- `t1, space1` - index_buf (StructuredBuffer<uint3>)
- `t2, space1` - normal_buf (StructuredBuffer<float3>)
- `t3, space1` - uv_buf (StructuredBuffer<float2>)
- `b0, space1` - MeshData (3 uint32_t: num_normals, num_uvs, material_id)

**Critical:** These are bound **per hit group** via shader records, not globally. Each geometry gets its own shader record with different buffer addresses.

---

## Shader Binding Table (SBT) Structure

### Hit Group Creation (render_dxr.cpp lines 754-771)

```cpp
std::vector<std::wstring> hg_names;
for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
    const auto &pm = parameterized_meshes[i];
    for (size_t j = 0; j < meshes[pm.mesh_id].geometries.size(); ++j) {
        const std::wstring hg_name = 
            L"HitGroup_param_mesh" + std::to_wstring(i) + L"_geom" + std::to_wstring(j);
        hg_names.push_back(hg_name);
        
        rt_pipeline_builder.add_hit_group({
            dxr::HitGroup(hg_name, D3D12_HIT_GROUP_TYPE_TRIANGLES, L"ClosestHit")
        });
    }
}
rt_pipeline_builder.set_shader_root_sig(hg_names, hitgroup_root_sig);
```

**Hit Group Naming:**
- `HitGroup_param_mesh0_geom0` → ParameterizedMesh 0, Geometry 0
- `HitGroup_param_mesh0_geom1` → ParameterizedMesh 0, Geometry 1
- `HitGroup_param_mesh1_geom0` → ParameterizedMesh 1, Geometry 0
- ... etc.

**Total Hit Groups:** Sum of `meshes[pm.mesh_id].geometries.size()` across all parameterized meshes.

**Example (Sponza with 50 parameterized meshes, avg 2 geometries each):**
- Total hit groups: 50 × 2 = **100 hit groups**
- Each hit group has a unique shader record in the SBT

---

### Shader Record Population (render_dxr.cpp lines 788-862)

```cpp
void RenderDXR::build_shader_binding_table() {
    rt_pipeline.map_shader_table();
    
    // Ray Gen shader record (one entry)
    {
        uint8_t *map = rt_pipeline.shader_record(L"RayGen");
        const dxr::RootSignature *sig = rt_pipeline.shader_signature(L"RayGen");
        
        // Write SceneParams constant (num_lights)
        const uint32_t num_lights = light_buf.size() / sizeof(QuadLight);
        std::memcpy(map + sig->offset("SceneParams"), &num_lights, sizeof(uint32_t));
        
        // Write descriptor heap handles
        D3D12_GPU_DESCRIPTOR_HANDLE desc_heap_handle = raygen_desc_heap->GetGPUDescriptorHandleForHeapStart();
        std::memcpy(map + sig->offset("cbv_srv_uav_heap"), &desc_heap_handle, sizeof(...));
        
        desc_heap_handle = raygen_sampler_heap->GetGPUDescriptorHandleForHeapStart();
        std::memcpy(map + sig->offset("sampler_heap"), &desc_heap_handle, sizeof(...));
    }
    
    // Hit group shader records (one per geometry)
    for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
        const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < meshes[pm.mesh_id].geometries.size(); ++j) {
            const std::wstring hg_name = 
                L"HitGroup_param_mesh" + std::to_wstring(i) + L"_geom" + std::to_wstring(j);
            
            auto &geom = meshes[pm.mesh_id].geometries[j];
            
            uint8_t *map = rt_pipeline.shader_record(hg_name);
            const dxr::RootSignature *sig = rt_pipeline.shader_signature(hg_name);
            
            // Write vertex buffer GPU address (t0, space1)
            D3D12_GPU_VIRTUAL_ADDRESS gpu_handle = geom.vertex_buf->GetGPUVirtualAddress();
            std::memcpy(map + sig->offset("vertex_buf"), &gpu_handle, sizeof(...));
            
            // Write index buffer GPU address (t1, space1)
            gpu_handle = geom.index_buf->GetGPUVirtualAddress();
            std::memcpy(map + sig->offset("index_buf"), &gpu_handle, sizeof(...));
            
            // Write normal buffer GPU address (t2, space1) - may be null
            if (geom.normal_buf.size() != 0) {
                gpu_handle = geom.normal_buf->GetGPUVirtualAddress();
            } else {
                gpu_handle = 0;
            }
            std::memcpy(map + sig->offset("normal_buf"), &gpu_handle, sizeof(...));
            
            // Write UV buffer GPU address (t3, space1) - may be null
            if (geom.uv_buf.size() != 0) {
                gpu_handle = geom.uv_buf->GetGPUVirtualAddress();
            } else {
                gpu_handle = 0;
            }
            std::memcpy(map + sig->offset("uv_buf"), &gpu_handle, sizeof(...));
            
            // Write MeshData constant (b0, space1)
            const std::array<uint32_t, 3> mesh_data = {
                uint32_t(geom.normal_buf.size() / sizeof(glm::vec3)),  // num_normals
                uint32_t(geom.uv_buf.size() / sizeof(glm::vec2)),      // num_uvs
                pm.material_ids[j]                                      // material_id
            };
            std::memcpy(map + sig->offset("MeshData"), mesh_data.data(), mesh_data.size() * sizeof(uint32_t));
        }
    }
    
    rt_pipeline.unmap_shader_table();
    
    // Upload SBT to GPU
    cmd_list->Reset(...);
    rt_pipeline.upload_shader_table(cmd_list.Get());
    cmd_list->Close();
    cmd_queue->ExecuteCommandLists(...);
    sync_gpu();
}
```

**Shader Record Layout (Hit Group):**

```
Shader Identifier (32 bytes, set by D3D12)
─────────────────────────────────────────
Root Signature Parameters:
  [offset 0]  vertex_buf (GPU VA, 8 bytes)
  [offset 8]  index_buf (GPU VA, 8 bytes)
  [offset 16] normal_buf (GPU VA, 8 bytes)
  [offset 24] uv_buf (GPU VA, 8 bytes)
  [offset 32] MeshData (3× uint32_t, 12 bytes):
              - num_normals (uint32_t)
              - num_uvs (uint32_t)
              - material_id (uint32_t)
─────────────────────────────────────────
Total: 32 (identifier) + 44 (params) = 76 bytes (aligned)
```

**SBT Memory Layout:**

```
SBT Buffer:
┌─────────────────────────────────────────────────────────────┐
│ Ray Gen Shader Record                                       │
│   - Shader identifier                                       │
│   - SceneParams (num_lights)                                │
│   - cbv_srv_uav_heap descriptor handle                      │
│   - sampler_heap descriptor handle                          │
├─────────────────────────────────────────────────────────────┤
│ Miss Shader Record (index 0)                                │
├─────────────────────────────────────────────────────────────┤
│ Miss Shader Record (index 1 - shadow)                       │
├─────────────────────────────────────────────────────────────┤
│ Hit Group 0: HitGroup_param_mesh0_geom0                     │
│   - vertex_buf GPU address (geom 0 vertex buffer)           │
│   - index_buf GPU address (geom 0 index buffer)             │
│   - normal_buf GPU address (geom 0 normal buffer)           │
│   - uv_buf GPU address (geom 0 UV buffer)                   │
│   - MeshData (num_normals, num_uvs, material_id)            │
├─────────────────────────────────────────────────────────────┤
│ Hit Group 1: HitGroup_param_mesh0_geom1                     │
│   - vertex_buf GPU address (geom 1 vertex buffer)           │
│   - ... (different buffers than geom 0)                     │
├─────────────────────────────────────────────────────────────┤
│ ... (one record per geometry)                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Shader Resource Binding (HLSL)

### Space 0 (Global Resources - Ray Gen)

**File:** render_dxr.hlsl lines 25-61

```hlsl
// Output UAVs
RWTexture2D<float4> output : register(u0);
RWTexture2D<float4> accum_buffer : register(u1);
#ifdef REPORT_RAY_STATS
RWTexture2D<uint> ray_stats : register(u2);
#endif

// View parameters (camera)
cbuffer ViewParams : register(b0) {
    float4 cam_pos;
    float4 cam_du;
    float4 cam_dv;
    float4 cam_dir_top_left;
    uint32_t frame_id;
    uint32_t samples_per_pixel;
}

// Scene parameters
cbuffer SceneParams : register(b1) {
    uint32_t num_lights;
};

// Scene resources
RaytracingAccelerationStructure scene : register(t0);
StructuredBuffer<MaterialParams> material_params : register(t1);
StructuredBuffer<QuadLight> lights : register(t2);
Texture2D textures[] : register(t3);  // Unbounded array
SamplerState tex_sampler : register(s0);
```

**Accessible to:** Ray Gen shader, Miss shaders, Closest Hit shader (via global scope)

---

### Space 1 (Local Resources - Hit Group)

**File:** render_dxr.hlsl lines 280-289

```hlsl
// Per-mesh parameters for the closest hit
StructuredBuffer<float3> vertices : register(t0, space1);
StructuredBuffer<uint3> indices : register(t1, space1);
StructuredBuffer<float3> normals : register(t2, space1);
StructuredBuffer<float2> uvs : register(t3, space1);

cbuffer MeshData : register(b0, space1) {
    uint32_t num_normals;
    uint32_t num_uvs;
    uint32_t material_id;
}
```

**Accessible to:** Closest Hit shader only  
**Bound via:** Shader records in SBT (different addresses per geometry)

---

### Closest Hit Shader

**File:** render_dxr.hlsl lines 292-329

```hlsl
[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) {
    // Fetch triangle indices from index buffer (space1)
    uint3 idx = indices[NonUniformResourceIndex(PrimitiveIndex())];
    
    // Fetch triangle vertices from vertex buffer (space1)
    float3 va = vertices[NonUniformResourceIndex(idx.x)];
    float3 vb = vertices[NonUniformResourceIndex(idx.y)];
    float3 vc = vertices[NonUniformResourceIndex(idx.z)];
    
    // Compute geometric normal
    float3 ng = normalize(cross(vb - va, vc - va));
    
    float3 n = ng;
    // Note: Shading normals currently disabled (#if 0)
    
    // Interpolate UVs if available
    float2 uv = float2(0, 0);
    if (num_uvs > 0) {
        float2 uva = uvs[NonUniformResourceIndex(idx.x)];
        float2 uvb = uvs[NonUniformResourceIndex(idx.y)];
        float2 uvc = uvs[NonUniformResourceIndex(idx.z)];
        uv = (1.f - attrib.bary.x - attrib.bary.y) * uva
            + attrib.bary.x * uvb + attrib.bary.y * uvc;
    }
    
    // Write output (UVs + material_id)
    payload.color_dist = float4(uv, 0, RayTCurrent());
    float3x3 inv_transp = float3x3(WorldToObject4x3()[0], WorldToObject4x3()[1], WorldToObject4x3()[2]);
    payload.normal = float4(normalize(mul(inv_transp, n)), material_id);
}
```

**Key Operations:**
1. `PrimitiveIndex()` → Triangle index within geometry
2. `indices[PrimitiveIndex()]` → Fetch triangle's vertex indices (from space1 index buffer)
3. `vertices[idx.x/y/z]` → Fetch vertex positions (from space1 vertex buffer)
4. Barycentric interpolation for UVs
5. Use `material_id` from MeshData constant (space1)

**Critical:** The shader uses `space1` resources which are **different for each geometry** due to per-geometry shader records.

---

## Data Flow Diagram

```
┌──────────────────────────────────────────────────────────────────────────┐
│                     SCENE LOADING (CPU → GPU)                            │
└──────────────────────────────────────────────────────────────────────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │   Scene::meshes           │
                    │   (CPU data structures)   │
                    └─────────────┬─────────────┘
                                  │
              ┌───────────────────┼───────────────────┐
              │                   │                   │
         Mesh 0                Mesh 1             Mesh N
      (M geometries)        (K geometries)     (P geometries)
              │                   │                   │
    ┌─────────┴─────────┐         │                   │
    │                   │         │                   │
Geometry 0          Geometry 1  Geometry K        Geometry P
    │                   │         │                   │
    ▼                   ▼         ▼                   ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                   PER-GEOMETRY GPU BUFFER CREATION                      │
│  For each geometry:                                                     │
│    1. Create upload buffers (CPU-writable)                              │
│    2. memcpy CPU data → upload buffers                                  │
│    3. Create device buffers (GPU VRAM)                                  │
│    4. CopyResource (upload → device)                                    │
│    5. Barrier (COPY_DEST → NON_PIXEL_SHADER_RESOURCE)                   │
│                                                                          │
│  Result: dxr::Geometry { vertex_buf, index_buf, normal_buf, uv_buf }   │
└─────────────────────────┬───────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    BLAS (Bottom-Level BVH) BUILDING                     │
│  For each mesh:                                                         │
│    1. Collect D3D12_RAYTRACING_GEOMETRY_DESC from all geometries       │
│       - References vertex_buf, index_buf GPU addresses                  │
│    2. BuildRaytracingAccelerationStructure()                            │
│       - Input: Geometry descs (vertex/index buffers)                    │
│       - Output: BLAS buffer (spatial hierarchy)                         │
│    3. Optional compaction                                               │
│    4. Finalize (release scratch buffers)                                │
│                                                                          │
│  Result: dxr::BottomLevelBVH { bvh, geometries[] }                     │
└─────────────────────────┬───────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    TLAS (Top-Level BVH) BUILDING                        │
│  1. Compute SBT offsets per parameterized mesh                          │
│  2. Create D3D12_RAYTRACING_INSTANCE_DESC array:                        │
│     - InstanceID (shader-visible)                                       │
│     - InstanceContributionToHitGroupIndex (SBT offset)                  │
│     - AccelerationStructure (BLAS GPU address)                          │
│     - Transform (3×4 matrix)                                            │
│  3. Upload instance buffer to GPU                                       │
│  4. BuildRaytracingAccelerationStructure()                              │
│                                                                          │
│  Result: dxr::TopLevelBVH { bvh, instance_buf }                        │
└─────────────────────────┬───────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                  SHADER BINDING TABLE (SBT) CREATION                    │
│  Ray Gen Record:                                                        │
│    - SceneParams (num_lights)                                           │
│    - Descriptor heap handles (space0 resources)                         │
│                                                                          │
│  Hit Group Records (one per geometry):                                  │
│    - vertex_buf GPU address (space1, t0)                                │
│    - index_buf GPU address (space1, t1)                                 │
│    - normal_buf GPU address (space1, t2)                                │
│    - uv_buf GPU address (space1, t3)                                    │
│    - MeshData: num_normals, num_uvs, material_id (space1, b0)          │
│                                                                          │
│  Total records: 1 (raygen) + 2 (miss) + Σ(geometries per mesh)         │
└─────────────────────────┬───────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        RAY TRACING EXECUTION                            │
│  1. DispatchRays()                                                      │
│  2. Ray Gen shader:                                                     │
│     - Access space0 resources (TLAS, materials, lights, textures)       │
│     - TraceRay()                                                        │
│  3. Ray traversal:                                                      │
│     - Traverse TLAS → Find instance                                     │
│     - Traverse BLAS → Find geometry                                     │
│     - Compute hit point                                                 │
│  4. Closest Hit shader:                                                 │
│     - DXR computes: InstanceContributionToHitGroupIndex + GeometryIndex │
│     - Looks up shader record in SBT                                     │
│     - Binds space1 resources from shader record                         │
│     - Executes ClosestHit() with geometry-specific buffers              │
│     - Access vertices[idx], indices[idx] (space1, per-geometry)         │
│     - Access material_id (space1, per-geometry)                         │
│  5. Return to Ray Gen                                                   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Key Findings for Phase 2A Refactor

### 1. Per-Geometry Buffer Explosion

**Current:**
- Sponza example: 100 geometries × 4 buffers = **400 separate D3D12 buffers**
- Each buffer has D3D12 overhead (resource descriptor, memory alignment)
- High SBT memory usage (100 shader records × ~76 bytes = ~7.6KB)

**Refactor Goal:**
- 3 global buffers total (vertex, index, mesh_desc)
- Single shader record for all geometries (or no local root signature at all)

---

### 2. Local Root Signature (space1) Must Be Removed

**Current:** Hit group local root signature binds per-geometry buffers via GPU virtual addresses

**Refactor Strategy:**
- Move all resources to space0 (global)
- Bind global buffers once in descriptor heap
- Use indirection in shader: `meshDescs[GeometryIndex()]` → offset into global buffers

---

### 3. Shader Record Complexity

**Current:** 
- One hit group per geometry (not per mesh!)
- Each hit group has unique shader record with buffer addresses
- build_shader_binding_table() writes ~100 shader records

**Refactor Strategy:**
- Single hit group for all geometries (use same ClosestHit shader)
- Minimal shader record (no per-geometry data)
- Use `InstanceID()` and `GeometryIndex()` built-ins for indirection

---

### 4. BLAS Building Uses Same Buffers

**Current:** BLAS build references `vertex_buf` and `index_buf` from each `dxr::Geometry`

**Refactor Consideration:**
- BLAS build will still need per-geometry vertex/index data
- **Option A:** Keep temporary per-geometry buffers for BLAS build, discard after
- **Option B:** Use global buffers with offsets for BLAS build (D3D12 supports offset in geometry desc)

**Recommended:** Option A during Phase 2A (simpler), Option B for future optimization

---

### 5. InstanceContributionToHitGroupIndex Calculation

**Current:**
```cpp
buf[i].InstanceContributionToHitGroupIndex = parameterized_mesh_sbt_offsets[inst.parameterized_mesh_id];
```

This maps instance → first hit group for that mesh's geometries.

**Refactor:**
- With single hit group, set `InstanceContributionToHitGroupIndex = 0` for all instances
- Use `InstanceID()` in shader to look up instance data
- Use `GeometryIndex()` to compute which mesh descriptor to use

---

### 6. Material Assignment

**Current:** `material_id` stored in MeshData constant per geometry (space1)

**Refactor:** Store `materialID` in `MeshDesc` structure, index via `GeometryIndex()`

---

## Recommendations for Phase 2A.2 (DXR Refactor)

Based on this analysis:

1. **Create Global Buffers:**
   - `global_vertex_buffer` (StructuredBuffer<Vertex>, space0)
   - `global_index_buffer` (StructuredBuffer<uint>, space0)
   - `mesh_desc_buffer` (StructuredBuffer<MeshDesc>, space0)

2. **Update Descriptor Heap:**
   - Add 3 SRVs to raygen_desc_heap (e.g., t10, t11, t12 in space0)

3. **Remove Local Root Signature:**
   - Delete `hitgroup_root_sig` 
   - Remove `.set_shader_root_sig(hg_names, hitgroup_root_sig)` call
   - Simplify hit group creation (single hit group, not per-geometry)

4. **Simplify SBT:**
   - Remove loop over geometries in `build_shader_binding_table()`
   - Create single hit group record (minimal or empty)

5. **Update HLSL:**
   - Remove `space1` declarations
   - Add global buffer declarations (space0)
   - Update ClosestHit shader with indirection logic

6. **BLAS Building Strategy (CRITICAL PAUSE POINT):**
   - Analyze whether to keep temporary per-geometry buffers for BLAS build
   - Or use global buffers with offset support

---

## Files to Modify in Phase 2A.2

- `backends/dxr/render_dxr.h` - Add global buffer member variables
- `backends/dxr/render_dxr.cpp` - Modify set_scene(), build_raytracing_pipeline(), build_shader_binding_table(), build_shader_resource_heap()
- `backends/dxr/render_dxr.hlsl` - Remove space1, add global buffers (space0), update ClosestHit()

---

**Analysis Complete:** DXR backend per-geometry buffer architecture fully documented. Ready to proceed to Phase 2A.0.3 (Vulkan analysis) or begin Phase 2A.2 implementation.
