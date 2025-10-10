# Phase 2A.3: Vulkan Backend Refactor - Copilot Prompt

**Phase:** 2A.3 - Vulkan Backend Refactor  
**Goal:** Update Vulkan backend to use global buffers, validate identical rendering  
**Status:** Implementation

---

## Context

I'm working on ChameleonRT ray tracing renderer. Phase 2A.2 (DXR backend refactor) is complete - DXR now uses global buffers and renders identically to baseline. Now we need to update the Vulkan backend to also use global buffers.

**Critical Success Criteria:**
- Rendering must be IDENTICAL to Phase 2A.0 baseline screenshots
- Remove all shader record buffer address usage
- All resources bound in set 0 (descriptor set 0)
- Use native GLSL shaders (NO Slang in this phase)

---

## Prerequisites

Before starting this phase:
- ✅ Phase 2A.0 analysis complete
- ✅ Phase 2A.1 CPU refactor complete
- ✅ Phase 2A.2 DXR refactor complete and validated
- ✅ Global buffers available in Scene
- ✅ Baseline screenshots for comparison

---

## Reference Documents

- `Migration/SlangIntegration/Phase2A_GlobalBuffers_Plan.md` - Section "Phase 2A.3"
- `Migration/SlangIntegration/Phase2A_Analysis/2A.0.3_Vulkan_Buffer_Analysis.md` - Current Vulkan architecture
- `Migration/SlangIntegration/Phase2A_Analysis/2A.2.0_BLAS_Strategy.md` - BLAS strategy (may apply to Vulkan too)
- `.github/copilot-instructions.md` - Project conventions

---

## ⚠️ CRITICAL ANALYSIS POINT: Vulkan BLAS Building Strategy

**BEFORE implementing code changes, we must decide how to build BLAS for Vulkan.**

### Analysis Required

Read `backends/vulkan/render_vulkan.cpp` and examine the BLAS building code:

1. How is `VkAccelerationStructureGeometryKHR` currently filled?
   - Does it reference vertex/index buffer device addresses?
   - What buffer addresses are used?

2. Can we reuse the DXR BLAS strategy from Phase 2A.2?
   - If DXR used temporary buffers, can Vulkan do the same?
   - If DXR used global buffers with offsets, can Vulkan?

3. Vulkan-specific considerations:
   - Buffer device addresses still needed for BLAS input
   - Can we get device addresses from global buffers with offsets?

### Decision Point

**PAUSE HERE and analyze the BLAS building code.** Document your findings:

Create a file: `Migration/SlangIntegration/Phase2A_Analysis/2A.3.0_Vulkan_BLAS_Strategy.md`

```markdown
# Phase 2A.3.0: BLAS Building Strategy for Vulkan

## Current BLAS Building Code Analysis

**Location:** `backends/vulkan/render_vulkan.cpp::set_scene()`

**Current Implementation:**
[Describe how BLAS is currently built]

**Geometry Structure Setup:**
```cpp
[Paste relevant VkAccelerationStructureGeometryKHR setup code]
```

**Buffer Device Address Usage:**
[How are addresses obtained and used?]

## Comparison with DXR Strategy

**DXR Approach (from Phase 2A.2):** [Summary of DXR BLAS strategy]

**Applicable to Vulkan?** [Yes/No - explain]

## Proposed Strategy

**Chosen Option:** [Describe approach]

**Rationale:**
[Explain why this option was chosen]

**Implementation Approach:**
[Describe how BLAS building will work with global buffers in Vulkan]

## Code Changes Required

[List specific changes needed for BLAS building]

## Validation Plan

[How will we verify BLAS is built correctly?]
```

**After completing this analysis and documenting the strategy, proceed with implementation below.**

---

## Task 2A.3.1: Update GLSL Shaders

**Objective:** Modify Vulkan shaders to use global buffers instead of buffer_reference pointers.

### File: `backends/vulkan/hit.rchit` (and potentially other shader files)

#### Step 1: Add New Structs

At the top of the file (after any extensions/includes), add:

```glsl
// Phase 2A.3: Global buffer structures
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

#### Step 2: Add Global Buffer Declarations (set 0)

Add these buffer declarations (choose appropriate bindings based on analysis):

```glsl
// Phase 2A.3: Global buffers for geometry data
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

**Important:** 
- Use bindings 10, 11, 12 to avoid conflicts with existing resources
- Requires `GL_EXT_scalar_block_layout` extension for `scalar` layout
- If scalar layout not available, use `std430` for all buffers

#### Step 3: Remove buffer_reference and shaderRecordEXT

Find and DELETE (or comment out) buffer_reference and shaderRecordEXT declarations:

```glsl
// DELETE OR COMMENT OUT:
// #extension GL_EXT_buffer_reference : require
// layout(buffer_reference, scalar) buffer VertexBuffer { vec3 v[]; };
// layout(buffer_reference, scalar) buffer IndexBuffer { uvec3 i[]; };
// layout(buffer_reference, scalar) buffer NormalBuffer { vec3 n[]; };
// layout(buffer_reference, scalar) buffer UVBuffer { vec2 uv[]; };
// layout(shaderRecordEXT, std430) buffer SBT {
//     uint64_t vertexAddr;
//     uint64_t indexAddr;
//     ...
// };
```

**Action:** Comment these out with `// Phase 2A.3: Removed - using global buffers` for easy rollback.

#### Step 4: Update main() Function

Find the `main()` function in `hit.rchit`. Replace geometry data access logic:

**Old pattern (example - adapt based on actual code):**
```glsl
// OLD CODE - buffer_reference pointers from shader record
IndexBuffer indices = IndexBuffer(sbt.indexAddr);
VertexBuffer vertices = VertexBuffer(sbt.vertexAddr);
uvec3 tri = indices.i[gl_PrimitiveID];
vec3 v0 = vertices.v[tri.x];
// ... etc
```

**New pattern:**
```glsl
void main()
{
    // Phase 2A.3: Use global buffers with indirection
    
    // Get which geometry we hit
    // NOTE: Verify that gl_InstanceCustomIndexEXT returns the correct mesh descriptor index
    // This may need adjustment based on how TLAS instances are set up
    uint meshID = gl_InstanceCustomIndexEXT;
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
    
    // Rest of shading logic UNCHANGED from this point
    // ... (continue with existing shading code using worldPos, worldNormal, texCoord, materialID)
}
```

**Important:** Preserve all existing shading logic. Only replace the geometry data access portion.

#### Step 5: Check Other Shader Files

Check if other shader files need updates:
- `miss.rmiss` - likely no geometry access, probably no changes
- `raygen.rgen` - likely no changes
- Any other `.rchit` files if they exist

### Actions Required

1. Read current Vulkan shaders to understand structure
2. Add Vertex and MeshDesc structs to `hit.rchit`
3. Add global buffer declarations (bindings 10, 11, 12 in set 0)
4. Comment out buffer_reference and shaderRecordEXT declarations
5. Update main() function with new geometry fetch logic
6. Check other shader files for needed changes
7. **DO NOT compile yet** - shaders compile when C++ builds

### Validation

- ✅ Shader syntax looks correct
- ✅ No buffer_reference usage remaining
- ✅ Global buffers use set 0
- ✅ Existing shading logic preserved

---

## Task 2A.3.2: Update Vulkan C++ - Add Global Buffer Member Variables

**Objective:** Add member variables to RenderVulkan class for global buffers.

### File: `backends/vulkan/render_vulkan.h`

Find the `RenderVulkan` class definition. Locate existing buffer member variables.

Add the following member variables:

```cpp
    // Phase 2A.3: Global buffers
    vkr::Buffer global_vertex_buffer;
    vkr::Buffer global_index_buffer;
    vkr::Buffer mesh_desc_buffer;
```

**Note:** Type should match existing buffer members (likely `vkr::Buffer` or similar wrapper class).

### Actions Required

1. Locate `RenderVulkan` class in `render_vulkan.h`
2. Add three global buffer member variables
3. Build to verify syntax

### Validation

- ✅ Member variables added to class
- ✅ Syntax correct (same type as other buffers)

---

## Task 2A.3.3: Update Vulkan C++ - Create Global Buffers

**Objective:** Create and upload global buffers to GPU.

### File: `backends/vulkan/render_vulkan.cpp`

Find the `set_scene()` function.

#### Step 1: Create Global Buffers

At the beginning of `set_scene()`, AFTER initialization, add:

```cpp
void RenderVulkan::set_scene(const Scene &scene)
{
    frame_id = 0;
    samples_per_pixel = scene.samples_per_pixel;
    
    // Phase 2A.3: Create global buffers
    std::cout << "[Phase 2A.3 Vulkan] Creating global buffers...\n";
    std::cout << "  - Vertices: " << scene.global_vertices.size() << "\n";
    std::cout << "  - Indices: " << scene.global_indices.size() << "\n";
    std::cout << "  - MeshDescriptors: " << scene.mesh_descriptors.size() << "\n";
    
    // 1. Global Vertex Buffer
    if (!scene.global_vertices.empty()) {
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
        std::memcpy(staging_verts.map(), scene.global_vertices.data(), 
                    scene.global_vertices.size() * sizeof(Vertex));
        staging_verts.unmap();
        
        // Copy staging to device (add to command buffer)
        // vkCmdCopyBuffer(cmd_buf, staging_verts.buffer, global_vertex_buffer.buffer, ...);
    }
    
    // 2. Global Index Buffer
    if (!scene.global_indices.empty()) {
        global_index_buffer = vkr::Buffer::device(
            device,
            scene.global_indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        );
        
        auto staging_indices = vkr::Buffer::host(
            device,
            scene.global_indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        );
        std::memcpy(staging_indices.map(), scene.global_indices.data(), 
                    scene.global_indices.size() * sizeof(uint32_t));
        staging_indices.unmap();
        
        // Copy staging to device
    }
    
    // 3. MeshDesc Buffer
    if (!scene.mesh_descriptors.empty()) {
        mesh_desc_buffer = vkr::Buffer::device(
            device,
            scene.mesh_descriptors.size() * sizeof(MeshDesc),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        );
        
        auto staging_descs = vkr::Buffer::host(
            device,
            scene.mesh_descriptors.size() * sizeof(MeshDesc),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        );
        std::memcpy(staging_descs.map(), scene.mesh_descriptors.data(), 
                    scene.mesh_descriptors.size() * sizeof(MeshDesc));
        staging_descs.unmap();
        
        // Copy staging to device
    }
    
    // Find where command buffer recording happens
    // Add vkCmdCopyBuffer calls for all three buffers
    // Add pipeline barriers to transition to shader read
    
    std::cout << "[Phase 2A.3 Vulkan] Global buffers created and uploaded\n";
    
    // Continue with BLAS/TLAS building...
    // (existing code follows)
}
```

**Important:**
- Adapt based on actual vkr::Buffer API
- Ensure staging buffers live long enough for copy
- Add proper pipeline barriers after copy
- Buffer usage flags should include STORAGE_BUFFER_BIT

#### Step 2: Update BLAS Building (Based on 2A.3.0 Strategy)

**THIS DEPENDS ON THE ANALYSIS FROM TASK 2A.3.0 BLAS STRATEGY**

Follow the strategy documented in `2A.3.0_Vulkan_BLAS_Strategy.md`.

Common approaches:
- **Temporary buffers:** Create temporary per-geometry buffers just for BLAS, discard after
- **Global buffers with offsets:** Get device address of global buffer, add offset for each geometry

#### Step 3: Remove Per-Geometry Buffer Creation Loop

Find the loop that creates per-geometry buffers. Comment it out:

```cpp
// Phase 2A.3: Removed - using global buffers instead
// Old per-geometry buffer creation code
```

**Exception:** Keep minimal buffer creation if BLAS building needs temporary buffers.

### Actions Required

1. Add global buffer creation code
2. Add staging buffer upload logic
3. Add copy commands to command buffer
4. Add pipeline barriers
5. Update BLAS building per 2A.3.0 strategy
6. Remove per-geometry buffer loop
7. Add `#include <iostream>` if needed

### Validation

- ✅ Global buffers created with correct sizes
- ✅ Staging and device buffers handled correctly
- ✅ Per-geometry buffer loop removed/commented
- ✅ Debug output shows buffer sizes

---

## Task 2A.3.4: Update Vulkan C++ - Descriptor Sets

**Objective:** Add descriptor bindings for global buffers.

### File: `backends/vulkan/render_vulkan.cpp`

Find descriptor set layout creation code (likely in initialization or `set_scene()`).

#### Step 1: Update Descriptor Set Layout

Find where `VkDescriptorSetLayoutBinding` structures are created. Add three new bindings:

```cpp
// Phase 2A.3: Descriptor bindings for global buffers

// Global Vertices (binding 10)
VkDescriptorSetLayoutBinding globalVertexBinding = {};
globalVertexBinding.binding = 10;
globalVertexBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
globalVertexBinding.descriptorCount = 1;
globalVertexBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
globalVertexBinding.pImmutableSamplers = nullptr;

// Global Indices (binding 11)
VkDescriptorSetLayoutBinding globalIndexBinding = {};
globalIndexBinding.binding = 11;
globalIndexBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
globalIndexBinding.descriptorCount = 1;
globalIndexBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
globalIndexBinding.pImmutableSamplers = nullptr;

// MeshDesc (binding 12)
VkDescriptorSetLayoutBinding meshDescBinding = {};
meshDescBinding.binding = 12;
meshDescBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
meshDescBinding.descriptorCount = 1;
meshDescBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
meshDescBinding.pImmutableSamplers = nullptr;

// Add to bindings array
std::vector<VkDescriptorSetLayoutBinding> bindings = {
    // ... existing bindings ...
    globalVertexBinding,
    globalIndexBinding,
    meshDescBinding
};

// Create descriptor set layout with updated bindings
VkDescriptorSetLayoutCreateInfo layoutInfo = {};
layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
layoutInfo.pBindings = bindings.data();
// ... rest of creation
```

#### Step 2: Update Descriptor Pool

Ensure descriptor pool has enough storage buffer descriptors:

```cpp
// Increase storage buffer descriptor count
VkDescriptorPoolSize poolSize = {};
poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
poolSize.descriptorCount = /* previous count */ + 3;  // Add 3 for global buffers
```

#### Step 3: Write Descriptors for Global Buffers

Find where descriptors are written (likely after descriptor set allocation):

```cpp
// Phase 2A.3: Write descriptors for global buffers

// Global Vertices
VkDescriptorBufferInfo vertexInfo = {};
vertexInfo.buffer = global_vertex_buffer.buffer;
vertexInfo.offset = 0;
vertexInfo.range = VK_WHOLE_SIZE;

VkWriteDescriptorSet vertexWrite = {};
vertexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
vertexWrite.dstSet = desc_set;  // Your descriptor set
vertexWrite.dstBinding = 10;
vertexWrite.dstArrayElement = 0;
vertexWrite.descriptorCount = 1;
vertexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
vertexWrite.pBufferInfo = &vertexInfo;

// Global Indices
VkDescriptorBufferInfo indexInfo = {};
indexInfo.buffer = global_index_buffer.buffer;
indexInfo.offset = 0;
indexInfo.range = VK_WHOLE_SIZE;

VkWriteDescriptorSet indexWrite = {};
indexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
indexWrite.dstSet = desc_set;
indexWrite.dstBinding = 11;
indexWrite.dstArrayElement = 0;
indexWrite.descriptorCount = 1;
indexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
indexWrite.pBufferInfo = &indexInfo;

// MeshDesc
VkDescriptorBufferInfo meshDescInfo = {};
meshDescInfo.buffer = mesh_desc_buffer.buffer;
meshDescInfo.offset = 0;
meshDescInfo.range = VK_WHOLE_SIZE;

VkWriteDescriptorSet meshDescWrite = {};
meshDescWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
meshDescWrite.dstSet = desc_set;
meshDescWrite.dstBinding = 12;
meshDescWrite.dstArrayElement = 0;
meshDescWrite.descriptorCount = 1;
meshDescWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
meshDescWrite.pBufferInfo = &meshDescInfo;

// Update all descriptors
std::vector<VkWriteDescriptorSet> writes = {
    // ... existing writes ...
    vertexWrite,
    indexWrite,
    meshDescWrite
};

vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
```

### Actions Required

1. Find descriptor set layout creation
2. Add three new bindings (10, 11, 12)
3. Update descriptor pool size
4. Add descriptor writes for global buffers
5. Build and check for errors

### Validation

- ✅ Descriptor set layout includes new bindings
- ✅ Descriptor pool large enough
- ✅ Descriptors written correctly
- ✅ Bindings match shader declarations

---

## Task 2A.3.5: Update Vulkan C++ - Shader Binding Table (SBT)

**Objective:** Simplify SBT by removing per-geometry shader records.

### File: `backends/vulkan/render_vulkan.cpp`

Find shader binding table creation code.

#### Current SBT Analysis

From Phase 2A.0.3, identify:
- How are hit group shader records currently created?
- What data goes into each record? (buffer addresses?)
- Is there a loop creating records per geometry?

#### Remove Shader Record Data

With global buffers, hit group shader records no longer need per-geometry data:

**Old pattern (example):**
```cpp
// OLD CODE - per-geometry shader records
for (each geometry) {
    struct ShaderRecord {
        uint64_t vertex_addr;
        uint64_t index_addr;
        // ... etc
    };
    // Write to SBT
}
```

**New pattern:**
```cpp
// Phase 2A.3: Simplified SBT - no per-geometry data needed
// All hit groups can share the same shader record (or minimal data)

// Hit group record structure is now empty or minimal
// Just need shader handle, no geometry-specific data
```

#### Simplify SBT Creation

1. Remove buffer device address calls for per-geometry buffers:
   ```cpp
   // DELETE:
   // uint64_t vertex_addr = vkGetBufferDeviceAddressKHR(...);
   ```

2. Remove shader record data structure (if it only contained buffer addresses)

3. Simplify hit group record size calculation

4. Update SBT region sizes if needed

### Actions Required

1. Find SBT creation code
2. Remove per-geometry shader record loop
3. Remove buffer device address calls for geometry buffers
4. Simplify hit group records
5. Update SBT region sizes
6. Build and check for errors

### Validation

- ✅ No vkGetBufferDeviceAddressKHR for per-geometry buffers
- ✅ SBT simplified (no per-geometry records)
- ✅ Hit group records minimal or empty
- ✅ Code compiles

---

## Task 2A.3.6: Build and Test Vulkan Backend

**Objective:** Verify that Vulkan rendering is identical to baseline.

### Build

```powershell
cd C:\dev\ChameleonRT
cmake --build build --config Debug --target crt_vulkan
```

**Fix any compilation errors before proceeding.**

**Common shader compilation issues:**
- Missing extension: Add `#extension GL_EXT_scalar_block_layout : enable` if using scalar layout
- Missing extension: Add `#extension GL_EXT_buffer_reference : enable` (wait, we removed this - make sure it's gone!)

### Test 1: test_cube.obj

Run the application:
```powershell
.\build\Debug\chameleonrt.exe vulkan .\test_cube.obj
```

**Expected Console Output:**
```
[Phase 2A.1] Global buffers built:
  - Vertices: ...
  - Indices: ...
  - MeshDescriptors: ...
  - GeometryInstances: ...
  - TransformMatrices: ...
[Phase 2A.3 Vulkan] Creating global buffers...
  - Vertices: ...
  - Indices: ...
  - MeshDescriptors: ...
[Phase 2A.3 Vulkan] Global buffers created and uploaded
```

**Visual Validation:**
1. Take a screenshot
2. Compare with baseline: `Migration/SlangIntegration/Phase2A_Analysis/baseline_vulkan_cube.png`
3. Compare with DXR output from Phase 2A.2
4. **CRITICAL:** Rendering should be IDENTICAL to both baseline and DXR

**Manual Checks:**
- [ ] Cube renders correctly
- [ ] Colors/shading match DXR output
- [ ] No validation layer errors
- [ ] No black screen or crash
- [ ] Normals correct

### Test 2: Sponza (if available)

Run with Sponza:
```powershell
.\build\Debug\chameleonrt.exe vulkan C:\Demo\Assets\sponza\sponza.obj
```

**Visual Validation:**
1. Take a screenshot
2. Compare with baseline
3. Compare with DXR output
4. Should be identical

**Manual Checks:**
- [ ] Scene loads without crash
- [ ] All geometry visible
- [ ] Matches DXR output
- [ ] No validation layer errors
- [ ] Performance similar

### Validation Layer Check

Enable Vulkan validation layers (if not already enabled) and check for errors:
- No descriptor binding errors
- No buffer access violations
- No synchronization warnings

### Deliverable

Create a file: `Migration/SlangIntegration/Phase2A_Analysis/2A.3.6_Vulkan_Test_Results.md`

```markdown
# Phase 2A.3.6: Vulkan Backend Test Results

**Date:** [Date]

## Build Status

- [x] Code compiles successfully
- [x] Shaders compile successfully
- [ ] No linker errors
- [ ] No validation layer errors

**Build Console Output:**
```
[Paste any relevant build output or warnings]
```

**Validation Layer Output:**
```
[Paste validation layer messages, if any]
```

## Test 1: test_cube.obj

**Console Output:**
```
[Paste console output]
```

**Visual Comparison:**
- [ ] Rendering IDENTICAL to baseline: [Yes/No]
- [ ] Rendering IDENTICAL to DXR: [Yes/No]
- [ ] Geometry correct: [Yes/No]
- [ ] Shading correct: [Yes/No]
- [ ] No artifacts: [Yes/No]

**Screenshot:** [Attach or reference]

**Observations:**
[Any notes]

## Test 2: Sponza

**Console Output:**
```
[Paste console output]
```

**Visual Comparison:**
- [ ] Rendering IDENTICAL to baseline: [Yes/No]
- [ ] Rendering IDENTICAL to DXR: [Yes/No]
- [ ] All geometry present: [Yes/No]
- [ ] Materials/textures correct: [Yes/No]

**Screenshot:** [Attach or reference]

**Observations:**
[Any notes]

## Validation Layer Analysis

**Errors:** [Count and list]
**Warnings:** [Count and list]

**All resolved:** [Yes/No]

## Issues Found

[List any issues]

### Issue 1: [Description]
**Impact:** [Visual/Performance/Crash]
**Potential Cause:** [Analysis]
**Resolution:** [How fixed]

## Performance Comparison

**Baseline:**
- test_cube FPS: [if available]
- Sponza FPS: [if available]

**After Refactor:**
- test_cube FPS: [measured]
- Sponza FPS: [measured]

**DXR Performance:**
- test_cube FPS: [from 2A.2]
- Sponza FPS: [from 2A.2]

**Analysis:**
[Performance similar across backends?]

## Status

- [ ] Phase 2A.3 Vulkan refactor complete
- [ ] Rendering identical to baseline
- [ ] Rendering identical to DXR
- [ ] Ready to proceed to Phase 2A.4 (Final Validation)

**If rendering is NOT identical, DO NOT proceed. Debug and fix issues first.**
```

---

## Common Issues and Debugging

### Issue: Validation layer error - descriptor not bound

**Possible Causes:**
1. Descriptor set layout missing bindings 10, 11, 12
2. Descriptors not written before use
3. Wrong descriptor set bound

**Debug Steps:**
1. Check descriptor set layout creation
2. Verify vkUpdateDescriptorSets called with new descriptors
3. Check pipeline layout includes descriptor set layout

### Issue: Black screen or corrupted geometry

**Possible Causes:**
1. gl_InstanceCustomIndexEXT wrong value
2. Buffer offsets incorrect
3. Scalar layout alignment issues
4. Missing pipeline barrier after buffer copy

**Debug Steps:**
1. Add debug output in shader (use payload for colors)
2. Verify MeshDesc data on CPU side
3. Check buffer barriers
4. Try std430 instead of scalar layout

### Issue: Shader compilation error

**Possible Causes:**
1. Missing extensions
2. Syntax errors in new code
3. Struct layout issues

**Debug Steps:**
1. Check glslangValidator output
2. Add required extensions
3. Verify struct definitions match CPU side

### Issue: Crash during BLAS building

**Possible Causes:**
1. BLAS strategy not implemented correctly
2. Buffer device addresses invalid
3. Geometry descriptors malformed

**Debug Steps:**
1. Review 2A.3.0_Vulkan_BLAS_Strategy.md
2. Check buffer lifetimes
3. Validate VkAccelerationStructureGeometryKHR

---

## Git Commit Recommendation

After successful completion of Phase 2A.3:

```powershell
git add backends/vulkan/
git commit -m "Phase 2A.3: Vulkan backend refactor - global buffers

- Updated GLSL shaders to use global buffers (set 0)
- Removed buffer_reference and shaderRecordEXT usage
- Added global vertex, index, and mesh descriptor buffers
- Updated descriptor set layout and writes
- Simplified shader binding table (no per-geometry records)
- BLAS building strategy: [describe approach]

Validation:
- test_cube.obj: Rendering identical to baseline and DXR
- Sponza: Rendering identical to baseline and DXR
- No validation layer errors
- No performance regression"
```

---

## Next Step

**Proceed to Phase 2A.4:** Final Validation

Use prompt file: `Phase_2A.4_Final_Validation.md`
