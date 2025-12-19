# Phase 2: Uniform Environment Light Sampling
## Image-Based Lighting Implementation - ChameleonRT

**Date:** December 18, 2025  
**Status:** Ready to Implement  
**Prerequisites:** Phase 1 Complete (Environment map loaded and visible in miss shader)

---

## Overview

**Goal:** Add environment map as a light source using simple uniform sampling.

**What This Phase Does:**
- Add environment lighting to direct illumination (alongside quad lights)
- Add environment contribution for indirect lighting (BRDF bounces)
- Use MIS to combine both strategies
- **NO importance sampling yet** - that's Phase 3!

**Key Insight:** Environment map is already on the GPU. We just need to:
1. Sample it uniformly for direct lighting
2. Detect when BRDF rays hit it
3. Calculate MIS weights using uniform PDF = `1/(4π)`

**Estimated Time:** 2-3 hours

---

## Prerequisites Check

Before starting Phase 2, verify:
- [x] Phase 1 complete: Environment map loads and displays
- [x] Environment texture bound to shader
- [x] Miss shader returns environment radiance
- [x] Existing quad light sampling works
- [x] Existing MIS implementation works for quad lights

---

## Step 2.1: Add Environment Sampling to Direct Lighting

**Goal:** Sample environment uniformly and trace shadow rays (like quad lights)

**File:** `shaders/unified_render.slang`

**Location:** Find the direct lighting loop (around line 296-360) where quad lights are sampled.

### Implementation

Add this code **after** the quad light sampling loop:

```glsl
// ===== NEW: Direct lighting from environment =====
if (scene_params.has_environment_map) {
    // Step 1: Generate random UV coordinates (uniform sampling)
    float env_u = lcg_randomf(rng);
    float env_v = lcg_randomf(rng);
    
    // Step 2: Convert UV to direction (spherical mapping)
    float theta = env_v * M_PI;
    float phi = env_u * 2.0f * M_PI - M_PI;
    
    float sin_theta = sin(theta);
    float cos_theta = cos(theta);
    float sin_phi = sin(phi);
    float cos_phi = cos(phi);
    
    float3 wi = float3(sin_theta * sin_phi, cos_theta, -sin_theta * cos_phi);
    
    // Step 3: Sample environment map at this UV
    float3 env_radiance = environment_map.SampleLevel(env_sampler, float2(env_u, env_v), 0).rgb;
    
    // Step 4: Trace shadow ray to check visibility
    RayDesc shadow_ray;
    shadow_ray.Origin = offset_ray(hit_p, v_z);
    shadow_ray.Direction = wi;
    shadow_ray.TMin = 0.0f;
    shadow_ray.TMax = 1e20f;  // Ray to infinity
    
    Payload shadow_payload;
    shadow_payload.color_dist = float4(0.0f, 0.0f, 0.0f, 0.0f);
    
    uint shadow_flags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
    TraceRay(scene, shadow_flags, 0xff, 0, 1, 0, shadow_ray, shadow_payload);
    
    // Step 5: If ray missed all geometry, it hit the environment
    if (shadow_payload.color_dist.w < 0.0f) {
        // Calculate BSDF for this direction
        float3 bsdf = disney_brdf(mat, v_z, w_o, wi, v_x, v_y);
        float bsdf_pdf = disney_pdf(mat, v_z, w_o, wi, v_x, v_y);
        
        // Uniform PDF over sphere = 1 / (4π)
        float env_pdf = M_1_PI * 0.25f;  // 1/(4π)
        
        // MIS weight (power heuristic, same as quad lights)
        float mis_weight = power_heuristic(1.0f, env_pdf, 1.0f, bsdf_pdf);
        
        // Add contribution
        float cos_term = abs(dot(wi, v_z));
        illum += bsdf * env_radiance * cos_term * mis_weight / env_pdf;
    }
}
```

### What Each Part Does

**Step 1 - Random UV:**
- `env_u`, `env_v` = random numbers in [0, 1]
- Uniformly samples the environment texture

**Step 2 - UV to Direction:**
- Converts texture coordinates to 3D direction
- Uses spherical (lat-long) mapping
- Same math as in miss shader, but reversed

**Step 3 - Sample Texture:**
- Gets radiance from environment map
- Uses the random UV coordinates

**Step 4 - Trace Shadow Ray:**
- Check if direction is visible from hit point
- Same pattern as quad light shadow rays
- Ray goes to infinity (`TMax = 1e20f`)

**Step 5 - Add Contribution:**
- If visible, calculate lighting contribution
- BSDF evaluation for this direction
- MIS weight between environment PDF and BSDF PDF
- Uniform PDF = `1/(4π) = 0.0795...`

### Testing This Step

**Test 1:** Environment only (no quad lights)
```
Expected: Scene lit by environment from all directions
```

**Test 2:** Check shadows
```
Expected: Geometry still casts shadows, blocking environment light
```

**Test 3:** With quad lights
```
Expected: Both environment and quad lights illuminate scene
```

---

## Step 2.2: Handle BRDF Samples That Hit Environment

**Goal:** When BRDF rays miss geometry, add environment contribution

**File:** `shaders/unified_render.slang`

**Location:** Find BRDF sampling code (around line 370-405), after quad light hit checking.

### Implementation

Add this code **after** the quad light hit checking loop:

```glsl
// ===== NEW: Check if BRDF ray hit environment =====
if (scene_params.has_environment_map && bsdf_payload.color_dist.w < 0.0f) {
    // Ray missed all geometry, so it hit the environment!
    
    // Get environment radiance (already computed by miss shader)
    float3 env_radiance = bsdf_payload.color_dist.rgb;
    
    // Calculate environment PDF for this direction
    // (Uniform PDF = 1/(4π))
    float env_pdf = M_1_PI * 0.25f;
    
    // MIS weight
    float mis_weight = power_heuristic(1.0f, bsdf_pdf, 1.0f, env_pdf);
    
    // Add contribution
    float cos_term = abs(dot(w_i, v_z));
    sample_illum += bsdf * env_radiance * cos_term * mis_weight / bsdf_pdf;
}
```

### What This Does

**Detects Environment Hits:**
- `bsdf_payload.color_dist.w < 0` means ray missed all geometry
- Miss shader already computed environment radiance
- We just need to add the contribution!

**Gets Radiance:**
- Miss shader already sampled the environment map
- No need to sample again - reuse the result!

**MIS Weight:**
- Combines BRDF PDF with environment PDF
- Same power heuristic as quad lights
- Reduces variance compared to BRDF-only sampling

**Adds Contribution:**
- Same pattern as quad light hits
- Division by `bsdf_pdf` (not `env_pdf`) because we sampled BRDF

### Testing This Step

**Test 1:** Indirect lighting
```
Expected: Surfaces lit by bounced light from environment
```

**Test 2:** Compare with/without MIS
```
Expected: MIS version converges faster, less noise
```

---

## Step 2.3: Add Environment Map Flag

**Goal:** Control environment sampling with a shader flag

### Shader Side

**File:** `shaders/unified_render.slang`

**Location:** Find the `SceneParams` struct (likely near the top of the file)

**Add this field:**

```glsl
struct SceneParams {
    // ... existing parameters ...
    uint num_lights;
    // ... other fields ...
    
    // NEW: Flag to indicate if environment map is present
    uint has_environment_map;  // 0 = no env map, 1 = has env map
};
```

### CPU Side

**File:** `backends/dxr/render_dxr.cpp`

**Location:** Find where `SceneParams` is populated and uploaded to GPU

**Set the flag:**

```cpp
// When creating/updating scene parameters
SceneParams params;
params.num_lights = lights.size();
// ... other parameters ...

// NEW: Set environment map flag
params.has_environment_map = (env_map.resource != nullptr) ? 1 : 0;

// Upload to GPU
// ...
```

### What This Does

- Provides a simple boolean check in shaders
- Avoids sampling environment if none is loaded
- Allows easy enable/disable for debugging

### Testing This Step

**Test 1:** No environment map
```
Load scene without environment
Expected: Flag = 0, no environment sampling, no crashes
```

**Test 2:** With environment map
```
Load scene with environment
Expected: Flag = 1, environment sampling works
```

---

## Step 2.4: Testing & Validation

### Test Case 1: Environment Only

**Setup:**
- Load scene with no quad lights
- Load environment map

**Expected Results:**
- Scene illuminated by environment
- Soft lighting from all directions
- No hard shadows (only ambient occlusion-like effects)
- Noisy but gradually converging

**What to Check:**
- [ ] Direct lighting works
- [ ] Indirect lighting works
- [ ] No fireflies or artifacts
- [ ] Converges to stable image

---

### Test Case 2: Environment + Quad Lights

**Setup:**
- Load scene with quad lights
- Add environment map

**Expected Results:**
- Scene lit by **both** sources
- Hard shadows from quad lights still visible
- Environment fills in soft lighting
- Combined illumination looks natural

**What to Check:**
- [ ] Quad lights still work
- [ ] Environment adds to illumination
- [ ] MIS combines both properly
- [ ] No double-counting of light

---

### Test Case 3: Uniform Environment

**Setup:**
- Create solid white .exr (constant color)
- Render simple sphere

**Expected Results:**
- Uniform illumination from all directions
- Matches analytical solution (if possible)
- No directional bias

**What to Check:**
- [ ] Uniform lighting
- [ ] Correct intensity
- [ ] Energy conservation

---

### Test Case 4: Directional Environment

**Setup:**
- Use environment with bright spot (e.g., sun)
- Render test scene

**Expected Results:**
- Strong lighting from bright direction
- **Will be noisy!** (This is expected with uniform sampling)
- Gradually converges to correct solution

**What to Check:**
- [ ] Bright areas light the scene more
- [ ] Noise is high but unbiased
- [ ] Converges slowly
- [ ] **Note:** Will improve dramatically in Phase 3!

---

## Common Issues & Debugging

### Issue 1: No Environment Lighting

**Symptoms:** Scene looks the same with/without environment

**Check:**
- [ ] Is `has_environment_map` flag set correctly?
- [ ] Is environment texture bound to shader?
- [ ] Are shadow rays hitting something unexpectedly?
- [ ] Is `env_pdf` zero (division error)?

**Debug:** Add `illum = float3(1, 0, 0)` inside environment sampling to see if code is reached.

---

### Issue 2: Too Bright/Dark

**Symptoms:** Environment lighting intensity is wrong

**Check:**
- [ ] Is PDF correct? Should be `1/(4π) ≈ 0.0796`
- [ ] Is cosine term included? `abs(dot(wi, v_z))`
- [ ] Is division correct? Divide by `env_pdf` for light sampling, `bsdf_pdf` for BRDF sampling
- [ ] Is environment map HDR values correct?

**Debug:** Print `env_radiance` values to verify texture sampling.

---

### Issue 3: Fireflies/Bright Pixels

**Symptoms:** Random very bright pixels that don't go away

**Check:**
- [ ] Is `env_pdf` zero causing division by zero?
- [ ] Are PDFs calculated correctly?
- [ ] Is MIS weight blowing up? (Add clamp for debugging)

**Debug:** Clamp MIS weight to reasonable range (e.g., [0, 100]) temporarily.

---

### Issue 4: Doesn't Work With Quad Lights

**Symptoms:** Adding environment breaks quad lights

**Check:**
- [ ] Did you modify existing quad light code by accident?
- [ ] Are you overwriting `illum` instead of adding to it (`+=`)?
- [ ] Is shadow ray setup correct?

**Debug:** Test environment sampling in separate shader temporarily.

---

## Validation Checklist

Before moving to Phase 3:

- [ ] Direct environment lighting works
- [ ] Indirect environment lighting works  
- [ ] Works alongside quad lights (both together)
- [ ] MIS weights calculated correctly
- [ ] No crashes or artifacts
- [ ] Converges to correct solution (slowly)
- [ ] Test Case 1 passes
- [ ] Test Case 2 passes
- [ ] Test Case 3 passes
- [ ] Test Case 4 passes

---

## Phase 2 Summary

**What We Built:**
1. ✅ Environment sampling for direct lighting (uniform random directions)
2. ✅ Environment hits from BRDF sampling (indirect lighting)
3. ✅ MIS weights combining both strategies
4. ✅ Environment works alongside quad lights

**What Works:**
- Environment illuminates the scene from all directions
- Both direct and indirect lighting
- Proper MIS variance reduction
- Compatible with existing features

**Limitations (Will Fix in Phase 3):**
- ⚠️ Noisy with directional lighting (bright spots)
- ⚠️ Slow convergence for complex environments
- ⚠️ Wastes samples on dark areas

**Next Step: Phase 3**
- Build importance sampling based on luminance
- Replace uniform PDF with importance-based PDF
- Dramatically reduce noise
- Much faster convergence

---

## Code Summary

**Total Lines Added:** ~50-60 lines
**Files Modified:** 2 files
- `shaders/unified_render.slang` (~45 lines)
- `backends/dxr/render_dxr.cpp` (~5 lines)

**Core Additions:**
1. Direct environment sampling (~25 lines)
2. Indirect environment contribution (~15 lines)
3. Environment flag (~5 lines)

---

## Ready to Implement?

**Checklist before coding:**
- [ ] Phase 1 complete and tested
- [ ] Understand uniform sampling concept
- [ ] Know where to add code in unified_render.slang
- [ ] Have test environment map ready
- [ ] Understand MIS from existing quad light code

**Implementation Order:**
1. Add environment flag (Step 2.3) - easiest, enables conditional code
2. Add direct lighting (Step 2.1) - test immediately
3. Add indirect lighting (Step 2.2) - builds on step 2
4. Run all tests (Step 2.4) - validate everything works

Good luck! 🚀

---

**END OF PHASE 2 DOCUMENT**
