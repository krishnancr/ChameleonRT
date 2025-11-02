include(CMakeDependentOption)

# Create dependent options - only available if parent options enabled
cmake_dependent_option(ENABLE_DXR_SLANG 
    "Build DXR backend with Slang shader compilation" 
    OFF 
    "ENABLE_SLANG" 
    OFF
)

cmake_dependent_option(ENABLE_VULKAN_SLANG 
    "Build Vulkan backend with Slang shader compilation" 
    OFF 
    "ENABLE_SLANG" 
    OFF
)

# Add backends with Slang support conditionally
if(ENABLE_DXR OR ENABLE_DXR_SLANG)
    add_subdirectory(dxr)
endif()

if(ENABLE_VULKAN OR ENABLE_VULKAN_SLANG)
    add_subdirectory(vulkan)
endif()

# Other backends - check if they exist and add if enabled
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/embree/CMakeLists.txt)
    if(ENABLE_EMBREE)
        add_subdirectory(embree)
    endif()
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/embree_sycl/CMakeLists.txt)
    if(ENABLE_EMBREE_SYCL)
        add_subdirectory(embree_sycl)
    endif()
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/optix/CMakeLists.txt)
    if(ENABLE_OPTIX)
        add_subdirectory(optix)
    endif()
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ospray/CMakeLists.txt)
    if(ENABLE_OSPRAY)
        add_subdirectory(ospray)
    endif()
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/metal/CMakeLists.txt)
    if(ENABLE_METAL)
        add_subdirectory(metal)
    endif()
endif()
