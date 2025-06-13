// Ray tracing shader that renders green color for ray hits and blue color for ray misses
#include "util.slang"

// Raytracing output texture, accessed as a UAV
RWTexture2D<float4> output : register(u0);

// Accumulation buffer for progressive refinement
RWTexture2D<float4> accum_buffer : register(u1);

#ifdef REPORT_RAY_STATS
RWTexture2D<uint> ray_stats : register(u2);
#endif

// View params buffer
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

// Raytracing acceleration structure, accessed as a SRV
RaytracingAccelerationStructure scene : register(t0);

// Per-mesh parameters for the closest hit
StructuredBuffer<float3> vertices : register(t0, space1);
StructuredBuffer<uint3> indices : register(t1, space1);
StructuredBuffer<float3> normals : register(t2, space1);
StructuredBuffer<float2> uvs : register(t3, space1);

cbuffer MeshData : register(b0, space1) {
    uint32_t material_id;
    uint32_t id_pad0;
    uint32_t id_pad1;
    uint32_t id_pad2;
}

[shader("raygeneration")] 
void RayGen() {
    const uint2 pixel = DispatchRaysIndex().xy;
    const float2 dims = float2(DispatchRaysDimensions().xy);

    // Ray setup
    RayDesc ray;
    ray.Origin = cam_pos.xyz;
    
    // Use the camera ray direction calculation
    const float2 d = pixel / dims;
    ray.Direction = normalize(d.x * cam_du.xyz + d.y * cam_dv.xyz + cam_dir_top_left.xyz);
    
    ray.TMin = 0;
    ray.TMax = 1e20f;

    HitInfo payload;
    payload.color_dist = float4(0, 0, 0, 0);
    payload.normal = float4(0, 0, 0, 0);

    // Trace the ray
    TraceRay(scene, 0, 0xff, 0, 1, 0, ray, payload);

    // Use the color from the ray tracing result - green for hits, blue for misses
    float3 result_color = payload.color_dist.xyz;

    if (frame_id == 0) {
        // First frame, initialize the accumulation buffer
        accum_buffer[pixel] = float4(result_color, 1.0);
    } else {
        // Progressive accumulation for refinement
        accum_buffer[pixel] += float4(result_color, 1.0);
    }

    // Write the normalized accumulated result to the output
    output[pixel] = float4(accum_buffer[pixel].rgb / float(frame_id + 1), 1.0);

#ifdef REPORT_RAY_STATS
    ray_stats[pixel] = 1; // Just report 1 ray per pixel for simplified version
#endif
}

[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload) {
    payload.color_dist.w = -1.f;
    // Miss shader outputs blue color
    payload.color_dist.rgb = float3(0, 0, 1); // Pure blue
}

[shader("miss")]
void ShadowMiss(inout OcclusionHitInfo occlusion : SV_RayPayload) {
    occlusion.hit = 0;
}

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) {
    // In the simplified shader, we output green for hit geometry
    payload.color_dist.rgb = float3(0, 1, 0); // Pure green color
    
    // Record hit distance
    payload.color_dist.w = RayTCurrent();
    
    // Get the triangle primitive vertices
    uint3 idx = indices[NonUniformResourceIndex(PrimitiveIndex())];
    float3 va = vertices[NonUniformResourceIndex(idx.x)];
    float3 vb = vertices[NonUniformResourceIndex(idx.y)];
    float3 vc = vertices[NonUniformResourceIndex(idx.z)];
    
    // Compute a simple geometric normal for the hit
    float3 normal = normalize(cross(vb - va, vc - va));
    payload.normal = float4(normal, material_id);
}
