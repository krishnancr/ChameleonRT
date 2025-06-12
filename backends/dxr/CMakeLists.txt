cmake_dependent_option(ENABLE_DXR "Build the DXR rendering backend" OFF
    "WIN32" OFF)

if (NOT ENABLE_DXR)
    return()
endif()

cmake_dependent_option(ENABLE_PIX_RUNTIME
    "Build support for WinPixEventRuntime tracing. Set WinPixEventRuntime_DIR" OFF
    "ENABLE_DXR" OFF)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(D3D12 REQUIRED)

if (ENABLE_PIX_RUNTIME)
    find_package(WinPixEventRuntime REQUIRED)
endif()

set(HLSL_COMPILE_DEFNS "")
if (REPORT_RAY_STATS)
	set(HLSL_COMPILE_DEFNS "REPORT_RAY_STATS=1")
endif()

# Print debug info for troubleshooting file globbing
message(STATUS "CMAKE_CURRENT_LIST_DIR: ${CMAKE_CURRENT_LIST_DIR}")

# Use GLOB to find all HLSL files except for render_dxr.hlsl
file(GLOB OTHER_HLSL_FILES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_LIST_DIR}/*.hlsl"
)

# Filter out render_dxr.hlsl from the list if it exists
list(FILTER OTHER_HLSL_FILES EXCLUDE REGEX ".*render_dxr\\.hlsl$")

# Explicitly add render_dxr.hlsl as the first file
set(DXR_HLSL_FILES 
    "${CMAKE_CURRENT_LIST_DIR}/render_dxr.hlsl"
    ${OTHER_HLSL_FILES}
)

message(STATUS "DXR_HLSL_FILES: ${DXR_HLSL_FILES}")

if(NOT DXR_HLSL_FILES)
    message(FATAL_ERROR "No HLSL files found in ${CMAKE_CURRENT_LIST_DIR}")
endif()

add_dxil_embed_library(dxr_shaders ${DXR_HLSL_FILES}
    COMPILE_OPTIONS -O3
    COMPILE_DEFINITIONS
        ${HLSL_COMPILE_DEFNS}
    INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_LIST_DIR}
        ${PROJECT_SOURCE_DIR}
)

add_library(crt_dxr MODULE
    render_dxr_plugin.cpp
    render_dxr.cpp
    dxdisplay.cpp
    dx12_utils.cpp
    dxr_utils.cpp
    imgui_impl_dx12.cpp)

set_target_properties(crt_dxr PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)

if (REPORT_RAY_STATS)
	target_compile_options(crt_dxr PUBLIC
		-DREPORT_RAY_STATS=1)
endif()

target_include_directories(crt_dxr PUBLIC
	$<BUILD_INTERFACE:${D3D12_INCLUDE_DIRS}>)

target_link_libraries(crt_dxr PUBLIC
	dxr_shaders
    util
    display
    ${D3D12_LIBRARIES})

if (ENABLE_PIX_RUNTIME)
    target_compile_options(crt_dxr PUBLIC
        -DUSE_PIX=1
        -DENABLE_PIX_RUNTIME=1)

    target_link_libraries(crt_dxr PUBLIC
        PIX::WinPixEventRuntime)
endif()

install(TARGETS crt_dxr
    LIBRARY DESTINATION bin)

