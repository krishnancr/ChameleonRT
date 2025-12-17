# Auto-download SDL2 if not found in system
include(FetchContent)

# SDL2 version and download URLs
set(SDL2_VERSION "2.30.9")

if(WIN32)
    set(SDL2_URL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip")
    set(SDL2_HASH "SHA256=59e6d4fcb08bb644c4ce93c62bfc05a1d4db7f3a5e3ad3c18ecc3a5e15b31c50")
elseif(APPLE)
    set(SDL2_URL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-${SDL2_VERSION}.dmg")
    set(SDL2_HASH "")  # Add hash if needed
elseif(UNIX)
    set(SDL2_URL "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-mingw.tar.gz")
    set(SDL2_HASH "")  # Add hash if needed
endif()

# Try to find SDL2 first
find_package(SDL2 CONFIG QUIET)

if(NOT SDL2_FOUND)
    message(STATUS "SDL2 not found in system, will auto-download SDL2 ${SDL2_VERSION}")
    
    FetchContent_Declare(
        SDL2
        URL ${SDL2_URL}
        URL_HASH ${SDL2_HASH}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    
    FetchContent_MakeAvailable(SDL2)
    
    if(WIN32)
        # Set SDL2 paths for Windows VC distribution
        set(SDL2_INCLUDE_DIR "${sdl2_SOURCE_DIR}/SDL2-${SDL2_VERSION}/include" CACHE PATH "SDL2 include directory")
        
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(SDL2_LIB_DIR "${sdl2_SOURCE_DIR}/SDL2-${SDL2_VERSION}/lib/x64")
        else()
            set(SDL2_LIB_DIR "${sdl2_SOURCE_DIR}/SDL2-${SDL2_VERSION}/lib/x86")
        endif()
        
        # Create imported target
        add_library(SDL2::SDL2 SHARED IMPORTED)
        set_target_properties(SDL2::SDL2 PROPERTIES
            IMPORTED_LOCATION "${SDL2_LIB_DIR}/SDL2.dll"
            IMPORTED_IMPLIB "${SDL2_LIB_DIR}/SDL2.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
        )
        
        add_library(SDL2::SDL2main STATIC IMPORTED)
        set_target_properties(SDL2::SDL2main PROPERTIES
            IMPORTED_LOCATION "${SDL2_LIB_DIR}/SDL2main.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
        )
        
        # Store DLL path for copying to output directory
        set(SDL2_DLL "${SDL2_LIB_DIR}/SDL2.dll" CACHE INTERNAL "SDL2 DLL path")
        
        message(STATUS "SDL2: Downloaded to ${sdl2_SOURCE_DIR}")
        message(STATUS "SDL2: Include dir: ${SDL2_INCLUDE_DIR}")
        message(STATUS "SDL2: Lib dir: ${SDL2_LIB_DIR}")
    endif()
    
    set(SDL2_FOUND TRUE CACHE BOOL "SDL2 found" FORCE)
endif()

# Function to copy SDL2 DLL to target output directory (Windows only)
function(copy_sdl2_dll TARGET_NAME)
    if(WIN32 AND DEFINED SDL2_DLL)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_DLL}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>"
            COMMENT "Copying SDL2.dll to output directory"
        )
    endif()
endfunction()
