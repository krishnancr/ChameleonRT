# Quick Build Reference - Your Setup

This is a quick reference for your specific ChameleonRT + Slang development setup.

## Your Environment

```
SDL2:  C:\dev\SDL2
Slang: C:\dev\slang\build\Debug (or Release)
Repo:  C:\dev\ChameleonRT
```

---

## Build Commands

### Using the Build Script (Recommended)

```powershell
cd C:\dev\ChameleonRT

# Check dependencies
.\build_with_slang.ps1 -CheckDeps

# Clean build (Debug)
.\build_with_slang.ps1

# Incremental build
.\build_with_slang.ps1 -Build

# Release build
.\build_with_slang.ps1 -Config Release
```

### Manual CMake Commands

**Debug Build:**
```powershell
cmake -B build `
    -G "Visual Studio 17 2022" -A x64 `
    -DSDL2_DIR="C:\dev\SDL2" `
    -DSlang_ROOT="C:\dev\slang\build\Debug" `
    -DENABLE_SLANG=ON `
    -DENABLE_DXR=ON

cmake --build build --config Debug --parallel
```

**Release Build:**
```powershell
cmake -B build `
    -G "Visual Studio 17 2022" -A x64 `
    -DSDL2_DIR="C:\dev\SDL2" `
    -DSlang_ROOT="C:\dev\slang\build\Release" `
    -DENABLE_SLANG=ON `
    -DENABLE_DXR=ON

cmake --build build --config Release --parallel
```

---

## Running

```powershell
# Debug
cd build\Debug
.\chameleonrt.exe dxr ..\..\test_cube.obj

# Release
cd build\Release
.\chameleonrt.exe dxr ..\..\test_cube.obj
```

---

## Optional: Set Environment Variables

Add to your PowerShell profile (`$PROFILE`):

```powershell
$env:SDL2_DIR = "C:\dev\SDL2"
$env:SLANG_PATH = "C:\dev\slang\build\Debug"
```

Then you can just run:
```powershell
.\build_with_slang.ps1
```

---

## Verify Build

After building, check:

```powershell
# These files should exist:
Test-Path build\Debug\chameleonrt.exe    # Should be True
Test-Path build\Debug\slang.dll          # Should be True (auto-copied)
Test-Path build\Debug\SDL2.dll           # Should be True (from SDL2 path)
```

---

## Common Tasks

**Clean rebuild:**
```powershell
.\build_with_slang.ps1
```

**Rebuild after code changes:**
```powershell
.\build_with_slang.ps1 -Build
```

**Switch to Release:**
```powershell
.\build_with_slang.ps1 -Config Release -Build
```

**Reconfigure CMake:**
```powershell
Remove-Item -Recurse -Force build
.\build_with_slang.ps1
```

---

## Next Steps (Migration Phase 1)

✅ **Part 1 Complete:** CMake infrastructure + utility files
- `ENABLE_SLANG` option added
- `slang_compiler_util` library created
- Build script with dependency checking

🔄 **Part 2 Next:** Backend integration (Prompt 2)
- Update `backends/CMakeLists.txt` for per-backend options
- Add `ENABLE_DXR_SLANG`, `ENABLE_VULKAN_SLANG` options
- Integrate `SlangShaderCompiler` into DXR backend

See: `Migration/prompts/01-cmake-setup.md` → Prompt 2

---

## Troubleshooting

**CMake doesn't find SDL2:**
```powershell
# Verify SDL2 structure:
dir C:\dev\SDL2\lib\x64\SDL2.dll
dir C:\dev\SDL2\include\SDL.h

# Or set explicitly:
cmake -B build -DSDL2_DIR="C:\dev\SDL2" ...
```

**slang.dll not found at runtime:**
```powershell
# Should be auto-copied. If not, manually copy:
Copy-Item "C:\dev\slang\build\Debug\bin\slang.dll" "build\Debug\"
```

**Wrong Slang version (Debug vs Release):**
```powershell
# Match ChameleonRT config with Slang config:
.\build_with_slang.ps1 -Config Debug   # Uses C:\dev\slang\build\Debug
.\build_with_slang.ps1 -Config Release # Uses C:\dev\slang\build\Release
```
