// Minimal ray tracing shader for Vulkan - based on working DXR Slang shader
// This shader is designed for Phase 1.4 Vulkan integration testing
// Uses Vulkan binding layout: binding 0, 1, 3 (not DXR's t0, u0, b0)

// Ray tracing acceleration structure - Vulkan binding 0
[[vk::binding(0, 0)]]
RaytracingAccelerationStructure scene;

// Output texture - Vulkan binding 1
[[vk::binding(1, 0)]]
RWTexture2D<float4> outputTexture;

// Simple camera parameters - Vulkan binding 3
[[vk::binding(3, 0)]]
cbuffer ViewParams
{
    float4 cam_pos;
    float4 cam_du;  
    float4 cam_dv;
    float4 cam_dir_top_left;
    uint frame_id;
    uint samples_per_pixel;
}

// Primary ray payload
struct Payload
{
    float3 color;
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
    
    // Trace primary ray
    Payload payload;
    payload.color = float3(0, 0, 0);
    
    TraceRay(scene, 
             RAY_FLAG_NONE,
             0xFF,          // Instance mask
             0,             // Ray contribution to SBT
             0,             // Multiplier for geometry contribution to SBT
             0,             // Miss shader index
             ray, 
             payload);
    
    // Write result to output texture
    outputTexture[pixel] = float4(payload.color, 1.0);
}

// ========== Miss Shader ==========

[shader("miss")]
void Miss(inout Payload payload)
{
    // Sky blue background
    payload.color = float3(0.5, 0.7, 1.0);
}

// ========== Shadow Miss Shader ==========

[shader("miss")]
void ShadowMiss(inout Payload payload)
{
    // Shadow rays that miss should contribute no occlusion
    payload.color = float3(1.0, 1.0, 1.0);
}

// ========== Closest Hit Shader ==========

[shader("closesthit")]
void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    // Simple barycentric coloring
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
                                attribs.barycentrics.x,
                                attribs.barycentrics.y);
    
    // Color based on barycentric coordinates
    payload.color = barycentrics;
}