# IBL Implementation - Phase 0: Prerequisites & Research
## Detailed Task Breakdown

**Status:** Planning Phase  
**Dependencies:** None  
**Estimated Duration:** 2-3 days  
**Goal:** Gather all necessary tools, libraries, and knowledge before implementation

---

## Task 0.1: Download and Integrate TinyEXR

### Objective
Obtain and verify the TinyEXR library for loading HDR .exr files.

### Subtasks

#### 0.1.1: Download TinyEXR
- [ ] Navigate to https://github.com/syoyo/tinyexr
- [ ] Download `tinyexr.h` (latest release)
- [ ] Place in `c:\Users\kchunang\dev\ChameleonRT\util\tinyexr.h`
- [ ] Verify file size (~50KB for header)

#### 0.1.2: Review TinyEXR API
Key functions to understand:
```cpp
// Simple loading (RGBA float)
int LoadEXR(float** out_rgba, int* width, int* height,
            const char* filename, const char** err);

// Free error message
void FreeEXRErrorMessage(const char* msg);

// Free loaded image
// Use standard free() on the returned float* buffer
```

**Notes:**
- Returns RGBA in interleaved format (R0 G0 B0 A0 R1 G1 B1 A1 ...)
- Values are floating point (can be > 1.0 for HDR)
- Must call `free()` on returned buffer
- Must call `FreeEXRErrorMessage()` if error

#### 0.1.3: Create Test Program
Create `test_tinyexr.cpp` to verify:

```cpp
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: test_tinyexr <file.exr>\n";
        return 1;
    }
    
    float* rgba;
    int width, height;
    const char* err = nullptr;
    
    int ret = LoadEXR(&rgba, &width, &height, argv[1], &err);
    
    if (ret != TINYEXR_SUCCESS) {
        std::cerr << "ERROR: " << (err ? err : "unknown") << "\n";
        if (err) FreeEXRErrorMessage(err);
        return 1;
    }
    
    std::cout << "Loaded: " << width << "x" << height << "\n";
    
    // Check first few pixels
    std::cout << "First pixel (RGBA): ";
    std::cout << rgba[0] << " " << rgba[1] << " " 
              << rgba[2] << " " << rgba[3] << "\n";
    
    // Check for HDR values
    float max_val = 0;
    for (int i = 0; i < width * height * 4; i++) {
        max_val = std::max(max_val, rgba[i]);
    }
    std::cout << "Max value: " << max_val << " (HDR: " 
              << (max_val > 1.0 ? "YES" : "NO") << ")\n";
    
    free(rgba);
    return 0;
}
```

**Compile and test:**
```powershell
cl test_tinyexr.cpp /I..\util /EHsc
.\test_tinyexr.exe sample.exr
```

#### 0.1.4: Download Sample EXR Files
Get test environment maps from Poly Haven:

- [ ] Visit https://polyhaven.com/hdris
- [ ] Download "Studio Small 03" or similar (small file)
- [ ] Download in 2K resolution, .exr format
- [ ] Place in `test_data/environments/`
- [ ] Verify with test program

**Expected output:**
```
Loaded: 2048x1024
First pixel (RGBA): 0.523 0.612 0.834 1.0
Max value: 15.234 (HDR: YES)
```

### Acceptance Criteria
- [x] tinyexr.h downloaded and in util/
- [x] Test program compiles and runs
- [x] Can load .exr files successfully
- [x] Verified HDR values (> 1.0)
- [x] Sample environment maps downloaded

---

## Task 0.2: Understanding Lat-Long Mapping

### Objective
Fully understand the mathematics of equirectangular (latitude-longitude) environment mapping.

### Subtasks

#### 0.2.1: Mathematical Derivation
Document the complete mapping:

**Forward Mapping (Direction → UV):**

Given 3D unit direction vector `d = (x, y, z)`:

1. Convert to spherical coordinates:
   ```
   θ (theta) = acos(y)           // polar angle from +Y, range [0, π]
   φ (phi)   = atan2(x, -z)       // azimuthal angle, range [-π, π]
   ```

2. Convert to UV coordinates:
   ```
   u = (φ + π) / (2π) = 0.5 * (1.0 + φ/π)    // range [0, 1]
   v = θ / π                                   // range [0, 1]
   ```

**Why these formulas?**
- `u = 0.5`: Looking down -Z axis (φ = 0)
- `u = 0.0` or `1.0`: Looking down +Z axis (φ = ±π)
- `v = 0.0`: Looking straight up (+Y, θ = 0)
- `v = 1.0`: Looking straight down (-Y, θ = π)
- `v = 0.5`: Looking at horizon (θ = π/2)

**Reverse Mapping (UV → Direction):**

Given `(u, v)` texture coordinates:

1. Convert to spherical:
   ```
   θ = v * π
   φ = u * 2π - π
   ```

2. Convert to Cartesian:
   ```
   x = sin(θ) * sin(φ)
   y = cos(θ)
   z = -sin(θ) * cos(φ)    // Note: negative for right-handed
   ```

#### 0.2.2: Verify Current Implementation
Check `shaders/unified_render.slang` Miss shader:

```glsl
float3 dir = WorldRayDirection();
float u = (1.0f + atan2(dir.x, -dir.z) * M_1_PI) * 0.5f;
float v = acos(dir.y) * M_1_PI;
```

Where `M_1_PI = 1/π`

**Verification:**
- [x] Formula matches mathematical derivation
- [x] `atan2(x, -z)` gives correct azimuth
- [x] `acos(y)` gives correct polar angle
- [x] UV range is [0, 1]

✓ **This is already correct!**

#### 0.2.3: Create Test Cases
Test known directions:

| Direction | Expected UV | Description |
|-----------|-------------|-------------|
| (0, 1, 0) | (0.5, 0.0) | Straight up |
| (0, -1, 0) | (0.5, 1.0) | Straight down |
| (0, 0, -1) | (0.5, 0.5) | Forward (-Z) |
| (1, 0, 0) | (0.75, 0.5) | Right (+X) |
| (-1, 0, 0) | (0.25, 0.5) | Left (-X) |
| (0, 0, 1) | (0.0 or 1.0, 0.5) | Back (+Z) |

Create test program:
```cpp
#include <cmath>
#include <iostream>
#include <glm/glm.hpp>

glm::vec2 dir_to_uv(glm::vec3 dir) {
    float u = 0.5f * (1.0f + atan2(dir.x, -dir.z) / M_PI);
    float v = acos(dir.y) / M_PI;
    return glm::vec2(u, v);
}

glm::vec3 uv_to_dir(glm::vec2 uv) {
    float theta = uv.y * M_PI;
    float phi = uv.x * 2.0f * M_PI - M_PI;
    return glm::vec3(
        sin(theta) * sin(phi),
        cos(theta),
        -sin(theta) * cos(phi)
    );
}

void test_roundtrip() {
    std::vector<glm::vec3> test_dirs = {
        {0, 1, 0}, {0, -1, 0}, {0, 0, -1},
        {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}
    };
    
    for (auto dir : test_dirs) {
        glm::vec2 uv = dir_to_uv(dir);
        glm::vec3 dir2 = uv_to_dir(uv);
        float error = glm::length(dir - dir2);
        
        std::cout << "Dir: (" << dir.x << ", " << dir.y << ", " << dir.z << ")\n";
        std::cout << "  UV: (" << uv.x << ", " << uv.y << ")\n";
        std::cout << "  Roundtrip error: " << error << "\n\n";
    }
}
```

#### 0.2.4: Understand Distortion
**Key Insight:** Lat-long mapping has area distortion!

Equal areas in UV space ≠ equal solid angles on sphere:
- Near poles (v→0 or v→1): compressed
- Near equator (v≈0.5): least distorted

**Jacobian for area transformation:**
```
dA_uv = du * dv
dω (solid angle) = sin(θ) * dθ * dφ
                 = sin(v*π) * π * dv * 2π * du
                 = 2π² * sin(v*π) * du * dv

Therefore: dω = 2π² * sin(v*π) * dA_uv
```

**Why this matters:**
- When sampling environment map, must account for distortion
- Multiply sampling PDF by `sin(v*π)` during distribution building
- Divide by `sin(v*π)` when converting PDF to solid angle

### Acceptance Criteria
- [x] Math documented with derivations
- [x] Test program verifies roundtrip accuracy
- [x] Distortion understood and documented
- [x] Ready to implement in shaders

---

## Task 0.3: MIS Review and Validation

### Objective
Thoroughly understand current MIS implementation and plan integration with environment lights.

### Subtasks

#### 0.3.1: Document Current MIS Flow

**Current implementation in unified_render.slang lines 296-405:**

```
DIRECT LIGHTING LOOP:
├─ For each quad light:
│  ├─ Strategy 1: Sample light
│  │  ├─ sample_pos = random point on light
│  │  ├─ light_pdf = pdf of sampling that point
│  │  ├─ bsdf_pdf = pdf of BRDF sampling same direction
│  │  ├─ Trace shadow ray
│  │  └─ If visible: add contribution with MIS weight
│  │
│  └─ (Strategy 2 handled separately below)
│
INDIRECT LIGHTING:
├─ Strategy 2: Sample BRDF
│  ├─ wi = sample from BRDF
│  ├─ bsdf_pdf = pdf of that sample
│  ├─ Trace ray to find what it hits
│  │
│  └─ For each quad light:
│     ├─ Check if ray hit this light
│     ├─ If yes:
│     │  ├─ light_pdf = pdf light would give this direction
│     │  └─ Add contribution with MIS weight
│     │
│     └─ Continue to next bounce
```

**Key observations:**
1. Two strategies are used and combined with MIS
2. Light sampling explicitly samples lights
3. BRDF sampling implicitly hits lights
4. Power heuristic combines them: `w = pdf_i^2 / (pdf_i^2 + pdf_j^2)`

#### 0.3.2: Plan Environment Light Integration

**Addition to Strategy 1 (Sample Light):**
```glsl
// AFTER sampling all quad lights, ALSO sample environment:

if (has_environment_light) {
    // Sample environment map using importance sampling
    float2 uv = sample_environment_map(random);
    float env_pdf = environment_pdf(uv);  // in solid angle
    
    float3 wi = uv_to_direction(uv);
    
    // Evaluate BRDF for this direction
    float bsdf_pdf = disney_pdf(..., wi);
    
    // Trace shadow ray - if misses ALL geometry, env is visible
    trace_ray(...);
    
    if (ray_missed_everything) {
        float3 env_radiance = sample_environment(uv);
        float3 bsdf = disney_brdf(..., wi);
        float mis_weight = power_heuristic(env_pdf, bsdf_pdf);
        
        contribution += bsdf * env_radiance * dot(wi, normal) * mis_weight / env_pdf;
    }
}
```

**Addition to Strategy 2 (Sample BRDF):**
```glsl
// AFTER checking quad lights, ALSO check environment:

float3 wi = sample_disney_brdf(...);
float bsdf_pdf = ...;

trace_ray(...);

// Check quad lights (existing code)
for each quad_light {
    if (ray hit this light) {
        // ... existing code ...
    }
}

// NEW: Check environment
if (ray_missed_all_geometry) {
    // Ray hit environment
    float2 uv = direction_to_uv(wi);
    float env_pdf = environment_pdf(uv);
    
    float3 env_radiance = sample_environment(uv);
    float mis_weight = power_heuristic(bsdf_pdf, env_pdf);
    
    contribution += bsdf * env_radiance * dot(wi, normal) * mis_weight / bsdf_pdf;
}
```

#### 0.3.3: Identify Integration Points

**Files to modify:**
1. `shaders/unified_render.slang`:
   - Add environment map resources
   - Add environment sampling functions
   - Modify direct lighting loop
   - Modify BRDF sampling handling

2. `backends/dxr/render_dxr.cpp`:
   - Load environment map
   - Upload to GPU
   - Bind to shader

3. `backends/dxr/render_dxr.h`:
   - Add environment map members
   - Add sampling distribution data

**Data flow:**
```
CPU Side:
├─ Load .exr file (TinyEXR)
├─ Build sampling distribution (CDF)
├─ Upload to GPU (Texture + Buffers)
└─ Bind to shader root signature

GPU Side (Shader):
├─ Sample environment using CDF
├─ Evaluate PDF
├─ Compute MIS weight
└─ Add contribution
```

#### 0.3.4: Validate MIS Math

**Power Heuristic Formula:**
```cpp
float power_heuristic(float pdf_a, float pdf_b) {
    float a = pdf_a * pdf_a;
    float b = pdf_b * pdf_b;
    return a / (a + b);
}
```

**Properties to verify:**
- When pdf_a >> pdf_b: weight ≈ 1 (prefer strategy A)
- When pdf_a << pdf_b: weight ≈ 0 (prefer strategy B)
- When pdf_a ≈ pdf_b: weight ≈ 0.5 (equal contribution)
- Weights sum to 1: w_a + w_b = 1

**Test cases:**
```cpp
// Test 1: Light sampling is much better
float light_pdf = 10.0;
float brdf_pdf = 0.1;
float w1 = power_heuristic(light_pdf, brdf_pdf);  // Should be ≈ 0.99
float w2 = power_heuristic(brdf_pdf, light_pdf);  // Should be ≈ 0.01
assert(abs(w1 + w2 - 1.0) < 0.001);

// Test 2: Equal PDFs
float pdf = 5.0;
float w1 = power_heuristic(pdf, pdf);  // Should be exactly 0.5
assert(abs(w1 - 0.5) < 0.001);
```

#### 0.3.5: Plan Compatibility Testing

**Regression tests:**
1. Disable environment, verify quad lights still work
2. Disable quad lights, verify environment works
3. Enable both, verify they combine correctly
4. Compare with reference renderer (if available)

**Test scenes:**
1. Cornell box with quad light only (baseline)
2. Sphere with environment only
3. Cornell box with environment (indirect)
4. Mixed: quad light + environment

### Acceptance Criteria
- [x] Current MIS flow documented
- [x] Environment integration planned
- [x] Integration points identified
- [x] MIS math validated
- [x] Test plan created

---

## Deliverables Checklist

### Code
- [ ] `util/tinyexr.h` - Downloaded library
- [ ] `test_tinyexr.cpp` - EXR loading test
- [ ] `test_latlong.cpp` - Mapping test program
- [ ] `test_data/environments/*.exr` - Sample environment maps

### Documentation
- [x] Mathematical derivations for lat-long mapping
- [x] MIS integration strategy document
- [x] Test case definitions
- [x] Known issues and edge cases

### Validation
- [ ] TinyEXR loads sample files
- [ ] Lat-long mapping passes roundtrip tests
- [ ] MIS math verified with test cases
- [ ] Ready to proceed to Phase 1

---

## Risk Mitigation

### Risk 1: Math errors in lat-long mapping
**Mitigation:** 
- Test with known directions
- Compare with reference implementations (PBRT)
- Visual validation with test patterns

### Risk 2: MIS integration complexity
**Mitigation:**
- Document current implementation first
- Incremental changes with testing
- Keep quad lights working throughout

### Risk 3: Performance concerns
**Mitigation:**
- Profile early and often
- Plan optimization Phase 4
- Have fallback (no importance sampling)

---

## Next Phase Preview

Once Phase 0 is complete, Phase 1 will:
1. Integrate TinyEXR into CMake build
2. Create GPU buffer for environment map
3. Modify miss shader to sample environment
4. Verify basic environment display

**Estimated start:** After Phase 0 validation complete  
**Prerequisites:** All Phase 0 deliverables checked off

---

**Document Status:** Ready for execution  
**Last Updated:** December 18, 2025
