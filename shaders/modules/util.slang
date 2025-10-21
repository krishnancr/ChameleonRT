// Utility functions and constants for ray tracing
// Ported from backends/dxr/util.hlsl

#ifndef UTIL_SLANG
#define UTIL_SLANG

// Mathematical constants
static const float M_PI = 3.14159265358979323846f;
static const float M_1_PI = 0.318309886183790671538f;
static const float EPSILON = 0.001f;

// Ray types
#define PRIMARY_RAY 0
#define OCCLUSION_RAY 1
#define MAX_PATH_DEPTH 5

// Convert linear color value to sRGB
float linear_to_srgb(float x) {
    if (x <= 0.0031308f) {
        return 12.92f * x;
    }
    return 1.055f * pow(x, 1.f / 2.4f) - 0.055f;
}

// Create an orthonormal basis from a normal vector
// Outputs tangent (v_x) and bitangent (v_y) perpendicular to normal n
void ortho_basis(out float3 v_x, out float3 v_y, const float3 n) {
    v_y = float3(0, 0, 0);

    if (n.x < 0.6f && n.x > -0.6f) {
        v_y.x = 1.f;
    } else if (n.y < 0.6f && n.y > -0.6f) {
        v_y.y = 1.f;
    } else if (n.z < 0.6f && n.z > -0.6f) {
        v_y.z = 1.f;
    } else {
        v_y.x = 1.f;
    }
    v_x = normalize(cross(v_y, n));
    v_y = normalize(cross(n, v_x));
}

// Calculate luminance of RGB color (Rec. 709 coefficients)
float luminance(const float3 c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// Square a value
float pow2(float x) {
    return x * x;
}

#endif // UTIL_SLANG
