# Phase 2A.3: Vulkan Backend Refactor Plan

**Goal:** Update Vulkan backend to use global buffers, validate rendering  
**Status:** ⏸️ PENDING (Waiting for DXR completion)  
**Date:** October 10, 2025

---

## Overview

Update the Vulkan backend to:
1. Use global buffers (bindings 10-12 in set0) for shader access
2. Remove buffer_reference and shaderRecordEXT usage
3. Simplify shader binding table (no per-geometry data)
4. Maintain BLAS building with existing approach

---

## Shader Changes

**File:** `backends/vulkan/hit.rchit` (and other shader files)

### Add New Structs

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

### Add Global Buffer Bindings (set0)

```glsl
layout(binding = 10, set = 0, scalar) readonly buffer GlobalVertices {
    Vertex vertices[];
} globalVertices;

layout(binding = 11, set = 0, scalar) readonly buffer GlobalIndices {
    uint indices[];  // uint not uvec3
} globalIndices;

layout(binding = 12, set = 0, std430) readonly buffer MeshDescBuffer {
    MeshDesc descs[];
} meshDescs;
```

### Remove buffer_reference and shaderRecordEXT

```glsl
// DELETE THESE:
// layout(buffer_reference, scalar) buffer VertexBuffer { vec3 v[]; };
// layout(buffer_reference, scalar) buffer IndexBuffer { uvec3 i[]; };
// layout(buffer_reference, scalar) buffer NormalBuffer { vec3 n[]; };
// layout(buffer_reference, scalar) buffer UVBuffer { vec2 uv[]; };
// layout(shaderRecordEXT, std430) buffer SBT { ... };
```

### Update main() in hit.rchit

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
    
    // Continue with shading...
}
```

---

## C++ Changes

**File:** `backends/vulkan/render_vulkan.cpp`

### Add Member Variables

```cpp
// In render_vulkan.h
class RenderVulkan {
    // Existing members...
    
    // NEW: Global buffers
    vkr::Buffer global_vertex_buffer;
    vkr::Buffer global_index_buffer;
    vkr::Buffer mesh_desc_buffer;
};
```

### Modify `set_scene()` Function

**Create global buffers:**

```cpp
void RenderVulkan::set_scene(const Scene &scene)
{
    frame_id = 0;
    samples_per_pixel = scene.samples_per_pixel;
    
    // 1. Create Global Vertex Buffer
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
    
    // 2. Create Global Index Buffer (similar)
    global_index_buffer = vkr::Buffer::device(
        device,
        scene.global_indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    // ... staging upload
    
    // 3. Create MeshDesc Buffer (similar)
    mesh_desc_buffer = vkr::Buffer::device(
        device,
        scene.mesh_descriptors.size() * sizeof(MeshDesc),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    // ... staging upload
    
    // Copy staging to device buffers via command buffer
    vkCmdCopyBuffer(cmd_buf, staging_verts.buffer, global_vertex_buffer.buffer, ...);
    vkCmdCopyBuffer(cmd_buf, staging_indices.buffer, global_index_buffer.buffer, ...);
    vkCmdCopyBuffer(cmd_buf, staging_descs.buffer, mesh_desc_buffer.buffer, ...);
    
    // Pipeline barriers to make buffers shader-visible
    // ...
    
    // Continue with BLAS/TLAS building
    // ...
}
```

### Update Descriptor Set Layout

**Add bindings for global buffers:**

```cpp
std::vector<VkDescriptorSetLayoutBinding> bindings;

// Existing bindings (TLAS, render targets, etc.)
// ...

// NEW: Global buffer bindings
bindings.push_back({
    .binding = 10,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
});

bindings.push_back({
    .binding = 11,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
});

bindings.push_back({
    .binding = 12,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .descriptorCount = 1,
    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
});

// Create layout
VkDescriptorSetLayoutCreateInfo layoutInfo = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = bindings.size(),
    .pBindings = bindings.data()
};
vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &desc_set_layout);
```

### Update Descriptor Set Writes

**Write descriptors for global buffers:**

```cpp
std::vector<VkWriteDescriptorSet> writes;

// Existing writes...

// NEW: Global vertex buffer
VkDescriptorBufferInfo vertexInfo = {
    .buffer = global_vertex_buffer.buffer,
    .offset = 0,
    .range = VK_WHOLE_SIZE
};
writes.push_back({
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = desc_set,
    .dstBinding = 10,
    .descriptorCount = 1,
    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .pBufferInfo = &vertexInfo
});

// Similar for indices (binding 11) and mesh_descs (binding 12)
// ...

vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
```

### Remove Shader Record Setup

**Delete:**
- `vkGetBufferDeviceAddressKHR` calls for per-geometry buffers
- Shader record buffer creation and data packing
- Hit group shader record writes

**Keep:**
- Simplified shader binding table (shader identifiers only)

---

## Tasks

### 2A.3.1: Update GLSL Shaders
- Add Vertex and MeshDesc structs
- Add global buffer declarations (binding 10-12, set0)
- Remove buffer_reference and shaderRecordEXT
- Update main() with indirection logic

### 2A.3.2: Update C++ Buffer Creation
- Add global buffer member variables
- Modify `set_scene()` to create global buffers
- Upload data via staging buffers
- Remove per-geometry buffer creation

### 2A.3.3: Update Descriptor Sets
- Add bindings for 3 global buffers
- Write descriptors for global buffers

### 2A.3.4: Simplify Shader Binding Table
- Remove shader record setup code
- Remove buffer address queries
- Keep only shader identifiers in SBT

### 2A.3.5: Test and Validate
- Build and run with test_cube.obj
- Compare screenshot to baseline
- Build and run with Sponza
- Compare screenshot to baseline
- Check validation layers (no errors)

---

## Validation Criteria

- ✅ Shaders compile without errors
- ✅ Application launches without crashes
- ✅ Renders test_cube.obj identically to baseline
- ✅ Renders Sponza identically to baseline
- ✅ No Vulkan validation layer errors
- ✅ Stable for 1000+ frames

---

## References

- **DXR Implementation:** See [../DXR/PLAN.md](../DXR/PLAN.md) for parallel approach
- **Buffer Analysis:** [Buffer_Analysis.md](Buffer_Analysis.md)

---

## Next Steps

After Phase 2A.3 completion:
1. Final cross-backend validation (DXR vs Vulkan)
2. Performance comparison (before vs after)
3. Documentation update
4. Begin Phase 2B: Unified Slang Shaders
