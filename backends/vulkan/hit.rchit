#version 460
#extension GL_EXT_scalar_block_layout : require

#include "util.glsl"

// ============================================================================
// PHASE 4: Global Buffer Implementation (COMPLETE)
// ============================================================================

// MeshDesc structure (matches CPU-side util/mesh.h)
struct MeshDesc {
    uint vbOffset;      // Offset into globalVertices
    uint ibOffset;      // Offset into globalIndices
    uint normalOffset;  // Offset into globalNormals
    uint uvOffset;      // Offset into globalUVs
    uint num_normals;   // Count (0 if none)
    uint num_uvs;       // Count (0 if none)
    uint material_id;   // Material index
    uint pad;           // Alignment to 32 bytes
};

layout(location = PRIMARY_RAY) rayPayloadInEXT RayPayload payload;

hitAttributeEXT vec3 attrib;

// Global geometry buffers (bindings 10-14, set 0)
layout(binding = 10, set = 0, scalar) readonly buffer GlobalVertices {
    vec3 v[];
} globalVertices;

layout(binding = 11, set = 0, scalar) readonly buffer GlobalIndices {
    uvec3 i[];
} globalIndices;

layout(binding = 12, set = 0, scalar) readonly buffer GlobalNormals {
    vec3 n[];
} globalNormals;

layout(binding = 13, set = 0, scalar) readonly buffer GlobalUVs {
    vec2 uv[];
} globalUVs;

layout(binding = 14, set = 0, std430) readonly buffer MeshDescBuffer {
    MeshDesc descs[];
} meshDescs;

// Shader Binding Table - only stores mesh descriptor index
layout(shaderRecordEXT, std430) buffer SBT {
    uint32_t meshDescIndex;    // Index into global meshDescs buffer
};

void main() {
    // Get geometry descriptor from SBT
    const uint meshID = meshDescIndex;
    MeshDesc mesh = meshDescs.descs[meshID];
    
    // Load triangle indices (ALREADY GLOBAL - pre-adjusted during scene build!)
    const uvec3 idx = globalIndices.i[mesh.ibOffset + gl_PrimitiveID];
    
    // ✅ CORRECT: Load vertex positions (direct indexing - idx is already global)
    // DO NOT add mesh.vbOffset here - would cause double-offset bug!
    const vec3 va = globalVertices.v[idx.x];
    const vec3 vb = globalVertices.v[idx.y];
    const vec3 vc = globalVertices.v[idx.z];
    
    // Compute geometry normal
    const vec3 n = normalize(cross(vb - va, vc - va));
    
    // Load UVs (convert to local indices first!)
    vec2 uv = vec2(0);
    if (mesh.num_uvs > 0) {
        // ✅ CORRECT: Convert global indices to local, then offset into UV buffer
        const uvec3 local_idx = idx - uvec3(mesh.vbOffset);
        const vec2 uva = globalUVs.uv[mesh.uvOffset + local_idx.x];
        const vec2 uvb = globalUVs.uv[mesh.uvOffset + local_idx.y];
        const vec2 uvc = globalUVs.uv[mesh.uvOffset + local_idx.z];
        uv = (1.f - attrib.x - attrib.y) * uva + attrib.x * uvb + attrib.y * uvc;
    }
    
    // Transform to world space
    mat3 inv_transp = transpose(mat3(gl_WorldToObjectEXT));
    payload.normal = normalize(inv_transp * n);
    payload.dist = gl_RayTmaxEXT;
    payload.uv = uv;
    payload.material_id = mesh.material_id;
}

