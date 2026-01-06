#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include "camera.h"
#include "lights.h"
#include "material.h"
#include "mesh.h"
#include "phmap.h"

#ifdef PBRT_PARSER_ENABLED
#include "pbrtParser/Scene.h"
#endif

/* Different material models for benchmarking
 * DEFAULT: Use the materials/textures/etc as they are in the file
 * WHITE_DIFFUSE: Ignore all materials and shade everything using the default white diffuse
 * material
 */
enum class MaterialMode { DEFAULT, WHITE_DIFFUSE };

struct Scene {
    std::vector<Mesh> meshes;
    std::vector<ParameterizedMesh> parameterized_meshes;
    std::vector<Instance> instances;
    std::vector<DisneyMaterial> materials;
    std::vector<Image> textures;
    std::vector<QuadLight> lights;
    std::vector<Camera> cameras;
    uint32_t samples_per_pixel = 1;
    MaterialMode material_mode = MaterialMode::DEFAULT;

    // Environment map (optional)
    std::string environment_map_path;

    // Global buffers for GPU upload (matching shader structure EXACTLY)
    // Separate arrays to match shader layout (not merged Vertex struct)
    std::vector<glm::vec3> global_vertices;       // All vertex positions concatenated
    std::vector<glm::uvec3> global_indices;       // All indices concatenated (as uvec3)
    std::vector<glm::vec3> global_normals;        // All normals concatenated
    std::vector<glm::vec2> global_uvs;            // All UVs concatenated
    std::vector<MeshDesc> mesh_descriptors;       // Metadata per geometry
    std::vector<GeometryInstanceData> geometry_instances; // Metadata per instance
    std::vector<glm::mat4> transform_matrices;    // Transform per instance
    
    // Scene bounds for adaptive camera movement
    struct Bounds {
        glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
        glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());
        
        void expand(const glm::vec3 &point) {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }
        
        void expand(const Bounds &other) {
            min = glm::min(min, other.min);
            max = glm::max(max, other.max);
        }
        
        glm::vec3 center() const { return (min + max) * 0.5f; }
        glm::vec3 extent() const { return max - min; }
        float diagonal() const { return glm::length(extent()); }
        bool is_valid() const { return min.x <= max.x; }
    };
    
    // Helper function to build global buffers
    void build_global_buffers();
    
    // Compute scene bounding box from all instances and meshes
    Bounds compute_bounds() const;

    Scene(const std::string &fname, MaterialMode material_mode, const std::string &env_map = "");
    Scene() = default;

    // Compute the unique number of triangles in the scene
    size_t unique_tris() const;

    // Compute the total number of triangles in the scene (after instancing)
    size_t total_tris() const;

    size_t num_geometries() const;

private:
    void load_obj(const std::string &file);

    void load_gltf(const std::string &file);

    void load_crts(const std::string &file);

#ifdef PBRT_PARSER_ENABLED
    void load_pbrt(const std::string &file);

    uint32_t load_pbrt_materials(
        const pbrt::Material::SP &mat,
        const std::map<std::string, pbrt::Texture::SP> &texture_overrides,
        const std::string &pbrt_base_dir,
        phmap::parallel_flat_hash_map<pbrt::Material::SP, size_t> &pbrt_materials,
        phmap::parallel_flat_hash_map<pbrt::Texture::SP, size_t> &pbrt_textures);

    uint32_t load_pbrt_texture(
        const pbrt::Texture::SP &texture,
        const std::string &pbrt_base_dir,
        phmap::parallel_flat_hash_map<pbrt::Texture::SP, size_t> &pbrt_textures);
#endif

    void validate_materials();
};
