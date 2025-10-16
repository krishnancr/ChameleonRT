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
#else // DXR
RWTexture2D<float4> outputTexture : register(u0);
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
    
    // Trace ray
    Payload payload;
    payload.color = float3(0, 0, 0);
    
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
    
    // Write result
    outputTexture[pixel] = float4(payload.color, 1.0);
}

// ========== Miss Shader ==========

[shader("miss")]
void Miss(inout Payload payload)
{
    // Light blue background to match expected output
    payload.color = float3(0.2, 0.4, 0.8);
}

// ========== Closest Hit Shader ==========

[shader("closesthit")]
void ClosestHit(
    inout Payload payload,
    in BuiltInTriangleIntersectionAttributes attrib)
{
    // Color based on barycentric coordinates for visual verification
    float3 barycentrics = float3(
        1.0 - attrib.barycentrics.x - attrib.barycentrics.y,
        attrib.barycentrics.x,
        attrib.barycentrics.y
    );
    
    payload.color = barycentrics;
}

// ========== Shadow Miss Shader ==========

[shader("miss")]
void ShadowMiss(inout Payload payload)
{
    // Simple implementation for pipeline completeness
    payload.color = float3(0, 0, 0);
}