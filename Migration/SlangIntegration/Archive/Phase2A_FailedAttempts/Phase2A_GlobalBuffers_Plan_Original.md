# Phase 2A: Global Buffer Refactor Plan

**Date:** October 10, 2025  
**Status:** Planning  
**Goal:** Refactor scene loading to use global buffers instead of per-geometry buffers, validate with native HLSL/GLSL shaders

**NO SLANG IN THIS PHASE** - Pure CPU refactor + native shader validation

---

## Overview

This phase refactors the scene buffer architecture to eliminate per-geometry buffers in favor of a single global buffer approach. This is a prerequisite for eventual Slang shader unification, but **this phase uses only native HLSL and GLSL**.

**Key Changes:**
1. CPU: Merge all geometry data into global arrays during scene loading
2. DXR: Update to use global buffers (space0), remove local root signature (space1)
3. Vulkan: Update to use global buffers (set0), remove shader record buffer addresses
4. Validate: Both backends should render identically to before

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
- test_cube.obj renders identically
- Sponza renders identically

✅ **Vulkan Backend:**
- No shader record buffer addresses
- All resources bound in set0
- Shader uses global buffers with offset indirection
- test_cube.obj renders identically
- Sponza renders identically

---

## Phase 2A.0: Preparation (Analysis)

**Goal:** Understand current architecture before changes

### Tasks

**2A.0.1: Document Current Scene Loading**
- Read `util/scene.cpp` - understand how OBJ/GLTF are loaded
- Read `util/mesh.h` - understand `Geometry`, `Mesh`, `ParameterizedMesh`, `Instance` structs
- Document current data flow: File → CPU structs → GPU buffers

**2A.0.2: Document Current DXR Buffer Creation**
- Read `backends/dxr/render_dxr.cpp::set_scene()`
- Identify where per-geometry buffers are created (vertices, indices, normals, UVs)
- Document current BLAS/TLAS building with per-geometry buffers
- Understand current local root signature setup (space1)

**2A.0.3: Document Current Vulkan Buffer Creation**
- Read `backends/vulkan/render_vulkan.cpp::set_scene()`
- Identify where per-geometry buffers are created
- Document current shader record setup (buffer device addresses)
- Understand current descriptor set layout

**Deliverables:**
- Understanding of current architecture (no code changes)
- List of files that need modification
- Baseline screenshots of test_cube.obj and Sponza for comparison

---

## Phase 2A.1: CPU Scene Refactor

**Goal:** Create global buffers during scene loading

### New Data Structures

**File:** `util/mesh.h` (additions)

```cpp
// Unified vertex structure (combines position, normal, texCoord)
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    
    Vertex() = default;
    Vertex(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& uv)
        : position(pos), normal(norm), texCoord(uv) {}
};

// Mesh descriptor (offset into global buffers)
struct MeshDesc {
    uint32_t vbOffset;      // Offset into global vertex buffer
    uint32_t ibOffset;      // Offset into global index buffer
    uint32_t vertexCount;   // Number of vertices
    uint32_t indexCount;    // Number of indices (triangles * 3)
    uint32_t materialID;    // Material index
    
    MeshDesc() = default;
    MeshDesc(uint32_t vb, uint32_t ib, uint32_t vc, uint32_t ic, uint32_t mat)
        : vbOffset(vb), ibOffset(ib), vertexCount(vc), indexCount(ic), materialID(mat) {}
};

// Geometry instance data (for TLAS instances)
struct GeometryInstanceData {
    uint32_t meshID;        // Index into MeshDesc array
    uint32_t matrixID;      // Index into transform matrix array
    uint32_t flags;         // Instance flags (e.g., double-sided)
    
    GeometryInstanceData() = default;
    GeometryInstanceData(uint32_t mesh, uint32_t mat, uint32_t f = 0)
        : meshID(mesh), matrixID(mat), flags(f) {}
};
```

### New Scene Fields

**File:** `util/scene.h` (additions to Scene struct)

```cpp
struct Scene {
    // Existing fields...
    std::vector<Mesh> meshes;
    std::vector<ParameterizedMesh> parameterized_meshes;
    std::vector<Instance> instances;
    // ...
    
    // NEW: Global buffers for GPU upload
    std::vector<Vertex> global_vertices;           // All vertices concatenated
    std::vector<uint32_t> global_indices;          // All indices concatenated (as uints, not uvec3)
    std::vector<MeshDesc> mesh_descriptors;        // Metadata per geometry
    std::vector<GeometryInstanceData> geometry_instances; // Metadata per instance
    std::vector<glm::mat4> transform_matrices;     // Transform per instance
    
    // Helper function to build global buffers
    void build_global_buffers();
};
```

### Implementation

**File:** `util/scene.cpp` (new function)

```cpp
void Scene::build_global_buffers() {
    // Clear previous data
    global_vertices.clear();
    global_indices.clear();
    mesh_descriptors.clear();
    geometry_instances.clear();
    transform_matrices.clear();
    
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t meshID = 0;
    
    // For each parameterized mesh (mesh + material assignments)
    for (const auto& pm : parameterized_meshes) {
        const Mesh& mesh = meshes[pm.mesh_id];
        
        // For each geometry in the mesh
        for (size_t geom_idx = 0; geom_idx < mesh.geometries.size(); ++geom_idx) {
            const Geometry& geom = mesh.geometries[geom_idx];
            
            // Get material ID for this geometry
            uint32_t materialID = (geom_idx < pm.material_ids.size()) 
                                  ? pm.material_ids[geom_idx] 
                                  : 0;
            
            // Create mesh descriptor
            MeshDesc desc(
                vertexOffset,
                indexOffset,
                geom.vertices.size(),
                geom.indices.size() * 3,
                materialID
            );
            mesh_descriptors.push_back(desc);
            
            // Append vertices to global array
            for (size_t i = 0; i < geom.vertices.size(); ++i) {
                Vertex v;
                v.position = geom.vertices[i];
                v.normal = (i < geom.normals.size()) ? geom.normals[i] : glm::vec3(0, 1, 0);
                v.texCoord = (i < geom.uvs.size()) ? geom.uvs[i] : glm::vec2(0, 0);
                global_vertices.push_back(v);
            }
            
            // Append indices to global array (adjust by vertex offset!)
            for (const auto& tri : geom.indices) {
                global_indices.push_back(tri.x + vertexOffset);
                global_indices.push_back(tri.y + vertexOffset);
                global_indices.push_back(tri.z + vertexOffset);
            }
            
            // Update offsets
            vertexOffset += geom.vertices.size();
            indexOffset += geom.indices.size() * 3;
            
            ++meshID;
        }
    }
    
    // Build geometry instance data (for TLAS)
    uint32_t matrixID = 0;
    for (const auto& instance : instances) {
        const ParameterizedMesh& pm = parameterized_meshes[instance.parameterized_mesh_id];
        const Mesh& mesh = meshes[pm.mesh_id];
        
        // Add transform matrix
        transform_matrices.push_back(instance.transform);
        
        // One GeometryInstanceData per geometry in the mesh
        for (size_t geom_idx = 0; geom_idx < mesh.geometries.size(); ++geom_idx) {
            // Calculate which mesh descriptor this corresponds to
            // This requires tracking mesh ID properly - simplified here
            uint32_t meshDescID = /* calculate based on pm and geom_idx */;
            
            GeometryInstanceData instData(meshDescID, matrixID, 0);
            geometry_instances.push_back(instData);
        }
        
        ++matrixID;
    }
}
```

**Note:** The instance tracking logic needs careful consideration based on how BLAS/TLAS are built.

### Tasks

**2A.1.1: Add New Structs**
- Add `Vertex`, `MeshDesc`, `GeometryInstanceData` to `util/mesh.h`
- Add global buffer fields to `Scene` struct in `util/scene.h`

**2A.1.2: Implement `build_global_buffers()`**
- Implement concatenation logic in `util/scene.cpp`
- Handle vertex offset adjustment for indices
- Handle missing normals/UVs gracefully
- Track mesh ID to geometry mapping

**2A.1.3: Call `build_global_buffers()` After Scene Load**
- Modify `Scene::Scene()` constructor to call `build_global_buffers()` after loading
- Or call explicitly in main.cpp after scene load

**2A.1.4: Test CPU Side**
- Load test_cube.obj
- Print sizes: `global_vertices.size()`, `global_indices.size()`, `mesh_descriptors.size()`
- Verify offsets are correct
- Load Sponza
- Verify no crashes, reasonable buffer sizes

**Validation:**
- No GPU code touched yet
- Scene loads without crashes
- Global buffer sizes match expected (sum of all geometries)

---

## Phase 2A.2: DXR Backend Refactor

**Goal:** Update DXR to use global buffers, validate rendering

### Shader Changes

**File:** `backends/dxr/render_dxr.hlsl`

**Add new structs (top of file):**
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

**Add new global buffers (space0):**
```hlsl
// Add after existing space0 resources
StructuredBuffer<Vertex> globalVertices : register(t10, space0);
StructuredBuffer<uint> globalIndices : register(t11, space0);    // Note: uint not uint3
StructuredBuffer<MeshDesc> meshDescs : register(t12, space0);
```

**Remove space1 resources:**
```hlsl
// DELETE THESE:
// StructuredBuffer<float3> vertices : register(t0, space1);
// StructuredBuffer<uint3> indices : register(t1, space1);
// StructuredBuffer<float3> normals : register(t2, space1);
// StructuredBuffer<float2> uvs : register(t3, space1);
// cbuffer MeshData : register(b0, space1) { ... }
```

**Update ClosestHit shader:**
```hlsl
[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib)
{
    // Get which geometry we hit
    uint meshID = InstanceID();  // DXR built-in
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
    
    // Rest of shading logic UNCHANGED
    // ...
}
```

### C++ Changes

**File:** `backends/dxr/render_dxr.cpp`

**Modify `set_scene()` function:**

**Current code (lines ~120-250):** Creates per-geometry buffers in loop  
**New code:** Create 3 global buffers

```cpp
void RenderDXR::set_scene(const Scene &scene)
{
    frame_id = 0;
    samples_per_pixel = scene.samples_per_pixel;
    
    // NEW: Create global buffers
    
    // 1. Global Vertex Buffer
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
    
    // 2. Global Index Buffer
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
    
    // 3. MeshDesc Buffer
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
    
    // Barriers
    std::vector<D3D12_RESOURCE_BARRIER> barriers = {
        barrier_transition(global_vertex_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        barrier_transition(global_index_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        barrier_transition(mesh_desc_buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    cmd_list->ResourceBarrier(barriers.size(), barriers.data());
    
    // Continue with BLAS/TLAS building using original per-geometry data for geometry descriptions
    // (BLAS input still uses individual geometry vertex/index data, but we don't need those buffers after build)
    
    // ... rest of set_scene (BLAS/TLAS build, materials, textures)
}
```

**Add member variables to `RenderDXR` class:**
```cpp
// In render_dxr.h
class RenderDXR {
    // ...
    dxr::Buffer global_vertex_buffer;
    dxr::Buffer global_index_buffer;
    dxr::Buffer mesh_desc_buffer;
};
```

**Update `build_descriptor_heap()` to include new buffers:**
```cpp
// Add SRVs for global buffers at indices t10, t11, t12 in space0
```

**Remove local root signature code:**
- Remove hit group shader record creation
- Remove space1 descriptor setup
- Use only global root signature

### Tasks

**2A.2.1: Update HLSL Shader**
- Add Vertex and MeshDesc structs
- Add global buffer declarations (t10, t11, t12 in space0)
- Remove space1 declarations
- Update ClosestHit shader with indirection logic

**2A.2.2: Update DXR C++ - Buffer Creation**
- Add global buffer member variables to RenderDXR class
- Modify `set_scene()` to create global buffers
- Remove per-geometry buffer creation loop

**2A.2.3: Update DXR C++ - Descriptor Heap**
- Add SRVs for 3 global buffers to descriptor heap
- Update descriptor indices if needed

**2A.2.4: Update DXR C++ - Root Signature**
- Remove local root signature setup
- Ensure global root signature includes new SRVs

**2A.2.5: Test DXR**
- Build and run with test_cube.obj
- Compare screenshot to baseline (should be identical)
- Build and run with Sponza
- Compare screenshot to baseline (should be identical)

**Validation:**
- DXR renders identically to before refactor
- No shader compilation errors
- No crashes

---

## Phase 2A.3: Vulkan Backend Refactor

**Goal:** Update Vulkan to use global buffers, validate rendering

### Shader Changes

**File:** `backends/vulkan/hit.rchit` (and other shader files)

**Add new structs (top of file):**
```glsl
struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 texCoord;
};

struct MeshDesc {
    uint vbOffset;
    uint ibOffset;
    uint vertexCount;
    uint indexCount;
    uint materialID;
};
```

**Add new global buffers (set0):**
```glsl
layout(binding = 10, set = 0, scalar) readonly buffer GlobalVertices {
    Vertex vertices[];
} globalVertices;

layout(binding = 11, set = 0, scalar) readonly buffer GlobalIndices {
    uint indices[];  // Note: uint not uvec3
} globalIndices;

layout(binding = 12, set = 0, std430) readonly buffer MeshDescBuffer {
    MeshDesc descs[];
} meshDescs;
```

**Remove buffer_reference and shaderRecordEXT:**
```glsl
// DELETE THESE:
// layout(buffer_reference, scalar) buffer VertexBuffer { vec3 v[]; };
// layout(buffer_reference, scalar) buffer IndexBuffer { uvec3 i[]; };
// layout(buffer_reference, scalar) buffer NormalBuffer { vec3 n[]; };
// layout(buffer_reference, scalar) buffer UVBuffer { vec2 uv[]; };
// layout(shaderRecordEXT, std430) buffer SBT { ... };
```

**Update main() in hit.rchit:**
```glsl
void main()
{
    // Get which geometry we hit
    uint meshID = gl_InstanceCustomIndexEXT;  // Vulkan built-in
    MeshDesc mesh = meshDescs.descs[meshID];
    
    // Fetch triangle indices (offset into global buffer)
    uint triIndex = mesh.ibOffset + gl_PrimitiveID * 3;
    uint idx0 = globalIndices.indices[triIndex + 0];
    uint idx1 = globalIndices.indices[triIndex + 1];
    uint idx2 = globalIndices.indices[triIndex + 2];
    
    // Fetch vertices
    Vertex v0 = globalVertices.vertices[idx0];
    Vertex v1 = globalVertices.vertices[idx1];
    Vertex v2 = globalVertices.vertices[idx2];
    
    // Barycentric interpolation
    vec3 barycentrics = vec3(1.0 - gl_HitTEXT.x - gl_HitTEXT.y, gl_HitTEXT.x, gl_HitTEXT.y);
    
    vec3 position = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
    vec3 normal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    vec2 texCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;
    
    // Transform to world space
    vec3 worldPos = (gl_ObjectToWorldEXT * vec4(position, 1.0)).xyz;
    vec3 worldNormal = normalize((gl_ObjectToWorldEXT * vec4(normal, 0.0)).xyz);
    
    // Material lookup
    uint materialID = mesh.materialID;
    
    // Rest of shading logic UNCHANGED
    // ...
}
```

### C++ Changes

**File:** `backends/vulkan/render_vulkan.cpp`

**Modify `set_scene()` function:**

Similar to DXR changes:
1. Create 3 global VkBuffers (vertices, indices, mesh_descs)
2. Upload data using staging buffers
3. Remove per-geometry buffer creation loop
4. Remove shader record buffer address setup

```cpp
void RenderVulkan::set_scene(const Scene &scene)
{
    frame_id = 0;
    samples_per_pixel = scene.samples_per_pixel;
    
    // Create global vertex buffer
    global_vertex_buffer = vkr::Buffer::device(
        device,
        scene.global_vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    
    // Upload via staging buffer
    auto staging_verts = vkr::Buffer::host(
        device,
        scene.global_vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    );
    std::memcpy(staging_verts.map(), scene.global_vertices.data(), staging_verts.size());
    staging_verts.unmap();
    
    // Similar for indices and mesh_descs...
    
    // Copy staging to device buffers via command buffer
    // ...
    
    // Continue with BLAS/TLAS building
    // ...
}
```

**Update descriptor set layout:**
```cpp
// Add 3 new bindings for global buffers at binding 10, 11, 12 in set 0
VkDescriptorSetLayoutBinding globalVertexBinding = {
    .binding = 10,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
};
// Similar for indices and mesh_descs
```

**Update descriptor set writes:**
```cpp
// Write descriptors for global buffers
VkDescriptorBufferInfo vertexInfo = {
    .buffer = global_vertex_buffer.buffer,
    .offset = 0,
    .range = VK_WHOLE_SIZE
};

VkWriteDescriptorSet vertexWrite = {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = desc_set,
    .dstBinding = 10,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .pBufferInfo = &vertexInfo
};
// Similar for indices and mesh_descs
```

**Remove shader record setup:**
- Remove `vkGetBufferDeviceAddressKHR` calls for per-geometry buffers
- Remove shader record buffer creation
- Simplify shader binding table (no per-geometry records)

### Tasks

**2A.3.1: Update GLSL Shaders**
- Add Vertex and MeshDesc structs to hit.rchit (and other shaders if needed)
- Add global buffer declarations (binding 10, 11, 12 in set 0)
- Remove buffer_reference and shaderRecordEXT declarations
- Update main() with indirection logic

**2A.3.2: Update Vulkan C++ - Buffer Creation**
- Add global buffer member variables to RenderVulkan class
- Modify `set_scene()` to create global buffers
- Remove per-geometry buffer creation loop

**2A.3.3: Update Vulkan C++ - Descriptor Sets**
- Add bindings for 3 global buffers to descriptor set layout
- Write descriptors for global buffers

**2A.3.4: Update Vulkan C++ - Shader Binding Table**
- Remove shader record setup code
- Simplify SBT (no per-geometry records)

**2A.3.5: Test Vulkan**
- Build and run with test_cube.obj
- Compare screenshot to baseline (should be identical)
- Build and run with Sponza
- Compare screenshot to baseline (should be identical)

**Validation:**
- Vulkan renders identically to before refactor
- No shader compilation errors
- No validation layer errors
- No crashes

---

## Phase 2A.4: Final Validation

**Goal:** Ensure both backends work correctly with global buffers

### Tasks

**2A.4.1: Cross-Backend Comparison**
- Render test_cube.obj with DXR
- Render test_cube.obj with Vulkan
- Compare screenshots (should be identical)
- Render Sponza with DXR
- Render Sponza with Vulkan
- Compare screenshots (should be identical)

**2A.4.2: Performance Check**
- Measure frame time before refactor (baseline)
- Measure frame time after refactor
- Verify no significant performance regression
- (Small overhead from indirection is acceptable)

**2A.4.3: Memory Usage Check**
- Compare GPU memory usage before/after
- Should be LESS memory (fewer buffers)
- Verify no memory leaks

**2A.4.4: Documentation**
- Update STATUS.md - mark Phase 2A complete
- Document any issues encountered and solutions
- Create comparison screenshots (before/after)

---

## Deliverables

Upon completion of Phase 2A:

1. ✅ **Working CPU scene refactor** - global buffers created during load
2. ✅ **Working DXR backend** - uses global buffers, renders identically
3. ✅ **Working Vulkan backend** - uses global buffers, renders identically
4. ✅ **Validation** - screenshots prove identical rendering
5. ✅ **No Slang** - all shaders still native HLSL/GLSL
6. ✅ **Foundation** - ready for eventual Slang conversion (future phase)

---

## Risk Mitigation

**Risk:** Breaking existing rendering  
**Mitigation:** Incremental approach (CPU → DXR → Vulkan), test after each step

**Risk:** Performance regression  
**Mitigation:** Measure and compare, accept small indirection overhead

**Risk:** Index offset calculation errors  
**Mitigation:** Test with simple scenes first (test_cube), add validation

**Risk:** BLAS/TLAS issues with new buffer layout  
**Mitigation:** BLAS build still uses original geometry data (temporary buffers)

---

## Notes

- This phase does NOT touch Slang at all
- BLAS building may still need temporary per-geometry buffers (fine, they're discarded after build)
- Focus on correctness first, optimization later
- Keep baseline screenshots for comparison
- Git commit after each sub-phase for easy rollback

---

## Next Steps (Not Part of This Phase)

After Phase 2A is complete and validated:
- **Phase 2B:** Convert native shaders to Slang language (future)
- **Phase 2C:** Unified Slang shader for both backends (future)

**For now: Focus only on 2A - global buffers with native shaders**
