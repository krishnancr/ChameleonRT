# Phase 1 Architecture Diagram

## Before (DXR-Specific Local Root Signatures)

```
┌─────────────────────────────────────────────────────────┐
│ space0 (Global Resources)                              │
├─────────────────────────────────────────────────────────┤
│ t0:  scene (TLAS)                                       │
│ t1:  material_params                                    │
│ t2:  lights                                             │
│ t3:  textures[] ← UNBOUNDED (claims t3 → UINT_MAX)     │
│                                                         │
│ b0:  ViewParams                                         │
│ b1:  SceneParams                                        │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│ space1 (Per-Geometry Local Root Signatures) ❌ PROBLEM │
├─────────────────────────────────────────────────────────┤
│ t0:  vertices    ← Different buffer per hit!            │
│ t1:  indices     ← Slang can't express this cross-API   │
│ t2:  normals                                            │
│ t3:  uvs                                                │
│ b0:  MeshData                                           │
└─────────────────────────────────────────────────────────┘
```

**Problem:** DXR local root signatures allow different buffers per ray hit. Vulkan uses buffer_reference (address-based). Slang compiler cannot express DXR local root signatures in a cross-API way.

---

## After (Slang-Compatible Global Buffers)

```
┌─────────────────────────────────────────────────────────┐
│ space0 (Global Resources) ✅ SOLUTION                  │
├─────────────────────────────────────────────────────────┤
│ t0:  scene (TLAS)                                       │
│ t1:  material_params                                    │
│ t2:  lights                                             │
│                                                         │
│ t10: globalVertices   ← NEW: ALL vertices              │
│ t11: globalIndices    ← NEW: ALL indices               │
│ t12: globalNormals    ← NEW: ALL normals               │
│ t13: globalUVs        ← NEW: ALL UVs                   │
│ t14: meshDescs        ← NEW: Per-mesh metadata         │
│                                                         │
│ t30: textures[]       ← Moved from t3 to free up space │
│                                                         │
│ b0:  ViewParams                                         │
│ b1:  SceneParams                                        │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│ space1 (OLD - Will be removed in Phase 5)              │
├─────────────────────────────────────────────────────────┤
│ t0:  vertices    ← Still exists for backward compat     │
│ t1:  indices                                            │
│ t2:  normals                                            │
│ t3:  uvs                                                │
│ b0:  MeshData                                           │
└─────────────────────────────────────────────────────────┘
```

**Solution:** Single global buffer bound ONCE at pipeline level. Shader uses offsets to access per-mesh data. Works in both DXR (register-based) and Vulkan (address-based).

---

## Data Flow: Old vs New

### OLD (Local Root Signatures)
```
TraceRay() → Hit Geometry 0
                ↓
    Local Root Signature binds:
        vertices  → geometry0_vertices_buffer
        indices   → geometry0_indices_buffer
                ↓
    ClosestHit:
        idx = indices[PrimitiveIndex()]  ← Points to geometry0 data
        va = vertices[idx.x]
```

### NEW (Global Buffers with Offsets)
```
TraceRay() → Hit Instance 3
                ↓
    Global Root Signature binds:
        globalVertices → [all_vertices]
        globalIndices  → [all_indices]
        meshDescs      → [metadata]
                ↓
    ClosestHit_GlobalBuffers:
        meshID = InstanceID()                    ← Get mesh ID (3)
        mesh = meshDescs[meshID]                 ← Load metadata
        idx = globalIndices[mesh.ibOffset + PrimitiveIndex()]
        va = globalVertices[mesh.vbOffset + idx.x]
```

**Key difference:** NEW version uses OFFSET into global buffers, not separate buffers per geometry.

---

## Memory Layout Example (test_cube.obj)

### CPU Side (Scene Class)
```
global_vertices:     [(−1,−1,−1), (1,−1,−1), (1,1,−1), ... ] ← 8 vertices
global_indices:      [(0,1,2), (0,2,3), (4,5,6), ... ]       ← 12 triangles
global_normals:      []                                       ← 0 normals
global_uvs:          []                                       ← 0 UVs
mesh_descriptors:    [MeshDesc{vb=0, ib=0, norm=0, uv=0}]   ← 1 mesh
```

### GPU Side (Shader Buffers)
```
globalVertices[0..7]:   [(−1,−1,−1), (1,−1,−1), (1,1,−1), ... ]
globalIndices[0..11]:   [(0,1,2), (0,2,3), (4,5,6), ... ]
globalNormals[]:        (empty)
globalUVs[]:            (empty)
meshDescs[0]:           {vbOffset=0, ibOffset=0, ...}
```

### Shader Access Pattern
```hlsl
// In ClosestHit_GlobalBuffers:
uint32_t meshID = InstanceID();              // meshID = 0
MeshDesc mesh = meshDescs[meshID];           // Load mesh metadata
                                             // mesh.vbOffset = 0
                                             // mesh.ibOffset = 0

uint3 idx = globalIndices[mesh.ibOffset + PrimitiveIndex()];
// If PrimitiveIndex() = 5, accesses globalIndices[0 + 5] = (4,5,6)

float3 va = globalVertices[mesh.vbOffset + idx.x];
// Accesses globalVertices[0 + 4] = 5th vertex
```

---

## Why This Works for Slang

### DXR (Register-Based)
```hlsl
// Shader declares buffers with registers
StructuredBuffer<float3> globalVertices : register(t10, space0);
StructuredBuffer<uint3> globalIndices : register(t11, space0);

// C++ binds buffers to descriptor table
descriptor_table.set(10, globalVertices_srv);
descriptor_table.set(11, globalIndices_srv);
```

### Vulkan (Address-Based)
```glsl
// Shader declares buffers with bindings
layout(set = 0, binding = 10) buffer GlobalVertices { vec3 data[]; };
layout(set = 0, binding = 11) buffer GlobalIndices { uvec3 data[]; };

// C++ binds buffers to descriptor set
vkUpdateDescriptorSets(..., binding=10, globalVertices_buffer);
vkUpdateDescriptorSets(..., binding=11, globalIndices_buffer);
```

**Slang can compile to BOTH!** Same semantic (offset-based indexing), different syntax.

---

## Register Number Collision Fix

### Problem
```
t3:  textures[]  ← Unbounded array claims t3 → t4294967295
t10: globalVertices  ← COLLISION! DXC validation error
```

### Solution
```
t10-t14: Global buffers (only uses 5 slots)
t30:     textures[]     ← Moved to t30, now claims t30 → t4294967295
         (no collision, plenty of space)
```

**Why t30?** Arbitrary choice, but:
- Leaves t15-t29 for future global resources
- Clear visual separation (textures are "special")
- Simple 1-line change in shader + 1-line change in C++

---

## Phase 1 Deliverables

```
✅ Shader declarations    (render_dxr.hlsl lines 299-307)
✅ New ClosestHit shader  (render_dxr.hlsl lines 364-414)
✅ CPU data structures    (scene.h, scene.cpp, mesh.h)
✅ Validation output      (console logs confirming correctness)
✅ Register collision fix (textures moved to t30)
✅ Build without -Vd      (no validation errors)
✅ Documentation          (this directory)
```

---

## Next: Phase 2

```
Create GPU buffers:
  global_vertices_buffer   ← D3D12_RESOURCE (GPU memory)
  global_indices_buffer
  global_normals_buffer
  global_uvs_buffer
  mesh_descs_buffer

Upload CPU data to GPU:
  Upload(scene.global_vertices → global_vertices_buffer)
  Upload(scene.global_indices  → global_indices_buffer)
  // etc...

Console validation:
  Print buffer sizes, confirm uploads
```

**Status:** Ready to proceed! 🚀
