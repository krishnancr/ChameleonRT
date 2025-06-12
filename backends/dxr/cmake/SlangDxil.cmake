# This CMake module provides the functionality to compile Slang shaders to DXIL
# and embed them into C++ headers for use in the application.

# The function takes similar arguments to add_dxil_embed_library but uses the Slang compiler
# instead of the DXC compiler to generate the DXIL bytecode.
function(add_slang_dxil_embed_library)
    set(options INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
    cmake_parse_arguments(PARSE_ARGV 1 SLANG "" "" "${options}")

    set(SLANG_INCLUDE_DIRS "")
    foreach (inc ${SLANG_INCLUDE_DIRECTORIES})
        file(TO_NATIVE_PATH "${inc}" native_path)
        list(APPEND SLANG_INCLUDE_DIRS "-I${native_path}")
    endforeach()

    set(SLANG_COMPILE_DEFNS "")
    foreach (def ${SLANG_COMPILE_DEFINITIONS})
        list(APPEND SLANG_COMPILE_DEFNS "-D${def}")
    endforeach()

    set(SLANG_LIB ${ARGV0})
    set(SLANG_SRCS "")
    foreach (shader ${SLANG_UNPARSED_ARGUMENTS})
        get_filename_component(SHADER_FULL_PATH ${shader} ABSOLUTE)
        list(APPEND SLANG_SRCS ${SHADER_FULL_PATH})
    endforeach()
    list(GET SLANG_SRCS 0 MAIN_SHADER)

    # We only compile the main shader, but use the rest to
    # set the target dependencies properly
    get_filename_component(FNAME ${MAIN_SHADER} NAME_WE)
    set(DXIL_EMBED_FILE "${CMAKE_CURRENT_BINARY_DIR}/${FNAME}_embedded_dxil.h")

    # Call slangc compiler to compile the shader to DXIL bytecode and embed it in a header file
    add_custom_command(OUTPUT ${DXIL_EMBED_FILE}
        COMMAND ${Slang_COMPILER} ${MAIN_SHADER}
        -target dxil 
        -profile lib_6_3
        -capability dxil_lib
        -o ${DXIL_EMBED_FILE}
        -source-embed-style binary-text
        -source-embed-name ${FNAME}_dxil
        ${SLANG_INCLUDE_DIRS} ${SLANG_COMPILE_DEFNS} ${SLANG_COMPILE_OPTIONS}
        DEPENDS ${SLANG_SRCS}
        COMMENT "Compiling and embedding ${MAIN_SHADER} as ${FNAME}_dxil using Slang")

    # This is needed for some reason to get CMake to generate the file properly
    # and not screw up the build, because the original approach of just
    # setting target_sources on ${SLANG_LIB} stopped working once this got put
    # in some subdirectory. CMake ¯\_(ツ)_/¯
    set(SLANG_CMAKE_CUSTOM_WRAPPER ${SLANG_LIB}_custom_target)
    add_custom_target(${SLANG_CMAKE_CUSTOM_WRAPPER} ALL DEPENDS ${DXIL_EMBED_FILE})

    add_library(${SLANG_LIB} INTERFACE)
    add_dependencies(${SLANG_LIB} ${SLANG_CMAKE_CUSTOM_WRAPPER})
    target_include_directories(${SLANG_LIB} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
endfunction()
