// Test shader to validate Phase 3.1 module compilation
// This shader imports all modules and uses basic functions from each

import modules.util;
import modules.lcg_rng;
import modules.disney_bsdf;
import modules.lights;

// Dummy structures to make this a valid shader
struct TestPayload {
    float3 color;
};

[shader("raygeneration")]
void TestRayGen()
{
    // Test util.slang
    float3 testColor = float3(0.5, 0.5, 0.5);
    float lum = luminance(testColor);
    float srgb = linear_to_srgb(lum);
    
    float3 normal = float3(0, 1, 0);
    float3 tangent, bitangent;
    ortho_basis(tangent, bitangent, normal);
    
    // Test lcg_rng.slang
    LCGRand rng = get_rng_raygen(0);
    float r1 = lcg_randomf(rng);
    float r2 = lcg_randomf(rng);
    
    // Test disney_bsdf.slang
    DisneyMaterial mat;
    mat.base_color = float3(0.8, 0.8, 0.8);
    mat.metallic = 0.0;
    mat.roughness = 0.5;
    mat.specular = 0.5;
    mat.specular_tint = 0.0;
    mat.anisotropy = 0.0;
    mat.sheen = 0.0;
    mat.sheen_tint = 0.0;
    mat.clearcoat = 0.0;
    mat.clearcoat_gloss = 0.0;
    mat.ior = 1.5;
    mat.specular_transmission = 0.0;
    
    float3 wo = float3(0, 0, 1);
    float3 wi = float3(0.5, 0, 0.5);
    float3 brdf = disney_brdf(mat, normal, wo, wi, tangent, bitangent);
    
    // Test lights.slang
    Light light;
    light.position = float3(0, 5, 0);
    light.type = LIGHT_TYPE_QUAD;
    light.emission = float3(10, 10, 10);
    light.area = 4.0;
    light.v1 = float3(1, 0, 0);
    light.v2 = float3(0, 0, 1);
    
    LightSample ls = sampleLight(light, float3(0, 0, 0), float2(r1, r2));
    
    // All modules imported and used successfully!
}

[shader("miss")]
void TestMiss(inout TestPayload payload)
{
    payload.color = float3(0, 0, 0);
}
