#include "render_vulkan.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <numeric>
#include <string>
#ifndef USE_SLANG_COMPILER
#include "spv_shaders_embedded_spv.h"
#endif
#ifdef USE_SLANG_COMPILER
#include "../../util/slang_shader_compiler.h"
#include <fstream>
#include <filesystem>
#endif
#include "util.h"
#include "mesh.h"  // For MeshDesc structure
#include <glm/ext.hpp>

RenderVulkan::RenderVulkan(std::shared_ptr<vkrt::Device> dev)
    : device(dev), native_display(true)
{
    command_pool = device->make_command_pool(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    {
        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = command_pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        CHECK_VULKAN(
            vkAllocateCommandBuffers(device->logical_device(), &info, &command_buffer));
    }

    render_cmd_pool = device->make_command_pool();
    {
        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = render_cmd_pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        CHECK_VULKAN(
            vkAllocateCommandBuffers(device->logical_device(), &info, &render_cmd_buf));
        CHECK_VULKAN(
            vkAllocateCommandBuffers(device->logical_device(), &info, &readback_cmd_buf));
    }

    {
        VkFenceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        CHECK_VULKAN(vkCreateFence(device->logical_device(), &info, nullptr, &fence));
    }

    view_param_buf = vkrt::Buffer::host(*device,
                                        4 * sizeof(glm::vec4) + 2 * sizeof(uint32_t),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkQueryPoolCreateInfo pool_ci = {};
    pool_ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    pool_ci.queryCount = 2;
    CHECK_VULKAN(
        vkCreateQueryPool(device->logical_device(), &pool_ci, nullptr, &timing_query_pool));

#ifdef USE_SLANG_COMPILER
    // Initialize Slang shader compiler
    if (!slangCompiler.isValid()) {
        throw std::runtime_error("Failed to initialize Slang shader compiler");
    }
#endif
}

RenderVulkan::RenderVulkan() : RenderVulkan(std::make_shared<vkrt::Device>())
{
    native_display = false;
}

RenderVulkan::~RenderVulkan()
{
    vkDestroyQueryPool(device->logical_device(), timing_query_pool, nullptr);
    vkDestroySampler(device->logical_device(), sampler, nullptr);
    if (env_map_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device->logical_device(), env_map_sampler, nullptr);
    }
    vkDestroyCommandPool(device->logical_device(), command_pool, nullptr);
    vkDestroyCommandPool(device->logical_device(), render_cmd_pool, nullptr);
    vkDestroyPipelineLayout(device->logical_device(), pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(device->logical_device(), desc_layout, nullptr);
    // DESCRIPTOR FLATTENING: No longer using separate textures_desc_layout
    // vkDestroyDescriptorSetLayout(device->logical_device(), textures_desc_layout, nullptr);
    vkDestroyDescriptorPool(device->logical_device(), desc_pool, nullptr);
    vkDestroyFence(device->logical_device(), fence, nullptr);
    vkDestroyPipeline(device->logical_device(), rt_pipeline.handle(), nullptr);
#ifdef ENABLE_OIDN
    if (tonemap_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device->logical_device(), tonemap_pipeline, nullptr);
    }
    if (tonemap_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device->logical_device(), tonemap_pipeline_layout, nullptr);
    }
    if (tonemap_desc_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device->logical_device(), tonemap_desc_layout, nullptr);
    }
    if (tonemap_desc_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device->logical_device(), tonemap_desc_pool, nullptr);
    }
#endif
}

std::string RenderVulkan::name()
{
    return "Vulkan Ray Tracing";
}

void RenderVulkan::initialize(const int fb_width, const int fb_height)
{
#ifdef ENABLE_OIDN
    // Query the UUID of the Vulkan physical device
    VkPhysicalDeviceIDProperties id_properties{};
    id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &id_properties;
    vkGetPhysicalDeviceProperties2(device->physical_device(), &properties);

    oidn::UUID uuid;
    std::memcpy(uuid.bytes, id_properties.deviceUUID, sizeof(uuid.bytes));

    // Initialize the denoiser device
    oidn_device = oidn::newDevice(uuid);
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to create OIDN device.");
    oidn_device.commit();
    if (oidn_device.getError() != oidn::Error::None)
        throw std::runtime_error("Failed to commit OIDN device.");

    // Find a compatible external memory handle type
    const auto oidn_external_mem_types = oidn_device.get<oidn::ExternalMemoryTypeFlags>("externalMemoryTypes");
    oidn::ExternalMemoryTypeFlag oidn_external_mem_type;
    VkExternalMemoryHandleTypeFlagBits external_mem_type;

#ifdef _WIN32
    if (oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::OpaqueWin32) {
        oidn_external_mem_type = oidn::ExternalMemoryTypeFlag::OpaqueWin32;
        external_mem_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    } else {
        throw std::runtime_error("Failed to find compatible external memory type");
    }
#else
    if (oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::OpaqueFD) {
        oidn_external_mem_type = oidn::ExternalMemoryTypeFlag::OpaqueFD;
        external_mem_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    } else if (oidn_external_mem_types & oidn::ExternalMemoryTypeFlag::DMABuf) {
        oidn_external_mem_type = oidn::ExternalMemoryTypeFlag::DMABuf;
        external_mem_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    } else {
        throw std::runtime_error("Failed to find compatible external memory type");
    }
#endif
#endif

    frame_id = 0;
    img.resize(fb_width * fb_height);

    render_target =
        vkrt::Texture2D::device(*device,
                                glm::uvec2(fb_width, fb_height),
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

#ifdef ENABLE_OIDN
    // Allocate 3x size for AccumPixel struct with external memory for OIDN sharing
    accum_buffer = vkrt::Buffer::device(*device,
                                        3 * sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        (VkMemoryPropertyFlagBits)0,
                                        external_mem_type);

    denoise_buffer = vkrt::Buffer::device(*device,
                                          sizeof(glm::vec4) * fb_width * fb_height,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                          (VkMemoryPropertyFlagBits)0,
                                          external_mem_type);
#else
    // Allocate 3x size for AccumPixel struct: color + albedo + normal
    accum_buffer = vkrt::Buffer::device(*device,
                                        3 * sizeof(glm::vec4) * fb_width * fb_height,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
#endif

    img_readback_buf = vkrt::Buffer::host(
        *device, img.size() * render_target->pixel_size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT);

#ifdef REPORT_RAY_STATS
    ray_stats =
        vkrt::Texture2D::device(*device,
                                glm::uvec2(fb_width, fb_height),
                                VK_FORMAT_R16_UINT,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    ray_stats_readback_buf = vkrt::Buffer::host(
        *device, img.size() * ray_stats->pixel_size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    ray_counts.resize(img.size(), 0);
#endif

    // Change image and accum buffer to the general layout
    {
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

#ifndef REPORT_RAY_STATS
        std::array<VkImageMemoryBarrier, 1> barriers = {};
#else
        std::array<VkImageMemoryBarrier, 2> barriers = {};
#endif
        for (auto &b : barriers) {
            b = VkImageMemoryBarrier{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.baseMipLevel = 0;
            b.subresourceRange.levelCount = 1;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount = 1;
        }
        barriers[0].image = render_target->image_handle();
#ifdef REPORT_RAY_STATS
        barriers[1].image = ray_stats->image_handle();
#endif

        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             barriers.size(),
                             barriers.data());

        CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

        // Submit the copy to run
        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
        CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

        // We didn't make the buffers individually reset-able, but we're just using it as temp
        // one to do this upload so clear the pool to reset
        vkResetCommandPool(device->logical_device(),
                           command_pool,
                           VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    }

    // If we've loaded the scene and are rendering (i.e., the window was just resized while
    // running) update the descriptor sets and re-record the rendering commands
    if (desc_set != VK_NULL_HANDLE) {
        vkrt::DescriptorSetUpdater()
            .write_storage_image(desc_set, 1, render_target)
            .write_ssbo(desc_set, 2, accum_buffer)
#ifdef REPORT_RAY_STATS
            .write_storage_image(desc_set, 6, ray_stats)
#endif
            .update(*device);

        record_command_buffers();
    }

#ifdef ENABLE_OIDN
    {
        // Initialize the denoiser filter
        oidn_filter = oidn_device.newFilter("RT");
        if (oidn_device.getError() != oidn::Error::None)
            throw std::runtime_error("Failed to create OIDN filter.");

        auto input_buffer = oidn_device.newBuffer(oidn_external_mem_type,
                                                  accum_buffer->external_mem_handle(external_mem_type),
#ifdef _WIN32
                                                  nullptr,
#endif
                                                  accum_buffer->size());

        auto output_buffer = oidn_device.newBuffer(oidn_external_mem_type,
                                                   denoise_buffer->external_mem_handle(external_mem_type),
#ifdef _WIN32
                                                   nullptr,
#endif
                                                   denoise_buffer->size());

        // Configure filter inputs from AccumPixel struct layout:
        // struct AccumPixel { float4 color; float4 albedo; float4 normal; }
        // Stride between pixels = 3 * sizeof(glm::vec4) = 48 bytes
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

void RenderVulkan::set_scene(const Scene &scene)
{
    frame_id = 0;
    samples_per_pixel = scene.samples_per_pixel;

    // TODO: We can actually run all these uploads and BVH builds in parallel
    // using multiple command lists, as long as the BVH builds don't need so
    // much build + scratch that we run out of GPU memory.
    // Some helpers for managing the temp upload heap buf allocation and queuing of
    // the commands would help to make it easier to write the parallel load version
    for (const auto &mesh : scene.meshes) {
        std::vector<vkrt::Geometry> geometries;
        for (const auto &geom : mesh.geometries) {
            // Upload triangle vertices to the device
            auto upload_verts = vkrt::Buffer::host(*device,
                                                   geom.vertices.size() * sizeof(glm::vec3),
                                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            {
                void *map = upload_verts->map();
                std::memcpy(map, geom.vertices.data(), upload_verts->size());
                upload_verts->unmap();
            }

            auto upload_indices = vkrt::Buffer::host(*device,
                                                     geom.indices.size() * sizeof(glm::uvec3),
                                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            {
                void *map = upload_indices->map();
                std::memcpy(map, geom.indices.data(), upload_indices->size());
                upload_indices->unmap();
            }

            std::shared_ptr<vkrt::Buffer> upload_normals = nullptr;
            std::shared_ptr<vkrt::Buffer> normal_buf = nullptr;
            if (!geom.normals.empty()) {
                upload_normals = vkrt::Buffer::host(*device,
                                                    geom.normals.size() * sizeof(glm::vec3),
                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
                normal_buf = vkrt::Buffer::device(
                    *device,
                    upload_normals->size(),
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

                void *map = upload_normals->map();
                std::memcpy(map, geom.normals.data(), upload_normals->size());
                upload_normals->unmap();
            }

            std::shared_ptr<vkrt::Buffer> upload_uvs = nullptr;
            std::shared_ptr<vkrt::Buffer> uv_buf = nullptr;
            if (!geom.uvs.empty()) {
                upload_uvs = vkrt::Buffer::host(*device,
                                                geom.uvs.size() * sizeof(glm::vec2),
                                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
                uv_buf = vkrt::Buffer::device(*device,
                                              upload_uvs->size(),
                                              VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

                void *map = upload_uvs->map();
                std::memcpy(map, geom.uvs.data(), upload_uvs->size());
                upload_uvs->unmap();
            }

            auto vertex_buf = vkrt::Buffer::device(
                *device,
                upload_verts->size(),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

            auto index_buf = vkrt::Buffer::device(
                *device,
                upload_indices->size(),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

            // Execute the upload to the device
            {
                VkCommandBufferBeginInfo begin_info = {};
                begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

                VkBufferCopy copy_cmd = {};
                copy_cmd.size = upload_verts->size();
                vkCmdCopyBuffer(command_buffer,
                                upload_verts->handle(),
                                vertex_buf->handle(),
                                1,
                                &copy_cmd);

                copy_cmd.size = upload_indices->size();
                vkCmdCopyBuffer(command_buffer,
                                upload_indices->handle(),
                                index_buf->handle(),
                                1,
                                &copy_cmd);

                if (upload_normals) {
                    copy_cmd.size = upload_normals->size();
                    vkCmdCopyBuffer(command_buffer,
                                    upload_normals->handle(),
                                    normal_buf->handle(),
                                    1,
                                    &copy_cmd);
                }

                if (upload_uvs) {
                    copy_cmd.size = upload_uvs->size();
                    vkCmdCopyBuffer(
                        command_buffer, upload_uvs->handle(), uv_buf->handle(), 1, &copy_cmd);
                }

                CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

                VkSubmitInfo submit_info = {};
                submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit_info.commandBufferCount = 1;
                submit_info.pCommandBuffers = &command_buffer;
                CHECK_VULKAN(
                    vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
                CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

                vkResetCommandPool(device->logical_device(),
                                   command_pool,
                                   VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
            }

            geometries.emplace_back(vertex_buf, index_buf, normal_buf, uv_buf);
            ++total_geom;
        }

        // Build the bottom level acceleration structure
        auto bvh = std::make_unique<vkrt::TriangleMesh>(*device, geometries);
        {
            // TODO: some convenience utils for this
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

            bvh->enqueue_build(command_buffer);

            CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

            VkSubmitInfo submit_info = {};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer;
            CHECK_VULKAN(
                vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
            CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

            vkResetCommandPool(device->logical_device(),
                               command_pool,
                               VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
        }

        // Compact the BVH
        {
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

            bvh->enqueue_compaction(command_buffer);

            CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

            VkSubmitInfo submit_info = {};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer;
            CHECK_VULKAN(
                vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
            CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

            vkResetCommandPool(device->logical_device(),
                               command_pool,
                               VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
        }
        bvh->finalize();
        meshes.emplace_back(std::move(bvh));
    }

    parameterized_meshes = scene.parameterized_meshes;
    std::vector<uint32_t> parameterized_mesh_sbt_offsets;
    {
        // Compute the offsets each parameterized mesh will be written too in the SBT,
        // these are then the instance SBT offsets shared by each instance
        uint32_t offset = 0;
        for (const auto &pm : parameterized_meshes) {
            parameterized_mesh_sbt_offsets.push_back(offset);
            offset += meshes[pm.mesh_id]->geometries.size();
        }
    }

    std::shared_ptr<vkrt::Buffer> instance_buf;
    {
        // Setup the instance buffer
        auto upload_instances = vkrt::Buffer::host(
            *device,
            scene.instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        VkAccelerationStructureInstanceKHR *map =
            reinterpret_cast<VkAccelerationStructureInstanceKHR *>(upload_instances->map());

        for (size_t i = 0; i < scene.instances.size(); ++i) {
            const auto &inst = scene.instances[i];
            std::memset(&map[i], 0, sizeof(VkAccelerationStructureInstanceKHR));
            map[i].instanceCustomIndex = i;
            map[i].instanceShaderBindingTableRecordOffset =
                parameterized_mesh_sbt_offsets[inst.parameterized_mesh_id];
            map[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
            map[i].accelerationStructureReference =
                meshes[parameterized_meshes[inst.parameterized_mesh_id].mesh_id]->handle;
            map[i].mask = 0xff;

            // Note: 4x3 row major
            const glm::mat4 m = glm::transpose(inst.transform);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 4; ++c) {
                    map[i].transform.matrix[r][c] = m[r][c];
                }
            }
        }
        upload_instances->unmap();

        instance_buf = vkrt::Buffer::device(
            *device,
            upload_instances->size(),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
        // Upload the instance data to the device
        {
            VkCommandBufferBeginInfo begin_info = {};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

            VkBufferCopy copy_cmd = {};
            copy_cmd.size = upload_instances->size();
            vkCmdCopyBuffer(command_buffer,
                            upload_instances->handle(),
                            instance_buf->handle(),
                            1,
                            &copy_cmd);

            CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

            VkSubmitInfo submit_info = {};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer;
            CHECK_VULKAN(
                vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
            CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

            vkResetCommandPool(device->logical_device(),
                               command_pool,
                               VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
        }
    }

    // Build the top level BVH
    scene_bvh = std::make_unique<vkrt::TopLevelBVH>(*device, instance_buf, scene.instances);
    {
        // TODO: some convenience utils for this
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

        scene_bvh->enqueue_build(command_buffer);

        CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
        CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

        vkResetCommandPool(device->logical_device(),
                           command_pool,
                           VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    }
    scene_bvh->finalize();

    mat_params = vkrt::Buffer::device(
        *device,
        scene.materials.size() * sizeof(DisneyMaterial),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    {
        auto upload_mat_params =
            vkrt::Buffer::host(*device, mat_params->size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        void *map = upload_mat_params->map();
        std::memcpy(map, scene.materials.data(), upload_mat_params->size());
        upload_mat_params->unmap();

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_mat_params->size();
        vkCmdCopyBuffer(
            command_buffer, upload_mat_params->handle(), mat_params->handle(), 1, &copy_cmd);

        CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
        CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

        vkResetCommandPool(device->logical_device(),
                           command_pool,
                           VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    }

    // Upload the scene textures
    for (const auto &t : scene.textures) {
        auto format =
            t.color_space == SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        auto tex = vkrt::Texture2D::device(
            *device,
            glm::uvec2(t.width, t.height),
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        auto upload_buf = vkrt::Buffer::host(
            *device, tex->pixel_size() * t.width * t.height, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        void *map = upload_buf->map();
        std::memcpy(map, t.img.data(), upload_buf->size());
        upload_buf->unmap();

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

        // Transition image to the general layout
        VkImageMemoryBarrier img_mem_barrier = {};
        img_mem_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        img_mem_barrier.image = tex->image_handle();
        img_mem_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img_mem_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        img_mem_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        img_mem_barrier.subresourceRange.baseMipLevel = 0;
        img_mem_barrier.subresourceRange.levelCount = 1;
        img_mem_barrier.subresourceRange.baseArrayLayer = 0;
        img_mem_barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &img_mem_barrier);

        VkImageSubresourceLayers copy_subresource = {};
        copy_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_subresource.mipLevel = 0;
        copy_subresource.baseArrayLayer = 0;
        copy_subresource.layerCount = 1;

        VkBufferImageCopy img_copy = {};
        img_copy.bufferOffset = 0;
        img_copy.bufferRowLength = 0;
        img_copy.bufferImageHeight = 0;
        img_copy.imageSubresource = copy_subresource;
        img_copy.imageOffset.x = 0;
        img_copy.imageOffset.y = 0;
        img_copy.imageOffset.z = 0;
        img_copy.imageExtent.width = t.width;
        img_copy.imageExtent.height = t.height;
        img_copy.imageExtent.depth = 1;

        vkCmdCopyBufferToImage(command_buffer,
                               upload_buf->handle(),
                               tex->image_handle(),
                               VK_IMAGE_LAYOUT_GENERAL,
                               1,
                               &img_copy);

        // Transition image to shader read optimal layout
        img_mem_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        img_mem_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &img_mem_barrier);

        CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
        CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

        vkResetCommandPool(device->logical_device(),
                           command_pool,
                           VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

        textures.push_back(tex);
    }

    {
        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.minLod = 0;
        sampler_info.maxLod = 0;
        CHECK_VULKAN(
            vkCreateSampler(device->logical_device(), &sampler_info, nullptr, &sampler));
    }

    light_params = vkrt::Buffer::device(
        *device,
        sizeof(QuadLight) * scene.lights.size(),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    {
        auto upload_light_params = vkrt::Buffer::host(
            *device, light_params->size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        void *map = upload_light_params->map();
        std::memcpy(map, scene.lights.data(), upload_light_params->size());
        upload_light_params->unmap();

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = upload_light_params->size();
        vkCmdCopyBuffer(command_buffer,
                        upload_light_params->handle(),
                        light_params->handle(),
                        1,
                        &copy_cmd);

        CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
        CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

        vkResetCommandPool(device->logical_device(),
                           command_pool,
                           VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    }

#ifdef USE_SLANG_COMPILER
    // Create scene params buffer (contains num_lights for Slang shader)
    // Slang path uses descriptor binding 7 instead of SBT for num_lights
    scene_params = vkrt::Buffer::host(
        *device, sizeof(uint32_t), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    {
        void *map = scene_params->map();
        uint32_t num_lights = static_cast<uint32_t>(scene.lights.size());
        std::memcpy(map, &num_lights, sizeof(uint32_t));
        scene_params->unmap();
    }

    // ============================================================================
    // Create global geometry buffers from scene data
    // ============================================================================
    //
    // NOTE: Slang SPIRV codegen uses standard SPIRV array layout rules which 
    // require vec3/uvec3 arrays to have ArrayStride 16 (aligned to vec4).
    // GLSL with VK_EXT_scalar_block_layout can use tightly-packed ArrayStride 12.
    // Therefore, we pad vec3/uvec3 data to vec4/uvec4 when using Slang compiler.
    // ============================================================================
   
    // --- Slang: Pad vec3/uvec3 arrays to vec4/uvec4 (ArrayStride 16) ---
    
    // 1. Global Vertex Buffer (vec3 positions -> padded to vec4)
    if (!scene.global_vertices.empty()) {
        global_vertex_count = scene.global_vertices.size();
        std::vector<glm::vec4> padded_vertices(global_vertex_count);
        for (size_t i = 0; i < global_vertex_count; ++i) {
            padded_vertices[i] = glm::vec4(scene.global_vertices[i], 0.0f);
        }
        upload_global_buffer(
            global_vertex_buffer,
            padded_vertices.data(),
            padded_vertices.size() * sizeof(glm::vec4),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
    }
    
    // 2. Global Index Buffer (uvec3 triangles -> padded to uvec4)
    if (!scene.global_indices.empty()) {
        global_index_count = scene.global_indices.size();
        std::vector<glm::uvec4> padded_indices(global_index_count);
        for (size_t i = 0; i < global_index_count; ++i) {
            padded_indices[i] = glm::uvec4(scene.global_indices[i], 0u);
        }
        upload_global_buffer(
            global_index_buffer,
            padded_indices.data(),
            padded_indices.size() * sizeof(glm::uvec4),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
    }
    
    // 3. Global Normal Buffer (vec3 normals -> padded to vec4)
    if (!scene.global_normals.empty()) {
        global_normal_count = scene.global_normals.size();
        std::vector<glm::vec4> padded_normals(global_normal_count);
        for (size_t i = 0; i < global_normal_count; ++i) {
            padded_normals[i] = glm::vec4(scene.global_normals[i], 0.0f);
        }
        upload_global_buffer(
            global_normal_buffer,
            padded_normals.data(),
            padded_normals.size() * sizeof(glm::vec4),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
    }
#else
    // --- GLSL: Use native vec3/uvec3 with scalar block layout (ArrayStride 12) ---
    
    // 1. Global Vertex Buffer (vec3 positions, tightly packed)
    if (!scene.global_vertices.empty()) {
        global_vertex_count = scene.global_vertices.size();
        upload_global_buffer(
            global_vertex_buffer,
            scene.global_vertices.data(),
            scene.global_vertices.size() * sizeof(glm::vec3),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
    }
    
    // 2. Global Index Buffer (uvec3 triangles, tightly packed)
    if (!scene.global_indices.empty()) {
        global_index_count = scene.global_indices.size();
        upload_global_buffer(
            global_index_buffer,
            scene.global_indices.data(),
            scene.global_indices.size() * sizeof(glm::uvec3),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
    }
    
    // 3. Global Normal Buffer (vec3 normals, tightly packed)
    if (!scene.global_normals.empty()) {
        global_normal_count = scene.global_normals.size();
        upload_global_buffer(
            global_normal_buffer,
            scene.global_normals.data(),
            scene.global_normals.size() * sizeof(glm::vec3),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
    }
    
#endif
    
    // 4. Global UV Buffer
    if (!scene.global_uvs.empty()) {
        global_uv_count = scene.global_uvs.size();
        
        upload_global_buffer(
            global_uv_buffer,
            scene.global_uvs.data(),
            scene.global_uvs.size() * sizeof(glm::vec2),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
    }
    
    // 5. MeshDesc Buffer
    if (!scene.mesh_descriptors.empty()) {
        mesh_desc_count = scene.mesh_descriptors.size();
        
        upload_global_buffer(
            mesh_desc_buffer,
            scene.mesh_descriptors.data(),
            scene.mesh_descriptors.size() * sizeof(MeshDesc),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
    }

    // Load environment map if specified, otherwise create dummy
    if (!scene.environment_map_path.empty()) {
        load_environment_map(scene.environment_map_path);
    } else {
        // Create dummy 1x1 black environment map to keep binding 15 valid
        create_dummy_environment_map();
    }

    build_raytracing_pipeline();
    build_shader_descriptor_table();
    build_shader_binding_table();
    record_command_buffers();
}

RenderStats RenderVulkan::render(const glm::vec3 &pos,
                                 const glm::vec3 &dir,
                                 const glm::vec3 &up,
                                 const float fovy,
                                 const bool camera_changed,
                                 const bool readback_framebuffer)
{
    using namespace std::chrono;
    RenderStats stats;

    if (camera_changed) {
        frame_id = 0;
    }

    update_view_parameters(pos, dir, up, fovy);

    CHECK_VULKAN(vkResetFences(device->logical_device(), 1, &fence));

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &render_cmd_buf;
    CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, fence));

    // Wait for the ray tracing to complete (critical for OIDN to read the buffer)
    CHECK_VULKAN(vkWaitForFences(
        device->logical_device(), 1, &fence, true, std::numeric_limits<uint64_t>::max()));

#ifdef ENABLE_OIDN
    // Execute OIDN denoising on the accumulated buffer
    oidn_filter.execute();
    
    // Check for OIDN errors
    const char* errorMessage = nullptr;
    if (oidn_device.getError(errorMessage) != oidn::Error::None) {
        std::cerr << "OIDN error: " << errorMessage << std::endl;
    }
    
    // Execute tonemap compute pass to convert denoised buffer to display format
    CHECK_VULKAN(vkResetFences(device->logical_device(), 1, &fence));
    submit_info.pCommandBuffers = &tonemap_cmd_buf;
    CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, fence));
    CHECK_VULKAN(vkWaitForFences(
        device->logical_device(), 1, &fence, true, std::numeric_limits<uint64_t>::max()));
#endif

#ifdef REPORT_RAY_STATS
    const bool need_readback = true;
#else
    const bool need_readback = !native_display || readback_framebuffer;
#endif

    // Queue the readback copy to start once rendering is done
    if (need_readback) {
        submit_info.pCommandBuffers = &readback_cmd_buf;
        CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
    }

    std::array<uint64_t, 2> render_timestamps;
    CHECK_VULKAN(vkGetQueryPoolResults(device->logical_device(),
                                       timing_query_pool,
                                       0,
                                       2,
                                       render_timestamps.size() * sizeof(uint64_t),
                                       render_timestamps.data(),
                                       sizeof(uint64_t),
                                       VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    stats.render_time = static_cast<double>(render_timestamps[1] - render_timestamps[0]) /
                        device->get_timestamp_frequency() * 1e3;

    // Now wait for the device to finish the readback copy as well
    if (need_readback) {
        CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));
        std::memcpy(img.data(), img_readback_buf->map(), img_readback_buf->size());
        img_readback_buf->unmap();
    }

#ifdef REPORT_RAY_STATS
    std::memcpy(ray_counts.data(),
                ray_stats_readback_buf->map(),
                ray_counts.size() * sizeof(uint16_t));
    ray_stats_readback_buf->unmap();

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

void RenderVulkan::build_raytracing_pipeline()
{
    // Maybe have the builder auto compute the binding numbers? May be annoying if tweaking
    // layouts or something though
    desc_layout =
        vkrt::DescriptorSetLayoutBuilder()
            .add_binding(0,
                         1,
                         VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                         VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .add_binding(
                1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .add_binding(
                2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .add_binding(
                3, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .add_binding(
                4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .add_binding(
                5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
#ifdef REPORT_RAY_STATS
            .add_binding(
                6, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
#endif
#ifdef USE_SLANG_COMPILER
            // Scene params (num_lights) at binding 7 - only for Slang path
            .add_binding(
                7, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
#endif
            // Global buffer bindings (10-14)
            .add_binding(
                10, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .add_binding(
                11, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .add_binding(
                12, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .add_binding(
                13, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .add_binding(
                14, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            // Environment map at binding 15
            .add_binding(
                15, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_MISS_BIT_KHR)
            // Textures at binding 30 (single descriptor set architecture)
            .add_binding(
                30,
                std::max(textures.size(), size_t(1)),
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_RAYGEN_BIT_KHR,
                VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)
            .build(*device);

    const size_t total_geom =
        std::accumulate(meshes.begin(),
                        meshes.end(),
                        0,
                        [](size_t n, const std::unique_ptr<vkrt::TriangleMesh> &t) {
                            return n + t->geometries.size();
                        });

    // DESCRIPTOR FLATTENING: Remove separate textures_desc_layout (Set 1)
    // Textures are now in Set 0 at binding 30
    std::vector<VkDescriptorSetLayout> descriptor_layouts = {desc_layout};

    VkPipelineLayoutCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create_info.setLayoutCount = descriptor_layouts.size();
    pipeline_create_info.pSetLayouts = descriptor_layouts.data();

    CHECK_VULKAN(vkCreatePipelineLayout(
        device->logical_device(), &pipeline_create_info, nullptr, &pipeline_layout));

#ifdef USE_SLANG_COMPILER
    // Load and compile Slang RT shaders
    chameleonrt::SlangShaderCompiler slangCompiler;
    if (!slangCompiler.isValid()) {
        throw std::runtime_error("Failed to initialize Slang shader compiler");
    }
    
    // Load unified Slang shader source with Vulkan bindings
    auto shaderSource = chameleonrt::SlangShaderCompiler::loadShaderSource("shaders/unified_render.slang");
    if (!shaderSource) {
        throw std::runtime_error("Failed to load unified_render.slang");
    }
    
    // Compile to SPIRV library with VULKAN define for binding selection
    // Add shaders/ directory to search paths so modules can be found
    std::vector<std::string> defines = {"VULKAN"};
#ifdef REPORT_RAY_STATS
    defines.push_back("REPORT_RAY_STATS");
#endif
    auto result = slangCompiler.compileSlangToSPIRVLibrary(*shaderSource, {"shaders"}, defines);
    if (!result) {
        throw std::runtime_error("Failed to compile Slang shader to SPIRV: " + slangCompiler.getLastError());
    }
    
    // Create shader modules from compiled SPIRV
    std::shared_ptr<vkrt::ShaderModule> raygen_shader, miss_shader, occlusion_miss_shader, closest_hit_shader;
    
    for (const auto& blob : *result) {
        // Validate SPIRV size alignment
        if (blob.bytecode.size() % 4 != 0) {
            std::cerr << "ERROR: SPIRV size " << blob.bytecode.size() 
                      << " is not aligned to 4-byte boundary for " << blob.entryPoint << std::endl;
            throw std::runtime_error("Invalid SPIRV alignment from Slang compiler");
        }
        
        // SPIRV must be 4-byte aligned - convert byte array to uint32_t array
        const uint32_t* spirv_code = reinterpret_cast<const uint32_t*>(blob.bytecode.data());
        size_t spirv_size_bytes = blob.bytecode.size();
        
        if (blob.entryPoint == "RayGen") {
            raygen_shader = std::make_shared<vkrt::ShaderModule>(*device, spirv_code, spirv_size_bytes);
        } else if (blob.entryPoint == "Miss") {
            miss_shader = std::make_shared<vkrt::ShaderModule>(*device, spirv_code, spirv_size_bytes);
        } else if (blob.entryPoint == "ShadowMiss") {
            occlusion_miss_shader = std::make_shared<vkrt::ShaderModule>(*device, spirv_code, spirv_size_bytes);
        } else if (blob.entryPoint == "ClosestHit") {
            closest_hit_shader = std::make_shared<vkrt::ShaderModule>(*device, spirv_code, spirv_size_bytes);
        }
    }
    
    // Verify all shaders were compiled
    if (!raygen_shader || !miss_shader || !occlusion_miss_shader || !closest_hit_shader) {
        throw std::runtime_error("Failed to compile all required Slang RT entry points");
    }
#else
    // Original embedded SPIRV path
    auto raygen_shader =
        std::make_shared<vkrt::ShaderModule>(*device, raygen_spv, sizeof(raygen_spv));

    auto miss_shader =
        std::make_shared<vkrt::ShaderModule>(*device, miss_spv, sizeof(miss_spv));
    auto occlusion_miss_shader = std::make_shared<vkrt::ShaderModule>(
        *device, occlusion_miss_spv, sizeof(occlusion_miss_spv));

    auto closest_hit_shader =
        std::make_shared<vkrt::ShaderModule>(*device, hit_spv, sizeof(hit_spv));
#endif

    rt_pipeline = vkrt::RTPipelineBuilder()
                      .set_raygen("raygen", raygen_shader)
                      .add_miss("miss", miss_shader)
                      .add_miss("occlusion_miss", occlusion_miss_shader)
                      .add_hitgroup("closest_hit", closest_hit_shader)
                      .set_recursion_depth(1)
                      .set_layout(pipeline_layout)
                      .build(*device);

#ifdef ENABLE_OIDN
    // Build tonemap compute pipeline using Slang compiler
    {
        // Load tonemap shader source
        auto tonemap_source = chameleonrt::SlangShaderCompiler::loadShaderSource("shaders/tonemap.slang");
        if (!tonemap_source) {
            throw std::runtime_error("Failed to load shaders/tonemap.slang");
        }
        
        std::vector<std::string> tonemap_defines;
        tonemap_defines.push_back("ENABLE_OIDN");
        
        auto tonemap_blob = slangCompiler.compileSlangToComputeSPIRV(
            *tonemap_source,
            "main",
            {"shaders"},
            tonemap_defines);
        
        if (!tonemap_blob) {
            throw std::runtime_error("Tonemap shader compilation failed: " + slangCompiler.getLastError());
        }
        
        // Create tonemap descriptor set layout
        tonemap_desc_layout =
            vkrt::DescriptorSetLayoutBuilder()
                .add_binding(1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)  // framebuffer
                .add_binding(8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT) // denoise_buffer
                .build(*device);
        
        // Create tonemap pipeline layout
        VkPipelineLayoutCreateInfo tonemap_layout_info = {};
        tonemap_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        tonemap_layout_info.setLayoutCount = 1;
        tonemap_layout_info.pSetLayouts = &tonemap_desc_layout;
        CHECK_VULKAN(vkCreatePipelineLayout(
            device->logical_device(), &tonemap_layout_info, nullptr, &tonemap_pipeline_layout));
        
        // Create shader module from compiled SPIRV
        const uint32_t* spirv_code = reinterpret_cast<const uint32_t*>(tonemap_blob->bytecode.data());
        VkShaderModuleCreateInfo module_info = {};
        module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_info.codeSize = tonemap_blob->bytecode.size();
        module_info.pCode = spirv_code;
        
        VkShaderModule tonemap_shader;
        CHECK_VULKAN(vkCreateShaderModule(device->logical_device(), &module_info, nullptr, &tonemap_shader));
        
        // Create compute pipeline
        VkComputePipelineCreateInfo compute_info = {};
        compute_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        compute_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compute_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        compute_info.stage.module = tonemap_shader;
        compute_info.stage.pName = "main";
        compute_info.layout = tonemap_pipeline_layout;
        
        CHECK_VULKAN(vkCreateComputePipelines(
            device->logical_device(), VK_NULL_HANDLE, 1, &compute_info, nullptr, &tonemap_pipeline));
        
        // Clean up shader module (pipeline has its own reference)
        vkDestroyShaderModule(device->logical_device(), tonemap_shader, nullptr);
        
        // Create descriptor pool for tonemap
        std::vector<VkDescriptorPoolSize> tonemap_pool_sizes = {
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
        };
        
        VkDescriptorPoolCreateInfo tonemap_pool_info = {};
        tonemap_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        tonemap_pool_info.maxSets = 1;
        tonemap_pool_info.poolSizeCount = tonemap_pool_sizes.size();
        tonemap_pool_info.pPoolSizes = tonemap_pool_sizes.data();
        CHECK_VULKAN(vkCreateDescriptorPool(
            device->logical_device(), &tonemap_pool_info, nullptr, &tonemap_desc_pool));
        
        // Allocate tonemap descriptor set
        VkDescriptorSetAllocateInfo tonemap_alloc_info = {};
        tonemap_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        tonemap_alloc_info.descriptorPool = tonemap_desc_pool;
        tonemap_alloc_info.descriptorSetCount = 1;
        tonemap_alloc_info.pSetLayouts = &tonemap_desc_layout;
        CHECK_VULKAN(vkAllocateDescriptorSets(device->logical_device(), &tonemap_alloc_info, &tonemap_desc_set));
        
        // Allocate tonemap command buffer
        VkCommandBufferAllocateInfo cmd_alloc_info = {};
        cmd_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_alloc_info.commandPool = render_cmd_pool;
        cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_alloc_info.commandBufferCount = 1;
        CHECK_VULKAN(vkAllocateCommandBuffers(device->logical_device(), &cmd_alloc_info, &tonemap_cmd_buf));
    }
#endif
}

void RenderVulkan::build_shader_descriptor_table()
{
    // DESCRIPTOR FLATTENING: Combined pool for all Set 0 resources (including textures at binding 30)
    const std::vector<VkDescriptorPoolSize> pool_sizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
#ifdef USE_SLANG_COMPILER
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},  // ViewParams + SceneParams (Slang only)
#else
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},  // ViewParams only (GLSL uses SBT)
#endif
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7},  // 2 existing + 5 global buffers
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             std::max(uint32_t(textures.size()), uint32_t(1)) + 1}};  // +1 for environment map at binding 15

    VkDescriptorPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create_info.maxSets = 1;  // Only Set 0 now (was 6)
    pool_create_info.poolSizeCount = pool_sizes.size();
    pool_create_info.pPoolSizes = pool_sizes.data();
    CHECK_VULKAN(vkCreateDescriptorPool(
        device->logical_device(), &pool_create_info, nullptr, &desc_pool));

    // Allocate Set 0 with variable descriptor count for textures at binding 30
    const uint32_t texture_set_size = textures.size();
    VkDescriptorSetVariableDescriptorCountAllocateInfo texture_set_size_info = {};
    texture_set_size_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    texture_set_size_info.descriptorSetCount = 1;
    texture_set_size_info.pDescriptorCounts = &texture_set_size;

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &desc_layout;
    alloc_info.pNext = &texture_set_size_info;  // Variable count for binding 30
    CHECK_VULKAN(vkAllocateDescriptorSets(device->logical_device(), &alloc_info, &desc_set));

    std::vector<vkrt::CombinedImageSampler> combined_samplers;
    for (const auto &t : textures) {
        combined_samplers.emplace_back(t, sampler);
    }

    // Write descriptors to Set 0 (including textures at binding 30)
    auto updater = vkrt::DescriptorSetUpdater()
                       .write_acceleration_structure(desc_set, 0, scene_bvh)
                       .write_storage_image(desc_set, 1, render_target)
                       .write_ssbo(desc_set, 2, accum_buffer)
                       .write_ubo(desc_set, 3, view_param_buf)
                       .write_ssbo(desc_set, 4, mat_params)
                       .write_ssbo(desc_set, 5, light_params);
#ifdef REPORT_RAY_STATS
    updater.write_storage_image(desc_set, 6, ray_stats);
#endif
#ifdef USE_SLANG_COMPILER
    updater.write_ubo(desc_set, 7, scene_params);  // num_lights for Slang shader
#endif

    // DESCRIPTOR FLATTENING: Write textures to Set 0, binding 30 (was Set 1, binding 0)
    if (!combined_samplers.empty()) {
        updater.write_combined_sampler_array(desc_set, 30, combined_samplers);
    }
    updater.update(*device);

    // ============================================================================
    // Write global buffer descriptors
    // ============================================================================
    
    // Global vertex buffer (binding 10)
    if (global_vertex_buffer) {
        VkDescriptorBufferInfo vertex_info = {};
        vertex_info.buffer = global_vertex_buffer->handle();
        vertex_info.offset = 0;
        vertex_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet vertex_write = {};
        vertex_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vertex_write.dstSet = desc_set;
        vertex_write.dstBinding = 10;
        vertex_write.dstArrayElement = 0;
        vertex_write.descriptorCount = 1;
        vertex_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vertex_write.pBufferInfo = &vertex_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &vertex_write, 0, nullptr);
    }
    
    // Global index buffer (binding 11)
    if (global_index_buffer) {
        VkDescriptorBufferInfo index_info = {};
        index_info.buffer = global_index_buffer->handle();
        index_info.offset = 0;
        index_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet index_write = {};
        index_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        index_write.dstSet = desc_set;
        index_write.dstBinding = 11;
        index_write.dstArrayElement = 0;
        index_write.descriptorCount = 1;
        index_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        index_write.pBufferInfo = &index_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &index_write, 0, nullptr);
    }
    
    // Global normal buffer (binding 12)
    if (global_normal_buffer) {
        VkDescriptorBufferInfo normal_info = {};
        normal_info.buffer = global_normal_buffer->handle();
        normal_info.offset = 0;
        normal_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet normal_write = {};
        normal_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        normal_write.dstSet = desc_set;
        normal_write.dstBinding = 12;
        normal_write.dstArrayElement = 0;
        normal_write.descriptorCount = 1;
        normal_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        normal_write.pBufferInfo = &normal_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &normal_write, 0, nullptr);
    }
    
    // Global UV buffer (binding 13)
    if (global_uv_buffer) {
        VkDescriptorBufferInfo uv_info = {};
        uv_info.buffer = global_uv_buffer->handle();
        uv_info.offset = 0;
        uv_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet uv_write = {};
        uv_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uv_write.dstSet = desc_set;
        uv_write.dstBinding = 13;
        uv_write.dstArrayElement = 0;
        uv_write.descriptorCount = 1;
        uv_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        uv_write.pBufferInfo = &uv_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &uv_write, 0, nullptr);
    }
    
    // MeshDesc buffer (binding 14)
    if (mesh_desc_buffer) {
        VkDescriptorBufferInfo mesh_info = {};
        mesh_info.buffer = mesh_desc_buffer->handle();
        mesh_info.offset = 0;
        mesh_info.range = VK_WHOLE_SIZE;
        
        VkWriteDescriptorSet mesh_write = {};
        mesh_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        mesh_write.dstSet = desc_set;
        mesh_write.dstBinding = 14;
        mesh_write.dstArrayElement = 0;
        mesh_write.descriptorCount = 1;
        mesh_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        mesh_write.pBufferInfo = &mesh_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &mesh_write, 0, nullptr);
    }

    // Environment map (binding 15) - always write since we guarantee texture exists
    if (env_map_texture && env_map_sampler) {
        VkDescriptorImageInfo env_image_info = {};
        env_image_info.sampler = env_map_sampler;
        env_image_info.imageView = env_map_texture->view_handle();
        env_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        
        VkWriteDescriptorSet env_write = {};
        env_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        env_write.dstSet = desc_set;
        env_write.dstBinding = 15;
        env_write.dstArrayElement = 0;
        env_write.descriptorCount = 1;
        env_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        env_write.pImageInfo = &env_image_info;
        
        vkUpdateDescriptorSets(device->logical_device(), 1, &env_write, 0, nullptr);
    }

#ifdef ENABLE_OIDN
    // Update tonemap descriptor set
    vkrt::DescriptorSetUpdater()
        .write_storage_image(tonemap_desc_set, 1, render_target)
        .write_ssbo(tonemap_desc_set, 8, denoise_buffer)
        .update(*device);
#endif
}

void RenderVulkan::build_shader_binding_table()
{
    vkrt::SBTBuilder sbt_builder(&rt_pipeline);
    sbt_builder.set_raygen(vkrt::ShaderRecord("raygen", "raygen", sizeof(uint32_t)))
        .add_miss(vkrt::ShaderRecord("miss", "miss", 0))
        .add_miss(vkrt::ShaderRecord("occlusion_miss", "occlusion_miss", 0));

    for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
        const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < meshes[pm.mesh_id]->geometries.size(); ++j) {
            const std::string hg_name =
                "HitGroup_param_mesh" + std::to_string(i) + "_geom" + std::to_string(j);
            sbt_builder.add_hitgroup(
                vkrt::ShaderRecord(hg_name, "closest_hit", sizeof(HitGroupParams)));
        }
    }

    shader_table = sbt_builder.build(*device);

    shader_table.map_sbt();

    // Raygen shader gets the number of lights through the SBT
    {
        uint32_t *params = reinterpret_cast<uint32_t *>(shader_table.sbt_params("raygen"));
        *params = light_params->size() / sizeof(QuadLight);
    }

    // Write simplified SBT with only meshDescIndex
    
    size_t mesh_desc_index = 0;
    for (size_t i = 0; i < parameterized_meshes.size(); ++i) {
        const auto &pm = parameterized_meshes[i];
        for (size_t j = 0; j < meshes[pm.mesh_id]->geometries.size(); ++j) {
            const std::string hg_name =
                "HitGroup_param_mesh" + std::to_string(i) + "_geom" + std::to_string(j);

            HitGroupParams *params =
                reinterpret_cast<HitGroupParams *>(shader_table.sbt_params(hg_name));

            // Write only meshDescIndex - all geometry data accessed via global buffers
            params->meshDescIndex = static_cast<uint32_t>(mesh_desc_index);
            
            mesh_desc_index++;
        }
    }

    {
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = shader_table.upload_sbt->size();
        vkCmdCopyBuffer(command_buffer,
                        shader_table.upload_sbt->handle(),
                        shader_table.sbt->handle(),
                        1,
                        &copy_cmd);

        CHECK_VULKAN(vkEndCommandBuffer(command_buffer));

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
        CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));

        vkResetCommandPool(device->logical_device(),
                           command_pool,
                           VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    }
}

void RenderVulkan::update_view_parameters(const glm::vec3 &pos,
                                          const glm::vec3 &dir,
                                          const glm::vec3 &up,
                                          const float fovy)
{
    glm::vec2 img_plane_size;
    img_plane_size.y = 2.f * std::tan(glm::radians(0.5f * fovy));
    img_plane_size.x = img_plane_size.y * static_cast<float>(render_target->dims().x) /
                       render_target->dims().y;

    const glm::vec3 dir_du = glm::normalize(glm::cross(dir, up)) * img_plane_size.x;
    const glm::vec3 dir_dv = -glm::normalize(glm::cross(dir_du, dir)) * img_plane_size.y;
    const glm::vec3 dir_top_left = dir - 0.5f * dir_du - 0.5f * dir_dv;

    uint8_t *buf = static_cast<uint8_t *>(view_param_buf->map());
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
    view_param_buf->unmap();
}

void RenderVulkan::record_command_buffers()
{
    vkResetCommandPool(device->logical_device(),
                       render_cmd_pool,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    CHECK_VULKAN(vkBeginCommandBuffer(render_cmd_buf, &begin_info));

    vkCmdResetQueryPool(render_cmd_buf, timing_query_pool, 0, 2);

    vkCmdBindPipeline(
        render_cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rt_pipeline.handle());

    // DESCRIPTOR FLATTENING: Only bind Set 0 (textures moved from Set 1 to binding 30)
    const std::vector<VkDescriptorSet> descriptor_sets = {desc_set};

    vkCmdBindDescriptorSets(render_cmd_buf,
                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            pipeline_layout,
                            0,
                            descriptor_sets.size(),
                            descriptor_sets.data(),
                            0,
                            nullptr);

    VkStridedDeviceAddressRegionKHR callable_table = {};
    callable_table.deviceAddress = 0;

    vkCmdWriteTimestamp(
        render_cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timing_query_pool, 0);
    vkrt::CmdTraceRaysKHR(render_cmd_buf,
                          &shader_table.raygen,
                          &shader_table.miss,
                          &shader_table.hitgroup,
                          &callable_table,
                          render_target->dims().x,
                          render_target->dims().y,
                          1);
    vkCmdWriteTimestamp(
        render_cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timing_query_pool, 1);

    // Queue a barrier for rendering to finish
    vkCmdPipelineBarrier(render_cmd_buf,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         0,
                         nullptr);

    CHECK_VULKAN(vkEndCommandBuffer(render_cmd_buf));

    CHECK_VULKAN(vkBeginCommandBuffer(readback_cmd_buf, &begin_info));

    VkImageSubresourceLayers copy_subresource = {};
    copy_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_subresource.mipLevel = 0;
    copy_subresource.baseArrayLayer = 0;
    copy_subresource.layerCount = 1;

    VkBufferImageCopy img_copy = {};
    img_copy.bufferOffset = 0;
    img_copy.bufferRowLength = 0;
    img_copy.bufferImageHeight = 0;
    img_copy.imageSubresource = copy_subresource;
    img_copy.imageOffset.x = 0;
    img_copy.imageOffset.y = 0;
    img_copy.imageOffset.z = 0;
    img_copy.imageExtent.width = render_target->dims().x;
    img_copy.imageExtent.height = render_target->dims().y;
    img_copy.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(readback_cmd_buf,
                           render_target->image_handle(),
                           VK_IMAGE_LAYOUT_GENERAL,
                           img_readback_buf->handle(),
                           1,
                           &img_copy);

#ifdef REPORT_RAY_STATS
    img_copy.bufferOffset = 0;
    img_copy.bufferRowLength = 0;
    img_copy.bufferImageHeight = 0;
    img_copy.imageSubresource = copy_subresource;
    img_copy.imageOffset.x = 0;
    img_copy.imageOffset.y = 0;
    img_copy.imageOffset.z = 0;
    img_copy.imageExtent.width = render_target->dims().x;
    img_copy.imageExtent.height = render_target->dims().y;
    img_copy.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(readback_cmd_buf,
                           ray_stats->image_handle(),
                           VK_IMAGE_LAYOUT_GENERAL,
                           ray_stats_readback_buf->handle(),
                           1,
                           &img_copy);
#endif

    CHECK_VULKAN(vkEndCommandBuffer(readback_cmd_buf));

#ifdef ENABLE_OIDN
    // Record tonemap compute pass command buffer
    CHECK_VULKAN(vkBeginCommandBuffer(tonemap_cmd_buf, &begin_info));
    
    vkCmdBindPipeline(tonemap_cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, tonemap_pipeline);
    vkCmdBindDescriptorSets(tonemap_cmd_buf,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            tonemap_pipeline_layout,
                            0,
                            1,
                            &tonemap_desc_set,
                            0,
                            nullptr);
    
    // Dispatch compute shader (16x16 workgroups to match tonemap.slang numthreads)
    uint32_t dispatch_x = (render_target->dims().x + 15) / 16;
    uint32_t dispatch_y = (render_target->dims().y + 15) / 16;
    vkCmdDispatch(tonemap_cmd_buf, dispatch_x, dispatch_y, 1);
    
    // Barrier for compute to complete before readback
    VkMemoryBarrier memory_barrier = {};
    memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    
    vkCmdPipelineBarrier(tonemap_cmd_buf,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         1, &memory_barrier,
                         0, nullptr,
                         0, nullptr);
    
    CHECK_VULKAN(vkEndCommandBuffer(tonemap_cmd_buf));
#endif
}

// Upload global buffer data to GPU via staging buffer
void RenderVulkan::upload_global_buffer(std::shared_ptr<vkrt::Buffer>& gpu_buf,
                                       const void* data,
                                       size_t size,
                                       VkBufferUsageFlags usage)
{
    // Create staging buffer
    auto staging = vkrt::Buffer::host(
        *device,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    );
    
    // Copy data to staging
    std::memcpy(staging->map(), data, size);
    staging->unmap();
    
    // Create device buffer
    gpu_buf = vkrt::Buffer::device(
        *device,
        size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    
    // Copy staging to device via command buffer
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));
    
    VkBufferCopy copy_region = {};
    copy_region.size = size;
    vkCmdCopyBuffer(command_buffer,
                   staging->handle(),
                   gpu_buf->handle(),
                   1,
                   &copy_region);
    
    // Barrier to make buffer shader-readable
    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = gpu_buf->handle();
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        0,
                        0, nullptr,
                        1, &barrier,
                        0, nullptr);
    
    CHECK_VULKAN(vkEndCommandBuffer(command_buffer));
    
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
    CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));
    
    vkResetCommandPool(device->logical_device(),
                      command_pool,
                      VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
}

void RenderVulkan::load_environment_map(const std::string& path) {
    try {
        // Load from file using util function
        HDRImage img = ::load_environment_map(path);
        
        // Upload to GPU
        upload_environment_map(img);
        
        has_environment = true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to load environment map: " << e.what() << std::endl;
        has_environment = false;
        // Create dummy environment map to keep descriptor valid
        create_dummy_environment_map();
    }
}

void RenderVulkan::upload_environment_map(const HDRImage& img) {
    // Create GPU texture for environment map (RGBA32F format)
    env_map_texture = vkrt::Texture2D::device(
        *device,
        glm::uvec2(img.width, img.height),
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    );
    
    // Create upload buffer
    const size_t upload_size = img.width * img.height * 4 * sizeof(float);
    auto upload_buf = vkrt::Buffer::host(
        *device,
        upload_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    );
    
    // Copy data to upload buffer
    void* mapped = upload_buf->map();
    std::memcpy(mapped, img.data, upload_size);
    upload_buf->unmap();
    
    // Record commands to upload texture
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));
    
    // Transition image to transfer destination layout
    VkImageMemoryBarrier img_barrier = {};
    img_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier.image = env_map_texture->image_handle();
    img_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    img_barrier.srcAccessMask = 0;
    img_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    img_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    img_barrier.subresourceRange.baseMipLevel = 0;
    img_barrier.subresourceRange.levelCount = 1;
    img_barrier.subresourceRange.baseArrayLayer = 0;
    img_barrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &img_barrier);
    
    // Copy buffer to image
    VkBufferImageCopy copy_region = {};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = 0;    // Tightly packed
    copy_region.bufferImageHeight = 0;  // Tightly packed
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageOffset = {0, 0, 0};
    copy_region.imageExtent = {static_cast<uint32_t>(img.width), 
                               static_cast<uint32_t>(img.height), 1};
    
    vkCmdCopyBufferToImage(command_buffer,
                           upload_buf->handle(),
                           env_map_texture->image_handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copy_region);
    
    // Transition to shader read optimal layout
    img_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    img_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &img_barrier);
    
    CHECK_VULKAN(vkEndCommandBuffer(command_buffer));
    
    // Submit and wait for completion
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
    CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));
    
    vkResetCommandPool(device->logical_device(),
                       command_pool,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    
    // Create sampler for environment map with appropriate settings
    if (env_map_sampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;   // Wrap horizontally (360°)
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;  // Clamp at poles
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.minLod = 0;
        sampler_info.maxLod = 0;
        
        CHECK_VULKAN(vkCreateSampler(device->logical_device(), &sampler_info, 
                                     nullptr, &env_map_sampler));
    }
}

void RenderVulkan::create_dummy_environment_map() {
    // Create a 1x1 black texture to use when no environment map is loaded
    // This ensures binding 15 always has a valid descriptor
    env_map_texture = vkrt::Texture2D::device(
        *device,
        glm::uvec2(1, 1),
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    );
    
    // Create upload buffer with black color (all zeros)
    const size_t upload_size = 1 * 1 * 4 * sizeof(float);
    auto upload_buf = vkrt::Buffer::host(
        *device,
        upload_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    );
    
    // Zero out the buffer (black)
    void* mapped = upload_buf->map();
    std::memset(mapped, 0, upload_size);
    upload_buf->unmap();
    
    // Upload to GPU
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK_VULKAN(vkBeginCommandBuffer(command_buffer, &begin_info));
    
    // Transition to transfer destination
    VkImageMemoryBarrier img_barrier = {};
    img_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    img_barrier.image = env_map_texture->image_handle();
    img_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    img_barrier.srcAccessMask = 0;
    img_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    img_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    img_barrier.subresourceRange.baseMipLevel = 0;
    img_barrier.subresourceRange.levelCount = 1;
    img_barrier.subresourceRange.baseArrayLayer = 0;
    img_barrier.subresourceRange.layerCount = 1;
    
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &img_barrier);
    
    // Copy buffer to image
    VkBufferImageCopy copy_region = {};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = 0;
    copy_region.bufferImageHeight = 0;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageOffset = {0, 0, 0};
    copy_region.imageExtent = {1, 1, 1};
    
    vkCmdCopyBufferToImage(command_buffer,
                           upload_buf->handle(),
                           env_map_texture->image_handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copy_region);
    
    // Transition to shader read
    img_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    img_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    img_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0, 0, nullptr, 0, nullptr, 1, &img_barrier);
    
    CHECK_VULKAN(vkEndCommandBuffer(command_buffer));
    
    // Submit and wait
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    CHECK_VULKAN(vkQueueSubmit(device->graphics_queue(), 1, &submit_info, VK_NULL_HANDLE));
    CHECK_VULKAN(vkQueueWaitIdle(device->graphics_queue()));
    
    vkResetCommandPool(device->logical_device(),
                       command_pool,
                       VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    
    // Create sampler if not exists
    if (env_map_sampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo sampler_info = {};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.minLod = 0;
        sampler_info.maxLod = 0;
        
        CHECK_VULKAN(vkCreateSampler(device->logical_device(), &sampler_info,
                                     nullptr, &env_map_sampler));
    }
    
    has_environment = false;
}
