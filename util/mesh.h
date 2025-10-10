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

// Phase 2A.1: Global buffer structures

// Unified vertex structure (combines position, normal, texCoord)
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    
    Vertex() = default;
    Vertex(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& uv)
        : position(pos), normal(norm), texCoord(uv) {}
};

// Mesh descriptor (offset into global buffers)
struct MeshDesc {
    uint32_t vbOffset;      // Offset into global vertex buffer
    uint32_t ibOffset;      // Offset into global index buffer
    uint32_t vertexCount;   // Number of vertices
    uint32_t indexCount;    // Number of indices (triangles * 3)
    uint32_t materialID;    // Material index
    
    MeshDesc() = default;
    MeshDesc(uint32_t vb, uint32_t ib, uint32_t vc, uint32_t ic, uint32_t mat)
        : vbOffset(vb), ibOffset(ib), vertexCount(vc), indexCount(ic), materialID(mat) {}
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
