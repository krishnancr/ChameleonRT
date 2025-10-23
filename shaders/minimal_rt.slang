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
[[vk::binding(5, 0)]] StructuredBuffer<QuadLight> lights;
[[vk::binding(7, 0)]] cbuffer SceneParams {
    uint num_lights;
}
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
cbuffer SceneParams : register(b1) {
    uint num_lights;
}
StructuredBuffer<MaterialParams> material_params : register(t1);
StructuredBuffer<QuadLight> lights : register(t2);
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
    float2 dims = float2(DispatchRaysDimensions().xy);
    
    // ===== MULTI-SAMPLE ANTI-ALIASING  =====
    float3 illum = float3(0.0f);
    
    for (uint s = 0; s < samples_per_pixel; ++s) {
        // Initialize RNG for this sample (unique seed per sample)
        LCGRand rng = get_rng(frame_id * samples_per_pixel + s);
        
        // ===== PIXEL JITTER Random sub-pixel offset =====
        // Uniform jitter in [0, 1) for anti-aliasing 
        float2 d = (float2(pixel) + float2(lcg_randomf(rng), lcg_randomf(rng))) / dims;
        
        float3 ray_origin = cam_pos.xyz;
        float3 ray_dir = normalize(d.x * cam_du.xyz + d.y * cam_dv.xyz + cam_dir_top_left.xyz);
        float t_min = 0.0f;
        float t_max = 1e20f;
        
        // ===== PATH TRACING LOOP =====
        int bounce = 0;
        float3 path_throughput = float3(1.0f, 1.0f, 1.0f);
        float3 sample_illum = float3(0.0f);  // Accumulate illumination for this sample
        DisneyMaterial mat;
    
    do {
        // Setup ray
        RayDesc ray;
        ray.Origin = ray_origin;
        ray.Direction = ray_dir;
        ray.TMin = t_min;
        ray.TMax = t_max;
        
        // Trace ray (primary or indirect bounce)
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
        
        // Miss? Add sky color and terminate
        if (payload.color_dist.w <= 0.0f) {
            sample_illum += path_throughput * payload.color_dist.rgb;  // Sky color from Miss shader
            break;
        }
        
        // Hit: Extract intersection data
        float3 w_o = -ray_dir;
        float3 hit_p = ray_origin + payload.color_dist.w * ray_dir;
        float2 uv = payload.color_dist.rg;
        uint material_id = uint(payload.normal.w);
        float3 normal = payload.normal.xyz;
        
        // Evaluate material at surface
        MaterialParams mat_params = evaluate_material_at_surface(material_id, uv);
        
        // Create Disney material
        mat.base_color = mat_params.base_color;
        mat.metallic = mat_params.metallic;
        mat.roughness = max(0.01f, mat_params.roughness);  // Clamp to avoid division by zero
        mat.specular = mat_params.specular;
        mat.specular_tint = mat_params.specular_tint;
        mat.anisotropy = mat_params.anisotropy;
        mat.sheen = mat_params.sheen;
        mat.sheen_tint = mat_params.sheen_tint;
        mat.clearcoat = mat_params.clearcoat;
        mat.clearcoat_gloss = mat_params.clearcoat_gloss;
        mat.ior = mat_params.ior;
        mat.specular_transmission = mat_params.specular_transmission;
        
        // Flip normal to face camera if opaque
        if (mat.specular_transmission == 0.0f && dot(w_o, normal) < 0.0f) {
            normal = -normal;
        }
        
        // Build tangent frame
        float3 v_x, v_y;
        float3 v_z = normal;
        ortho_basis(v_x, v_y, v_z);
        
        // ===== DIRECT LIGHTING (from Phase 3.3) =====
        float3 direct_light = float3(0.0f);
        
        // Iterate through all lights
        for (uint light_idx = 0; light_idx < num_lights; ++light_idx) {
            QuadLight light = lights[light_idx];
            
            // Sample light position
            float2 light_samples = float2(lcg_randomf(rng), lcg_randomf(rng));
            float3 light_pos = sample_quad_light_position(light, light_samples);
            
            // Direction to light
            float3 w_i = light_pos - hit_p;
            float dist_to_light = length(w_i);
            w_i = w_i / dist_to_light;
            
            // Trace shadow ray
            RayDesc shadow_ray;
            shadow_ray.Origin = hit_p;
            shadow_ray.Direction = w_i;
            shadow_ray.TMin = EPSILON;
            shadow_ray.TMax = dist_to_light - EPSILON;
            
            Payload shadow_payload;
            shadow_payload.color_dist = float4(0, 0, 0, 1);  // w=1 means hit
            shadow_payload.normal = float4(0, 0, 0, 0);
            
            TraceRay(
                scene,
                RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
                0xFF,
                0,
                1,
                1,  // Shadow miss shader index
                shadow_ray,
                shadow_payload
            );
            
            // If visible (miss shader sets w < 0), evaluate BRDF
            if (shadow_payload.color_dist.w < 0) {
                float3 f = disney_brdf(mat, v_z, w_o, w_i, v_x, v_y);
                float3 emission = light.emission.rgb;
                float3 light_contrib = f * emission * abs(dot(w_i, v_z));
                direct_light += light_contrib;
            }
        }
        
        // Add direct lighting contribution
        sample_illum += path_throughput * direct_light;
        
        // ===== INDIRECT LIGHTING: Sample BRDF for next bounce =====
        float3 w_i;
        float pdf;
        float3 bsdf = sample_disney_brdf(mat, v_z, w_o, v_x, v_y, rng, w_i, pdf);
        
        // Check for path termination BEFORE using pdf
        bool terminate = (pdf <= 0.0f) || all(bsdf == float3(0.0f));
        
        if (!terminate) {
            // Only compute throughput if we're continuing (pdf is guaranteed > 0 here)
            float cos_theta = abs(dot(w_i, v_z));
            path_throughput *= (bsdf * cos_theta) / pdf;
            
            // Update ray for next bounce
            ray_origin = hit_p;
            ray_dir = w_i;
            t_min = EPSILON;  // Avoid self-intersection
            t_max = 1e20f;
            ++bounce;
            
            // ===== RUSSIAN ROULETTE TERMINATION (Task 3.4.4) =====
            // Unbiased early path termination based on throughput
            if (bounce > 3) {
                float q = max(0.05f, 
                              1.0f - max(path_throughput.x, 
                                         max(path_throughput.y, path_throughput.z)));
                if (lcg_randomf(rng) < q) {
                    break;  // Terminate path
                }
                path_throughput = path_throughput / (1.0f - q);  // Compensate to remain unbiased
            }
        } else {
            break;  // Terminate path (absorption)
        }
        
    } while (bounce < MAX_PATH_DEPTH);
    
        // Accumulate this sample's contribution
        illum += sample_illum;
        
    }  // End of SPP loop
    
    // Average all samples
    illum = illum / float(samples_per_pixel);
    
    // TODO: Add progressive accumulation (Task 3.4.5)
    // TODO: Add sRGB conversion (Task 3.4.6)
    
    // For now, write directly
    outputTexture[pixel] = float4(illum, 1.0f);
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
    // Shadow miss = light is visible
    payload.color_dist = float4(0, 0, 0, -1);  // w < 0 indicates miss/visible
    payload.normal = float4(0, 0, 0, 0);
}