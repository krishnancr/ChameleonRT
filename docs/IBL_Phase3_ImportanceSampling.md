# IBL Phase 3: Importance Sampling Implementation Plan
## Environment Map Importance Sampling

**Date:** December 19, 2025  
**Status:** Planning Phase  
**Prerequisites:** Phase 2 Complete (Uniform Environment Sampling Working)

---

## Overview

Phase 3 implements importance sampling for environment maps to dramatically reduce noise when sampling directional lighting (e.g., sun, bright sky regions). This replaces the uniform sphere sampling from Phase 2 with a 2D probability distribution based on environment map luminance.

**Key Benefits:**
- **10-100x noise reduction** for environments with bright spots
- **Faster convergence** to clean images
- **Better use of samples** - focus on high-contribution directions

**Performance Impact:**
- ~5-10% slower per sample (binary search overhead)
- ~10-100x fewer samples needed for same quality
- **Net result: 9x-90x faster convergence**

---

## Why Importance Sampling?

### The Problem with Uniform Sampling (Phase 2)

Currently we sample directions uniformly across the sphere (PDF = 1/(4π)):

```
Uniform Sampling:
- Pick random UV: u ∈ [0,1), v ∈ [0,1)
- Every direction has equal probability
- Works but wastes samples on dark areas
```

**Example:** Environment with small bright sun:
- Sun covers 1% of sphere, contributes 90% of energy
- Uniform sampling: Only 1 in 100 samples hits sun → **very noisy**
- Need ~10,000 samples for clean result

### The Solution: Importance Sampling

Sample directions proportional to their luminance (brightness):

```
Importance Sampling:
- Bright areas sampled more often
- Dark areas sampled less often
- PDF proportional to luminance
```

**Same example with importance sampling:**
- 90 out of 100 samples hit sun (matches energy contribution)
- Need ~100 samples for clean result → **100x improvement**

---

## Data Structures Required

### Two Arrays Needed on GPU:

**1. Marginal CDF (1D array):**
- Size: `env_height` floats
- Purpose: Sample which row (V coordinate) to use
- Each entry: cumulative probability up to that row

**2. Conditional CDFs (2D array):**
- Size: `env_height × env_width` floats  
- Purpose: For each row V, sample which column (U coordinate)
- Each row: cumulative probability across that row

**Memory overhead:**
- For 2048×1024 env map: ~8 MB additional
- For 4096×2048 env map: ~32 MB additional
- Still need original EXR texture for color lookup!

### Why We Need Both CDF and Original Texture:

```
Sampling Workflow:
┌────────────────────────────────────────────┐
│ Step 1: Sample CDF → get UV coordinate     │  ← CDF data (luminance-based)
│   float2 uv = sample_cdf(random)           │     
│                                            │
│ Step 2: Look up RGB color at UV            │  ← Original EXR texture
│   float3 rgb = environment_map.Sample(uv)  │     (full RGB radiance)
│                                            │
│ Step 3: Get PDF for MIS weight             │  ← CDF data again
│   float pdf = environment_pdf(uv)          │
└────────────────────────────────────────────┘
```

**CDF is grayscale** (based on luminance), **texture is RGB** (actual colors)!

---

## Implementation: 6 Stages

### Stage 1: CPU-Side CDF Construction

**Goal:** Build 2D sampling distribution from EXR luminance

**Algorithm:**

```cpp
// Pseudocode for CDF construction
void build_environment_cdf(const HDRImage& exr) {
    int width = exr.width;
    int height = exr.height;
    
    // Step 1: Calculate luminance with solid angle weighting
    std::vector<std::vector<float>> luminance(height, std::vector<float>(width));
    for (int v = 0; v < height; ++v) {
        float theta = (v + 0.5f) / height * M_PI;
        float sin_theta = sin(theta);  // Solid angle correction!
        
        for (int u = 0; u < width; ++u) {
            int idx = (v * width + u) * 4;  // RGBA
            float r = exr.data[idx + 0];
            float g = exr.data[idx + 1];
            float b = exr.data[idx + 2];
            
            // Luminance formula
            float lum = 0.212671f * r + 0.715160f * g + 0.072169f * b;
            
            // Weight by solid angle (pixels near poles are smaller)
            luminance[v][u] = lum * sin_theta;
        }
    }
    
    // Step 2: Build conditional CDFs (one per row)
    std::vector<std::vector<float>> conditional_cdfs(height);
    for (int v = 0; v < height; ++v) {
        conditional_cdfs[v].resize(width);
        
        // Compute CDF for this row
        float sum = 0.0f;
        for (int u = 0; u < width; ++u) {
            sum += luminance[v][u];
            conditional_cdfs[v][u] = sum;
        }
        
        // Normalize to [0, 1]
        if (sum > 0) {
            for (int u = 0; u < width; ++u) {
                conditional_cdfs[v][u] /= sum;
            }
        }
    }
    
    // Step 3: Build marginal CDF (sum of rows)
    std::vector<float> marginal_cdf(height);
    float total = 0.0f;
    for (int v = 0; v < height; ++v) {
        // Sum of row v (before normalization)
        float row_sum = 0.0f;
        for (int u = 0; u < width; ++u) {
            row_sum += luminance[v][u];
        }
        
        total += row_sum;
        marginal_cdf[v] = total;
    }
    
    // Normalize marginal CDF
    if (total > 0) {
        for (int v = 0; v < height; ++v) {
            marginal_cdf[v] /= total;
        }
    }
    
    // Store for GPU upload
    this->marginal_cdf = marginal_cdf;
    this->conditional_cdfs = conditional_cdfs;
}
```

**Key Points:**
- **sin(theta) weighting is critical!** Accounts for pixel solid angle
- Marginal CDF: probability distribution over rows (V coordinate)
- Conditional CDF: probability distribution over columns given row (U coordinate)

**Validation Tests:**

```cpp
// Test 1: CDF monotonicity
for (int v = 0; v < height; ++v) {
    for (int u = 1; u < width; ++u) {
        assert(conditional_cdfs[v][u] >= conditional_cdfs[v][u-1]);
    }
}

// Test 2: CDF normalization
for (int v = 0; v < height; ++v) {
    assert(abs(conditional_cdfs[v].back() - 1.0f) < 0.001f);
}
assert(abs(marginal_cdf.back() - 1.0f) < 0.001f);

// Test 3: Uniform environment → linear CDF
// Load white.exr (all pixels = 1.0)
// CDF should be linear: cdf[i] ≈ i / size
```

**Files to Create/Modify:**
- `util/environment_sampling.h` - New file for CDF structure
- `util/environment_sampling.cpp` - Implementation
- `backends/dxr/render_dxr.cpp` - Call from environment load

**Deliverables:**
- [ ] CDF construction function
- [ ] Unit tests pass
- [ ] Debug: print CDF statistics (min, max, avg)
- [ ] Optional: Save CDF as grayscale image for visualization

---

### Stage 2: GPU Upload & Resource Binding

**Goal:** Make CDF data accessible in shaders

**GPU Resources Needed:**

```cpp
// In render_dxr.h
class RenderDXR {
private:
    // Existing environment map
    dxr::Texture2D env_map;
    D3D12_GPU_DESCRIPTOR_HANDLE env_map_srv;
    
    // NEW: CDF buffers
    dxr::Buffer marginal_cdf_buffer;
    dxr::Buffer conditional_cdf_buffer;
    D3D12_GPU_DESCRIPTOR_HANDLE marginal_srv;
    D3D12_GPU_DESCRIPTOR_HANDLE conditional_srv;
    
    // Metadata
    uint32_t env_width;
    uint32_t env_height;
};
```

**Upload Code:**

```cpp
void RenderDXR::upload_environment_cdf(
    const std::vector<float>& marginal_cdf,
    const std::vector<std::vector<float>>& conditional_cdfs)
{
    // Create marginal CDF buffer
    size_t marginal_size = marginal_cdf.size() * sizeof(float);
    marginal_cdf_buffer = dxr::Buffer::device(
        device.Get(),
        marginal_size,
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    
    // Upload marginal via staging
    // ... (similar to existing buffer uploads)
    
    // Flatten conditional CDFs into 1D array
    std::vector<float> conditional_flat;
    conditional_flat.reserve(env_height * env_width);
    for (const auto& row : conditional_cdfs) {
        conditional_flat.insert(conditional_flat.end(), row.begin(), row.end());
    }
    
    // Create conditional CDF buffer
    size_t conditional_size = conditional_flat.size() * sizeof(float);
    conditional_cdf_buffer = dxr::Buffer::device(
        device.Get(),
        conditional_size,
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    
    // Upload conditional via staging
    // ... (similar to existing buffer uploads)
    
    // Create SRVs
    // Add to descriptor heap
    // Bind to root signature
}
```

**Shader Bindings:**

```glsl
// In unified_render.slang
StructuredBuffer<float> env_marginal_cdf : register(t20);
StructuredBuffer<float> env_conditional_cdf : register(t21);

cbuffer EnvParams : register(b3) {
    uint env_width;
    uint env_height;
}
```

**Validation Tests:**

```cpp
// Test 1: Buffers created successfully
assert(marginal_cdf_buffer.resource != nullptr);
assert(conditional_cdf_buffer.resource != nullptr);

// Test 2: Readback verification
// Copy GPU buffer back to CPU, verify first/last values
std::vector<float> readback = readback_buffer(marginal_cdf_buffer);
assert(abs(readback[0] - marginal_cdf[0]) < 0.0001f);
assert(abs(readback.back() - marginal_cdf.back()) < 0.0001f);
```

**Files to Modify:**
- `backends/dxr/render_dxr.h` - Add buffer members
- `backends/dxr/render_dxr.cpp` - Upload implementation
- `shaders/unified_render.slang` - Add buffer declarations

**Deliverables:**
- [ ] GPU buffers uploaded
- [ ] Readable in shader (verify via PIX/NSight)
- [ ] Readback test passes

---

### Stage 3: Sampling Function (CPU Testing)

**Goal:** Implement sampling algorithm and test thoroughly on CPU FIRST

**Why CPU first?**
- Easy to debug (print statements)
- Fast iteration (no shader recompile)
- Can validate against ground truth
- Once working, shader port is trivial

**Implementation:**

```cpp
// environment_sampling.cpp

struct EnvSample {
    float u, v;     // UV coordinates [0, 1)
    float pdf;      // Probability density at this UV
};

// Binary search helper
int binary_search(const std::vector<float>& cdf, float value) {
    int left = 0;
    int right = cdf.size() - 1;
    
    while (left < right) {
        int mid = (left + right) / 2;
        if (cdf[mid] < value) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    
    return std::clamp(left, 0, (int)cdf.size() - 1);
}

EnvSample sample_environment_map(
    float random_u, float random_v,
    const std::vector<float>& marginal_cdf,
    const std::vector<std::vector<float>>& conditional_cdfs,
    int width, int height)
{
    EnvSample result;
    
    // Step 1: Sample V coordinate from marginal CDF
    int v_index = binary_search(marginal_cdf, random_v);
    
    // Step 2: Sample U coordinate from conditional CDF[v]
    int u_index = binary_search(conditional_cdfs[v_index], random_u);
    
    // Step 3: Convert indices to UV coordinates [0, 1)
    // Use center of pixel
    result.u = (u_index + 0.5f) / width;
    result.v = (v_index + 0.5f) / height;
    
    // Step 4: Calculate PDF
    // PDF = (luminance at pixel) / (integral of luminance over sphere)
    
    // Get probability from CDFs
    float marginal_pdf = (v_index == 0) 
        ? marginal_cdf[0] 
        : (marginal_cdf[v_index] - marginal_cdf[v_index - 1]);
    
    float conditional_pdf = (u_index == 0)
        ? conditional_cdfs[v_index][0]
        : (conditional_cdfs[v_index][u_index] - conditional_cdfs[v_index][u_index - 1]);
    
    // Convert from UV space to solid angle
    float theta = result.v * M_PI;
    float sin_theta = sin(theta);
    
    // PDF in solid angle = PDF_u * PDF_v / sin(theta)
    // Note: We divided by sin_theta when building CDF, so multiply back
    result.pdf = (conditional_pdf * marginal_pdf * width * height) / (2.0f * M_PI * M_PI * sin_theta);
    
    return result;
}

// PDF evaluation (for MIS when BRDF rays hit environment)
float environment_pdf(
    float u, float v,
    const std::vector<float>& marginal_cdf,
    const std::vector<std::vector<float>>& conditional_cdfs,
    int width, int height)
{
    // Convert UV to indices
    int u_index = std::clamp((int)(u * width), 0, width - 1);
    int v_index = std::clamp((int)(v * height), 0, height - 1);
    
    // Get PDFs from CDFs (same as sampling)
    float marginal_pdf = (v_index == 0) 
        ? marginal_cdf[0] 
        : (marginal_cdf[v_index] - marginal_cdf[v_index - 1]);
    
    float conditional_pdf = (u_index == 0)
        ? conditional_cdfs[v_index][0]
        : (conditional_cdfs[v_index][u_index] - conditional_cdfs[v_index][u_index - 1]);
    
    float theta = v * M_PI;
    float sin_theta = sin(theta);
    
    return (conditional_pdf * marginal_pdf * width * height) / (2.0f * M_PI * M_PI * sin_theta);
}
```

**Validation Tests:**

```cpp
// Test 1: Sample distribution matches luminance
void test_sample_distribution() {
    std::vector<int> histogram(width * height, 0);
    
    // Sample 100,000 times
    for (int i = 0; i < 100000; ++i) {
        float ru = random_float();
        float rv = random_float();
        
        EnvSample sample = sample_environment_map(ru, rv, marginal, conditional, w, h);
        
        int u = (int)(sample.u * width);
        int v = (int)(sample.v * height);
        histogram[v * width + u]++;
    }
    
    // Compare histogram to luminance
    // High-luminance pixels should have more hits
    for (int v = 0; v < height; ++v) {
        for (int u = 0; u < width; ++u) {
            float expected = luminance[v][u];  // From original image
            float actual = histogram[v * width + u] / 100000.0f;
            
            // Should be proportional (not exact due to randomness)
            // Check correlation coefficient > 0.9
        }
    }
}

// Test 2: PDF integration
void test_pdf_integration() {
    double integral = 0.0;
    
    for (int v = 0; v < height; ++v) {
        for (int u = 0; u < width; ++u) {
            float uv_u = (u + 0.5f) / width;
            float uv_v = (v + 0.5f) / height;
            
            float pdf = environment_pdf(uv_u, uv_v, marginal, conditional, width, height);
            
            // Solid angle of pixel
            float theta = uv_v * M_PI;
            float sin_theta = sin(theta);
            float dtheta = M_PI / height;
            float dphi = 2.0f * M_PI / width;
            float solid_angle = sin_theta * dtheta * dphi;
            
            integral += pdf * solid_angle;
        }
    }
    
    // PDF should integrate to 1.0
    assert(abs(integral - 1.0) < 0.01);
}

// Test 3: Uniform environment
void test_uniform_environment() {
    // Create white environment (all luminance = 1.0)
    // CDF should be linear
    // Samples should be uniformly distributed
    
    std::vector<float2> samples;
    for (int i = 0; i < 10000; ++i) {
        auto s = sample_environment_map(rand(), rand(), ...);
        samples.push_back({s.u, s.v});
    }
    
    // Check that samples are uniform
    // Use chi-squared test
}

// Test 4: Single bright pixel
void test_single_bright_pixel() {
    // Environment with one bright pixel, rest dark
    // ~90% of samples should hit that pixel
    
    int bright_u = width / 2;
    int bright_v = height / 2;
    int hit_count = 0;
    
    for (int i = 0; i < 10000; ++i) {
        auto s = sample_environment_map(rand(), rand(), ...);
        int u = (int)(s.u * width);
        int v = (int)(s.v * height);
        
        if (u == bright_u && v == bright_v) {
            hit_count++;
        }
    }
    
    // Should hit ~90% of the time (depends on brightness ratio)
    assert(hit_count > 8000);
}
```

**Files to Create:**
- `util/environment_sampling_test.cpp` - Unit tests
- Run as standalone program before GPU integration

**Deliverables:**
- [ ] CPU sampling function complete
- [ ] All unit tests pass
- [ ] Histogram visualization shows correct distribution
- [ ] PDF integrates to 1.0 ± 1%

---

### Stage 4: Port to Shader

**Goal:** Translate working C++ code to HLSL/Slang

**New Shader Module:**

```glsl
// shaders/modules/environment_sampling.slang

// Binary search in CDF (same logic as C++)
int binary_search_cdf(StructuredBuffer<float> cdf, float value, int size) {
    int left = 0;
    int right = size - 1;
    
    while (left < right) {
        int mid = (left + right) / 2;
        if (cdf[mid] < value) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    
    return clamp(left, 0, size - 1);
}

struct EnvSample {
    float2 uv;
    float pdf;
};

// Sample environment map
EnvSample sample_environment_map(
    float2 random,
    StructuredBuffer<float> marginal_cdf,
    StructuredBuffer<float> conditional_cdf,
    uint width,
    uint height)
{
    EnvSample result;
    
    // Sample V from marginal
    int v_idx = binary_search_cdf(marginal_cdf, random.y, height);
    
    // Sample U from conditional[v]
    // Conditional CDF is flattened: row v starts at offset v * width
    int u_idx = binary_search_cdf_offset(conditional_cdf, random.x, width, v_idx * width);
    
    // Convert to UV
    result.uv.x = (u_idx + 0.5f) / float(width);
    result.uv.y = (v_idx + 0.5f) / float(height);
    
    // Calculate PDF
    float marginal_pdf = (v_idx == 0) 
        ? marginal_cdf[0]
        : (marginal_cdf[v_idx] - marginal_cdf[v_idx - 1]);
    
    int cond_idx = v_idx * width + u_idx;
    float conditional_pdf = (u_idx == 0)
        ? conditional_cdf[cond_idx]
        : (conditional_cdf[cond_idx] - conditional_cdf[cond_idx - 1]);
    
    float theta = result.uv.y * M_PI;
    float sin_theta = sin(theta);
    
    result.pdf = (conditional_pdf * marginal_pdf * width * height) 
                 / (2.0f * M_PI * M_PI * sin_theta);
    
    return result;
}

// Binary search with offset (for flattened 2D array)
int binary_search_cdf_offset(
    StructuredBuffer<float> cdf, 
    float value, 
    int size,
    int offset)
{
    int left = 0;
    int right = size - 1;
    
    while (left < right) {
        int mid = (left + right) / 2;
        if (cdf[offset + mid] < value) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    
    return clamp(left, 0, size - 1);
}

// Evaluate PDF at given UV
float environment_pdf(
    float2 uv,
    StructuredBuffer<float> marginal_cdf,
    StructuredBuffer<float> conditional_cdf,
    uint width,
    uint height)
{
    int u_idx = clamp(int(uv.x * width), 0, int(width) - 1);
    int v_idx = clamp(int(uv.y * height), 0, int(height) - 1);
    
    float marginal_pdf = (v_idx == 0)
        ? marginal_cdf[0]
        : (marginal_cdf[v_idx] - marginal_cdf[v_idx - 1]);
    
    int cond_idx = v_idx * width + u_idx;
    float conditional_pdf = (u_idx == 0)
        ? conditional_cdf[cond_idx]
        : (conditional_cdf[cond_idx] - conditional_cdf[cond_idx - 1]);
    
    float theta = uv.y * M_PI;
    float sin_theta = sin(theta);
    
    return (conditional_pdf * marginal_pdf * width * height)
           / (2.0f * M_PI * M_PI * sin_theta);
}
```

**Import in Main Shader:**

```glsl
// unified_render.slang
import modules.environment_sampling;
```

**Validation Test Shader:**

```glsl
// test_env_sampling.slang
// Compute shader that samples and outputs results

[shader("compute")]
[numthreads(256, 1, 1)]
void TestEnvSampling(uint3 thread_id : SV_DispatchThreadID)
{
    uint idx = thread_id.x;
    
    // Generate deterministic random numbers for testing
    float2 random = float2(
        frac(sin(idx * 12.9898) * 43758.5453),
        frac(sin(idx * 78.233) * 43758.5453)
    );
    
    // Sample
    EnvSample sample = sample_environment_map(
        random,
        env_marginal_cdf,
        env_conditional_cdf,
        env_width,
        env_height
    );
    
    // Write to output buffer
    output_uvs[idx] = sample.uv;
    output_pdfs[idx] = sample.pdf;
}
```

**CPU-Side Validation:**

```cpp
// Run test shader with 10,000 threads
dispatch_compute_shader(test_env_sampling, 10000 / 256);

// Read back results
std::vector<float2> gpu_uvs = readback(output_uvs);
std::vector<float> gpu_pdfs = readback(output_pdfs);

// Compare with CPU results (using same random seeds)
for (int i = 0; i < 10000; ++i) {
    float ru = frac(sin(i * 12.9898) * 43758.5453);
    float rv = frac(sin(i * 78.233) * 43758.5453);
    
    auto cpu_sample = sample_environment_map(ru, rv, marginal, conditional, w, h);
    
    // Should match exactly (same math, same inputs)
    assert(abs(gpu_uvs[i].x - cpu_sample.u) < 0.001f);
    assert(abs(gpu_uvs[i].y - cpu_sample.v) < 0.001f);
    assert(abs(gpu_pdfs[i] - cpu_sample.pdf) < 0.01f);
}
```

**Files to Create/Modify:**
- `shaders/modules/environment_sampling.slang` - New module
- `shaders/test_env_sampling.slang` - Test compute shader (optional)
- `backends/dxr/render_dxr.cpp` - Run validation test

**Deliverables:**
- [ ] Shader module compiles
- [ ] GPU results match CPU results exactly
- [ ] Binary search works correctly on GPU

---

### Stage 5: Integration with Direct Lighting

**Goal:** Replace uniform sampling with importance sampling in direct lighting

**Location:** unified_render.slang, direct lighting section (~line 380)

**Before (Phase 2 - Uniform Sampling):**

```glsl
// OLD CODE - uniform sampling
if (env_width > 1) {
    float env_u = lcg_randomf(rng);
    float env_v = lcg_randomf(rng);
    
    float theta = env_v * M_PI;
    float phi = env_u * 2.0f * M_PI - M_PI;
    
    float sin_theta = sin(theta);
    float cos_theta = cos(theta);
    float sin_phi = sin(phi);
    float cos_phi = cos(phi);
    
    float3 wi_env = float3(sin_theta * sin_phi, cos_theta, -sin_theta * cos_phi);
    
    float3 env_radiance = environment_map.SampleLevel(...);
    
    float env_pdf = M_1_PI * 0.25f;  // Uniform: 1/(4π)
    
    // ... trace shadow ray, calculate contribution
}
```

**After (Phase 3 - Importance Sampling):**

```glsl
// NEW CODE - importance sampling
if (env_width > 1) {
    // Sample environment using importance sampling
    float2 random = float2(lcg_randomf(rng), lcg_randomf(rng));
    
    EnvSample env_sample = sample_environment_map(
        random,
        env_marginal_cdf,
        env_conditional_cdf,
        env_width,
        env_height
    );
    
    // Convert UV to direction
    float theta = env_sample.uv.y * M_PI;
    float phi = env_sample.uv.x * 2.0f * M_PI - M_PI;
    
    float sin_theta = sin(theta);
    float cos_theta = cos(theta);
    float sin_phi = sin(phi);
    float cos_phi = cos(phi);
    
    float3 wi_env = float3(sin_theta * sin_phi, cos_theta, -sin_theta * cos_phi);
    
    // Sample environment at importance-sampled UV
    #ifdef VULKAN
    float3 env_radiance = environment_map.SampleLevel(env_sample.uv, 0).rgb;
    #else
    float3 env_radiance = environment_map.SampleLevel(tex_sampler, env_sample.uv, 0).rgb;
    #endif
    
    // Use importance-sampled PDF
    float env_pdf = env_sample.pdf;
    
    // Rest is the same (shadow ray, MIS, contribution)
    // ... (no changes needed)
}
```

**Key Changes:**
1. Call `sample_environment_map()` instead of uniform random
2. Use returned `env_sample.uv` for texture lookup
3. Use `env_sample.pdf` instead of `1/(4π)`
4. Everything else stays the same!

**Validation Tests:**

```cpp
// Test 1: Render comparison
// Render same scene with uniform (Phase 2) and importance (Phase 3)
// Should look identical, but Phase 3 much less noisy

// Test 2: Energy conservation
// White sphere in white environment
// Should still match analytical solution (proves it's unbiased)

// Test 3: Noise measurement
void compare_noise_levels() {
    // Render reference at 100,000 SPP (ground truth)
    render_reference(100000);
    
    // Render with uniform sampling
    for (int spp = 1; spp <= 10000; spp *= 2) {
        render_uniform(spp);
        float mse_uniform = compute_mse(current, reference);
        
        render_importance(spp);
        float mse_importance = compute_mse(current, reference);
        
        printf("SPP %d: Uniform MSE = %.4f, Importance MSE = %.4f, Ratio = %.2fx\n",
               spp, mse_uniform, mse_importance, mse_uniform / mse_importance);
    }
    
    // Expected: Importance sampling has 10-100x lower MSE
}

// Test 4: MIS weights validation
// Print PDF values during rendering
// Should be > 0 and < infinity
// Should vary based on direction (bright = higher PDF)
```

**Debugging Tips:**

```glsl
// Add debug output to see PDFs
if (pixel.x == 512 && pixel.y == 512) {  // Center pixel
    printf("Direct lighting: env_pdf = %f, bsdf_pdf = %f, mis_weight = %f\n",
           env_pdf, bsdf_pdf, mis_weight);
}

// Visualize PDF values
// Render just the PDF at each pixel (helps debug bright spots)
outputTexture[pixel] = float4(env_pdf, env_pdf, env_pdf, 1.0);
```

**Files to Modify:**
- `shaders/unified_render.slang` - Replace uniform with importance sampling

**Deliverables:**
- [ ] Direct lighting uses importance sampling
- [ ] Renders correctly (same look as Phase 2, less noise)
- [ ] Energy conservation test passes
- [ ] Noise reduction measured (10-100x improvement)

---

### Stage 6: Integration with Indirect Lighting (BRDF Sampling)

**Goal:** Update PDF evaluation when BRDF rays hit environment

**Location:** unified_render.slang, BRDF environment hit detection (~line 250)

**Before (Phase 2 - Uniform PDF):**

```glsl
// When BRDF ray hits environment (misses all geometry)
if (payload.color_dist.w < 0.0f) {
    float3 env_radiance = payload.color_dist.rgb;
    
    if (bounce > 0 && env_width > 1 && prev_bsdf_pdf > EPSILON) {
        // OLD: Uniform PDF
        float env_pdf = M_1_PI * 0.25f;
        
        float mis_weight = power_heuristic(1.0f, prev_bsdf_pdf, 1.0f, env_pdf);
        
        sample_illum += path_throughput * env_radiance * mis_weight;
    } else {
        sample_illum += path_throughput * env_radiance;
    }
    
    break;
}
```

**After (Phase 3 - Importance PDF):**

```glsl
// When BRDF ray hits environment (misses all geometry)
if (payload.color_dist.w < 0.0f) {
    float3 env_radiance = payload.color_dist.rgb;
    
    if (bounce > 0 && env_width > 1 && prev_bsdf_pdf > EPSILON) {
        // NEW: Calculate importance-sampled PDF for this direction
        
        // Convert ray direction to UV
        float3 dir = normalize(ray_dir);
        float u = (1.0f + atan2(dir.x, -dir.z) * M_1_PI) * 0.5f;
        float v = acos(dir.y) * M_1_PI;
        
        // Evaluate environment PDF at this UV
        float env_pdf = environment_pdf(
            float2(u, v),
            env_marginal_cdf,
            env_conditional_cdf,
            env_width,
            env_height
        );
        
        float mis_weight = power_heuristic(1.0f, prev_bsdf_pdf, 1.0f, env_pdf);
        
        sample_illum += path_throughput * env_radiance * mis_weight;
    } else {
        sample_illum += path_throughput * env_radiance;
    }
    
    break;
}
```

**Key Changes:**
1. Convert ray direction to UV coordinates
2. Call `environment_pdf()` to get importance-sampled PDF
3. Use this PDF for MIS weight
4. Everything else unchanged

**Validation Tests:**

```cpp
// Test 1: Indirect-only rendering
// Disable direct lighting, only allow bounces
void test_indirect_only() {
    render_with_indirect_only();
    
    // Should still converge to correct result
    // Just slower without direct lighting
    // Proves indirect path works
}

// Test 2: Glossy sphere test
// Sphere with low roughness reflecting environment
void test_glossy_reflection() {
    // Place glossy sphere in environment
    // Should show clear reflection of env map
    // Importance sampling should dramatically reduce noise
    
    render_uniform(spp);
    float noise_uniform = measure_variance();
    
    render_importance(spp);
    float noise_importance = measure_variance();
    
    // Should see 10-100x reduction in variance
    assert(noise_uniform / noise_importance > 10.0f);
}

// Test 3: MIS effectiveness
// Compare different strategies
void test_mis_strategies() {
    // Strategy A: Only direct lighting (sample environment)
    render_direct_only();
    
    // Strategy B: Only BRDF sampling (no direct env sampling)
    render_brdf_only();
    
    // Strategy C: MIS (both strategies)
    render_mis();
    
    // MIS should have lowest variance
    assert(variance_mis < variance_direct);
    assert(variance_mis < variance_brdf);
}

// Test 4: PDF consistency check
// PDF from sampling should match PDF from evaluation
void test_pdf_consistency() {
    // Sample a direction
    EnvSample sample = sample_environment_map(random, ...);
    
    // Evaluate PDF at that direction
    float pdf_eval = environment_pdf(sample.uv, ...);
    
    // Should match (within floating point error)
    assert(abs(sample.pdf - pdf_eval) < 0.01f);
}
```

**Edge Cases to Handle:**

```glsl
// Edge case 1: Ray direction at poles (theta = 0 or π)
// sin(theta) ≈ 0, PDF calculation needs care
if (sin_theta < 0.0001f) {
    // Handle near-pole case
    env_pdf = 0.0f;  // Or very small value
}

// Edge case 2: UV at boundaries (u = 0 or 1, v = 0 or 1)
// Clamp to valid range
float2 uv = clamp(float2(u, v), float2(0.0f), float2(0.9999f));

// Edge case 3: Very dark environment pixels
// PDF might be extremely small but non-zero
if (env_pdf < EPSILON) {
    env_pdf = EPSILON;  // Avoid division by zero in MIS
}
```

**Files to Modify:**
- `shaders/unified_render.slang` - Update BRDF environment hit

**Deliverables:**
- [ ] Indirect lighting uses importance PDF
- [ ] Glossy reflections converge faster
- [ ] MIS test shows combined strategy best
- [ ] All edge cases handled

---

## Complete Integration Checklist

**Phase 3 Complete When:**

- [ ] Stage 1: CDF construction working on CPU
  - [ ] Marginal and conditional CDFs built correctly
  - [ ] Unit tests pass
  - [ ] Validates with uniform environment

- [ ] Stage 2: GPU upload successful
  - [ ] Buffers accessible in shader
  - [ ] Readback verification passes

- [ ] Stage 3: CPU sampling validated
  - [ ] Sampling function correct
  - [ ] PDF integration test passes
  - [ ] Histogram matches luminance distribution

- [ ] Stage 4: Shader port working
  - [ ] GPU results match CPU
  - [ ] Binary search correct

- [ ] Stage 5: Direct lighting integrated
  - [ ] Renders correctly
  - [ ] Energy conservation maintained
  - [ ] Noise reduction measured

- [ ] Stage 6: Indirect lighting integrated
  - [ ] PDF evaluation correct
  - [ ] MIS working properly
  - [ ] All tests pass

---

## Performance Benchmarks

**Expected Results (before/after Phase 3):**

| Environment Type | Noise Reduction | Convergence Speedup |
|-----------------|-----------------|---------------------|
| Uniform (sky only) | 1x | 1x (no benefit) |
| Outdoor (sun + sky) | 20-50x | 15-40x |
| Studio (spot lights) | 50-100x | 40-80x |
| Interior window | 10-30x | 8-25x |

**Per-Sample Overhead:**
- Binary search: ~10-20 instructions
- PDF calculation: ~5-10 instructions
- **Total overhead: ~5-10% slower per sample**
- **But need 10-100x fewer samples!**

---

## Debugging Tools

**Visualizations to Add:**

```glsl
// 1. Show CDF as image
// Save marginal/conditional CDFs as grayscale

// 2. Show PDF map
// Render environment_pdf() at each pixel
outputTexture[pixel] = float4(pdf, pdf, pdf, 1.0);

// 3. Show sampling heatmap
// Accumulate where samples land over time
atomic_add(sample_heatmap[uv], 1);

// 4. Show MIS weights
// Visualize how MIS is balancing strategies
float mis_vis = mis_weight;  // 0 = all BRDF, 1 = all light
outputTexture[pixel] = float4(mis_vis, 0, 1 - mis_vis, 1.0);
```

---

## Rollback Strategy

**If Stage N fails, can rollback:**

- Stage 1-3: Pure CPU work, no risk to GPU code
- Stage 4: Can keep CPU version for testing
- Stage 5: Add `#define USE_IMPORTANCE_SAMPLING` toggle
  ```glsl
  #ifdef USE_IMPORTANCE_SAMPLING
      // Importance sampling code
  #else
      // Fall back to uniform sampling (Phase 2)
  #endif
  ```
- Stage 6: Can use uniform PDF temporarily

**Never break existing Phase 2 functionality!**

---

## References

**Algorithm Source:**
- PBR Book Section 13.6.4: "Piecewise-Constant 2D Distributions"
- PBRT Source: `src/core/sampling.h` - `Distribution2D`

**Implementation Examples:**
- Mitsuba: `src/librender/envmap.cpp`
- Falcor: `Source/Falcor/Scene/Lights/EnvMap.cpp`

**Mathematical Background:**
- Veach Thesis Chapter 9: Light Transport with MIS
- Importance Sampling: Monte Carlo Theory

---

## Success Criteria

**Must Have:**
- [x] Phase 2 uniform sampling working (prerequisite)
- [ ] CDF construction correct (validated on CPU)
- [ ] Importance sampling working (GPU)
- [ ] Direct lighting integrated
- [ ] Indirect lighting integrated
- [ ] 10x+ noise reduction demonstrated
- [ ] Energy conservation maintained

**Should Have:**
- [ ] All unit tests passing
- [ ] Performance benchmarks documented
- [ ] Debug visualizations working
- [ ] Works with Vulkan and DXR

**Nice to Have:**
- [ ] Adaptive resolution CDF (lower res for faster sampling)
- [ ] Runtime environment rotation (rebuild CDF)
- [ ] Multiple environment maps support

---

**Document Version:** 1.0  
**Status:** Ready for Implementation  
**Estimated Time:** 12-20 hours (6 stages × 2-3 hours each)
