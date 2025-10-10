cmake_dependent_option(ENABLE_DXR "Build the DXR rendering backend" OFF
    "WIN32" OFF)

# Build if either regular DXR or DXR with Slang is enabled
if (NOT ENABLE_DXR AND NOT ENABLE_DXR_SLANG)
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

add_dxil_embed_library(dxr_shaders render_dxr.hlsl util.hlsl
	disney_bsdf.hlsl lcg_rng.hlsl
	COMPILE_OPTIONS -O3
	COMPILE_DEFINITIONS
        ${HLSL_COMPILE_DEFNS}
	INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_LIST_DIR}
        ${PROJECT_SOURCE_DIR})

add_library(crt_dxr MODULE
    render_dxr_plugin.cpp
    render_dxr.cpp
    dxdisplay.cpp
    dx12_utils.cpp
    dxr_utils.cpp
    imgui_impl_dx12.cpp)

set_target_properties(crt_dxr PROPERTIES
	CXX_STANDARD 17
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

# Add Slang support if enabled
if(ENABLE_DXR_SLANG)
    target_link_libraries(crt_dxr PRIVATE slang_compiler_util)
    target_compile_definitions(crt_dxr PRIVATE USE_SLANG_COMPILER=1)
    
    # Slang requires DXC for HLSL->DXIL compilation
    # Deploy dxcompiler.dll and dxil.dll to output directory
    if(WIN32)
        # Find Windows SDK bin directory with DXC
        # Try common SDK versions
        set(POSSIBLE_SDK_VERSIONS 
            "10.0.26100.0"
            "10.0.22621.0" 
            "10.0.22000.0"
            "10.0.20348.0"
            "10.0.19041.0"
        )
        
        set(DXC_FOUND FALSE)
        foreach(SDK_VERSION ${POSSIBLE_SDK_VERSIONS})
            set(WINSDK_BIN_DIR "C:/Program Files (x86)/Windows Kits/10/bin/${SDK_VERSION}/x64")
            if(EXISTS "${WINSDK_BIN_DIR}/dxcompiler.dll")
                set(DXC_FOUND TRUE)
                message(STATUS "Found DXC in Windows SDK ${SDK_VERSION}")
                break()
            endif()
        endforeach()
        
        if(DXC_FOUND)
            # Copy DXC DLLs to output directory after build
            add_custom_command(TARGET crt_dxr POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${WINSDK_BIN_DIR}/dxcompiler.dll"
                    "$<TARGET_FILE_DIR:crt_dxr>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${WINSDK_BIN_DIR}/dxil.dll"
                    "$<TARGET_FILE_DIR:crt_dxr>"
                COMMENT "Deploying DXC DLLs for Slang HLSL compilation"
            )
            message(STATUS "DXC DLLs will be deployed to output directory")
        else()
            message(WARNING "DXC compiler not found in Windows SDK. Slang HLSL compilation may fail at runtime.")
            message(WARNING "Please install Windows 10 SDK or manually copy dxcompiler.dll and dxil.dll to output directory")
        endif()
        
        # Copy shader source files to output directory for runtime compilation
        set(SHADER_FILES
            render_dxr.hlsl
            util.hlsl
            disney_bsdf.hlsl
            lcg_rng.hlsl
            lights.hlsl
        )
        
        foreach(SHADER_FILE ${SHADER_FILES})
            add_custom_command(TARGET crt_dxr POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}"
                    "$<TARGET_FILE_DIR:crt_dxr>/${SHADER_FILE}"
                COMMENT "Deploying ${SHADER_FILE} for runtime Slang compilation"
            )
        endforeach()
        
        # Copy utility header from util directory (for #include "util/texture_channel_mask.h")
        add_custom_command(TARGET crt_dxr POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:crt_dxr>/util"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PROJECT_SOURCE_DIR}/util/texture_channel_mask.h"
                "$<TARGET_FILE_DIR:crt_dxr>/util/texture_channel_mask.h"
            COMMENT "Deploying util/texture_channel_mask.h for runtime Slang compilation"
        )
        
        message(STATUS "Shader files will be deployed to output directory")
    endif()
endif()

if (ENABLE_PIX_RUNTIME)
    target_compile_options(crt_dxr PUBLIC
        -DUSE_PIX=1
        -DENABLE_PIX_RUNTIME=1)

    target_link_libraries(crt_dxr PUBLIC
        PIX::WinPixEventRuntime)
endif()

install(TARGETS crt_dxr
    LIBRARY DESTINATION bin)

