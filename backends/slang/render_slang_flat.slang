// render_slang_flat.slang
// Simple flat color shader for initial Slang backend implementation

// Uniform data for rendering parameters
cbuffer Uniforms
{
    float4x4 viewProjectionMatrix;  // Camera transformation (unused for now)
    float3 flatColor;               // The flat color to render
    float padding;                  // Padding for alignment
}

// Vertex input - simple position
struct VertexInput
{
    float3 position : POSITION;
}

// Vertex shader output / Fragment shader input
struct VertexOutput
{
    float4 position : SV_Position;
}

// Vertex Shader - transforms vertices to screen space
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    // For fullscreen quad, position is already in NDC space
    output.position = float4(input.position, 1.0);
    return output;
}

// Fragment Shader - outputs the flat color
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(flatColor, 1.0);
}
