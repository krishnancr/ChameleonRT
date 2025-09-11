// Simple D3D12-focused validation shader 
// Uses minimal HLSL-compatible structure

cbuffer Constants : register(b0)
{
    float4 iMouse;
    float2 iResolution; 
    float iTime;
}

struct VSInput
{
    uint vertexID : SV_VertexID;
};

struct VSOutput  
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Vertex shader
[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    
    // Generate fullscreen triangle (standard approach)
    float2 positions[3] = {
        float2(-1.0, -3.0),
        float2(-1.0,  1.0), 
        float2( 3.0,  1.0)
    };
    
    float2 uvs[3] = {
        float2(0.0, 2.0),
        float2(0.0, 0.0),
        float2(2.0, 0.0)
    };
    
    output.position = float4(positions[input.vertexID], 0.0, 1.0);
    output.uv = uvs[input.vertexID];
    
    return output;
}

// Fragment shader 
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
    float2 uv = input.uv;
    
    // Simple animated pattern
    float2 p = uv * 8.0 + iTime * 0.5;
    float pattern = sin(p.x) * cos(p.y) * 0.5 + 0.5;
    
    // Return animated color
    return float4(pattern, pattern * 0.7, pattern * 0.4, 1.0);
}
