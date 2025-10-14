# Phase 1: Flat Shading

**Status:** 📋 Awaiting Phase 0 Completion  
**Duration:** 3-5 days  
**Dependencies:** Phase 0

## Objective

Create minimal Slang RT shaders and validate end-to-end RT pipeline creation for both DXR and Vulkan. Achieve normal-shaded rendering using unified Slang source.

## Sub-Phases

1. **Phase 1.1:** Compilation infrastructure test (no RT)
2. **Phase 1.2:** Minimal RT shader (barycentric colors)
3. **Phase 1.3:** DXR pipeline integration
4. **Phase 1.4:** Vulkan RT pipeline integration
5. **Phase 1.5:** Global buffer access (normal shading)

## Quick Start

1. Complete Phase 0 first
2. Read `PLAN.md` for detailed sub-phase breakdown
3. Execute sub-phases in order
4. Validate visual output matches current implementation
5. Document in `RESULTS.md`

## Deliverables

- `test_compilation_only.slang`
- `minimal_rt.slang`
- `flat_shading_with_buffers.slang`
- DXR and Vulkan integration code
- Performance benchmarks

## Success Criteria

✅ Barycentric coloring works  
✅ Normal shading matches current output  
✅ Works on both DXR and Vulkan  
✅ No validation errors  
✅ Performance within 1% of native

---

See `PLAN.md` for complete details.
