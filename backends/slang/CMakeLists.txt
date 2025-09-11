# FindSlang.cmake is now in the root cmake/ directory
find_package(Slang REQUIRED)

# Only search for D3D12/DXC on Windows platforms when not building for Vulkan
if(WIN32 AND NOT (DEFINED USE_VULKAN AND USE_VULKAN))
    # Use the existing FindD3D12 module from DXR backend for DXC detection
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../dxr/cmake")
    
    find_package(D3D12)
    
    if(D3D12_FOUND AND D3D12_SHADER_COMPILER)
        message(STATUS "Found DXC compiler: ${D3D12_SHADER_COMPILER}")
        
        # Get the directory containing DXC
        get_filename_component(DXC_BIN_DIR ${D3D12_SHADER_COMPILER} DIRECTORY)
        
        # Look for dxcompiler.dll in the same directory as dxc.exe
        find_file(DXC_RUNTIME_DLL 
            NAMES dxcompiler.dll
            PATHS ${DXC_BIN_DIR}
            NO_DEFAULT_PATH)
            
        if(DXC_RUNTIME_DLL)
            message(STATUS "Found DXC runtime: ${DXC_RUNTIME_DLL}")
            set(DXC_DEPLOY_NEEDED TRUE)
        else()
            message(WARNING "DXC compiler found but dxcompiler.dll missing - D3D12 DXIL compilation may fail")
        endif()
    else()
        message(WARNING "D3D12/DXC not found - D3D12 backend will use fallback compilation")
    endif()
endif()

set(CRT_SLANG_SOURCES
    render_slang.cpp
    render_slang.h
    slangdisplay.cpp
    slangdisplay.h
    render_slang_plugin.cpp
    render_slang_flat.slang
)

# Note: ImGui backend implementations removed - using unified Slang GFX rendering

add_library(crt_slang MODULE ${CRT_SLANG_SOURCES})

set_target_properties(crt_slang PROPERTIES 
    OUTPUT_NAME "crt_slang"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(crt_slang
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${PROJECT_SOURCE_DIR}
        ${Slang_INCLUDE_DIRS}
)

target_link_libraries(crt_slang PRIVATE util display Slang::Slang)
# Link to the GFX library which provides gfxCreateDevice
target_link_libraries(crt_slang PRIVATE Slang::GFX)

# Add conditional dependencies based on USE_VULKAN define
if(DEFINED USE_VULKAN AND USE_VULKAN)
    # Add Vulkan dependencies
    find_package(Vulkan REQUIRED)
    target_link_libraries(crt_slang PRIVATE ${Vulkan_LIBRARIES})
    target_include_directories(crt_slang PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(crt_slang PRIVATE USE_VULKAN)
    
    # Add platform-specific dependencies for Vulkan
    if(UNIX AND NOT APPLE)
        # Linux specific libraries for Vulkan
        target_link_libraries(crt_slang PRIVATE ${CMAKE_DL_LIBS})
    endif()
else()
    # Add D3D12 dependencies for Windows (default behavior)
    if(WIN32)
        target_link_libraries(crt_slang PRIVATE d3d12.lib)
    endif()
endif()

# Only copy dependencies to the main build output directory (e.g., build/Debug)
if(WIN32)
    set(SLANG_DEPENDENCIES
        "${Slang_ROOT}/bin/slang.dll"
        "${Slang_ROOT}/bin/gfx.dll"
    )
    set(FINAL_OUTPUT_DIR "${PROJECT_BINARY_DIR}/$<CONFIG>")
elseif(APPLE)
    set(SLANG_DEPENDENCIES
        "${Slang_ROOT}/lib/libslang.dylib"
        "${Slang_ROOT}/lib/libgfx.dylib"
    )
    set(FINAL_OUTPUT_DIR "${PROJECT_BINARY_DIR}/$<CONFIG>")
else()
    set(SLANG_DEPENDENCIES
        "${Slang_ROOT}/lib/libslang.so"
        "${Slang_ROOT}/lib/libgfx.so"
    )
    set(FINAL_OUTPUT_DIR "${PROJECT_BINARY_DIR}/$<CONFIG>")
endif()

foreach(DEP ${SLANG_DEPENDENCIES})
    add_custom_command(TARGET crt_slang POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${DEP}
            ${FINAL_OUTPUT_DIR}
        COMMENT "Copying ${DEP} to main build output directory"
    )
endforeach()

# Deploy DXC runtime if available and needed for D3D12 DXIL compilation
if(WIN32 AND DXC_DEPLOY_NEEDED)
    add_custom_command(TARGET crt_slang POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DXC_RUNTIME_DLL}"
        "${FINAL_OUTPUT_DIR}"
        COMMENT "Deploying DXC runtime for Slang D3D12 support"
    )
endif()

# Copy the shader file to the output directory so it can be found at runtime
add_custom_command(TARGET crt_slang POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/render_slang_flat.slang"
        "${FINAL_OUTPUT_DIR}/render_slang_flat.slang"
    COMMENT "Copying shader file to output directory"
)
