// Unified minimal ray tracing shader - works for both DXR and Vulkan
// This shader is designed for Phase 1 Slang integration testing
// Uses preprocessor guards to handle binding differences between APIs

// Ray tracing acceleration structure
#ifdef VULKAN
[[vk::binding(0, 0)]]
RaytracingAccelerationStructure scene;
#else // DXR
RaytracingAccelerationStructure scene : register(t0);
#endif

// Output texture  
#ifdef VULKAN
[[vk::binding(1, 0)]]
RWTexture2D<float4> outputTexture;
[[vk::binding(2, 0)]]  
RWTexture2D<float4> accum_buffer;
#else // DXR
RWTexture2D<float4> outputTexture : register(u0);
RWTexture2D<float4> accum_buffer : register(u1);
#endif

// Simple camera parameters
#ifdef VULKAN
[[vk::binding(3, 0)]]
cbuffer ViewParams
#else // DXR
cbuffer ViewParams : register(b0)
#endif
{
    float4 cam_pos;
    float4 cam_du;  
    float4 cam_dv;
    float4 cam_dir_top_left;
    uint frame_id;
    uint samples_per_pixel;
}

// Scene buffers (matches HLSL layout)
#ifdef VULKAN
[[vk::binding(4, 0)]]  // Vulkan: material_params is at binding 4 (mat_params in backend)
StructuredBuffer<MaterialParams> material_params;
#else // DXR
StructuredBuffer<MaterialParams> material_params : register(t1);
#endif

// Textures and sampler (matches HLSL layout)
#ifdef VULKAN
[[vk::binding(29, 0)]]  // Vulkan: explicit sampler at binding 29 (before textures)
SamplerState tex_sampler;
[[vk::binding(30, 0)]]  // Vulkan: combined image samplers for textures (must be last binding)
Texture2D textures[];
#else // DXR
// Unbounded texture array - moved to t30 to avoid conflict with global buffers at t10-t14
Texture2D textures[] : register(t30, space0);
SamplerState tex_sampler : register(s0);
#endif

// Texture channel mask macros (from texture_channel_mask.h)
#define TEXTURED_PARAM_MASK 0x80000000
#define IS_TEXTURED_PARAM(x) ((x) & 0x80000000)
#define GET_TEXTURE_CHANNEL(x) (((x) >> 29) & 0x3)
#define SET_TEXTURE_CHANNEL(x, c) x |= (c & 0x3) << 29
#define GET_TEXTURE_ID(x) ((x) & 0x1fffffff)
#define SET_TEXTURE_ID(x, i) (x |= i & 0x1fffffff)

// Texture sampling function (matches HLSL implementation)
float textured_scalar_param(const float x, in const float2 uv) {
    const uint mask = asuint(x);
    if (IS_TEXTURED_PARAM(mask)) {
        const uint tex_id = GET_TEXTURE_ID(mask);
        const uint channel = GET_TEXTURE_CHANNEL(mask);
        return textures[NonUniformResourceIndex(tex_id)]
            .SampleLevel(tex_sampler, uv, 0)[channel];
        
    }
    return x;
}

// MeshDesc structure for per-mesh metadata (matches HLSL layout)
struct MeshDesc {
    uint vbOffset;      // Offset into globalVertices
    uint ibOffset;      // Offset into globalIndices  
    uint normalOffset;  // Offset into globalNormals
    uint uvOffset;      // Offset into globalUVs
    uint num_normals;   // Number of normals for this mesh
    uint num_uvs;       // Number of UVs for this mesh
    uint material_id;   // Material ID for this mesh
    uint pad;           // Padding to 32 bytes
};

// MaterialParams structure for material properties (matches HLSL layout)
struct MaterialParams {
    float3 base_color;
    float metallic;

    float specular;
    float roughness;
    float specular_tint;
    float anisotropy;

    float sheen;
    float sheen_tint;
    float clearcoat;
    float clearcoat_gloss;

    float ior;
    float specular_transmission;
    float2 pad;
};

// Global geometry buffers (matches HLSL layout at registers t10-t14)
#ifdef VULKAN
[[vk::binding(10, 0)]]
StructuredBuffer<float3> globalVertices;
[[vk::binding(11, 0)]]
StructuredBuffer<uint3> globalIndices;
[[vk::binding(12, 0)]]
StructuredBuffer<float3> globalNormals;
[[vk::binding(13, 0)]]
StructuredBuffer<float2> globalUVs;
[[vk::binding(14, 0)]]
StructuredBuffer<MeshDesc> meshDescs;
#else // DXR
StructuredBuffer<float3> globalVertices : register(t10, space0);
StructuredBuffer<uint3> globalIndices : register(t11, space0);
StructuredBuffer<float3> globalNormals : register(t12, space0);
StructuredBuffer<float2> globalUVs : register(t13, space0);
StructuredBuffer<MeshDesc> meshDescs : register(t14, space0);
#endif

// Hit group data for mesh identification
#ifdef VULKAN
// Vulkan uses vk::shader_record attribute for per-hit-group data (stored in SBT, not a descriptor)
[[vk::shader_record]]
cbuffer HitGroupData
{
    uint meshDescIndex;  // Index into global meshDescs buffer
}
#else // DXR
// DXR uses constant buffer binding for hit group local root signature
cbuffer HitGroupData : register(b2, space0)
{
    uint meshDescIndex;  // Index into meshDescs buffer
}
#endif

// Primary ray payload (matches HLSL HitInfo structure)
struct Payload
{
    float4 color_dist;  // xy=UV coordinates, z=unused, w=ray distance
    float4 normal;      // xyz=surface normal, w=material_id  
}

// ========== Ray Generation Shader ==========

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;
    
    // Calculate normalized screen coordinates [0, 1]
    float2 screen_uv = (float2(pixel) + 0.5) / float2(dims);
    
    // Calculate ray direction using DXR camera model
    float3 ray_dir = cam_dir_top_left.xyz + 
                     screen_uv.x * cam_du.xyz + 
                     screen_uv.y * cam_dv.xyz;
    ray_dir = normalize(ray_dir);
    
    // Setup ray
    RayDesc ray;
    ray.Origin = cam_pos.xyz;
    ray.Direction = ray_dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    
    // Trace ray
    Payload payload;
    payload.color_dist = float4(0, 0, 0, -1);
    payload.normal = float4(0, 0, 0, 0);
    
    TraceRay(
        scene,              // Acceleration structure
        RAY_FLAG_NONE,      // Ray flags
        0xFF,               // Instance inclusion mask
        0,                  // Ray contribution to hit group index
        1,                  // Multiplier for geometry contribution to hit group index
        0,                  // Miss shader index
        ray,
        payload
    );
    
    // Write result - Both DXR and Vulkan: Process materials in RayGen
    float3 display_color = payload.color_dist.rgb;  // Miss shader background or UV
    if (payload.color_dist.w > 0) {
        // Hit something - get material data and process textures  
        uint material_id = uint(payload.normal.w);
        float2 uv = payload.color_dist.xy;
        
        // Unified material processing for both backends
        MaterialParams p = material_params[NonUniformResourceIndex(material_id)];
        
        // Sample base_color (either from texture or material parameter)
        float3 base_color;
        const uint mask = asuint(p.base_color.x);
        if (IS_TEXTURED_PARAM(mask)) {
            const uint tex_id = GET_TEXTURE_ID(mask);
            base_color = textures[NonUniformResourceIndex(tex_id)]
                .SampleLevel(tex_sampler, uv, 0).xyz;
        } else {
            base_color = p.base_color;
        }
        display_color = base_color;
    }
    outputTexture[pixel] = float4(display_color, 1.0);
}

// ========== Miss Shader ==========

[shader("miss")]
void Miss(inout Payload payload)
{
    // Light blue background to match expected output
    payload.color_dist = float4(0.2, 0.4, 0.8, -1.0);  // w=-1 indicates miss
    payload.normal = float4(0, 0, 0, 0);
}

// ========== Closest Hit Shader ==========

[shader("closesthit")]
void ClosestHit(
    inout Payload payload,
    in BuiltInTriangleIntersectionAttributes attrib)
{
    // Use meshDescIndex from SBT to identify which geometry was hit
    // InstanceID() returns instance index (TLAS), but we need geometry index (BLAS)
    // Since GeometryIndex() is unavailable in SM 6.3, we pass meshDescIndex via SBT
    const uint meshID = meshDescIndex;
    
    // Load mesh descriptor
    MeshDesc mesh = meshDescs[NonUniformResourceIndex(meshID)];
    
    // Load indices from global buffer (with offset)
    uint3 idx = globalIndices[NonUniformResourceIndex(mesh.ibOffset + PrimitiveIndex())];
    
    // CRITICAL: idx contains GLOBAL vertex indices (already offset by vbOffset during build)
    // So we use idx directly for vertices, but need LOCAL indices for UVs/normals
    
    // Load vertices from global buffer (NO additional offset - idx is already global!)
    float3 va = globalVertices[NonUniformResourceIndex(idx.x)];
    float3 vb = globalVertices[NonUniformResourceIndex(idx.y)];
    float3 vc = globalVertices[NonUniformResourceIndex(idx.z)];
    float3 ng = normalize(cross(vb - va, vc - va));

    float3 n = ng;
    // Per-vertex normals (can be enabled if needed)
    uint3 local_vertex_idx = idx - uint3(mesh.vbOffset, mesh.vbOffset, mesh.vbOffset);
    
    // Note: Normal interpolation disabled for now (matches HLSL #if 0 block)
    // if (mesh.num_normals > 0) {
    //     float3 na = globalNormals[NonUniformResourceIndex(mesh.normalOffset + local_vertex_idx.x)];
    //     float3 nb = globalNormals[NonUniformResourceIndex(mesh.normalOffset + local_vertex_idx.y)];
    //     float3 nc = globalNormals[NonUniformResourceIndex(mesh.normalOffset + local_vertex_idx.z)];
    //     n = normalize((1.f - attrib.barycentrics.x - attrib.barycentrics.y) * na
    //             + attrib.barycentrics.x * nb + attrib.barycentrics.y * nc);
    // }

    // Convert global vertex indices to local indices for UV lookup
    float2 uv = float2(0, 0);
    if (mesh.num_uvs > 0) {
        float2 uva = globalUVs[NonUniformResourceIndex(mesh.uvOffset + local_vertex_idx.x)];
        float2 uvb = globalUVs[NonUniformResourceIndex(mesh.uvOffset + local_vertex_idx.y)];
        float2 uvc = globalUVs[NonUniformResourceIndex(mesh.uvOffset + local_vertex_idx.z)];
        uv = (1.f - attrib.barycentrics.x - attrib.barycentrics.y) * uva
            + attrib.barycentrics.x * uvb + attrib.barycentrics.y * uvc;
    }

    // Both DXR and Vulkan: Return geometry data for material processing in RayGen
    payload.color_dist = float4(uv, 0, RayTCurrent());
    payload.normal = float4(n, mesh.material_id);
}

// ========== Shadow Miss Shader ==========

[shader("miss")]
void ShadowMiss(inout Payload payload)
{
    // Simple implementation for pipeline completeness
    payload.color_dist = float4(0, 0, 0, -1);
    payload.normal = float4(0, 0, 0, 0);
}