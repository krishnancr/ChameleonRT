// Simple Lambertian shader compatible with ChameleonRT DXR pipeline
// This shader matches the pipeline's resource binding expectations while
// implementing basic diffuse (Lambertian) shading

#ifndef UTIL_HLSL
#define UTIL_HLSL

#define M_PI 3.14159265358979323846f
#define M_1_PI 0.318309886183790671538f
#define EPSILON 0.001f

#define PRIMARY_RAY 0
#define OCCLUSION_RAY 1
#define MAX_PATH_DEPTH 5

// Match the production pipeline's payload structures
struct HitInfo {
    float4 color_dist;
    float4 normal;
};

struct OcclusionHitInfo {
    int hit;
};

struct Attributes {
    float2 bary;
};

#endif // UTIL_HLSL

// ============================================================================
// Global Resources (must match production pipeline expectations)
// ============================================================================

// UAVs (Unordered Access Views)
RWTexture2D<float4> output : register(u0);
RWTexture2D<float4> accum_buffer : register(u1);

#ifdef REPORT_RAY_STATS
RWTexture2D<uint> ray_stats : register(u2);
#endif

// CBVs (Constant Buffer Views)
cbuffer ViewParams : register(b0) {
    float4 cam_pos;
    float4 cam_du;
    float4 cam_dv;
    float4 cam_dir_top_left;
    uint32_t frame_id;
    uint32_t samples_per_pixel;
}

cbuffer SceneParams : register(b1) {
    uint32_t num_lights;
};

// SRVs (Shader Resource Views)
RaytracingAccelerationStructure scene : register(t0);

// Dummy structured buffers to match pipeline expectations
// (We won't use these in the simple shader, but they must be declared)
StructuredBuffer<float4> material_params : register(t1);
StructuredBuffer<float4> lights : register(t2);
Texture2D textures[] : register(t3);

// Samplers
SamplerState tex_sampler : register(s0);

// ============================================================================
// Space1 Resources (for ClosestHit shader)
// ============================================================================

StructuredBuffer<float3> vertices : register(t0, space1);
StructuredBuffer<uint3> indices : register(t1, space1);
StructuredBuffer<float3> normals : register(t2, space1);
StructuredBuffer<float2> uvs : register(t3, space1);

cbuffer MeshData : register(b0, space1) {
    uint32_t num_normals;
    uint32_t num_uvs;
    uint32_t material_id;
}

// ============================================================================
// Utility Functions
// ============================================================================

float linear_to_srgb(float x) {
    if (x <= 0.0031308f) {
        return 12.92f * x;
    }
    return 1.055f * pow(x, 1.f / 2.4f) - 0.055f;
}

// Simple pseudo-random number generator (LCG)
uint lcg_step(inout uint rng_state) {
    uint state = rng_state;
    rng_state = rng_state * 1664525u + 1013904223u;
    return state;
}

float lcg_randomf(inout uint rng_state) {
    return (lcg_step(rng_state) & 0xffffffu) / 16777216.0f;
}

// Generate orthonormal basis from normal
void ortho_basis(out float3 v_x, out float3 v_y, const float3 n) {
    v_y = float3(0, 0, 0);

    if (n.x < 0.6f && n.x > -0.6f) {
        v_y.x = 1.f;
    } else if (n.y < 0.6f && n.y > -0.6f) {
        v_y.y = 1.f;
    } else if (n.z < 0.6f && n.z > -0.6f) {
        v_y.z = 1.f;
    } else {
        v_y.x = 1.f;
    }

    v_x = normalize(cross(v_y, n));
    v_y = normalize(cross(n, v_x));
}

// Cosine-weighted hemisphere sampling
float3 sample_cosine_hemisphere(float2 uv, out float pdf) {
    float r = sqrt(uv.x);
    float theta = 2.0f * M_PI * uv.y;
    
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0f, 1.0f - uv.x));
    
    pdf = z * M_1_PI;
    return float3(x, y, z);
}

// ============================================================================
// Ray Generation Shader
// ============================================================================

[shader("raygeneration")]
void RayGen() {
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;

    // Initialize RNG with pixel coordinates and frame ID
    uint rng_state = (pixel.x * 1973u + pixel.y * 9277u + frame_id * 26699u) | 1u;

    float3 color = 0.0f;

    // Simple multi-sample anti-aliasing
    for (uint s = 0; s < samples_per_pixel; ++s) {
        // Compute pixel center with jitter for anti-aliasing
        float2 jitter = float2(lcg_randomf(rng_state), lcg_randomf(rng_state));
        float2 d = ((float2(pixel) + jitter) / float2(dims)) * 2.f - 1.f;

        // Construct primary ray
        RayDesc ray;
        ray.Origin = cam_pos.xyz;
        ray.Direction = normalize(d.x * cam_du.xyz + d.y * cam_dv.xyz + cam_dir_top_left.xyz);
        ray.TMin = 0.0f;
        ray.TMax = 1e20f;

        HitInfo payload;
        payload.color_dist = float4(0, 0, 0, -1);
        payload.normal = float4(0, 0, 1, 0);

        // Trace primary ray
        TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xff, PRIMARY_RAY, 1, PRIMARY_RAY, ray, payload);

        // Simple path tracing with Lambertian BRDF
        float3 path_color = 0.0f;
        float3 throughput = 1.0f;

        if (payload.color_dist.w > 0.0f) {
            // We hit something
            float3 hit_pos = ray.Origin + ray.Direction * payload.color_dist.w;
            float3 normal = normalize(payload.normal.xyz);
            float3 wo = -ray.Direction;

            // Make normal face forward
            if (dot(wo, normal) < 0.0f) {
                normal = -normal;
            }

            // Simple diffuse material (Lambertian)
            // Base color from UV coordinates (simple procedural texture)
            float3 albedo = float3(0.8f, 0.6f, 0.4f);
            if (payload.color_dist.x > 0.0f || payload.color_dist.y > 0.0f) {
                // Use UV for simple checkerboard pattern
                float checker = fmod(floor(payload.color_dist.x * 4.0f) + floor(payload.color_dist.y * 4.0f), 2.0f);
                albedo = checker > 0.5f ? float3(0.8f, 0.6f, 0.4f) : float3(0.4f, 0.6f, 0.8f);
            }

            // Simple single-bounce indirect lighting
            float3 v_x, v_y;
            ortho_basis(v_x, v_y, normal);

            // Sample hemisphere
            float2 random_uv = float2(lcg_randomf(rng_state), lcg_randomf(rng_state));
            float pdf;
            float3 local_dir = sample_cosine_hemisphere(random_uv, pdf);
            float3 wi = local_dir.x * v_x + local_dir.y * v_y + local_dir.z * normal;

            // Lambertian BRDF = albedo / pi
            float3 brdf = albedo * M_1_PI;
            float cos_theta = dot(wi, normal);

            // Trace secondary ray
            RayDesc secondary_ray;
            secondary_ray.Origin = hit_pos + normal * EPSILON;
            secondary_ray.Direction = wi;
            secondary_ray.TMin = 0.0f;
            secondary_ray.TMax = 1e20f;

            HitInfo secondary_payload;
            secondary_payload.color_dist = float4(0, 0, 0, -1);
            secondary_payload.normal = float4(0, 0, 1, 0);

            TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xff, PRIMARY_RAY, 1, PRIMARY_RAY, secondary_ray, secondary_payload);

            // Simple sky lighting from miss shader
            float3 indirect = secondary_payload.color_dist.rgb;

            // Combine direct (ambient) and indirect lighting
            float3 ambient = float3(0.3f, 0.3f, 0.3f);
            path_color = albedo * ambient;
            
            if (pdf > EPSILON) {
                path_color += brdf * indirect * cos_theta / pdf;
            }
        } else {
            // Miss - use sky color
            path_color = payload.color_dist.rgb;
        }

        color += path_color;
    }

    color = color / float(samples_per_pixel);

    // Accumulate with previous frames
    const float4 accum_color = (float4(color, 1.0f) + frame_id * accum_buffer[pixel]) / (frame_id + 1);
    accum_buffer[pixel] = accum_color;

    // Output with gamma correction
    output[pixel] = float4(
        linear_to_srgb(accum_color.r),
        linear_to_srgb(accum_color.g),
        linear_to_srgb(accum_color.b),
        1.0f
    );

#ifdef REPORT_RAY_STATS
    ray_stats[pixel] = samples_per_pixel * 2; // Primary + secondary rays
#endif
}

// ============================================================================
// Miss Shader
// ============================================================================

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload) {
    payload.color_dist.w = -1.f;

    // Simple procedural sky (same as production shader)
    float3 dir = WorldRayDirection();
    float u = (1.f + atan2(dir.x, -dir.z) * M_1_PI) * 0.5f;
    float v = acos(dir.y) * M_1_PI;

    int check_x = u * 10.f;
    int check_y = v * 10.f;

    if (dir.y > -0.1 && (check_x + check_y) % 2 == 0) {
        payload.color_dist.rgb = 0.5f;
    } else {
        payload.color_dist.rgb = 0.1f;
    }
}

// ============================================================================
// Shadow Miss Shader (required by pipeline)
// ============================================================================

[shader("miss")]
void ShadowMiss(inout OcclusionHitInfo occlusion : SV_RayPayload) {
    occlusion.hit = 0;
}

// ============================================================================
// Closest Hit Shader
// ============================================================================

[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib) {
    // Get triangle indices
    uint3 idx = indices[NonUniformResourceIndex(PrimitiveIndex())];

    // Get vertex positions
    float3 va = vertices[NonUniformResourceIndex(idx.x)];
    float3 vb = vertices[NonUniformResourceIndex(idx.y)];
    float3 vc = vertices[NonUniformResourceIndex(idx.z)];
    
    // Compute geometric normal
    float3 ng = normalize(cross(vb - va, vc - va));

    float3 n = ng;
    
    // Use smooth normals if available
    if (num_normals > 0) {
        float3 na = normals[NonUniformResourceIndex(idx.x)];
        float3 nb = normals[NonUniformResourceIndex(idx.y)];
        float3 nc = normals[NonUniformResourceIndex(idx.z)];
        n = normalize((1.f - attrib.bary.x - attrib.bary.y) * na
                + attrib.bary.x * nb + attrib.bary.y * nc);
    }

    // Interpolate UV coordinates if available
    float2 uv = float2(0, 0);
    if (num_uvs > 0) {
        float2 uva = uvs[NonUniformResourceIndex(idx.x)];
        float2 uvb = uvs[NonUniformResourceIndex(idx.y)];
        float2 uvc = uvs[NonUniformResourceIndex(idx.z)];
        uv = (1.f - attrib.bary.x - attrib.bary.y) * uva
            + attrib.bary.x * uvb + attrib.bary.y * uvc;
    }

    // Store hit information
    payload.color_dist = float4(uv, 0, RayTCurrent());
    
    // Transform normal to world space
    float3x3 inv_transp = float3x3(
        WorldToObject4x3()[0],
        WorldToObject4x3()[1],
        WorldToObject4x3()[2]
    );
    payload.normal = float4(normalize(mul(inv_transp, n)), material_id);
}
