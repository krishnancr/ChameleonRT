cmake_dependent_option(ENABLE_VULKAN "Build the Vulkan rendering backend. Requires Vulkan" OFF
    "NOT APPLE" OFF)

# Build if either regular Vulkan or Vulkan with Slang is enabled
if (NOT ENABLE_VULKAN AND NOT ENABLE_VULKAN_SLANG)
    return()
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Vulkan REQUIRED)

set(GLSL_COMPILE_DEFNS "")
if (REPORT_RAY_STATS)
	set(GLSL_COMPILE_DEFNS "REPORT_RAY_STATS=1")
endif()

# Only create embedded SPIRV shaders if we're NOT using Slang
# When using Slang, shaders are compiled at runtime from .slang files
if(NOT ENABLE_SLANG)
    add_spirv_embed_library(spv_shaders raygen.rgen miss.rmiss hit.rchit
        occlusion_miss.rmiss
        COMPILE_OPTIONS -O
        INCLUDE_DIRECTORIES
            ${PROJECT_SOURCE_DIR}
        COMPILE_DEFINITIONS
            ${GLSL_COMPILE_DEFNS})
else()
    # Create empty library when using Slang (to satisfy linker dependencies)
    add_library(spv_shaders INTERFACE)
endif()

add_library(crt_vulkan MODULE
    render_vulkan_plugin.cpp
    render_vulkan.cpp
    vulkan_utils.cpp
    vulkanrt_utils.cpp
    vkdisplay.cpp
    imgui_impl_vulkan.cpp)

set_target_properties(crt_vulkan PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON)

if (REPORT_RAY_STATS)
	target_compile_options(crt_vulkan PUBLIC
		-DREPORT_RAY_STATS=1)
endif()

target_compile_options(crt_vulkan PUBLIC
    -DVK_ENABLE_BETA_EXTENSIONS=1)

target_link_libraries(crt_vulkan PUBLIC
	spv_shaders
    util
    display
    Vulkan::Vulkan)

# Add Slang support if enabled
if(ENABLE_VULKAN_SLANG)
    target_link_libraries(crt_vulkan PRIVATE slang_compiler_util)
    target_compile_definitions(crt_vulkan PRIVATE USE_SLANG_COMPILER=1)
    
    # Note: Vulkan uses SPIRV, doesn't need DXC DLLs like D3D12
    # Keep existing glslang/shaderc finding for fallback
    message(STATUS "Vulkan backend: Slang runtime shader compilation enabled")
endif()

install(TARGETS crt_vulkan
    LIBRARY DESTINATION bin)

# Copy OIDN DLLs to output directory
if(ENABLE_OIDN)
    copy_oidn_dlls(crt_vulkan)
endif()
