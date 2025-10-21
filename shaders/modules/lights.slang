// Light sampling functions
// NEW module for Phase 3

#ifndef LIGHTS_SLANG
#define LIGHTS_SLANG

import util;

// Light types (using constants instead of #define for Slang compatibility)
static const uint LIGHT_TYPE_QUAD = 0;
static const uint LIGHT_TYPE_SPHERE = 1;

// Light structure (must match C++ side QuadLight structure)
struct Light {
    float3 position;
    uint type;           // LIGHT_TYPE_QUAD, LIGHT_TYPE_SPHERE
    float3 emission;     // Emitted radiance
    float area;          // Surface area
    float3 v1;          // Edge 1 for quad lights
    float pad1;
    float3 v2;          // Edge 2 for quad lights
    float pad2;
};

// Light sample result
struct LightSample {
    float3 position;    // Point on light
    float3 normal;      // Light normal
    float3 wi;          // Direction to light (normalized)
    float3 radiance;    // Emitted radiance
    float distance;     // Distance to light
    float pdf;          // Sampling probability density
};

// Sample a quad area light
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
    
    // PDF: (distance^2) / (area * |cos(theta)|)
    // where theta is angle between light normal and direction to shading point
    float cosTheta = abs(dot(ls.normal, -ls.wi));
    ls.pdf = (ls.distance * ls.distance) / (light.area * cosTheta + 1e-8);
    
    ls.position = pointOnLight;
    
    return ls;
}

// Sample a sphere area light (simplified - treat as point light for now)
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
    
    // PDF: 1.0 for point light
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
