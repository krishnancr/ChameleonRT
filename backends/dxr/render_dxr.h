#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include "dx12_utils.h"
#include "dxr_utils.h"
#include "render_backend.h"

#ifdef ENABLE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

#ifdef USE_SLANG_COMPILER
#include "slang_shader_compiler.h"
#endif

struct RenderDXR : RenderBackend {
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    Microsoft::WRL::ComPtr<ID3D12Device5> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> cmd_queue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmd_allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmd_list;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> render_cmd_allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> render_cmd_list, readback_cmd_list;

    dxr::Buffer view_param_buf, img_readback_buf, instance_buf, material_param_buf, light_buf,
        ray_stats_readback_buf;

    // Global buffers (for shader access)
    dxr::Buffer global_vertex_buffer;
    dxr::Buffer global_index_buffer;
    dxr::Buffer global_normal_buffer;
    dxr::Buffer global_uv_buffer;
    dxr::Buffer mesh_desc_buffer;
    dxr::Buffer instance_to_mesh_desc_buffer;  // Maps InstanceID to MeshDesc index
    
    // Track buffer sizes for SRV creation
    size_t global_vertex_count = 0;
    size_t global_index_count = 0;
    size_t global_normal_count = 0;
    size_t global_uv_count = 0;
    size_t mesh_desc_count = 0;
    size_t instance_count = 0;

    dxr::Texture2D render_target, ray_stats;
    dxr::Buffer accum_buffer;
    std::vector<dxr::Texture2D> textures;

#ifdef ENABLE_OIDN
    dxr::Buffer denoise_buffer;
    oidn::DeviceRef oidn_device;
    oidn::FilterRef oidn_filter;
#endif

    std::vector<dxr::BottomLevelBVH> meshes;
    dxr::TopLevelBVH scene_bvh;

    std::vector<ParameterizedMesh> parameterized_meshes;

    dxr::RTPipeline rt_pipeline;
    dxr::DescriptorHeap raygen_desc_heap, raygen_sampler_heap;

    // Tonemap compute shader pipeline
    dxr::RootSignature tonemap_root_sig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> tonemap_ps;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> tonemap_cmd_list;

    uint64_t fence_value = 1;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    HANDLE fence_evt;

    uint32_t frame_id = 0;
    bool native_display = false;

    // Query pool to measure just dispatch rays perf
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timing_query_heap;
    dxr::Buffer query_resolve_buffer;

#ifdef REPORT_RAY_STATS
    std::vector<uint16_t> ray_counts;
#endif

#ifdef USE_SLANG_COMPILER
    // Slang shader compiler for runtime compilation
    chameleonrt::SlangShaderCompiler slangCompiler;
#endif

    RenderDXR(Microsoft::WRL::ComPtr<ID3D12Device5> device);

    RenderDXR();

    virtual ~RenderDXR();

    std::string name() override;

    void initialize(const int fb_width, const int fb_height) override;

    void set_scene(const Scene &scene) override;

    RenderStats render(const glm::vec3 &pos,
                       const glm::vec3 &dir,
                       const glm::vec3 &up,
                       const float fovy,
                       const bool camera_changed,
                       const bool readback_framebuffer) override;

private:
    void create_device_objects();

    void build_raytracing_pipeline();

    void build_shader_resource_heap();

    void build_shader_binding_table();

    void update_view_parameters(const glm::vec3 &pos,
                                const glm::vec3 &dir,
                                const glm::vec3 &up,
                                const float fovy);

    void build_descriptor_heap();

    void record_command_lists();

    void sync_gpu();
};
