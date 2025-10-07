# Slang Shader Compiler Migration Package

## Overview
This directory contains extracted, reusable components for integrating Slang shader compilation into ChameleonRT backends **without** using slang-gfx abstraction layer.

**Purpose:** Enable direct API backends (DXR, Vulkan, Metal) to use Slang as a shader compiler while maintaining full control over pipeline creation, resource binding, and synchronization.

## What's Included

### Core Components (`util/`)
- **`slang_shader_compiler.h/cpp`** - Main shader compilation utility
  - Compile HLSL → DXIL (D3D12)
  - Compile GLSL → SPIRV (Vulkan)
  - Compile Slang → DXIL/SPIRV/Metal
  - Shader reflection support

### CMake Integration (`cmake/`)
- **`FindSlang.cmake`** - CMake module for locating Slang installation
  - Finds Slang library, headers, and compiler
  - Creates `Slang::Slang` imported target
  - Platform-aware (Windows/Linux/macOS)

### Documentation
- **`MIGRATION_PLAN.md`** - Week-by-week migration strategy
- **`LEARNINGS.md`** - Lessons learned from slang-gfx experiment
- **`INTEGRATION_GUIDE.md`** - Step-by-step backend integration instructions

### Reference (`reference/`)
- **Working CMakeLists.txt examples** - Known-good configurations
- **Integration snippets** - Copy-paste code for backends
- **Notes on what worked** - Quick troubleshooting reference

## Migration Strategy

### Phase 1: Pass-Through Compilation (Weeks 1-2)
Start by using Slang to compile existing HLSL/GLSL shaders:
- D3D12: HLSL → DXIL via Slang (instead of DXC)
- Vulkan: GLSL → SPIRV via Slang (instead of glslang)
- **No shader code changes yet** - prove Slang works as drop-in replacement

### Phase 2: First Slang Shader (Week 3)
Port one simple shader to Slang language:
- Write in `.slang` file
- Compile to both DXIL and SPIRV from same source
- Validate on both D3D12 and Vulkan

### Phase 3: Ray Tracing Shaders (Weeks 4-6)
Gradually port ray tracing shaders:
- Ray generation
- Miss shaders
- Closest hit
- Any hit (if needed)

### Phase 4: Advanced Features (Week 7+)
Leverage Slang-specific capabilities:
- Interfaces for material abstraction
- Parameter blocks
- Cross-platform shader code

## Why Not Slang-GFX?

After experimenting with slang-gfx abstraction layer (see `LEARNINGS.md`), we determined:

**Learning Goals:** Direct API work provides deeper understanding of:
- Pipeline creation differences (D3D12 vs Vulkan)
- Resource binding models
- Synchronization primitives
- Platform-specific optimizations

**Benchmarking Goals:** Eliminating abstraction layer ensures:
- Predictable performance (no hidden overhead)
- Direct comparison of API efficiency
- Clear attribution of performance differences

**Metal Reality:** Slang-gfx Metal support is experimental; ray tracing not fully implemented

**Decision:** Use Slang as shader compiler only, keep pipeline/binding/sync native to each API

## How to Use This Package

### Option A: New Integration Branch (Recommended)
```powershell
# Create clean branch from master
git checkout master
git pull
git checkout -b feature/slang-integration

# Copy Migration/ files to appropriate locations
# Follow INTEGRATION_GUIDE.md
```

### Option B: Integrate on Current Branch
```powershell
# Stay on current branch
# Copy files from Migration/ to project structure
# See INTEGRATION_GUIDE.md for details
```

## Quick Start Validation

1. **Verify CMake can find Slang:**
   ```powershell
   cd c:\dev\ChameleonRT
   cmake -B build_test -DENABLE_SLANG=ON
   ```
   Should complete without "Slang not found" errors

2. **Build utility library:**
   ```powershell
   cmake --build build_test --target chameleonrt_util
   ```
   Should compile without errors

3. **Test compilation (manual):**
   Create small test program using `SlangShaderCompiler` class

## Files Overview

```
Migration/
├── README.md (this file)
├── MIGRATION_PLAN.md          # Detailed week-by-week plan
├── LEARNINGS.md               # Slang-GFX experiment postmortem
├── INTEGRATION_GUIDE.md       # Backend integration steps
├── cmake/
│   └── FindSlang.cmake        # CMake module (production-ready)
├── util/
│   ├── slang_shader_compiler.h    # Main utility class
│   └── slang_shader_compiler.cpp  # Implementation
└── reference/
    ├── slang_CMakeLists.txt       # Working backend CMake example
    ├── root_CMakeLists.txt        # Root CMake changes needed
    ├── backends_CMakeLists.txt    # Backends CMake structure
    └── integration_notes.txt      # Quick tips and gotchas
```

## Prerequisites

- Slang SDK installed (from c:\dev\slang\build\Debug or Release)
- `SLANG_PATH` environment variable set (or specify `Slang_ROOT` to CMake)
- CMake 3.23+
- C++17 compiler

## Support

See individual documentation files for detailed information:
- Questions about strategy → `MIGRATION_PLAN.md`
- Integration issues → `INTEGRATION_GUIDE.md`
- Why this approach → `LEARNINGS.md`

## Next Steps

1. Read `MIGRATION_PLAN.md` for overall strategy
2. Follow `INTEGRATION_GUIDE.md` for backend integration
3. Start with D3D12 backend (best tooling/documentation)
4. Validate with simple HLSL pass-through before Slang shaders

---

**Created:** October 7, 2025  
**Branch:** SlangBackend  
**Purpose:** Extract reusable Slang compilation utilities for clean migration to direct API approach
