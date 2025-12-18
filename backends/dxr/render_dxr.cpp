#include "render_dxr.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#ifndef USE_SLANG_COMPILER
#include "render_dxr_embedded_dxil.h"
#endif
#ifdef USE_SLANG_COMPILER
#include "../../util/slang_shader_compiler.h"
#endif
#include "util.h"
#include <glm/ext.hpp>

#ifdef ENABLE_PIX_RUNTIME
#include <pix3.h>
#endif

#define NUM_RAY_TYPES 2

using Microsoft::WRL::ComPtr;

RenderDXR::RenderDXR(Microsoft::WRL::ComPtr<ID3D12Device5> device)
    : device(device), native_display(true)
{
    create_device_objects();
}

RenderDXR::RenderDXR() : native_display(false)
{
    // Enable debugging for D3D12
#ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> debug_controller;
        auto err = D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller));
        if (FAILED(err)) {
            std::cout << "Failed to enable debug layer!\n";
            throw std::runtime_error("get debug failed");
        }
        debug_controller->EnableDebugLayer();
    }
#endif

#ifdef _DEBUG
    uint32_t factory_flags = DXGI_CREATE_FACTORY_DEBUG;
#else
    uint32_t factory_flags = 0;
#endif
    CHECK_ERR(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)));

    device = dxr::create_dxr_device(factory);
    if (!device) {
        std::cout << "Failed to find DXR capable GPU!" << std::endl;
        throw std::runtime_error("Failed to find DXR capable device!");
    }
    create_device_objects();
}

RenderDXR::~RenderDXR()
{
    CloseHandle(fence_evt);
}

std::string RenderDXR::name()
{
    return "DirectX Ray Tracing";
}

void RenderDXR::initialize(const int fb_width, const int fb_height)
{
    frame_id = 0;
    img.resize(fb_width * fb_height);

#ifdef ENABLE_OIDN
    // Get the LUID of the adapter
    LUID luid = device->GetAdapterLuid();

    // Initialize the denoiser device
    oidn_device = oidn::newDevice(oidn::LUID{luid.LowPart, luid.HighPart});
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to create OIDN device.");
    oidn_device.commit();
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to commit OIDN device.");

    // Find a compatible external memory handle type
    const auto oidn_external_mem_types = oidn_device.get<oidn::ExternalMemoryTypeFlags>("externalMemoryTypes");
    if (!(oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::OpaqueWin32))
        throw std::runtime_error("Failed to find compatible external memory type");
#endif

    render_target = dxr::Texture2D::device(device.Get(),
                                           glm::uvec2(fb_width, fb_height),
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                           DXGI_FORMAT_R8G8B8A8_UNORM,
                                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

#ifdef ENABLE_OIDN
    // Allocate 3x size for AccumPixel struct (shared for OIDN access)
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       3 * sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_HEAP_FLAG_SHARED);

    denoise_buffer = dxr::Buffer::device(device.Get(),
                                         sizeof(glm::vec4) * fb_width * fb_height,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_HEAP_FLAG_SHARED);
#else
    // Allocate 3x size for AccumPixel struct: color + albedo + normal
    accum_buffer = dxr::Buffer::device(device.Get(),
                                       3 * sizeof(glm::vec4) * fb_width * fb_height,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
#endif

    // Allocate the readback buffer so we can read the image back to the CPU
    img_readback_buf = dxr::Buffer::readback(device.Get(),
                                             render_target.linear_row_pitch() * fb_height,
                                             D3D12_RESOURCE_STATE_COPY_DEST);

#ifdef REPORT_RAY_STATS
    ray_stats = dxr::Texture2D::device(device.Get(),
                                       glm::uvec2(fb_width, fb_height),
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                       DXGI_FORMAT_R16_UINT,
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    ray_stats_readback_buf = dxr::Buffer::readback(device.Get(),
                                                   ray_stats.linear_row_pitch() * fb_height,
                                                   D3D12_RESOURCE_STATE_COPY_DEST);
    ray_counts.resize(ray_stats.dims().x * ray_stats.dims().y, 0);
#endif

    if (rt_pipeline.get()) {
        build_descriptor_heap();
        record_command_lists();
    }

#ifdef ENABLE_OIDN
    {
        // Initialize the denoiser filter
        oidn_filter = oidn_device.newFilter("RT");
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to create OIDN filter.");

        // Create shared handle for accum_buffer
        HANDLE accum_buffer_handle = nullptr;
        CHECK_ERR(device->CreateSharedHandle(
                    accum_buffer.get(),
                    nullptr,
                    GENERIC_ALL,
                    nullptr,
                    &accum_buffer_handle));
        auto input_buffer = oidn_device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32,
                                                  accum_buffer_handle, nullptr, accum_buffer.size());

        // Create shared handle for denoise_buffer
        HANDLE denoise_buffer_handle = nullptr;
        CHECK_ERR(device->CreateSharedHandle(
                    denoise_buffer.get(),
                    nullptr,
                    GENERIC_ALL,
                    nullptr,
                    &denoise_buffer_handle));
        auto output_buffer = oidn_device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32,
                                                   denoise_buffer_handle, nullptr, denoise_buffer.size());

        // Configure filter inputs from AccumPixel struct layout:
        // struct AccumPixel { float4 color; float4 albedo; float4 normal; }
        // Stride between pixels = 3 * sizeof(glm::vec4) = 48 bytes
        // Color at offset 0, Albedo at offset 16, Normal at offset 32
        oidn_filter.setImage("color",  input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                             0 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
        oidn_filter.setImage("albedo", input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                             1 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));
        oidn_filter.setImage("normal", input_buffer,  oidn::Format::Float3, fb_width, fb_height,
                             2 * sizeof(glm::vec4), 3 * sizeof(glm::vec4));

        oidn_filter.setImage("output", output_buffer, oidn::Format::Float3, fb_width, fb_height,
                             0, sizeof(glm::vec4));

        oidn_filter.set("hdr", true);
        oidn_filter.set("quality", oidn::Quality::High);

        oidn_filter.commit();
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to commit OIDN filter.");
    }
#endif
}

void RenderDXR::set_scene(const Scene &scene)
{
    frame_id = 0;
    samples_per_pixel = scene.samples_per_pixel;

    // TODO: We can actually run all these uploads and BVH builds in parallel
    // using multiple command lists, as long as the BVH builds don't need so
    // much build + scratch that we run out of GPU memory.
    // Some helpers for managing the temp upload heap buf allocation and queuing of
    // the commands would help to make it easier to write the parallel load version
    for (const auto &mesh : scene.meshes) {
        std::vector<dxr::Geometry> geometries;
        for (const auto &geom : mesh.geometries) {
            // Upload the mesh to the vertex buffer, build accel structures
            // Place the data in an upload heap first, then do a GPU-side copy
            // into a default heap (resident in VRAM)
            dxr::Buffer upload_verts =
                dxr::Buffer::upload(device.Get(),
                                    geom.vertices.size() * sizeof(glm::vec3),
                                    D3D12_RESOURCE_STATE_GENERIC_READ);
            dxr::Buffer upload_indices =
                dxr::Buffer::upload(device.Get(),
                                    geom.indices.size() * sizeof(glm::uvec3),
                                    D3D12_RESOURCE_STATE_GENERIC_READ);

            // Copy vertex and index data into the upload buffers
            std::memcpy(upload_verts.map(), geom.vertices.data(), upload_verts.size());
            std::memcpy(upload_indices.map(), geom.indices.data(), upload_indices.size());
            upload_verts.unmap();
            upload_indices.unmap();

            dxr::Buffer upload_uvs;
            if (!geom.uvs.empty()) {
                upload_uvs = dxr::Buffer::upload(device.Get(),
                                                 geom.uvs.size() * sizeof(glm::vec2),
                                                 D3D12_RESOURCE_STATE_GENERIC_READ);
                std::memcpy(upload_uvs.map(), geom.uvs.data(), upload_uvs.size());
                upload_uvs.unmap();
            }

            dxr::Buffer upload_normals;
            if (!geom.normals.empty()) {
                upload_normals = dxr::Buffer::upload(device.Get(),
                                                     geom.normals.size() * sizeof(glm::vec3),
                                                     D3D12_RESOURCE_STATE_GENERIC_READ);
                std::memcpy(upload_normals.map(), geom.normals.data(), upload_normals.size());
                upload_normals.unmap();
            }

            // Allocate GPU side buffers for the data so we can have it resident in VRAM
            dxr::Buffer vertex_buf = dxr::Buffer::device(
                device.Get(), upload_verts.size(), D3D12_RESOURCE_STATE_COPY_DEST);
            dxr::Buffer index_buf = dxr::Buffer::device(
                device.Get(), upload_indices.size(), D3D12_RESOURCE_STATE_COPY_DEST);

            CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));

            // Enqueue the copy into GPU memory
            cmd_list->CopyResource(vertex_buf.get(), upload_verts.get());
            cmd_list->CopyResource(index_buf.get(), upload_indices.get());

            dxr::Buffer uv_buf;
            if (!geom.uvs.empty()) {
                uv_buf = dxr::Buffer::device(
                    device.Get(), upload_uvs.size(), D3D12_RESOURCE_STATE_COPY_DEST);
                cmd_list->CopyResource(uv_buf.get(), upload_uvs.get());
            }

            dxr::Buffer normal_buf;
            if (!geom.normals.empty()) {
                normal_buf = dxr::Buffer::device(
                    device.Get(), upload_normals.size(), D3D12_RESOURCE_STATE_COPY_DEST);
                cmd_list->CopyResource(normal_buf.get(), upload_normals.get());
            }

            // Barriers to wait for the copies to finish before building the accel. structs
            {
                std::vector<D3D12_RESOURCE_BARRIER> b;
                b.push_back(barrier_transition(
                    vertex_buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
                b.push_back(barrier_transition(
                    index_buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
                if (!geom.uvs.empty()) {
                    b.push_back(barrier_transition(
                        uv_buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
                };
                if (!geom.normals.empty()) {
                    b.push_back(barrier_transition(
                        normal_buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
                };
                cmd_list->ResourceBarrier(b.size(), b.data());
            }

            geometries.emplace_back(vertex_buf, index_buf, normal_buf, uv_buf);

            // TODO: Some possible perf improvements: We can run all the upload of
            // index data in parallel, and the BVH building in parallel for all the
            // geometries. This should help for some large scenes, though with the assumption
            // that the entire build space for all the bottom level stuff can fit on the GPU.
            // For large scenes it would be best to monitor the available space needed for
            // the queued builds vs. the available GPU memory and then run stuff and compact
            // when we start getting full.
            CHECK_ERR(cmd_list->Close());
            ID3D12CommandList *cmd_lists = cmd_list.Get();
            cmd_queue->ExecuteCommandLists(1, &cmd_lists);
            sync_gpu();
        }

        meshes.emplace_back(geometries);

        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
        meshes.back().enqeue_build(device.Get(), cmd_list.Get());
        CHECK_ERR(cmd_list->Close());
        ID3D12CommandList *cmd_lists = cmd_list.Get();
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();

        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));

        meshes.back().enqueue_compaction(device.Get(), cmd_list.Get());
        CHECK_ERR(cmd_list->Close());
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();

        meshes.back().finalize();
    }

    parameterized_meshes = scene.parameterized_meshes;
    std::vector<uint32_t> parameterized_mesh_sbt_offsets;
    {
        // Compute the offsets each parameterized mesh will be written too in the SBT,
        // these are then the instance SBT offsets shared by each instance
        uint32_t offset = 0;
        for (const auto &pm : parameterized_meshes) {
            parameterized_mesh_sbt_offsets.push_back(offset);
            offset += meshes[pm.mesh_id].geometries.size();
        }
    }

    // TODO: May be best to move this into the top-level BVH build step,
    // and have it take the parameterized mesh info as well, similar to what
    // I have in the Metal backend
    auto upload_instance_buf = dxr::Buffer::upload(
        device.Get(),
        align_to(scene.instances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
                 D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT),
        D3D12_RESOURCE_STATE_GENERIC_READ);
    {
        // TODO: We want to keep some of the instance to BLAS mapping info for setting up the
        // hitgroups/sbt so the toplevel bvh can become something a bit higher-level to manage
        // this and filling out the instance buffers
        // Write the data about our instance
        D3D12_RAYTRACING_INSTANCE_DESC *buf =
            static_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(upload_instance_buf.map());

        for (size_t i = 0; i < scene.instances.size(); ++i) {
            const auto &inst = scene.instances[i];
            buf[i].InstanceID = i;
            buf[i].InstanceContributionToHitGroupIndex =
                parameterized_mesh_sbt_offsets[inst.parameterized_mesh_id];
            
            buf[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
            buf[i].AccelerationStructure =
                meshes[parameterized_meshes[inst.parameterized_mesh_id].mesh_id]
                    ->GetGPUVirtualAddress();
            buf[i].InstanceMask = 0xff;

            // Note: D3D matrices are row-major
            std::memset(buf[i].Transform, 0, sizeof(buf[i].Transform));
            const glm::mat4 m = glm::transpose(inst.transform);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 4; ++c) {
                    buf[i].Transform[r][c] = m[r][c];
                }
            }
        }
        upload_instance_buf.unmap();
    }

    // Copy instance data to the device heap
    instance_buf = dxr::Buffer::device(
        device.Get(),
        align_to(scene.instances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
                 D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT),
        D3D12_RESOURCE_STATE_COPY_DEST);
    {
        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));

        // Enqueue the copy into GPU memory
        cmd_list->CopyResource(instance_buf.get(), upload_instance_buf.get());

        auto b =
            barrier_transition(instance_buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmd_list->ResourceBarrier(1, &b);

        CHECK_ERR(cmd_list->Close());
        ID3D12CommandList *cmd_lists = cmd_list.Get();
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();
    }

    // Now build the top level acceleration structure on our instance
    scene_bvh = dxr::TopLevelBVH(instance_buf, scene.instances);

    CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
    scene_bvh.enqeue_build(device.Get(), cmd_list.Get());
    CHECK_ERR(cmd_list->Close());

    ID3D12CommandList *cmd_lists = cmd_list.Get();
    cmd_queue->ExecuteCommandLists(1, &cmd_lists);
    sync_gpu();

    scene_bvh.finalize();

    // Upload the textures
    for (const auto &t : scene.textures) {
        const DXGI_FORMAT format = t.color_space == SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                         : DXGI_FORMAT_R8G8B8A8_UNORM;

        dxr::Texture2D tex = dxr::Texture2D::device(device.Get(),
                                                    glm::uvec2(t.width, t.height),
                                                    D3D12_RESOURCE_STATE_COPY_DEST,
                                                    format);

        dxr::Buffer tex_upload = dxr::Buffer::upload(device.Get(),
                                                     tex.linear_row_pitch() * t.height,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ);

        // TODO: Some better texture upload handling here, and readback for handling the row
        // pitch stuff
        if (tex.linear_row_pitch() == t.width * tex.pixel_size()) {
            std::memcpy(tex_upload.map(), t.img.data(), tex_upload.size());
        } else {
            uint8_t *buf = static_cast<uint8_t *>(tex_upload.map());
            for (uint32_t y = 0; y < t.height; ++y) {
                std::memcpy(buf + y * tex.linear_row_pitch(),
                            t.img.data() + y * t.width * tex.pixel_size(),
                            t.width * tex.pixel_size());
            }
        }
        tex_upload.unmap();

        // TODO: We can upload these textures at once as well
        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));

        tex.upload(cmd_list.Get(), tex_upload);
        auto b = barrier_transition(tex, D3D12_RESOURCE_STATE_GENERIC_READ);
        cmd_list->ResourceBarrier(1, &b);

        CHECK_ERR(cmd_list->Close());
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();

        textures.push_back(tex);
    }

    // Upload the material data
    CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
    {
        dxr::Buffer mat_upload_buf =
            dxr::Buffer::upload(device.Get(),
                                scene.materials.size() * sizeof(DisneyMaterial),
                                D3D12_RESOURCE_STATE_GENERIC_READ);
        std::memcpy(mat_upload_buf.map(), scene.materials.data(), mat_upload_buf.size());
        mat_upload_buf.unmap();

        material_param_buf = dxr::Buffer::device(
            device.Get(), mat_upload_buf.size(), D3D12_RESOURCE_STATE_COPY_DEST);

        cmd_list->CopyResource(material_param_buf.get(), mat_upload_buf.get());
        auto b = barrier_transition(material_param_buf, D3D12_RESOURCE_STATE_GENERIC_READ);
        cmd_list->ResourceBarrier(1, &b);

        CHECK_ERR(cmd_list->Close());
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();
    }

    // Upload the light data
    CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
    {
        dxr::Buffer light_upload_buf =
            dxr::Buffer::upload(device.Get(),
                                scene.lights.size() * sizeof(QuadLight),
                                D3D12_RESOURCE_STATE_GENERIC_READ);
        std::memcpy(light_upload_buf.map(), scene.lights.data(), light_upload_buf.size());
        light_upload_buf.unmap();

        light_buf = dxr::Buffer::device(
            device.Get(), light_upload_buf.size(), D3D12_RESOURCE_STATE_COPY_DEST);

        cmd_list->CopyResource(light_buf.get(), light_upload_buf.get());
        auto b = barrier_transition(light_buf, D3D12_RESOURCE_STATE_GENERIC_READ);
        cmd_list->ResourceBarrier(1, &b);

        CHECK_ERR(cmd_list->Close());
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();
    }

    // ============================================================================
    // Create Global Buffers (for shader access)
    // ============================================================================

    // 1. Global Vertex Buffer (positions)
    if (!scene.global_vertices.empty()) {
        global_vertex_count = scene.global_vertices.size();
        
        dxr::Buffer upload_global_verts = dxr::Buffer::upload(
            device.Get(),
            scene.global_vertices.size() * sizeof(glm::vec3),
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        std::memcpy(upload_global_verts.map(), 
                    scene.global_vertices.data(), 
                    upload_global_verts.size());
        upload_global_verts.unmap();
        
        global_vertex_buffer = dxr::Buffer::device(
            device.Get(),
            upload_global_verts.size(),
            D3D12_RESOURCE_STATE_COPY_DEST
        );

        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
        cmd_list->CopyResource(global_vertex_buffer.get(), upload_global_verts.get());
        auto b = barrier_transition(global_vertex_buffer, 
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd_list->ResourceBarrier(1, &b);
        CHECK_ERR(cmd_list->Close());
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();
    }

    // 2. Global Index Buffer (uvec3 triangles)
    if (!scene.global_indices.empty()) {
        global_index_count = scene.global_indices.size();
        
        dxr::Buffer upload_global_indices = dxr::Buffer::upload(
            device.Get(),
            scene.global_indices.size() * sizeof(glm::uvec3),
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        std::memcpy(upload_global_indices.map(), 
                    scene.global_indices.data(), 
                    upload_global_indices.size());
        upload_global_indices.unmap();
        
        global_index_buffer = dxr::Buffer::device(
            device.Get(),
            upload_global_indices.size(),
            D3D12_RESOURCE_STATE_COPY_DEST
        );

        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
        cmd_list->CopyResource(global_index_buffer.get(), upload_global_indices.get());
        auto b = barrier_transition(global_index_buffer, 
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd_list->ResourceBarrier(1, &b);
        CHECK_ERR(cmd_list->Close());
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();
    }

    // 3. Global Normal Buffer (may be empty)
    if (!scene.global_normals.empty()) {
        global_normal_count = scene.global_normals.size();
        
        dxr::Buffer upload_global_normals = dxr::Buffer::upload(
            device.Get(),
            scene.global_normals.size() * sizeof(glm::vec3),
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        std::memcpy(upload_global_normals.map(), 
                    scene.global_normals.data(), 
                    upload_global_normals.size());
        upload_global_normals.unmap();
        
        global_normal_buffer = dxr::Buffer::device(
            device.Get(),
            upload_global_normals.size(),
            D3D12_RESOURCE_STATE_COPY_DEST
        );

        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
        cmd_list->CopyResource(global_normal_buffer.get(), upload_global_normals.get());
        auto b = barrier_transition(global_normal_buffer, 
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd_list->ResourceBarrier(1, &b);
        CHECK_ERR(cmd_list->Close());
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();
    }

    // 4. Global UV Buffer (may be empty)
    if (!scene.global_uvs.empty()) {
        global_uv_count = scene.global_uvs.size();
        
        dxr::Buffer upload_global_uvs = dxr::Buffer::upload(
            device.Get(),
            scene.global_uvs.size() * sizeof(glm::vec2),
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        std::memcpy(upload_global_uvs.map(), 
                    scene.global_uvs.data(), 
                    upload_global_uvs.size());
        upload_global_uvs.unmap();
        
        global_uv_buffer = dxr::Buffer::device(
            device.Get(),
            upload_global_uvs.size(),
            D3D12_RESOURCE_STATE_COPY_DEST
        );

        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
        cmd_list->CopyResource(global_uv_buffer.get(), upload_global_uvs.get());
        auto b = barrier_transition(global_uv_buffer, 
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd_list->ResourceBarrier(1, &b);
        CHECK_ERR(cmd_list->Close());
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();
    }

    // 5. MeshDesc Buffer
    if (!scene.mesh_descriptors.empty()) {
        mesh_desc_count = scene.mesh_descriptors.size();
        
        dxr::Buffer upload_mesh_descs = dxr::Buffer::upload(
            device.Get(),
            scene.mesh_descriptors.size() * sizeof(MeshDesc),
            D3D12_RESOURCE_STATE_GENERIC_READ
        );
        std::memcpy(upload_mesh_descs.map(), 
                    scene.mesh_descriptors.data(), 
                    upload_mesh_descs.size());
        upload_mesh_descs.unmap();
        
        mesh_desc_buffer = dxr::Buffer::device(
            device.Get(),
            upload_mesh_descs.size(),
            D3D12_RESOURCE_STATE_COPY_DEST
        );

        CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
        cmd_list->CopyResource(mesh_desc_buffer.get(), upload_mesh_descs.get());
        auto b = barrier_transition(mesh_desc_buffer, 
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd_list->ResourceBarrier(1, &b);
        CHECK_ERR(cmd_list->Close());
        cmd_queue->ExecuteCommandLists(1, &cmd_lists);
        sync_gpu();
    }

    // Load environment map if specified
    if (!scene.environment_map_path.empty()) {
        load_environment_map(scene.environment_map_path);
    }

    build_shader_resource_heap();
    build_raytracing_pipeline();
    build_shader_binding_table();
    build_descriptor_heap();
    record_command_lists();
}

void RenderDXR::load_environment_map(const std::string& path) {
    try {
        // Load from file using util function
        HDRImage img = ::load_environment_map(path);
        
        // Upload to GPU
        upload_environment_map(img);
        
        has_environment = true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to load environment map: " << e.what() << std::endl;
        has_environment = false;
    }
}

void RenderDXR::upload_environment_map(const HDRImage& img) {
    // Create GPU texture
    env_map_texture = dxr::Texture2D::device(
        device.Get(),
        glm::uvec2(img.width, img.height),
        D3D12_RESOURCE_STATE_COPY_DEST,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        D3D12_RESOURCE_FLAG_NONE
    );
    
    // Create upload buffer
    const size_t upload_size = env_map_texture.linear_row_pitch() * img.height;
    
    dxr::Buffer upload_buf = dxr::Buffer::upload(
        device.Get(),
        upload_size,
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    
    // Copy data to upload buffer (handle row pitch)
    uint8_t* mapped = static_cast<uint8_t*>(upload_buf.map());
    const size_t src_row_pitch = img.width * 4 * sizeof(float);
    const size_t dst_row_pitch = env_map_texture.linear_row_pitch();
    
    if (src_row_pitch == dst_row_pitch) {
        std::memcpy(mapped, img.data, img.width * img.height * 4 * sizeof(float));
    } else {
        // Copy row by row if pitch differs
        for (int y = 0; y < img.height; y++) {
            std::memcpy(
                mapped + y * dst_row_pitch,
                reinterpret_cast<uint8_t*>(img.data) + y * src_row_pitch,
                src_row_pitch
            );
        }
    }
    upload_buf.unmap();
    
    // Upload to GPU
    CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
    
    env_map_texture.upload(cmd_list.Get(), upload_buf);
    auto b = dxr::barrier_transition(env_map_texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd_list->ResourceBarrier(1, &b);
    
    CHECK_ERR(cmd_list->Close());
    ID3D12CommandList* lists[] = {cmd_list.Get()};
    cmd_queue->ExecuteCommandLists(1, lists);
    sync_gpu();
}

RenderStats RenderDXR::render(const glm::vec3 &pos,
                              const glm::vec3 &dir,
                              const glm::vec3 &up,
                              const float fovy,
                              const bool camera_changed,
                              const bool readback_framebuffer)
{
    using namespace std::chrono;
    RenderStats stats;

    // TODO: probably just pass frame_id directly
    if (camera_changed) {
        frame_id = 0;
    }

    update_view_parameters(pos, dir, up, fovy);

    auto start = high_resolution_clock::now();
    ID3D12CommandList *render_cmds = render_cmd_list.Get();
    cmd_queue->ExecuteCommandLists(1, &render_cmds);
    
    // Wait for ray tracing to complete before running OIDN
    const uint64_t render_signal_val = fence_value++;
    CHECK_ERR(cmd_queue->Signal(fence.Get(), render_signal_val));
    if (fence->GetCompletedValue() < render_signal_val) {
        CHECK_ERR(fence->SetEventOnCompletion(render_signal_val, fence_evt));
        WaitForSingleObject(fence_evt, INFINITE);
    }
    auto end = high_resolution_clock::now();
    stats.render_time = duration_cast<nanoseconds>(end - start).count() * 1.0e-6;

#ifdef ENABLE_OIDN
    // Denoise the accumulated frame
    oidn_filter.execute();
    
    // Execute tonemap compute shader to convert denoised HDR to sRGB display
    {
        ID3D12CommandList *tonemap_cmds = tonemap_cmd_list.Get();
        cmd_queue->ExecuteCommandLists(1, &tonemap_cmds);
    }
#endif

#ifdef REPORT_RAY_STATS
    const bool need_readback = true;
#else
    const bool need_readback = !native_display || readback_framebuffer;
#endif

    if (need_readback) {
        ID3D12CommandList *readback_cmds = readback_cmd_list.Get();
        cmd_queue->ExecuteCommandLists(1, &readback_cmds);
    }

    // Wait for the image readback commands to complete as well
    sync_gpu();

    // Read back the timestamps for DispatchRays to compute the true time spent rendering
    {
        D3D12_RANGE read_range;
        read_range.Begin = 0;
        read_range.End = query_resolve_buffer.size();
        const uint64_t *timestamps =
            static_cast<const uint64_t *>(query_resolve_buffer.map(read_range));
        uint64_t timestamp_freq = 0;
        cmd_queue->GetTimestampFrequency(&timestamp_freq);

        const uint64_t delta = timestamps[1] - timestamps[0];
        const double elapsed_time = static_cast<double>(delta) / timestamp_freq * 1e3;
        stats.render_time = elapsed_time;

        query_resolve_buffer.unmap();
    }

    if (need_readback) {
        // Map the readback buf and copy out the rendered image
        // We may have needed some padding for the readback buffer, so we might have to read
        // row by row.
        D3D12_RANGE read_range;
        read_range.Begin = 0;
        read_range.End = img_readback_buf.size();
        if (render_target.linear_row_pitch() ==
            render_target.dims().x * render_target.pixel_size()) {
            std::memcpy(img.data(), img_readback_buf.map(read_range), img_readback_buf.size());
        } else {
            uint8_t *buf = static_cast<uint8_t *>(img_readback_buf.map(read_range));
            for (uint32_t y = 0; y < render_target.dims().y; ++y) {
                std::memcpy(img.data() + y * render_target.dims().x,
                            buf + y * render_target.linear_row_pitch(),
                            render_target.dims().x * render_target.pixel_size());
            }
        }
        img_readback_buf.unmap();
    }

#ifdef REPORT_RAY_STATS
    if (ray_stats.linear_row_pitch() == ray_stats.dims().x * ray_stats.pixel_size()) {
        std::memcpy(
            ray_counts.data(), ray_stats_readback_buf.map(), ray_stats_readback_buf.size());
    } else {
        uint8_t *buf = static_cast<uint8_t *>(ray_stats_readback_buf.map());
        for (uint32_t y = 0; y < ray_stats.dims().y; ++y) {
            std::memcpy(ray_counts.data() + y * ray_stats.dims().x,
                        buf + y * ray_stats.linear_row_pitch(),
                        ray_stats.dims().x * ray_stats.pixel_size());
        }
    }
    ray_stats_readback_buf.unmap();

    const uint64_t total_rays =
        std::accumulate(ray_counts.begin(),
                        ray_counts.end(),
                        uint64_t(0),
                        [](const uint64_t &total, const uint16_t &c) { return total + c; });
    stats.rays_per_second = total_rays / (stats.render_time * 1.0e-3);
#endif

    ++frame_id;
    return stats;
}

void RenderDXR::create_device_objects()
{
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fence_evt = CreateEvent(nullptr, false, false, nullptr);

    // Create the command queue and command allocator
    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CHECK_ERR(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&cmd_queue)));
    CHECK_ERR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&cmd_allocator)));

    CHECK_ERR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&render_cmd_allocator)));

    // Make the command lists
    CHECK_ERR(device->CreateCommandList(0,
                                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        cmd_allocator.Get(),
                                        nullptr,
                                        IID_PPV_ARGS(&cmd_list)));
    CHECK_ERR(cmd_list->Close());

    CHECK_ERR(device->CreateCommandList(0,
                                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        cmd_allocator.Get(),
                                        nullptr,
                                        IID_PPV_ARGS(&render_cmd_list)));
    CHECK_ERR(render_cmd_list->Close());

    CHECK_ERR(device->CreateCommandList(0,
                                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        cmd_allocator.Get(),
                                        nullptr,
                                        IID_PPV_ARGS(&tonemap_cmd_list)));
    CHECK_ERR(tonemap_cmd_list->Close());

    CHECK_ERR(device->CreateCommandList(0,
                                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        cmd_allocator.Get(),
                                        nullptr,
                                        IID_PPV_ARGS(&readback_cmd_list)));
    CHECK_ERR(readback_cmd_list->Close());

    // Allocate a constants buffer for the view parameters.
    // These are write once, read once (assumed to change each frame).
    // The params will be:
    // vec4 cam_pos
    // vec4 cam_du
    // vec4 cam_dv
    // vec4 cam_dir_top_left
    // uint32_t frame_id
    // uint32_t samples_per_pixel
    view_param_buf = dxr::Buffer::upload(
        device.Get(),
        align_to(5 * sizeof(glm::vec4), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT),
        D3D12_RESOURCE_STATE_GENERIC_READ);

    // Our query heap will store two timestamps, the time that DispatchRays starts and the time
    // it ends
    D3D12_QUERY_HEAP_DESC timing_query_heap_desc = {};
    timing_query_heap_desc.Count = 2;
    timing_query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    device->CreateQueryHeap(&timing_query_heap_desc, IID_PPV_ARGS(&timing_query_heap));

    // Buffer to readback query results in to
    query_resolve_buffer = dxr::Buffer::readback(
        device.Get(), sizeof(uint64_t) * 2, D3D12_RESOURCE_STATE_COPY_DEST);
}

void RenderDXR::build_raytracing_pipeline()
{
#ifdef USE_SLANG_COMPILER
    // Load and compile Slang shader using relative path
    auto slang_source_opt = chameleonrt::SlangShaderCompiler::loadShaderSource("shaders/unified_render.slang");
    if (!slang_source_opt) {
        throw std::runtime_error("Failed to load shaders/unified_render.slang");
    }
    std::string slang_source = *slang_source_opt;
    
    // Compile Slang to DXIL library
    std::vector<std::string> defines;
#ifdef REPORT_RAY_STATS
    defines.push_back("REPORT_RAY_STATS");
#endif
    
    auto result = slangCompiler.compileSlangToDXILLibrary(slang_source, {"shaders"}, defines);
    
    if (!result) {
        std::string error = slangCompiler.getLastError();
        std::cerr << "[Slang] Compilation failed: " << error << std::endl;
        throw std::runtime_error("Slang shader compilation failed");
    }
    
    // Create separate D3D12 shader libraries for each entry point
    // D3D12 RT pipeline supports multiple DXIL libraries
    std::vector<dxr::ShaderLibrary> shader_libraries;
    
    for (const auto& blob : *result) {
        std::wstring export_name(blob.entryPoint.begin(), blob.entryPoint.end());
        shader_libraries.emplace_back(
            blob.bytecode.data(),
            blob.bytecode.size(),
            std::vector<std::wstring>{export_name}
        );
    }
#else
    // Original embedded DXIL path
    std::vector<dxr::ShaderLibrary> shader_libraries;
    shader_libraries.emplace_back(
        render_dxr_dxil,
        sizeof(render_dxr_dxil),
        std::vector<std::wstring>{L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}
    );
#endif

    // Move descriptor heaps to global root signature so ClosestHit can access them
    dxr::RootSignature global_root_sig =
        dxr::RootSignatureBuilder::global()
            .add_desc_heap("cbv_srv_uav_heap", raygen_desc_heap)
            .add_desc_heap("sampler_heap", raygen_sampler_heap)
            .create(device.Get());

    // Create the root signature for our ray gen shader (only has local constants)
    dxr::RootSignature raygen_root_sig =
        dxr::RootSignatureBuilder::local()
            .add_constants("SceneParams", 1, 1, 0)
            .create(device.Get());

    // Create the local root signature for ClosestHit
    // Uses space0 (not space1) for Slang compatibility
    // Uses b2 to avoid conflict with ViewParams (b0) and SceneParams (b1)
    dxr::RootSignature hitgroup_root_sig = dxr::RootSignatureBuilder::local()
                                                      .add_constants("HitGroupData", 2, 1, 0)  // b2, space0
                                                      .create(device.Get());

    // Build RT pipeline - add all shader libraries
    dxr::RTPipelineBuilder rt_pipeline_builder =
        dxr::RTPipelineBuilder()
            .set_global_root_sig(global_root_sig);
    
    // Add all shader libraries to the pipeline
    // D3D12 RT pipeline supports multiple DXIL libraries (see dxr_utils.cpp:495)
    for (auto& lib : shader_libraries) {
        rt_pipeline_builder.add_shader_library(lib);
    }
    
    // Collect all export names for shader payload configuration
    std::vector<std::wstring> all_exports;
    for (const auto& lib : shader_libraries) {
        for (const auto& name : lib.export_names()) {
            all_exports.push_back(name);
        }
    }
    
    rt_pipeline_builder
            .set_ray_gen(L"RayGen")
            .add_miss_shader(L"Miss")
            .add_miss_shader(L"ShadowMiss")
            .set_shader_root_sig({L"RayGen"}, raygen_root_sig)
            .configure_shader_payload(
                all_exports, 8 * sizeof(float), 2 * sizeof(float))
            .set_max_recursion(1);

    // Setup hit groups and shader root signatures for our instances
    std::vector<std::wstring> hg_names;
    size_t hit_group_index = 0;
    for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
        const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < meshes[pm.mesh_id].geometries.size(); ++j) {
            const std::wstring hg_name =
                L"HitGroup_param_mesh" + std::to_wstring(i) + L"_geom" + std::to_wstring(j);
            hg_names.push_back(hg_name);
            
            hit_group_index++;

            rt_pipeline_builder.add_hit_group(
                {dxr::HitGroup(hg_name, D3D12_HIT_GROUP_TYPE_TRIANGLES, L"ClosestHit")});
        }
    }
    // Assign local root signature to all hit groups
    rt_pipeline_builder.set_shader_root_sig(hg_names, hitgroup_root_sig);

    rt_pipeline = rt_pipeline_builder.create(device.Get());

    // Create tonemap compute shader pipeline
    tonemap_root_sig =
        dxr::RootSignatureBuilder::global()
            .add_desc_heap("cbv_srv_uav_heap", raygen_desc_heap)
            .create(device.Get());

    // Compile tonemap compute shader from Slang
    auto tonemap_source_opt = chameleonrt::SlangShaderCompiler::loadShaderSource("shaders/tonemap.slang");
    if (!tonemap_source_opt) {
        throw std::runtime_error("Failed to load shaders/tonemap.slang");
    }
    
    std::vector<std::string> tonemap_defines;
#ifdef ENABLE_OIDN
    tonemap_defines.push_back("ENABLE_OIDN");
#endif
    
    auto tonemap_blob = slangCompiler.compileSlangToComputeDXIL(
        *tonemap_source_opt,
        "main",
        {"shaders"},
        tonemap_defines);
    
    if (!tonemap_blob) {
        std::string error = slangCompiler.getLastError();
        throw std::runtime_error("Tonemap shader compilation failed: " + error);
    }
    
    D3D12_COMPUTE_PIPELINE_STATE_DESC tonemap_pso = {};
    tonemap_pso.pRootSignature = tonemap_root_sig.get();
    tonemap_pso.CS.pShaderBytecode = tonemap_blob->bytecode.data();
    tonemap_pso.CS.BytecodeLength = tonemap_blob->bytecode.size();
    tonemap_pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    CHECK_ERR(device->CreateComputePipelineState(&tonemap_pso, IID_PPV_ARGS(&tonemap_ps)));
}

void RenderDXR::build_shader_resource_heap()
{
    // The CBV/SRV/UAV resource heap has the pointers/views things to our output image buffer
    // and the top level acceleration structure, and any textures
    
    raygen_desc_heap = dxr::DescriptorHeapBuilder()
#if defined(REPORT_RAY_STATS) && defined(ENABLE_OIDN)
                           .add_uav_range(4, 0, 0)  // render_target + accum_buffer + denoise_buffer + ray_stats
#elif defined(REPORT_RAY_STATS)
                           .add_uav_range(3, 0, 0)  // render_target + accum_buffer + ray_stats
#elif defined(ENABLE_OIDN)
                           .add_uav_range(3, 0, 0)  // render_target + accum_buffer + denoise_buffer
#else
                           .add_uav_range(2, 0, 0)  // render_target + accum_buffer
#endif
                           .add_srv_range(3, 0, 0)
                           .add_cbv_range(1, 0, 0)
                           .add_srv_range(!textures.empty() ? textures.size() : 1, 30, 0)
                           .add_srv_range(5, 10, 0)  // t10-t14 (global buffers)
                           .add_srv_range(1, 15, 0)  // t15 (environment map)
                           .create(device.Get());

    raygen_sampler_heap =
        dxr::DescriptorHeapBuilder().add_sampler_range(1, 0, 0).create(device.Get());
}

void RenderDXR::build_shader_binding_table()
{
    rt_pipeline.map_shader_table();
    {
        uint8_t *map = rt_pipeline.shader_record(L"RayGen");
        const dxr::RootSignature *sig = rt_pipeline.shader_signature(L"RayGen");

        const uint32_t num_lights = light_buf.size() / sizeof(QuadLight);
        std::memcpy(map + sig->offset("SceneParams"), &num_lights, sizeof(uint32_t));
    }
    
    // Write meshDescIndex to shader records for ClosestHit
    size_t mesh_desc_index = 0;
    for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
        const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < meshes[pm.mesh_id].geometries.size(); ++j) {
            const std::wstring hg_name =
                L"HitGroup_param_mesh" + std::to_wstring(i) + L"_geom" + std::to_wstring(j);

            uint8_t *map = rt_pipeline.shader_record(hg_name);
            const dxr::RootSignature *sig = rt_pipeline.shader_signature(hg_name);

            const uint32_t mesh_desc_idx = static_cast<uint32_t>(mesh_desc_index);
            
            std::memcpy(map + sig->offset("HitGroupData"),
                        &mesh_desc_idx,
                        sizeof(uint32_t));
            
            mesh_desc_index++;
        }
    }
    rt_pipeline.unmap_shader_table();

    CHECK_ERR(cmd_list->Reset(cmd_allocator.Get(), nullptr));
    rt_pipeline.upload_shader_table(cmd_list.Get());
    std::array<ID3D12CommandList *, 1> cmd_lists = {cmd_list.Get()};
    CHECK_ERR(cmd_list->Close());
    cmd_queue->ExecuteCommandLists(cmd_lists.size(), cmd_lists.data());
    sync_gpu();
}

void RenderDXR::update_view_parameters(const glm::vec3 &pos,
                                       const glm::vec3 &dir,
                                       const glm::vec3 &up,
                                       const float fovy)
{
    glm::vec2 img_plane_size;
    img_plane_size.y = 2.f * std::tan(glm::radians(0.5f * fovy));
    img_plane_size.x =
        img_plane_size.y * static_cast<float>(render_target.dims().x) / render_target.dims().y;

    const glm::vec3 dir_du = glm::normalize(glm::cross(dir, up)) * img_plane_size.x;
    const glm::vec3 dir_dv = -glm::normalize(glm::cross(dir_du, dir)) * img_plane_size.y;
    const glm::vec3 dir_top_left = dir - 0.5f * dir_du - 0.5f * dir_dv;

    uint8_t *buf = static_cast<uint8_t *>(view_param_buf.map());
    {
        glm::vec4 *vecs = reinterpret_cast<glm::vec4 *>(buf);
        vecs[0] = glm::vec4(pos, 0.f);
        vecs[1] = glm::vec4(dir_du, 0.f);
        vecs[2] = glm::vec4(dir_dv, 0.f);
        vecs[3] = glm::vec4(dir_top_left, 0.f);
    }
    {
        uint32_t *fid = reinterpret_cast<uint32_t *>(buf + 4 * sizeof(glm::vec4));
        fid[0] = frame_id;
        fid[1] = samples_per_pixel;
    }

    view_param_buf.unmap();
}

void RenderDXR::build_descriptor_heap()
{
    D3D12_CPU_DESCRIPTOR_HANDLE heap_handle = raygen_desc_heap.cpu_desc_handle();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {0};

    // Render target
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(render_target.get(), nullptr, &uav_desc, heap_handle);
    heap_handle.ptr +=
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Accum buffer (structured buffer UAV) - AccumPixel struct: color + albedo + normal
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC buffer_uav_desc{};
        buffer_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        buffer_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        buffer_uav_desc.Buffer.FirstElement = 0;
        buffer_uav_desc.Buffer.StructureByteStride = 3 * sizeof(glm::vec4);  // AccumPixel = 3 x float4
        buffer_uav_desc.Buffer.NumElements = accum_buffer.size() / (3 * sizeof(glm::vec4));
        buffer_uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(accum_buffer.get(), nullptr, &buffer_uav_desc, heap_handle);
        heap_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

#ifdef REPORT_RAY_STATS
    // Ray stats buffer
    device->CreateUnorderedAccessView(ray_stats.get(), nullptr, &uav_desc, heap_handle);
    heap_handle.ptr +=
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
#endif

#ifdef ENABLE_OIDN
    // Denoise buffer UAV
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC denoise_uav_desc{};
        denoise_uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        denoise_uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        denoise_uav_desc.Buffer.FirstElement = 0;
        denoise_uav_desc.Buffer.StructureByteStride = sizeof(glm::vec4);
        denoise_uav_desc.Buffer.NumElements = denoise_buffer.size() / sizeof(glm::vec4);
        denoise_uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(denoise_buffer.get(), nullptr, &denoise_uav_desc, heap_handle);
        heap_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
#endif

    // Write the TLAS after the output image in the heap
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC tlas_desc = {0};
        tlas_desc.Format = DXGI_FORMAT_UNKNOWN;
        tlas_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        tlas_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        tlas_desc.RaytracingAccelerationStructure.Location = scene_bvh->GetGPUVirtualAddress();
        device->CreateShaderResourceView(nullptr, &tlas_desc, heap_handle);
        heap_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Write the material params buffer view
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {0};
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = material_param_buf.size() / sizeof(DisneyMaterial);
        srv_desc.Buffer.StructureByteStride = sizeof(DisneyMaterial);
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(material_param_buf.get(), &srv_desc, heap_handle);
        heap_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Write the light params buffer view
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {0};
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = light_buf.size() / sizeof(QuadLight);
        srv_desc.Buffer.StructureByteStride = sizeof(QuadLight);
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(light_buf.get(), &srv_desc, heap_handle);
        heap_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Write the view params constants buffer
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {0};
    cbv_desc.BufferLocation = view_param_buf->GetGPUVirtualAddress();
    cbv_desc.SizeInBytes = view_param_buf.size();
    device->CreateConstantBufferView(&cbv_desc, heap_handle);
    heap_handle.ptr +=
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Write the SRVs for the textures
    // CRITICAL: When textures.empty(), create a null descriptor for t30 to maintain
    // correct heap offsets. The root signature reserves a slot for t30 even with no
    // textures, so we must create a descriptor (null or real) to keep t10-t14 aligned.
    if (textures.empty()) {
        D3D12_SHADER_RESOURCE_VIEW_DESC null_desc = {0};
        null_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        null_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_desc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &null_desc, heap_handle);
        heap_handle.ptr +=
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    } else {
        for (auto &t : textures) {
            D3D12_SHADER_RESOURCE_VIEW_DESC tex_desc = {0};
            tex_desc.Format = t.pixel_format();
            tex_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            tex_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            tex_desc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(t.get(), &tex_desc, heap_handle);
            heap_handle.ptr +=
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    // Create SRVs for global buffers at t10-t14
    // CRITICAL: Always create descriptors (null if buffer empty) to maintain heap layout
    const UINT descriptor_increment = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // ===== t10: globalVertices SRV =====
    if (global_vertex_count > 0) {
        D3D12_SHADER_RESOURCE_VIEW_DESC vertex_srv_desc = {};
        vertex_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        vertex_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        vertex_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        vertex_srv_desc.Buffer.FirstElement = 0;
        vertex_srv_desc.Buffer.NumElements = static_cast<UINT>(global_vertex_count);
        vertex_srv_desc.Buffer.StructureByteStride = sizeof(glm::vec3);  // 12 bytes
        vertex_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        
        device->CreateShaderResourceView(
            global_vertex_buffer.get(),
            &vertex_srv_desc,
            heap_handle
        );
    } else {
        // Null descriptor with explicit structure
        D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc = {};
        null_srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        null_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_srv_desc.Buffer.FirstElement = 0;
        null_srv_desc.Buffer.NumElements = 0;
        null_srv_desc.Buffer.StructureByteStride = 0;
        null_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        device->CreateShaderResourceView(nullptr, &null_srv_desc, heap_handle);
    }
    heap_handle.ptr += descriptor_increment;

    // ===== t11: globalIndices SRV =====
    if (global_index_count > 0) {
        D3D12_SHADER_RESOURCE_VIEW_DESC index_srv_desc = {};
        index_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        index_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        index_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        index_srv_desc.Buffer.FirstElement = 0;
        index_srv_desc.Buffer.NumElements = static_cast<UINT>(global_index_count);
        index_srv_desc.Buffer.StructureByteStride = sizeof(glm::uvec3);  // 12 bytes
        index_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        
        device->CreateShaderResourceView(
            global_index_buffer.get(),
            &index_srv_desc,
            heap_handle
        );
    } else {
        // Null descriptor with explicit structure
        D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc = {};
        null_srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        null_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_srv_desc.Buffer.FirstElement = 0;
        null_srv_desc.Buffer.NumElements = 0;
        null_srv_desc.Buffer.StructureByteStride = 0;
        null_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        device->CreateShaderResourceView(nullptr, &null_srv_desc, heap_handle);
    }
    heap_handle.ptr += descriptor_increment;

    // ===== t12: globalNormals SRV =====
    if (global_normal_count > 0) {
        D3D12_SHADER_RESOURCE_VIEW_DESC normal_srv_desc = {};
        normal_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        normal_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        normal_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        normal_srv_desc.Buffer.FirstElement = 0;
        normal_srv_desc.Buffer.NumElements = static_cast<UINT>(global_normal_count);
        normal_srv_desc.Buffer.StructureByteStride = sizeof(glm::vec3);  // 12 bytes
        normal_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        
        device->CreateShaderResourceView(
            global_normal_buffer.get(),
            &normal_srv_desc,
            heap_handle
        );
    } else {
        // Null descriptor with explicit structure
        D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc = {};
        null_srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        null_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_srv_desc.Buffer.FirstElement = 0;
        null_srv_desc.Buffer.NumElements = 0;
        null_srv_desc.Buffer.StructureByteStride = 0;
        null_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        device->CreateShaderResourceView(nullptr, &null_srv_desc, heap_handle);
    }
    heap_handle.ptr += descriptor_increment;

    // ===== t13: globalUVs SRV =====
    if (global_uv_count > 0) {
        D3D12_SHADER_RESOURCE_VIEW_DESC uv_srv_desc = {};
        uv_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        uv_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        uv_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        uv_srv_desc.Buffer.FirstElement = 0;
        uv_srv_desc.Buffer.NumElements = static_cast<UINT>(global_uv_count);
        uv_srv_desc.Buffer.StructureByteStride = sizeof(glm::vec2);  // 8 bytes
        uv_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        
        device->CreateShaderResourceView(
            global_uv_buffer.get(),
            &uv_srv_desc,
            heap_handle
        );
    } else {
        // Null descriptor with explicit structure
        D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc = {};
        null_srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        null_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_srv_desc.Buffer.FirstElement = 0;
        null_srv_desc.Buffer.NumElements = 0;
        null_srv_desc.Buffer.StructureByteStride = 0;
        null_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        device->CreateShaderResourceView(nullptr, &null_srv_desc, heap_handle);
    }
    heap_handle.ptr += descriptor_increment;

    // ===== t14: meshDescs SRV =====
    if (mesh_desc_count > 0) {
        D3D12_SHADER_RESOURCE_VIEW_DESC mesh_srv_desc = {};
        mesh_srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        mesh_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        mesh_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        mesh_srv_desc.Buffer.FirstElement = 0;
        mesh_srv_desc.Buffer.NumElements = static_cast<UINT>(mesh_desc_count);
        mesh_srv_desc.Buffer.StructureByteStride = sizeof(MeshDesc);  // 32 bytes
        mesh_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        
        device->CreateShaderResourceView(
            mesh_desc_buffer.get(),
            &mesh_srv_desc,
            heap_handle
        );
    } else {
        // Null descriptor with explicit structure
        D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc = {};
        null_srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        null_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        null_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_srv_desc.Buffer.FirstElement = 0;
        null_srv_desc.Buffer.NumElements = 0;
        null_srv_desc.Buffer.StructureByteStride = 0;
        null_srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        device->CreateShaderResourceView(nullptr, &null_srv_desc, heap_handle);
    }
    heap_handle.ptr += descriptor_increment;

    // ===== t15: environment map SRV =====
    if (has_environment) {
        D3D12_SHADER_RESOURCE_VIEW_DESC env_srv_desc = {};
        env_srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        env_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        env_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        env_srv_desc.Texture2D.MipLevels = 1;
        env_srv_desc.Texture2D.MostDetailedMip = 0;
        
        device->CreateShaderResourceView(
            env_map_texture.get(),
            &env_srv_desc,
            heap_handle
        );
    } else {
        // Create null descriptor to maintain heap layout
        D3D12_SHADER_RESOURCE_VIEW_DESC null_desc = {};
        null_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        null_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_desc.Texture2D.MipLevels = 1;
        
        device->CreateShaderResourceView(nullptr, &null_desc, heap_handle);
    }
    heap_handle.ptr += descriptor_increment;

    // Write the sampler to the sampler heap
    D3D12_SAMPLER_DESC sampler_desc = {0};
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = 0;
    sampler_desc.MipLODBias = 0.0f;
    sampler_desc.MaxAnisotropy = 1;
    device->CreateSampler(&sampler_desc, raygen_sampler_heap.cpu_desc_handle());
}

void RenderDXR::record_command_lists()
{
    CHECK_ERR(render_cmd_allocator->Reset());
    CHECK_ERR(render_cmd_list->Reset(render_cmd_allocator.Get(), nullptr));

    // TODO: We'll need a second desc. heap for the sampler and bind both of them here
    std::array<ID3D12DescriptorHeap *, 2> desc_heaps = {raygen_desc_heap.get(),
                                                        raygen_sampler_heap.get()};
    render_cmd_list->SetDescriptorHeaps(desc_heaps.size(), desc_heaps.data());
    render_cmd_list->SetPipelineState1(rt_pipeline.get());
    render_cmd_list->SetComputeRootSignature(rt_pipeline.global_sig());
    
    // Bind descriptor heaps to global root signature
    // Parameter 0: cbv_srv_uav_heap
    // Parameter 1: sampler_heap
    render_cmd_list->SetComputeRootDescriptorTable(
        0, raygen_desc_heap->GetGPUDescriptorHandleForHeapStart());
    render_cmd_list->SetComputeRootDescriptorTable(
        1, raygen_sampler_heap->GetGPUDescriptorHandleForHeapStart());

    render_cmd_list->EndQuery(timing_query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);

    D3D12_DISPATCH_RAYS_DESC dispatch_rays = rt_pipeline.dispatch_rays(render_target.dims());
    render_cmd_list->DispatchRays(&dispatch_rays);

    render_cmd_list->EndQuery(timing_query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);

    render_cmd_list->ResolveQueryData(timing_query_heap.Get(),
                                      D3D12_QUERY_TYPE_TIMESTAMP,
                                      0,
                                      2,
                                      query_resolve_buffer.get(),
                                      0);

    CHECK_ERR(render_cmd_list->Close());

    // Tonemap compute shader command list
    CHECK_ERR(tonemap_cmd_list->Reset(render_cmd_allocator.Get(), nullptr));
    tonemap_cmd_list->SetDescriptorHeaps(desc_heaps.size(), desc_heaps.data());
    tonemap_cmd_list->SetPipelineState(tonemap_ps.Get());
    tonemap_cmd_list->SetComputeRootSignature(tonemap_root_sig.get());
    tonemap_cmd_list->SetComputeRootDescriptorTable(0, raygen_desc_heap.gpu_desc_handle());

    glm::uvec2 dispatch_dim = render_target.dims();
    glm::uvec2 workgroup_dim(16, 16);
    dispatch_dim = (dispatch_dim + workgroup_dim - glm::uvec2(1)) / workgroup_dim;
    tonemap_cmd_list->Dispatch(dispatch_dim.x, dispatch_dim.y, 1);

    CHECK_ERR(tonemap_cmd_list->Close());

    // Now copy the rendered image into our readback heap so we can give it back
    // to our simple window to blit the image (TODO: Maybe in the future keep this on the GPU?
    // would we be able to share with GL or need a separate DX window backend?)
    CHECK_ERR(readback_cmd_list->Reset(render_cmd_allocator.Get(), nullptr));
    {
        // Render target from UA -> Copy Source
        auto b = barrier_transition(render_target, D3D12_RESOURCE_STATE_COPY_SOURCE);
        readback_cmd_list->ResourceBarrier(1, &b);
#ifdef REPORT_RAY_STATS
        b = barrier_transition(ray_stats, D3D12_RESOURCE_STATE_COPY_SOURCE);
        readback_cmd_list->ResourceBarrier(1, &b);
#endif

        render_target.readback(readback_cmd_list.Get(), img_readback_buf);
#ifdef REPORT_RAY_STATS
        ray_stats.readback(readback_cmd_list.Get(), ray_stats_readback_buf);
#endif

        // Transition the render target back to UA so we can write to it in the next frame
        b = barrier_transition(render_target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        readback_cmd_list->ResourceBarrier(1, &b);
#ifdef REPORT_RAY_STATS
        b = barrier_transition(ray_stats, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        readback_cmd_list->ResourceBarrier(1, &b);
#endif
    }
    CHECK_ERR(readback_cmd_list->Close());
}

void RenderDXR::sync_gpu()
{
    const uint64_t signal_val = fence_value++;
    CHECK_ERR(cmd_queue->Signal(fence.Get(), signal_val));

    if (fence->GetCompletedValue() < signal_val) {
        CHECK_ERR(fence->SetEventOnCompletion(signal_val, fence_evt));
        WaitForSingleObject(fence_evt, INFINITE);
    }
}
