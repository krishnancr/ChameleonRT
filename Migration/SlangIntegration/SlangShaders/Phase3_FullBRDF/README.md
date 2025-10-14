# Phase 3: Full BRDF

**Status:** 📋 Awaiting Phase 2 Completion  
**Duration:** 3-5 days  
**Dependencies:** Phase 2

## Objective

Implement complete Disney BRDF-based path tracing in Slang, achieving full feature parity with current implementation.

## Sub-Phases

1. **Phase 3.1:** Import BRDF modules from Falcor
2. **Phase 3.2:** Direct lighting with shadow rays
3. **Phase 3.3:** Path tracing (recursive rays)
4. **Phase 3.4:** Validation and optimization

## Quick Start

1. Complete Phase 2 first
2. Read `PLAN.md` for detailed task breakdown
3. Study Falcor's BRDF implementation
4. Implement modules incrementally
5. Validate against current path tracer
6. Document in `RESULTS.md`

## Deliverables

- BRDF modules: `disney_bsdf.slang`, `lights.slang`, `util.slang`, `lcg_rng.slang`
- `direct_lighting.slang`
- `path_tracing.slang`
- Performance analysis
- Visual quality comparison

## Success Criteria

✅ Physically plausible rendering  
✅ Global illumination correct  
✅ Performance within 5% of current  
✅ Works on DXR and Vulkan  
✅ Production ready

---

See `PLAN.md` for complete details.
