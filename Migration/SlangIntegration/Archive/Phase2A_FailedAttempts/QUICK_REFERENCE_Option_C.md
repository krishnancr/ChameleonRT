# Quick Reference: Option C TLAS Change

**For:** Phase 2A.2 (DXR) and Phase 2A.3 (Vulkan)  
**What:** 2-line change to TLAS instance setup  
**Why:** Enable direct mesh lookup from shader

---

## DXR (Phase 2A.2)

**File:** `backends/dxr/render_dxr.cpp`  
**Function:** `set_scene()`  
**Search for:** `D3D12_RAYTRACING_INSTANCE_DESC`

### Change These 2 Lines:

```cpp
// ❌ REMOVE:
buf[i].InstanceID = i;
buf[i].InstanceContributionToHitGroupIndex = 
    parameterized_mesh_sbt_offsets[inst.parameterized_mesh_id];

// ✅ REPLACE WITH:
buf[i].InstanceID = inst.parameterized_mesh_id;
buf[i].InstanceContributionToHitGroupIndex = 0;
```

---

## Vulkan (Phase 2A.3)

**File:** `backends/vulkan/render_vulkan.cpp`  
**Function:** `set_scene()`  
**Search for:** `VkAccelerationStructureInstanceKHR`

### Change This Line:

```cpp
// ❌ REMOVE:
instance.instanceCustomIndex = i;

// ✅ REPLACE WITH:
instance.instanceCustomIndex = inst.parameterized_mesh_id;
```

---

## Why This Works

**Shader needs to index `meshDescs[]` array:**
- `meshDescs[0..N]` where N = number of parameterized meshes
- Each entry describes a unique mesh (geometry + material)

**TLAS provides `InstanceID()` / `gl_InstanceCustomIndexEXT`:**
- Default: Instance index (0, 1, 2, ... for each instance)
- **Problem:** 100 instances might use only 15 unique meshes
- **Solution:** Set to `parameterized_mesh_id` instead!

**Result:**
```hlsl
// DXR
uint meshID = InstanceID();  // Returns parameterized_mesh_id directly

// Vulkan
uint meshID = gl_InstanceCustomIndexEXT;  // Returns parameterized_mesh_id directly

// Both:
MeshDesc mesh = meshDescs[meshID];  // Direct lookup, no indirection needed!
```

---

## Validation

After change, test:

1. **Single instance scene:** Should render correctly
2. **Multiple instances, same mesh:** All should render (same appearance, different transforms)
3. **Multiple instances, different meshes:** Each should show correct mesh

---

## Common Mistakes

❌ **Forgetting to set `InstanceContributionToHitGroupIndex = 0`**
- Old code used this for per-geometry hit group indexing
- We removed per-geometry hit groups (no more space1)
- Must set to 0 (single hit group for all)

❌ **Setting `InstanceID = i` in some instances**
- ALL instances must use `parameterized_mesh_id`
- Inconsistency will cause wrong geometry to render

---

## See Also

- `KNOWN_LIMITATIONS.md` - Full analysis and trade-offs
- `2A.2.0_Option_C_Summary.md` - Decision summary
- `Phase_2A.2_DXR_Backend_Refactor.md` - Full implementation guide
