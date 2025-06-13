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
set(DXIL_BINARY "${CMAKE_CURRENT_BINARY_DIR}/${FNAME}.dxil")
set(DXIL_EMBED_FILE "${CMAKE_CURRENT_BINARY_DIR}/${FNAME}_embedded_dxil.h")

# Step 1: Compile Slang to DXIL binary
# - Use `slangc` to compile .slang source to .dxil binary file
# - This leverages Slang's superior language features (generics, modules, cross-platform)
# - Output is raw DXIL bytecode, same as what DXC would produce
# Step 1: Generate DXIL binary
add_custom_command(OUTPUT ${DXIL_BINARY}
    COMMAND ${Slang_COMPILER} ${MAIN_SHADER}
    -target dxil 
    -profile lib_6_3
    -o ${DXIL_BINARY}
    ${HLSL_INCLUDE_DIRS} ${HLSL_COMPILE_DEFNS} ${DXIL_COMPILE_OPTIONS}
    DEPENDS ${HLSL_SRCS}
    COMMENT "Compiling ${MAIN_SHADER} to DXIL with Slang")

# Step 2: Convert DXIL binary to header file
# Step 2: Convert DXIL binary to C++ header
# - Read the binary .dxil file and convert bytes to C++ array syntax
# - Generate the exact same header format that DXC's `-Fh/-Vn` would produce
# - Use CMake to create: `const unsigned char varname[] = {0x44, 0x58, ...};`
add_custom_command(OUTPUT ${DXIL_EMBED_FILE}
    COMMAND ${CMAKE_COMMAND} -E echo "const unsigned char ${FNAME}_dxil[] = {" > ${DXIL_EMBED_FILE}
    COMMAND ${CMAKE_COMMAND} -DINPUT_FILE=${DXIL_BINARY} -DOUTPUT_FILE=${DXIL_EMBED_FILE} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bin_to_header.cmake
    COMMAND ${CMAKE_COMMAND} -E echo "};" >> ${DXIL_EMBED_FILE}
    COMMAND ${CMAKE_COMMAND} -E echo "const unsigned int ${FNAME}_dxil_len = sizeof(${FNAME}_dxil);" >> ${DXIL_EMBED_FILE}
    DEPENDS ${DXIL_BINARY}
    COMMENT "Converting ${DXIL_BINARY} to header file")















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
