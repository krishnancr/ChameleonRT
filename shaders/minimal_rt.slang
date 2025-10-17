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

// Hit group data for mesh identification (matches HLSL b2)
#ifdef VULKAN
[[vk::binding(2, 0)]]
cbuffer HitGroupData
#else // DXR
cbuffer HitGroupData : register(b2, space0)
#endif
{
    uint meshDescIndex;  // Index into meshDescs buffer
}

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
    
    // Write result - Phase 2: Show UV coordinates as colors for verification
    float3 display_color = payload.color_dist.rgb;  // Miss shader sets this to blue background
    if (payload.color_dist.w > 0) {
        // Hit something - show UV coordinates as Red-Green colors
        // Red = U coordinate, Green = V coordinate, Blue = 0
        float2 uv = payload.color_dist.xy;
        display_color = float3(uv.x, uv.y, 0.0);
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

    payload.color_dist = float4(uv, 0, RayTCurrent());
    // Note: WorldToObject4x3() transform will be added later when needed
    // For now, just use geometric normal and material_id
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