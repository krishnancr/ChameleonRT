# Prevent re-defining the package target
if(TARGET Slang::Slang)
  return()
endif()

# Set the build config for Windows/Unix
if(WIN32)
    set(Slang_BUILD_CONFIG "windows-x64")
elseif(UNIX)
    set(Slang_BUILD_CONFIG "linux-x64")
endif()

# Find the Slang include path
if (DEFINED Slang_ROOT)
    find_path(Slang_INCLUDE_DIR
        NAMES slang.h
        NO_DEFAULT_PATH
        PATHS "${Slang_ROOT}/include"
        DOC "path to Slang header files"
    )
    find_library(Slang_LIBRARY
        NAMES slang
        NO_DEFAULT_PATH
        PATHS "${Slang_ROOT}/lib"
        DOC "path to slang library files"
    )
    find_program(Slang_COMPILER
        NAMES slangc
        NO_DEFAULT_PATH
        PATHS "${Slang_ROOT}/bin/${Slang_BUILD_CONFIG}" "${Slang_ROOT}/bin" "${Slang_ROOT}"
        DOC "path to slangc compiler executable"
    )
endif()

# Fallback search if not found
find_path(Slang_INCLUDE_DIR
    NAMES slang.h
    PATHS "${Slang_ROOT}/include"
    DOC "path to Slang header files"
)
find_library(Slang_LIBRARY
    NAMES slang
    PATHS "${Slang_ROOT}/lib"
    DOC "path to slang library files"
)
find_program(Slang_COMPILER
    NAMES slangc
    PATHS "${Slang_ROOT}/bin/${Slang_BUILD_CONFIG}" "${Slang_ROOT}/bin" "${Slang_ROOT}"
    DOC "path to slangc compiler executable"
)

set(Slang_LIBRARIES ${Slang_LIBRARY})
set(Slang_INCLUDE_DIRS ${Slang_INCLUDE_DIR})

get_filename_component(Slang_LIBRARY_DIR ${Slang_LIBRARY} DIRECTORY)
if(WIN32)
    file(GLOB Slang_LIBRARY_DLL ${Slang_LIBRARY_DIR}/*.dll)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Slang
    DEFAULT_MSG
    Slang_INCLUDE_DIRS
    Slang_LIBRARIES
)

if(Slang_FOUND)
    add_library(Slang::Slang UNKNOWN IMPORTED)
    set_target_properties(Slang::Slang PROPERTIES
        IMPORTED_LOCATION ${Slang_LIBRARY}
        IMPORTED_LINK_INTERFACE_LIBRARIES "${Slang_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${Slang_INCLUDE_DIRS}"
    )
endif()

mark_as_advanced(
    Slang_INCLUDE_DIRS
    Slang_INCLUDE_DIR
    Slang_LIBRARIES
    Slang_LIBRARY
    Slang_LIBRARY_DIR
    Slang_LIBRARY_DLL
)
