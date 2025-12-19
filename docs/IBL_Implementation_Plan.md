# Image-Based Lighting (IBL) Implementation Plan
## ChameleonRT DXR Backend

**Date:** December 18, 2025  
**Author:** Planning Document  
**Status:** Planning Phase - NO IMPLEMENTATION YET

---

## Executive Summary

This document outlines the complete plan for implementing Image-Based Lighting (IBL) in ChameleonRT. IBL allows scenes to be illuminated using high-dynamic-range environment maps (typically in .exr format), providing realistic lighting from all directions around the scene.

**Key Goals:**
1. Load HDR environment maps (.exr format)
2. Sample environment maps in miss shaders for background radiance
3. Implement importance sampling for environment lighting
4. Integrate with existing MIS (Multiple Importance Sampling) framework

---

## Current State Analysis

### What We Have ✓
1. **MIS Implementation**: Already implemented for quad area lights
   - Light sampling strategy (sample light position, trace shadow ray)
   - BRDF sampling strategy (sample BRDF, check if hits light)
   - Power heuristic for combining strategies
   - Located in: [shaders/unified_render.slang](c:\Users\kchunang\dev\ChameleonRT\shaders\unified_render.slang#L296-L360)

2. **Miss Shader**: Currently returns checkered sky pattern
   - Located in: [shaders/unified_render.slang](c:\Users\kchunang\dev\ChameleonRT\shaders\unified_render.slang#L485-L503)
   - Simple procedural pattern, no texture sampling

3. **Image Loading Infrastructure**: 
   - stb_image for LDR images
   - Located in: [util/stb_image.h](c:\Users\kchunang\dev\ChameleonRT\util\stb_image.h)

4. **Quad Light Structure**: Well-defined light sampling
   - Located in: [shaders/modules/lights.slang](c:\Users\kchunang\dev\ChameleonRT\shaders\modules\lights.slang#L1-L100)

### What We Need ✗
1. **EXR Support**: Need tinyexr for HDR environment maps
2. **Environment Map Buffer**: GPU-side texture/buffer for env map
3. **Direction-to-UV Mapping**: Lat-long (equirectangular) mapping math
4. **Environment Light Sampling**: Importance sampling for infinite area lights
5. **MIS Integration**: Combine environment sampling with existing BRDF sampling

---

## Phase 0: Prerequisites & Research

### 0.1 Download TinyEXR Library
**Goal:** Obtain the header-only library for EXR loading

**Tasks:**
- [ ] Download tinyexr.h from https://github.com/syoyo/tinyexr
- [ ] Place in `util/` directory alongside stb_image.h
- [ ] Review API for loading float* RGB data
- [ ] Understand miniz dependency (comes with tinyexr)

**Reference Code Pattern:**
```cpp
// From tinyexr docs:
const char* input = "environment.exr";
float* out; // width * height * RGBA
int width, height;
const char* err = NULL;

int ret = LoadEXR(&out, &width, &height, input, &err);
if (ret != TINYEXR_SUCCESS) {
    // handle error
}
// out contains RGBA float data
free(out);
```

**Deliverable:** tinyexr.h in util/ directory with example test program

---

### 0.2 Understanding Lat-Long Mapping Mathematics
**Goal:** Fully understand ray direction → (u,v) texture coordinate conversion

**Mathematical Foundation:**

Given a ray direction vector `dir = (x, y, z)` in world space:

1. **Spherical Coordinates:**
   - θ (theta) = polar angle from +Y axis = `acos(dir.y)`
   - φ (phi) = azimuthal angle from +Z axis = `atan2(dir.x, -dir.z)`

2. **UV Mapping (Equirectangular):**
   ```
   u = (φ + π) / (2π) = (1.0 + atan2(dir.x, -dir.z) / π) * 0.5
   v = θ / π = acos(dir.y) / π
   ```
   - u ∈ [0, 1]: wraps around horizontally (longitude)
   - v ∈ [0, 1]: from pole to pole (latitude)

3. **Reverse Mapping (UV → Direction):**
   ```
   θ = v * π
   φ = u * 2π - π
   
   dir.x = sin(θ) * sin(φ)
   dir.y = cos(θ)
   dir.z = -sin(θ) * cos(φ)
   ```

**Visual Reference:** 
- Equirectangular mapping is like unwrapping a sphere onto a rectangle
- Top/bottom of image = poles (compressed)
- Middle = equator (least distorted)

**Example Code from Current Miss Shader:**
```glsl
// Already in unified_render.slang:
float3 dir = WorldRayDirection();
float u = (1.0f + atan2(dir.x, -dir.z) * M_1_PI) * 0.5f;  // M_1_PI = 1/π
float v = acos(dir.y) * M_1_PI;
```
This is ALREADY correct! Just needs to sample texture instead of procedural pattern.

**Tasks:**
- [ ] Document the math clearly with diagrams
- [ ] Verify current implementation in miss shader
- [ ] Create test cases with known directions
- [ ] Understand PDF transformation from UV to solid angle

**Deliverable:** Math reference document with examples

---

### 0.3 MIS (Multiple Importance Sampling) Review
**Goal:** Validate current MIS implementation and understand integration points

**Current MIS Strategy (from unified_render.slang lines 296-405):**

**Strategy 1: Sample Light**
```glsl
for each light:
    sample_position = sample_quad_light(light, random)
    light_pdf = quad_light_pdf(light, ...)
    bsdf_pdf = disney_pdf(material, ...)
    
    trace shadow ray
    if visible:
        weight = power_heuristic(1.0, light_pdf, 1.0, bsdf_pdf)
        contribution = bsdf * light_emission * abs(dot) * weight / light_pdf
```

**Strategy 2: Sample BRDF**
```glsl
sample_direction = sample_disney_brdf(material, random, ...)
bsdf_pdf = pdf from sampling

for each light:
    if ray hits light:
        light_pdf = quad_light_pdf(light, ...)
        weight = power_heuristic(1.0, bsdf_pdf, 1.0, light_pdf)
        contribution = bsdf * light_emission * abs(dot) * weight / bsdf_pdf
```

**Key Insight:** This is EXACTLY the pattern needed for environment lights!

**For Environment Lights:**
- **Strategy 1:** Sample environment map (importance sampling)
  - Need: PDF based on environment map pixel luminance
  - Need: Convert to solid angle PDF
  
- **Strategy 2:** Sample BRDF, check if misses geometry
  - Already works! Just evaluate environment in miss shader
  - Need: Environment PDF for MIS weight calculation

**Questions to Answer:**
- [ ] How to keep existing quad lights working alongside env light?
- [ ] Should environment light be sampled in same loop as quad lights?
- [ ] What's the PDF when BRDF ray misses all geometry? (answer: env_pdf)

**Deliverable:** MIS integration strategy document

---

## Phase 1: Basic Environment Map Loading & Display

### 1.1 Integrate TinyEXR into Build System
**Goal:** Add EXR loading capability to the project

**Files to Modify:**
- `util/CMakeLists.txt` - Add tinyexr header
- `util/util.h` - Declare load_exr function
- `util/util.cpp` - Implement load_exr wrapper

**Implementation:**
```cpp
// util/util.h
struct HDRImage {
    float* data;  // RGBA float data
    int width;
    int height;
    
    ~HDRImage() { if (data) free(data); }
};

HDRImage load_exr(const std::string& filename);

// util/util.cpp
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

HDRImage load_exr(const std::string& filename) {
    HDRImage img;
    const char* err = nullptr;
    int ret = LoadEXR(&img.data, &img.width, &img.height, 
                      filename.c_str(), &err);
    
    if (ret != TINYEXR_SUCCESS) {
        if (err) {
            std::cerr << "EXR load error: " << err << std::endl;
            FreeEXRErrorMessage(err);
        }
        throw std::runtime_error("Failed to load EXR");
    }
    
    return img;
}
```

**Tasks:**
- [ ] Add tinyexr.h to util/
- [ ] Create load_exr wrapper function
- [ ] Test with sample .exr file
- [ ] Handle errors gracefully

**Testing:**
- Download sample .exr from https://polyhaven.com/hdris
- Load and verify width/height
- Verify float data ranges (HDR values > 1.0)

**Deliverable:** Working EXR loader with test program

---

### 1.2 Create GPU Buffer for Environment Map
**Goal:** Upload environment map to GPU as a texture resource

**DXR-Specific Requirements:**
- Use D3D12 Texture2D with DXGI_FORMAT_R32G32B32A32_FLOAT
- Create SRV (Shader Resource View) for sampling
- Upload via upload heap → default heap copy

**Files to Modify:**
- `backends/dxr/render_dxr.h` - Add environment map members
- `backends/dxr/render_dxr.cpp` - Implement upload

**Implementation Sketch:**
```cpp
// render_dxr.h
class RenderDXR {
private:
    // Environment map resources
    dxr::Texture2D env_map;
    D3D12_GPU_DESCRIPTOR_HANDLE env_map_srv;
    
    void load_environment_map(const std::string& path);
};

// render_dxr.cpp
void RenderDXR::load_environment_map(const std::string& path) {
    HDRImage exr = load_exr(path);
    
    // Create texture
    env_map = dxr::Texture2D::device(
        device.Get(),
        glm::uvec2(exr.width, exr.height),
        D3D12_RESOURCE_STATE_COPY_DEST,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        D3D12_RESOURCE_FLAG_NONE
    );
    
    // Upload via staging buffer
    // (similar to existing texture uploads)
    // ...
    
    // Create SRV
    // Add to descriptor heap
    // ...
}
```

**Key Considerations:**
- Texture filtering: Linear or Point? (Linear for smooth env)
- Wrap mode: Should wrap horizontally (U), clamp vertically (V)
- Mip-maps: Probably not needed initially

**Tasks:**
- [ ] Create Texture2D resource
- [ ] Upload float RGBA data
- [ ] Create SRV descriptor
- [ ] Bind to shader root signature

**Deliverable:** Environment map uploaded to GPU and accessible in shaders

---

### 1.3 Simple Miss Shader Environment Lookup
**Goal:** Replace checkered pattern with environment map sampling

**Files to Modify:**
- `shaders/unified_render.slang` - Miss shader implementation
- `backends/dxr/render_dxr.cpp` - Bind env map to shader

**Implementation:**
```glsl
// unified_render.slang

// Add global resource
Texture2D<float4> environment_map;
SamplerState env_sampler;

[shader("miss")]
void Miss(inout Payload payload)
{
    payload.color_dist.w = -1.0f;  // Indicates miss
    
    // Get ray direction
    float3 dir = WorldRayDirection();
    
    // Convert to UV (lat-long mapping)
    float u = (1.0f + atan2(dir.x, -dir.z) * M_1_PI) * 0.5f;
    float v = acos(dir.y) * M_1_PI;
    
    // Sample environment map
    float4 env_color = environment_map.SampleLevel(env_sampler, float2(u, v), 0);
    
    payload.color_dist.rgb = env_color.rgb;
}
```

**Tasks:**
- [ ] Declare Texture2D in shader
- [ ] Bind texture in root signature
- [ ] Implement UV calculation
- [ ] Test with known environment maps

**Testing:**
- Use simple gradient .exr to verify UV mapping
- Check seam at u=0/u=1 boundary
- Verify orientation (top=sky, bottom=ground)

**Deliverable:** Environment map visible as background

---

## Phase 2: Uniform Environment Light Sampling

**Goal:** Add environment map as a light source using simple uniform sampling. No importance sampling yet - that's Phase 3!

**Key Insight:** Environment map is already loaded on GPU (from Phase 1). We just need to:
1. Sample it uniformly for direct lighting (like we sample quad lights)
2. Detect when BRDF rays hit it for indirect lighting
3. Use MIS to combine both strategies (framework already exists!)

**No Fancy Math Required:** Uniform sampling over a sphere = PDF of `1/(4π)`. That's it!

---

### 2.1 Add Environment Sampling to Direct Lighting
**Goal:** Sample environment uniformly and trace shadow rays (just like quad lights)

**Files to Modify:**
- `shaders/unified_render.slang` - Add environment sampling to direct lighting loop

**Where to Add Code:**
Look for the existing direct lighting loop that samples quad lights (around line 296-360). We'll add environment sampling right after the quad light loop.

**Implementation:**

```glsl
// ===== EXISTING CODE: Direct lighting from quad lights =====
for (uint light_idx = 0; light_idx < scene_params.num_lights; ++light_idx) {
    // ... existing quad light sampling code ...
    // ... (keep all of this exactly as is) ...
}

// ===== NEW CODE: Direct lighting from environment =====
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

**What This Does:**
- Picks random direction uniformly over sphere
- Samples environment texture at that direction
- Traces shadow ray to check if direction is visible
- If visible, adds lighting contribution with MIS weight
- **Works alongside existing quad lights** (not replacing them!)

**Tasks:**
- [ ] Add environment sampling code after quad light loop
- [ ] Test with environment map only (no quad lights)
- [ ] Test with environment map + quad lights (both working together)
- [ ] Verify shadows from geometry still block environment light

**Deliverable:** Direct lighting from environment working

---

### 2.2 Handle BRDF Samples That Hit Environment
**Goal:** When BRDF rays miss all geometry, get radiance from environment

**Files to Modify:**
- `shaders/unified_render.slang` - Add environment check after BRDF sampling

**Where to Add Code:**
Look for the existing BRDF sampling code (around line 370-405). After tracing the BRDF ray and checking quad lights, we'll add an environment check.

**Implementation:**

```glsl
// ===== EXISTING CODE: Sample BRDF =====
float3 w_i;
float bsdf_pdf;
float3 bsdf = sample_disney_brdf(mat, v_z, w_o, v_x, v_y, rng, w_i, bsdf_pdf);

// Trace ray in sampled direction
RayDesc bsdf_ray;
bsdf_ray.Origin = offset_ray(hit_p, v_z);
bsdf_ray.Direction = w_i;
bsdf_ray.TMin = 0.0f;
bsdf_ray.TMax = 1e20f;

Payload bsdf_payload;
bsdf_payload.color_dist = float4(0.0f, 0.0f, 0.0f, 0.0f);

TraceRay(scene, 0, 0xff, 0, 1, 0, bsdf_ray, bsdf_payload);

// ===== EXISTING CODE: Check if BRDF ray hit quad lights =====
for (uint light_idx = 0; light_idx < scene_params.num_lights; ++light_idx) {
    // ... existing quad light hit checking code ...
    // ... (keep all of this exactly as is) ...
}

// ===== NEW CODE: Check if BRDF ray hit environment =====
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

**What This Does:**
- After tracing BRDF sample, checks if ray missed all geometry
- If missed (`payload.color_dist.w < 0`), it hit the environment
- Gets radiance from miss shader (already calculated in Phase 1!)
- Calculates MIS weight between BRDF PDF and uniform environment PDF
- Adds contribution

**Key Point:** Miss shader already returns environment radiance! We just need to detect the miss and add the contribution.

**Tasks:**
- [ ] Add environment hit detection after BRDF sampling
- [ ] Test indirect lighting from environment
- [ ] Verify MIS weights are calculated correctly
- [ ] Compare with/without MIS to see variance reduction

**Deliverable:** Indirect lighting from environment working

---

### 2.3 Add Environment Map Flag to Scene Parameters
**Goal:** Control environment sampling with a flag

**Files to Modify:**
- `shaders/unified_render.slang` - Add flag to scene parameters struct
- `backends/dxr/render_dxr.cpp` - Set flag when environment map is loaded

**Implementation (Shader Side):**

```glsl
// In scene parameters struct
struct SceneParams {
    // ... existing parameters ...
    uint num_lights;
    // ... other fields ...
    
    // NEW: Flag to indicate if environment map is present
    uint has_environment_map;  // 0 = no env map, 1 = has env map
};
```

**Implementation (CPU Side):**

```cpp
// In render_dxr.cpp, when uploading scene parameters
SceneParams params;
params.num_lights = lights.size();
// ... other parameters ...

// NEW: Set environment map flag
params.has_environment_map = (env_map.resource != nullptr) ? 1 : 0;

// Upload to GPU
// ...
```

**What This Does:**
- Provides a simple way to check if environment map exists
- Allows conditional code in shaders
- Can be used to disable environment sampling if no map loaded

**Tasks:**
- [ ] Add flag to SceneParams struct
- [ ] Set flag on CPU side based on env map presence
- [ ] Use flag in shader for conditional environment sampling
- [ ] Test enabling/disabling environment map

**Deliverable:** Environment sampling controlled by flag

---

### 2.4 Testing & Validation
**Goal:** Verify uniform environment sampling is correct

**Test Cases:**

**Test 1: Environment Only (No Quad Lights)**
- Load scene with no quad lights
- Render with environment map
- **Expected:** Scene lit by environment, no hard shadows except from geometry
- **Validates:** Direct + indirect environment lighting works

**Test 2: Environment + Quad Lights**
- Load scene with quad lights
- Add environment map
- **Expected:** Scene lit by both sources, shadows from quad lights still work
- **Validates:** Environment and quad lights work together

**Test 3: Uniform Environment**
- Create solid color .exr (e.g., white)
- Render white sphere
- **Expected:** Uniform illumination from all directions
- **Validates:** Uniform sampling is unbiased

**Test 4: Directional Environment**
- Use environment with bright spot (e.g., sun)
- **Expected:** Lighting mostly from bright direction, but noisy
- **Note:** Will be improved in Phase 3 with importance sampling!

**Metrics:**
- [ ] Visual inspection - looks correct?
- [ ] No fireflies or artifacts
- [ ] Convergence - slowly reduces noise
- [ ] Energy conservation - not too bright/dark

**Deliverable:** Validated uniform environment sampling

---

## Phase 2 Summary

**What We Built:**
1. ✓ Environment sampling in direct lighting (uniform random directions)
2. ✓ Environment hits from BRDF sampling (indirect lighting)
3. ✓ MIS weights combining both strategies (using uniform PDF)

**What Works:**
- Environment illuminates the scene
- Both direct and indirect lighting from environment
- Works alongside existing quad lights
- Correct but noisy (will improve in Phase 3!)

**Next Phase (Phase 3):**
- Build importance sampling based on environment luminance
- Replace uniform PDF with importance-sampled PDF
- Dramatically reduce noise, especially for directional lighting

**Phase 2 is Complete When:**
- [ ] Direct environment lighting works
- [ ] Indirect environment lighting works
- [ ] MIS weights calculated correctly
- [ ] Tests pass validation
- [ ] Ready to add importance sampling

---

## Phase 3: MIS Integration

### 3.1 Add Environment Light Sampling to Path Tracer
**Goal:** Sample environment light alongside quad lights in direct lighting

**Modification to unified_render.slang:**

```glsl
// ===== DIRECT LIGHTING with MIS =====
float3 illum = float3(0.0f);

// Strategy 1a: Sample quad lights (existing code)
for (uint light_idx = 0; light_idx < num_lights; ++light_idx) {
    // ... existing quad light sampling ...
}

// Strategy 1b: Sample environment light
if (has_environment_map) {
    float2 light_samples = float2(lcg_randomf(rng), lcg_randomf(rng));
    
    // Sample environment map
    float env_pdf;
    float2 env_uv = sample_environment_map(light_samples, env_pdf);
    
    // Convert UV to direction
    float theta = env_uv.y * PI;
    float phi = env_uv.x * 2 * PI - PI;
    float3 wi = float3(sin(theta) * sin(phi), cos(theta), -sin(theta) * cos(phi));
    
    // Trace shadow ray to check visibility
    Payload shadow_hit;
    // ... (similar to quad light shadow ray) ...
    
    // If visible (misses all geometry)
    if (shadow_hit.color_dist.w < 0) {
        float bsdf_pdf = disney_pdf(mat, v_z, w_o, wi, v_x, v_y);
        
        if (env_pdf > EPSILON && bsdf_pdf > EPSILON) {
            float3 bsdf = disney_brdf(mat, v_z, w_o, wi, v_x, v_y);
            float3 env_radiance = environment_map.SampleLevel(env_sampler, env_uv, 0).rgb;
            float w = power_heuristic(1.0, env_pdf, 1.0, bsdf_pdf);
            
            illum += bsdf * env_radiance * abs(dot(wi, v_z)) * w / env_pdf;
        }
    }
}
```

**Tasks:**
- [ ] Add environment sampling code
- [ ] Integrate with existing MIS framework
- [ ] Test with and without quad lights
- [ ] Verify energy conservation

**Deliverable:** Environment light sampled in direct lighting

---

### 3.2 Handle BRDF Samples That Hit Environment
**Goal:** Correctly evaluate environment contribution for BRDF samples

**Modification to existing BRDF sampling code:**

```glsl
// Strategy 2: Sample BRDF (existing)
float3 w_i;
float pdf;
float3 bsdf = sample_disney_brdf(mat, v_z, w_o, v_x, v_y, rng, w_i, pdf);

// Trace ray
RayDesc ray;
ray.Origin = hit_p;
ray.Direction = w_i;
// ...

TraceRay(scene, flags, 0xFF, 0, 1, 0, ray, payload);

// Check if we hit a quad light (existing)
for (uint light_idx = 0; light_idx < num_lights; ++light_idx) {
    // ... existing quad light handling ...
}

// NEW: Check if we hit environment (missed all geometry)
if (has_environment_map && payload.color_dist.w < 0) {
    // Ray missed all geometry, hit environment
    
    // Get environment radiance
    float3 env_radiance = payload.color_dist.rgb;  // From miss shader
    
    // Calculate environment PDF
    float u = (1.0f + atan2(w_i.x, -w_i.z) * M_1_PI) * 0.5f;
    float v = acos(w_i.y) * M_1_PI;
    float env_pdf = environment_pdf(float2(u, v));  // From sampling distribution
    
    // MIS weight
    float w = power_heuristic(1.0, pdf, 1.0, env_pdf);
    
    // Add contribution
    sample_illum += path_throughput * bsdf * env_radiance * abs(dot(w_i, v_z)) * w / pdf;
}
```

**Tasks:**
- [ ] Add environment hit detection
- [ ] Implement environment_pdf lookup
- [ ] Calculate MIS weight
- [ ] Test convergence vs reference

**Deliverable:** Complete MIS for environment lighting

---

### 3.3 Validation & Testing
**Goal:** Verify correctness of implementation

**Test Cases:**

1. **Uniform Environment Map**
   - Should produce uniform lighting from all directions
   - No variance benefit from importance sampling
   - Validates basic correctness

2. **Single Bright Spot**
   - Small bright region in env map
   - Should see dramatic variance reduction with importance sampling
   - Validates sampling effectiveness

3. **Indoor Scene with Window**
   - Window shows environment
   - Indoor surfaces lit by environment through window
   - Tests visibility and indirect lighting

4. **Energy Conservation**
   - Render white sphere in white environment
   - Should match analytical solution
   - Validates MIS weights sum correctly

**Metrics:**
- [ ] Compare MSE vs uniform sampling
- [ ] Compare MSE vs BRDF-only sampling
- [ ] Measure convergence rate
- [ ] Visual comparison with reference renderer

**Deliverable:** Validation test suite and results

---

## Phase 4: Optimization & Polish

### 4.1 Performance Optimization
**Goals:**
- [ ] Profile sampling overhead
- [ ] Optimize binary search in shader
- [ ] Consider precomputed alias table
- [ ] Cache environment lookups if possible

### 4.2 Features & Quality
**Goals:**
- [ ] Add environment map rotation
- [ ] Add intensity multiplier
- [ ] Support multiple environment maps (?)
- [ ] Add tonemapping for preview

### 4.3 User Interface
**Goals:**
- [ ] Load environment map from command line
- [ ] UI controls for rotation/intensity
- [ ] Display environment preview
- [ ] Save/load settings

---

## Sample Code References

### Sample Projects to Study:

1. **PBRT-v3**
   - File: `src/lights/infinite.cpp`
   - Has complete implementation of importance sampling
   - Uses Distribution2D class

2. **Mitsuba Renderer**
   - Environment map sampling with MIS
   - Good reference for edge cases

3. **Ray Tracing Gems**
   - Chapter on importance sampling
   - DXR-specific examples

4. **Falcor**
   - Microsoft's real-time renderer
   - Has DXR environment map implementation
   - https://github.com/NVIDIAGameWorks/Falcor

### Sample Environment Maps:

1. **Poly Haven (FREE)**
   - https://polyhaven.com/hdris
   - High quality, CC0 license
   - Various resolutions

2. **sIBL Archive**
   - http://www.hdrlabs.com/sibl/archive.html
   - Calibrated HDR images

---

## Risk Assessment & Mitigation

### Technical Risks:

1. **Risk:** PDF calculation errors lead to bias
   - **Mitigation:** Extensive validation, compare with PBRT
   - **Validation:** Energy conservation tests

2. **Risk:** Performance overhead from sampling
   - **Mitigation:** Profile early, optimize CDFs
   - **Fallback:** Option to disable importance sampling

3. **Risk:** Integration with existing lights breaks
   - **Mitigation:** Incremental testing
   - **Validation:** Regression tests

4. **Risk:** Memory overhead for large environment maps
   - **Mitigation:** Support multiple resolutions
   - **Option:** Downsampling for sampling distribution

### Schedule Risks:

1. **Risk:** Math implementation takes longer than expected
   - **Mitigation:** Study references thoroughly in Phase 0
   - **Buffer:** Extra time in Phase 1

2. **Risk:** MIS integration has subtle bugs
   - **Mitigation:** Implement validation early
   - **Tool:** Reference images for comparison

---

## Success Criteria

### Must Have (Phase 1-3):
- [x] Load .exr environment maps
- [x] Display environment in miss shader
- [x] Importance sampling working
- [x] MIS integration complete
- [x] Passes energy conservation test

### Should Have (Phase 4):
- [ ] Performance within 20% of BRDF-only
- [ ] Variance reduction demonstrated
- [ ] Works with existing quad lights
- [ ] UI for loading environment maps

### Nice to Have:
- [ ] Multiple environment maps
- [ ] Real-time environment rotation
- [ ] Procedural sky models
- [ ] Environment map baking tools

---

## Next Steps

Before starting implementation:

1. **Download Resources**
   - [ ] Get tinyexr.h
   - [ ] Download sample .exr files
   - [ ] Clone PBRT for reference

2. **Math Validation**
   - [ ] Write standalone lat-long test
   - [ ] Verify PDF calculations
   - [ ] Study PBRT Distribution2D

3. **Architecture Review**
   - [ ] Review with team
   - [ ] Identify integration points
   - [ ] Plan testing strategy

4. **Create Phase Documents**
   - [ ] Detailed Phase 1 plan
   - [ ] Detailed Phase 2 plan
   - [ ] Detailed Phase 3 plan

---

## References

1. **PBR Book (Physically Based Rendering)**
   - Section 14.2.4: Infinite Area Lights
   - https://www.pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Light_Sources#InfiniteAreaLights

2. **TinyEXR**
   - https://github.com/syoyo/tinyexr
   - Header-only EXR loader

3. **Ray Tracing Gems**
   - Chapter on Importance Sampling
   - Chapter on MIS

4. **Veach Thesis**
   - Original MIS formulation
   - Power heuristic derivation

5. **Poly Haven**
   - Free HDR environment maps
   - https://polyhaven.com/hdris

---

## Appendix: Mathematical Formulas

### Lat-Long Mapping
```
Direction to UV:
  u = (1 + atan2(dir.x, -dir.z) / π) * 0.5
  v = acos(dir.y) / π

UV to Direction:
  θ = v * π
  φ = u * 2π - π
  dir = (sin(θ)sin(φ), cos(θ), -sin(θ)cos(φ))
```

### PDF Conversion
```
pdf_solid_angle = pdf_uv / (2π² * sin(θ))
where θ = v * π
```

### Power Heuristic (MIS)
```
w_i = (n_i * pdf_i)^β / Σ_j (n_j * pdf_j)^β
where β = 2 (commonly used)
```

### Luminance Calculation
```
Y = 0.212671*R + 0.715160*G + 0.072169*B
```

---

## Document History

- **v1.0** - December 18, 2025 - Initial planning document
- **Status:** PLANNING ONLY - NO IMPLEMENTATION YET

---

**END OF PLANNING DOCUMENT**
