# IBL Quick Reference Card

## Essential Links

### Download Now
- TinyEXR: https://raw.githubusercontent.com/syoyo/tinyexr/release/tinyexr.h
- Sample HDR: https://polyhaven.com/hdris

### Reading
- PBR Book IBL: https://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Light_Sources#InfiniteAreaLights
- PBRT Source: https://github.com/mmp/pbrt-v3/blob/master/src/lights/infinite.cpp

---

## Key Formulas

### Lat-Long Mapping
```glsl
// Direction → UV
u = 0.5 * (1.0 + atan2(dir.x, -dir.z) / π)
v = acos(dir.y) / π

// UV → Direction  
θ = v * π
φ = u * 2π - π
dir.x = sin(θ) * sin(φ)
dir.y = cos(θ)
dir.z = -sin(θ) * cos(φ)
```

### PDF Conversion
```cpp
pdf_solid_angle = pdf_uv / (2π² * sin(θ))
where θ = v * π
```

### Power Heuristic (MIS)
```cpp
weight = pdf_a² / (pdf_a² + pdf_b²)
```

### Luminance Weight
```cpp
weight(u,v) = luminance(u,v) * sin(v*π)
luminance = 0.212671*R + 0.715160*G + 0.072169*B
```

---

## Code Snippets

### Load EXR
```cpp
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

float* rgba;
int width, height;
const char* err = nullptr;

LoadEXR(&rgba, &width, &height, "env.exr", &err);
// Use rgba...
free(rgba);
if (err) FreeEXRErrorMessage(err);
```

### Sample Environment (Shader)
```glsl
[shader("miss")]
void Miss(inout Payload p) {
    float3 dir = WorldRayDirection();
    float u = 0.5 * (1.0 + atan2(dir.x, -dir.z) * M_1_PI);
    float v = acos(dir.y) * M_1_PI;
    
    p.color = environment_map.SampleLevel(sampler, float2(u,v), 0);
}
```

---

## File Locations

### ChameleonRT
```
util/tinyexr.h                          [Phase 0: Download]
util/util.h                             [Phase 1: Add HDRImage]
util/util.cpp                           [Phase 1: Implement load]
backends/dxr/render_dxr.h               [Phase 1: Add members]
backends/dxr/render_dxr.cpp             [Phase 1: Upload env]
shaders/unified_render.slang            [Phase 1: Miss shader]
shaders/modules/environment.slang       [Phase 2: Sampling]
```

### Test Data
```
test_data/environments/
  ├─ gradient.exr                       [Simple test]
  ├─ grid.exr                           [Seam test]
  └─ outdoor_001.exr                    [Real environment]
```

---

## Phase Checklist

### Phase 0: Prerequisites (2-3 days)
- [ ] Download tinyexr.h → util/
- [ ] Download sample .exr files
- [ ] Verify lat-long math
- [ ] Document MIS integration points
- [ ] Test roundtrip conversions

### Phase 1: Display (3-4 days)  
- [ ] HDRImage wrapper class
- [ ] GPU texture upload
- [ ] Modify miss shader
- [ ] Visual validation
- [ ] Performance test (<5%)

### Phase 2: Sampling (4-5 days)
- [ ] Build marginal CDF
- [ ] Build conditional CDFs
- [ ] Upload to GPU
- [ ] GPU binary search
- [ ] PDF calculations
- [ ] Variance reduction test

### Phase 3: MIS (3-4 days)
- [ ] Add to direct lighting
- [ ] Handle BRDF samples
- [ ] Calculate MIS weights
- [ ] Energy conservation test
- [ ] Compare with reference

### Phase 4: Polish (2-3 days)
- [ ] Performance profiling
- [ ] Environment rotation
- [ ] Intensity control
- [ ] UI integration
- [ ] Documentation

---

## Common Pitfalls

### ❌ Wrong: atan2(x, z)
### ✅ Right: atan2(x, -z)
The negative is important for correct mapping!

### ❌ Wrong: Forget sin(θ) in PDF
### ✅ Right: Always divide by sin(θ) for solid angle

### ❌ Wrong: Sample environment at mip level > 0
### ✅ Right: Use SampleLevel(..., 0) in miss shader

### ❌ Wrong: Clamp U coordinate
### ✅ Right: Wrap U, clamp V

### ❌ Wrong: Multiply MIS weights
### ✅ Right: Use w_a + w_b = 1, they're mutually exclusive

---

## Testing Quick Commands

### Compile Test
```powershell
cd build
cmake ..
cmake --build . --config Release
```

### Run with Environment
```powershell
.\Release\chameleonrt.exe --env test_data/environments/outdoor_001.exr
```

### Measure Performance
```powershell
# Baseline (no env)
.\chameleonrt.exe --benchmark

# With environment
.\chameleonrt.exe --env outdoor.exr --benchmark
```

---

## Debug Checklist

### Environment not showing?
- [ ] Check texture uploaded successfully
- [ ] Verify SRV created
- [ ] Check root signature binding
- [ ] Verify sampler state
- [ ] Print UV coordinates in shader

### Wrong orientation?
- [ ] Test with known directions
- [ ] Check atan2(x, -z) vs atan2(x, z)
- [ ] Verify acos(y) not acos(-y)
- [ ] Try flipping V coordinate

### Seam visible?
- [ ] Check sampler address mode (U=WRAP)
- [ ] Test with grid pattern
- [ ] Verify texture borders

### Too dark/bright?
- [ ] Check HDR values loaded correctly
- [ ] Verify no tone mapping applied
- [ ] Check emission values
- [ ] Compare with reference

### Performance bad?
- [ ] Profile sampling overhead
- [ ] Check texture resolution
- [ ] Optimize binary search
- [ ] Consider downsampling CDFs

---

## MIS Integration Points

### Direct Lighting (Strategy 1)
```glsl
// Line ~300 in unified_render.slang
for (uint i = 0; i < num_lights; i++) {
    // Sample quad light
}

// ADD HERE:
if (has_environment) {
    // Sample environment light
    float2 uv = sample_env(random);
    float3 wi = uv_to_dir(uv);
    // Trace shadow, evaluate MIS
}
```

### BRDF Sampling (Strategy 2)
```glsl
// Line ~360 in unified_render.slang
float3 wi = sample_brdf(...);
TraceRay(...);

for (uint i = 0; i < num_lights; i++) {
    if (hit_light) {
        // Evaluate with MIS
    }
}

// ADD HERE:
if (missed_all && has_environment) {
    float2 uv = dir_to_uv(wi);
    float env_pdf = get_env_pdf(uv);
    // Evaluate with MIS
}
```

---

## Performance Expectations

| Resolution | Memory | Sample Time | Notes |
|------------|--------|-------------|-------|
| 512×256    | 2 MB   | ~0.1 ms     | Low quality |
| 1024×512   | 8 MB   | ~0.2 ms     | Good |
| 2048×1024  | 32 MB  | ~0.3 ms     | High quality |
| 4096×2048  | 128 MB | ~0.5 ms     | Very high |

Importance sampling overhead: ~10-20% of miss shader time

Variance reduction: 50-100x for typical outdoor scenes

---

## Sample Environments

### Download from Poly Haven

**Indoor:**
- Studio Small 03 (small, simple)
- Abandoned Hall (complex)

**Outdoor:**
- Kloppenheim 02 (clear sky)
- Sunflowers (bright sun)
- Noon Grass (even lighting)

**Artistic:**
- Night City (night scene)
- Blue Lagoon (blue hour)

Download 2K .exr format for testing

---

## Validation Tests

### Energy Conservation
```cpp
// White sphere in white environment should be white
// (No brighter or darker than environment)
assert(sphere_color ≈ environment_average);
```

### Variance Reduction
```cpp
// Compare uniform vs importance sampling
float mse_uniform = measure_mse(uniform_samples);
float mse_importance = measure_mse(importance_samples);
assert(mse_importance < mse_uniform * 0.5);  // >2x better
```

### MIS Correctness
```cpp
// Weights should sum to 1
float w_light = power_heuristic(pdf_light, pdf_brdf);
float w_brdf = power_heuristic(pdf_brdf, pdf_light);
assert(abs(w_light + w_brdf - 1.0) < 0.001);
```

---

## Contact & Resources

### Questions?
- PBR Book: Complete reference
- PBRT Discord: Active community
- Graphics Programming Discord

### Sample Projects
- PBRT-v3: Reference implementation
- Mitsuba: Alternative renderer
- Falcor: Real-time path tracer
- Tungsten: Modern path tracer

---

**Last Updated:** December 18, 2025  
**Status:** Planning Phase - Ready to execute  
**Estimated Total Time:** 14-18 days
