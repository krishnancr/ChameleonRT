# ChameleonRT Project Instructions
Act as a senior C++ developer, with extensive experience in low graphics libraries such as Vulkan, Direct 3D 12 and Cuda.

## Project Overview

ChameleonRT is a **multi-backend ray tracing renderer** designed for cross-platform benchmarking and comparison of ray tracing performance across different APIs and GPUs.

## Core Information

### Tech Stack
- **Language:** C++17
- **Build System:** CMake 3.23+
- **Primary APIs:** DirectX Raytracing (DXR), Vulkan Ray Tracing
- **Additional Backends:** Embree, Intel OSPRay, NVIDIA OptiX, Apple Metal
- **Platform:** Windows (primary), Linux, macOS

### Project Structure
```
ChameleonRT/
├── main.cpp                 # Main application entry
├── CMakeLists.txt          # Root build configuration
├── backends/               # Rendering backend implementations
│   ├── dxr/               # DirectX Raytracing (Windows)
│   ├── vulkan/            # Vulkan Ray Tracing (cross-platform)
│   ├── embree/            # Intel Embree CPU ray tracer
│   ├── optix/             # NVIDIA OptiX
│   ├── metal/             # Apple Metal (macOS/iOS)
│   └── ospray/            # Intel OSPRay
├── util/                  # Shared utilities
├── imgui/                 # ImGui integration
└── Migration/             # Slang integration resources (current work)
```

### Build System
- **Configuration:** `cmake -B build`
- **Build:** `cmake --build build --config Debug`
- **Output:** Binaries in `build/Debug/` or `build/Release/`
- **Backend selection:** Via CMake options (`ENABLE_DXR`, `ENABLE_VULKAN`, etc.)

## Architecture Principles

### Backend Plugin System
Each backend is built as a **dynamically loaded module** (`.dll` on Windows, `.so` on Linux):
- Main executable loads backends at runtime
- Backends implement a common rendering interface
- Allows comparing different APIs with same scene/camera

### Direct API Approach
**CRITICAL:** We do NOT use graphics abstraction layers (e.g., slang-gfx):
- Each backend uses its native API directly (D3D12, Vulkan, etc.)
- Pipeline creation, resource binding, synchronization are API-specific
- This design choice enables:
  - Deep understanding of each API's characteristics
  - Fair performance benchmarking (no abstraction overhead)
  - Platform-specific optimizations
  - Clear attribution of performance differences

### Shader Compilation Strategy
- **Previously:** Each backend used native shader compilers (DXC for D3D12, glslang for Vulkan)
- **Current migration:** Integrating Slang for unified shader compilation
- **Key:** Slang is used ONLY for shader compilation, NOT for API abstraction
- **Goal:** Write shaders once in Slang language, compile to DXIL/SPIRV/Metal

## Current State & Active Work

### Slang Integration Migration (Active)
**Status:** Migrating from native shader compilers to Slang
**Location:** `Migration/` directory contains all resources
**Strategy:** 
- Phase 1: Pass-through compilation (HLSL→DXIL, GLSL→SPIRV via Slang)
- Phase 2: Migrate to native Slang shader language
- Phase 3: Advanced Slang features (interfaces, generics)

**DO NOT suggest:**
- Using slang-gfx or GFX library
- Abstracting pipeline creation through Slang
- Replacing direct API calls with abstractions

**DO suggest:**
- Using `SlangShaderCompiler` utility class from `Migration/util/`
- Compiling HLSL/GLSL/Slang to target formats
- Maintaining dual compilation paths during migration
- Following patterns from `Migration/reference/`

## Coding Conventions

### General Style
- Follow existing code patterns in the codebase
- Prefer explicit over implicit
- Use modern C++17 features where appropriate
- RAII for resource management

### Error Handling
- Check return values and handle errors gracefully
- Use exceptions for exceptional cases, not control flow
- Provide informative error messages

### CMake
- Keep platform-specific code conditional
- Use `find_package()` for dependencies
- Create imported targets for libraries
- Document non-obvious configuration

### Platform Compatibility
- Use `#ifdef` for platform-specific code
- Test on Windows primarily, consider Linux/macOS
- Be aware of path separators, case sensitivity
- Handle DLL/shared library deployment

## Key Constraints

1. **No graphics abstraction layers** - Use direct APIs
2. **Plugin architecture** - Backends are loadable modules
3. **Cross-platform where possible** - But Windows is primary
4. **Performance matters** - This is a benchmarking tool
5. **Shader compilation via Slang** - Current migration goal

## Dependencies

### Required
- SDL2 - Window management and input
- GLM - Mathematics library
- Slang - Shader compiler (current integration)

### Backend-Specific
- **DXR:** D3D12, DXC (DirectX Shader Compiler)
- **Vulkan:** Vulkan SDK, validation layers
- **Embree:** Intel Embree library
- **OptiX:** NVIDIA OptiX SDK
- **Metal:** Xcode, Metal SDK

## Common Tasks

### Building with specific backend
```powershell
cmake -B build -DENABLE_DXR=ON
cmake --build build --config Debug
```

### Running with backend
```powershell
.\build\Debug\chameleonrt.exe dxr path\to\scene.obj
```

### Adding Slang support (current work)
```powershell
cmake -B build -DENABLE_SLANG=ON -DENABLE_DXR_SLANG=ON -DSlang_ROOT=C:\dev\slang\build\Debug
```

## File Locations

- **Shader compilation:** Backend-specific, migrating to use `SlangShaderCompiler`
- **Scene loading:** `util/` directory
- **ImGui rendering:** Per-backend implementations
- **Migration docs:** `Migration/` directory
- **Reference code:** `Migration/reference/`

## When Helping

1. **Understand the context:** Check which backend/phase of work
2. **Follow existing patterns:** Look at similar code in the backend
3. **Respect architecture:** Don't suggest abstractions over direct APIs
4. **Reference migration docs:** For Slang-related work, check `Migration/INTEGRATION_GUIDE.md`
5. **Test suggestions:** Consider build implications, platform compatibility
6. **Be explicit:** Show complete code, don't use "...existing code..." placeholders

## Resources

- Migration documentation: `Migration/README.md`, `Migration/INTEGRATION_GUIDE.md`
- Build instructions: `docs/building.md` (if exists)
- Reference implementations: `Migration/reference/`
- Prompt templates: `Migration/prompts/`

---

**Remember:** This is a learning project focused on understanding ray tracing APIs deeply. Suggestions should enable learning, not hide complexity behind abstractions.
