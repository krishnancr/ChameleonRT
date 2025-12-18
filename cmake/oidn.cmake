# cmake/oidn.cmake
# Auto-download Intel Open Image Denoise (OIDN) from GitHub releases

include(ExternalProject)

set(OIDN_VERSION "2.3.3")

# Determine platform-specific download URL and archive format
if(WIN32)
    set(OIDN_PLATFORM "x64.windows")
    set(OIDN_ARCHIVE_EXT "zip")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        set(OIDN_PLATFORM "arm64.macos")
    else()
        set(OIDN_PLATFORM "x86_64.macos")
    endif()
    set(OIDN_ARCHIVE_EXT "tar.gz")
else()
    # Linux
    set(OIDN_PLATFORM "x86_64.linux")
    set(OIDN_ARCHIVE_EXT "tar.gz")
endif()

set(OIDN_ARCHIVE_NAME "oidn-${OIDN_VERSION}.${OIDN_PLATFORM}.${OIDN_ARCHIVE_EXT}")
set(OIDN_DOWNLOAD_URL "https://github.com/RenderKit/oidn/releases/download/v${OIDN_VERSION}/${OIDN_ARCHIVE_NAME}")
set(OIDN_EXTRACTED_DIR "oidn-${OIDN_VERSION}.${OIDN_PLATFORM}")

message(STATUS "OIDN: Will download from ${OIDN_DOWNLOAD_URL}")

# Download and extract OIDN
ExternalProject_Add(oidn_ext
    PREFIX oidn
    DOWNLOAD_DIR oidn
    STAMP_DIR oidn/stamp
    SOURCE_DIR oidn/src
    BINARY_DIR oidn
    URL "${OIDN_DOWNLOAD_URL}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_ALWAYS OFF
)

# Set paths to OIDN after extraction
# Files extract directly to SOURCE_DIR (not in subdirectory)
set(OIDN_ROOT ${CMAKE_CURRENT_BINARY_DIR}/oidn/src)
set(OIDN_INCLUDE_DIR ${OIDN_ROOT}/include)
set(OIDN_LIB_DIR ${OIDN_ROOT}/lib)
set(OIDN_BIN_DIR ${OIDN_ROOT}/bin)

# Create imported target for OIDN
add_library(OpenImageDenoise SHARED IMPORTED GLOBAL)
add_dependencies(OpenImageDenoise oidn_ext)

if(WIN32)
    set_target_properties(OpenImageDenoise PROPERTIES
        IMPORTED_LOCATION "${OIDN_BIN_DIR}/OpenImageDenoise.dll"
        IMPORTED_IMPLIB "${OIDN_LIB_DIR}/OpenImageDenoise.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${OIDN_INCLUDE_DIR}"
    )
    
    # List of OIDN DLLs that need to be copied to output directory
    set(OIDN_DLLS
        "${OIDN_BIN_DIR}/OpenImageDenoise.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_core.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_device_cpu.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_device_cuda.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_device_hip.dll"
        "${OIDN_BIN_DIR}/OpenImageDenoise_device_sycl.dll"
        "${OIDN_BIN_DIR}/tbb12.dll"
        CACHE INTERNAL "OIDN DLLs to copy"
    )
    
elseif(APPLE)
    set_target_properties(OpenImageDenoise PROPERTIES
        IMPORTED_LOCATION "${OIDN_LIB_DIR}/libOpenImageDenoise.dylib"
        INTERFACE_INCLUDE_DIRECTORIES "${OIDN_INCLUDE_DIR}"
    )
    
    # List of OIDN dylibs
    file(GLOB OIDN_DYLIBS "${OIDN_LIB_DIR}/*.dylib")
    set(OIDN_DLLS ${OIDN_DYLIBS} CACHE INTERNAL "OIDN dylibs to copy")
    
else()
    # Linux
    set_target_properties(OpenImageDenoise PROPERTIES
        IMPORTED_LOCATION "${OIDN_LIB_DIR}/libOpenImageDenoise.so"
        INTERFACE_INCLUDE_DIRECTORIES "${OIDN_INCLUDE_DIR}"
    )
    
    # List of OIDN shared libraries
    file(GLOB OIDN_SOS "${OIDN_LIB_DIR}/*.so*")
    set(OIDN_DLLS ${OIDN_SOS} CACHE INTERNAL "OIDN shared libs to copy")
endif()

# Function to copy OIDN DLLs to target output directory
function(copy_oidn_dlls TARGET_NAME)
    if(WIN32)
        foreach(DLL ${OIDN_DLLS})
            # Note: Don't check if file exists - it may not exist at configure time
            # since ExternalProject_Add downloads at build time
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>"
                COMMENT "Copying ${DLL} to output directory"
            )
        endforeach()
    elseif(APPLE)
        foreach(DYLIB ${OIDN_DLLS})
            # Note: Don't check if file exists - it may not exist at configure time
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DYLIB}"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>"
                COMMENT "Copying ${DYLIB} to output directory"
            )
        endforeach()
    else()
        # Linux - copy to lib directory next to executable
        foreach(SO ${OIDN_DLLS})
            # Note: Don't check if file exists - it may not exist at configure time
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SO}"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>"
                COMMENT "Copying ${SO} to output directory"
            )
        endforeach()
    endif()
endfunction()

message(STATUS "OIDN: Include dir will be ${OIDN_INCLUDE_DIR}")
message(STATUS "OIDN: Lib dir will be ${OIDN_LIB_DIR}")
