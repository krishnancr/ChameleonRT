# Phase 2A.1: CPU Scene Refactor - Copilot Prompt

**Phase:** 2A.1 - CPU Scene Refactor  
**Goal:** Create global buffers during scene loading (CPU-side only, no GPU code changes)  
**Status:** Implementation

---

## Context

I'm working on ChameleonRT ray tracing renderer. Phase 2A.0 analysis is complete. Now we're implementing the CPU-side refactor to create global buffers that concatenate all geometry data.

**Critical:** This phase ONLY modifies CPU-side scene loading code. We are NOT touching GPU backend code (DXR/Vulkan) yet.

---

## Prerequisites

Before starting this phase:
- ✅ Phase 2A.0 analysis complete
- ✅ Baseline screenshots captured
- ✅ Current architecture understood

---

## Reference Documents

- `Migration/SlangIntegration/Phase2A_GlobalBuffers_Plan.md` - Section "Phase 2A.1"
- `Migration/SlangIntegration/Phase2A_Analysis/2A.0.1_Scene_Loading_Analysis.md` - Current scene loading architecture
- `.github/copilot-instructions.md` - Project conventions

---

## Task 2A.1.1: Add New Data Structures

**Objective:** Add new structs to support global buffer architecture.

### File: `util/mesh.h`

Add the following three structs to the end of the file (before the closing brace, if any):

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

### File: `util/scene.h`

Add the following fields to the `Scene` struct (after existing fields, before private section):

```cpp
    // Global buffers for GPU upload (Phase 2A.1)
    std::vector<Vertex> global_vertices;           // All vertices concatenated
    std::vector<uint32_t> global_indices;          // All indices concatenated (as uints, not uvec3)
    std::vector<MeshDesc> mesh_descriptors;        // Metadata per geometry
    std::vector<GeometryInstanceData> geometry_instances; // Metadata per instance
    std::vector<glm::mat4> transform_matrices;     // Transform per instance
    
    // Helper function to build global buffers
    void build_global_buffers();
```

### Actions Required

1. Add the three structs to `util/mesh.h`
2. Add the global buffer fields and function declaration to `Scene` struct in `util/scene.h`
3. Build the project to ensure no compilation errors:
   ```powershell
   cmake --build build --config Debug
   ```

### Validation

- ✅ Code compiles successfully
- ✅ No errors related to the new structs
- ✅ `build_global_buffers()` is declared but not yet implemented (linker will complain - that's expected)

---

## Task 2A.1.2: Implement `build_global_buffers()` Function

**Objective:** Implement the logic to concatenate all geometry data into global arrays.

### File: `util/scene.cpp`

Add the following function implementation. Find an appropriate location (after other Scene member functions):

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
    
    // Build mapping from parameterized_mesh_id -> starting mesh descriptor index
    std::vector<uint32_t> pm_to_mesh_desc_start;
    pm_to_mesh_desc_start.reserve(parameterized_meshes.size());
    
    // For each parameterized mesh (mesh + material assignments)
    for (const auto& pm : parameterized_meshes) {
        pm_to_mesh_desc_start.push_back(static_cast<uint32_t>(mesh_descriptors.size()));
        
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
                static_cast<uint32_t>(geom.vertices.size()),
                static_cast<uint32_t>(geom.indices.size() * 3),
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
            vertexOffset += static_cast<uint32_t>(geom.vertices.size());
            indexOffset += static_cast<uint32_t>(geom.indices.size() * 3);
        }
    }
    
    // Build geometry instance data (for TLAS)
    uint32_t matrixID = 0;
    for (const auto& instance : instances) {
        const ParameterizedMesh& pm = parameterized_meshes[instance.parameterized_mesh_id];
        const Mesh& mesh = meshes[pm.mesh_id];
        
        // Add transform matrix
        transform_matrices.push_back(instance.transform);
        
        // Get starting mesh descriptor index for this parameterized mesh
        uint32_t mesh_desc_start = pm_to_mesh_desc_start[instance.parameterized_mesh_id];
        
        // One GeometryInstanceData per geometry in the mesh
        for (size_t geom_idx = 0; geom_idx < mesh.geometries.size(); ++geom_idx) {
            uint32_t meshDescID = mesh_desc_start + static_cast<uint32_t>(geom_idx);
            
            GeometryInstanceData instData(meshDescID, matrixID, 0);
            geometry_instances.push_back(instData);
        }
        
        ++matrixID;
    }
    
    // Debug output
    std::cout << "[Phase 2A.1] Global buffers built:\n";
    std::cout << "  - Vertices: " << global_vertices.size() << "\n";
    std::cout << "  - Indices: " << global_indices.size() << "\n";
    std::cout << "  - MeshDescriptors: " << mesh_descriptors.size() << "\n";
    std::cout << "  - GeometryInstances: " << geometry_instances.size() << "\n";
    std::cout << "  - TransformMatrices: " << transform_matrices.size() << "\n";
}
```

### Important Notes

**Instance Tracking Logic:** The function uses `pm_to_mesh_desc_start` mapping to track which mesh descriptor corresponds to each geometry instance. This is critical for correct rendering.

**Default Values:** Missing normals default to `(0, 1, 0)` (up vector), missing UVs default to `(0, 0)`.

**Index Adjustment:** Indices are adjusted by `vertexOffset` to account for concatenation into global array.

### Actions Required

1. Add the `build_global_buffers()` implementation to `util/scene.cpp`
2. Add `#include <iostream>` at the top if not already present (for debug output)
3. Build the project:
   ```powershell
   cmake --build build --config Debug
   ```

### Validation

- ✅ Code compiles successfully
- ✅ No linker errors for `build_global_buffers()`

---

## Task 2A.1.3: Call `build_global_buffers()` After Scene Load

**Objective:** Ensure global buffers are built whenever a scene is loaded.

### File: `util/scene.cpp`

Locate the `Scene` constructor(s). Add a call to `build_global_buffers()` at the end of the constructor, after all scene loading is complete.

**Example location (adapt based on actual code):**

```cpp
Scene::Scene(const std::string &fname, MaterialMode material_mode)
    : material_mode(material_mode)
{
    // ... existing loading logic (load_obj, load_gltf, etc.) ...
    
    // Phase 2A.1: Build global buffers after scene is loaded
    build_global_buffers();
}
```

### Actions Required

1. Find the `Scene` constructor(s) that load scenes from files
2. Add `build_global_buffers();` call at the end, after all loading is complete
3. Build the project:
   ```powershell
   cmake --build build --config Debug
   ```

### Validation

- ✅ Code compiles successfully
- ✅ Call is placed after all scene loading logic

---

## Task 2A.1.4: Test CPU Side with Debug Output

**Objective:** Verify that global buffers are correctly built for both simple and complex scenes.

### Test 1: Simple Scene (test_cube.obj)

Run the application with test_cube.obj:

```powershell
cd C:\dev\ChameleonRT
.\build\Debug\chameleonrt.exe dxr .\test_cube.obj
```

**Expected Console Output:**
```
[Phase 2A.1] Global buffers built:
  - Vertices: <number>
  - Indices: <number>
  - MeshDescriptors: <number>
  - GeometryInstances: <number>
  - TransformMatrices: <number>
```

**Manual Validation Questions:**

1. Do the vertex/index counts seem reasonable for a cube?
   - Cube typically has 8 vertices, 12 triangles (36 indices)
   - Multiple geometries or materials might increase these counts

2. Are MeshDescriptors and GeometryInstances consistent?
   - Should have at least 1 MeshDescriptor per geometry
   - GeometryInstances should match: num_instances * geometries_per_mesh

3. Does the application still run without crashing?
   - Note: Rendering may be incorrect (expected - GPU code not updated yet)

### Test 2: Complex Scene (Sponza)

If Sponza is available, run:

```powershell
.\build\Debug\chameleonrt.exe dxr C:\Demo\Assets\sponza\sponza.obj
```

**Expected Console Output:**
```
[Phase 2A.1] Global buffers built:
  - Vertices: <large number, e.g., 50000+>
  - Indices: <large number, e.g., 150000+>
  - MeshDescriptors: <many, e.g., 100+>
  - GeometryInstances: <many>
  - TransformMatrices: <matching instances>
```

**Manual Validation Questions:**

1. Are buffer sizes reasonable for a large scene?
   - Should be significantly larger than test_cube

2. Does the application load without crashing?
   - Loading might be slower (creating global buffers)

3. Are there any warnings or errors in the console?

### Additional Validation (Optional but Recommended)

Add temporary debug code to `build_global_buffers()` to verify offsets:

```cpp
// After mesh_descriptors.push_back(desc);
if (mesh_descriptors.size() <= 5) {  // Only log first 5
    std::cout << "    MeshDesc[" << (mesh_descriptors.size()-1) << "]: "
              << "vbOffset=" << desc.vbOffset << ", "
              << "ibOffset=" << desc.ibOffset << ", "
              << "vertexCount=" << desc.vertexCount << ", "
              << "indexCount=" << desc.indexCount << ", "
              << "materialID=" << desc.materialID << "\n";
}
```

Rebuild and run again. Verify that:
- vbOffset increases correctly (accumulates vertex counts)
- ibOffset increases correctly (accumulates index counts)
- Counts match expected geometry sizes

### Actions Required

1. Run test_cube.obj with DXR backend
2. Verify console output shows reasonable buffer sizes
3. Check that application doesn't crash during scene loading
4. (Optional) Add debug output for offset verification
5. Run Sponza if available
6. Document any issues or unexpected behavior

### Deliverable

Create a markdown file: `Migration/SlangIntegration/Phase2A_Analysis/2A.1.4_CPU_Test_Results.md`

```markdown
# Phase 2A.1.4: CPU Side Test Results

**Date:** [Date]

## Test 1: test_cube.obj

**Console Output:**
```
[Paste actual console output]
```

**Validation:**
- [ ] Vertex count reasonable: [Yes/No - explain]
- [ ] Index count reasonable: [Yes/No - explain]
- [ ] MeshDescriptors count: [number]
- [ ] GeometryInstances count: [number]
- [ ] No crashes during load: [Yes/No]

**Observations:**
[Any notes about the output]

## Test 2: Sponza (if available)

**Console Output:**
```
[Paste actual console output]
```

**Validation:**
- [ ] Large buffer sizes as expected: [Yes/No]
- [ ] No crashes during load: [Yes/No]
- [ ] Memory usage reasonable: [Yes/No]

**Observations:**
[Any notes about the output]

## Offset Verification (Optional)

**First 5 MeshDescriptors:**
```
[Paste debug output if added]
```

**Verification:**
- [ ] vbOffset increments correctly: [Yes/No]
- [ ] ibOffset increments correctly: [Yes/No]
- [ ] Counts match geometry sizes: [Yes/No]

## Issues Found

[List any issues, unexpected behavior, or edge cases]

## Status

- [x] Phase 2A.1 CPU refactor complete
- [x] Global buffers successfully built
- [ ] Ready to proceed to Phase 2A.2 (DXR backend)

**Note:** Rendering may be incorrect at this point - GPU backends not yet updated.
```

---

## Validation Checklist for Phase 2A.1

Before proceeding to Phase 2A.2, ensure:

- ✅ New structs added to `util/mesh.h` (Vertex, MeshDesc, GeometryInstanceData)
- ✅ Global buffer fields added to Scene struct in `util/scene.h`
- ✅ `build_global_buffers()` implemented in `util/scene.cpp`
- ✅ `build_global_buffers()` called in Scene constructor
- ✅ Code compiles without errors
- ✅ test_cube.obj loads and shows reasonable buffer sizes
- ✅ Sponza loads without crashes (if available)
- ✅ Test results documented in 2A.1.4_CPU_Test_Results.md
- ✅ **NO GPU CODE MODIFIED** (DXR/Vulkan backends not touched)

---

## Expected Behavior at This Point

**What works:**
- Scene loading from OBJ/GLTF files
- Global buffer creation and concatenation
- Debug output showing buffer sizes

**What doesn't work (expected):**
- Rendering will likely be incorrect or crash
- DXR backend still uses old per-geometry buffers
- Vulkan backend still uses old per-geometry buffers

**This is expected!** GPU backends will be updated in Phases 2A.2 and 2A.3.

---

## Common Issues and Solutions

**Issue:** Linker error - `build_global_buffers()` undefined  
**Solution:** Ensure implementation is in `scene.cpp` and not just declared in `scene.h`

**Issue:** Vertex/Index counts seem too low  
**Solution:** Check if normals/UVs are stored separately - may need to verify Geometry structure

**Issue:** Crash during `build_global_buffers()`  
**Solution:** Check bounds on `pm.material_ids` vector access, ensure parameterized_meshes is populated

**Issue:** Zero geometry instances  
**Solution:** Check if `instances` vector is populated during scene load

---

## Git Commit Recommendation

After successful completion of Phase 2A.1:

```powershell
git add util/mesh.h util/scene.h util/scene.cpp
git commit -m "Phase 2A.1: CPU scene refactor - global buffer creation

- Added Vertex, MeshDesc, GeometryInstanceData structs
- Added global buffer fields to Scene struct
- Implemented build_global_buffers() to concatenate all geometry
- Added debug output for buffer sizes
- GPU backends not yet updated (Phase 2A.2/2A.3)

Test results: test_cube.obj and Sponza load successfully"
```

---

## Next Step

**Proceed to Phase 2A.2:** DXR Backend Refactor

Use prompt file: `Phase_2A.2_DXR_Backend_Refactor.md`
