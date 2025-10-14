# Vulkan Global Buffer Refactor - Implementation Plan

**Goal:** Update Vulkan backend to use global buffers, validate rendering  
**Status:** 📋 READY TO IMPLEMENT  
**Date:** October 14, 2025  
**Lessons Learned:** DXR implementation complete - applying proven patterns

**⚠️ FOLLOWS DXR ORDER EXACTLY** - This plan mirrors the successful DXR sequence

---

## Executive Summary

This plan applies the **proven DXR global buffer architecture** to Vulkan with careful validation at each step.

**Key Differences from DXR:**
- Vulkan uses **buffer device addresses** (BDA) instead of SRVs
- Descriptor sets instead of descriptor heaps
- GLSL instead of HLSL
- `shaderRecordEXT` instead of local root signatures
- Validation layers provide better debugging than D3D12

**Strategy:**
- ✅ Incremental changes (validate after each step)
- ✅ Parallel implementation (keep old path during development)
- ✅ Test simple scene first (cube), then complex (Sponza)
- ✅ Extensive console logging for verification
- ✅ **FOLLOW DXR ORDER:** Headers → Upload → Bind → Shaders → SBT → Test

---

## Architecture Comparison

### Current (Per-Geometry Buffer Device Addresses)
```
Vulkan Per-Geometry:
├── shaderRecordEXT (per hit group)
│   ├── vert_buf (uint64_t device address)
│   ├── idx_buf (uint64_t device address)
│   ├── normal_buf (uint64_t device address)
│   ├── uv_buf (uint64_t device address)
│   ├── num_normals (uint32_t)
│   ├── num_uvs (uint32_t)
│   └── material_id (uint32_t)
└── Shader access via buffer_reference

Problems:
- Complex SBT records (7 fields × 381 geometries)
- buffer_reference extension usage
- Per-geometry buffer management
```

### Target (Global Buffers)
```
Vulkan Global Buffers:
├── Descriptor Set 0 (scene-wide)
│   ├── binding 10: globalVertices (storage buffer)
│   ├── binding 11: globalIndices (storage buffer)
│   ├── binding 12: globalNormals (storage buffer)
│   ├── binding 13: globalUVs (storage buffer)
│   └── binding 14: meshDescs (storage buffer)
├── shaderRecordEXT (minimal)
│   └── meshDescIndex (uint32_t)
└── Shader access via storage buffer bindings

Benefits:
✅ Simplified SBT (1 field vs 7)
✅ Standard storage buffers (no buffer_reference)
✅ Matches DXR architecture
✅ Slang-compatible
```

---

## Critical Indexing Pattern (From DXR)

**⚠️ CRITICAL:** Indices are **pre-adjusted to global space** during scene build!

```glsl
// Load triangle indices (ALREADY GLOBAL)
uvec3 idx = globalIndices.i[mesh.ibOffset + gl_PrimitiveID];

// ✅ CORRECT: Vertices - direct indexing (idx is global)
vec3 va = globalVertices.v[idx.x];
vec3 vb = globalVertices.v[idx.y];
vec3 vc = globalVertices.v[idx.z];

// ✅ CORRECT: UVs - convert to local first
uvec3 local_idx = idx - uvec3(mesh.vbOffset);
vec2 uva = globalUVs.uv[mesh.uvOffset + local_idx.x];
vec2 uvb = globalUVs.uv[mesh.uvOffset + local_idx.y];
vec2 uvc = globalUVs.uv[mesh.uvOffset + local_idx.z];

// ❌ WRONG: Double-offset (this caused the DXR black screen bug!)
vec3 va = globalVertices.v[mesh.vbOffset + idx.x];  // DON'T DO THIS
vec2 uva = globalUVs.uv[mesh.uvOffset + idx.x];     // DON'T DO THIS
```

---

## DXR Order Reference

For comparison, here's what we did in DXR (PROVEN SUCCESSFUL):

1. ✅ **Phase 1:** Update headers (add member variables)
2. ✅ **Phase 2:** Create and upload global buffers to GPU
3. ✅ **Phase 3:** Add global buffers to descriptor heap  
4. ✅ **Phase 4:** Update shaders (parallel implementation)
5. ✅ **Phase 5:** Simplify SBT (meshDescIndex only)
6. ✅ **Phase 6:** Test and validate

**Vulkan will follow this EXACT sequence!**

---

## Phase 1: Header Updates (5 min)

**Goal:** Add global buffer members to RenderVulkan class  
**File:** `backends/vulkan/render_vulkan.h`  
**DXR Parallel:** ✅ Matches DXR Phase 1

## Step 1.1: Update HitGroupParams (2 min)

**Location:** Line ~15

**BEFORE:**
```cpp
struct HitGroupParams {
    uint64_t vert_buf = 0;
    uint64_t idx_buf = 0;
    uint64_t normal_buf = 0;
    uint64_t uv_buf = 0;
    uint32_t num_normals = 0;
    uint32_t num_uvs = 0;
    uint32_t material_id = 0;
};
```

**AFTER:**
```cpp
struct HitGroupParams {
    uint32_t meshDescIndex = 0;  // Index into meshDescs buffer
};
```

**Validation:**
- File compiles ✅

---

## Step 1.2: Add Global Buffer Members (3 min)

**Location:** Inside `RenderVulkan` class (after existing buffer members ~line 27)

**ADD:**
```cpp
    // Global geometry buffers
    std::shared_ptr<vkrt::Buffer> global_vertex_buffer;
    std::shared_ptr<vkrt::Buffer> global_index_buffer;
    std::shared_ptr<vkrt::Buffer> global_normal_buffer;
    std::shared_ptr<vkrt::Buffer> global_uv_buffer;
    std::shared_ptr<vkrt::Buffer> mesh_desc_buffer;
    
    size_t global_vertex_count = 0;
    size_t global_index_count = 0;
    size_t global_normal_count = 0;
    size_t global_uv_count = 0;
    size_t mesh_desc_count = 0;
```

**Validation:**
- Build succeeds ✅
- Application runs (buffers not used yet) ✅

**Commit:** `git commit -m "Vulkan Phase 1: Add global buffer members"`

---

## Phase 2: Upload Global Buffers (45-60 min)

**Goal:** Create GPU buffers and upload scene data  
**File:** `backends/vulkan/render_vulkan.cpp`  
**DXR Parallel:** ✅ Matches DXR Phase 2 - Upload BEFORE shaders

**⚠️ KEY INSIGHT:** Scene class already built `scene.global_vertices` etc. in `util/scene.cpp`. We just need to upload them to GPU!

## Step 2.1: Add Upload Helper Function (10 min)

**In render_vulkan.h, add declaration:**
```cpp
private:
    void upload_global_buffer(std::shared_ptr<vkrt::Buffer>& gpu_buf,
                             const void* data,
                             size_t size,
                             VkBufferUsageFlags usage);
```

**In render_vulkan.cpp, add implementation:**
```cpp
void RenderVulkan::upload_global_buffer(std::shared_ptr<vkrt::Buffer>& gpu_buf,
                                       const void* data,
                                       size_t size,
                                       VkBufferUsageFlags usage)
{
    // Create staging buffer
    auto staging = vkrt::Buffer::host(
        *device,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    );
    
    // Copy data to staging
    std::memcpy(staging->map(), data, size);
    staging->unmap();
    
    // Create device buffer
    gpu_buf = vkrt::Buffer::device(
        *device,
        size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    
    // Copy staging to device via command buffer
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));
    
    VkBufferCopy copy_region = {};
    copy_region.size = size;
    vkCmdCopyBuffer(command_buffer,
                   staging->handle(),
                   gpu_buf->handle(),
                   1,
                   &copy_region);
    
    // Barrier to make buffer shader-readable
    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = gpu_buf->handle();
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        0,
                        0, nullptr,
                        1, &barrier,
                        0, nullptr);
    
    CHECK_VULKAN(vkEndCommandBuffer(command_buffer));
    
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
    CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));
    
    vkResetCommandPool(device->logical_device(),
                      command_pool,
                      VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
}
```

**Validation:**
- Compiles ✅

---

## Step 2.2: Upload Global Buffers in set_scene() (40 min)

**Location:** In `set_scene()`, after material/light upload, before pipeline building (around line 670)

**ADD:**
```cpp
    // ============================================================================
    // Create Global Buffers (matches DXR Phase 2)
    // ============================================================================
    std::cout << "\n[PHASE 2] Creating global buffers from scene data...\n";
    
    // 1. Global Vertex Buffer (positions)
    if (!scene.global_vertices.empty()) {
        global_vertex_count = scene.global_vertices.size();
        
        upload_global_buffer(
            global_vertex_buffer,
            scene.global_vertices.data(),
            scene.global_vertices.size() * sizeof(glm::vec3),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        
        std::cout << "  ✅ globalVertices: " << global_vertex_count << " elements ("
                  << (global_vertex_count * sizeof(glm::vec3)) << " bytes)\n";
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
        
        std::cout << "  ✅ globalIndices: " << global_index_count << " triangles ("
                  << (global_index_count * sizeof(glm::uvec3)) << " bytes)\n";
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
        
        std::cout << "  ✅ globalNormals: " << global_normal_count << " elements ("
                  << (global_normal_count * sizeof(glm::vec3)) << " bytes)\n";
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
        
        std::cout << "  ✅ globalUVs: " << global_uv_count << " elements ("
                  << (global_uv_count * sizeof(glm::vec2)) << " bytes)\n";
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
        
        std::cout << "  ✅ meshDescs: " << mesh_desc_count << " descriptors ("
                  << (mesh_desc_count * sizeof(MeshDesc)) << " bytes)\n";
    }
    
    std::cout << "[PHASE 2] Global buffers created successfully\n\n";
```

**Expected Console Output (Cube):**
```
[PHASE 2] Creating global buffers from scene data...
  ✅ globalVertices: 24 elements (288 bytes)
  ✅ globalIndices: 12 triangles (144 bytes)
  ✅ globalUVs: 24 elements (192 bytes)
  ✅ meshDescs: 1 descriptors (32 bytes)
[PHASE 2] Global buffers created successfully
```

**Expected Console Output (Sponza):**
```
[PHASE 2] Creating global buffers from scene data...
  ✅ globalVertices: 184406 elements (2212872 bytes)
  ✅ globalIndices: 262267 triangles (3147204 bytes)
  ✅ globalUVs: 184406 elements (1475248 bytes)
  ✅ meshDescs: 381 descriptors (12192 bytes)
[PHASE 2] Global buffers created successfully
```

**Validation:**
- Build succeeds ✅
- Run with cube: Console shows buffer creation ✅
- Application still renders (using old path) ✅
- No crashes ✅

**Commit:** `git commit -m "Vulkan Phase 2: Upload global buffers to GPU"`

---

## Phase 3: Descriptor Set Updates (30-45 min)

**Goal:** Bind global buffers to descriptor sets  
**File:** `backends/vulkan/render_vulkan.cpp`  
**DXR Parallel:** ✅ Matches DXR Phase 3 - Bind BEFORE shader changes

## Step 3.1: Update Descriptor Set Layout (15 min)

**Location:** In `build_shader_descriptor_table()`, around line 860

**FIND:**
```cpp
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    // ... existing bindings (TLAS at 0, render_target at 1, etc.)
```

**ADD (after existing bindings):**
```cpp
    std::cout << "\n[PHASE 3] Adding global buffer descriptor bindings...\n";
    
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
    
    std::cout << "  ✅ Added bindings 10-14 for global buffers\n";
```

---

## Step 3.2: Update Descriptor Pool (5 min)

**Location:** Before creating descriptor set layout

**FIND:**
```cpp
    VkDescriptorPoolSize pool_sizes[] = {
        // ... existing pool sizes
    };
```

**MODIFY:** Increase storage buffer count by 5
```cpp
    // Find existing STORAGE_BUFFER entry and increase descriptorCount by 5
    // OR add new entry if none exists
```

---

## Step 3.3: Write Descriptor Sets (20 min)

**Location:** In `build_shader_descriptor_table()`, after existing descriptor writes

**ADD:**
```cpp
    std::cout << "[PHASE 3] Writing global buffer descriptors...\n";
    
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
        std::cout << "  ✅ Binding 10 (globalVertices): " << global_vertex_count << " elements\n";
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
        std::cout << "  ✅ Binding 11 (globalIndices): " << global_index_count << " elements\n";
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
        std::cout << "  ✅ Binding 12 (globalNormals): " << global_normal_count << " elements\n";
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
        std::cout << "  ✅ Binding 13 (globalUVs): " << global_uv_count << " elements\n";
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
        std::cout << "  ✅ Binding 14 (meshDescs): " << mesh_desc_count << " elements\n";
    }
    
    std::cout << "[PHASE 3] Global buffer descriptors bound successfully\n\n";
```

**Expected Console Output:**
```
[PHASE 3] Adding global buffer descriptor bindings...
  ✅ Added bindings 10-14 for global buffers
[PHASE 3] Writing global buffer descriptors...
  ✅ Binding 10 (globalVertices): 24 elements
  ✅ Binding 11 (globalIndices): 12 elements
  ✅ Binding 13 (globalUVs): 24 elements
  ✅ Binding 14 (meshDescs): 1 elements
[PHASE 3] Global buffer descriptors bound successfully
```

**Validation:**
- Build succeeds ✅
- Console shows descriptors written ✅
- No Vulkan validation errors ✅

**Commit:** `git commit -m "Vulkan Phase 3: Bind global buffers to descriptor sets"`

---

## Phase 4: Shader Updates + Descriptor Flattening (60-75 min)

**Goal:** Update GLSL shaders to use global buffers AND flatten descriptor sets to match DXR architecture  
**Files:** `backends/vulkan/hit.rchit`, `backends/vulkan/render_vulkan.cpp`, `backends/vulkan/render_vulkan.h`  
**DXR Parallel:** ✅ Matches DXR Phase 4 - Shaders AFTER buffers uploaded and bound

**⚠️ CRITICAL LESSON FROM PHASE 3 CLEANUP:**
- Descriptor layout changes and shader updates MUST be done together
- Compiled SPIRV contains hardcoded set/binding references
- Cannot change C++ descriptor layout without recompiling shaders

**Strategy:**
1. Update GLSL shaders to use global buffers AND move textures to Set 0 binding 30
2. Recompile GLSL → SPIRV
3. Update C++ descriptor layout to match (flatten to single Set 0)
4. Result: Perfect architectural alignment with DXR (single set/space)

---

## Step 4.1: Add MeshDesc Structure (5 min)

**Location:** After `#include` directives, before existing buffer declarations

**ADD:**
```glsl
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : require

#include "util.glsl"

// MeshDesc structure (matches CPU-side mesh.h)
struct MeshDesc {
    uint vbOffset;      // Offset into globalVertices
    uint ibOffset;      // Offset into globalIndices
    uint normalOffset;  // Offset into globalNormals
    uint uvOffset;      // Offset into globalUVs
    uint num_normals;   // Count (0 if none)
    uint num_uvs;       // Count (0 if none)
    uint material_id;   // Material index
    uint pad;           // Alignment to 32 bytes
};
```

---

## Step 4.2: Add Global Buffer Declarations + Move Textures (15 min)

**Location:** After MeshDesc struct

**ADD:**
```glsl
// PHASE 4: Global buffers (bindings 10-14, set 0)
layout(binding = 10, set = 0, scalar) readonly buffer GlobalVertices {
    vec3 v[];
} globalVertices;

layout(binding = 11, set = 0, scalar) readonly buffer GlobalIndices {
    uvec3 i[];
} globalIndices;

layout(binding = 12, set = 0, scalar) readonly buffer GlobalNormals {
    vec3 n[];
} globalNormals;

layout(binding = 13, set = 0, scalar) readonly buffer GlobalUVs {
    vec2 uv[];
} globalUVs;

layout(binding = 14, set = 0, std430) readonly buffer MeshDescBuffer {
    MeshDesc descs[];
} meshDescs;
```

**FIND (textures in Set 1):**
```glsl
layout(binding = 0, set = 1) uniform sampler2D textures[];
```

**REPLACE WITH (textures in Set 0):**
```glsl
// PHASE 4: Textures moved to Set 0 binding 30 (matches DXR register t30)
layout(binding = 30, set = 0) uniform sampler2D textures[];
```

**DELETE:**
- Old buffer_reference declarations (VertexBuffer, IndexBuffer, etc.)
- Old shaderRecordEXT with 7 fields

**Validation:**
- Shader compiles with new declarations ✅
- Textures reference `set = 0, binding = 30` ✅

---

## Step 4.3: Update shaderRecordEXT and main() (15 min)

**Location:** Update SBT declaration and implement new main()

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

**APPEND WITH (simplified SBT):**
```glsl
// PHASE 4: Simplified SBT record (matches Phase 5 expectation)
layout(shaderRecordEXT, std430) buffer SBT {
    uint32_t meshDescIndex;
};
```

**APPEND to  main() function:**
```glsl
void main() {
    // PHASE 4: Global buffer implementation
    // Get geometry descriptor from SBT
    const uint meshID = meshDescIndex;  // From shaderRecordEXT
    MeshDesc mesh = meshDescs.descs[meshID];
    
    // Load triangle indices (ALREADY GLOBAL - critical!)
    const uvec3 idx = globalIndices.i[mesh.ibOffset + gl_PrimitiveID];
    
    // Load vertex positions (direct indexing - idx is global)
    const vec3 va = globalVertices.v[idx.x];
    const vec3 vb = globalVertices.v[idx.y];
    const vec3 vc = globalVertices.v[idx.z];
    
    // Compute geometry normal
    const vec3 n = normalize(cross(vb - va, vc - va));
    
    // Load UVs (convert to local indices first!)
    vec2 uv = vec2(0);
    if (mesh.num_uvs > 0) {
        const uvec3 local_idx = idx - uvec3(mesh.vbOffset);
        const vec2 uva = globalUVs.uv[mesh.uvOffset + local_idx.x];
        const vec2 uvb = globalUVs.uv[mesh.uvOffset + local_idx.y];
        const vec2 uvc = globalUVs.uv[mesh.uvOffset + local_idx.z];
        uv = (1.f - attrib.x - attrib.y) * uva + attrib.x * uvb + attrib.y * uvc;
    }
    
    // Transform to world space
    mat3 inv_transp = transpose(mat3(gl_WorldToObjectEXT));
    payload.normal = normalize(inv_transp * n);
    payload.dist = gl_RayTmaxEXT;
    payload.uv = uv;
    payload.material_id = mesh.material_id;
}
```

**Validation:**
- Shader compiles with new main() ✅
- Uses global buffers for geometry access ✅
- Textures reference binding 30 ✅

---

## Step 4.4: Recompile GLSL to SPIRV (10 min)

**⚠️ CRITICAL:** Compiled shaders must match descriptor layout changes!

**Location:** Project root or shader directory

**Run shader compilation:**
```powershell
# Assuming glslangValidator is in PATH
# Adjust paths to match your shader locations

glslangValidator -V backends/vulkan/raygen.rgen -o backends/vulkan/raygen.rgen.spv
glslangValidator -V backends/vulkan/hit.rchit -o backends/vulkan/hit.rchit.spv
glslangValidator -V backends/vulkan/miss.rmiss -o backends/vulkan/miss.rmiss.spv
glslangValidator -V backends/vulkan/shadow_hit.rchit -o backends/vulkan/shadow_hit.rchit.spv
glslangValidator -V backends/vulkan/shadow_miss.rmiss -o backends/vulkan/shadow_miss.rmiss.spv
```

**Expected Output:**
```
backends/vulkan/raygen.rgen
backends/vulkan/raygen.rgen.spv
```

**Validation:**
- All shaders compile without errors ✅
- New .spv files contain updated descriptor references ✅
- File timestamps updated ✅

**Note:** If you use a different shader compilation method (CMake custom commands, build scripts), use that instead.

---

## Step 4.5: Update C++ Descriptor Layout - Remove Set 1 (15 min)

**File:** `backends/vulkan/render_vulkan.cpp`  
**Location:** `build_raytracing_pipeline()` around line 910

**FIND (descriptor layout creation with Set 1):**
```cpp
    // Create descriptor set layout for textures (separate set)
    VkDescriptorSetLayoutBinding textures_binding = {};
    textures_binding.binding = 0;
    textures_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textures_binding.descriptorCount = /* variable count */;
    // ... create textures_desc_layout
```

**MODIFY descriptor set layout creation:**
```cpp
    std::cout << "\n[PHASE 4] Flattening descriptor sets - adding textures to Set 0...\n";
    
    // Add textures to Set 0 at binding 30 (variable descriptor count)
    VkDescriptorSetLayoutBinding textures_binding = {};
    textures_binding.binding = 30;
    textures_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textures_binding.descriptorCount = scene->textures.size();
    textures_binding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    textures_binding.pImmutableSamplers = nullptr;
    
    bindings.push_back(textures_binding);
    
    // Enable variable descriptor count for binding 30
    VkDescriptorBindingFlags binding_flags[num_bindings];
    // ... set all to 0 except binding 30
    binding_flags[index_of_binding_30] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    
    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {};
    binding_flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    binding_flags_info.bindingCount = bindings.size();
    binding_flags_info.pBindingFlags = binding_flags;
    
    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.pNext = &binding_flags_info;  // Enable variable count
    layout_info.bindingCount = bindings.size();
    layout_info.pBindings = bindings.data();
    
    CHECK_VULKAN(vkCreateDescriptorSetLayout(device->logical_device(), 
                                             &layout_info, 
                                             nullptr, 
                                             &desc_layout));
    
    std::cout << "  ✅ Textures added to Set 0 at binding 30 (variable count)\n";
    
    // DO NOT create textures_desc_layout - everything is in Set 0 now
```

**DELETE these lines:**
```cpp
    vkCreateDescriptorSetLayout(/* for textures_desc_layout */);
```

**UPDATE pipeline layout creation:**
```cpp
    // Create pipeline layout with SINGLE descriptor set
    VkDescriptorSetLayout layouts[] = { desc_layout };  // Only Set 0
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;  // Changed from 2
    pipeline_layout_info.pSetLayouts = layouts;
```

**Validation:**
- Compiles ✅
- Pipeline layout uses single descriptor set ✅

---

## Step 4.6: Update Descriptor Set Allocation (10 min)

**File:** `backends/vulkan/render_vulkan.cpp`  
**Location:** `build_shader_descriptor_table()` around line 1092

**FIND (separate descriptor pool/set for textures):**
```cpp
    // Allocate textures descriptor set from separate pool
    VkDescriptorSetAllocateInfo textures_alloc_info = {};
    textures_alloc_info.descriptorPool = textures_desc_pool;
    textures_alloc_info.descriptorSetCount = 1;
    textures_alloc_info.pSetLayouts = &textures_desc_layout;
    
    vkAllocateDescriptorSets(device->logical_device(), 
                            &textures_alloc_info, 
                            &textures_desc_set);
```

**REPLACE WITH (single descriptor set with variable count):**
```cpp
    std::cout << "[PHASE 4] Allocating single descriptor set with variable count...\n";
    
    // Update descriptor pool to include textures in Set 0
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7 },  // 2 existing + 5 global buffers
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(scene->textures.size()) }
    };
    
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;  // Single descriptor set
    pool_info.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]);
    pool_info.pPoolSizes = pool_sizes;
    
    vkCreateDescriptorPool(device->logical_device(), &pool_info, nullptr, &desc_pool);
    
    // Allocate single descriptor set with variable count
    uint32_t variable_desc_count = static_cast<uint32_t>(scene->textures.size());
    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info = {};
    variable_count_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variable_count_info.descriptorSetCount = 1;
    variable_count_info.pDescriptorCounts = &variable_desc_count;
    
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.pNext = &variable_count_info;
    alloc_info.descriptorPool = desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &desc_layout;
    
    CHECK_VULKAN(vkAllocateDescriptorSets(device->logical_device(), 
                                          &alloc_info, 
                                          &desc_set));
    
    std::cout << "  ✅ Single descriptor set allocated with " << variable_desc_count 
              << " texture descriptors at binding 30\n";
```

**Validation:**
- Compiles ✅
- Single descriptor pool created ✅
- Variable descriptor count allocated ✅

---

## Step 4.7: Update Texture Descriptor Writes (5 min)

**File:** `backends/vulkan/render_vulkan.cpp`  
**Location:** After global buffer descriptor writes

**FIND (textures written to Set 1):**
```cpp
    // Write textures to Set 1
    std::vector<VkDescriptorImageInfo> texture_infos;
    // ... populate texture_infos
    
    VkWriteDescriptorSet texture_write = {};
    texture_write.dstSet = textures_desc_set;  // Set 1
    texture_write.dstBinding = 0;
```

**REPLACE WITH (textures written to Set 0 binding 30):**
```cpp
    std::cout << "[PHASE 4] Writing textures to Set 0 binding 30...\n";
    
    // Write textures to Set 0 binding 30
    std::vector<VkDescriptorImageInfo> texture_infos;
    // ... populate texture_infos (same as before)
    
    VkWriteDescriptorSet texture_write = {};
    texture_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    texture_write.dstSet = desc_set;  // Set 0 now!
    texture_write.dstBinding = 30;    // Binding 30 instead of 0
    texture_write.dstArrayElement = 0;
    texture_write.descriptorCount = static_cast<uint32_t>(texture_infos.size());
    texture_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_write.pImageInfo = texture_infos.data();
    
    vkUpdateDescriptorSets(device->logical_device(), 1, &texture_write, 0, nullptr);
    
    std::cout << "  ✅ " << texture_infos.size() << " textures bound to Set 0 binding 30\n";
    std::cout << "[PHASE 4] Descriptor flattening complete - single Set 0!\n\n";
```

**Validation:**
- Textures written to Set 0 binding 30 ✅
- Console confirms flattening ✅

---

## Step 4.8: Update Command Buffer Binding (5 min)

**File:** `backends/vulkan/render_vulkan.cpp`  
**Location:** `record_command_buffers()` around line 1321

**FIND (binding both Set 0 and Set 1):**
```cpp
    VkDescriptorSet descriptor_sets[] = { desc_set, textures_desc_set };
    vkCmdBindDescriptorSets(command_buffers[i],
                           VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                           pipeline_layout,
                           0,
                           2,  // Binding 2 sets
                           descriptor_sets,
                           0, nullptr);
```

**REPLACE WITH (binding only Set 0):**
```cpp
    // PHASE 4: Bind single descriptor set (Set 0 contains everything)
    vkCmdBindDescriptorSets(command_buffers[i],
                           VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                           pipeline_layout,
                           0,
                           1,  // Single descriptor set
                           &desc_set,
                           0, nullptr);
```

**Validation:**
- Compiles ✅
- Only Set 0 bound for rendering ✅

---

## Step 4.9: Update Header - Remove Set 1 Members (5 min)

**File:** `backends/vulkan/render_vulkan.h`  
**Location:** Class member variables around line 81

**FIND:**
```cpp
    VkDescriptorSetLayout desc_layout;
    VkDescriptorSetLayout textures_desc_layout;  // Remove this
    
    VkDescriptorSet desc_set;
    VkDescriptorSet textures_desc_set;  // Remove this
```

**REPLACE WITH:**
```cpp
    VkDescriptorSetLayout desc_layout;  // Single layout for Set 0
    // textures_desc_layout removed - all in Set 0 now
    
    VkDescriptorSet desc_set;  // Single descriptor set
    // textures_desc_set removed - all in Set 0 now
```

**UPDATE destructor** (remove cleanup of removed resources):
```cpp
    vkDestroyDescriptorSetLayout(device->logical_device(), desc_layout, nullptr);
    // Do NOT destroy textures_desc_layout - doesn't exist anymore
```

**Validation:**
- Compiles ✅
- No unused member variables ✅

---

## Step 4.10: Build and Validate (5 min)

```powershell
cmake --build build --config Debug
```

**Expected:**
- Clean build ✅
- No compilation errors ✅
- Shaders loaded correctly ✅

**Test run (quick validation):**
```powershell
.\build\Debug\chameleonrt.exe vulkan assets\cube.obj
```

**Expected Console Output:**
```
[PHASE 2] Creating global buffers from scene data...
  ✅ globalVertices: 24 elements (288 bytes)
  ✅ globalIndices: 12 triangles (144 bytes)
  ✅ globalUVs: 24 elements (192 bytes)
  ✅ meshDescs: 1 descriptors (32 bytes)
[PHASE 2] Global buffers created successfully

[PHASE 3] Writing global buffer descriptors...
  ✅ Binding 10 (globalVertices): 24 elements
  ✅ Binding 11 (globalIndices): 12 elements
  ✅ Binding 13 (globalUVs): 24 elements
  ✅ Binding 14 (meshDescs): 1 elements
[PHASE 3] Global buffer descriptors bound successfully

[PHASE 4] Flattening descriptor sets - adding textures to Set 0...
  ✅ Textures added to Set 0 at binding 30 (variable count)
[PHASE 4] Allocating single descriptor set with variable count...
  ✅ Single descriptor set allocated with N texture descriptors at binding 30
[PHASE 4] Writing textures to Set 0 binding 30...
  ✅ N textures bound to Set 0 binding 30
[PHASE 4] Descriptor flattening complete - single Set 0!
```

**Expected Rendering:**
- Application launches ✅
- Scene renders (may not be correct yet - SBT not updated) ✅
- No validation errors about missing descriptor sets ✅
- No crashes ✅

**Note:** Rendering may not be perfect yet because SBT still has old structure (7 fields). This will be fixed in Phase 5.

**Commit:** `git commit -m "Vulkan Phase 4: Shader updates + descriptor flattening complete"`

---

## Phase 5: SBT Simplification (20-30 min)

**Goal:** Update shader binding table to use simplified HitGroupParams  
**File:** `backends/vulkan/render_vulkan.cpp`  
**DXR Parallel:** ✅ Matches DXR Phase 5 - Final SBT changes

**Note:** Shaders already updated in Phase 4 to use meshDescIndex, so this is purely C++ SBT changes.

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
    std::cout << "\n[PHASE 5] Updating SBT with meshDescIndex...\n";
    
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
    
    std::cout << "[PHASE 5] SBT updated: " << mesh_desc_index << " hit groups configured\n";
    std::cout << "  📊 SBT reduction: " << (7 * mesh_desc_index) << " fields → " 
              << mesh_desc_index << " fields (80% reduction)\n\n";
```

**Expected Console Output (Sponza):**
```
[PHASE 5] Updating SBT with meshDescIndex...
  HitGroup[0] = param_mesh0_geom0 -> meshDescIndex=0
  HitGroup[1] = param_mesh0_geom1 -> meshDescIndex=1
  HitGroup[2] = param_mesh0_geom2 -> meshDescIndex=2
  HitGroup[3] = param_mesh0_geom3 -> meshDescIndex=3
  HitGroup[4] = param_mesh0_geom4 -> meshDescIndex=4
[PHASE 5] SBT updated: 381 hit groups configured
  📊 SBT reduction: 2667 fields → 381 fields (80% reduction)
```

**Validation:**
- Build succeeds ✅
- Console shows SBT mapping ✅

**Commit:** `git commit -m "Vulkan Phase 5: Simplify SBT to meshDescIndex only"`

---

## Step 5.2: Verify Shader Consistency (5 min)

**File:** `backends/vulkan/hit.rchit`

**VERIFY shader has:**
```glsl
// MeshDesc struct at top ✅
struct MeshDesc { ... };

// Global buffers at bindings 10-14 ✅
layout(binding = 10, set = 0, scalar) readonly buffer GlobalVertices { ... };
// ... bindings 11-14

// Textures at binding 30 ✅
layout(binding = 30, set = 0) uniform sampler2D textures[];

// Simplified shaderRecordEXT ✅
layout(shaderRecordEXT, std430) buffer SBT {
    uint32_t meshDescIndex;
};

// main() using global buffers ✅
void main() {
    const uint meshID = meshDescIndex;
    MeshDesc mesh = meshDescs.descs[meshID];
    // ...
}
```

**DELETE if present:**
- `main_OLD()` function
- Old buffer_reference declarations
- Commented-out old code

**Validation:**
- Shader clean and consistent ✅
- Only new code path exists ✅

**Commit:** `git commit -m "Vulkan Phase 5: Clean up old shader code"`

---

## Phase 6: Testing & Validation (30-60 min)

**Goal:** Verify rendering correctness  
**DXR Parallel:** ✅ Matches DXR Phase 6 - Comprehensive testing

## Test 6.1: Build Verification (5 min)

```powershell
cmake --build build --config Debug
```

**Expected:**
- Clean build ✅
- No warnings ✅
- All shaders compile ✅

---

## Test 6.2: Simple Scene (Cube) (10 min)

```powershell
.\build\Debug\chameleonrt.exe vulkan "C:\Demo\Assets\cube\cube.obj"
```

**Expected Console Output:**
```
[PHASE 2] Creating global buffers from scene data...
  ✅ globalVertices: 24 elements (288 bytes)
  ✅ globalIndices: 12 triangles (144 bytes)
  ✅ globalUVs: 24 elements (192 bytes)
  ✅ meshDescs: 1 descriptors (32 bytes)
[PHASE 2] Global buffers created successfully

[PHASE 3] Adding global buffer descriptor bindings...
  ✅ Added bindings 10-14 for global buffers
[PHASE 3] Writing global buffer descriptors...
  ✅ Binding 10 (globalVertices): 24 elements
  ✅ Binding 11 (globalIndices): 12 elements
  ✅ Binding 13 (globalUVs): 24 elements
  ✅ Binding 14 (meshDescs): 1 elements
[PHASE 3] Global buffer descriptors bound successfully

[PHASE 5] Updating SBT with meshDescIndex...
  HitGroup[0] = param_mesh0_geom0 -> meshDescIndex=0
[PHASE 5] SBT updated: 1 hit groups configured
  📊 SBT reduction: 7 fields → 1 fields (80% reduction)
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

## Test 6.3: Complex Scene (Sponza) (15 min)

```powershell
.\build\Debug\chameleonrt.exe vulkan "C:\Demo\Assets\sponza\sponza.obj"
```

**Expected Console Output:**
```
[PHASE 2] Creating global buffers from scene data...
  ✅ globalVertices: 184406 elements (2212872 bytes)
  ✅ globalIndices: 262267 triangles (3147204 bytes)
  ✅ globalUVs: 184406 elements (1475248 bytes)
  ✅ meshDescs: 381 descriptors (12192 bytes)
[PHASE 2] Global buffers created successfully

[PHASE 3] Adding global buffer descriptor bindings...
  ✅ Added bindings 10-14 for global buffers
[PHASE 3] Writing global buffer descriptors...
  ✅ Binding 10 (globalVertices): 184406 elements
  ✅ Binding 11 (globalIndices): 262267 elements
  ✅ Binding 13 (globalUVs): 184406 elements
  ✅ Binding 14 (meshDescs): 381 elements
[PHASE 3] Global buffer descriptors bound successfully

[PHASE 5] Updating SBT with meshDescIndex...
  HitGroup[0] = param_mesh0_geom0 -> meshDescIndex=0
  HitGroup[1] = param_mesh0_geom1 -> meshDescIndex=1
  HitGroup[2] = param_mesh0_geom2 -> meshDescIndex=2
  HitGroup[3] = param_mesh0_geom3 -> meshDescIndex=3
  HitGroup[4] = param_mesh0_geom4 -> meshDescIndex=4
[PHASE 5] SBT updated: 381 hit groups configured
  📊 SBT reduction: 2667 fields → 381 fields (80% reduction)
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

**Commit:** `git commit -m "Vulkan Phase 6: Testing complete - global buffers working"`

---

## Success Criteria Checklist

- [ ] **Phase 1: Headers** (5 min)
  - [ ] HitGroupParams updated to meshDescIndex only
  - [ ] Global buffer members added to RenderVulkan class
  - [ ] Builds successfully

- [ ] **Phase 2: Upload** (60 min)
  - [ ] Upload helper function implemented
  - [ ] All 5 global buffers uploaded to GPU
  - [ ] Console shows buffer sizes correctly
  - [ ] Application runs (old path still works)

- [ ] **Phase 3: Bind** (45 min)
  - [ ] Descriptor set layout includes bindings 10-14
  - [ ] Descriptor pool updated for 5 new buffers
  - [ ] Descriptors written and bound
  - [ ] Console confirms all bindings
  - [ ] No Vulkan validation errors

- [ ] **Phase 4: Shaders + Descriptor Flatten** (75 min)
  - [ ] Add MeshDesc struct to shaders
  - [ ] Add global buffer declarations (bindings 10-14)
  - [ ] Move textures from Set 1 to Set 0 binding 30
  - [ ] Update shaderRecordEXT to meshDescIndex only
  - [ ] Implement new main() with global buffers
  - [ ] Recompile GLSL → SPIRV (critical!)
  - [ ] Flatten C++ descriptor layout (remove Set 1)
  - [ ] Update descriptor pool and allocation
  - [ ] Update texture descriptor writes (Set 0 binding 30)
  - [ ] Update command buffer binding (single set)
  - [ ] Remove Set 1 members from header
  - [ ] Build and validate

- [ ] **Phase 5: SBT** (25 min)
  - [ ] Update SBT to write meshDescIndex only
  - [ ] Verify shader consistency (no old code)
  - [ ] Console shows SBT reduction

- [ ] **Phase 6: Testing** (60 min)
  - [ ] Cube renders identically to baseline
  - [ ] Sponza renders identically to baseline
  - [ ] No validation errors
  - [ ] No crashes or memory leaks
  - [ ] Stable for extended runtime

---

## Estimated Timeline

| Phase | Task | Time | Cumulative |
|-------|------|------|------------|
| 1 | Header updates | 5 min | 5 min |
| 2 | Upload global buffers | 60 min | 65 min |
| 3 | Bind descriptor sets | 45 min | 110 min |
| 4 | Shaders + descriptor flatten | 75 min | 185 min |
| 5 | Simplify SBT | 25 min | 210 min |
| 6 | Testing & validation | 60 min | 270 min |

**Total:** ~4.5 hours (with validation gates)

**Recommendation:** Split across 2 sessions:
- Session 1: Phases 1-3 (2 hours) - Headers, Upload, Bind
- Session 2: Phases 4-6 (2.5 hours) - Shaders, Flatten, SBT, Test

---

## DXR vs Vulkan Phase Mapping

| Phase | DXR | Vulkan | Time |
|-------|-----|--------|------|
| 1 | Headers | Headers | 5 min |
| 2 | Upload buffers | Upload buffers | 60 min |
| 3 | Descriptor heap | Descriptor sets | 45 min |
| 4 | Update shaders | Shaders + flatten sets | 75 min |
| 5 | Simplify SBT | Simplify SBT | 25 min |
| 6 | Testing | Testing | 60 min |

**✅ EXACT SEQUENCE MATCH** - Proven pattern from DXR!

**Key Enhancement:** Phase 4 combines shader updates and descriptor flattening atomically, addressing the lesson learned from Phase 3 cleanup attempt (can't change descriptor layout without recompiling shaders).

---

## References

- **DXR Completion Report:** `../DXR/COMPLETION_REPORT.md`
- **DXR Technical Notes:** `../DXR/TECHNICAL_NOTES.md`
- **DXR Quick Reference:** `../DXR/QUICK_REFERENCE.md`
- **Vulkan Buffer Analysis:** `Buffer_Analysis.md`
- **Scene Global Buffers:** `../../util/scene.h`

---

## Key Differences vs Old Plan

**OLD Plan Issues:**
- Shaders before buffers (risky - can't test until late)
- Unclear dependencies between phases
- Mixed ordering vs DXR

**NEW Plan (This Version):**
- ✅ Follows DXR order EXACTLY
- ✅ Upload buffers BEFORE shaders
- ✅ Can validate GPU data before shader changes
- ✅ Clear phase dependencies
- ✅ Proven sequence with demonstrable results

---

**Ready to implement!** 🚀

Follow the DXR pattern that we **know works**. Each phase builds on the previous one with clear validation gates. The exact sequence matters - don't skip ahead or reorder phases!
