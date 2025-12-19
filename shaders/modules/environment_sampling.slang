// Environment map importance sampling using 2D CDF
// Stage 4: GPU implementation of importance sampling
// Reference: PBRT section 13.6.4 - Piecewise-Constant 2D Distributions

#ifndef ENVIRONMENT_SAMPLING_SLANG
#define ENVIRONMENT_SAMPLING_SLANG

import modules.util;

// Result structure for environment map sampling
struct EnvSample {
    float u;    // Sampled U coordinate [0, 1)
    float v;    // Sampled V coordinate [0, 1)
    float pdf;  // Probability density function value
};

// Binary search in CDF buffer
// Finds the index where cdf[index] >= value
// Parameters:
//   cdf_buffer: CDF array as StructuredBuffer
//   offset: Starting offset in buffer
//   size: Number of elements to search
//   value: Random value to search for [0, 1]
int binary_search_cdf(StructuredBuffer<float> cdf_buffer, uint offset, uint size, float value) {
    if (size == 0) return 0;
    
    int left = 0;
    int right = int(size) - 1;
    
    // Handle edge cases
    if (value <= cdf_buffer[offset]) return 0;
    if (value >= cdf_buffer[offset + right]) return right;
    
    // Binary search loop
    while (left < right) {
        int mid = (left + right) / 2;
        if (cdf_buffer[offset + mid] < value) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    
    return clamp(left, 0, int(size) - 1);
}

// Sample environment map using importance sampling
// Parameters:
//   random_u, random_v: Random numbers in [0, 1]
//   env_marginal_cdf: Marginal CDF buffer (height elements)
//   env_conditional_cdf: Conditional CDF buffer (width * height elements)
//   width, height: Environment map dimensions
// Returns:
//   EnvSample with UV coordinates and PDF value
EnvSample sample_environment_map(
    float random_u,
    float random_v,
    StructuredBuffer<float> env_marginal_cdf,
    StructuredBuffer<float> env_conditional_cdf,
    uint width,
    uint height)
{
    EnvSample result;
    result.u = 0.5f;
    result.v = 0.5f;
    result.pdf = 1.0f / (4.0f * M_PI);  // Default uniform sphere PDF
    
    if (width == 0 || height == 0) {
        return result;
    }
    
    // Step 1: Sample V coordinate from marginal CDF
    int v_index = binary_search_cdf(env_marginal_cdf, 0, height, random_v);
    
    // Step 2: Sample U coordinate from conditional CDF for this row
    // Conditional CDFs are stored row-major: [row0_width_values][row1_width_values]...
    uint conditional_offset = v_index * width;
    int u_index = binary_search_cdf(env_conditional_cdf, conditional_offset, width, random_u);
    
    // Step 3: Convert indices to UV coordinates [0, 1)
    // Use center of pixel for better distribution
    result.u = (float(u_index) + 0.5f) / float(width);
    result.v = (float(v_index) + 0.5f) / float(height);
    
    // Step 4: Calculate PDF
    // PDF = probability from CDF (derivative = difference between adjacent values)
    
    // Marginal PDF for V coordinate
    float marginal_pdf;
    if (v_index == 0) {
        marginal_pdf = env_marginal_cdf[0];
    } else {
        marginal_pdf = env_marginal_cdf[v_index] - env_marginal_cdf[v_index - 1];
    }
    
    // Conditional PDF for U coordinate
    float conditional_pdf;
    if (u_index == 0) {
        conditional_pdf = env_conditional_cdf[conditional_offset];
    } else {
        conditional_pdf = env_conditional_cdf[conditional_offset + u_index] - 
                         env_conditional_cdf[conditional_offset + u_index - 1];
    }
    
    // Step 5: Convert from UV space PDF to solid angle PDF
    // Environment maps use equirectangular projection
    // Solid angle element: dω = sin(θ) dθ dφ
    // UV mapping: θ = v * π, φ = u * 2π
    // Thus: dω = sin(v*π) * π * 2π * du * dv
    
    float theta = result.v * M_PI;
    float sin_theta = sin(theta);
    
    // Avoid division by zero at poles
    if (sin_theta < 0.0001f) {
        sin_theta = 0.0001f;
    }
    
    // PDF in solid angle = PDF_uv / (sin(θ) * area_uv)
    // where area_uv = (2π * π) = 2π²
    float pdf_uv = marginal_pdf * conditional_pdf;
    result.pdf = pdf_uv / (sin_theta * 2.0f * M_PI * M_PI);
    
    // Clamp PDF to reasonable range to avoid numerical issues
    result.pdf = max(result.pdf, 1e-8f);
    
    return result;
}

// Evaluate PDF at a given UV coordinate
// Used for MIS (Multiple Importance Sampling) in Stage 6
// Parameters:
//   u, v: UV coordinates [0, 1]
//   env_marginal_cdf, env_conditional_cdf: CDF buffers
//   width, height: Environment map dimensions
// Returns:
//   PDF value at the given UV coordinate
float environment_pdf(
    float u,
    float v,
    StructuredBuffer<float> env_marginal_cdf,
    StructuredBuffer<float> env_conditional_cdf,
    uint width,
    uint height)
{
    if (width == 0 || height == 0) {
        return 1.0f / (4.0f * M_PI);
    }
    
    // Convert UV to pixel indices
    int u_index = int(u * float(width));
    int v_index = int(v * float(height));
    
    // Clamp to valid range
    u_index = clamp(u_index, 0, int(width) - 1);
    v_index = clamp(v_index, 0, int(height) - 1);
    
    // Get marginal PDF
    float marginal_pdf;
    if (v_index == 0) {
        marginal_pdf = env_marginal_cdf[0];
    } else {
        marginal_pdf = env_marginal_cdf[v_index] - env_marginal_cdf[v_index - 1];
    }
    
    // Get conditional PDF
    uint conditional_offset = v_index * width;
    float conditional_pdf;
    if (u_index == 0) {
        conditional_pdf = env_conditional_cdf[conditional_offset];
    } else {
        conditional_pdf = env_conditional_cdf[conditional_offset + u_index] - 
                         env_conditional_cdf[conditional_offset + u_index - 1];
    }
    
    // Convert to solid angle PDF
    float theta = v * M_PI;
    float sin_theta = sin(theta);
    
    if (sin_theta < 0.0001f) {
        sin_theta = 0.0001f;
    }
    
    float pdf_uv = marginal_pdf * conditional_pdf;
    float pdf = pdf_uv / (sin_theta * 2.0f * M_PI * M_PI);
    
    return max(pdf, 1e-8f);
}

#endif // ENVIRONMENT_SAMPLING_SLANG
