// Light sampling functions
// Direct port from backends/dxr/lights.hlsl and backends/vulkan/lights.glsl

#ifndef LIGHTS_SLANG
#define LIGHTS_SLANG

import util;

// Quad-shaped light source
// MUST match C++ QuadLight structure exactly (80 bytes total)
struct QuadLight {
    float4 emission;   // RGB emission + padding (16 bytes)
    float4 position;   // XYZ position + padding (16 bytes)
    float4 normal;     // XYZ normal + padding (16 bytes)
    // x and y vectors spanning the quad, with
    // the half-width and height in the w component
    float4 v_x;        // X-axis tangent (xyz) + half-width (w) (16 bytes)
    float4 v_y;        // Y-axis tangent (xyz) + half-height (w) (16 bytes)
};

// Sample a random position on the quad light surface
float3 sample_quad_light_position(const QuadLight light, float2 samples)
{
    return samples.x * light.v_x.xyz * light.v_x.w + samples.y * light.v_y.xyz * light.v_y.w +
           light.position.xyz;
}

/* Compute the PDF of sampling the sampled point p light with the ray specified by orig and
 * dir, assuming the light is not occluded
 */
float quad_light_pdf(const QuadLight light,
                     const float3 p,
                     const float3 orig,
                     const float3 dir)
{
    float surface_area = light.v_x.w * light.v_y.w;
    float3 to_pt = p - dir;
    float dist_sqr = dot(to_pt, to_pt);
    float n_dot_w = dot(light.normal.xyz, -dir);
    if (n_dot_w < EPSILON) {
        return 0.f;
    }
    return dist_sqr / (n_dot_w * surface_area);
}

// Intersect ray with quad light
bool quad_intersect(const QuadLight light,
                    const float3 orig,
                    const float3 dir,
                    out float t,
                    out float3 light_pos)
{
    float denom = dot(dir, light.normal.xyz);
    if (denom != 0.f) {
        t = dot(light.position.xyz - orig, light.normal.xyz) / denom;
        if (t < 0.f) {
            light_pos = float3(0, 0, 0);
            return false;
        }

        // It's a finite plane so now see if the hit point is actually inside the plane
        light_pos = orig + dir * t;
        float3 hit_v = light_pos - light.position.xyz;
        if (abs(dot(hit_v, light.v_x.xyz)) < light.v_x.w &&
            abs(dot(hit_v, light.v_y.xyz)) < light.v_y.w) {
            return true;
        }
    }
    light_pos = float3(0, 0, 0);
    return false;
}

#endif
