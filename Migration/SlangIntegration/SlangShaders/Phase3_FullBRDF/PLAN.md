# Phase 3: Full BRDF - Detailed Plan

**Status:** 📋 Planning  
**Duration:** 3-5 days  
**Risk Level:** MEDIUM-HIGH

---

## Objectives

Implement complete Disney BRDF-based path tracing in Slang. This achieves full feature parity with current implementation, including:
- Physically-based material evaluation (Disney BRDF)
- Direct lighting with shadow rays
- Indirect lighting with recursive path tracing
- Importance sampling for efficiency

---

## Prerequisites

- ✅ Phase 2 completed (material system working)
- ✅ Falcor source code available for reference
- ✅ Current implementation has working Disney BRDF
- ✅ Understanding of path tracing algorithms

---

## Phase 3.1: Import BRDF Modules

**Duration:** 1 day  
**Goal:** Create reusable Slang modules for BRDF, lighting, and utilities

### Module Architecture

```
Phase3_FullBRDF/modules/
├── disney_bsdf.slang       # Disney BRDF evaluation and sampling
├── lights.slang            # Light sampling functions
├── util.slang              # Math utilities, coordinate transforms
└── lcg_rng.slang          # Random number generation
```

### Task 3.1.1: Create Utility Module (2 hours)

**File:** `Phase3_FullBRDF/modules/util.slang`

```slang
// Mathematical constants and helper functions

#ifndef UTIL_SLANG
#define UTIL_SLANG

static const float PI = 3.14159265359;
static const float INV_PI = 0.31830988618;
static const float TWO_PI = 6.28318530718;

// Vector operations
float luminance(float3 rgb) {
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

float sqr(float x) {
    return x * x;
}

// Coordinate system construction
void createOrthonormalBasis(float3 n, out float3 tangent, out float3 bitangent) {
    // Choose tangent perpendicular to normal
    float3 up = abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    tangent = normalize(cross(up, n));
    bitangent = cross(n, tangent);
}

// Transform vector from world space to local (tangent) space
float3 worldToLocal(float3 v, float3 tangent, float3 bitangent, float3 normal) {
    return float3(dot(v, tangent), dot(v, bitangent), dot(v, normal));
}

// Transform vector from local (tangent) space to world space
float3 localToWorld(float3 v, float3 tangent, float3 bitangent, float3 normal) {
    return v.x * tangent + v.y * bitangent + v.z * normal;
}

// Fresnel-Schlick approximation
float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

#endif // UTIL_SLANG
```

**Validation:**
- Compiles as standalone module
- Can be imported by other shaders

### Task 3.1.2: Create RNG Module (1 hour)

**File:** `Phase3_FullBRDF/modules/lcg_rng.slang`

```slang
// Linear Congruential Generator for random numbers

#ifndef LCG_RNG_SLANG
#define LCG_RNG_SLANG

// LCG state (pass by reference)
struct RNGState {
    uint state;
};

// Initialize RNG with seed
void initRNG(inout RNGState rng, uint2 pixel, uint frame) {
    rng.state = (pixel.x * 1973 + pixel.y * 9277 + frame * 26699) | 1;
}

// Generate random uint
uint lcg(inout RNGState rng) {
    const uint a = 1664525u;
    const uint c = 1013904223u;
    rng.state = a * rng.state + c;
    return rng.state;
}

// Generate random float in [0, 1)
float rnd(inout RNGState rng) {
    return float(lcg(rng)) / 4294967296.0;
}

// Generate random float in [min, max)
float rndRange(inout RNGState rng, float minVal, float maxVal) {
    return minVal + (maxVal - minVal) * rnd(rng);
}

// Generate random point on unit disk
float2 sampleDisk(inout RNGState rng) {
    float r = sqrt(rnd(rng));
    float theta = 2.0 * 3.14159265359 * rnd(rng);
    return float2(r * cos(theta), r * sin(theta));
}

// Generate random direction in hemisphere (cosine-weighted)
float3 sampleCosineHemisphere(inout RNGState rng) {
    float2 disk = sampleDisk(rng);
    float z = sqrt(max(0.0, 1.0 - dot(disk, disk)));
    return float3(disk, z);
}

#endif // LCG_RNG_SLANG
```

### Task 3.1.3: Extract Disney BRDF from Falcor (4 hours)

**Reference:** `c:\dev\Falcor\Source\Falcor\Rendering\Materials\BxDF.slang`

**File:** `Phase3_FullBRDF/modules/disney_bsdf.slang`

```slang
// Disney Principled BRDF
// Based on Falcor's implementation and your current disney_bsdf.hlsl

#ifndef DISNEY_BSDF_SLANG
#define DISNEY_BSDF_SLANG

#include "util.slang"

struct DisneyMaterial {
    float3 baseColor;
    float metallic;
    float roughness;
    float specular;
    float specularTint;
    float anisotropic;
    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;
};

// GGX/Trowbridge-Reitz normal distribution function
float D_GGX(float NoH, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float NoH2 = NoH * NoH;
    float denom = (NoH2 * (alpha2 - 1.0) + 1.0);
    return alpha2 / (PI * denom * denom);
}

// Smith GGX visibility function (height-correlated)
float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - alpha2) + alpha2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - alpha2) + alpha2);
    
    return 0.5 / (GGXV + GGXL);
}

// Fresnel term (Schlick approximation)
float3 F_Schlick(float VoH, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

// Diffuse component (Lambertian for now, can upgrade to Disney diffuse)
float3 diffuseLambertian(float3 baseColor) {
    return baseColor * INV_PI;
}

// Evaluate Disney BRDF
// wo: outgoing direction (towards camera)
// wi: incident direction (towards light)
// n: surface normal
float3 evalDisneyBRDF(
    DisneyMaterial mat,
    float3 wo,
    float3 wi,
    float3 n)
{
    float3 h = normalize(wo + wi);
    
    float NoL = max(0.0, dot(n, wi));
    float NoV = max(0.0, dot(n, wo));
    float NoH = max(0.0, dot(n, h));
    float VoH = max(0.0, dot(wo, h));
    
    if (NoL <= 0.0 || NoV <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    
    // Metallic workflow: interpolate between dielectric and conductor
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), mat.baseColor, mat.metallic);
    
    // Specular component (Cook-Torrance)
    float D = D_GGX(NoH, mat.roughness);
    float V = V_SmithGGXCorrelated(NoV, NoL, mat.roughness);
    float3 F = F_Schlick(VoH, F0);
    float3 specular = D * V * F;
    
    // Diffuse component (only for dielectrics)
    float3 kD = (1.0 - F) * (1.0 - mat.metallic);
    float3 diffuse = kD * diffuseLambertian(mat.baseColor);
    
    return diffuse + specular;
}

// Sample Disney BRDF (importance sampling)
// Returns sampled direction and PDF
struct BRDFSample {
    float3 wi;      // Sampled incident direction
    float3 weight;  // BRDF value / PDF
    float pdf;      // Probability density
};

BRDFSample sampleDisneyBRDF(
    DisneyMaterial mat,
    float3 wo,
    float3 n,
    float2 random)
{
    BRDFSample result;
    
    // For now: simple cosine-weighted hemisphere sampling
    // TODO: Importance sample based on roughness/metallic
    
    // Build tangent frame
    float3 tangent, bitangent;
    createOrthonormalBasis(n, tangent, bitangent);
    
    // Sample cosine hemisphere in local space
    float r1 = random.x;
    float r2 = random.y;
    float phi = 2.0 * PI * r1;
    float cosTheta = sqrt(r2);
    float sinTheta = sqrt(1.0 - r2);
    
    float3 localWi = float3(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta
    );
    
    // Transform to world space
    result.wi = localToWorld(localWi, tangent, bitangent, n);
    result.pdf = cosTheta * INV_PI;
    
    // Evaluate BRDF
    float3 brdf = evalDisneyBRDF(mat, wo, result.wi, n);
    
    // Weight = BRDF * cos(theta) / PDF
    // But cosine sampling already has cos/PDF = 1, so weight = BRDF * PI
    result.weight = brdf * PI;
    
    return result;
}

#endif // DISNEY_BSDF_SLANG
```

**Note:** This is a simplified Disney BRDF. For full implementation, reference:
- Your existing `backends/dxr/disney_bsdf.hlsl`
- Falcor's `Source/Falcor/Rendering/Materials/BxDF.slang`
- Disney's original 2012 SIGGRAPH paper

### Task 3.1.4: Create Lights Module (3 hours)

**File:** `Phase3_FullBRDF/modules/lights.slang`

```slang
// Light sampling functions

#ifndef LIGHTS_SLANG
#define LIGHTS_SLANG

#include "util.slang"

// Light types
#define LIGHT_TYPE_QUAD 0
#define LIGHT_TYPE_SPHERE 1

struct Light {
    float3 position;
    uint type;
    float3 emission;
    float area;
    float3 v1;  // For quad lights
    float pad1;
    float3 v2;  // For quad lights
    float pad2;
};

// Light sample result
struct LightSample {
    float3 position;    // Point on light
    float3 normal;      // Light normal
    float3 wi;          // Direction to light
    float3 radiance;    // Emitted radiance
    float distance;     // Distance to light
    float pdf;          // Sampling probability
};

// Sample quad light
LightSample sampleQuadLight(Light light, float3 shadingPoint, float2 random) {
    LightSample ls;
    
    // Sample random point on quad
    float2 uv = random;
    float3 pointOnLight = light.position + uv.x * light.v1 + uv.y * light.v2;
    
    // Direction and distance to light
    float3 toLight = pointOnLight - shadingPoint;
    ls.distance = length(toLight);
    ls.wi = toLight / ls.distance;
    
    // Light normal (cross product of quad edges)
    ls.normal = normalize(cross(light.v1, light.v2));
    
    // Radiance
    ls.radiance = light.emission;
    
    // PDF: 1 / area * (distance^2 / |cos(theta)|)
    float cosTheta = abs(dot(ls.normal, -ls.wi));
    ls.pdf = (ls.distance * ls.distance) / (light.area * cosTheta + 1e-8);
    
    ls.position = pointOnLight;
    
    return ls;
}

// Sample sphere light (simplified - treat as point light)
LightSample sampleSphereLight(Light light, float3 shadingPoint, float2 random) {
    LightSample ls;
    
    // For now: sample center of sphere (point light approximation)
    // TODO: Proper sphere sampling for large lights
    
    float3 toLight = light.position - shadingPoint;
    ls.distance = length(toLight);
    ls.wi = toLight / ls.distance;
    ls.normal = -ls.wi;
    ls.radiance = light.emission;
    ls.position = light.position;
    
    // PDF: 1 / solid angle
    // For point light: PDF = 1.0
    ls.pdf = 1.0;
    
    return ls;
}

// Main light sampling function
LightSample sampleLight(Light light, float3 shadingPoint, float2 random) {
    if (light.type == LIGHT_TYPE_QUAD) {
        return sampleQuadLight(light, shadingPoint, random);
    } else {
        return sampleSphereLight(light, shadingPoint, random);
    }
}

#endif // LIGHTS_SLANG
```

### Deliverables (Phase 3.1)
- [ ] `util.slang` module
- [ ] `lcg_rng.slang` module
- [ ] `disney_bsdf.slang` module
- [ ] `lights.slang` module
- [ ] Compilation tests for each module

### Success Criteria
- ✅ All modules compile independently
- ✅ Can import modules in test shader
- ✅ BRDF evaluation produces reasonable values (test with unit tests)

---

## Phase 3.2: Direct Lighting

**Duration:** 1-2 days  
**Goal:** Evaluate BRDF with direct light sampling (no recursion)

### Shader Design: `direct_lighting.slang`

```slang
#include "modules/util.slang"
#include "modules/lcg_rng.slang"
#include "modules/disney_bsdf.slang"
#include "modules/lights.slang"

// Scene data (from Phase 2)
RaytracingAccelerationStructure scene : register(t0, space0);
RWTexture2D<float4> outputTexture : register(u0, space0);

StructuredBuffer<float3> globalVertices : register(t10, space0);
StructuredBuffer<uint3> globalIndices : register(t11, space0);
StructuredBuffer<float2> globalUVs : register(t12, space0);
StructuredBuffer<float3> globalNormals : register(t13, space0);  // NEW
StructuredBuffer<MeshDesc> meshDescs : register(t14, space0);

StructuredBuffer<MaterialParams> materials : register(t1, space0);
StructuredBuffer<Light> lights : register(t2, space0);  // NEW

Texture2D textures[] : register(t30, space0);
SamplerState linearSampler : register(s0, space0);

cbuffer CameraParams : register(b0, space0) {
    float4x4 invViewProj;
    float3 cameraPosition;
    uint frameCount;
};

cbuffer SceneParams : register(b1, space0) {
    uint numLights;
    uint maxDepth;
    uint samplesPerPixel;
    uint pad;
};

cbuffer HitGroupData : register(b2, space0) {
    uint meshDescIndex;
};

// Payloads
struct Payload {
    float3 color;
    uint depth;
    RNGState rng;
};

struct ShadowPayload {
    bool hit;
};

// ========== Ray Generation ==========

[shader("raygeneration")]
void RayGen() {
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;
    
    // Initialize RNG
    RNGState rng;
    initRNG(rng, pixel, frameCount);
    
    // Calculate ray (with jitter for antialiasing)
    float2 jitter = float2(rnd(rng), rnd(rng)) - 0.5;
    float2 screenPos = (float2(pixel) + jitter + 0.5) / float2(dims);
    screenPos = screenPos * 2.0 - 1.0;
    screenPos.y = -screenPos.y;
    
    float4 target = mul(invViewProj, float4(screenPos, 1.0, 1.0));
    target /= target.w;
    
    RayDesc ray;
    ray.Origin = cameraPosition;
    ray.Direction = normalize(target.xyz - cameraPosition);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    
    Payload payload;
    payload.color = float3(0, 0, 0);
    payload.depth = 0;
    payload.rng = rng;
    
    TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    
    // Accumulate samples (if doing multi-sample per pixel)
    float3 color = payload.color;
    
    // Tone mapping (simple Reinhard)
    color = color / (color + 1.0);
    
    // Gamma correction
    color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    
    outputTexture[pixel] = float4(color, 1.0);
}

// ========== Closest Hit ==========

[shader("closesthit")]
void ClosestHit(
    inout Payload payload,
    in BuiltInTriangleIntersectionAttributes attrib)
{
    // Get intersection data
    MeshDesc mesh = meshDescs[meshDescIndex];
    uint primitiveID = PrimitiveIndex();
    uint3 indices = globalIndices[mesh.ibOffset + primitiveID];
    
    // Get vertex data
    float3 v0 = globalVertices[mesh.vbOffset + indices.x];
    float3 v1 = globalVertices[mesh.vbOffset + indices.y];
    float3 v2 = globalVertices[mesh.vbOffset + indices.z];
    
    float3 n0 = globalNormals[mesh.normalOffset + indices.x];
    float3 n1 = globalNormals[mesh.normalOffset + indices.y];
    float3 n2 = globalNormals[mesh.normalOffset + indices.z];
    
    float2 uv0 = globalUVs[mesh.uvOffset + indices.x];
    float2 uv1 = globalUVs[mesh.uvOffset + indices.y];
    float2 uv2 = globalUVs[mesh.uvOffset + indices.z];
    
    // Interpolate
    float3 bary = float3(
        1.0 - attrib.barycentrics.x - attrib.barycentrics.y,
        attrib.barycentrics.x,
        attrib.barycentrics.y
    );
    
    float3 position = v0 * bary.x + v1 * bary.y + v2 * bary.z;
    float3 normal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    
    // Get material
    MaterialParams mat = materials[mesh.materialID];
    float3 baseColor = textures[mat.baseColorTexIdx].SampleLevel(linearSampler, uv, 0).rgb;
    
    // Create Disney material
    DisneyMaterial disneyMat;
    disneyMat.baseColor = baseColor;
    disneyMat.metallic = mat.metallicFactor;
    disneyMat.roughness = mat.roughnessFactor;
    disneyMat.specular = 0.5;
    disneyMat.specularTint = 0.0;
    disneyMat.anisotropic = 0.0;
    disneyMat.sheen = 0.0;
    disneyMat.sheenTint = 0.0;
    disneyMat.clearcoat = 0.0;
    disneyMat.clearcoatGloss = 0.0;
    
    // Outgoing direction (towards camera)
    float3 wo = -WorldRayDirection();
    
    // Direct lighting
    float3 directLight = float3(0, 0, 0);
    
    for (uint i = 0; i < numLights; i++) {
        Light light = lights[i];
        
        // Sample light
        float2 lightRandom = float2(rnd(payload.rng), rnd(payload.rng));
        LightSample ls = sampleLight(light, position, lightRandom);
        
        // Trace shadow ray
        RayDesc shadowRay;
        shadowRay.Origin = position;
        shadowRay.Direction = ls.wi;
        shadowRay.TMin = 0.001;
        shadowRay.TMax = ls.distance - 0.001;
        
        ShadowPayload shadowPayload;
        shadowPayload.hit = true;
        
        TraceRay(
            scene,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            0xFF,
            0,
            1,
            1,  // Shadow miss shader index
            shadowRay,
            shadowPayload
        );
        
        // If not occluded, evaluate BRDF
        if (!shadowPayload.hit) {
            float3 brdf = evalDisneyBRDF(disneyMat, wo, ls.wi, normal);
            float cosTheta = max(0.0, dot(normal, ls.wi));
            directLight += brdf * ls.radiance * cosTheta / ls.pdf;
        }
    }
    
    payload.color = directLight;
}

// ========== Miss Shaders ==========

[shader("miss")]
void Miss(inout Payload payload) {
    // Environment color (could sample environment map here)
    payload.color = float3(0.1, 0.1, 0.15); // Sky blue
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload) {
    payload.hit = false;
}
```

### Tasks

#### Task 3.2.1: Add Normal Buffer (1 hour)
- Upload smooth normals to global buffer (t13)
- Update MeshDesc with normalOffset

#### Task 3.2.2: Add Light Buffer (1 hour)
- Create light data structure
- Upload scene lights to GPU
- Bind at t2, space0

#### Task 3.2.3: Write Direct Lighting Shader (2 hours)
- Integrate all modules
- Implement closest hit with BRDF evaluation

#### Task 3.2.4: Test Rendering (2 hours)
- Start with single light source
- Verify shadows cast correctly
- Check BRDF response (metals vs dielectrics)

### Deliverables (Phase 3.2)
- [ ] `direct_lighting.slang`
- [ ] Normal buffer implementation
- [ ] Light buffer implementation
- [ ] Screenshots showing direct lighting

### Success Criteria
- ✅ Shadows render correctly
- ✅ BRDF evaluation looks physically plausible
- ✅ Metals and dielectrics render differently
- ✅ Matches current direct lighting quality

---

## Phase 3.3: Path Tracing (Recursive Rays)

**Duration:** 1-2 days  
**Goal:** Full recursive path tracing with indirect lighting

### Shader Update: `path_tracing.slang`

Modify `ClosestHit` to add indirect lighting:

```slang
[shader("closesthit")]
void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attrib)
{
    // ... same setup as Phase 3.2 ...
    
    // Direct lighting (same as Phase 3.2)
    float3 directLight = evaluateDirectLighting(...);
    
    // Indirect lighting (recursive ray)
    float3 indirectLight = float3(0, 0, 0);
    
    if (payload.depth < maxDepth) {
        // Sample BRDF for next direction
        float2 brdfRandom = float2(rnd(payload.rng), rnd(payload.rng));
        BRDFSample brdfSample = sampleDisneyBRDF(disneyMat, wo, normal, brdfRandom);
        
        // Trace indirect ray
        RayDesc indirectRay;
        indirectRay.Origin = position;
        indirectRay.Direction = brdfSample.wi;
        indirectRay.TMin = 0.001;
        indirectRay.TMax = 10000.0;
        
        Payload indirectPayload;
        indirectPayload.color = float3(0, 0, 0);
        indirectPayload.depth = payload.depth + 1;
        indirectPayload.rng = payload.rng;
        
        TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, indirectRay, indirectPayload);
        
        indirectLight = indirectPayload.color * brdfSample.weight;
        
        // Russian roulette termination (after depth > 3)
        if (payload.depth > 3) {
            float prob = min(0.95, luminance(brdfSample.weight));
            if (rnd(payload.rng) > prob) {
                indirectLight = float3(0, 0, 0);
            } else {
                indirectLight /= prob;
            }
        }
    }
    
    payload.color = directLight + indirectLight;
}
```

### Tasks

#### Task 3.3.1: Implement Recursive Tracing (2 hours)
- Add indirect ray tracing to closest hit
- Implement Russian roulette

#### Task 3.3.2: Test Convergence (2 hours)
- Render with different max depths (1, 2, 4, 8)
- Verify convergence over samples
- Compare with current path tracer

#### Task 3.3.3: Optimize Performance (2 hours)
- Profile shader performance
- Optimize hot paths
- Reduce register pressure if needed

### Deliverables (Phase 3.3)
- [ ] `path_tracing.slang`
- [ ] Convergence analysis
- [ ] Performance comparison

### Success Criteria
- ✅ Global illumination renders correctly
- ✅ Converges to correct solution
- ✅ Performance within 5% of current implementation
- ✅ No fireflies or artifacts

---

## Phase 3.4: Validation and Polish

**Duration:** 1 day  
**Goal:** Ensure quality and performance match current implementation

### Tasks

#### Task 3.4.1: Visual Validation (2 hours)
- Render reference scenes (Cornell box, Sponza, etc.)
- Compare pixel-by-pixel with current implementation
- Document any differences

#### Task 3.4.2: Performance Optimization (3 hours)
- Profile with Nsight/RenderDoc
- Optimize material parameter fetches
- Optimize texture sampling
- Ensure functions inline properly

#### Task 3.4.3: Quality Testing (2 hours)
- Test various material types (metal, glass, rough, smooth)
- Test different lighting scenarios
- Test edge cases (grazing angles, etc.)

#### Task 3.4.4: Documentation (1 hour)
- Document shader structure
- Add comments explaining BRDF implementation
- Create usage guide

---

## Deliverables Checklist (Phase 3 Complete)

- [ ] All BRDF modules (util, rng, disney_bsdf, lights)
- [ ] `direct_lighting.slang`
- [ ] `path_tracing.slang`
- [ ] Performance analysis document
- [ ] Visual quality comparisons
- [ ] Known issues and limitations document

---

## Success Criteria (Phase 3 Complete)

### Visual Quality
- ✅ Renders physically plausible images
- ✅ Metals, dielectrics, rough/smooth surfaces correct
- ✅ Global illumination matches reference
- ✅ No visual artifacts (fireflies, bias, etc.)

### Performance
- ✅ Frame time within 5% of current implementation
- ✅ Convergence rate similar or better
- ✅ Memory usage reasonable

### Technical
- ✅ BRDF mathematically correct
- ✅ Importance sampling working
- ✅ Energy conservation verified
- ✅ Works on both DXR and Vulkan

### Production Ready
- ✅ Can be primary rendering path
- ✅ All test scenes work
- ✅ No validation errors
- ✅ Code documented

**Estimated Time:** 3-5 days

**Next Steps:** Production deployment, optimization, advanced features

---

## Known Challenges

### Challenge 1: BRDF Accuracy
**Issue:** Small differences in BRDF evaluation can cause visible differences  
**Mitigation:** 
- Copy exact formulas from current implementation
- Validate against reference images
- Use unit tests for BRDF components

### Challenge 2: Importance Sampling
**Issue:** Poor importance sampling causes slow convergence  
**Mitigation:**
- Start with simple cosine sampling
- Upgrade to GGX importance sampling if needed
- Test with different roughness values

### Challenge 3: Performance Regression
**Issue:** Slang might generate less optimal code  
**Mitigation:**
- Use compiler optimization flags
- Manually inline critical functions if needed
- Profile and optimize hot paths
- Compare generated assembly/DXIL

---

## Reference Materials

- Your existing shaders: `backends/dxr/render_dxr.hlsl`
- Falcor materials: `Source/Falcor/Rendering/Materials/`
- Disney BRDF paper: "Physically-Based Shading at Disney" (SIGGRAPH 2012)
- PBR Book: pbr-book.org
- Real-Time Rendering 4th Edition

---

**READY TO EXECUTE?** 🚀

Once Phase 3 is complete, you'll have a fully functional Slang-based path tracer that works across DXR and Vulkan!
