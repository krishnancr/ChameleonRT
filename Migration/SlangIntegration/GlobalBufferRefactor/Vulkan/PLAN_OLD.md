# Vulkan Global Buffer Refactor - Methodical Implementation Plan# Vulkan Global Buffer Refactor - Methodical Implementation Plan



**Goal:** Update Vulkan backend to use global buffers, validate rendering  **Goal:** Update Vulkan backend to use global buffers, validate rendering  

**Status:** 📋 READY TO IMPLEMENT  **Status:** 📋 READY TO IMPLEMENT  

**Date:** October 14, 2025  **Date:** October 14, 2025  

**Lessons Learned:** DXR implementation complete - applying proven patterns**Lessons Learned:** DXR implementation complete - applying proven patterns



------



## Executive Summary## Executive Summary



This plan applies the **proven DXR global buffer architecture** to Vulkan with careful validation at each step.This plan applies the **proven DXR global buffer architecture** to Vulkan with careful validation at each step.



**Key Differences from DXR:****Key Differences from DXR:**

- Vulkan uses **buffer device addresses** (BDA) instead of SRVs  - Vulkan uses **buffer device addresses** (BDA) instead of SRVs

- Descriptor sets instead of descriptor heaps- Descriptor sets instead of descriptor heaps

- GLSL instead of HLSL- GLSL instead of HLSL

- `shaderRecordEXT` instead of local root signatures- `shaderRecordEXT` instead of local root signatures

- Validation layers provide better debugging than D3D12- Validation layers provide better debugging than D3D12



**Strategy:****Strategy:**

- ✅ Incremental changes (validate after each step)- ✅ Incremental changes (validate after each step)

- ✅ Parallel implementation (keep old path during development)- ✅ Parallel implementation (keep old path during development)

- ✅ Test simple scene first (cube), then complex (Sponza)- ✅ Test simple scene first (cube), then complex (Sponza)

- ✅ Extensive console logging for verification- ✅ Extensive console logging for verification



------



## Architecture Comparison## Architecture Comparison



### Current (Per-Geometry Buffer Device Addresses)### Current (Per-Geometry Buffer Device Addresses)

``````

Vulkan Per-Geometry:Vulkan Per-Geometry:

├── shaderRecordEXT (per hit group)├── shaderRecordEXT (per hit group)

│   ├── vert_buf (uint64_t device address)│   ├── vert_buf (uint64_t device address)

│   ├── idx_buf (uint64_t device address)│   ├── idx_buf (uint64_t device address)

│   ├── normal_buf (uint64_t device address)│   ├── normal_buf (uint64_t device address)

│   ├── uv_buf (uint64_t device address)│   ├── uv_buf (uint64_t device address)

│   ├── num_normals (uint32_t)│   ├── num_normals (uint32_t)

│   ├── num_uvs (uint32_t)│   ├── num_uvs (uint32_t)

│   └── material_id (uint32_t)│   └── material_id (uint32_t)

└── Shader access via buffer_reference└── Shader access via buffer_reference



SBT Size: 7 fields × 381 geometries = complex managementProblems:

```- Complex SBT records (7 fields × 381 geometries)

- buffer_reference extension usage

### Target (Global Buffers)- Per-geometry buffer management

``````

Vulkan Global Buffers:

├── Descriptor Set 0 (scene-wide)### Target (Global Buffers)

│   ├── binding 10: globalVertices (storage buffer)```

│   ├── binding 11: globalIndices (storage buffer)Vulkan Global Buffers:

│   ├── binding 12: globalNormals (storage buffer)├── Descriptor Set 0 (scene-wide)

│   ├── binding 13: globalUVs (storage buffer)│   ├── binding 10: globalVertices (storage buffer)

│   └── binding 14: meshDescs (storage buffer)│   ├── binding 11: globalIndices (storage buffer)

├── shaderRecordEXT (minimal)│   ├── binding 12: globalNormals (storage buffer)

│   └── meshDescIndex (uint32_t)│   ├── binding 13: globalUVs (storage buffer)

└── Shader access via standard storage buffers│   └── binding 14: meshDescs (storage buffer)

├── shaderRecordEXT (minimal)

SBT Size: 1 field × 381 geometries = 80% reduction│   └── meshDescIndex (uint32_t)

```└── Shader access via storage buffer bindings



**Benefits:**Benefits:

✅ Simplified SBT  ✅ Simplified SBT (1 field vs 7)

✅ No buffer_reference extension  ✅ Standard storage buffers (no buffer_reference)

✅ Matches DXR architecture  ✅ Matches DXR architecture

✅ Slang-compatible✅ Slang-compatible

```

---

---

## Critical Indexing Pattern (From DXR Lessons)

## Critical Indexing Pattern (From DXR)

**⚠️ CRITICAL:** Indices are **pre-adjusted to global space** during scene build!

**REMEMBER:** Indices are **pre-adjusted to global space** during scene build!

```glsl

// Load triangle indices (ALREADY GLOBAL)```glsl

uvec3 idx = globalIndices.i[mesh.ibOffset + gl_PrimitiveID];// Load triangle indices (ALREADY GLOBAL)

uvec3 idx = globalIndices.i[mesh.ibOffset + gl_PrimitiveID];

// ✅ CORRECT: Vertices - direct indexing (idx is global)

vec3 va = globalVertices.v[idx.x];// ✅ CORRECT: Vertices - direct indexing (idx is global)

vec3 vb = globalVertices.v[idx.y];vec3 va = globalVertices.v[idx.x];

vec3 vc = globalVertices.v[idx.z];vec3 vb = globalVertices.v[idx.y];

vec3 vc = globalVertices.v[idx.z];

// ✅ CORRECT: UVs - convert to local first

uvec3 local_idx = idx - uvec3(mesh.vbOffset);// ✅ CORRECT: UVs - convert to local first

vec2 uva = globalUVs.uv[mesh.uvOffset + local_idx.x];uvec3 local_idx = idx - uvec3(mesh.vbOffset);

vec2 uvb = globalUVs.uv[mesh.uvOffset + local_idx.y];vec2 uva = globalUVs.uv[mesh.uvOffset + local_idx.x];

vec2 uvc = globalUVs.uv[mesh.uvOffset + local_idx.z];vec2 uvb = globalUVs.uv[mesh.uvOffset + local_idx.y];

vec2 uvc = globalUVs.uv[mesh.uvOffset + local_idx.z];

// ❌ WRONG: Double-offset (this caused the DXR black screen bug!)

vec3 va = globalVertices.v[mesh.vbOffset + idx.x];  // DON'T DO THIS// ❌ WRONG: Double-offset

vec2 uva = globalUVs.uv[mesh.uvOffset + idx.x];     // DON'T DO THISvec3 va = globalVertices.v[mesh.vbOffset + idx.x];  // DON'T DO THIS

```vec2 uva = globalUVs.uv[mesh.uvOffset + idx.x];     // DON'T DO THIS

```

---

---

## Phase 1: Header Updates (5 min)

## Shader Changes

**Goal:** Add global buffer members to RenderVulkan class

**File:** `backends/vulkan/hit.rchit` (and other shader files)

**File:** `backends/vulkan/render_vulkan.h`

### Add New Structs

## Step 1.1: Update HitGroupParams (2 min)

```glsl

**Location:** Line ~15struct Vertex {

    vec3 position;

**BEFORE:**    vec3 normal;

```cpp    vec2 texCoord;

struct HitGroupParams {};

    uint64_t vert_buf = 0;

    uint64_t idx_buf = 0;struct MeshDesc {

    uint64_t normal_buf = 0;    uint vbOffset;

    uint64_t uv_buf = 0;    uint ibOffset;

    uint32_t num_normals = 0;    uint vertexCount;

    uint32_t num_uvs = 0;    uint indexCount;

    uint32_t material_id = 0;    uint materialID;

};};

``````



**AFTER:**### Add Global Buffer Bindings (set0)

```cpp

struct HitGroupParams {```glsl

    uint32_t meshDescIndex = 0;  // Index into meshDescs bufferlayout(binding = 10, set = 0, scalar) readonly buffer GlobalVertices {

};    Vertex vertices[];

```} globalVertices;



**Validation:**layout(binding = 11, set = 0, scalar) readonly buffer GlobalIndices {

- File compiles ✅    uint indices[];  // uint not uvec3

- No other changes needed yet} globalIndices;



---layout(binding = 12, set = 0, std430) readonly buffer MeshDescBuffer {

    MeshDesc descs[];
```
## Step 1.2: Add Global Buffer Members (3 min)} meshDescs;



**Location:** Inside `RenderVulkan` class (after existing buffer members ~line 27)

### Remove buffer_reference and shaderRecordEXT

**ADD (after `light_params`):**

```
cppglsl

    // Global geometry buffers// DELETE THESE:

    std::shared_ptr<vkrt::Buffer> global_vertex_buffer;// layout(buffer_reference, scalar) buffer VertexBuffer { vec3 v[]; };

    std::shared_ptr<vkrt::Buffer> global_index_buffer;// layout(buffer_reference, scalar) buffer IndexBuffer { uvec3 i[]; };

    std::shared_ptr<vkrt::Buffer> global_normal_buffer;// layout(buffer_reference, scalar) buffer NormalBuffer { vec3 n[]; };

    std::shared_ptr<vkrt::Buffer> global_uv_buffer;// layout(buffer_reference, scalar) buffer UVBuffer { vec2 uv[]; };

    std::shared_ptr<vkrt::Buffer> mesh_desc_buffer;// layout(shaderRecordEXT, std430) buffer SBT { ... };

    

    size_t global_vertex_count = 0;

    size_t global_index_count = 0;### Update main() in hit.rchit

    size_t global_normal_count = 0;

    size_t global_uv_count = 0;

    size_t mesh_desc_count = 0;void main()

{ ```

    // Get which geometry we hit

**Validation:**    uint meshID = gl_InstanceCustomIndexEXT;  // Vulkan built-in

- Build succeeds ✅    MeshDesc mesh = meshDescs.descs[meshID];

- Application runs (buffers not used yet) ✅    

    // Fetch triangle indices (offset into global buffer)

**Commit:** `git commit -m "Vulkan: Add global buffer members to RenderVulkan class"`    uint triIndex = mesh.ibOffset + gl_PrimitiveID * 3;

    uint idx0 = globalIndices.indices[triIndex + 0];

---    uint idx1 = globalIndices.indices[triIndex + 1];

    uint idx2 = globalIndices.indices[triIndex + 2];



## Phase 2: Shader Updates (30-45 min)    

    // Fetch vertices

**Goal:** Update GLSL shaders to use global buffers (parallel implementation)    Vertex v0 = globalVertices.vertices[idx0];

    Vertex v1 = globalVertices.vertices[idx1];

**File:** `backends/vulkan/hit.rchit`    Vertex v2 = globalVertices.vertices[idx2];

    

## Step 2.1: Add MeshDesc Structure (5 min)    // Barycentric interpolation

    vec3 barycentrics = vec3(1.0 - gl_HitTEXT.x - gl_HitTEXT.y, gl_HitTEXT.x, gl_HitTEXT.y);

**Location:** After `#include` directives, before existing buffer declarations    

    vec3 position = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;

**ADD:**    vec3 normal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;

```glsl    vec2 texCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;

#version 460    

#extension GL_EXT_ray_tracing : require    // Transform to world space

#extension GL_EXT_scalar_block_layout : require    vec3 worldPos = (gl_ObjectToWorldEXT * vec4(position, 1.0)).xyz;

    vec3 worldNormal = normalize((gl_ObjectToWorldEXT * vec4(normal, 0.0)).xyz);

#include "util.glsl"    

    // Material lookup

// MeshDesc structure (matches CPU-side mesh.h)    uint materialID = mesh.materialID;

struct MeshDesc {    

    uint vbOffset;      // Offset into globalVertices    // Continue with shading...

    uint ibOffset;      // Offset into globalIndices}

    uint normalOffset;  // Offset into globalNormals```

    uint uvOffset;      // Offset into globalUVs

    uint num_normals;   // Count (0 if none)---

    uint num_uvs;       // Count (0 if none)

    uint material_id;   // Material index## C++ Changes

    uint pad;           // Alignment to 32 bytes

};**File:** `backends/vulkan/render_vulkan.cpp`

```

### Add Member Variables

**Validation:**

- Shader compiles ✅```cpp

- No runtime testing yet// In render_vulkan.h

class RenderVulkan {

---    // Existing members...

    

## Step 2.2: Add Global Buffer Declarations (10 min)    // NEW: Global buffers

    vkr::Buffer global_vertex_buffer;

**Location:** After MeshDesc struct    vkr::Buffer global_index_buffer;

    vkr::Buffer mesh_desc_buffer;

**ADD:**};

```glsl```

// Global buffers (bindings 10-14, set 0)

layout(binding = 10, set = 0, scalar) readonly buffer GlobalVertices {### Modify `set_scene()` Function

    vec3 v[];

} globalVertices;**Create global buffers:**



layout(binding = 11, set = 0, scalar) readonly buffer GlobalIndices {```cpp

    uvec3 i[];void RenderVulkan::set_scene(const Scene &scene)

} globalIndices;{

    frame_id = 0;

layout(binding = 12, set = 0, scalar) readonly buffer GlobalNormals {    samples_per_pixel = scene.samples_per_pixel;

    vec3 n[];    

} globalNormals;    // 1. Create Global Vertex Buffer

    global_vertex_buffer = vkr::Buffer::device(

layout(binding = 13, set = 0, scalar) readonly buffer GlobalUVs {        device,

    vec2 uv[];        scene.global_vertices.size() * sizeof(Vertex),

} globalUVs;        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT

    );

layout(binding = 14, set = 0, std430) readonly buffer MeshDescBuffer {    

    MeshDesc descs[];    // Upload via staging buffer

} meshDescs;    auto staging_verts = vkr::Buffer::host(

```        device,

        scene.global_vertices.size() * sizeof(Vertex),

**KEEP existing buffer_reference declarations for now** (parallel implementation)        VK_BUFFER_USAGE_TRANSFER_SRC_BIT

    );

**Validation:**    std::memcpy(staging_verts.map(), scene.global_vertices.data(), staging_verts.size());

- Shader compiles with both old and new declarations ✅    staging_verts.unmap();

- CMake rebuild succeeds ✅    

    // 2. Create Global Index Buffer (similar)

---    global_index_buffer = vkr::Buffer::device(

        device,

## Step 2.3: Add New main() Function (15 min)        scene.global_indices.size() * sizeof(uint32_t),

        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT

**Location:** After existing main(), rename old to main_OLD    );

    // ... staging upload

**RENAME existing main():**    

```glsl    // 3. Create MeshDesc Buffer (similar)

void main_OLD() {    mesh_desc_buffer = vkr::Buffer::device(

    // ... existing code        device,

}        scene.mesh_descriptors.size() * sizeof(MeshDesc),

```        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT

    );

**ADD new main():**    // ... staging upload

```glsl    

void main() {    // Copy staging to device buffers via command buffer

    // Get geometry descriptor from SBT (will be set up in Phase 5)    vkCmdCopyBuffer(cmd_buf, staging_verts.buffer, global_vertex_buffer.buffer, ...);

    const uint meshID = meshDescIndex;  // From shaderRecordEXT    vkCmdCopyBuffer(cmd_buf, staging_indices.buffer, global_index_buffer.buffer, ...);

    MeshDesc mesh = meshDescs.descs[meshID];    vkCmdCopyBuffer(cmd_buf, staging_descs.buffer, mesh_desc_buffer.buffer, ...);

        

    // Load triangle indices (ALREADY GLOBAL - critical!)    // Pipeline barriers to make buffers shader-visible

    const uvec3 idx = globalIndices.i[mesh.ibOffset + gl_PrimitiveID];    // ...

        

    // Load vertex positions (direct indexing - idx is global)    // Continue with BLAS/TLAS building

    const vec3 va = globalVertices.v[idx.x];    // ...

    const vec3 vb = globalVertices.v[idx.y];}

    const vec3 vc = globalVertices.v[idx.z];```

    

    // Compute geometry normal### Update Descriptor Set Layout

    const vec3 n = normalize(cross(vb - va, vc - va));

    **Add bindings for global buffers:**

    // Load UVs (convert to local indices first!)

    vec2 uv = vec2(0);```cpp

    if (mesh.num_uvs > 0) {std::vector<VkDescriptorSetLayoutBinding> bindings;

        const uvec3 local_idx = idx - uvec3(mesh.vbOffset);

        const vec2 uva = globalUVs.uv[mesh.uvOffset + local_idx.x];// Existing bindings (TLAS, render targets, etc.)

        const vec2 uvb = globalUVs.uv[mesh.uvOffset + local_idx.y];// ...

        const vec2 uvc = globalUVs.uv[mesh.uvOffset + local_idx.z];

        uv = (1.f - attrib.x - attrib.y) * uva + attrib.x * uvb + attrib.y * uvc;// NEW: Global buffer bindings

    }bindings.push_back({

        .binding = 10,

    // Transform to world space    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,

    mat3 inv_transp = transpose(mat3(gl_WorldToObjectEXT));    .descriptorCount = 1,

    payload.normal = normalize(inv_transp * n);    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR

    payload.dist = gl_RayTmaxEXT;});

    payload.uv = uv;

    payload.material_id = mesh.material_id;bindings.push_back({

}    .binding = 11,

```    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,

    .descriptorCount = 1,

**Validation:**    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR

- Shader compiles with both main() and main_OLD() ✅});

- Build succeeds ✅

- Application still uses main_OLD() (descriptor sets not set up yet) ✅bindings.push_back({

    .binding = 12,

**Commit:** `git commit -m "Vulkan: Add global buffer shader code (parallel path)"`    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,

    .descriptorCount = 1,

---    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR

});

## Phase 3: C++ Buffer Creation (45-60 min)

// Create layout

**Goal:** Create and upload global buffers to GPUVkDescriptorSetLayoutCreateInfo layoutInfo = {

    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,

**File:** `backends/vulkan/render_vulkan.cpp`    .bindingCount = bindings.size(),

    .pBindings = bindings.data()

## Step 3.1: Add Upload Helper Function (10 min)};

vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &desc_set_layout);

**In render_vulkan.h, add declaration:**```

```cpp

private:### Update Descriptor Set Writes

    void upload_global_buffer(std::shared_ptr<vkrt::Buffer>& gpu_buf,

                             const void* data,**Write descriptors for global buffers:**

                             size_t size,

                             VkBufferUsageFlags usage);```cpp

```std::vector<VkWriteDescriptorSet> writes;



**In render_vulkan.cpp, add implementation:**// Existing writes...

```cpp

void RenderVulkan::upload_global_buffer(std::shared_ptr<vkrt::Buffer>& gpu_buf,// NEW: Global vertex buffer

                                       const void* data,VkDescriptorBufferInfo vertexInfo = {

                                       size_t size,    .buffer = global_vertex_buffer.buffer,

                                       VkBufferUsageFlags usage)    .offset = 0,

{    .range = VK_WHOLE_SIZE

    // Create staging buffer};

    auto staging = vkrt::Buffer::host(writes.push_back({

        *device,    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,

        size,    .dstSet = desc_set,

        VK_BUFFER_USAGE_TRANSFER_SRC_BIT    .dstBinding = 10,

    );    .descriptorCount = 1,

        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,

    // Copy data to staging    .pBufferInfo = &vertexInfo

    std::memcpy(staging->map(), data, size);});

    staging->unmap();

    // Similar for indices (binding 11) and mesh_descs (binding 12)

    // Create device buffer// ...

    gpu_buf = vkrt::Buffer::device(

        *device,vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);

        size,```

        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT

    );### Remove Shader Record Setup

    

    // Copy staging to device via command buffer**Delete:**

    VkCommandBufferBeginInfo begin_info = {};- `vkGetBufferDeviceAddressKHR` calls for per-geometry buffers

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;- Shader record buffer creation and data packing

    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;- Hit group shader record writes

    CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

    **Keep:**

    VkBufferCopy copy_region = {};- Simplified shader binding table (shader identifiers only)

    copy_region.size = size;

    vkCmdCopyBuffer(command_buffer,---

                   staging->handle(),

                   gpu_buf->handle(),## Tasks

                   1,

                   &copy_region);### 2A.3.1: Update GLSL Shaders

    - Add Vertex and MeshDesc structs

    // Barrier to make buffer shader-readable- Add global buffer declarations (binding 10-12, set0)

    VkBufferMemoryBarrier barrier = {};- Remove buffer_reference and shaderRecordEXT

    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;- Update main() with indirection logic

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;### 2A.3.2: Update C++ Buffer Creation

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;- Add global buffer member variables

    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;- Modify `set_scene()` to create global buffers

    barrier.buffer = gpu_buf->handle();- Upload data via staging buffers

    barrier.offset = 0;- Remove per-geometry buffer creation

    barrier.size = VK_WHOLE_SIZE;

    ### 2A.3.3: Update Descriptor Sets

    vkCmdPipelineBarrier(command_buffer,- Add bindings for 3 global buffers

                        VK_PIPELINE_STAGE_TRANSFER_BIT,- Write descriptors for global buffers

                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,

                        0,### 2A.3.4: Simplify Shader Binding Table

                        0, nullptr,- Remove shader record setup code

                        1, &barrier,- Remove buffer address queries

                        0, nullptr);- Keep only shader identifiers in SBT

    

    CHECK_VULKAN(vkEndCommandBuffer(command_buffer));### 2A.3.5: Test and Validate

    - Build and run with test_cube.obj

    VkSubmitInfo submit_info = {};- Compare screenshot to baseline

    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;- Build and run with Sponza

    submit_info.commandBufferCount = 1;- Compare screenshot to baseline

    submit_info.pCommandBuffers = &command_buffer;- Check validation layers (no errors)

    CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));

    CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));---

    

    vkResetCommandPool(device->logical_device(),## Validation Criteria

                      command_pool,

                      VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);- ✅ Shaders compile without errors

}- ✅ Application launches without crashes

```- ✅ Renders test_cube.obj identically to baseline

- ✅ Renders Sponza identically to baseline

**Validation:**- ✅ No Vulkan validation layer errors

- Compiles ✅- ✅ Stable for 1000+ frames

- Not called yet

---

---

## References

## Step 3.2: Upload Global Buffers in set_scene() (30 min)

- **DXR Implementation:** See [../DXR/PLAN.md](../DXR/PLAN.md) for parallel approach

**Location:** In `set_scene()`, after material/light upload, before pipeline building- **Buffer Analysis:** [Buffer_Analysis.md](Buffer_Analysis.md)



**ADD (after light buffer upload, around line 670):**---

```cpp

    // ============================================================================## Next Steps

    // Create Global Buffers

    // ============================================================================After Phase 2A.3 completion:

    std::cout << "Creating global buffers...\n";1. Final cross-backend validation (DXR vs Vulkan)

    2. Performance comparison (before vs after)

    // 1. Global Vertex Buffer (positions)3. Documentation update

    if (!scene.global_vertices.empty()) {4. Begin Phase 2B: Unified Slang Shaders

        global_vertex_count = scene.global_vertices.size();
        
        upload_global_buffer(
            global_vertex_buffer,
            scene.global_vertices.data(),
            scene.global_vertices.size() * sizeof(glm::vec3),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        
        std::cout << "  - globalVertices: " << global_vertex_count << " elements\n";
    }
    
    // 2. Global Index Buffer (uvec3 triangles)
    if (!scene.global_indices.empty()) {
        global_index_count = scene.global_indices.size();
        
        upload_global_buffer(
            global_index_buffer,
            scene.global_indices.data(),
            scene.global_indices.size() * sizeof(glm::uvec3),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        
        std::cout << "  - globalIndices: " << global_index_count << " elements\n";
    }
    
    // 3. Global Normal Buffer
    if (!scene.global_normals.empty()) {
        global_normal_count = scene.global_normals.size();
        
        upload_global_buffer(
            global_normal_buffer,
            scene.global_normals.data(),
            scene.global_normals.size() * sizeof(glm::vec3),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        
        std::cout << "  - globalNormals: " << global_normal_count << " elements\n";
    }
    
    // 4. Global UV Buffer
    if (!scene.global_uvs.empty()) {
        global_uv_count = scene.global_uvs.size();
        
        upload_global_buffer(
            global_uv_buffer,
            scene.global_uvs.data(),
            scene.global_uvs.size() * sizeof(glm::vec2),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        
        std::cout << "  - globalUVs: " << global_uv_count << " elements\n";
    }
    
    // 5. MeshDesc Buffer
    if (!scene.mesh_descriptors.empty()) {
        mesh_desc_count = scene.mesh_descriptors.size();
        
        upload_global_buffer(
            mesh_desc_buffer,
            scene.mesh_descriptors.data(),
            scene.mesh_descriptors.size() * sizeof(MeshDesc),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        
        std::cout << "  - meshDescs: " << mesh_desc_count << " elements\n";
    }
    
    std::cout << "Global buffers created successfully\n";
```

**Validation:**
- Build succeeds ✅
- Run with cube: Console shows global buffer creation ✅
- Application still renders (using old path) ✅
- No crashes ✅

**Commit:** `git commit -m "Vulkan: Create and upload global buffers to GPU"`

---

## Phase 4: Descriptor Set Updates (30-45 min)

**Goal:** Add global buffers to descriptor set layout and bind them

**File:** `backends/vulkan/render_vulkan.cpp`

## Step 4.1: Update Descriptor Set Layout (15 min)

**Location:** In `build_shader_descriptor_table()`, around line 860

**FIND:**
```cpp
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    // ... existing bindings (TLAS at 0, render_target at 1, etc.)
```

**ADD (after existing bindings, before creating layout):**
```cpp
    // Global geometry buffers (bindings 10-14)
    bindings.push_back({
        .binding = 10,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        .pImmutableSamplers = nullptr
    });
    
    bindings.push_back({
        .binding = 11,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        .pImmutableSamplers = nullptr
    });
    
    bindings.push_back({
        .binding = 12,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        .pImmutableSamplers = nullptr
    });
    
    bindings.push_back({
        .binding = 13,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        .pImmutableSamplers = nullptr
    });
    
    bindings.push_back({
        .binding = 14,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        .pImmutableSamplers = nullptr
    });
    
    std::cout << "Descriptor set layout: added global buffer bindings 10-14\n";
```

**Validation:**
- Build succeeds ✅
- Console shows layout created with new bindings ✅

---

## Step 4.2: Update Descriptor Pool (10 min)

**Location:** Before creating descriptor set layout

**FIND:**
```cpp
    VkDescriptorPoolSize pool_sizes[] = {
        // ... existing pool sizes
    };
```

**ADD (increase storage buffer count):**
```cpp
    // Find existing STORAGE_BUFFER pool size and increase count by 5
    // OR add new entry:
    {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 10  // Existing + 5 new global buffers
    }
```

**Validation:**
- Build succeeds ✅

---

## Step 4.3: Write Descriptor Sets for Global Buffers (15 min)

**Location:** In `build_shader_descriptor_table()`, after existing descriptor writes

**ADD:**
```cpp
    std::cout << "Writing descriptors for global buffers...\n";
    
    // Global vertex buffer (binding 10)
    if (global_vertex_buffer) {
        VkDescriptorBufferInfo vertex_info = {};
        vertex_info.buffer = global_vertex_buffer->handle();
        vertex_info.offset = 0;
        vertex_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet vertex_write = {};
        vertex_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vertex_write.dstSet = desc_set;
        vertex_write.dstBinding = 10;
        vertex_write.dstArrayElement = 0;
        vertex_write.descriptorCount = 1;
        vertex_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vertex_write.pBufferInfo = &vertex_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &vertex_write, 0, nullptr);
        std::cout << "  - Binding 10 (globalVertices): " << global_vertex_count << " elements\n";
    }
    
    // Global index buffer (binding 11)
    if (global_index_buffer) {
        VkDescriptorBufferInfo index_info = {};
        index_info.buffer = global_index_buffer->handle();
        index_info.offset = 0;
        index_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet index_write = {};
        index_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        index_write.dstSet = desc_set;
        index_write.dstBinding = 11;
        index_write.dstArrayElement = 0;
        index_write.descriptorCount = 1;
        index_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        index_write.pBufferInfo = &index_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &index_write, 0, nullptr);
        std::cout << "  - Binding 11 (globalIndices): " << global_index_count << " elements\n";
    }
    
    // Global normal buffer (binding 12)
    if (global_normal_buffer) {
        VkDescriptorBufferInfo normal_info = {};
        normal_info.buffer = global_normal_buffer->handle();
        normal_info.offset = 0;
        normal_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet normal_write = {};
        normal_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        normal_write.dstSet = desc_set;
        normal_write.dstBinding = 12;
        normal_write.dstArrayElement = 0;
        normal_write.descriptorCount = 1;
        normal_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        normal_write.pBufferInfo = &normal_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &normal_write, 0, nullptr);
        std::cout << "  - Binding 12 (globalNormals): " << global_normal_count << " elements\n";
    }
    
    // Global UV buffer (binding 13)
    if (global_uv_buffer) {
        VkDescriptorBufferInfo uv_info = {};
        uv_info.buffer = global_uv_buffer->handle();
        uv_info.offset = 0;
        uv_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet uv_write = {};
        uv_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uv_write.dstSet = desc_set;
        uv_write.dstBinding = 13;
        uv_write.dstArrayElement = 0;
        uv_write.descriptorCount = 1;
        uv_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        uv_write.pBufferInfo = &uv_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &uv_write, 0, nullptr);
        std::cout << "  - Binding 13 (globalUVs): " << global_uv_count << " elements\n";
    }
    
    // MeshDesc buffer (binding 14)
    if (mesh_desc_buffer) {
        VkDescriptorBufferInfo mesh_info = {};
        mesh_info.buffer = mesh_desc_buffer->handle();
        mesh_info.offset = 0;
        mesh_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet mesh_write = {};
        mesh_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        mesh_write.dstSet = desc_set;
        mesh_write.dstBinding = 14;
        mesh_write.dstArrayElement = 0;
        mesh_write.descriptorCount = 1;
        mesh_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        mesh_write.pBufferInfo = &mesh_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &mesh_write, 0, nullptr);
        std::cout << "  - Binding 14 (meshDescs): " << mesh_desc_count << " elements\n";
    }
    
    std::cout << "Global buffer descriptors written successfully\n";
```

**Validation:**
- Build succeeds ✅
- Console shows all descriptors written ✅
- No Vulkan validation errors ✅

**Commit:** `git commit -m "Vulkan: Add global buffers to descriptor sets"`

---

## Phase 5: SBT Update (20-30 min)

**Goal:** Update shader binding table to pass meshDescIndex

**File:** `backends/vulkan/render_vulkan.cpp`

## Step 5.1: Update build_shader_binding_table() (20 min)

**Location:** `build_shader_binding_table()`, around line 993

**FIND:**
```cpp
    for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
        const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < meshes[pm.mesh_id]->geometries.size(); ++j) {
            auto &geom = meshes[pm.mesh_id]->geometries[j];
            const std::string hg_name =
                "HitGroup_param_mesh" + std::to_string(i) + "_geom" + std::to_string(j);

            HitGroupParams *params =
                reinterpret_cast<HitGroupParams *>(shader_table.sbt_params(hg_name));

            params->vert_buf = meshes[pm.mesh_id]->geometries[j].vertex_buf->device_address();
            // ... (7 fields)
        }
    }
```

**REPLACE WITH:**
```cpp
    std::cout << "Writing meshDescIndex to SBT...\n";
    
    size_t mesh_desc_index = 0;
    for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
        const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < meshes[pm.mesh_id]->geometries.size(); ++j) {
            const std::string hg_name =
                "HitGroup_param_mesh" + std::to_string(i) + "_geom" + std::to_string(j);

            HitGroupParams *params =
                reinterpret_cast<HitGroupParams *>(shader_table.sbt_params(hg_name));

            params->meshDescIndex = static_cast<uint32_t>(mesh_desc_index);
            
            // Debug output for first few
            if (mesh_desc_index < 5) {
                std::cout << "  HitGroup[" << mesh_desc_index << "] = param_mesh" << i 
                          << "_geom" << j << " -> meshDescIndex=" << mesh_desc_index << "\n";
            }
            
            mesh_desc_index++;
        }
    }
    
    std::cout << "SBT updated: " << mesh_desc_index << " hit groups configured\n";
```

**Validation:**
- Build succeeds ✅
- Console shows SBT mapping ✅

**Commit:** `git commit -m "Vulkan: Update SBT to pass meshDescIndex"`

---

## Phase 6: Shader Finalization (10 min)

**Goal:** Switch from old to new shader path

**File:** `backends/vulkan/hit.rchit`

## Step 6.1: Update shaderRecordEXT Declaration (5 min)

**Location:** After global buffer declarations

**FIND (old shaderRecordEXT):**
```glsl
layout(shaderRecordEXT, std430) buffer SBT {
    VertexBuffer verts;
    IndexBuffer indices;
    NormalBuffer normals;
    UVBuffer uvs;
    uint32_t num_normals;
    uint32_t num_uvs;
    uint32_t material_id;
};
```

**REPLACE WITH:**
```glsl
layout(shaderRecordEXT, std430) buffer SBT {
    uint32_t meshDescIndex;
};
```

**Validation:**
- Shader compiles ✅

---

## Step 6.2: Remove Old Code (5 min)

**DELETE:**
- Old `main_OLD()` function
- Old buffer_reference declarations
- Any commented-out old code

**Validation:**
- Shader compiles ✅
- Only new main() remains ✅

**Commit:** `git commit -m "Vulkan: Finalize shader - remove old path"`

---

## Phase 7: Testing & Validation (30-60 min)

**Goal:** Verify rendering correctness

### Test 7.1: Build Verification (5 min)

```powershell
cmake --build build --config Debug
```

**Expected:**
- Clean build ✅
- No warnings ✅
- All shaders compile ✅

---

### Test 7.2: Simple Scene (Cube) (10 min)

```powershell
.\build\Debug\chameleonrt.exe vulkan "C:\Demo\Assets\cube\cube.obj"
```

**Expected Console Output:**
```
Creating global buffers...
  - globalVertices: 24 elements
  - globalIndices: 12 elements
  - globalUVs: 24 elements
  - meshDescs: 1 elements
Global buffers created successfully
Descriptor set layout: added global buffer bindings 10-14
Writing descriptors for global buffers...
  - Binding 10 (globalVertices): 24 elements
  - Binding 11 (globalIndices): 12 elements
  - Binding 13 (globalUVs): 24 elements
  - Binding 14 (meshDescs): 1 elements
Global buffer descriptors written successfully
Writing meshDescIndex to SBT...
  HitGroup[0] = param_mesh0_geom0 -> meshDescIndex=0
SBT updated: 1 hit groups configured
```

**Expected Rendering:**
- Cube renders correctly ✅
- Textures visible (if textured) ✅
- No black screen ✅
- No validation errors ✅

**If Issues:**
- Check console for Vulkan validation errors
- Verify buffer sizes match expectations
- Check descriptor bindings are correct

---

### Test 7.3: Complex Scene (Sponza) (15 min)

```powershell
.\build\Debug\chameleonrt.exe vulkan "C:\Demo\Assets\sponza\sponza.obj"
```

**Expected Console Output:**
```
Creating global buffers...
  - globalVertices: 184406 elements
  - globalIndices: 262267 elements
  - globalUVs: 184406 elements
  - meshDescs: 381 elements
Global buffers created successfully
...
Writing meshDescIndex to SBT...
  HitGroup[0] = param_mesh0_geom0 -> meshDescIndex=0
  HitGroup[1] = param_mesh0_geom1 -> meshDescIndex=1
  HitGroup[2] = param_mesh0_geom2 -> meshDescIndex=2
  HitGroup[3] = param_mesh0_geom3 -> meshDescIndex=3
  HitGroup[4] = param_mesh0_geom4 -> meshDescIndex=4
SBT updated: 381 hit groups configured
```

**Expected Rendering:**
- Sponza renders correctly ✅
- All 381 geometries visible ✅
- Textures correct ✅
- Materials correct ✅
- No validation errors ✅

**Performance Check:**
- Frame rate similar to baseline ✅
- No stuttering ✅

---

## Success Criteria Checklist

- [ ] **Architecture**
  - [ ] Global buffers created (5 buffers)
  - [ ] Descriptor sets updated (bindings 10-14)
  - [ ] SBT simplified (meshDescIndex only)
  - [ ] Shader uses storage buffers (no buffer_reference)

- [ ] **Correctness**
  - [ ] Cube renders identically to baseline
  - [ ] Sponza renders identically to baseline
  - [ ] No validation errors
  - [ ] No crashes or memory leaks

- [ ] **Code Quality**
  - [ ] Old code removed
  - [ ] Console logging informative
  - [ ] Code follows DXR patterns
  - [ ] Documentation complete

- [ ] **Performance**
  - [ ] Frame rate similar to baseline
  - [ ] No stuttering or hitches
  - [ ] Stable over extended runtime

---

## Estimated Timeline

| Phase | Task | Time | Cumulative |
|-------|------|------|------------|
| 1 | Header updates | 5 min | 5 min |
| 2 | Shader updates | 45 min | 50 min |
| 3 | Buffer creation | 60 min | 110 min |
| 4 | Descriptor sets | 45 min | 155 min |
| 5 | SBT update | 30 min | 185 min |
| 6 | Shader finalization | 10 min | 195 min |
| 7 | Testing | 60 min | 255 min |

**Total:** ~4-5 hours (with validation gates)

**Recommendation:** Split across 2 sessions:
- Session 1: Phases 1-4 (2.5 hours)
- Session 2: Phases 5-7 (2 hours)

---

## References

- **DXR Completion Report:** `../DXR/COMPLETION_REPORT.md`
- **DXR Technical Notes:** `../DXR/TECHNICAL_NOTES.md`
- **DXR Quick Reference:** `../DXR/QUICK_REFERENCE.md`
- **Vulkan Buffer Analysis:** `Buffer_Analysis.md`
- **Scene Global Buffers:** `../../util/scene.h`

---

**Ready to implement!** 🚀

Follow each phase carefully, validate after each step, and commit frequently. The DXR implementation proves this pattern works - now we're applying it to Vulkan with the lessons learned!
