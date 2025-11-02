#pragma once

#include <vector>
#include <glm/glm.hpp>

struct Geometry {
    std::vector<glm::vec3> vertices, normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::uvec3> indices;

    size_t num_tris() const;
};

struct Mesh {
    std::vector<Geometry> geometries;

    Mesh(const std::vector<Geometry> &geometries);

    Mesh() = default;

    size_t num_tris() const;
};

/* A parameterized mesh is a combination of a mesh containing the geometries
 * with a set of material parameters to set the appearance information for those
 * geometries.
 */
struct ParameterizedMesh {
    size_t mesh_id;
    // Material IDs for the geometry to parameterize this mesh with
    std::vector<uint32_t> material_ids;

    ParameterizedMesh(size_t mesh_id, const std::vector<uint32_t> &material_ids);

    ParameterizedMesh() = default;
};

/* An instance places a parameterized mesh at some location in the scene
 */
struct Instance {
    glm::mat4 transform;
    size_t parameterized_mesh_id;

    Instance(const glm::mat4 &transform, size_t parameterized_mesh_id);

    Instance() = default;
};

// Must match render_dxr.hlsl MeshDesc (32 bytes)
struct MeshDesc {
    uint32_t vbOffset;      // Offset into globalVertices (float3 array)
    uint32_t ibOffset;      // Offset into globalIndices (uint3 array)
    uint32_t normalOffset;  // Offset into globalNormals (float3 array)
    uint32_t uvOffset;      // Offset into globalUVs (float2 array)
    uint32_t num_normals;   // Number of normals for this mesh (0 if none)
    uint32_t num_uvs;       // Number of UVs for this mesh (0 if none)
    uint32_t material_id;   // Material ID for this mesh
    uint32_t pad;           // Padding to 32 bytes
    
    MeshDesc() = default;
    MeshDesc(uint32_t vb, uint32_t ib, uint32_t no, uint32_t uv, 
             uint32_t nn, uint32_t nu, uint32_t mat)
        : vbOffset(vb), ibOffset(ib), normalOffset(no), uvOffset(uv),
          num_normals(nn), num_uvs(nu), material_id(mat), pad(0) {}
};

// Geometry instance data (for TLAS instances)
struct GeometryInstanceData {
    uint32_t meshID;        // Index into MeshDesc array
    uint32_t matrixID;      // Index into transform matrix array
    uint32_t flags;         // Instance flags (e.g., double-sided)
    
    GeometryInstanceData() = default;
    GeometryInstanceData(uint32_t mesh, uint32_t mat, uint32_t f = 0)
        : meshID(mesh), matrixID(mat), flags(f) {}
};
