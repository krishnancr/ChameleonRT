# Task 2A.0.3: Vulkan Buffer Creation Analysis

**Date:** 2025-10-10  
**Status:** Complete  
**Deliverable:** Analysis of Vulkan backend per-geometry buffer architecture

---

## Executive Summary

The Vulkan backend uses a **per-geometry buffer architecture** with **buffer device addresses** passed via shader records. Each geometry gets 4 separate Vulkan buffers (vertex, index, normal, UV) which are accessed in shaders using the **`buffer_reference`** extension and **`shaderRecordEXT`** for per-geometry data.

**Critical Findings:**
1. **Per-Geometry Buffers:** Each `vkrt::Geometry` object creates 4 separate VkBuffer objects
2. **Buffer Device Addresses:** Uses `vkGetBufferDeviceAddressKHR` to get 64-bit GPU pointers
3. **Shader Records:** Each hit group has a `HitGroupParams` structure containing buffer addresses
4. **buffer_reference Extension:** Shaders use `buffer_reference` to dereference GPU pointers
5. **shaderRecordEXT:** Shader accesses per-geometry data from shader binding table
6. **Descriptor Set 0:** Contains scene-wide resources (TLAS, render targets, materials, lights, textures)
7. **Descriptor Set 1:** Contains texture array with variable descriptor count

---

## RenderVulkan Class Structure

### Member Variables (render_vulkan.h)

**Buffer-Related Members:**
```cpp
struct HitGroupParams {
    uint64_t vert_buf = 0;       // Buffer device address for vertex positions
    uint64_t idx_buf = 0;        // Buffer device address for triangle indices
    uint64_t normal_buf = 0;     // Buffer device address for normals
    uint64_t uv_buf = 0;         // Buffer device address for UVs
    uint32_t num_normals = 0;    // Flag: 1 if normals present, 0 otherwise
    uint32_t num_uvs = 0;        // Flag: 1 if UVs present, 0 otherwise
    uint32_t material_id = 0;    // Material index
};

struct RenderVulkan : RenderBackend {
    // Scene-wide buffers (NOT per-geometry)
    std::shared_ptr<vkrt::Buffer> view_param_buf;    // Camera parameters (set 0, binding 3)
    std::shared_ptr<vkrt::Buffer> img_readback_buf;  // CPU readback buffer
    std::shared_ptr<vkrt::Buffer> mat_params;        // Materials (set 0, binding 4)
    std::shared_ptr<vkrt::Buffer> light_params;      // Lights (set 0, binding 5)
    
    // Render targets
    std::shared_ptr<vkrt::Texture2D> render_target;  // Output image (set 0, binding 1)
    std::shared_ptr<vkrt::Texture2D> accum_buffer;   // Accumulation buffer (set 0, binding 2)
    std::shared_ptr<vkrt::Texture2D> ray_stats;      // Optional (set 0, binding 6)
    
    // Acceleration structures
    std::vector<std::unique_ptr<vkrt::TriangleMesh>> meshes;  // One BLAS per Mesh
    std::unique_ptr<vkrt::TopLevelBVH> scene_bvh;             // TLAS (set 0, binding 0)
    size_t total_geom = 0;                                     // Total geometry count
    
    // Textures
    std::vector<std::shared_ptr<vkrt::Texture2D>> textures;   // Texture array (set 1, binding 0)
    VkSampler sampler = VK_NULL_HANDLE;                        // Texture sampler
    
    // Descriptor sets
    VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;         // Set 0 layout
    VkDescriptorSetLayout textures_desc_layout = VK_NULL_HANDLE; // Set 1 layout
    VkDescriptorSet desc_set = VK_NULL_HANDLE;                   // Set 0
    VkDescriptorSet textures_desc_set = VK_NULL_HANDLE;          // Set 1
    
    // Shader binding table
    vkrt::ShaderBindingTable shader_table;
    
    // Scene metadata (CPU-side)
    std::vector<ParameterizedMesh> parameterized_meshes;
};
```

**Key Observation:** Like DXR, there are NO per-geometry buffer member variables in `RenderVulkan`. The per-geometry buffers are stored inside each `vkrt::Geometry` object within `vkrt::TriangleMesh::geometries`.

---

## Per-Geometry Buffer Creation

### Vulkan Geometry Structure (vulkanrt_utils.h)

```cpp
struct Geometry {
    std::shared_ptr<Buffer> vertex_buf;   // VkBuffer for vertex positions (vec3)
    std::shared_ptr<Buffer> index_buf;    // VkBuffer for triangle indices (uvec3)
    std::shared_ptr<Buffer> normal_buf;   // VkBuffer for vertex normals (vec3) - may be null
    std::shared_ptr<Buffer> uv_buf;       // VkBuffer for texture coordinates (vec2) - may be null
    VkAccelerationStructureGeometryKHR geom_desc;  // Geometry descriptor for BLAS building
};
```

**Each Geometry has 4 independent GPU buffers** (vertex, index, normal, UV).

**Buffer Usage Flags:**
- `vertex_buf`, `index_buf`:
  - `VK_BUFFER_USAGE_TRANSFER_DST_BIT` - Upload destination
  - `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` - Shader access
  - `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` - **Get GPU pointer**
  - `VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR` - BLAS input
- `normal_buf`, `uv_buf`:
  - `VK_BUFFER_USAGE_TRANSFER_DST_BIT`
  - `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`
  - `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`

---

## Buffer Creation Loop (render_vulkan.cpp::set_scene)

### Pseudocode

```cpp
void RenderVulkan::set_scene(const Scene &scene) {
    // For each Mesh in the scene
    for (const auto &mesh : scene.meshes) {
        std::vector<vkrt::Geometry> geometries;
        
        // For each Geometry in the Mesh
        for (const auto &geom : mesh.geometries) {
            // ========== CREATE UPLOAD (STAGING) BUFFERS ==========
            // Upload buffers are in CPU-writable memory (host-visible)
            auto upload_verts = vkrt::Buffer::host(
                *device,
                geom.vertices.size() * sizeof(glm::vec3),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            );
            void *map = upload_verts->map();
            std::memcpy(map, geom.vertices.data(), upload_verts->size());
            upload_verts->unmap();
            
            auto upload_indices = vkrt::Buffer::host(
                *device,
                geom.indices.size() * sizeof(glm::uvec3),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            );
            map = upload_indices->map();
            std::memcpy(map, geom.indices.data(), upload_indices->size());
            upload_indices->unmap();
            
            // Optional: Upload normals if present
            std::shared_ptr<vkrt::Buffer> upload_normals = nullptr;
            std::shared_ptr<vkrt::Buffer> normal_buf = nullptr;
            if (!geom.normals.empty()) {
                upload_normals = vkrt::Buffer::host(
                    *device,
                    geom.normals.size() * sizeof(glm::vec3),
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                );
                normal_buf = vkrt::Buffer::device(
                    *device,
                    upload_normals->size(),
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT  // <-- For GPU pointer
                );
                map = upload_normals->map();
                std::memcpy(map, geom.normals.data(), upload_normals->size());
                upload_normals->unmap();
            }
            
            // Optional: Upload UVs if present
            std::shared_ptr<vkrt::Buffer> upload_uvs = nullptr;
            std::shared_ptr<vkrt::Buffer> uv_buf = nullptr;
            if (!geom.uvs.empty()) {
                upload_uvs = vkrt::Buffer::host(
                    *device,
                    geom.uvs.size() * sizeof(glm::vec2),
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                );
                uv_buf = vkrt::Buffer::device(
                    *device,
                    upload_uvs->size(),
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT  // <-- For GPU pointer
                );
                map = upload_uvs->map();
                std::memcpy(map, geom.uvs.data(), upload_uvs->size());
                upload_uvs->unmap();
            }
            
            // ========== CREATE DEVICE (GPU) BUFFERS ==========
            auto vertex_buf = vkrt::Buffer::device(
                *device,
                upload_verts->size(),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |  // <-- For GPU pointer
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR  // BLAS
            );
            
            auto index_buf = vkrt::Buffer::device(
                *device,
                upload_indices->size(),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |  // <-- For GPU pointer
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR  // BLAS
            );
            
            // ========== GPU COPY ==========
            vkBeginCommandBuffer(command_buffer, &begin_info);
            
            VkBufferCopy copy_cmd = {};
            copy_cmd.size = upload_verts->size();
            vkCmdCopyBuffer(command_buffer,
                            upload_verts->handle(),
                            vertex_buf->handle(),
                            1,
                            &copy_cmd);
            
            copy_cmd.size = upload_indices->size();
            vkCmdCopyBuffer(command_buffer,
                            upload_indices->handle(),
                            index_buf->handle(),
                            1,
                            &copy_cmd);
            
            if (upload_normals) {
                copy_cmd.size = upload_normals->size();
                vkCmdCopyBuffer(command_buffer,
                                upload_normals->handle(),
                                normal_buf->handle(),
                                1,
                                &copy_cmd);
            }
            
            if (upload_uvs) {
                copy_cmd.size = upload_uvs->size();
                vkCmdCopyBuffer(command_buffer,
                                upload_uvs->handle(),
                                uv_buf->handle(),
                                1,
                                &copy_cmd);
            }
            
            vkEndCommandBuffer(command_buffer);
            vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE);
            vkQueueWaitIdle(device->graphics_queue());
            
            // ========== CREATE GEOMETRY OBJECT ==========
            // vkrt::Geometry constructor stores buffer references and creates VkAccelerationStructureGeometryKHR
            geometries.emplace_back(vertex_buf, index_buf, normal_buf, uv_buf);
            ++total_geom;
            
            vkResetCommandPool(device->logical_device(), command_pool, ...);
        }
        
        // ========== BUILD BOTTOM-LEVEL BVH ==========
        auto bvh = std::make_unique<vkrt::TriangleMesh>(*device, geometries);
        
        vkBeginCommandBuffer(command_buffer, &begin_info);
        bvh->enqueue_build(command_buffer);
        vkEndCommandBuffer(command_buffer);
        vkQueueSubmit(...);
        vkQueueWaitIdle(...);
        
        vkBeginCommandBuffer(command_buffer, &begin_info);
        bvh->enqueue_compaction(command_buffer);
        vkEndCommandBuffer(command_buffer);
        vkQueueSubmit(...);
        vkQueueWaitIdle(...);
        
        bvh->finalize();
        meshes.emplace_back(std::move(bvh));
    }
    
    // Continue with TLAS build, materials, lights, textures...
}
```

---

## Buffer Device Address Usage

### Getting Device Addresses

**In `vkrt::Buffer` class (likely in vulkan_utils.h/cpp):**
```cpp
uint64_t Buffer::device_address() const {
    VkBufferDeviceAddressInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = handle();  // VkBuffer
    return vkGetBufferDeviceAddressKHR(device->logical_device(), &info);
}
```

**When Called:** In `build_shader_binding_table()` for each geometry:
```cpp
params->vert_buf = meshes[pm.mesh_id]->geometries[j].vertex_buf->device_address();
params->idx_buf = meshes[pm.mesh_id]->geometries[j].index_buf->device_address();
params->normal_buf = meshes[pm.mesh_id]->geometries[j].normal_buf->device_address();  // if present
params->uv_buf = meshes[pm.mesh_id]->geometries[j].uv_buf->device_address();          // if present
```

**Result:** 64-bit GPU virtual address (pointer) stored in shader record.

---

## BLAS (Bottom-Level Acceleration Structure) Building

### BLAS Structure

```cpp
class TriangleMesh {
    std::vector<VkAccelerationStructureGeometryKHR> geom_descs;  // Geometry descriptors
    std::vector<Geometry> geometries;  // Stores buffers (vertex_buf, index_buf, etc.)
    VkAccelerationStructureKHR bvh = VK_NULL_HANDLE;  // Acceleration structure
    uint64_t handle = 0;  // Device address of BVH
    // ... scratch buffers, query pool for compaction
};
```

**Key Point:** One `TriangleMesh` per `Mesh` (not per `Geometry`). Each BLAS contains multiple geometries.

### Geometry Descriptor Creation

When `vkrt::Geometry` is constructed (vulkanrt_utils.cpp, not shown but inferred):

```cpp
Geometry::Geometry(std::shared_ptr<Buffer> vertex_buf,
                   std::shared_ptr<Buffer> index_buf,
                   std::shared_ptr<Buffer> normal_buf,
                   std::shared_ptr<Buffer> uv_buf,
                   uint32_t geom_flags) {
    this->vertex_buf = vertex_buf;
    this->index_buf = index_buf;
    this->normal_buf = normal_buf;
    this->uv_buf = uv_buf;
    
    // Create VkAccelerationStructureGeometryKHR for BLAS building
    geom_desc.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom_desc.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom_desc.flags = geom_flags;  // e.g., VK_GEOMETRY_OPAQUE_BIT_KHR
    geom_desc.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    
    // Vertex data (positions only, used by BLAS for spatial partitioning)
    geom_desc.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geom_desc.geometry.triangles.vertexData.deviceAddress = vertex_buf->device_address();
    geom_desc.geometry.triangles.vertexStride = sizeof(glm::vec3);  // 12 bytes
    geom_desc.geometry.triangles.maxVertex = vertex_buf->size() / sizeof(glm::vec3) - 1;
    
    // Index data
    geom_desc.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    geom_desc.geometry.triangles.indexData.deviceAddress = index_buf->device_address();
    
    // Transform (identity for now)
    geom_desc.geometry.triangles.transformData.deviceAddress = 0;
}
```

**BLAS Build Process:**
1. Collect `VkAccelerationStructureGeometryKHR` from all geometries in the mesh
2. Call `vkCmdBuildAccelerationStructuresKHR()` with:
   - Input: Array of geometry descriptors (uses buffer device addresses for vertex/index data)
   - Output: BLAS buffer (compact spatial hierarchy)
   - Scratch: Temporary buffer for build algorithm
3. Optionally compact BLAS using query pool to get compacted size
4. Finalize: Release scratch buffer, keep BLAS buffer

**Important:** BLAS building **reads vertex_buf and index_buf via device addresses**, but does NOT read normal_buf or uv_buf (those are for shading only).

---

## TLAS (Top-Level Acceleration Structure) Building

### Instance Buffer Creation (render_vulkan.cpp lines 395-460)

```cpp
// Compute SBT offsets for each parameterized mesh
std::vector<uint32_t> parameterized_mesh_sbt_offsets;
uint32_t offset = 0;
for (const auto &pm : parameterized_meshes) {
    parameterized_mesh_sbt_offsets.push_back(offset);
    offset += meshes[pm.mesh_id]->geometries.size();  // Increment by geometry count
}

// Create upload buffer for instance descriptors
auto upload_instances = vkrt::Buffer::host(
    *device,
    scene.instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT
);
VkAccelerationStructureInstanceKHR *map = 
    reinterpret_cast<VkAccelerationStructureInstanceKHR *>(upload_instances->map());

// Fill instance buffer
for (size_t i = 0; i < scene.instances.size(); ++i) {
    const auto &inst = scene.instances[i];
    std::memset(&map[i], 0, sizeof(VkAccelerationStructureInstanceKHR));
    
    map[i].instanceCustomIndex = i;  // Used in shader as gl_InstanceCustomIndexEXT
    
    // CRITICAL: Maps instance to SBT hit group range
    map[i].instanceShaderBindingTableRecordOffset = 
        parameterized_mesh_sbt_offsets[inst.parameterized_mesh_id];
    
    map[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
    
    // Reference to BLAS (device address)
    map[i].accelerationStructureReference = 
        meshes[parameterized_meshes[inst.parameterized_mesh_id].mesh_id]->handle;
    
    map[i].mask = 0xff;
    
    // Transform matrix (4×3 row-major)
    const glm::mat4 m = glm::transpose(inst.transform);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            map[i].transform.matrix[r][c] = m[r][c];
        }
    }
}
upload_instances->unmap();

// Copy to device buffer
instance_buf = vkrt::Buffer::device(
    *device,
    upload_instances->size(),
    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
);
// ... upload via vkCmdCopyBuffer ...

// Build TLAS
scene_bvh = std::make_unique<vkrt::TopLevelBVH>(*device, instance_buf, scene.instances);
vkBeginCommandBuffer(command_buffer, &begin_info);
scene_bvh->enqueue_build(command_buffer);
vkEndCommandBuffer(command_buffer);
vkQueueSubmit(...);
vkQueueWaitIdle(...);
scene_bvh->finalize();
```

**TLAS Structure:**
- Input: `instance_buf` containing array of `VkAccelerationStructureInstanceKHR`
- Each instance references:
  - BLAS (via device address)
  - Transform matrix (4×3 row-major)
  - SBT hit group offset
  - Instance custom index (passed to shader)

**Critical Insight:** `instanceShaderBindingTableRecordOffset` maps each instance to a **range of hit groups** in the SBT. For an instance using a mesh with N geometries, it maps to SBT entries `[offset, offset+N)`.

---

## Descriptor Set Layout

### Set 0: Scene-Wide Resources (render_vulkan.cpp lines 761-782)

```cpp
desc_layout = vkrt::DescriptorSetLayoutBuilder()
    .add_binding(0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                 VK_SHADER_STAGE_RAYGEN_BIT_KHR)       // TLAS
    .add_binding(1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 VK_SHADER_STAGE_RAYGEN_BIT_KHR)       // render_target
    .add_binding(2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 VK_SHADER_STAGE_RAYGEN_BIT_KHR)       // accum_buffer
    .add_binding(3, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 VK_SHADER_STAGE_RAYGEN_BIT_KHR)       // view_param_buf
    .add_binding(4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 VK_SHADER_STAGE_RAYGEN_BIT_KHR)       // mat_params
    .add_binding(5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 VK_SHADER_STAGE_RAYGEN_BIT_KHR)       // light_params
#ifdef REPORT_RAY_STATS
    .add_binding(6, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                 VK_SHADER_STAGE_RAYGEN_BIT_KHR)       // ray_stats
#endif
    .build(*device);
```

**Descriptor Writes (render_vulkan.cpp lines 975-988):**
```cpp
vkrt::DescriptorSetUpdater()
    .write_acceleration_structure(desc_set, 0, scene_bvh)
    .write_storage_image(desc_set, 1, render_target)
    .write_storage_image(desc_set, 2, accum_buffer)
    .write_ubo(desc_set, 3, view_param_buf)
    .write_ssbo(desc_set, 4, mat_params)
    .write_ssbo(desc_set, 5, light_params)
#ifdef REPORT_RAY_STATS
    .write_storage_image(desc_set, 6, ray_stats)
#endif
    .update(*device);
```

**Set 0 Bindings:**
- **Binding 0:** `accelerationStructureEXT scene` (TLAS)
- **Binding 1:** `layout(rgba8, set=0, binding=1) image2D render_target`
- **Binding 2:** `layout(rgba32f, set=0, binding=2) image2D accum_buffer`
- **Binding 3:** `layout(set=0, binding=3) uniform ViewParams { ... }`
- **Binding 4:** `layout(set=0, binding=4) buffer MaterialParams { ... }`
- **Binding 5:** `layout(set=0, binding=5) buffer LightParams { ... }`
- **Binding 6:** `layout(r16ui, set=0, binding=6) image2D ray_stats` (optional)

---

### Set 1: Texture Array (render_vulkan.cpp lines 791-796)

```cpp
textures_desc_layout = vkrt::DescriptorSetLayoutBuilder()
    .add_binding(0,
                 std::max(textures.size(), size_t(1)),  // Variable count
                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 VK_SHADER_STAGE_RAYGEN_BIT_KHR,
                 VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)  // Variable size!
    .build(*device);
```

**Descriptor Write:**
```cpp
if (!combined_samplers.empty()) {
    updater.write_combined_sampler_array(textures_desc_set, 0, combined_samplers);
}
```

**Set 1 Bindings:**
- **Binding 0:** `layout(set=1, binding=0) uniform sampler2D textures[]` (variable-size array)

**Note:** Vulkan 1.2+ allows variable descriptor count, so texture array size can be determined at runtime.

---

## Shader Binding Table (SBT) Structure

### Hit Group Creation and Record Population (render_vulkan.cpp lines 993-1073)

```cpp
void RenderVulkan::build_shader_binding_table() {
    vkrt::SBTBuilder sbt_builder(&rt_pipeline);
    
    // Ray gen shader record (one entry)
    sbt_builder.set_raygen(vkrt::ShaderRecord("raygen", "raygen", sizeof(uint32_t)))
        .add_miss(vkrt::ShaderRecord("miss", "miss", 0))
        .add_miss(vkrt::ShaderRecord("occlusion_miss", "occlusion_miss", 0));
    
    // Hit group records (one per geometry)
    for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
        const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < meshes[pm.mesh_id]->geometries.size(); ++j) {
            const std::string hg_name = 
                "HitGroup_param_mesh" + std::to_string(i) + "_geom" + std::to_string(j);
            sbt_builder.add_hitgroup(
                vkrt::ShaderRecord(hg_name, "closest_hit", sizeof(HitGroupParams))
            );
        }
    }
    
    shader_table = sbt_builder.build(*device);
    
    // Map SBT buffer to CPU memory for writing
    shader_table.map_sbt();
    
    // Write ray gen shader parameters
    {
        uint32_t *params = reinterpret_cast<uint32_t *>(shader_table.sbt_params("raygen"));
        *params = light_params->size() / sizeof(QuadLight);  // num_lights
    }
    
    // Write hit group shader parameters (one per geometry)
    for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
        const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < meshes[pm.mesh_id]->geometries.size(); ++j) {
            auto &geom = meshes[pm.mesh_id]->geometries[j];
            const std::string hg_name = 
                "HitGroup_param_mesh" + std::to_string(i) + "_geom" + std::to_string(j);
            
            HitGroupParams *params = 
                reinterpret_cast<HitGroupParams *>(shader_table.sbt_params(hg_name));
            
            // Get buffer device addresses (64-bit GPU pointers)
            params->vert_buf = geom.vertex_buf->device_address();
            params->idx_buf = geom.index_buf->device_address();
            
            // Optional buffers
            if (geom.normal_buf) {
                params->normal_buf = geom.normal_buf->device_address();
                params->num_normals = 1;  // Flag: normals present
            } else {
                params->num_normals = 0;  // Flag: no normals
            }
            
            if (geom.uv_buf) {
                params->uv_buf = geom.uv_buf->device_address();
                params->num_uvs = 1;  // Flag: UVs present
            } else {
                params->num_uvs = 0;  // Flag: no UVs
            }
            
            params->material_id = pm.material_ids[j];
        }
    }
    
    // Upload SBT to GPU
    vkBeginCommandBuffer(command_buffer, &begin_info);
    VkBufferCopy copy_cmd = {};
    copy_cmd.size = shader_table.upload_sbt->size();
    vkCmdCopyBuffer(command_buffer,
                    shader_table.upload_sbt->handle(),
                    shader_table.sbt->handle(),
                    1,
                    &copy_cmd);
    vkEndCommandBuffer(command_buffer);
    vkQueueSubmit(...);
    vkQueueWaitIdle(...);
}
```

### Shader Record Layout

**Hit Group Shader Record:**
```
Shader Group Handle (32 bytes, set by Vulkan driver)
─────────────────────────────────────────────────────
HitGroupParams (40 bytes):
  [offset 0]  vert_buf (uint64_t, 8 bytes)      ← Buffer device address
  [offset 8]  idx_buf (uint64_t, 8 bytes)       ← Buffer device address
  [offset 16] normal_buf (uint64_t, 8 bytes)    ← Buffer device address (or 0)
  [offset 24] uv_buf (uint64_t, 8 bytes)        ← Buffer device address (or 0)
  [offset 32] num_normals (uint32_t, 4 bytes)   ← Flag: 1 if present, 0 if not
  [offset 36] num_uvs (uint32_t, 4 bytes)       ← Flag: 1 if present, 0 if not
  [offset 40] material_id (uint32_t, 4 bytes)   ← Material index
─────────────────────────────────────────────────────
Total: 32 (handle) + 44 (params) = 76 bytes (aligned)
```

**SBT Memory Layout:**
```
SBT Buffer:
┌─────────────────────────────────────────────────────────────┐
│ Ray Gen Shader Record                                       │
│   - Shader group handle                                     │
│   - num_lights (uint32_t)                                   │
├─────────────────────────────────────────────────────────────┤
│ Miss Shader Record (index 0)                                │
├─────────────────────────────────────────────────────────────┤
│ Miss Shader Record (index 1 - occlusion)                    │
├─────────────────────────────────────────────────────────────┤
│ Hit Group 0: HitGroup_param_mesh0_geom0                     │
│   - vert_buf (device address of geom 0 vertex buffer)       │
│   - idx_buf (device address of geom 0 index buffer)         │
│   - normal_buf (device address of geom 0 normal buffer)     │
│   - uv_buf (device address of geom 0 UV buffer)             │
│   - num_normals, num_uvs, material_id                       │
├─────────────────────────────────────────────────────────────┤
│ Hit Group 1: HitGroup_param_mesh0_geom1                     │
│   - vert_buf (device address of geom 1 vertex buffer)       │
│   - ... (different addresses than geom 0)                   │
├─────────────────────────────────────────────────────────────┤
│ ... (one record per geometry)                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Shader Resource Access (GLSL)

### Shader Record Access (hit.rchit)

**File:** backends/vulkan/hit.rchit lines 1-60

```glsl
#version 460

#include "util.glsl"

layout(location = PRIMARY_RAY) rayPayloadInEXT RayPayload payload;
hitAttributeEXT vec3 attrib;

// ========== BUFFER_REFERENCE EXTENSION ==========
// Allows dereferencing 64-bit GPU pointers (device addresses)

layout(buffer_reference, scalar) buffer VertexBuffer {
    vec3 v[];  // Array of vec3 positions
};

layout(buffer_reference, scalar) buffer IndexBuffer {
    uvec3 i[];  // Array of uvec3 triangle indices
};

layout(buffer_reference, scalar) buffer NormalBuffer {
    vec3 n[];  // Array of vec3 normals
};

layout(buffer_reference, buffer_reference_align=8) buffer UVBuffer {
    vec2 uv;  // Single vec2 (accessed per-vertex)
};

// ========== SHADER RECORD ==========
// Reads per-geometry data from SBT (HitGroupParams)
layout(shaderRecordEXT, std430) buffer SBT {
    VertexBuffer verts;    // 64-bit device address
    IndexBuffer indices;   // 64-bit device address
    NormalBuffer normals;  // 64-bit device address
    UVBuffer uvs;          // 64-bit device address
    uint32_t num_normals;  // Flag: 1 if present, 0 if not
    uint32_t num_uvs;      // Flag: 1 if present, 0 if not
    uint32_t material_id;  // Material index
};

void main() {
    // Fetch triangle indices from index buffer (via device address)
    const uvec3 idx = indices.i[gl_PrimitiveID];
    
    // Fetch triangle vertices from vertex buffer (via device address)
    const vec3 va = verts.v[idx.x];
    const vec3 vb = verts.v[idx.y];
    const vec3 vc = verts.v[idx.z];
    
    // Compute geometric normal
    const vec3 n = normalize(cross(vb - va, vc - va));
    
    // Interpolate UVs if available
    vec2 uv = vec2(0);
    if (num_uvs > 0) {
        const vec2 uva = uvs[idx.x].uv;
        const vec2 uvb = uvs[idx.y].uv;
        const vec2 uvc = uvs[idx.z].uv;
        uv = (1.f - attrib.x - attrib.y) * uva
            + attrib.x * uvb + attrib.y * uvc;
    }
    
    // Transform normal to world space
    mat3 inv_transp = transpose(mat3(gl_WorldToObjectEXT));
    payload.normal = normalize(inv_transp * n);
    payload.dist = gl_RayTmaxEXT;
    payload.uv = uv;
    payload.material_id = material_id;  // From shader record
}
```

**Key Operations:**
1. `shaderRecordEXT` → Read `HitGroupParams` from SBT
2. Buffer references (`VertexBuffer`, `IndexBuffer`, etc.) → Typed GPU pointers
3. `indices.i[gl_PrimitiveID]` → Dereference index buffer to get triangle indices
4. `verts.v[idx.x/y/z]` → Dereference vertex buffer to get vertex positions
5. `uvs[idx.x].uv` → Dereference UV buffer (note: `UVBuffer` is scalar, accessed per-vertex)
6. Use `material_id` from shader record

**Critical:** All per-geometry data access goes through **buffer device addresses** stored in the shader record. This is completely different from traditional descriptor set binding.

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
│    1. Create upload buffers (host-visible)                              │
│    2. memcpy CPU data → upload buffers                                  │
│    3. Create device buffers (GPU VRAM) with:                            │
│       - VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ← CRITICAL            │
│       - VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_...          │
│    4. vkCmdCopyBuffer (upload → device)                                 │
│    5. No barriers needed (copy is implicit sync)                        │
│                                                                          │
│  Result: vkrt::Geometry { vertex_buf, index_buf, normal_buf, uv_buf }  │
└─────────────────────────┬───────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    BLAS (Bottom-Level BVH) BUILDING                     │
│  For each mesh:                                                         │
│    1. Collect VkAccelerationStructureGeometryKHR from all geometries   │
│       - geometry.triangles.vertexData.deviceAddress ← from vertex_buf   │
│       - geometry.triangles.indexData.deviceAddress ← from index_buf     │
│    2. vkCmdBuildAccelerationStructuresKHR()                             │
│       - Input: Geometry descs (uses device addresses)                   │
│       - Output: BLAS buffer (spatial hierarchy)                         │
│    3. Optional compaction (query pool → compacted_bvh)                  │
│    4. Finalize (release scratch buffers)                                │
│                                                                          │
│  Result: vkrt::TriangleMesh { bvh, handle, geometries[] }              │
└─────────────────────────┬───────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    TLAS (Top-Level BVH) BUILDING                        │
│  1. Compute SBT offsets per parameterized mesh                          │
│  2. Create VkAccelerationStructureInstanceKHR array:                    │
│     - instanceCustomIndex (gl_InstanceCustomIndexEXT)                   │
│     - instanceShaderBindingTableRecordOffset (SBT offset)               │
│     - accelerationStructureReference (BLAS device address)              │
│     - transform (4×3 matrix)                                            │
│  3. Upload instance buffer to GPU                                       │
│  4. vkCmdBuildAccelerationStructuresKHR()                               │
│                                                                          │
│  Result: vkrt::TopLevelBVH { bvh }                                     │
└─────────────────────────┬───────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                  SHADER BINDING TABLE (SBT) CREATION                    │
│  Ray Gen Record:                                                        │
│    - num_lights (uint32_t)                                              │
│                                                                          │
│  Hit Group Records (one per geometry):                                  │
│    - vert_buf ← vkGetBufferDeviceAddressKHR(vertex_buf)  (uint64_t)    │
│    - idx_buf ← vkGetBufferDeviceAddressKHR(index_buf)    (uint64_t)    │
│    - normal_buf ← vkGetBufferDeviceAddressKHR(normal_buf)(uint64_t)    │
│    - uv_buf ← vkGetBufferDeviceAddressKHR(uv_buf)        (uint64_t)    │
│    - num_normals, num_uvs, material_id (3× uint32_t)                   │
│                                                                          │
│  Total records: 1 (raygen) + 2 (miss) + Σ(geometries per mesh)         │
└─────────────────────────┬───────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        RAY TRACING EXECUTION                            │
│  1. vkCmdTraceRaysKHR()                                                 │
│  2. Ray Gen shader:                                                     │
│     - Access set 0 resources (TLAS, render targets, materials, lights) │
│     - Access set 1 resources (textures array)                           │
│     - traceRayEXT()                                                     │
│  3. Ray traversal:                                                      │
│     - Traverse TLAS → Find instance                                     │
│     - Traverse BLAS → Find geometry                                     │
│     - Compute hit point                                                 │
│  4. Closest Hit shader:                                                 │
│     - Vulkan computes: instanceShaderBindingTableRecordOffset + geomID  │
│     - Looks up shader record in SBT                                     │
│     - Reads shaderRecordEXT (HitGroupParams)                            │
│     - Gets buffer device addresses (vert_buf, idx_buf, etc.)            │
│     - Dereferences pointers using buffer_reference                      │
│     - Access: indices.i[gl_PrimitiveID], verts.v[idx]                  │
│     - Access: material_id (from shader record)                          │
│  5. Return to Ray Gen                                                   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Key Findings for Phase 2A Refactor

### 1. Per-Geometry Buffer Explosion (Same as DXR)

**Current:**
- Sponza example: 100 geometries × 4 buffers = **400 separate VkBuffer objects**
- Each buffer has Vulkan overhead (VkDeviceMemory allocation, alignment)
- High SBT memory usage (100 shader records × ~76 bytes = ~7.6KB)

**Refactor Goal:**
- 3 global buffers total (vertex, index, mesh_desc)
- Single shader record for all geometries (or minimal SBT)

---

### 2. Buffer Device Address Usage Must Be Refactored

**Current:** 
- Each geometry's buffers get individual device addresses via `vkGetBufferDeviceAddressKHR`
- Addresses stored in shader records
- Shaders dereference via `buffer_reference`

**Refactor Strategy:**
- Single device address for global vertex buffer
- Single device address for global index buffer
- Use offset arithmetic in shader: `globalVertices[meshDesc.vbOffset + localIndex]`

---

### 3. shaderRecordEXT Must Be Simplified or Removed

**Current:** 
- `shaderRecordEXT` in shader reads `HitGroupParams` (40 bytes per geometry)
- Contains 4 buffer addresses + 3 uint32_t values

**Refactor Strategy Option A:**
- Remove `shaderRecordEXT` entirely
- Use descriptor set bindings for global buffers (set 0)
- Use `gl_InstanceCustomIndexEXT` and `gl_GeometryIndexEXT` for indirection

**Refactor Strategy Option B (Hybrid):**
- Minimal `shaderRecordEXT` with just global buffer addresses (16 bytes total)
- All geometries share same addresses
- Still need `gl_GeometryIndexEXT` for MeshDesc lookup

---

### 4. buffer_reference Extension Still Useful

**Current:** `buffer_reference` dereferences per-geometry buffer addresses

**Refactor:** Can still use `buffer_reference` for global buffers:
```glsl
layout(buffer_reference, scalar) buffer GlobalVertexBuffer {
    Vertex v[];  // Interleaved vertex data
};

layout(buffer_reference, scalar) buffer GlobalIndexBuffer {
    uint i[];  // Global indices (not uvec3!)
};

layout(shaderRecordEXT, std430) buffer SBT {
    GlobalVertexBuffer globalVerts;  // Single address for all geometries
    GlobalIndexBuffer globalIndices;
    // ... MeshDesc buffer address
};
```

Then access via offset: `globalVerts.v[meshDesc.vbOffset + localIndex]`

---

### 5. Descriptor Set 0 Can Include Global Buffers

**Alternative to shaderRecordEXT:**
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

Access via: `uint meshID = gl_GeometryIndexEXT; MeshDesc desc = descs[meshID];`

**Tradeoff:** Descriptor set approach is more "traditional" but requires binding updates. shaderRecordEXT approach is more flexible but requires SBT.

---

### 6. BLAS Building Strategy (Same as DXR)

**Current:** BLAS build references per-geometry `vertex_buf` and `index_buf` device addresses

**Refactor Consideration:**
- BLAS build will still need per-geometry vertex/index data
- **Option A:** Keep temporary per-geometry buffers for BLAS build, discard after
- **Option B:** Use global buffers with base address + offset for BLAS build
  - `geometry.triangles.vertexData.deviceAddress = globalVertexBuf_address + meshDesc.vbOffset * sizeof(Vertex)`

**Recommended:** Option A during Phase 2A (simpler), Option B for future optimization

---

### 7. Instance Mapping (Same as DXR)

**Current:**
```cpp
map[i].instanceShaderBindingTableRecordOffset = parameterized_mesh_sbt_offsets[inst.parameterized_mesh_id];
```

**Refactor:**
- With single hit group, set `instanceShaderBindingTableRecordOffset = 0` for all instances
- Use `gl_InstanceCustomIndexEXT` in shader to look up instance data
- Use `gl_GeometryIndexEXT` to compute which mesh descriptor to use

---

### 8. Material Assignment (Same as DXR)

**Current:** `material_id` stored in `HitGroupParams` per geometry

**Refactor:** Store `materialID` in `MeshDesc` structure, index via `gl_GeometryIndexEXT`

---

## Recommendations for Phase 2A.3 (Vulkan Refactor)

Based on this analysis:

1. **Create Global Buffers:**
   - `global_vertex_buffer` (device address via `vkGetBufferDeviceAddressKHR`)
   - `global_index_buffer` (device address)
   - `mesh_desc_buffer` (device address or descriptor set binding)

2. **Update Shader:**
   - Option A: Use `buffer_reference` with global buffer addresses in `shaderRecordEXT`
   - Option B: Use descriptor set bindings (set 0, bindings 7-9) for global buffers
   - Update hit.rchit with indirection logic using `gl_GeometryIndexEXT`

3. **Simplify SBT:**
   - **If using Option A:** Minimal shader record (16 bytes: 2× uint64_t for global buffer addresses)
   - **If using Option B:** No shader record needed (or just num_lights for raygen)
   - Remove loop over geometries in `build_shader_binding_table()`

4. **Update Descriptor Set 0 (If using Option B):**
   - Add 3 new bindings for global buffers
   - Update `build_shader_descriptor_table()` to write global buffer descriptors

5. **BLAS Building Strategy (CRITICAL PAUSE POINT):**
   - Analyze whether to keep temporary per-geometry buffers for BLAS build
   - Or use global buffers with offset support in `VkAccelerationStructureGeometryKHR`

---

## Files to Modify in Phase 2A.3

- `backends/vulkan/render_vulkan.h` - Add global buffer member variables
- `backends/vulkan/render_vulkan.cpp` - Modify set_scene(), build_shader_binding_table(), build_shader_descriptor_table(), build_raytracing_pipeline()
- `backends/vulkan/hit.rchit` - Remove per-geometry buffer_reference, add global buffers, update main() with indirection
- Possibly other shader files if they access vertex data

---

**Analysis Complete:** Vulkan backend per-geometry buffer architecture fully documented. Ready to proceed to Phase 2A.0.4 (baseline screenshots) and Phase 2A.0.5 (files to modify list), then Phase 2A.3 implementation.
