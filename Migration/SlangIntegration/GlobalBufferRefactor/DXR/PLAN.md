# Phase 2A.2: DXR Backend Refactor Plan

**Goal:** Update DXR backend to use global buffers, validate rendering  
**Status:** 🔄 IN PROGRESS  
**Strategy:** Option A - Temporary per-geometry buffers for BLAS, global buffers for shaders  
**Date:** October 10-11, 2025

---

## Overview

Update the DXR backend to:
1. Use global buffers (t10-t12 in space0) for shader access
2. Remove local root signatures (space1)
3. Simplify shader binding table (no per-geometry data)
4. Maintain BLAS building with existing approach (temporary buffers during load)

**See [BLAS_STRATEGY.md](BLAS_STRATEGY.md) for detailed strategy decision (Option A selected)**

---

## Shader Changes

**File:** `backends/dxr/render_dxr.hlsl`

### Add New Structs

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
```

### Add Global Buffer Bindings (space0)

```hlsl
// Add after existing space0 resources
StructuredBuffer<Vertex> globalVertices : register(t10, space0);
StructuredBuffer<uint> globalIndices : register(t11, space0);    // uint not uint3
StructuredBuffer<MeshDesc> meshDescs : register(t12, space0);
```

### Remove space1 Resources

```hlsl
// DELETE THESE:
// StructuredBuffer<float3> vertices : register(t0, space1);
// StructuredBuffer<uint3> indices : register(t1, space1);
// StructuredBuffer<float3> normals : register(t2, space1);
// StructuredBuffer<float2> uvs : register(t3, space1);
// cbuffer MeshData : register(b0, space1) { ... }
```

### Update ClosestHit Shader

```hlsl
[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    // Get which geometry we hit
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
    
    // Continue with shading...
}
```

---

## C++ Changes

**File:** `backends/dxr/render_dxr.cpp`

### Add Member Variables

```cpp
// In render_dxr.h
class RenderDXR {
    // Existing members...
    
    // NEW: Global buffers
    dxr::Buffer global_vertex_buffer;
    dxr::Buffer global_index_buffer;
    dxr::Buffer mesh_desc_buffer;
};
```

### Modify `set_scene()` Function

**Create global buffers:**

```cpp
void RenderDXR::set_scene(const Scene &scene)
{
    frame_id = 0;
    samples_per_pixel = scene.samples_per_pixel;
    
    // 1. Create Global Vertex Buffer
    dxr::Buffer upload_vertices = dxr::Buffer::upload(
        device.Get(),
        scene.global_vertices.size() * sizeof(Vertex),
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    std::memcpy(upload_vertices.map(), scene.global_vertices.data(), upload_vertices.size());
    upload_vertices.unmap();
    
    global_vertex_buffer = dxr::Buffer::device(
        device.Get(),
        upload_vertices.size(),
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    
    // 2. Create Global Index Buffer
    dxr::Buffer upload_indices = dxr::Buffer::upload(
        device.Get(),
        scene.global_indices.size() * sizeof(uint32_t),
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    std::memcpy(upload_indices.map(), scene.global_indices.data(), upload_indices.size());
    upload_indices.unmap();
    
    global_index_buffer = dxr::Buffer::device(
        device.Get(),
        upload_indices.size(),
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    
    // 3. Create MeshDesc Buffer
    dxr::Buffer upload_mesh_descs = dxr::Buffer::upload(
        device.Get(),
        scene.mesh_descriptors.size() * sizeof(MeshDesc),
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    std::memcpy(upload_mesh_descs.map(), scene.mesh_descriptors.data(), upload_mesh_descs.size());
    upload_mesh_descs.unmap();
    
    mesh_desc_buffer = dxr::Buffer::device(
        device.Get(),
        upload_mesh_descs.size(),
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    
    // Upload to GPU
    CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
    cmd_list->CopyResource(global_vertex_buffer.get(), upload_vertices.get());
    cmd_list->CopyResource(global_index_buffer.get(), upload_indices.get());
    cmd_list->CopyResource(mesh_desc_buffer.get(), upload_mesh_descs.get());
    
    // Transition to shader-visible state
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {
        barrier_transition(global_vertex_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        barrier_transition(global_index_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        barrier_transition(mesh_desc_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    cmd_list->ResourceBarrier(barriers.size(), barriers.data());
    
    // Continue with BLAS/TLAS build using temporary per-geometry buffers
    // (See BLAS_STRATEGY.md for details)
}
```

### Update Descriptor Heap

**Add SRVs for global buffers:**

```cpp
// In build_descriptor_heap() or similar
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_uav_range(2, 0, 0)      // Existing UAVs
    .add_srv_range(3, 0, 0)      // Existing SRVs (TLAS, etc.)
    .add_cbv_range(1, 0, 0)      // Existing CBV
    .add_srv_range(textures.size(), 3, 0)  // Existing texture SRVs
    .add_srv_range(3, 10, 0)     // NEW: t10-t12 (global buffers)
    .create(device.Get());

// Create SRVs for global buffers at t10, t11, t12
// ...
```

### Remove Local Root Signature

**Delete:**
- Hit group local root signature setup
- Shader binding table per-geometry shader record data
- space1 descriptor creation

**Keep:**
- Global root signature only
- Simplified shader binding table (just shader identifiers)

---

## Tasks

### 2A.2.1: Update HLSL Shader
- Add Vertex and MeshDesc structs
- Add global buffer declarations (t10-t12, space0)
- Remove space1 declarations
- Update ClosestHit with indirection logic

### 2A.2.2: Update C++ Buffer Creation
- Add global buffer member variables
- Modify `set_scene()` to create global buffers
- Keep temporary per-geometry buffer creation for BLAS

### 2A.2.3: Update Descriptor Heap
- Add SRV range for t10-t12
- Create SRVs for global buffers
- Bind to descriptor heap

### 2A.2.4: Update Root Signature
- Remove local root signature setup
- Ensure global root signature includes t10-t12

### 2A.2.5: Simplify Shader Binding Table
- Remove per-geometry shader record data
- Keep only shader identifiers

### 2A.2.6: Test and Validate
- Build and run with test_cube.obj
- Compare screenshot to baseline
- Build and run with Sponza
- Compare screenshot to baseline

---

## Validation Criteria

- ✅ Shader compiles without errors
- ✅ Application launches without crashes
- ✅ Renders test_cube.obj identically to baseline
- ✅ Renders Sponza identically to baseline
- ✅ No D3D12 validation errors
- ✅ Stable for 1000+ frames

---

## References

- **BLAS Strategy:** [BLAS_STRATEGY.md](BLAS_STRATEGY.md)
- **Buffer Analysis:** [Buffer_Analysis.md](Buffer_Analysis.md)
- **D3D12 Resource Binding:** [../Concepts/D3D12_Resource_Binding.md](../Concepts/D3D12_Resource_Binding.md)

---

## Next Phase

Phase 2A.3: Vulkan Backend Refactor
- Apply same global buffer approach to Vulkan
- Remove shader record buffer addresses
- Use set0 for all bindings
- See [../Vulkan/](../Vulkan/) for planning
