# Auto-download Slang shader compiler
include(FetchContent)

# Slang version
set(SLANG_VERSION "v2025.12.1")
set(SLANG_VERSION_NUMBER "2025.12.1")

# Platform-specific download URLs
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/${SLANG_VERSION}/slang-${SLANG_VERSION_NUMBER}-windows-x86_64.zip")
    else()
        set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/${SLANG_VERSION}/slang-${SLANG_VERSION_NUMBER}-windows-x86.zip")
    endif()
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/${SLANG_VERSION}/slang-${SLANG_VERSION_NUMBER}-macos-aarch64.zip")
    else()
        set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/${SLANG_VERSION}/slang-${SLANG_VERSION_NUMBER}-macos-x86_64.zip")
    endif()
elseif(UNIX)
    set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/${SLANG_VERSION}/slang-${SLANG_VERSION_NUMBER}-linux-x86_64.tar.gz")
endif()

# Check if user specified SLANG_PATH or Slang_ROOT
if(DEFINED ENV{SLANG_PATH})
    set(Slang_ROOT $ENV{SLANG_PATH})
elseif(NOT DEFINED Slang_ROOT)
    # Auto-download Slang
    message(STATUS "Slang not found in environment, will auto-download Slang ${SLANG_VERSION}")
    
    FetchContent_Declare(
        slang_download
        URL ${SLANG_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    
    FetchContent_MakeAvailable(slang_download)
    
    # Set Slang_ROOT to downloaded location
    set(Slang_ROOT "${slang_download_SOURCE_DIR}" CACHE PATH "Slang root directory")
    
    message(STATUS "Slang: Downloaded to ${Slang_ROOT}")
endif()

# Now find Slang using the existing FindSlang.cmake module
find_package(Slang REQUIRED)

# Function to copy Slang DLLs to target output directory
function(copy_slang_dlls TARGET_NAME)
    if(WIN32 AND DEFINED Slang_ROOT)
        # Find all DLLs in the bin directory
        file(GLOB SLANG_DLLS "${Slang_ROOT}/bin/*.dll")
        if(SLANG_DLLS)
            foreach(DLL ${SLANG_DLLS})
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${DLL}"
                        "$<TARGET_FILE_DIR:${TARGET_NAME}>"
                    COMMENT "Copying ${DLL} to output directory"
                )
            endforeach()
        else()
            message(WARNING "No Slang DLLs found in ${Slang_ROOT}/bin")
        endif()
    endif()
endfunction()
