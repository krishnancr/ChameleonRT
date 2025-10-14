# Slang Integration - Planning Complete ✅

**Date:** October 14, 2025  
**Status:** Ready to Execute

---

## Overview

This directory contains the complete planning documentation for integrating Slang into ChameleonRT. The goal is to replace backend-specific shaders (HLSL for DXR, GLSL for Vulkan) with a single unified Slang shader source that compiles to both DXIL and SPIRV.

## Four-Phase Approach

### ✅ Phase 0: Utility Upgrade (2-3 hours)
**Location:** `Phase0_UtilityUpgrade/`  
Enhance `SlangShaderCompiler` utility with missing features.

**Key Deliverables:**
- Search path support
- SPIRV library compilation
- Reflection extraction

**Start:** Immediately  
**Depends On:** Nothing

---

### ⏳ Phase 1: Flat Shading (3-5 days)
**Location:** `Phase1_FlatShading/`  
Create minimal RT shaders and validate pipeline creation.

**Key Deliverables:**
- `minimal_rt.slang` - Barycentric colors
- `flat_shading_with_buffers.slang` - Normal shading
- DXR and Vulkan integration

**Start:** After Phase 0  
**Depends On:** Phase 0

---

### ⏳ Phase 2: Textures (2-3 days)
**Location:** `Phase2_Textures/`  
Add texture sampling and material system.

**Key Deliverables:**
- `single_texture.slang` - Basic sampling
- `textured_with_materials.slang` - Full materials
- UV and texture array binding

**Start:** After Phase 1  
**Depends On:** Phase 1

---

### ⏳ Phase 3: Full BRDF (3-5 days)
**Location:** `Phase3_FullBRDF/`  
Implement Disney BRDF path tracing.

**Key Deliverables:**
- BRDF modules (disney_bsdf, lights, util, rng)
- `direct_lighting.slang`
- `path_tracing.slang`

**Start:** After Phase 2  
**Depends On:** Phase 2

---

## Timeline

```
Week 1:
  Day 1: Phase 0 (complete)
  Day 2-3: Phase 1.1-1.3 (compilation, DXR)
  Day 4-5: Phase 1.4-1.5 (Vulkan, buffers)

Week 2:
  Day 1: Phase 1 validation
  Day 2-3: Phase 2 (textures)
  Day 4-5: Phase 3.1-3.2 (modules, direct lighting)

Week 3:
  Day 1-2: Phase 3.3 (path tracing)
  Day 3: Phase 3.4 (validation, optimization)
  Day 4-5: Final testing, documentation

Total: 2-3 weeks + buffer
```

---

## Documentation Structure

Each phase directory contains:

```
PhaseN_Name/
├── README.md       # Quick overview
├── PLAN.md         # Detailed task breakdown
├── RESULTS.md      # Execution results (created during phase)
├── ISSUES.md       # Known problems (created during phase)
└── (shaders)       # Actual .slang files
```

---

## Master Planning Documents

- **`SLANG_INTEGRATION_ROADMAP.md`** - High-level overview of all phases
- **`../SLANG_UTILITY_COMPARISON.md`** - Analysis of available Slang utilities
- **`../slang_shader_compiler_enhancements.md`** - Recommended compiler improvements

---

## Key Principles

### Incremental Validation
Every sub-phase must produce verifiable results:
- Visual output matches reference
- Performance within acceptable range
- No validation layer errors

### Dual Path Support
During transition, both Slang and native paths should work:
```cmake
-DENABLE_DXR_SLANG=ON    # Use Slang for DXR
-DENABLE_DXR_SLANG=OFF   # Use native HLSL (default)
```

### Reference-Based Development
- Compare with current implementation at each step
- Use Falcor for BRDF reference
- Use Slang examples for API patterns

---

## Success Metrics

### Phase 0 Success
✅ Compiler utility enhanced  
✅ All enhancement tests pass

### Phase 1 Success
✅ Normal shading identical to current  
✅ Works on DXR and Vulkan  
✅ Performance within 1%

### Phase 2 Success
✅ Textures display correctly  
✅ Materials assign properly  
✅ Performance within 1-2%

### Phase 3 Success
✅ BRDF physically correct  
✅ Path tracer matches current quality  
✅ Performance within 5%  
✅ **PRODUCTION READY**

---

## Next Actions

1. ✅ **Planning Complete** (You are here)
2. 🔨 **Execute Phase 0** - Start with utility upgrades
3. ✅ **Validate Phase 0** - Run tests, verify compilation
4. 🔨 **Execute Phase 1** - Build first RT shaders
5. ... continue through phases

---

## Questions Before Starting?

**For Slang API questions:**
- Check `c:\dev\slang\docs\user-guide\`
- Reference `c:\dev\slang\examples\ray-tracing-pipeline\`

**For BRDF reference:**
- Check `c:\dev\Falcor\Source\Falcor\Rendering\Materials\`
- Your existing `backends/dxr/disney_bsdf.hlsl`

**For integration patterns:**
- Check `backends/dxr/render_dxr.hlsl` (current implementation)
- Check `backends/vulkan/render_vulkan.cpp` (Vulkan patterns)

---

## Rollback Strategy

Each phase can be rolled back via CMake flags:
```powershell
# Disable Slang path if issues arise
cmake -B build -DENABLE_DXR_SLANG=OFF
```

Native HLSL/GLSL shaders remain in repository as fallback.

---

## Ready to Execute? 🚀

**Start with Phase 0:**
```powershell
cd Phase0_UtilityUpgrade
# Read PLAN.md
# Start with Task 0.1
```

**Good luck! The planning is solid - now it's execution time!**
