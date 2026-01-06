# ChameleonRT - Slang Integration Fork

This repository is a fork of Will Usher's [ChameleonRT](https://github.com/Twinklebear/ChameleonRT), enhanced with [Slang](https://shader-slang.com/) shader language integration. The primary goal of this fork is to enable unified shader development across multiple ray tracing backends using Slang, eliminating the need to write and maintain separate shader code for each platform.

**Key additions in this fork:**
- Slang-based shader compilation for **DXR and Vulkan backends** (tested on Windows)
- Unified shader codebase in `shaders/unified_render.slang`

**Current Status:** Slang integration is currently implemented and tested for **DirectX Ray Tracing (DXR)** and **Vulkan** backends on **Windows only**. Support for additional backends and platforms may be added in the future.

**Original ChameleonRT:** An example path tracer supporting multiple ray tracing backends (Embree, Embree+SYCL, DXR, OptiX, Vulkan, Metal, OSPRay). This fork maintains compatibility with the original backends while adding Slang integration options for DXR and Vulkan.

The project uses [tinyobjloader](https://github.com/syoyo/tinyobjloader) for OBJ files,
[tinyexr](https://github.com/syoyo/tinyexr) for hdr environment lighting
[tinygltf](https://github.com/syoyo/tinygltf) for glTF files, and optionally
[pbrt-parser](https://github.com/ingowald/pbrt-parser) for PBRTv3 files.
Example models (San Miguel, Sponza, Rungholt) are from Morgan McGuire's [Computer Graphics Data Archive](https://casual-effects.com/data/).

[![San Miguel, Sponza and Rungholt](https://i.imgur.com/tKZYjzn.jpg)](https://i.imgur.com/pVhQK3j.jpg)

## Features

- ✅ **Physically-based path tracing** with Multiple Importance Sampling (MIS)
- ✅ **Image-Based Lighting (IBL)** with importance sampling from HDR environment maps
- ✅ **Emissive materials** - materials can emit light (area lights)
- ✅ **Progressive accumulation** with smart firefly filtering for glossy surfaces
- ✅ **Disney BRDF** (principled materials) with full PBR workflow
- ✅ **Russian roulette** path termination for efficiency
- ✅ **Texture support** for material parameters (albedo, roughness, metallic, etc.)
- ⚠️  **Opaque materials only** - transparency and refraction not yet implemented

## Rendering Pipeline

For detailed visual diagrams of the path tracing pipeline, including MIS strategy and sample accumulation, see:

**📊 [Rendering Pipeline Documentation](docs/Rendering_Pipeline.md)**

The pipeline document includes:
- High-level rendering loop overview
- Detailed path tracing with Multiple Importance Sampling (MIS)
- Sample accumulation and firefly clamping strategy
- Convergence timeline and quality recommendations

## Rendering Quality

The path tracer uses progressive accumulation. Quality improves over time:

## Known Limitations

- **Opaque surfaces only** - No transparency, refraction, or glass materials
- **No volumetric rendering** - Fog, smoke, and participating media not supported
- **No subsurface scattering** - Skin, wax, and translucent materials render as opaque
- **IBL format limitation** - Environment maps must be equirectangular/lat-long HDR (.exr format recommended)
- **No light sampling optimization** - All lights evaluated per bounce (may be slow with 100+ lights)

## Backend Status

**⚡ Actively Maintained & Fully Featured:**
- **Vulkan + Slang** - Primary backend, most tested (Linux + Windows)
- **DXR + Slang** - Full feature support, less tested (Windows only)

**⚠️ Deprecated / Potentially Broken:**
- **Legacy backends** (Embree, OptiX, Metal, OSPRay) may have outdated shader code
- Backend-specific HLSL/GLSL/Metal shaders in `backends/*/` are **out of sync** with current unified Slang shader (`unified_render.slang`)
- These legacy backends may produce **incorrect lighting** or **missing features** (no IBL, no emissive materials, broken accumulation)

**Recommendation:** Use **Vulkan** or **DXR** backends with Slang for accurate, feature-complete rendering. Legacy backends are kept for reference but not guaranteed to work correctly.

## Controls

The camera is an arcball camera that moves around the camera's focal point.

- Zoom in/out using the mousewheel. (Two-finger scroll motion on MacBook Pro etc. trackpad.)
- Click and drag to rotate.
- Right-click and drag to pan. (Two-finger click and drag on MacBook Pro etc. trackpad.)

Keys while the application window is in focus:

- Print the current camera position, center point, up vector and field of view (FOV) to the terminal by pressing the `p` key.
- Save image by pressing the `s` key.

## Command-line Options

```text
-eye <x> <y> <z>       Set the camera position
-center <x> <y> <z>    Set the camera focus point
-up <x> <y> <z>        Set the camera up vector
-fov <fovy>            Specify the camera field of view (in degrees)
-camera <n>            If the scene contains multiple cameras, specify which
                       should be used. Defaults to the first camera
-img <x> <y>           Specify the window dimensions. Defaults to 1280x720
```

## Building with Slang Support

This fork adds Slang shader language support for unified shader development across the **DXR and Vulkan backends on Windows**.

### Prerequisites

- **Windows 10/11** (required for current Slang backend support)
- [Slang](https://github.com/shader-slang/slang) compiler (tested with version **v2025.12.1**)
- [SDL2](https://www.libsdl.org/) for window management
- [GLM](https://github.com/g-truc/glm) (automatically downloaded by CMake)
- **For DXR**: Windows 10 1809 or higher, latest Windows 10 SDK, DXR-capable GPU
- **For Vulkan**: Vulkan SDK 1.2.162 or higher, Vulkan ray tracing capable GPU

### Building Slang v2025.12.1

This fork has been built and tested against **Slang version v2025.12.1**. Build Slang first:

```bash
git clone https://github.com/shader-slang/slang.git
cd slang
git checkout v2025.12.1
# Follow Slang's build instructions for Windows
# Typical build: cmake -B build -S . && cmake --build build --config Release
```

### CMake Build with Slang

To build ChameleonRT with Slang support for DXR and/or Vulkan:

```bash
# Build with DXR + Slang backend
cmake -B build -S . \
    -DENABLE_SLANG=ON \
    -DSlang_ROOT=C:/path/to/slang/build/Release \
    -DENABLE_DXR_SLANG=ON \
    -DSDL2_DIR=C:/path/to/SDL2/cmake

cmake --build build --config Release
```

```bash
# Build with Vulkan + Slang backend
cmake -B build -S . \
    -DENABLE_SLANG=ON \
    -DSlang_ROOT=C:/path/to/slang/build/Release \
    -DENABLE_VULKAN_SLANG=ON \
    -DSDL2_DIR=C:/path/to/SDL2/cmake \
    -DVULKAN_SDK=C:/path/to/VulkanSDK/version

cmake --build build --config Release
```

```bash
# Build with both DXR and Vulkan Slang backends
cmake -B build -S . \
    -DENABLE_SLANG=ON \
    -DSlang_ROOT=C:/path/to/slang/build/Release \
    -DENABLE_DXR_SLANG=ON \
    -DENABLE_VULKAN_SLANG=ON \
    -DSDL2_DIR=C:/path/to/SDL2/cmake \
    -DVULKAN_SDK=C:/path/to/VulkanSDK/version

cmake --build build --config Release
```

### Slang CMake Options

- `ENABLE_SLANG=ON` - Enable Slang shader compilation support (required for Slang backends)
- `Slang_ROOT=<path>` - Path to Slang build directory (e.g., `C:/dev/slang/build/Release`)
- `ENABLE_DXR_SLANG=ON` - Build DXR backend with Slang shaders (Windows only)
- `ENABLE_VULKAN_SLANG=ON` - Build Vulkan backend with Slang shaders (Windows only)

**Note:** The `Slang_ROOT` should point to the Slang build directory containing the compiled binaries and headers. The Slang DLL will be automatically copied to the executable directory during the build process.

### Running with Slang Backends

After building, run ChameleonRT with the Slang-enabled backends:

```bash
# Using DXR with Slang shaders
./chameleonrt.exe dxr path/to/scene.obj

# Using Vulkan with Slang shaders
./chameleonrt.exe vulkan path/to/scene.obj
```

The Slang-based backends use the same command-line interface as the original backends, with shader compilation handled transparently by Slang at runtime or build time.

## Ray Tracing Backends  

The currently implemented backends are: Embree, DXR, OptiX, Vulkan, and Metal.
When running the program, you can pick which backend you want from
those you compiled with on the command line. Running the program with no
arguments will print help information.

```
./chameleonrt <backend> <mesh.obj/gltf/glb>
```

All five ray tracing backends use [SDL2](https://www.libsdl.org/index.php) for window management
and [GLM](https://github.com/g-truc/glm) for math.
If CMake doesn't find your SDL2 install you can point it to the root
of your SDL2 directory by passing `-DSDL2_DIR=<path>`. GLM will be automatically
downloaded by CMake during the build process.

To track statistics about the number of rays traced per-second
run CMake with `-DREPORT_RAY_STATS=ON`. Tracking these statistics can
impact performance slightly.

ChameleonRT only supports per-OBJ group/mesh materials, OBJ files using per-face materials
can be reexported from Blender with the "Material Groups" option enabled.

To build with PBRT file support set the CMake option `CHAMELEONRT_PBRT_SUPPORT=ON` and pass
`-DpbrtParser_DIR=<path>` with `<path>` pointing to the CMake export files for
your build of [Ingo Wald's pbrt-parser](https://github.com/ingowald/pbrt-parser).

### Embree

Dependencies: [Embree 4](https://embree.github.io/),
[TBB](https://www.threadingbuildingblocks.org/) and [ISPC](https://ispc.github.io/).

To build the Embree backend run CMake with:

```
cmake .. -DENABLE_EMBREE=ON \
	-Dembree_DIR=<path to embree-config.cmake> \
	-DTBB_DIR=<path TBBConfig.cmake> \
	-DISPC_DIR=<path to ispc>
```

You can then pass `embree` to use the Embree backend. The `TBBConfig.cmake` will
be under `<tbb root>/cmake`, while `embree-config.cmake` is in the root of the
Embree directory.

### Embree + SYCL

Dependencies: [Embree 4](https://embree.github.io/),
[TBB](https://www.threadingbuildingblocks.org/) and Intel's oneAPI toolkit
and the nightly SYCL compiler.
Currently (4/4/2023) the Embree4 + SYCL backend requires the
[20230304 nightly](https://github.com/intel/llvm/releases/tag/sycl-nightly%2F20230304) build of
the oneAPI SYCL compiler and the latest Intel Arc GPU drivers.
I have tested with driver version 31.0.101.4255 on Windows, it seems that the Ubuntu
drivers are on an older version with their last release showing as being in Oct 2022.
The regular [oneAPI Base Toolkit](https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit.html)
is also required to provide additional library
dependencies, and the build must be run within the oneAPI tools command line
(or with the build environment scripts source'd).

After opening the oneAPI command prompt you can start powershell within it,
then to build the Embree4 + SYCL backend run CMake with:

```
cmake .. -G Ninja `
    -DCMAKE_C_COMPILER=<path to dpcpp nightly>/bin/clang.exe `
    -DCMAKE_CXX_COMPILER=<path to dpcpp nightly>/bin/clang++.exe `
    -DENABLE_EMBREE_SYCL=ON `
    -Dembree_DIR=<path to embree-config.cmake> `
	-DTBB_DIR=<path TBBConfig.cmake>
cmake --build . --config relwithdebinfo
```

You can then pass `embree_sycl` to use the Embree4 + SYCL backend. The `TBBConfig.cmake` will
be under `<tbb root>/cmake`, while `embree-config.cmake` is in the root of the
Embree directory.

Note that building the Embree4 + SYCL backend is currently incompatible with OptiX
since the compiler is not supported by CUDA. You can simply build the OptiX backend
(or the Embree4 + SYCL) backend in a separate build directory to get builds of both.

### OptiX

Dependencies: [OptiX 7.2](https://developer.nvidia.com/optix), [CUDA 11](https://developer.nvidia.com/cuda-zone).

To build the OptiX backend run CMake with:

```
cmake .. -DENABLE_OPTIX=ON
```

You can then pass `optix` to use the OptiX backend.

If CMake doesn't find your install of OptiX you can tell it where
it's installed with `-DOptiX_INSTALL_DIR`.

### DirectX Ray Tracing

If you're on Windows 10 1809 or higher, have the latest Windows 10 SDK installed and a DXR
capable GPU you can also run the DirectX Ray Tracing backend.

To build the DXR backend run CMake with:

```
cmake .. -DENABLE_DXR=ON
```

You can then pass `dxr` to use the DXR backend.

### Vulkan KHR Ray Tracing

Dependencies: [Vulkan](https://vulkan.lunarg.com/) (SDK 1.2.162 or higher)

To build the Vulkan backend run CMake with:

```
cmake .. -DENABLE_VULKAN=ON
```

You can then pass `vulkan` to use the Vulkan backend.

If CMake doesn't find your install of Vulkan you can tell it where it's
installed with `-DVULKAN_SDK`. This path should be to the specific version
of Vulkan, for example: `-DVULKAN_SDK=<path>/VulkanSDK/<version>/`

### Metal

Dependencies: [Metal](https://developer.apple.com/metal/) and a macOS 11+ device that supports ray tracing.

To build the Metal backend run CMake with:

```
cmake .. -DENABLE_METAL=ON
```

You can then pass `metal` to use the Metal backend.

### OSPRay

Dependencies: [OSPRay 2.0](http://www.ospray.org/), [TBB](https://www.threadingbuildingblocks.org/).

To build the OSPRay backend run CMake with:

```
cmake .. -DENABLE_OSPRAY=ON -Dospray_DIR=<path to osprayConfig.cmake>
```

You may also need to specify OSPRay's dependencies,
[ospcommon](https://github.com/ospray/ospcommon) and [OpenVKL](https://github.com/openvkl/openvkl),
depending on how you got or built the OSPRay binaries.
If you downloaded the OSPRay release binaries, you just need to
specify that path.

You can then pass `ospray` to use the OSPRay backend.

## Citations

### This Fork

If you use this Slang-integrated fork in your work, please cite both this fork and the original ChameleonRT:

```bibtex
@misc{chameleonrt-slang-fork,
	author = {Krishnan, C. R.},
	year = {2025},
	howpublished = {\url{https://github.com/krishnancr/ChameleonRT}},
	title = {{ChameleonRT - Slang Integration Fork}},
	note = {Fork of ChameleonRT with Slang shader language support for DXR and Vulkan}
}
```

### Original ChameleonRT

Please also cite the original ChameleonRT project:

```bibtex
@misc{chameleonrt,
	author = {Will Usher},
	year = {2019},
	howpublished = {\url{https://github.com/Twinklebear/ChameleonRT}},
	title = {{ChameleonRT}}
}
```

## Acknowledgments

This fork builds upon the excellent foundation provided by [Will Usher's ChameleonRT](https://github.com/Twinklebear/ChameleonRT). The Slang integration demonstrates the power of unified shader languages for cross-platform ray tracing development.
