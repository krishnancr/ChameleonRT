cmake_minimum_required(VERSION 3.23)
project(chameleonrt)

if (NOT WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
endif()

add_definitions(-DNOMINMAX
    -DGLM_ENABLE_EXPERIMENTAL
    -DWIN32_LEAN_AND_MEAN
    -DSDL_MAIN_HANDLED)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")
# Output all render plugins to the root binary dir where the executable is
# so setting runtime paths is easier
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

find_package(Threads REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
# Include glm as an external project
include(cmake/glm.cmake)
include(cmake/package.cmake)

include(CMakeDependentOption)

# ============================================================================
# Slang Shader Compiler Support
# ============================================================================

option(ENABLE_SLANG "Use Slang for shader compilation" OFF)

if(ENABLE_SLANG)
    # Allow user to specify Slang_ROOT from environment variable
    if(DEFINED ENV{SLANG_PATH})
        set(Slang_ROOT $ENV{SLANG_PATH})
    endif()
    
    # Find the Slang package (uses cmake/FindSlang.cmake)
    find_package(Slang REQUIRED)
    
    # Create Slang compiler utility library
    add_library(slang_compiler_util STATIC
        util/slang_shader_compiler.h
        util/slang_shader_compiler.cpp
    )
    
    # Link to Slang library
    target_link_libraries(slang_compiler_util
        PUBLIC Slang::Slang
    )
    
    # Set include directories
    target_include_directories(slang_compiler_util
        PUBLIC 
            ${CMAKE_CURRENT_SOURCE_DIR}/util
            ${Slang_INCLUDE_DIRS}
    )
    
    # Define macro to indicate Slang is available
    target_compile_definitions(slang_compiler_util
        PUBLIC CHAMELEONRT_USE_SLANG=1
    )
    
    # Set C++ standard for the utility library (requires C++17 for std::optional)
    set_target_properties(slang_compiler_util PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    
    message(STATUS "Slang shader compiler support enabled")
    message(STATUS "Slang include directory: ${Slang_INCLUDE_DIRS}")
    message(STATUS "Slang library: ${Slang_LIBRARIES}")
endif()

add_subdirectory(imgui)
add_subdirectory(util)
add_subdirectory(backends)

option(REPORT_RAY_STATS "Track and report rays/second. May incur a slight rendering performance penalty" OFF)

add_executable(chameleonrt main.cpp)

set_target_properties(chameleonrt PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)

target_link_libraries(chameleonrt PUBLIC
    util
    display)

# ============================================================================
# Slang DLL Deployment (Windows)
# ============================================================================

if(WIN32 AND ENABLE_SLANG)
    # Copy slang.dll to the executable directory so it can be found at runtime
    add_custom_command(TARGET chameleonrt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${Slang_ROOT}/bin/slang.dll"
            "$<TARGET_FILE_DIR:chameleonrt>"
        COMMENT "Deploying slang.dll to executable directory"
    )
endif()

# ============================================================================
# Slang Shader Deployment
# ============================================================================

if(ENABLE_SLANG)
    # List of Slang shader files to deploy
    set(SLANG_SHADER_FILES
        minimal_rt.slang
    )
    
    # Create shaders directory in output
    add_custom_command(TARGET chameleonrt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:chameleonrt>/shaders"
        COMMENT "Creating shaders directory in output"
    )
    
    # Copy each Slang shader file to the output directory
    foreach(SHADER_FILE ${SLANG_SHADER_FILES})
        # Check if source shader file exists
        if(EXISTS "${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE}")
            add_custom_command(TARGET chameleonrt POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE}"
                    "$<TARGET_FILE_DIR:chameleonrt>/shaders/"
                COMMENT "Copying ${SHADER_FILE} to output directory"
            )
        else()
            message(WARNING "Slang shader file not found: ${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE}")
        endif()
    endforeach()
    
    message(STATUS "Slang shader deployment configured for ${CMAKE_BUILD_TYPE} builds")
endif()

install(TARGETS chameleonrt
        RUNTIME DESTINATION bin)

