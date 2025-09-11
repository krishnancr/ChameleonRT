// Slang shader validation using proven shader-toy example
// This uses the working example-effect from Slang's own examples

// Shader toy uniforms (same structure as shader-toy example)
cbuffer ShaderToyUniforms
{
    float4 iMouse;
    float2 iResolution;
    float iTime;
}

// GLSL compatibility (from shader-toy.slang)
typedef float2 vec2;
typedef float3 vec3;
typedef float4 vec4;

float fract(float value)
{
    return frac(value);
}

float mix(float a, float b, float t)
{
    return lerp(a, b, t);
}

// Example effect implementation (from example-effect.slang)
float rand(float n)
{
    return fract(sin(n) * 43758.5453123);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float screenScale = length(iResolution.xy);
    vec2 uv = fragCoord / screenScale;

    float frequency = 5.0f;
    vec2 pos = (uv + iTime*vec2(0.25f, 0.0f)) * frequency;

    vec2 center = floor(pos + vec2(0.5));

    float r0 = rand(center.x*3.0f  + center.y*7.0f);
    float r1 = rand(center.x*7.0f  + center.y*13.0f);
    float r2 = rand(center.x*13.0f + center.y*3.0f);

    float p = mix(0.0f, 4.0f, r0);
    float f = mix(5.0f, 8.0f, r1);

    float a = 0.5f * (1.0f + cos(iTime*f + p));

    float rad0 = mix(0.1, 0.4, r2);
    float rad1 = mix(0.2, 0.9, r0);

    float radius = 0.5f*mix(rad0, rad1, a);

    vec2 delta = pos - center;
    float distance = length(delta);

    fragColor.xyz = vec3(r0, r1, r2);
    fragColor.w = 1.0f;

    if(distance > radius) fragColor.xyz = vec3(0.25f);
}

// Vertex shader - generates fullscreen triangle (from shader-toy.slang)
[shader("vertex")]
float4 vertexMain(float2 position : POSITION) : SV_Position
{
    return float4(position, 0.5, 1.0);
}

// Fragment shader - calls the shader toy effect
[shader("fragment")]
float4 fragmentMain(float4 sv_position : SV_Position) : SV_Target
{
    float2 fragCoord = sv_position.xy;
    float4 fragColor = 0;
    mainImage(fragColor, fragCoord);
    return float4(fragColor.xyz, 1);
}
