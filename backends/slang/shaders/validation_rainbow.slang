// Rainbow gradient shader for validation
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
    // Create animated rainbow gradient
    float2 uv = input.texcoord;
    
    // Time-based animation
    float t = time * 0.5;
    
    // Rainbow colors based on position and time
    float hue = (uv.x + t) * 6.28318; // 2π for full color cycle
    
    // Convert HSV to RGB for rainbow effect
    float c = 1.0;
    float x = c * (1.0 - abs(fmod(hue / 1.047198, 2.0) - 1.0)); // π/3 = 1.047198
    float m = 0.0;
    
    float3 rgb;
    if (hue < 1.047198)      rgb = float3(c, x, 0);
    else if (hue < 2.094396) rgb = float3(x, c, 0);
    else if (hue < 3.141593) rgb = float3(0, c, x);
    else if (hue < 4.188791) rgb = float3(0, x, c);
    else if (hue < 5.235988) rgb = float3(x, 0, c);
    else                     rgb = float3(c, 0, x);
    
    // Add vertical gradient intensity
    float intensity = 0.3 + 0.7 * (1.0 - uv.y);
    
    return float4(rgb * intensity, 1.0);
}
