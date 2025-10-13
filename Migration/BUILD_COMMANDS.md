# ChameleonRT Build Commands with Dependencies

This document provides complete build commands including all dependencies for ChameleonRT with Slang support.

## Dependencies Overview

### Required (All Backends)
- **CMake 3.23+** - Build system
- **SDL2** - Window management and input
- **GLM** - Math library (auto-downloaded by CMake)
- **Slang** - Shader compiler (new integration)

### Backend-Specific (Optional)
- **DXR**: Windows 10 SDK (comes with Visual Studio)
- **Vulkan**: Vulkan SDK 1.2.162+
- **Embree**: Embree 4.x, TBB, ISPC
- **OptiX**: OptiX 7.x, CUDA 11+
- **Metal**: macOS 11+, Xcode

---

## Quick Start (Windows)

### Option A: Using Manual SDL2 Installation (Your Setup)

```powershell
# Set environment variables (optional, but recommended)
$env:SDL2_PATH = "C:\dev\SDL2\cmake"
$env:SLANG_PATH = "C:\dev\slang\build\Debug"

# Check if all dependencies are installed
cd C:\dev\ChameleonRT
.\build_with_slang.ps1 -CheckDeps

# Clean build with Slang enabled
.\build_with_slang.ps1

# Build only (incremental)
.\build_with_slang.ps1 -Build

# Release build
.\build_with_slang.ps1 -Config Release
```

### Option B: Using vcpkg for SDL2

```powershell
# If you don't have vcpkg, install it first:
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat

# Install SDL2 with Vulkan support
vcpkg install sdl2[vulkan]:x64-windows

# Set environment variable (optional)
$env:VCPKG_ROOT = "C:\vcpkg"

# Build
cd C:\dev\ChameleonRT
.\build_with_slang.ps1
```

---

## Manual Build Commands

### Minimal Build (DXR + Slang)

**With Manual SDL2:**

```powershell
# Configure
cmake -B build `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DSDL2_DIR="C:\dev\SDL2" `
    -DENABLE_SLANG=ON `
    -DSlang_ROOT="C:\dev\slang\build\Debug" `
    -DENABLE_DXR=ON `
    -DCMAKE_SYSTEM_VERSION="10.0.19042"

# Build
cmake --build build --config Debug --parallel

# Run
cd build\Debug
.\chameleonrt.exe dxr ..\..\test_cube.obj
```

**With vcpkg SDL2:**

```powershell
# Configure
cmake -B build `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake" `
    -DENABLE_SLANG=ON `
    -DSlang_ROOT="C:\dev\slang\build\Debug" `
    -DENABLE_DXR=ON `
    -DCMAKE_SYSTEM_VERSION="10.0.19042"

# Build
cmake --build build --config Debug --parallel

# Run
cd build\Debug
.\chameleonrt.exe dxr ..\..\test_cube.obj
```

### Full Build (All Backends)

```powershell
# Prerequisites:
# - SDL2 via vcpkg: vcpkg install sdl2[vulkan]:x64-windows
# - Vulkan SDK: https://vulkan.lunarg.com/
# - Embree, TBB, ISPC (for Embree backend)
# - OptiX SDK + CUDA (for OptiX backend)

cmake -B build `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake" `
    -DENABLE_SLANG=ON `
    -DSlang_ROOT="C:\dev\slang\build\Debug" `
    -DENABLE_DXR=ON `
    -DENABLE_VULKAN=ON `
    -DENABLE_EMBREE=ON `
    -DENABLE_OPTIX=ON `
    -DVULKAN_SDK="C:\VulkanSDK\1.3.236.0" `
    -Dembree_DIR="C:\dev\embree-4.0.1\lib\cmake\embree-4.0.1" `
    -DTBB_DIR="C:\dev\tbb\lib\cmake\tbb" `
    -DISPC_DIR="C:\dev\ispc\bin" `
    -DOptiX_INSTALL_DIR="C:\ProgramData\NVIDIA Corporation\OptiX SDK 7.7.0" `
    -DCMAKE_SYSTEM_VERSION="10.0.19042"

cmake --build build --config Debug --parallel
```

---

## Environment Variables (Optional)

Set these to simplify build commands:

**For Manual SDL2 Installation:**

```powershell
# Add to PowerShell profile or set temporarily
$env:SDL2_DIR = "C:\dev\SDL2"
$env:SLANG_PATH = "C:\dev\slang\build\Debug"
```

**For vcpkg SDL2:**

```powershell
# Add to PowerShell profile or set temporarily
$env:VCPKG_ROOT = "C:\vcpkg"
$env:SLANG_PATH = "C:\dev\slang\build\Debug"
$env:VULKAN_SDK = "C:\VulkanSDK\1.3.236.0"  # If using Vulkan
```

Then configure with:
```powershell
cmake -B build -DENABLE_SLANG=ON -DENABLE_DXR=ON
```

The build script will automatically detect and use these paths.

---

## Troubleshooting

### "Could not find SDL2"

**Solution 1: Using Manual SDL2 Installation**
```powershell
# Download SDL2 development libraries from:
# https://github.com/libsdl-org/SDL/releases
# Extract to C:\dev\SDL2

# Then configure with SDL2_DIR
cmake -B build -DSDL2_DIR="C:\dev\SDL2" ...

# Or set environment variable
$env:SDL2_DIR = "C:\dev\SDL2"
```

**Solution 2: Using vcpkg**
```powershell
# Install via vcpkg
vcpkg install sdl2[vulkan]:x64-windows

# Then configure with vcpkg toolchain
cmake -B build -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake" ...
```

### "Could not find Slang"

**Solution:**
```powershell
# Set Slang_ROOT
cmake -B build -DSlang_ROOT="C:\dev\slang\build\Debug" ...

# Or set environment variable
$env:SLANG_PATH = "C:\dev\slang\build\Debug"
```

### "slang.dll not found at runtime"

**Solution:**
The CMakeLists.txt should auto-copy slang.dll via POST_BUILD command.
If it doesn't, manually copy:
```powershell
Copy-Item "C:\dev\slang\build\Debug\bin\slang.dll" "build\Debug\"
```

---

## Build Configurations

### Development (Default)
```powershell
.\build_with_slang.ps1 -Config Debug
```

### Release (Optimized)
```powershell
.\build_with_slang.ps1 -Config Release
```

### Release with Debug Info
```powershell
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo ...
cmake --build build --config RelWithDebInfo
```

---

## Next Steps After Build

1. **Verify slang.dll deployment:**
   ```powershell
   Test-Path build\Debug\slang.dll  # Should be True
   ```

2. **Test the build:**
   ```powershell
   cd build\Debug
   .\chameleonrt.exe dxr ..\..\test_cube.obj
   ```

3. **Enable Slang compilation in backends:**
   - Currently Phase 1, Part 1 is complete (infrastructure only)
   - Next: Integrate SlangShaderCompiler into DXR backend (Prompt 2)
   - Use `-DENABLE_DXR_SLANG=ON` once backend integration is complete

---

## Reference: GitHub Actions Build

This is how the official CI builds on Windows:

```yaml
# Install SDL2
vcpkg install sdl2[vulkan]:x64-windows

# Configure
cmake -A x64 -G "Visual Studio 17 2022" `
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" `
    -DENABLE_EMBREE=ON `
    -DENABLE_DXR=ON `
    -DENABLE_VULKAN=ON `
    -DENABLE_OPTIX=ON `
    ...additional backend paths...

# Build
cmake --build . --config Release --target install
```

---

## Summary: Minimum Required Commands

For **DXR-only build with Slang** (your current setup):

```powershell
# No setup needed - you already have:
# - SDL2 at C:\dev\SDL2
# - Slang at C:\dev\slang\build\Debug

# Just build:
cd C:\dev\ChameleonRT
.\build_with_slang.ps1

# Or manually:
cmake -B build -DSDL2_DIR="C:\dev\SDL2" -DSlang_ROOT="C:\dev\slang\build\Debug" -DENABLE_SLANG=ON -DENABLE_DXR=ON
cmake --build build --config Debug --parallel
```

That's it! The script handles everything else.

---

## Phase 1 Build Notes (Global Buffer Refactor)

**Current Status:** Phase 1 Complete (Shader declarations added)

**Build Command for Incremental Phases:**
```powershell
# Build only DXR backend (faster iteration)
cmake --build build --config Debug --target crt_dxr

# Clean build if CMakeLists.txt changed
cmake --build build --config Debug --target crt_dxr --clean-first
```

**Important Notes:**
- `-Vd` flag added to CMakeLists.txt to disable DXC validation
- Required because unbounded texture array (`Texture2D textures[]` at t3) conflicts with global buffers (t20-t22)
- Global buffers declared but NOT bound yet (Phase 2 will create them)
- `ClosestHit_GlobalBuffers` shader exists but NOT used yet (Phase 4 will switch to it)
- Old `ClosestHit` shader still active - parallel implementation approach

**Validation:**
✅ Shader compiles successfully
✅ DLL builds: `build\Debug\crt_dxr.dll`
✅ Embedded DXIL: `build\backends\dxr\render_dxr_embedded_dxil.h`
✅ Both ClosestHit shaders present in shader library
