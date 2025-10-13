# Phase 2A.1: CPU Scene Refactor Plan

**Goal:** Create global buffers during scene loading  
**Status:** ✅ COMPLETE (Commits: 66f10b01, e89498ed)  
**Date:** October 10-11, 2025

---

## Overview

Refactor scene loading to create consolidated global buffers instead of per-geometry buffers. This is a CPU-only change - no GPU backend code is modified in this phase.

---

## New Data Structures

### Vertex Structure (util/mesh.h)

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
```

### MeshDesc Structure (util/mesh.h)

```cpp
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
```

### GeometryInstanceData Structure (util/mesh.h)

```cpp
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

---

## Scene Structure Changes

**File:** `util/scene.h`

Added new fields to Scene struct:

```cpp
struct Scene {
    // Existing fields...
    std::vector<Mesh> meshes;
    std::vector<ParameterizedMesh> parameterized_meshes;
    std::vector<Instance> instances;
    
    // NEW: Global buffers for GPU upload
    std::vector<Vertex> global_vertices;           // All vertices concatenated
    std::vector<uint32_t> global_indices;          // All indices concatenated (as uints)
    std::vector<MeshDesc> mesh_descriptors;        // Metadata per geometry
    std::vector<GeometryInstanceData> geometry_instances; // Metadata per instance
    std::vector<glm::mat4> transform_matrices;     // Transform per instance
    
    // Helper function to build global buffers
    void build_global_buffers();
};
```

---

## Implementation

**File:** `util/scene.cpp`

Implemented `build_global_buffers()` to:
1. Iterate through all geometries
2. Concatenate vertices into `global_vertices`
3. Concatenate indices into `global_indices` (with offset adjustment)
4. Create `MeshDesc` for each geometry (stores offsets, counts, material ID)
5. Create `GeometryInstanceData` for each TLAS instance
6. Build `transform_matrices` array

**Key Implementation Details:**
- Vertex offset adjustment for indices
- Handle missing normals/UVs (default values)
- Track mesh ID to geometry mapping
- Build instance metadata for TLAS

---

## Tasks Completed

### 2A.1.1: Add New Structs ✅
- Added `Vertex`, `MeshDesc`, `GeometryInstanceData` to `util/mesh.h`
- Added global buffer fields to `Scene` struct in `util/scene.h`

### 2A.1.2: Implement `build_global_buffers()` ✅
- Implemented concatenation logic in `util/scene.cpp`
- Handle vertex offset adjustment for indices
- Handle missing normals/UVs gracefully
- Track mesh ID to geometry mapping

### 2A.1.3: Call After Scene Load ✅
- Modified scene loading to call `build_global_buffers()` after load
- Integrated into scene construction pipeline

### 2A.1.4: Test CPU Side ✅
- Tested with test_cube.obj
- Tested with Sponza
- Verified buffer sizes and offsets
- No crashes, reasonable buffer sizes

---

## Validation Results

See detailed results in:
- [Test_Results.md](Test_Results.md) - test_cube.obj validation
- [Sponza_Validation.md](Sponza_Validation.md) - Sponza scene validation
- [Validation_Complete.md](Validation_Complete.md) - Final validation summary

---

## Git Commits

- **66f10b01** - Initial implementation
- **e89498ed** - Validation complete

---

## Next Phase

Phase 2A.2: DXR Backend Refactor
- Update DXR to use global buffers
- Remove local root signatures (space1)
- Shader changes for global buffer access
- See [../DXR/](../DXR/) for details
