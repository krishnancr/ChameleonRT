# Add path to custom FindSlang.cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Slang REQUIRED)

set(CRT_SLANG_SOURCES
    render_slang.cpp
    render_slang.h
    slangdisplay.cpp
    slangdisplay.h
    render_slang_plugin.cpp
    # Only include one copy of the ImGui DX12 implementation
    # Use the one from the DXR backend
    ${CMAKE_CURRENT_SOURCE_DIR}/../dxr/imgui_impl_dx12.cpp
)

add_library(crt_slang MODULE ${CRT_SLANG_SOURCES})

set_target_properties(crt_slang PROPERTIES OUTPUT_NAME "crt_slang")

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

# Add D3D12 dependencies for Windows
if(WIN32)
    target_link_libraries(crt_slang PRIVATE d3d12.lib)
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
