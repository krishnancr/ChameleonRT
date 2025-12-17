// Tonemap compute shader - Converts linear HDR to sRGB for display
// Reads from denoise_buffer (when OIDN enabled) or accum_buffer (without OIDN)

// Inline sRGB conversion function (avoid module import issues with sm_6_0 profile)
float linear_to_srgb(float x) {
    if (x <= 0.0031308f) {
        return 12.92f * x;
    }
    return 1.055f * pow(x, 1.f / 2.4f) - 0.055f;
}

struct AccumPixel {
    float4 color;   // RGB accumulated radiance + sample count
    float4 albedo;  // RGB first-hit surface color + alpha
    float4 normal;  // XYZ first-hit normal + alpha
};

#ifdef VULKAN
[[vk::binding(1, 0)]] RWTexture2D<float4> framebuffer;
#ifdef ENABLE_OIDN
[[vk::binding(8, 0)]] RWStructuredBuffer<float4> color_buffer;
#else
[[vk::binding(2, 0)]] RWStructuredBuffer<AccumPixel> accum_buffer;
#endif
#else
// DXR bindings
RWTexture2D<float4> framebuffer : register(u0);
#ifdef ENABLE_OIDN
RWStructuredBuffer<float4> color_buffer : register(u2);
#else
RWStructuredBuffer<AccumPixel> accum_buffer : register(u1);
#endif
#endif

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    const uint2 pixel = dispatchThreadId.xy;
    uint2 dims;
    framebuffer.GetDimensions(dims.x, dims.y);
    if (pixel.x >= dims.x || pixel.y >= dims.y)
        return;

    const uint pixel_index = dims.x * pixel.y + pixel.x;
    
#ifdef ENABLE_OIDN
    // Read from denoised buffer
    const float4 color = color_buffer[pixel_index];
#else
    // Read from accumulated color buffer
    const float4 color = accum_buffer[pixel_index].color;
#endif
    
    // Apply sRGB conversion for display
    framebuffer[pixel] = float4(
        linear_to_srgb(color.r),
        linear_to_srgb(color.g),
        linear_to_srgb(color.b),
        1.0f
    );
}
