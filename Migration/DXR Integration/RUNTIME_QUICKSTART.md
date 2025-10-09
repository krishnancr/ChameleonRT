# Runtime Compilation Integration - Quick Start

**Created:** October 9, 2025  
**Status:** Ready to implement!  
**Estimated Time:** 6-8 hours total

---

## 🎯 What We're Doing

**Skipping Phase 1** (CMake build-time compilation - already done before)  
**Jumping to Phase 2** (Runtime HLSL → DXIL compilation with Slang)

**Why:** More valuable, enables hot-reload, better learning experience!

---

## 📋 Prerequisites Completed

✅ **SlangShaderCompiler utility** exists in `util/slang_shader_compiler.h`  
✅ **CMake infrastructure** set up with `slang_compiler_util` library  
✅ **DXR architecture** fully documented and understood  
✅ **Simple Lambertian shader** created at `backends/dxr/shaders/simple_lambertian.hlsl`  
✅ **ShaderLibrary constructor** investigated: single blob with multiple exports

---

## 🚀 Implementation Steps

### Step 1: Add SlangShaderCompiler Member (30 min)
**Prompt:** `02-dxr-integration-runtime.md` - Prompt 1

- Add `#include "slang_shader_compiler.h"` to `render_dxr.cpp`
- Add `SlangShaderCompiler slangCompiler;` member to `RenderDXR` class
- Initialize in constructor, verify with `isValid()`
- Update `CMakeLists.txt` to link `slang_compiler_util`

**Success:** Console shows "[Slang] Compiler initialized successfully"

---

### Step 2: Test Hardcoded HLSL String (1-2 hours)
**Prompt:** `02-dxr-integration-runtime.md` - Prompt 2

- Create minimal hardcoded test shader (raygen, miss, hit)
- Call `slangCompiler.compileHLSLToDXIL()` with `ShaderStage::Library`
- Create `ShaderLibrary` from Slang bytecode instead of embedded array
- Verify pipeline creates (may not render correctly yet)

**Success:**
- Compilation succeeds with reasonable bytecode size
- Pipeline state object creates without errors
- Application runs without crashing

---

### Step 3: Load Simple Lambertian from File (1 hour)
**Prompt:** `02-dxr-integration-runtime.md` - Prompt 3

- Add `loadShaderSource()` helper function
- Load `backends/dxr/shaders/simple_lambertian.hlsl`
- Compile and use (same as Step 2)
- See basic Lambertian-shaded geometry

**Success:**
- File loads successfully
- Scene renders with gray/colored shading
- Can modify shader file and see changes (after restart)

---

### Step 4: Load Production Shaders with Includes (2-3 hours)
**Prompt:** `02-dxr-integration-runtime.md` - Prompt 4

- Implement manual `#include` resolution
- Load `backends/dxr/render_dxr.hlsl` with all dependencies
- Compile full Disney BRDF shader
- Restore all 4 entry points (RayGen, Miss, ShadowMiss, ClosestHit)

**Success:**
- All includes resolve and inline correctly
- Full production shader compiles
- Rendering matches original quality
- Textures, shadows, lighting all work

---

### Step 5: Add Hot-Reload (1-2 hours) - OPTIONAL
**Prompt:** `02-dxr-integration-runtime.md` - Prompt 5

- Add F5 key handler
- Implement `reloadShaders()` method
- Rebuild pipeline on keypress
- Edit shaders, press F5, see changes instantly!

**Success:**
- F5 triggers shader reload
- Changes visible without restart
- Can iterate on shaders quickly

---

## 🎓 Key Concepts

### Slang Compilation
```cpp
auto result = slangCompiler.compileHLSLToDXIL(
    hlsl_source,              // std::string of HLSL code
    "RayGen",                 // Primary entry point
    ShaderStage::Library      // DXR library mode
);
```

### ShaderLibrary with Runtime Bytecode
```cpp
dxr::ShaderLibrary shader_library(
    result->bytecode.data(),  // From Slang (not embedded array!)
    result->bytecode.size(),
    {L"RayGen", L"Miss", L"ClosestHit", L"ShadowMiss"}
);
```

### Include Resolution (Manual for now)
- Read main shader file
- Find `#include "file.hlsl"` lines
- Recursively load and inline
- Return concatenated string
- Later: use Slang's proper include API

---

## ✅ Success Criteria

**Minimum Success (Steps 1-3):**
- ✅ Slang initializes
- ✅ Hardcoded shader compiles
- ✅ Simple Lambertian renders

**Full Success (Step 4):**
- ✅ Production shaders compile
- ✅ Disney BRDF rendering works
- ✅ Visual quality matches original
- ✅ No D3D12 errors

**Bonus (Step 5):**
- ✅ Hot-reload functional
- ✅ Development workflow improved

---

## 📁 Files You'll Modify

1. **render_dxr.h** - Add SlangShaderCompiler member
2. **render_dxr.cpp** - Add include, initialization, compilation logic
3. **backends/dxr/CMakeLists.txt** - Link slang_compiler_util
4. **(optional) main.cpp** - Add F5 handler for hot-reload

---

## 🐛 Common Issues & Solutions

### Issue: Slang not found
**Solution:** Check `Slang_ROOT` environment variable or CMake config

### Issue: Compilation fails "unknown entry point"
**Solution:** Use `ShaderStage::Library` not individual stage enums

### Issue: Pipeline creation fails
**Solution:** Verify export names match exactly (case-sensitive!)

### Issue: Black screen
**Solution:** Check console for Slang errors, verify all shaders compiled

### Issue: Include resolution broken
**Solution:** Print each include path being loaded, check file exists

---

## 🎯 Current Focus

**START HERE:** Prompt 1 - Add SlangShaderCompiler to class

**Resources:**
- Full prompts: `Migration/prompts/02-dxr-integration-runtime.md`
- Test shader: `backends/dxr/shaders/simple_lambertian.hlsl`
- Architecture: `Migration/DXR Integration/DXR_ARCHITECTURE_DIAGRAM.md`
- API reference: `util/slang_shader_compiler.h`

---

## 💪 Let's Go!

You're all set! The prompts are detailed and tested. Just follow them one by one, validate at each step, and you'll have runtime Slang compilation working end-to-end.

**Estimated completion:** Tonight or tomorrow! 🚀

**First command:**
```powershell
code backends\dxr\render_dxr.h
code backends\dxr\render_dxr.cpp
code Migration\prompts\02-dxr-integration-runtime.md
```

Then follow **Prompt 1** step-by-step!

Good luck! 🎉
