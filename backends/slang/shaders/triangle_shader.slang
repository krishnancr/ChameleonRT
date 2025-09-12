// triangle_shader.slang
//
// Simple RGB triangle shader based on Slang triangle example
// Creates a hardcoded triangle with red, green, blue vertices

// Uniform data to be passed from application -> shader.
cbuffer Uniforms
{
    float4x4 modelViewProjection;
}

// Per-vertex attributes to be assembled from bound vertex buffers.
struct AssembledVertex
{
    float3	position : POSITION;
    float3	color    : COLOR;
};

// Output of the vertex shader, and input to the fragment shader.
struct CoarseVertex
{
    float3 color;
};

// Output of the fragment shader
struct Fragment
{
    float4 color;
};

// Vertex Shader
struct VertexStageOutput
{
    CoarseVertex    coarseVertex    : CoarseVertex;
    float4          sv_position     : SV_Position;
};

[shader("vertex")]
VertexStageOutput vertexMain(
    AssembledVertex assembledVertex)
{
    VertexStageOutput output;

    float3 position = assembledVertex.position;
    float3 color    = assembledVertex.color;

    output.coarseVertex.color = color;
    
    // DEBUG: Test with direct position assignment to verify pipeline works
    output.sv_position = float4(position, 1.0);
    
    // TODO: This bypasses the matrix transformation to test if the issue is:
    // 1. Matrix data (incorrect values)
    // 2. Matrix multiplication order (should it be mul(float4(position, 1.0), modelViewProjection)?)
    // 3. Uniform binding (is the matrix reaching the shader correctly?)

    return output;
}

// Fragment Shader
[shader("fragment")]
Fragment fragmentMain(
    CoarseVertex coarseVertex : CoarseVertex) : SV_Target
{
    float3 color = coarseVertex.color;

    Fragment output;
    output.color = float4(color, 1.0);
    return output;
}
