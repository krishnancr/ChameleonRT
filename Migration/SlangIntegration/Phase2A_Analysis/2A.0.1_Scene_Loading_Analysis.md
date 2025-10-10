# Task 2A.0.1: Scene Loading Analysis

**Date:** 2025-01-XX  
**Status:** Complete  
**Deliverable:** Analysis of current scene loading architecture

---

## Executive Summary

ChameleonRT's scene loading system creates **separate vertex/index buffers per Geometry** by design. The Scene struct uses a hierarchical architecture:

```
File (.obj/.gltf/.crts)
  ↓ (load_obj / load_gltf / load_crts)
Geometry (raw vertex arrays: vertices, normals, uvs, indices)
  ↓ (grouped into)
Mesh (collection of Geometry objects)
  ↓ (paired with materials)
ParameterizedMesh (mesh_id + material_ids per geometry)
  ↓ (positioned in world)
Instance (transform + parameterized_mesh_id)
```

**Critical Insight:** Each `Geometry` object contains its own `std::vector<vec3> vertices`, `std::vector<vec3> normals`, `std::vector<vec2> uvs`, and `std::vector<uvec3> indices`. This creates the **per-geometry buffer architecture** that Phase 2A aims to refactor into global buffers.

---

## Scene Data Structures

### 1. Core Data Structures (util/mesh.h)

```cpp
struct Geometry {
    std::vector<glm::vec3> vertices;  // Per-geometry vertex positions
    std::vector<glm::vec3> normals;   // Per-geometry normals
    std::vector<glm::vec2> uvs;       // Per-geometry texture coordinates
    std::vector<glm::uvec3> indices;  // Per-geometry triangle indices
};

struct Mesh {
    std::vector<Geometry> geometries;  // Collection of geometries
};

struct ParameterizedMesh {
    size_t mesh_id;                     // Index into Scene::meshes
    std::vector<uint32_t> material_ids; // One material per Geometry
};

struct Instance {
    glm::mat4 transform;                // World transform matrix
    size_t parameterized_mesh_id;       // Index into Scene::parameterized_meshes
};
```

### 2. Scene Container (util/scene.h)

```cpp
struct Scene {
    std::vector<Mesh> meshes;
    std::vector<ParameterizedMesh> parameterized_meshes;
    std::vector<Instance> instances;
    std::vector<DisneyMaterial> materials;
    std::vector<Image> textures;
    std::vector<QuadLight> lights;
    std::vector<Camera> cameras;
    // ... other members
};
```

**Relationships:**
- `Instance` → `ParameterizedMesh` (via `parameterized_mesh_id`)
- `ParameterizedMesh` → `Mesh` (via `mesh_id`)
- `Mesh` → `Geometry[]` (contains geometries)
- `ParameterizedMesh` → `materials[]` (via `material_ids` per geometry)

---

## Scene Loading Flow

### OBJ Loading (util/scene.cpp::load_obj)

**File Format:** Wavefront OBJ via `tinyobjloader`

**Key Steps:**
1. **Load OBJ data**: `tinyobj::LoadObj(&attrib, &shapes, &materials, ...)`
2. **Index Remapping**: OBJ uses separate indices for positions/normals/UVs (per-attribute indexing). ChameleonRT requires unified per-vertex indexing:
   ```cpp
   // Example: OBJ index (pos=5, normal=3, uv=7) → unified index lookup
   std::map<std::tuple<int, int, int>, uint32_t> unique_verts;
   ```
3. **Create Geometries**: One `Geometry` per OBJ shape:
   ```cpp
   for (auto &s : shapes) {
       Geometry geom;
       // Build vertices/normals/uvs arrays from remapped indices
       mesh.geometries.push_back(geom);
   }
   ```
4. **Create ParameterizedMesh**: Single parameterized mesh for entire OBJ file:
   ```cpp
   parameterized_meshes.emplace_back(meshes.size(), material_ids);
   ```
5. **Create Instance**: Single instance at origin with identity transform:
   ```cpp
   instances.emplace_back(glm::mat4(1.0f), parameterized_meshes.size() - 1);
   ```

**OBJ Loading Pattern:**
- **1 Mesh** (containing N geometries, one per OBJ shape)
- **1 ParameterizedMesh** (referencing the mesh)
- **1 Instance** (at world origin)

**Edge Cases Handled:**
- Missing normals: `if (idx.normal_index == -1)` → normals array remains empty
- Missing UVs: `if (idx.texcoord_index == -1)` → uvs array remains empty
- Note: Backends must handle empty normals (compute them) or empty UVs (provide defaults)

---

### GLTF/GLB Loading (util/scene.cpp::load_gltf)

**File Format:** glTF 2.0 (JSON or binary) via `tinygltf`

**Key Steps:**
1. **Load GLTF**: `context.LoadASCIIFromFile()` or `LoadBinaryFromFile()`
2. **Flatten GLTF**: `flatten_gltf(model)` - resolves node hierarchy into flat transforms
3. **Create Geometry per Primitive**:
   ```cpp
   for (auto &m : model.meshes) {  // GLTF "mesh" = collection of primitives
       Mesh mesh;
       for (auto &p : m.primitives) {
           Geometry geom;
           // Read POSITION attribute (required)
           Accessor<glm::vec3> pos_accessor(...);
           geom.vertices = pos_accessor data;
           
           // Read TEXCOORD_0 (optional)
           if (p.attributes.find("TEXCOORD_0") != end) {
               geom.uvs = uv_accessor data;
           }
           
           // Read indices (ushort or uint)
           geom.indices = index_accessor data;
           
           mesh.geometries.push_back(geom);
       }
       parameterized_meshes.emplace_back(meshes.size(), material_ids);
       meshes.push_back(mesh);
   }
   ```
4. **Create Instances from Scene Nodes**:
   ```cpp
   for (const auto &nid : model.scenes[model.defaultScene].nodes) {
       if (n.mesh != -1) {
           glm::mat4 transform = read_node_transform(n);
           instances.emplace_back(transform, n.mesh);  // n.mesh is parameterized_mesh index
       }
   }
   ```

**GLTF Loading Pattern:**
- **N Meshes** (one per GLTF mesh, each containing M primitives as geometries)
- **N ParameterizedMeshes** (one per GLTF mesh)
- **M Instances** (one per scene node referencing a mesh, with computed transforms)

**Critical Note:** GLTF's concept of "mesh" maps to ChameleonRT's "ParameterizedMesh" (mesh + materials). GLTF "primitives" map to ChameleonRT's "Geometry".

**Edge Cases:**
- Normals currently ignored (commented out `#if 0`) - must be computed by backend or later processing
- Only `TEXCOORD_0` supported - multiple UV sets not supported
- Only triangle primitives supported - other modes rejected

---

## Vertex Data Storage

### Current Architecture (Per-Geometry)

Each `Geometry` owns its vertex data in **separate, non-interleaved arrays**:

```cpp
struct Geometry {
    std::vector<glm::vec3> vertices;  // Array 1: positions
    std::vector<glm::vec3> normals;   // Array 2: normals (separate allocation)
    std::vector<glm::vec2> uvs;       // Array 3: UVs (separate allocation)
    std::vector<glm::uvec3> indices;  // Array 4: indices
};
```

**Memory Layout Example (2 geometries):**
```
Geometry 0:
  vertices:  [vec3, vec3, vec3, ...] (100 vertices, 1200 bytes)
  normals:   [vec3, vec3, vec3, ...] (100 normals, 1200 bytes)
  uvs:       [vec2, vec2, vec2, ...] (100 UVs, 800 bytes)
  indices:   [uvec3, uvec3, ...]     (50 triangles, 600 bytes)

Geometry 1:
  vertices:  [vec3, vec3, ...]       (200 vertices, 2400 bytes)
  normals:   [vec3, vec3, ...]       (200 normals, 2400 bytes)
  uvs:       [vec2, vec2, ...]       (200 UVs, 1600 bytes)
  indices:   [uvec3, uvec3, ...]     (100 triangles, 1200 bytes)
```

**Implications for Backends:**
- Each geometry's data is copied to **separate GPU buffers** (see Task 2A.0.2/2A.0.3)
- Shaders index into geometry-specific buffers using `GeometryIndex` from hit attributes
- No unified vertex/index addressing across geometries

---

### Target Architecture (Global Buffers) - NOT YET IMPLEMENTED

Phase 2A will refactor to:

```cpp
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

struct Scene {
    std::vector<Vertex> global_vertices;     // All vertices, interleaved
    std::vector<uint32_t> global_indices;    // All indices, remapped to global vertex space
    std::vector<MeshDesc> mesh_descs;        // Metadata per geometry
    // ... other members
};

struct MeshDesc {
    uint32_t vbOffset;      // Offset into global_vertices
    uint32_t ibOffset;      // Offset into global_indices
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t materialID;
};
```

**Memory Layout Example (same 2 geometries):**
```
global_vertices: [Vertex{pos,norm,uv}, Vertex{pos,norm,uv}, ...] (300 vertices total)
                 └─ Geometry 0 (indices 0-99) ─┘└─ Geometry 1 (indices 100-299) ─┘

global_indices:  [0, 1, 2,  0, 2, 3, ...  100, 101, 102, ...] (150 triangles total)
                 └─ Geometry 0 triangles ─┘ └─ Geometry 1 triangles ─┘

mesh_descs[0]: {vbOffset=0,   ibOffset=0,   vertexCount=100, indexCount=150, materialID=0}
mesh_descs[1]: {vbOffset=100, ibOffset=150, vertexCount=200, indexCount=300, materialID=1}
```

**Benefits:**
- Single vertex buffer for all geometries
- Single index buffer for all geometries
- Simplified shader indexing using global offsets
- Required for Slang integration (Phase 3) due to shader reflection limitations

---

## Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        FILE LOADING                                     │
└─────────────────────────────────────────────────────────────────────────┘
                                  │
              ┌───────────────────┼───────────────────┐
              │                   │                   │
         .obj file           .gltf/.glb           .crts file
              │                   │                   │
      ┌───────▼────────┐  ┌───────▼────────┐  ┌──────▼──────┐
      │ tinyobjloader  │  │   tinygltf     │  │ Custom CRTS │
      │ load_obj()     │  │ load_gltf()    │  │ load_crts() │
      └───────┬────────┘  └───────┬────────┘  └──────┬──────┘
              │                   │                   │
┌─────────────▼───────────────────▼───────────────────▼─────────────────┐
│                     GEOMETRY CREATION                                 │
│  • Index remapping (OBJ only: per-attr → per-vertex)                 │
│  • Attribute extraction (GLTF: POSITION, TEXCOORD_0, indices)        │
│  • Create Geometry objects with separate arrays:                     │
│      - std::vector<vec3> vertices                                    │
│      - std::vector<vec3> normals   (may be empty)                    │
│      - std::vector<vec2> uvs       (may be empty)                    │
│      - std::vector<uvec3> indices                                    │
└───────────────────────────────┬───────────────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────────────┐
│                     MESH ASSEMBLY                                     │
│  • Group Geometries into Mesh objects:                               │
│      Mesh { std::vector<Geometry> geometries; }                      │
│  • Add to Scene::meshes                                              │
└───────────────────────────────┬───────────────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────────────┐
│                PARAMETERIZED MESH CREATION                            │
│  • Pair Mesh with materials:                                         │
│      ParameterizedMesh { mesh_id, material_ids[] }                   │
│  • Add to Scene::parameterized_meshes                                │
└───────────────────────────────┬───────────────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────────────┐
│                     INSTANCE CREATION                                 │
│  • Position meshes in world space:                                   │
│      Instance { transform, parameterized_mesh_id }                   │
│  • Add to Scene::instances                                           │
└───────────────────────────────┬───────────────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────────────┐
│                        SCENE STRUCT                                   │
│  Scene {                                                              │
│      std::vector<Mesh> meshes;                     ← Raw geometry    │
│      std::vector<ParameterizedMesh> param_meshes;  ← Mesh + materials│
│      std::vector<Instance> instances;              ← Positioned      │
│      std::vector<DisneyMaterial> materials;                          │
│      std::vector<Image> textures;                                    │
│      ...                                                              │
│  }                                                                    │
└───────────────────────────────┬───────────────────────────────────────┘
                                │
                                ▼
                    To Backend set_scene() calls
                    (Task 2A.0.2: DXR, Task 2A.0.3: Vulkan)
```

---

## Edge Cases and Special Handling

### 1. Missing Normals
**Where:** OBJ loading (when `idx.normal_index == -1`)  
**Current Behavior:** `normals` array remains empty for that geometry  
**Backend Responsibility:** Backends must detect empty normals and compute them (face normals or smooth normals)  
**Phase 2A Impact:** Global buffer refactor must handle geometries with/without normals

### 2. Missing UVs
**Where:** OBJ/GLTF loading (when TEXCOORD attribute missing)  
**Current Behavior:** `uvs` array remains empty for that geometry  
**Backend Responsibility:** Provide default UVs (e.g., `vec2(0, 0)`) or handle in shaders  
**Phase 2A Impact:** Interleaved `Vertex` struct must accommodate missing UVs (use default value)

### 3. GLTF Normals Ignored
**Where:** GLTF loading (lines 293-300 commented out with `#if 0`)  
**Current Behavior:** GLTF normals are not loaded even if present  
**Implication:** Backends always compute normals for GLTF scenes  
**Phase 2A Impact:** Document this behavior; may enable GLTF normal loading in future

### 4. OBJ Index Remapping
**Where:** OBJ loading  
**Problem:** OBJ allows separate indices for pos/normal/UV per triangle corner  
**Solution:** Build `std::map<std::tuple<int,int,int>, uint32_t> unique_verts` to create unified vertex indices  
**Phase 2A Impact:** Global buffer loading must perform same remapping when converting to interleaved vertices

### 5. Material Assignment
**Where:** Both OBJ and GLTF  
**OBJ:** One material per shape → one material_id per Geometry  
**GLTF:** One material per primitive → one material_id per Geometry  
**Phase 2A Impact:** `MeshDesc` needs `materialID` field to preserve per-geometry materials

---

## Cardinality Patterns

### OBJ Files
- **Meshes:** 1 (single mesh containing all shapes)
- **Geometries:** N (one per OBJ shape/group)
- **ParameterizedMeshes:** 1 (references the single mesh)
- **Instances:** 1 (identity transform at origin)

**Example:** `sponza.obj` with 100 shapes:
- 1 Mesh with 100 Geometries
- 1 ParameterizedMesh with 100 material IDs
- 1 Instance

### GLTF/GLB Files
- **Meshes:** M (one per GLTF mesh)
- **Geometries:** N (one per GLTF primitive, distributed across meshes)
- **ParameterizedMeshes:** M (one per GLTF mesh)
- **Instances:** K (one per scene node referencing a mesh)

**Example:** `bistro.gltf` with 50 meshes, 200 primitives, 75 scene nodes:
- 50 Meshes (each containing 1-10 Geometries)
- 50 ParameterizedMeshes
- 75 Instances (some meshes used multiple times)

---

## Key Findings for Phase 2A Refactor

### 1. Per-Geometry Buffer Creation is Fundamental
**Current:** Each `Geometry` object maintains independent arrays (`vertices`, `normals`, `uvs`, `indices`)  
**Impact:** Backends create **separate GPU buffers per Geometry** (confirmed in upcoming Tasks 2A.0.2/2A.0.3)  
**Refactor Goal:** Merge all per-geometry arrays into global arrays during scene loading

### 2. Index Remapping is Already Implemented
**Current:** OBJ loader remaps per-attribute indices to per-vertex indices  
**Impact:** We can reuse this pattern when building global buffers from per-geometry data  
**Refactor Task:** Apply similar remapping when converting existing Geometry arrays to global buffers

### 3. ParameterizedMesh → Geometry Relationship Must Be Preserved
**Current:** `ParameterizedMesh` stores `mesh_id` (→ Mesh → Geometries array)  
**Target:** `ParameterizedMesh` must map to specific ranges in global buffers  
**Solution:** Introduce `MeshDesc` to store offsets/counts for each geometry in global buffers

### 4. Material Assignment is Per-Geometry
**Current:** `ParameterizedMesh.material_ids[i]` assigns material to `i`-th geometry  
**Target:** `MeshDesc` must include `materialID` field  
**Refactor Task:** Flatten `ParameterizedMesh.material_ids[]` into per-MeshDesc material assignments

### 5. Instance Tracking Requires New Mapping
**Current:** `Instance.parameterized_mesh_id` → ParameterizedMesh → Mesh → Geometries[]  
**Target:** Instance → MeshDesc[] (range of descriptors for that instance's geometries)  
**Solution:** Create mapping from `parameterized_mesh_id` to start index in `mesh_descs` array

---

## Recommendations for Phase 2A.1 (CPU Refactor)

Based on this analysis, the CPU refactor should:

1. **Create Global Arrays During Loading:**
   - Allocate `Scene::global_vertices` and `Scene::global_indices` upfront
   - Iterate through all geometries, appending to global arrays
   - Track offsets for each geometry

2. **Build MeshDesc Array:**
   - One `MeshDesc` per Geometry (not per Mesh)
   - Store `{vbOffset, ibOffset, vertexCount, indexCount, materialID}`
   - Derive `materialID` from `ParameterizedMesh.material_ids[geom_index]`

3. **Create Instance → MeshDesc Mapping:**
   - Store `mesh_desc_start_index` per ParameterizedMesh
   - Instances can then compute: `mesh_desc_index = pm_to_mesh_desc_start[inst.parameterized_mesh_id] + geom_offset`

4. **Handle Missing Data Gracefully:**
   - If `geom.normals.empty()`, compute face normals or store zero normals in global buffer
   - If `geom.uvs.empty()`, use `vec2(0, 0)` default in global buffer

5. **Preserve Legacy Structures (Temporarily):**
   - Keep `Geometry`, `Mesh`, etc. intact during Phase 2A for debugging
   - Backends will read from global buffers, but CPU data remains dual-path until Phase 3

---

## Next Steps

- **Task 2A.0.2:** Analyze DXR backend buffer creation (`backends/dxr/render_dxr.cpp::set_scene()`)
- **Task 2A.0.3:** Analyze Vulkan backend buffer creation (`backends/vulkan/render_vulkan.cpp::set_scene()`)
- **Phase 2A.1 Planning:** Use findings from all three analysis tasks to design global buffer refactor strategy

---

**Analysis Complete:** Scene loading architecture fully documented. Ready to proceed to backend buffer analysis.
