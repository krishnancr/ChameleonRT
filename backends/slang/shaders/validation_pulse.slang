// Pulsing gradient shader for validation
// Part of Standard complexity shader validation detour

cbuffer TimeData : register(b0)
{
    float time;
    float padding[3];
}

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
}

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
}

PSInput vertexMain(VSInput input)
{
    PSInput output;
    output.position = float4(input.position, 1.0);
    output.texcoord = input.texcoord;
    return output;
}

float4 fragmentMain(PSInput input) : SV_TARGET
{
    // Create pulsing radial gradient
    float2 uv = input.texcoord;
    float2 center = float2(0.5, 0.5);
    
    // Distance from center
    float dist = length(uv - center);
    
    // Pulsing animation
    float pulse = 0.5 + 0.5 * sin(time * 2.0);
    
    // Create radial gradient with pulse
    float gradient = 1.0 - smoothstep(0.0, 0.7 * pulse, dist);
    
    // Color cycling
    float colorPhase = time * 0.3;
    float3 color1 = float3(0.2, 0.4, 0.8); // Blue
    float3 color2 = float3(0.8, 0.2, 0.4); // Red
    float3 color3 = float3(0.4, 0.8, 0.2); // Green
    
    // Interpolate between colors based on time
    float t = fmod(colorPhase, 3.0);
    float3 finalColor;
    if (t < 1.0)
        finalColor = lerp(color1, color2, t);
    else if (t < 2.0)
        finalColor = lerp(color2, color3, t - 1.0);
    else
        finalColor = lerp(color3, color1, t - 2.0);
    
    return float4(finalColor * gradient, 1.0);
}
