# Find SDL2 - prefer system installation
# On Ubuntu/Debian: sudo apt install libsdl2-dev
# On Fedora/RHEL: sudo dnf install SDL2-devel

# Try pkg-config first (works well on Linux)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
endif()

# If not found via pkg-config, try CMake find_package
if(NOT SDL2_FOUND)
    find_package(SDL2 CONFIG QUIET)
endif()

if(SDL2_FOUND)
    # System SDL2 found
    message(STATUS "SDL2: Using system installation (version ${SDL2_VERSION})")
    
    # Ensure SDL2::SDL2 target exists
    if(NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 INTERFACE IMPORTED)
        if(SDL2_INCLUDE_DIRS)
            set_target_properties(SDL2::SDL2 PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}")
        endif()
        if(SDL2_LIBRARIES)
            set_target_properties(SDL2::SDL2 PROPERTIES
                INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}")
        elseif(SDL2_LINK_LIBRARIES)
            set_target_properties(SDL2::SDL2 PROPERTIES
                INTERFACE_LINK_LIBRARIES "${SDL2_LINK_LIBRARIES}")
        endif()
    endif()
else()
    message(WARNING "SDL2 not found. Please install:")
    message(WARNING "  Ubuntu/Debian: sudo apt install libsdl2-dev")
    message(WARNING "  Fedora/RHEL:   sudo dnf install SDL2-devel")
    message(WARNING "  Arch:          sudo pacman -S sdl2")
    message(FATAL_ERROR "SDL2 is required but not found.")
endif()

# Function to copy SDL2 DLL to target output directory (Windows only)
function(copy_sdl2_dll TARGET_NAME)
    # No-op on Linux/macOS where SDL2 is system-installed
    # On Windows with prebuilt SDL2, this would need to be implemented
endfunction()
