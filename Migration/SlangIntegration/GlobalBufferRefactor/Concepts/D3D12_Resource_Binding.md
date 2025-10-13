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
       |                                        |
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

---

## Part 11: Frequently Asked Questions (FAQ)

### Q1: Are SRVs only for textures?

**A: No - SRVs can be textures, buffers, or acceleration structures.**

While the `t` register is commonly associated with textures, **SRVs encompass any read-only resource**:

```hlsl
// ALL of these use 't' registers (SRVs):
Texture2D myTexture : register(t0);                    // ✅ Texture
StructuredBuffer<Vertex> vertices : register(t1);      // ✅ Structured buffer
Buffer<float4> colors : register(t2);                  // ✅ Typed buffer
ByteAddressBuffer raw : register(t3);                  // ✅ Raw buffer
RaytracingAccelerationStructure tlas : register(t4);   // ✅ Ray tracing TLAS
```

**Key principle:** `t` = **read-only resource** (not just textures)

**C++ side - creating different SRV types:**
```cpp
// SRV for structured buffer
D3D12_SHADER_RESOURCE_VIEW_DESC srv_structured = {};
srv_structured.Format = DXGI_FORMAT_UNKNOWN;
srv_structured.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
srv_structured.Buffer.StructureByteStride = sizeof(Vertex);
srv_structured.Buffer.NumElements = vertex_count;

// SRV for typed buffer
D3D12_SHADER_RESOURCE_VIEW_DESC srv_typed = {};
srv_typed.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;  // float4
srv_typed.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
srv_typed.Buffer.NumElements = element_count;

// SRV for acceleration structure (ray tracing)
D3D12_SHADER_RESOURCE_VIEW_DESC srv_tlas = {};
srv_tlas.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
srv_tlas.RaytracingAccelerationStructure.Location = tlas_buffer->GetGPUVirtualAddress();
```

---

### Q2: Does each texture need its own sampler?

**A: No - samplers are SEPARATE from textures. One sampler can be used with many textures.**

This is a **critical** concept that many developers misunderstand.

**Key Facts:**
- **Textures** (`t` registers) = The image data
- **Samplers** (`s` registers) = How to read/filter that data
- **They are INDEPENDENT resources**

**Common Pattern - Share Samplers:**
```hlsl
// Declare multiple textures
Texture2D albedoMap : register(t0);
Texture2D normalMap : register(t1);
Texture2D roughnessMap : register(t2);
Texture2D metallicMap : register(t3);

// Declare fewer samplers (shared)
SamplerState linearSampler : register(s0);   // Linear filtering
SamplerState pointSampler : register(s1);    // Point filtering
SamplerState anisotropicSampler : register(s2);  // Anisotropic

// Use same sampler with different textures
float4 albedo = albedoMap.Sample(linearSampler, uv);
float3 normal = normalMap.Sample(linearSampler, uv);
float roughness = roughnessMap.Sample(linearSampler, uv);
float metallic = metallicMap.Sample(pointSampler, uv);  // Different sampler!
```

**What Samplers Define:**

| Property | Example Values | Purpose |
|----------|----------------|---------|
| **Filter Mode** | Point, Linear, Anisotropic | How pixels are interpolated |
| **Address Mode** | Wrap, Clamp, Mirror, Border | What happens at texture edges |
| **Mip Behavior** | Nearest, Linear between mips | How mipmaps are selected |
| **Comparison** | Less, Greater, Always | For shadow map comparisons |
| **Max Anisotropy** | 1, 4, 8, 16 | Quality of anisotropic filtering |

**C++ Side - Creating a Sampler:**
```cpp
D3D12_SAMPLER_DESC sampler_desc = {};
sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;  // Trilinear filtering
sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;  // Repeat in U
sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;  // Repeat in V
sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;  // Repeat in W
sampler_desc.MaxAnisotropy = 16;  // High-quality anisotropic filtering
sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
sampler_desc.MinLOD = 0.0f;
sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;

// Create sampler descriptor (in sampler heap, NOT CBV/SRV/UAV heap!)
device->CreateSampler(&sampler_desc, sampler_heap_handle);
```

**Typical Setup:**
- **10-100 textures** in a scene
- **3-5 samplers** total (linear, point, anisotropic, clamp variants)
- Samplers bound once, reused across many textures

**Special Case - Comparison Samplers (for shadow maps):**
```hlsl
Texture2D shadowMap : register(t0);
SamplerComparisonState shadowSampler : register(s0);  // Special sampler type

// Returns 0.0 (in shadow) or 1.0 (lit)
float shadow = shadowMap.SampleCmp(shadowSampler, uv, compareDepth);
```
```cpp
// C++ side - comparison sampler for shadows
sampler_desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;  // Shadow test
```

---

### Q3: Do acceleration structures (TLAS/BLAS) need to be bound like textures?

**A: YES - TLAS MUST be explicitly bound as an SRV (`t` register). BLAS is NOT bound (referenced indirectly).**

This is a common source of confusion in ray tracing.

#### TLAS (Top-Level Acceleration Structure)

**TLAS is an SRV** - it uses a `t` register and must be bound like any other shader resource:

**Shader Side:**
```hlsl
// TLAS is bound as SRV at t0, space0
RaytracingAccelerationStructure scene : register(t0, space0);

[shader("raygeneration")]
void RayGen()
{
    RayDesc ray;
    ray.Origin = cameraPos;
    ray.Direction = rayDir;
    ray.TMin = 0.001f;
    ray.TMax = 10000.0f;
    
    RayPayload payload;
    
    // TraceRay requires the TLAS (explicitly bound at t0)
    TraceRay(scene,              // ← TLAS from register(t0)
             RAY_FLAG_NONE,
             0xFF,               // Instance mask
             0,                  // Ray contribution to hit group index
             1,                  // Multiplier for geometry contribution
             0,                  // Miss shader index
             ray,
             payload);
}
```

**C++ Side - Building and Binding TLAS:**
```cpp
// STEP 1: Build BLAS for each mesh (bottom-level)
for (auto& mesh : meshes) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_desc = {};
    blas_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_desc.Inputs.pGeometryDescs = &geometry_desc;
    blas_desc.Inputs.NumDescs = 1;
    blas_desc.DestAccelerationStructureData = blas_buffer->GetGPUVirtualAddress();
    
    cmd_list->BuildRaytracingAccelerationStructure(&blas_desc, 0, nullptr);
    // ... resource barriers ...
}

// STEP 2: Build TLAS (top-level - references BLASes)
D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_desc = {};
tlas_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
tlas_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
tlas_desc.Inputs.InstanceDescs = instance_buffer->GetGPUVirtualAddress();
tlas_desc.Inputs.NumDescs = instance_count;
tlas_desc.DestAccelerationStructureData = tlas_buffer->GetGPUVirtualAddress();

cmd_list->BuildRaytracingAccelerationStructure(&tlas_desc, 0, nullptr);

// STEP 3: Create SRV descriptor for TLAS (at t0)
D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
srv_desc.RaytracingAccelerationStructure.Location = tlas_buffer->GetGPUVirtualAddress();

// Write descriptor to heap (at position for t0)
D3D12_CPU_DESCRIPTOR_HANDLE heap_handle = /* position for t0 in descriptor heap */;
device->CreateShaderResourceView(nullptr, &srv_desc, heap_handle);

// STEP 4: Include in descriptor heap layout
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_srv_range(1, 0, 0)   // ← t0 for TLAS (MUST include!)
    .add_srv_range(3, 1, 0)   // t1, t2 (materials, lights)
    .add_cbv_range(1, 0, 0)   // b0 (view constants)
    .add_srv_range(textures.size(), 3, 0)  // t3+
    .add_srv_range(3, 10, 0)  // t10-t12 (global buffers)
    .create(device.Get());

// STEP 5: Bind descriptor heap before ray tracing
cmd_list->SetDescriptorHeaps(1, &raygen_desc_heap.Get());
cmd_list->SetComputeRootSignature(global_root_sig.Get());
cmd_list->SetComputeRootDescriptorTable(0, heap_gpu_handle);

// STEP 6: Dispatch rays (now TLAS is accessible at t0)
cmd_list->DispatchRays(&dispatch_desc);
```

**In ChameleonRT DXR (`render_dxr.cpp`):**
```cpp
// See build_raytracing_descriptor_heap()
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_uav_range(2, 0, 0)     // u0, u1 (render target, accumulation)
    .add_srv_range(1, 0, 0)     // t0 (TLAS) ← Explicitly added!
    .add_srv_range(2, 1, 0)     // t1, t2 (materials, lights)
    .add_cbv_range(1, 0, 0)     // b0 (view constants)
    // ... more resources
    .create(device.Get());
```

#### BLAS (Bottom-Level Acceleration Structure)

**BLAS is NOT bound** to shader registers - it's referenced indirectly:

**How BLAS Works:**
1. **TLAS contains instance descriptions** (on GPU)
2. Each instance description has a `AccelerationStructure` field pointing to a BLAS GPU address
3. When `TraceRay()` hits geometry, DXR runtime uses TLAS instance data to find the BLAS
4. **Shader never directly accesses BLAS** - it's all internal to the ray tracing pipeline

**Instance Description (in TLAS):**
```cpp
D3D12_RAYTRACING_INSTANCE_DESC instance = {};
instance.Transform = transform_matrix;  // Object transform
instance.InstanceID = mesh_id;  // User-defined ID
instance.InstanceMask = 0xFF;
instance.InstanceContributionToHitGroupIndex = 0;
instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
instance.AccelerationStructure = blas_buffer->GetGPUVirtualAddress();  // ← Points to BLAS!
```

**Summary Table:**

| Structure | Bound to Shader? | Register Type | How Accessed |
|-----------|------------------|---------------|--------------|
| **TLAS** | ✅ YES | `t` (SRV) | Explicitly bound, used in `TraceRay()` |
| **BLAS** | ❌ NO | N/A | Referenced via TLAS instance data (internal) |

---

### Q4: How do UAVs differ from SRVs in terms of synchronization?

**A: UAVs require manual synchronization for concurrent writes. SRVs are read-only so no synchronization needed.**

#### SRVs (Shader Resource Views) - Read-Only

**No synchronization needed:**
- Multiple threads can read the same data simultaneously
- GPU hardware guarantees coherent reads
- No race conditions possible

```hlsl
StructuredBuffer<Material> materials : register(t0);  // Read-only

[shader("closesthit")]
void ClosestHit(...)
{
    // Safe: Multiple rays can read materials[5] simultaneously
    Material mat = materials[materialID];  // ✅ No synchronization needed
}
```

#### UAVs (Unordered Access Views) - Read-Write

**Synchronization IS required:**
- Multiple threads can write to the same location (race condition!)
- You must either:
  1. Ensure each thread writes to unique locations, OR
  2. Use atomic operations for shared locations

**Pattern 1: Unique Writes (No Synchronization Needed)**

```hlsl
RWTexture2D<float4> output : register(u0);

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;  // Unique per-thread
    
    // Safe: Each thread writes to its own pixel
    output[pixel] = ComputeColor();  // ✅ No race condition
}
```

**Each thread gets a unique pixel coordinate, so writes never overlap.**

**Pattern 2: Atomic Operations (For Shared Writes)**

```hlsl
RWStructuredBuffer<uint> histogram : register(u0);

[shader("raygeneration")]
void RayGen()
{
    float luminance = ComputeLuminance();
    uint bin = uint(luminance * 255.0f);
    
    // UNSAFE: Multiple threads might increment histogram[bin] simultaneously!
    // histogram[bin] = histogram[bin] + 1;  // ❌ RACE CONDITION!
    
    // SAFE: Atomic operation guarantees thread-safe increment
    InterlockedAdd(histogram[bin], 1);  // ✅ Thread-safe
}
```

**Available Atomic Operations:**

| Function | Operation | Example |
|----------|-----------|---------|
| `InterlockedAdd(dest, value)` | Atomic add | Counter increment |
| `InterlockedMin(dest, value)` | Atomic min | Minimum reduction |
| `InterlockedMax(dest, value)` | Atomic max | Maximum reduction |
| `InterlockedAnd(dest, value)` | Atomic AND | Bitmask operations |
| `InterlockedOr(dest, value)` | Atomic OR | Flag setting |
| `InterlockedXor(dest, value)` | Atomic XOR | Toggle operations |
| `InterlockedCompareExchange(dest, compare, value)` | Compare-and-swap | Lock-free algorithms |
| `InterlockedExchange(dest, value)` | Atomic swap | Lock-free data structures |

**Pattern 3: Per-Thread Accumulation + Final Combine**

```hlsl
// Instead of shared UAV, each thread accumulates locally
groupshared float3 sharedColor[256];  // Thread group shared memory

[shader("raygeneration")]
void RayGen()
{
    uint threadID = DispatchRaysIndex().x;
    
    // Each thread computes its own color
    float3 color = TraceRays();
    
    // Store in thread-local shared memory
    sharedColor[threadID] = color;  // ✅ No race (unique index)
    
    GroupMemoryBarrierWithGroupSync();  // Synchronize threads
    
    // Thread 0 combines results
    if (threadID == 0) {
        float3 totalColor = 0.0f;
        for (uint i = 0; i < 256; ++i) {
            totalColor += sharedColor[i];
        }
        output[groupID] = totalColor / 256.0f;  // ✅ Safe (one thread writes)
    }
}
```

**Performance Considerations:**

| Method | Performance | Use Case |
|--------|-------------|----------|
| **Unique writes** | ⚡ Fastest | Image output, per-pixel data |
| **Atomic operations** | 🐌 Slower (contention) | Histograms, counters, global stats |
| **Thread group shared memory** | ⚡ Fast | Local reductions, tile-based algorithms |

**Common UAV Use Cases:**

```hlsl
// 1. Render target (unique writes per pixel)
RWTexture2D<float4> renderTarget : register(u0);

// 2. Accumulation buffer (read-modify-write with caution)
RWTexture2D<float4> accumBuffer : register(u1);

// 3. Global counter (atomic operations required)
RWStructuredBuffer<uint> rayCounter : register(u2);

// 4. Debug buffer (unique writes per thread)
RWStructuredBuffer<DebugData> debugBuffer : register(u3);
```

---

### Q5: What's the difference between CBVs and inline root constants?

**A: CBVs are buffers in GPU memory (larger, slower access). Root constants are inline 32-bit values (smaller, faster access).**

#### Comparison Table

| Feature | CBV (Constant Buffer View) | Root Constants |
|---------|---------------------------|----------------|
| **Location** | GPU memory (buffer) | Root signature itself (inline) |
| **Size Limit** | 64 KB per buffer | 64 DWORDs (256 bytes) total |
| **Access Speed** | Slower (memory fetch) | Faster (direct in root signature) |
| **Binding Cost** | Cheaper (pointer update) | More expensive (data copy) |
| **Typical Use** | Large constant data | Small, frequently changing values |
| **Alignment** | 256-byte aligned | 4-byte aligned |

#### Root Constants - Fast and Small

**Best for:** Small data that changes frequently (per-draw/dispatch)

**Shader Side:**
```hlsl
// Root constants - NO buffer, just inline data
cbuffer InlineConstants : register(b0)
{
    uint frameIndex;        // 4 bytes
    float time;             // 4 bytes
    uint2 screenSize;       // 8 bytes
    uint instanceID;        // 4 bytes
    // Total: 20 bytes (5 DWORDs) ✅ Fits in root constants
};

[shader("raygeneration")]
void RayGen()
{
    // Fast access - data is in root signature (no memory fetch)
    uint frame = frameIndex;  // ⚡ Very fast
}
```

**C++ Side - Root Constants Setup:**
```cpp
// Define root signature with inline constants
CD3DX12_ROOT_PARAMETER1 rootParams[2];

// Parameter 0: Inline root constants (5 DWORDs at b0)
rootParams[0].InitAsConstants(5, 0, 0);  // 5 DWORDs, b0, space0

// Parameter 1: Descriptor table for other resources
CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 13, 0, 0);  // t0-t12, space0
rootParams[1].InitAsDescriptorTable(1, ranges);

// Create root signature from this definition
// ...
```

**When to Use Root Constants:**
- ✅ Material ID (per-object)
- ✅ Instance ID (per-draw)
- ✅ Frame index (per-frame)
- ✅ Small transforms (2x3 matrix = 6 floats)
- ❌ Large transforms (4x4 matrix = 16 floats - use CBV instead)
- ❌ Complex material data (use CBV)

#### Constant Buffer Views - Larger Data

**Best for:** Larger constant data that doesn't change every frame

**Shader Side:**
```hlsl
// CBV - data comes from a buffer in GPU memory
cbuffer ViewParams : register(b0)
{
    float4x4 view;           // 64 bytes
    float4x4 proj;           // 64 bytes
    float4x4 viewProj;       // 64 bytes
    float4x4 invView;        // 64 bytes
    float3 cameraPos;        // 12 bytes
    float fov;               // 4 bytes
    // Total: 268 bytes ✅ Fits in CBV (max 64KB)
    // ❌ TOO LARGE for root constants (max 256 bytes)
};

[shader("raygeneration")]
void RayGen()
{
    // Access requires memory fetch (slower than root constants)
    float3 camPos = cameraPos;  // 🐌 Memory fetch
}
```

**C++ Side - CBV Setup:**
```cpp
// STEP 1: Create constant buffer (GPU memory)
constant_buffer = dxr::Buffer::device(
    device.Get(),
    256,  // Size (must be multiple of 256 bytes!)
    D3D12_RESOURCE_STATE_GENERIC_READ
);

// STEP 2: Upload constant data
ViewParams params;
params.view = view_matrix;
params.proj = proj_matrix;
// ... fill params
constant_buffer->upload(&params, cmd_list);

// STEP 3: Create CBV descriptor
D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
cbv_desc.BufferLocation = constant_buffer->GetGPUVirtualAddress();
cbv_desc.SizeInBytes = 256;  // Must be multiple of 256!

device->CreateConstantBufferView(&cbv_desc, heap_handle);

// STEP 4: Define root signature with CBV in descriptor table
CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0);  // 1 CBV at b0

CD3DX12_ROOT_PARAMETER1 rootParams[1];
rootParams[0].InitAsDescriptorTable(1, ranges);
// ... (create root signature)

// STEP 5: Bind descriptor heap (CBV is in the heap)
cmd_list->SetDescriptorHeaps(1, &desc_heap.Get());
cmd_list->SetComputeRootDescriptorTable(0, heap_gpu_handle);
```

**When to Use CBVs:**
- ✅ Camera parameters (view/projection matrices)
- ✅ Light data (position, color, direction)
- ✅ Large material parameters
- ✅ Global scene constants
- ❌ Single integers/floats (use root constants)
- ❌ Per-draw material ID (use root constants)

#### Hybrid Approach (Best Practice)

**Combine both for optimal performance:**

```hlsl
// Root constants (b0) - small, frequently changing
cbuffer PerDrawConstants : register(b0)
{
    uint materialID;     // Changes per draw
    uint instanceID;     // Changes per draw
};

// CBV (b1) - larger, less frequent changes
cbuffer ViewConstants : register(b1)
{
    float4x4 viewProj;   // Changes per camera
    float3 cameraPos;
    // ... more camera data
};

// CBV (b2) - rarely changes
cbuffer SceneConstants : register(b2)
{
    Light lights[8];     // Changes per scene load
    float ambientFactor;
    // ... more scene data
};
```

**C++ Root Signature - Hybrid:**
```cpp
CD3DX12_ROOT_PARAMETER1 rootParams[2];

// Parameter 0: Root constants (inline, fast)
rootParams[0].InitAsConstants(2, 0, 0);  // 2 DWORDs at b0 (materialID, instanceID)

// Parameter 1: Descriptor table with CBVs (slower, larger)
CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0);  // b1 (ViewConstants)
ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2, 0);  // b2 (SceneConstants)
rootParams[1].InitAsDescriptorTable(2, ranges);

// Create root signature with both
// ...
```

#### Root Descriptors (Alternative to Descriptor Tables)

**There's also a third option** - root descriptors (less common):

```cpp
// Root descriptor - inline pointer to CBV (no descriptor heap needed)
rootParams[0].InitAsConstantBufferView(0, 0);  // Direct pointer to b0

// Binding:
cmd_list->SetComputeRootConstantBufferView(0, cbv_gpu_address);
```

**Comparison:**

| Method | Speed | Root Signature Cost | Flexibility |
|--------|-------|---------------------|-------------|
| **Root Constants** | ⚡⚡⚡ Fastest | 1 DWORD per constant | Limited (256 bytes max) |
| **Root Descriptor** | ⚡⚡ Fast | 2 DWORDs | Medium (1 resource) |
| **Descriptor Table** | ⚡ Slower | 1 DWORD | High (unlimited resources) |

**ChameleonRT Pattern:**
```cpp
// DXR typically uses descriptor tables for flexibility
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_cbv_range(1, 0, 0)  // CBV at b0 (view constants)
    // Could add root constants in root signature for per-frame data
    .create(device.Get());
```

---

### Q6: Why are samplers in a separate descriptor heap from CBV/SRV/UAV?

**A: Hardware limitation - samplers have different descriptor format and size constraints.**

#### Two Types of Descriptor Heaps

D3D12 has **two separate descriptor heap types** for shader-visible resources:

| Heap Type | Stores | Descriptor Size | Max Descriptors | Heap Flags |
|-----------|--------|----------------|-----------------|------------|
| **CBV_SRV_UAV** | Constants, textures, buffers | ~32 bytes | 1,000,000 | `D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE` |
| **SAMPLER** | Sampler states | ~16 bytes | 2,048 | `D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE` |

**C++ - Creating Separate Heaps:**
```cpp
// Heap 1: CBV/SRV/UAV descriptor heap
D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_uav_heap_desc = {};
cbv_srv_uav_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
cbv_srv_uav_heap_desc.NumDescriptors = 100;
cbv_srv_uav_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

ComPtr<ID3D12DescriptorHeap> cbv_srv_uav_heap;
device->CreateDescriptorHeap(&cbv_srv_uav_heap_desc, IID_PPV_ARGS(&cbv_srv_uav_heap));

// Heap 2: Sampler descriptor heap (SEPARATE!)
D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
sampler_heap_desc.NumDescriptors = 16;
sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

ComPtr<ID3D12DescriptorHeap> sampler_heap;
device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap));

// CANNOT mix: This would fail
// CreateSampler(..., cbv_srv_uav_heap_handle);  // ❌ Wrong heap type!
```

#### Why Separate?

**Historical reasons:**
1. **Hardware design** - GPUs have separate texture and sampler units
2. **Descriptor format** - Samplers have different hardware layout than buffers/textures
3. **Size constraints** - Limited sampler slots in hardware (2048 vs 1,000,000)
4. **Performance** - Separate heaps enable hardware optimizations

#### Binding Both Heaps

**You must bind BOTH heaps before rendering:**
```cpp
// Bind both heaps (order doesn't matter)
ID3D12DescriptorHeap* heaps[] = {
    cbv_srv_uav_heap.Get(),  // For t, u, b registers
    sampler_heap.Get()       // For s registers
};

cmd_list->SetDescriptorHeaps(2, heaps);  // Bind both

// Then set root signature and descriptor tables
cmd_list->SetComputeRootSignature(root_sig.Get());
cmd_list->SetComputeRootDescriptorTable(0, cbv_srv_uav_heap_gpu_handle);
cmd_list->SetComputeRootDescriptorTable(1, sampler_heap_gpu_handle);
```

#### Root Signature - Separate Descriptor Tables

**Root signature must have separate descriptor tables for samplers:**

```cpp
CD3DX12_DESCRIPTOR_RANGE1 ranges[2];

// Range 0: SRVs (textures) in CBV_SRV_UAV heap
ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 0, 0);  // t0-t9

// Range 1: Samplers in SAMPLER heap (SEPARATE!)
ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 3, 0, 0);  // s0-s2

CD3DX12_ROOT_PARAMETER1 rootParams[2];

// Parameter 0: Textures (points to CBV_SRV_UAV heap)
rootParams[0].InitAsDescriptorTable(1, &ranges[0]);

// Parameter 1: Samplers (points to SAMPLER heap)
rootParams[1].InitAsDescriptorTable(1, &ranges[1]);

// Create root signature
// ...
```

#### Typical Setup Pattern

**ChameleonRT-style:**
```cpp
// Build CBV/SRV/UAV heap
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_uav_range(2, 0, 0)      // u0, u1
    .add_srv_range(3, 0, 0)      // t0, t1, t2
    .add_cbv_range(1, 0, 0)      // b0
    .add_srv_range(textures.size(), 3, 0)  // t3+
    .add_srv_range(3, 10, 0)  // t10-t12 (global buffers)
    .create(device.Get());

// Build SAMPLER heap (separate builder or manual)
D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc = {};
sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
sampler_heap_desc.NumDescriptors = 3;
sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
device->CreateDescriptorHeap(&sampler_heap_desc, IID_PPV_ARGS(&sampler_heap));

// Create samplers in sampler heap
D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle = sampler_heap->GetCPUDescriptorHandleForHeapStart();

// Sampler 0: Linear wrap
D3D12_SAMPLER_DESC linear_wrap = {};
linear_wrap.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
linear_wrap.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
linear_wrap.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
linear_wrap.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
device->CreateSampler(&linear_wrap, sampler_handle);

sampler_handle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

// Sampler 1: Point clamp
D3D12_SAMPLER_DESC point_clamp = {};
point_clamp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
point_clamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
// ... (create sampler)

// ... more samplers

// Bind both heaps before rendering
ID3D12DescriptorHeap* heaps[] = {
    raygen_desc_heap.get(),
    sampler_heap.Get()
};
cmd_list->SetDescriptorHeaps(2, heaps);
```

**Shader uses both:**
```hlsl
// From CBV_SRV_UAV heap
Texture2D albedoMap : register(t3);
Texture2D normalMap : register(t4);

// From SAMPLER heap (separate!)
SamplerState linearSampler : register(s0);
SamplerState pointSampler : register(s1);

// Use together
float4 albedo = albedoMap.Sample(linearSampler, uv);
float3 normal = normalMap.Sample(linearSampler, uv);
```

#### Key Takeaways

1. **Two heap types** - CBV/SRV/UAV and SAMPLER (cannot mix)
2. **Both must be bound** - Call `SetDescriptorHeaps()` with array of both
3. **Root signature has separate tables** - One for textures, one for samplers
4. **Hardware limitation** - This is not a design choice, it's required by GPU architecture

---

### Q7: What's the relationship between SM (Streaming Multiprocessor) and shader execution?

**A: "SM" is GPU hardware. All shader threads on an SM can access all bound resources (t, u, b, s registers).**

#### What is an SM?

**SM (Streaming Multiprocessor)** is a **hardware unit** in NVIDIA GPUs:
- Contains execution units (CUDA cores, Tensor cores, etc.)
- Has local shared memory (L1 cache, register file)
- Executes groups of threads (warps = 32 threads)

**Other GPU vendors use different names:**
- **NVIDIA:** SM (Streaming Multiprocessor)
- **AMD:** CU (Compute Unit)
- **Intel:** EU (Execution Unit) or Xe-core

**Key concept:** SM is **hardware**, not something you control directly in shaders.

#### How Shader Threads Map to SMs

When you dispatch a shader (ray tracing, compute, pixel shader):

```cpp
// Dispatch ray tracing
D3D12_DISPATCH_RAYS_DESC dispatch_desc = {};
dispatch_desc.Width = 1920;   // Screen width
dispatch_desc.Height = 1080;  // Screen height
dispatch_desc.Depth = 1;

cmd_list->DispatchRays(&dispatch_desc);
// Launches 1920×1080 = 2,073,600 shader threads!
```

**What happens:**
1. **GPU scheduler** divides threads into warps (groups of 32)
2. **Warps are assigned to SMs** based on availability
3. **Each SM executes many warps** (hundreds of threads per SM)
4. **ALL threads on an SM can access ALL bound resources** (`t`, `u`, `b`, `s` registers)

#### Resource Visibility

**Important:** Resources are **globally visible** to all shader threads:

```hlsl
// These resources are visible to ALL shader threads on ALL SMs
Texture2D globalTexture : register(t0);              // ✅ All threads can read
StructuredBuffer<Vertex> globalVertices : register(t10);  // ✅ All threads can read
RWTexture2D<float4> output : register(u0);           // ✅ All threads can write
cbuffer ViewParams : register(b0) { ... };           // ✅ All threads can read

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;  // Unique per-thread
    
    // This thread (on some SM) accesses global resources
    float4 texColor = globalTexture.SampleLevel(sampler, uv, 0);  // ✅ OK
    Vertex v = globalVertices[index];  // ✅ OK
    output[pixel] = ComputeColor();    // ✅ OK
    
    // All threads on all SMs can access these resources
}
```

**There is NO concept of "per-SM resources" in D3D12/DXR.**

#### Thread Groups and Shared Memory

**What you CAN control:** Thread groups and shared memory

```hlsl
// Thread group: 16×16 threads (256 threads per group)
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeMain(
    uint3 threadID : SV_DispatchThreadID,       // Global thread ID
    uint3 groupThreadID : SV_GroupThreadID,     // Thread ID within group
    uint3 groupID : SV_GroupID                  // Group ID
)
{
    // Thread group shared memory (visible only to threads in THIS group)
    groupshared float sharedData[16][16];  // 256 floats (likely on same SM)
    
    // All threads in group can see sharedData
    sharedData[groupThreadID.xy] = ComputeValue();
    
    GroupMemoryBarrierWithGroupSync();  // Synchronize threads in group
    
    // Now all threads in group can read each other's results
    float sum = 0.0f;
    for (uint i = 0; i < 16; ++i) {
        sum += sharedData[groupThreadID.x][i];
    }
    
    // Write result (global resource - visible to all threads)
    output[threadID.xy] = sum;
}
```

**Thread group shared memory:**
- **Shared within group** (typically 256-1024 threads)
- **Likely on same SM** (GPU scheduler tries to co-locate)
- **Fast access** (on-chip memory)
- **Requires synchronization** (`GroupMemoryBarrierWithGroupSync()`)

#### SM Hardware Details (NVIDIA Example)

**NVIDIA RTX 4090 (Ada Lovelace architecture):**
- **128 SMs** total
- Each SM has:
  - **128 CUDA cores** (FP32 units)
  - **64 KB L1 cache / shared memory**
  - **65,536 registers** (distributed across threads)
  - **4 warp schedulers** (can execute 4 warps simultaneously)

**Execution:**
```
DispatchRays(1920, 1080) launches 2,073,600 threads

GPU divides into warps: 2,073,600 / 32 = 64,800 warps

Each SM can hold ~64 active warps (depending on register usage)

128 SMs × 64 warps = 8,192 active warps at once

Multiple "waves" needed: 64,800 / 8,192 ≈ 8 waves

All threads access same bound resources (t, u, b, s)
```

#### What You Control vs. What GPU Controls

| Aspect | You Control | GPU Controls |
|--------|-------------|--------------|
| **Resource binding** | ✅ Which buffers/textures to bind | ❌ |
| **Thread dispatch** | ✅ How many threads to launch | ❌ |
| **Thread group size** | ✅ `[numthreads(X,Y,Z)]` | ❌ |
| **Shared memory** | ✅ `groupshared` variables | ❌ |
| **SM assignment** | ❌ | ✅ GPU scheduler |
| **Warp execution order** | ❌ | ✅ Warp scheduler |
| **Memory access pattern** | ⚠️ Indirectly via code | ✅ Memory controller |

**Bottom line:** You don't manage SMs directly. You:
1. Bind resources (visible to all threads)
2. Dispatch threads (GPU assigns to SMs)
3. Optionally use thread group shared memory (fast, local)

