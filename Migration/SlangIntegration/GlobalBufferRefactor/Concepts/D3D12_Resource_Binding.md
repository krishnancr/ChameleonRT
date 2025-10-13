# D3D12/DXR Resource Binding - Conceptual Guide

**Purpose:** Explain how shaders access GPU resources in D3D12/DXR  
**Audience:** Developers learning D3D12 resource binding model  
**Date:** 2025-10-11

---

## The Big Picture: How Shaders Access Data

When a shader runs on the GPU, it needs to access various data:
- Textures (images)
- Buffers (arrays of data like vertices, materials)
- Constants (small amounts of data like camera position)

**The Challenge:** GPU memory is a huge flat space. How does the shader know WHERE your vertex buffer is? WHERE your texture is?

**The Solution:** A multi-level indirection system:
1. **Registers** - Shader variables with names like `t0`, `t1`, `b0`
2. **Descriptors** - Pointers/metadata about resources
3. **Descriptor Heaps** - Arrays that hold descriptors
4. **Root Signature** - Contract between CPU and shader about what registers exist

Think of it like a **library system**:
- **Registers** = Call numbers on book spines (t0, t1, t2...)
- **Descriptors** = Library catalog cards (metadata about each book)
- **Descriptor Heap** = The catalog cabinet (holds all the cards)
- **Root Signature** = The library's organization system (how books are categorized)

---

## Part 1: Registers - The Shader's View

### What Are Registers?

In HLSL (D3D12 shader language), you declare resources with **register bindings**:

```hlsl
Texture2D myTexture : register(t0);          // Texture at register t0
StructuredBuffer<Vertex> vertices : register(t1);  // Buffer at register t1
cbuffer Camera : register(b0) { ... }        // Constants at register b0
RWTexture2D<float4> output : register(u0);   // Writable texture at u0
SamplerState sampler : register(s0);         // Sampler at s0
```

### Register Types (The Letters)

| Prefix | Type | Description | Example Use |
|--------|------|-------------|-------------|
| **t** | Texture/SRV | **Read-only** textures or buffers | `Texture2D tex : register(t0)` |
| **u** | UAV | **Read-write** buffers or textures | `RWTexture2D output : register(u0)` |
| **b** | CBV | **Constants** (small, fast data) | `cbuffer Params : register(b0)` |
| **s** | Sampler | Texture sampling behavior | `SamplerState samp : register(s0)` |

**Memory Aid:**
- **t** = **T**exture (really: read-only resource)
- **u** = **U**nordered access (read-write)
- **b** = **B**uffer (really: constants)
- **s** = **S**ampler

### Register Numbers

The number after the letter is the **slot** within that type:

```hlsl
register(t0)   // First SRV slot
register(t1)   // Second SRV slot
register(t2)   // Third SRV slot
register(t10)  // Eleventh SRV slot
```

**Important:** Each register type has its OWN numbering:
- You can have BOTH `t0` AND `b0` AND `u0` - they don't conflict!
- Think of them as separate arrays: `t[]`, `u[]`, `b[]`, `s[]`

---

## Part 2: Spaces - Multiple Binding Namespaces

### What Are Register Spaces?

Sometimes you need MULTIPLE sets of registers. That's where **spaces** come in:

```hlsl
// Space 0 (default - global resources)
Texture2D globalTexture : register(t0, space0);
StructuredBuffer<Material> materials : register(t1, space0);

// Space 1 (local resources - per-object)
StructuredBuffer<Vertex> vertices : register(t0, space1);  // Different t0!
StructuredBuffer<uint3> indices : register(t1, space1);    // Different t1!
```

**Key Insight:** `register(t0, space0)` and `register(t0, space1)` are **DIFFERENT** registers!

### Why Use Multiple Spaces?

**Scenario:** Ray tracing with multiple geometries

**Without Spaces (Problem):**
```hlsl
// Global data (shared by all objects)
Texture2D globalTextures[100] : register(t0);  // Uses t0-t99

// Per-object data (HOW DO WE BIND THIS?)
StructuredBuffer<Vertex> vertices : register(t???);  // Can't use t0, it's taken!
```

**With Spaces (Solution):**
```hlsl
// Global data (space0)
Texture2D globalTextures[100] : register(t0, space0);  // t0-t99 in space0

// Per-object data (space1) - uses SAME register numbers!
StructuredBuffer<Vertex> vertices : register(t0, space1);  // t0 in space1 (different!)
StructuredBuffer<uint3> indices : register(t1, space1);
```

**Analogy:** Spaces are like **floors in a building**:
- **Space 0** = Ground floor (room t0, room t1, room t2...)
- **Space 1** = Second floor (room t0, room t1, room t2...)
- Same room numbers, different floors!

### Common Use Cases

| Space | Typical Use | Binding Frequency |
|-------|-------------|-------------------|
| **space0** | Global resources (scene-wide) | Once per frame |
| **space1** | Local resources (per-object) | Changes per draw/dispatch |
| **space2+** | Specialized (rarely used) | Varies |

---

## Part 3: Descriptors - The Resource Metadata

### What Is a Descriptor?

A **descriptor** is a small chunk of data (32-64 bytes) that describes a resource:

```
Descriptor for a Texture:
- GPU memory address: 0x1A2B3C4D5E6F
- Width: 1024 pixels
- Height: 1024 pixels
- Format: RGBA8 (4 bytes per pixel)
- Mip levels: 10
```

**Why Not Just Use Pointers?**
- GPU needs to know HOW to interpret the data (format, size, etc.)
- Descriptors provide this metadata efficiently

### Descriptor Types (Match Register Types)

| Descriptor Type | Register Type | Purpose |
|----------------|---------------|---------|
| **SRV** (Shader Resource View) | `t` | Read-only access to textures/buffers |
| **UAV** (Unordered Access View) | `u` | Read-write access |
| **CBV** (Constant Buffer View) | `b` | Small constants data |
| **Sampler** | `s` | Texture sampling parameters |

**SRV Example:**
```cpp
// C++ side: Create descriptor for global_vertex_buffer
D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {0};
srv_desc.Format = DXGI_FORMAT_UNKNOWN;  // Structured buffer
srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
srv_desc.Buffer.NumElements = 1000;  // 1000 vertices
srv_desc.Buffer.StructureByteStride = 48;  // sizeof(Vertex)
device->CreateShaderResourceView(global_vertex_buffer.get(), &srv_desc, handle);
```

This creates metadata saying: "This buffer has 1000 elements, each 48 bytes."

---

## Part 4: Descriptor Heaps - The Descriptor Storage

### What Is a Descriptor Heap?

A **descriptor heap** is a GPU-visible **array of descriptors**:

```
Descriptor Heap (Array of Descriptors):
[0] -> Descriptor for render target (UAV)
[1] -> Descriptor for accumulation buffer (UAV)
[2] -> Descriptor for TLAS (SRV)
[3] -> Descriptor for materials buffer (SRV)
[4] -> Descriptor for lights buffer (SRV)
[5] -> Descriptor for view constants (CBV)
[6] -> Descriptor for texture 0 (SRV)
[7] -> Descriptor for texture 1 (SRV)
...
[15] -> Descriptor for globalVertices (SRV)  <-- t10
[16] -> Descriptor for globalIndices (SRV)   <-- t11
[17] -> Descriptor for meshDescs (SRV)       <-- t12
```

**Key Points:**
- Descriptor heaps are allocated on the GPU
- You can have multiple heaps (CBV/SRV/UAV heap, Sampler heap)
- Shaders access descriptors via the **Root Signature** (explained next)

### Descriptor Heap Types

| Heap Type | Stores | Max Descriptors |
|-----------|--------|----------------|
| **CBV_SRV_UAV** | Constants, read-only, read-write | 1,000,000 |
| **SAMPLER** | Texture samplers | 2,048 |
| **RTV** | Render targets (not shader-visible) | N/A |
| **DSV** | Depth/stencil (not shader-visible) | N/A |

**In ChameleonRT DXR:**
```cpp
// Build descriptor heap with space for all our resources
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_uav_range(2, 0, 0)     // 2 UAVs starting at u0 (render target, accum buffer)
    .add_srv_range(3, 0, 0)     // 3 SRVs starting at t0 (TLAS, materials, lights)
    .add_cbv_range(1, 0, 0)     // 1 CBV at b0 (view constants)
    .add_srv_range(textures.size(), 3, 0)  // N SRVs starting at t3 (textures)
    .add_srv_range(3, 10, 0)    // 3 SRVs starting at t10 (global buffers)
    .create(device.Get());
```

This creates a heap with space for:
- UAVs at indices 0-1 (for u0, u1)
- SRVs at indices 2-4 (for t0, t1, t2)
- CBV at index 5 (for b0)
- SRVs at indices 6+ (for t3, t4, t5...)
- SRVs at indices (6+N), (7+N), (8+N) (for t10, t11, t12)

---

## Part 5: Root Signature - The Binding Contract

### What Is a Root Signature?

The **root signature** defines:
1. What registers exist in the shader
2. How those registers map to descriptor heap entries
3. What data is passed directly vs. via descriptor table

**Analogy:** It's the **wiring diagram** between CPU and GPU.

### Root Signature Components

```cpp
// Simplified example
CD3DX12_ROOT_PARAMETER1 rootParams[2];

// Parameter 0: Descriptor table (points into descriptor heap)
CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 13, 0, 0);  // t0-t12 in space0
rootParams[0].InitAsDescriptorTable(1, ranges);

// Parameter 1: Inline constants (32-bit values)
rootParams[1].InitAsConstants(4, 0, 0);  // 4 DWORDs at b0, space0

// Create root signature
D3D12_ROOT_SIGNATURE_DESC desc;
desc.NumParameters = 2;
desc.pParameters = rootParams;
// ... (create signature)
```

### Descriptor Tables vs. Root Constants

| Method | Speed | Size Limit | Use Case |
|--------|-------|-----------|----------|
| **Descriptor Table** | Slower | Unlimited | Large resources (buffers, textures) |
| **Root Constants** | Faster | 64 DWORDs | Small data (matrix, few floats) |
| **Root Descriptors** | Medium | 1 resource | Single buffer (rare) |

**In ChameleonRT:**
- Most resources use **descriptor tables** (points to descriptor heap)
- Some small constants might use **root constants** (direct values)

---

## Part 6: How It All Connects - The Full Pipeline

### Step-by-Step: From C++ to Shader

Let's trace how `globalVertices` buffer gets accessed in the shader:

#### 1. **C++ Side: Create the Buffer**

```cpp
// Create GPU buffer
global_vertex_buffer = dxr::Buffer::device(
    device.Get(),
    scene.global_vertices.size() * sizeof(Vertex),
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
);

// Upload data (CPU -> GPU)
cmd_list->CopyResource(global_vertex_buffer.get(), upload_buffer.get());
```

#### 2. **C++ Side: Create Descriptor (SRV)**

```cpp
// Create descriptor heap entry for this buffer
D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {0};
srv_desc.Format = DXGI_FORMAT_UNKNOWN;  // Structured buffer
srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
srv_desc.Buffer.NumElements = global_vertex_buffer.size() / sizeof(Vertex);
srv_desc.Buffer.StructureByteStride = sizeof(Vertex);

// Write descriptor to heap at specific location (for t10)
D3D12_CPU_DESCRIPTOR_HANDLE handle = /* heap position for t10 */;
device->CreateShaderResourceView(global_vertex_buffer.get(), &srv_desc, handle);
```

#### 3. **C++ Side: Build Descriptor Heap**

```cpp
// Allocate descriptor heap with enough space
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_srv_range(3, 10, 0)  // 3 SRVs at t10-t12, space0
    .create(device.Get());
```

#### 4. **C++ Side: Create Root Signature**

```cpp
// Define root signature: "Shader can access t0-t12 via descriptor table"
CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 13, 0, 0);  // t0-t12, space0

CD3DX12_ROOT_PARAMETER1 rootParams[1];
rootParams[0].InitAsDescriptorTable(1, ranges);

// Create root signature from this definition
// ...
```

#### 5. **C++ Side: Bind Resources Before Rendering**

```cpp
// Set descriptor heaps (make them active)
cmd_list->SetDescriptorHeaps(1, &raygen_desc_heap.get());

// Set root signature (activate the binding contract)
cmd_list->SetComputeRootSignature(root_signature.get());

// Bind descriptor table to root parameter 0
cmd_list->SetComputeRootDescriptorTable(0, heap_gpu_handle);
```

#### 6. **Shader Side: Declare and Use**

```hlsl
// Declare resource at register t10, space0
StructuredBuffer<Vertex> globalVertices : register(t10, space0);

[shader("closesthit")]
void ClosestHit(...)
{
    // Access the buffer - GPU finds it via:
    // - Root signature says "t10 is in descriptor table"
    // - Descriptor table points to heap entry for t10
    // - Heap entry contains buffer address and metadata
    Vertex v = globalVertices[index];  // ✅ Works!
}
```

### The Data Flow Diagram

```
CPU (C++ Code)                      GPU (Shader Code)
==============                      =================

global_vertex_buffer                StructuredBuffer<Vertex> globalVertices
   (GPU memory)                        : register(t10, space0);
       |                                        ^
       v                                        |
CreateShaderResourceView()              [Root Signature Maps]
       |                                        |
       v                                        |
Descriptor Heap Entry                   [Descriptor Table]
   (metadata at index N)                       |
       ^                                        |
       |                                  cmd_list->
  .add_srv_range(3, 10, 0) ----------> SetComputeRootDescriptorTable()
       |
       v
Root Signature Definition
  "t10-t12 accessible via table"
```

---

## Part 7: Phase 2A.2 Changes Explained

### Before Phase 2A.2 (Per-Geometry Buffers)

**Shader:**
```hlsl
// Space 1 - per-geometry (changes for each mesh)
StructuredBuffer<float3> vertices : register(t0, space1);
StructuredBuffer<uint3> indices : register(t1, space1);
cbuffer MeshData : register(b0, space1) { uint materialID; }
```

**C++ Binding:**
- Each geometry had its own vertex/index buffer
- Shader binding table (SBT) had **per-geometry shader records**
- Each hit group had **local root signature** (space1)
- CPU bound different buffers for each geometry

**Problem:** Lots of descriptor management, complex SBT

### After Phase 2A.2 (Global Buffers)

**Shader:**
```hlsl
// Space 0 - global (set once)
StructuredBuffer<Vertex> globalVertices : register(t10, space0);
StructuredBuffer<uint> globalIndices : register(t11, space0);
StructuredBuffer<MeshDesc> meshDescs : register(t12, space0);

[shader("closesthit")]
void ClosestHit(...)
{
    uint meshID = InstanceID();  // Which mesh did we hit?
    MeshDesc mesh = meshDescs[meshID];  // Get mesh metadata
    
    // Index into global buffers using offsets
    uint triIndex = mesh.ibOffset + PrimitiveIndex() * 3;
    uint idx0 = globalIndices[triIndex + 0];
    Vertex v0 = globalVertices[idx0];
    // ...
}
```

**C++ Binding:**
- Single set of global buffers (all vertices, all indices, all mesh metadata)
- Descriptor heap has entries at t10, t11, t12
- Root signature includes these in descriptor table
- SBT simplified - no per-geometry records needed
- Bound once, used for all geometries

**Benefits:**
- Simpler descriptor management
- Fewer API calls
- Easier to add Slang later (fewer bindings to manage)

---

## Part 8: D3D12 vs. Vulkan - Same Concepts, Different Names

| Concept | D3D12 | Vulkan | Explanation |
|---------|-------|--------|-------------|
| **Register** | `register(t0)` | `binding = 0` | Shader binding point |
| **Space** | `space0` | `set = 0` | Binding namespace |
| **Descriptor** | Descriptor | Descriptor | Resource metadata |
| **Descriptor Heap** | Descriptor Heap | Descriptor Pool | Storage for descriptors |
| **Root Signature** | Root Signature | Pipeline Layout | Binding contract |
| **SRV** | SRV | Sampled Image / Storage Buffer | Read-only resource |
| **UAV** | UAV | Storage Image / Buffer | Read-write resource |
| **CBV** | CBV | Uniform Buffer | Constants |

**Key Insight:** The CONCEPTS are the same across APIs:
1. Resources (buffers, textures)
2. Descriptors (metadata about resources)
3. Descriptor storage (heaps/pools)
4. Binding points (registers/bindings)
5. Binding namespaces (spaces/sets)
6. Binding contract (root sig/pipeline layout)

**Vulkan Example (equivalent to D3D12):**

```glsl
// Vulkan GLSL - same concept as D3D12 HLSL
layout(set = 0, binding = 10) buffer GlobalVertices { Vertex data[]; };
layout(set = 0, binding = 11) buffer GlobalIndices { uint data[]; };
layout(set = 0, binding = 12) buffer MeshDescs { MeshDesc data[]; };

// set = 0  --> D3D12: space0
// binding = 10 --> D3D12: register(t10)
```

---

## Part 9: Common Debugging Scenarios

### Issue: "Descriptor heap too small"

**Error:** Creating SRV fails or D3D12 validation error

**Cause:** Descriptor heap doesn't have enough entries

**Fix:**
```cpp
// Before:
.add_srv_range(3, 0, 0)  // Only 3 SRV slots

// After:
.add_srv_range(3, 0, 0)   // t0-t2
.add_srv_range(3, 10, 0)  // t10-t12 (added)
```

### Issue: "Resource not bound" or black screen

**Possible Causes:**

1. **Descriptor not created**
   ```cpp
   // Missing: CreateShaderResourceView() for t10
   ```

2. **Descriptor heap not bound**
   ```cpp
   // Missing: SetDescriptorHeaps() before draw/dispatch
   ```

3. **Root signature doesn't include register**
   ```cpp
   // Root signature has range t0-t9, but shader uses t10!
   ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 0, 0);  // WRONG: only t0-t9
   ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 13, 0, 0);  // CORRECT: t0-t12
   ```

4. **Register mismatch**
   ```hlsl
   // Shader: register(t10)
   // C++ descriptor created at t11 position
   ```

### Issue: "Wrong data in shader"

**Possible Causes:**

1. **Wrong descriptor heap offset**
   ```cpp
   // Created descriptor at wrong heap position
   // Should be at position for t10, but created at position for t5
   ```

2. **Wrong buffer bound**
   ```cpp
   // Created SRV for wrong buffer
   device->CreateShaderResourceView(wrong_buffer.get(), &srv_desc, handle);
   ```

3. **StructureByteStride mismatch**
   ```cpp
   srv_desc.Buffer.StructureByteStride = 32;  // WRONG: Vertex is 48 bytes
   srv_desc.Buffer.StructureByteStride = sizeof(Vertex);  // CORRECT
   ```

---

## Part 10: Quick Reference Cheat Sheet

### Register Types
- **t** = Texture/Buffer (read-only) → SRV
- **u** = Unordered Access (read-write) → UAV
- **b** = Constants (small data) → CBV
- **s** = Sampler (texture sampling) → Sampler

### Creating Descriptors

**SRV (Structured Buffer):**
```cpp
srv_desc.Format = DXGI_FORMAT_UNKNOWN;
srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
srv_desc.Buffer.StructureByteStride = sizeof(MyStruct);
```

**SRV (Typed Buffer - uint):**
```cpp
srv_desc.Format = DXGI_FORMAT_R32_UINT;
srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
srv_desc.Buffer.StructureByteStride = 0;  // Raw typed buffer
```

**UAV (Texture):**
```cpp
uav_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
```

### Descriptor Heap Builder Pattern

```cpp
heap = DescriptorHeapBuilder()
    .add_uav_range(count, start_register, space)  // UAVs
    .add_srv_range(count, start_register, space)  // SRVs
    .add_cbv_range(count, start_register, space)  // CBVs
    .create(device);
```

### Root Signature Pattern

```cpp
CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, count, base_register, space);

CD3DX12_ROOT_PARAMETER1 params[1];
params[0].InitAsDescriptorTable(1, ranges);

// ... create root signature from params
```

---

## Summary: The Mental Model

**Think of GPU resource binding as a library:**

1. **Registers (t0, b0, u0)** = Call numbers on book spines
2. **Spaces (space0, space1)** = Floors in the library building
3. **Descriptors** = Catalog cards with book metadata
4. **Descriptor Heap** = The catalog cabinet holding all cards
5. **Root Signature** = The library's organization rules
6. **Binding Resources** = Librarian pulling books to the reading table
7. **Shader Access** = You reading the book

**When debugging:**
- Check the "call number" (register) matches
- Check the "floor" (space) is correct
- Check the "catalog card" (descriptor) exists
- Check the "card is in the cabinet" (descriptor heap has entry)
- Check the "organization rules allow access" (root signature includes it)
- Check the "librarian pulled the right book" (correct buffer bound)

---

## Further Reading

- **Microsoft Docs:** [Resource Binding in HLSL](https://docs.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-in-hlsl)
- **Microsoft Docs:** [Root Signatures](https://docs.microsoft.com/en-us/windows/win32/direct3d12/root-signatures)
- **Microsoft Docs:** [Descriptor Heaps](https://docs.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps)

**ChameleonRT-specific:**
- `backends/dxr/dxr_utils.h` - Descriptor heap builder implementation
- `backends/dxr/render_dxr.cpp` - See `build_descriptor_heap()`, `build_shader_resource_heap()`

---

**Hope this helps! The key is understanding the INDIRECTION:**

**Shader Register → Root Signature → Descriptor Table → Descriptor Heap Entry → Descriptor → Actual GPU Buffer**

Each layer adds flexibility and management overhead, but enables powerful binding scenarios like our global buffer refactor.
