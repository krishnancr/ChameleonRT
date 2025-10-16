// Simple compute shader - no RT, just test compilation
// This is Phase 1.1 from the Slang integration plan

RWTexture2D<float4> outputTexture : register(u0, space0);

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    uint2 dims;
    outputTexture.GetDimensions(dims.x, dims.y);
    
    // Checkerboard pattern
    bool checker = ((pixel.x / 32) + (pixel.y / 32)) & 1;
    float3 color = checker ? float3(1, 0, 0) : float3(0, 0, 1);
    
    outputTexture[pixel] = float4(color, 1.0);
}