# DXR Global Buffer Refactor - Technical Deep Dive

**Companion Document to:** `COMPLETION_REPORT.md`  
**Audience:** Developers implementing similar patterns or debugging issues

---

## Index Offset Mathematics

### The Critical Discovery: Pre-Adjusted Indices

**Scene Building (CPU-side):**
```cpp
// scene.cpp: build_global_buffers()
uint32_t vertexOffset = global_vertices.size();  // Current global count

for (auto &tri : geom.indices) {
    // CRITICAL: Indices stored ALREADY adjusted to global space
    global_indices.push_back(glm::uvec3(
        tri.x + vertexOffset,  // <--- PRE-ADJUSTED
        tri.y + vertexOffset,
        tri.z + vertexOffset
    ));
}

// Store offset for this geometry's descriptor
mesh_desc.vbOffset = vertexOffset;
mesh_desc.ibOffset = global_indices.size() - geom.indices.size();
```

**Shader Access (GPU-side):**
```hlsl
// Load triangle indices (ALREADY GLOBAL)
uint3 idx = globalIndices[mesh.ibOffset + PrimitiveIndex()];

// Example with concrete values (Sponza, geometry 42):
// mesh.ibOffset = 15000 (offset to this geometry's first triangle)
// PrimitiveIndex() = 3 (4th triangle in geometry)
// idx = globalIndices[15003] = uint3(48321, 48322, 48323)  // GLOBAL INDICES

// CORRECT: Direct lookup for vertices (idx is global)
float3 va = globalVertices[idx.x];  // globalVertices[48321] ✅

// WRONG: Adding vbOffset would double-offset
float3 va = globalVertices[mesh.vbOffset + idx.x];  // globalVertices[48000 + 48321] ❌
```

### Per-Vertex Attribute Indexing (UVs, Normals)

**Why Different from Vertices?**

Vertices are **indexed globally**, but UVs/normals are **stored sequentially** per-geometry:

```cpp
// CPU: UV storage (scene.cpp)
// Geometry 0: UVs [0..99]    → uvOffset=0,   num_uvs=100
// Geometry 1: UVs [100..249] → uvOffset=100, num_uvs=150
// Geometry 2: UVs [250..399] → uvOffset=250, num_uvs=150

global_uvs.insert(global_uvs.end(), geom.uvs.begin(), geom.uvs.end());
mesh_desc.uvOffset = global_uvs.size() - geom.uvs.size();
```

**Shader Conversion:**
```hlsl
// idx.x = 48321 (GLOBAL vertex index)
// mesh.vbOffset = 48000 (start of geometry 42's vertices)

// Step 1: Convert global index to LOCAL vertex index
uint local_vertex_idx = idx.x - mesh.vbOffset;  // 48321 - 48000 = 321

// Step 2: Offset into global UV array
float2 uva = globalUVs[mesh.uvOffset + local_vertex_idx];  // ✅

// WRONG patterns:
float2 uva = globalUVs[idx.x];                    // ❌ Out of bounds
float2 uva = globalUVs[mesh.uvOffset + idx.x];    // ❌ Double offset
```

**Visual Example:**
```
Global Vertices: [v0, v1, ..., v48000, ..., v48321, ..., v184405]
                                 ↑              ↑
                           Geom 42 start    idx.x (global)
                           
Global UVs:      [uv0, uv1, ..., uv5000, ..., uv5321, ..., uv184405]
                                  ↑              ↑
                            uvOffset=5000   uvOffset + local_idx
                            
Local vertex 321 in geometry 42 → UV at global index 5321
```

---

## SBT (Shader Binding Table) Architecture

### Old vs. New Binding Model

**Old (Space1 - Per-Geometry SRVs):**
```
SBT Record for HitGroup[42]:
┌────────────────────────────────┐
│ Shader Identifier (32 bytes)  │  ← D3D12 RT required
├────────────────────────────────┤
│ vertex_buf SRV (8 bytes)       │  ← GPU descriptor
│ index_buf SRV (8 bytes)        │
│ normal_buf SRV (8 bytes)       │
│ uv_buf SRV (8 bytes)           │
│ MeshData CBV (8 bytes)         │  ← material_id, counts
└────────────────────────────────┘
Total: 32 + 40 = 72 bytes/geometry

For 381 geometries: 72 × 381 = 27,432 bytes
```

**New (Space0 - Global Buffers + Index):**
```
SBT Record for HitGroup[42]:
┌────────────────────────────────┐
│ Shader Identifier (32 bytes)  │
├────────────────────────────────┤
│ meshDescIndex (4 bytes)        │  ← Index into meshDescs[]
│ (padding to alignment)         │
└────────────────────────────────┘
Total: 32 + 8 = 40 bytes/geometry (aligned)

For 381 geometries: 40 × 381 = 15,240 bytes

Reduction: (27432 - 15240) / 27432 = 44% size reduction
```

### SBT Building Code Flow

```cpp
// build_shader_binding_table() in render_dxr.cpp

rt_pipeline.map_shader_table();  // Map GPU memory for writing

size_t mesh_desc_index = 0;
for (parameterized_mesh : parameterized_meshes) {
    for (geometry : mesh.geometries) {
        std::wstring hg_name = L"HitGroup_param_mesh" + i + L"_geom" + j;
        
        // Get pointer to this hit group's SBT record
        uint8_t *map = rt_pipeline.shader_record(hg_name);
        
        // Get offset of "HitGroupData" parameter in local root sig
        const dxr::RootSignature *sig = rt_pipeline.shader_signature(hg_name);
        size_t offset = sig->offset("HitGroupData");  // Usually 32 (after identifier)
        
        // Write meshDescIndex (single uint32)
        uint32_t mesh_desc_idx = static_cast<uint32_t>(mesh_desc_index);
        std::memcpy(map + offset, &mesh_desc_idx, sizeof(uint32_t));
        
        mesh_desc_index++;
    }
}

rt_pipeline.unmap_shader_table();
rt_pipeline.upload_shader_table(cmd_list);  // GPU copy
```

**Mapping Validation:**
```
Instance[0] → param_mesh0 → HitGroupBase=0
  HitGroup[0] (param_mesh0_geom0) → meshDescIndex=0 → meshDescs[0]
  HitGroup[1] (param_mesh0_geom1) → meshDescIndex=1 → meshDescs[1]
  ...
  HitGroup[380] (param_mesh0_geom380) → meshDescIndex=380 → meshDescs[380]

At runtime:
  DispatchRays() → TraceRay() → Hit on triangle
  → D3D12 loads SBT[HitGroupIndex]
  → Reads meshDescIndex from local root signature
  → Shader: MeshDesc mesh = meshDescs[meshDescIndex]
```

---

## Descriptor Heap Layout

### Space0 Organization

**Heap Structure:**
```
raygen_desc_heap (CBV_SRV_UAV):
┌─────────────────────────────────────────────┐
│ Slot 0: render_target (UAV, u0)            │ ← Output image
│ Slot 1: accum_buffer (UAV, u1)             │ ← Accumulation
│ Slot 2: scene_bvh (SRV, t0)                │ ← TLAS (acceleration structure)
│ Slot 3: material_param_buf (SRV, t1)       │ ← Materials
│ Slot 4: light_buf (SRV, t2)                │ ← Lights
│ Slot 5: view_param_buf (CBV, b0)           │ ← Camera constants
│ Slots 6..N: textures (SRV, t30+)           │ ← Texture array
│──────────────────────────────────────────────│
│ Slot N+1: globalVertices (SRV, t10)        │ ← Global buffers
│ Slot N+2: globalIndices (SRV, t11)         │
│ Slot N+3: globalNormals (SRV, t12)         │
│ Slot N+4: globalUVs (SRV, t13)             │
│ Slot N+5: meshDescs (SRV, t14)             │
└─────────────────────────────────────────────┘

raygen_sampler_heap (SAMPLER):
┌─────────────────────────────────────────────┐
│ Slot 0: linear_wrap_sampler (s0)           │
└─────────────────────────────────────────────┘
```

**Creation Code:**
```cpp
// build_shader_resource_heap()
raygen_desc_heap = dxr::DescriptorHeapBuilder()
    .add_uav_range(2, 0, 0)         // u0-u1, space0
    .add_srv_range(3, 0, 0)         // t0-t2, space0 (BVH, materials, lights)
    .add_cbv_range(1, 0, 0)         // b0, space0 (view params)
    .add_srv_range(textures.size(), 30, 0)  // t30+, space0 (textures)
    .add_srv_range(5, 10, 0)        // t10-t14, space0 (GLOBAL BUFFERS)
    .create(device.Get());

// build_descriptor_heap()
D3D12_CPU_DESCRIPTOR_HANDLE heap_handle = raygen_desc_heap.cpu_desc_handle();

// ... (create UAVs, scene SRVs, CBV, texture SRVs)

// Create global buffer SRVs at t10-t14
D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
srv_desc.Format = DXGI_FORMAT_UNKNOWN;
srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
srv_desc.Buffer.StructureByteStride = sizeof(glm::vec3);  // or vec2 for UVs
srv_desc.Buffer.NumElements = global_vertex_count;
srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

device->CreateShaderResourceView(global_vertex_buffer.get(), &srv_desc, heap_handle);
// ... (repeat for indices, normals, UVs, meshDescs)
```

---

## Root Signature Architecture

### Global Root Signature
```cpp
dxr::RootSignature global_root_sig =
    dxr::RootSignatureBuilder::global()
        .add_desc_heap("cbv_srv_uav_heap", raygen_desc_heap)  // Parameter 0
        .add_desc_heap("sampler_heap", raygen_sampler_heap)   // Parameter 1
        .create(device.Get());

// Binding in command list:
render_cmd_list->SetComputeRootDescriptorTable(0, cbv_srv_uav_heap_gpu_start);
render_cmd_list->SetComputeRootDescriptorTable(1, sampler_heap_gpu_start);
```

**HLSL Access:**
```hlsl
// Automatically visible to all shaders via global root sig
RaytracingAccelerationStructure scene : register(t0, space0);
StructuredBuffer<DisneyMaterial> materials : register(t1, space0);
StructuredBuffer<float3> globalVertices : register(t10, space0);
// ... etc
```

### Local Root Signatures

**RayGen (b1, space0):**
```cpp
dxr::RootSignature raygen_root_sig =
    dxr::RootSignatureBuilder::local()
        .add_constants("SceneParams", 1, 1, 0)  // b1, space0
        .create(device.Get());
```

```hlsl
cbuffer SceneParams : register(b1, space0) {
    uint32_t num_lights;
}
```

**HitGroup (b2, space0):**
```cpp
dxr::RootSignature hitgroup_root_sig =
    dxr::RootSignatureBuilder::local()
        .add_constants("HitGroupData", 2, 1, 0)  // b2, space0
        .create(device.Get());
```

```hlsl
cbuffer HitGroupData : register(b2, space0) {
    uint32_t meshDescIndex;
}
```

**Register Allocation Summary:**
```
space0:
├── b0: ViewParams (camera) - global
├── b1: SceneParams (num_lights) - RayGen local
├── b2: HitGroupData (meshDescIndex) - HitGroup local
├── t0-t2: BVH, materials, lights - global
├── t10-t14: Global geometry buffers - global
├── t30+: Textures - global
├── u0-u1: Output images - global
└── s0: Sampler - global

NO space1 usage ✅ Slang-compatible
```

---

## Slang Compilation Pipeline

### Dual Compilation Paths

**1. Embedded DXIL (Production/Fast Iteration):**
```cpp
#ifndef USE_SLANG_COMPILER
    // Pre-compiled DXIL blob (generated offline by DXC)
    std::vector<dxr::ShaderLibrary> shader_libraries;
    shader_libraries.emplace_back(
        render_dxr_dxil,              // Embedded DXIL binary
        sizeof(render_dxr_dxil),
        {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}
    );
#endif
```

**2. Runtime Slang Compilation (Development/Testing):**
```cpp
#ifdef USE_SLANG_COMPILER
    // Load shader source from file
    std::filesystem::path shader_path = dll_dir / "render_dxr.hlsl";
    std::string hlsl_source = load_shader_source(shader_path);
    
    // Setup include search paths
    std::vector<std::string> searchPaths = {
        dll_dir.string(),                          // Shader directory
        (dll_dir.parent_path() / "util").string()  // Utility headers
    };
    
    // Compile HLSL → DXIL via Slang
    auto result = slangCompiler.compileHLSLToDXILLibrary(hlsl_source, searchPaths);
    
    // Create D3D12 shader libraries from compiled bytecode
    std::vector<dxr::ShaderLibrary> shader_libraries;
    for (const auto& blob : *result) {
        std::wstring export_name(blob.entryPoint.begin(), blob.entryPoint.end());
        shader_libraries.emplace_back(
            blob.bytecode.data(),
            blob.bytecode.size(),
            {export_name}
        );
    }
#endif
```

**Slang Compiler Invocation:**
```cpp
// SlangShaderCompiler::compileHLSLToDXILLibrary()
slang::SessionDesc sessionDesc = {};
sessionDesc.targetCount = 1;
sessionDesc.targets = &targetDesc;  // DXIL target (SM 6.3)
sessionDesc.searchPathCount = searchPaths.size();

ComPtr<slang::ISession> session;
globalSession->createSession(sessionDesc, session.writeRef());

ComPtr<slang::IBlob> diagnostics;
ComPtr<slang::IModule> module = session->loadModuleFromSourceString(
    "render_dxr",
    "render_dxr.hlsl",
    shaderSource.c_str(),
    diagnostics.writeRef()
);

// Compile each entry point
for (auto& entryPoint : {"RayGen", "Miss", "ClosestHit", "ShadowMiss"}) {
    ComPtr<slang::IEntryPoint> ep;
    module->findEntryPointByName(entryPoint, ep.writeRef());
    
    std::vector<slang::IComponentType*> components = {module, ep};
    ComPtr<slang::IComponentType> program;
    session->createCompositeComponentType(components.data(), components.size(), 
                                         program.writeRef());
    
    ComPtr<slang::IBlob> dxilCode;
    program->getEntryPointCode(0, 0, dxilCode.writeRef());
    
    bytecode_blobs.push_back({entryPoint, dxilCode});
}
```

---

## Common Pitfalls & Solutions

### 1. "Black Screen" Bug
**Symptom:** Scene renders completely black  
**Cause:** Indexing out of bounds (double-offset)  
**Solution:**
```hlsl
// ❌ WRONG
float3 va = globalVertices[mesh.vbOffset + idx.x];

// ✅ CORRECT
float3 va = globalVertices[idx.x];  // idx already global
```

### 2. "Missing Textures" Bug
**Symptom:** Materials render as untextured (diffuse color only)  
**Cause:** UV indexing out of bounds  
**Solution:**
```hlsl
// ❌ WRONG
float2 uva = globalUVs[mesh.uvOffset + idx.x];  // idx.x is global!

// ✅ CORRECT
uint local_idx = idx.x - mesh.vbOffset;
float2 uva = globalUVs[mesh.uvOffset + local_idx];
```

### 3. "Crash on Launch" Bug
**Symptom:** Access violation in DispatchRays()  
**Cause:** Descriptor heap not bound, or SRVs not created  
**Debug:**
```cpp
// Check GPU addresses after upload
D3D12_GPU_VIRTUAL_ADDRESS gpu_va = global_vertex_buffer->GetGPUVirtualAddress();
assert(gpu_va != 0);  // Non-zero means valid

// Verify descriptor creation
std::cout << "Creating SRV at t10 with NumElements=" << global_vertex_count << "\n";
```

### 4. "Slang Compilation Fails" Bug
**Symptom:** `compileHLSLToDXILLibrary()` returns nullptr  
**Cause:** Missing include files, wrong search paths  
**Solution:**
```cpp
// Ensure search paths include both shader dir and util dir
std::vector<std::string> searchPaths = {
    dll_dir.string(),                          // For #include "local.hlsli"
    (dll_dir.parent_path() / "util").string()  // For #include "texture_channel_mask.h"
};

// Check diagnostics on failure
if (!result) {
    std::cerr << slangCompiler.getLastError() << std::endl;
}
```

---

## Memory Layout & Alignment

### GPU Buffer Alignment
```cpp
// D3D12 alignment requirements
constexpr size_t CONSTANT_BUFFER_ALIGNMENT = 256;  // D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
constexpr size_t STRUCTURED_BUFFER_ALIGNMENT = 4;  // Element alignment
constexpr size_t INSTANCE_DESC_ALIGNMENT = 256;    // D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT

// Example: View params buffer
view_param_buf = dxr::Buffer::upload(
    device.Get(),
    align_to(5 * sizeof(glm::vec4), CONSTANT_BUFFER_ALIGNMENT),  // Align to 256 bytes
    D3D12_RESOURCE_STATE_GENERIC_READ
);
```

### MeshDesc Packing
```cpp
struct MeshDesc {
    uint32_t vbOffset;      // 4 bytes
    uint32_t ibOffset;      // 4 bytes
    uint32_t normalOffset;  // 4 bytes
    uint32_t uvOffset;      // 4 bytes
    uint32_t num_normals;   // 4 bytes
    uint32_t num_uvs;       // 4 bytes
    uint32_t material_id;   // 4 bytes
    uint32_t pad;           // 4 bytes (alignment to 32)
};  // Total: 32 bytes (naturally aligned for D3D12 StructuredBuffer)
```

---

## Performance Considerations

### Expected Benefits
1. **Reduced SBT Size:** 44% smaller → less memory bandwidth
2. **Cache Coherency:** Sequential global arrays vs. scattered per-geometry buffers
3. **Fewer Indirections:** Single meshDescIndex lookup vs. 5 SRV fetches

### Potential Costs
1. **Larger Buffers:** All geometry data in single buffers (worse for partial updates)
2. **Index Arithmetic:** Local index conversion (minimal - single subtraction)

### Not Yet Measured
- Actual ray tracing performance (FPS, rays/sec)
- GPU memory usage (before/after)
- Build time for large scenes

**Recommendation:** Profile with PIX/NSight after Vulkan port completed.

---

## Testing Checklist

### Pre-Commit Validation
- [x] Builds successfully (Debug + Release)
- [x] Embedded DXIL path works
- [x] Slang runtime compilation works
- [x] Simple scene (cube) renders correctly
- [x] Complex scene (Sponza) renders correctly
- [x] No console errors/warnings
- [x] GPU address validation passes
- [x] SBT mapping verified (console output)
- [x] Old code removed (no phase markers)

### Regression Testing
```powershell
# Test suite
.\build\Debug\chameleonrt.exe dxr "C:\Demo\Assets\cube\cube.obj"
.\build\Debug\chameleonrt.exe dxr "C:\Demo\Assets\sponza\sponza.obj"
.\build\Debug\chameleonrt.exe dxr "C:\Demo\Assets\cornell\cornell.obj"

# Expected: All render correctly, no errors
```

---

## Future Enhancements

### Short-Term
- Enable per-vertex normals (currently `#if 0`)
- Add support for multiple UV sets
- Implement mesh instance culling (large scenes)

### Medium-Term
- Add GPU-side validation (bounds checking in debug builds)
- Implement mesh compression (quantization, delta encoding)
- Add statistics reporting (buffer sizes, SBT size, descriptor counts)

### Long-Term
- Multi-GPU support (distribute global buffers)
- Streaming geometry (partial buffer updates for large scenes)
- Mesh shaders for BVH building acceleration

---

## Appendix: Complete Code Snippets

### A. ClosestHit Shader (Full Implementation)
```hlsl
struct HitData {
    glm::vec3 position;
    glm::vec3 geometry_normal;
    glm::vec3 normal;
    glm::vec2 uv;
    uint32_t material_id;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attrib)
{
    // Get geometry descriptor
    const uint32_t meshID = meshDescIndex;
    MeshDesc mesh = meshDescs[meshID];
    
    // Load triangle indices (already global)
    uint3 idx = globalIndices[mesh.ibOffset + PrimitiveIndex()];
    
    // Load vertex positions (idx is global)
    float3 va = globalVertices[idx.x];
    float3 vb = globalVertices[idx.y];
    float3 vc = globalVertices[idx.z];
    
    // Interpolate hit position
    const float3 bary = float3(1.0 - attrib.barycentrics.x - attrib.barycentrics.y,
                               attrib.barycentrics.x,
                               attrib.barycentrics.y);
    const float3 hit_pos = va * bary.x + vb * bary.y + vc * bary.z;
    
    // Compute geometry normal
    const float3 v0v1 = vb - va;
    const float3 v0v2 = vc - va;
    float3 geom_normal = normalize(cross(v0v1, v0v2));
    
    // Load UVs (convert to local indices first)
    uint3 local_vertex_idx = idx - uint3(mesh.vbOffset, mesh.vbOffset, mesh.vbOffset);
    float2 uva = globalUVs[mesh.uvOffset + local_vertex_idx.x];
    float2 uvb = globalUVs[mesh.uvOffset + local_vertex_idx.y];
    float2 uvc = globalUVs[mesh.uvOffset + local_vertex_idx.z];
    float2 uv = uva * bary.x + uvb * bary.y + uvc * bary.z;
    
    // Load normals (if available)
    float3 shading_normal = geom_normal;
    #if 0  // Disabled for now
    if (mesh.num_normals > 0) {
        float3 na = globalNormals[mesh.normalOffset + local_vertex_idx.x];
        float3 nb = globalNormals[mesh.normalOffset + local_vertex_idx.y];
        float3 nc = globalNormals[mesh.normalOffset + local_vertex_idx.z];
        shading_normal = normalize(na * bary.x + nb * bary.y + nc * bary.z);
    }
    #endif
    
    // Populate hit data
    HitData hit_data;
    hit_data.position = hit_pos;
    hit_data.geometry_normal = geom_normal;
    hit_data.normal = shading_normal;
    hit_data.uv = uv;
    hit_data.material_id = mesh.material_id;
    
    // Shade (Disney BRDF, path tracing, etc.)
    payload.color = shade(hit_data, payload);
}
```

### B. Global Buffer Upload (Full Flow)
```cpp
// scene.cpp: build_global_buffers()
void Scene::build_global_buffers() {
    global_vertices.clear();
    global_indices.clear();
    global_normals.clear();
    global_uvs.clear();
    mesh_descriptors.clear();
    
    for (const auto& mesh : meshes) {
        for (const auto& geom : mesh.geometries) {
            MeshDesc desc = {};
            
            // Record offsets
            desc.vbOffset = global_vertices.size();
            desc.ibOffset = global_indices.size();
            desc.normalOffset = global_normals.size();
            desc.uvOffset = global_uvs.size();
            desc.num_normals = geom.normals.size();
            desc.num_uvs = geom.uvs.size();
            desc.material_id = geom.material_id;
            
            // Append vertices
            global_vertices.insert(global_vertices.end(), 
                                  geom.vertices.begin(), 
                                  geom.vertices.end());
            
            // Append indices (PRE-ADJUSTED to global space)
            uint32_t vertexOffset = desc.vbOffset;
            for (const auto& tri : geom.indices) {
                global_indices.push_back(glm::uvec3(
                    tri.x + vertexOffset,
                    tri.y + vertexOffset,
                    tri.z + vertexOffset
                ));
            }
            
            // Append normals and UVs (unchanged)
            global_normals.insert(global_normals.end(), 
                                 geom.normals.begin(), 
                                 geom.normals.end());
            global_uvs.insert(global_uvs.end(), 
                             geom.uvs.begin(), 
                             geom.uvs.end());
            
            mesh_descriptors.push_back(desc);
        }
    }
}

// render_dxr.cpp: GPU upload
void RenderDXR::set_scene(const Scene& scene) {
    // ... (BVH building, texture upload, etc.)
    
    // Upload global buffers to GPU
    upload_global_buffer(global_vertex_buffer, scene.global_vertices);
    upload_global_buffer(global_index_buffer, scene.global_indices);
    upload_global_buffer(global_normal_buffer, scene.global_normals);
    upload_global_buffer(global_uv_buffer, scene.global_uvs);
    upload_global_buffer(mesh_desc_buffer, scene.mesh_descriptors);
}

template<typename T>
void RenderDXR::upload_global_buffer(dxr::Buffer& gpu_buf, 
                                     const std::vector<T>& data) {
    dxr::Buffer upload_buf = dxr::Buffer::upload(
        device.Get(), 
        data.size() * sizeof(T), 
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    std::memcpy(upload_buf.map(), data.data(), upload_buf.size());
    upload_buf.unmap();
    
    gpu_buf = dxr::Buffer::device(
        device.Get(), 
        upload_buf.size(), 
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    
    cmd_list->Reset(cmd_allocator.Get(), nullptr);
    cmd_list->CopyResource(gpu_buf.get(), upload_buf.get());
    auto barrier = barrier_transition(gpu_buf, 
                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmd_list->ResourceBarrier(1, &barrier);
    cmd_list->Close();
    
    ID3D12CommandList* cmd_lists = cmd_list.Get();
    cmd_queue->ExecuteCommandLists(1, &cmd_lists);
    sync_gpu();
}
```

---

**End of Technical Deep Dive**
