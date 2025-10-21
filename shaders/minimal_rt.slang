// Unified minimal ray tracing shader - works for both DXR and Vulkan
// Phase 2: Textured materials with consolidated bindings

// Import Phase 3.1 modules (available but not used yet in Phase 2)
import modules.util;
import modules.lcg_rng;
import modules.disney_bsdf;
import modules.lights;

// =============================================================================
// BACKEND-SPECIFIC RESOURCE BINDINGS
// =============================================================================

#ifdef VULKAN
// Vulkan bindings (all in Set 0)
[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene;
[[vk::binding(1, 0)]] RWTexture2D<float4> outputTexture;
[[vk::binding(2, 0)]] RWTexture2D<float4> accum_buffer;
[[vk::binding(3, 0)]] cbuffer ViewParams {
    float4 cam_pos;
    float4 cam_du;  
    float4 cam_dv;
    float4 cam_dir_top_left;
    uint frame_id;
    uint samples_per_pixel;
}
[[vk::binding(4, 0)]] StructuredBuffer<MaterialParams> material_params;
[[vk::binding(10, 0)]] StructuredBuffer<float3> globalVertices;
[[vk::binding(11, 0)]] StructuredBuffer<uint3> globalIndices;
[[vk::binding(12, 0)]] StructuredBuffer<float3> globalNormals;
[[vk::binding(13, 0)]] StructuredBuffer<float2> globalUVs;
[[vk::binding(14, 0)]] StructuredBuffer<MeshDesc> meshDescs;
[[vk::binding(30, 0)]] Sampler2D textures[];
[[vk::shader_record]] cbuffer HitGroupData { uint meshDescIndex; }
#else
// DXR register bindings
RaytracingAccelerationStructure scene : register(t0);
RWTexture2D<float4> outputTexture : register(u0);
RWTexture2D<float4> accum_buffer : register(u1);
cbuffer ViewParams : register(b0) {
    float4 cam_pos;
    float4 cam_du;  
    float4 cam_dv;
    float4 cam_dir_top_left;
    uint frame_id;
    uint samples_per_pixel;
}
StructuredBuffer<MaterialParams> material_params : register(t1);
StructuredBuffer<float3> globalVertices : register(t10, space0);
StructuredBuffer<uint3> globalIndices : register(t11, space0);
StructuredBuffer<float3> globalNormals : register(t12, space0);
StructuredBuffer<float2> globalUVs : register(t13, space0);
StructuredBuffer<MeshDesc> meshDescs : register(t14, space0);
Texture2D textures[] : register(t30, space0);
SamplerState tex_sampler : register(s0);
cbuffer HitGroupData : register(b2, space0) { uint meshDescIndex; }
#endif

// =============================================================================
// SHARED DATA STRUCTURES
// =============================================================================

// Texture channel mask macros (from texture_channel_mask.h)
#define TEXTURED_PARAM_MASK 0x80000000
#define IS_TEXTURED_PARAM(x) ((x) & 0x80000000)
#define GET_TEXTURE_CHANNEL(x) (((x) >> 29) & 0x3)
#define GET_TEXTURE_ID(x) ((x) & 0x1fffffff)

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

// Primary ray payload (matches HLSL HitInfo structure)
struct Payload
{
    float4 color_dist;  // xy=UV coordinates, w=ray distance
    float4 normal;      // xyz=surface normal, w=material_id  
};

// =============================================================================
// MATERIAL TEXTURE SAMPLING UTILITIES
// =============================================================================

float sample_scalar_texture_param(float param_value, float2 texcoord)
{
    const uint encoding = asuint(param_value);
    if ((encoding & TEXTURED_PARAM_MASK) != 0) {
        const uint texture_index = encoding & 0x1fffffff;
        const uint component_index = (encoding >> 29) & 0x3;
        
        #ifdef VULKAN
            return textures[texture_index].SampleLevel(texcoord, 0)[component_index];
        #else
            return textures[NonUniformResourceIndex(texture_index)]
                .SampleLevel(tex_sampler, texcoord, 0)[component_index];
        #endif
    }
    return param_value;
}

MaterialParams evaluate_material_at_surface(uint mat_id, float2 texcoord)
{
    MaterialParams source = material_params[NonUniformResourceIndex(mat_id)];
    MaterialParams evaluated;
    
    // Handle base color (RGB texture or constant)
    const uint color_encoding = asuint(source.base_color.x);
    if ((color_encoding & TEXTURED_PARAM_MASK) != 0) {
        const uint texture_index = color_encoding & 0x1fffffff;
        #ifdef VULKAN
            evaluated.base_color = textures[texture_index].SampleLevel(texcoord, 0).xyz;
        #else
            evaluated.base_color = textures[NonUniformResourceIndex(texture_index)]
                .SampleLevel(tex_sampler, texcoord, 0).xyz;
        #endif
    } else {
        evaluated.base_color = source.base_color;
    }
    
    // Evaluate scalar material properties
    evaluated.metallic = sample_scalar_texture_param(source.metallic, texcoord);
    evaluated.specular = sample_scalar_texture_param(source.specular, texcoord);
    evaluated.roughness = sample_scalar_texture_param(source.roughness, texcoord);
    evaluated.specular_tint = sample_scalar_texture_param(source.specular_tint, texcoord);
    evaluated.anisotropy = sample_scalar_texture_param(source.anisotropy, texcoord);
    evaluated.sheen = sample_scalar_texture_param(source.sheen, texcoord);
    evaluated.sheen_tint = sample_scalar_texture_param(source.sheen_tint, texcoord);
    evaluated.clearcoat = sample_scalar_texture_param(source.clearcoat, texcoord);
    evaluated.clearcoat_gloss = sample_scalar_texture_param(source.clearcoat_gloss, texcoord);
    evaluated.ior = sample_scalar_texture_param(source.ior, texcoord);
    evaluated.specular_transmission = sample_scalar_texture_param(source.specular_transmission, texcoord);
    
    return evaluated;
}

// =============================================================================
// RAY TRACING SHADERS
// =============================================================================

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
    
    // Write result - Process materials using unified utility functions
    float3 display_color = payload.color_dist.rgb;  // Miss shader background or UV
    if (payload.color_dist.w > 0) {
        // Hit something - evaluate material at surface
        uint material_id = uint(payload.normal.w);
        float2 surface_uv = payload.color_dist.xy;
        
        MaterialParams mat = evaluate_material_at_surface(material_id, surface_uv);
        display_color = mat.base_color;
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
    in BuiltInTriangleIntersectionAttributes bary)
{
    const uint meshID = meshDescIndex;
    MeshDesc mesh = meshDescs[NonUniformResourceIndex(meshID)];
    
    // Load indices from global buffer (with offset)
    uint3 idx = globalIndices[NonUniformResourceIndex(mesh.ibOffset + PrimitiveIndex())];
    
    // Load vertices (idx contains global vertex indices)
    float3 va = globalVertices[NonUniformResourceIndex(idx.x)];
    float3 vb = globalVertices[NonUniformResourceIndex(idx.y)];
    float3 vc = globalVertices[NonUniformResourceIndex(idx.z)];
    float3 ng = normalize(cross(vb - va, vc - va));

    // Convert global vertex indices to local indices for UV lookup
    float2 uv = float2(0, 0);
    if (mesh.num_uvs > 0) {
        uint3 local_vertex_idx = idx - uint3(mesh.vbOffset, mesh.vbOffset, mesh.vbOffset);
        float2 uva = globalUVs[NonUniformResourceIndex(mesh.uvOffset + local_vertex_idx.x)];
        float2 uvb = globalUVs[NonUniformResourceIndex(mesh.uvOffset + local_vertex_idx.y)];
        float2 uvc = globalUVs[NonUniformResourceIndex(mesh.uvOffset + local_vertex_idx.z)];
        
        uv = (1.f - bary.barycentrics.x - bary.barycentrics.y) * uva
            + bary.barycentrics.x * uvb + bary.barycentrics.y * uvc;
    }

    payload.color_dist = float4(uv, 0, RayTCurrent());
    payload.normal = float4(ng, mesh.material_id);
}
// ========== Shadow Miss Shader ==========

[shader("miss")]
void ShadowMiss(inout Payload payload)
{
    // Simple implementation for pipeline completeness
    payload.color_dist = float4(0, 0, 0, -1);
    payload.normal = float4(0, 0, 0, 0);
}