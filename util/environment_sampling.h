#pragma once

#include <vector>
#include <string>

namespace envsampling {

// Structure to hold the CDF data for environment map importance sampling
struct EnvironmentCDF {
    std::vector<float> marginal_cdf;
    std::vector<std::vector<float>> conditional_cdfs;
    int width;
    int height;
    
    EnvironmentCDF() : width(0), height(0) {}
};

// Structure for sampling results (used in later stages)
struct EnvSample {
    float u, v;     // UV coordinates [0, 1)
    float pdf;      // Probability density at this UV
};

// Build CDF from HDR image data
// image_data: RGBA float array (width * height * 4)
// width, height: dimensions of environment map
EnvironmentCDF build_environment_cdf(const float* image_data, int width, int height);

// Sample environment map using importance sampling
// random_u, random_v: uniform random numbers in [0, 1)
// Returns UV coordinates and PDF value
EnvSample sample_environment_map(
    float random_u, 
    float random_v,
    const EnvironmentCDF& cdf);

// Evaluate PDF at given UV coordinate
// u, v: UV coordinates [0, 1)
// Returns probability density at this location
float environment_pdf(
    float u, 
    float v,
    const EnvironmentCDF& cdf);

// Print statistics about the CDF for debugging
void print_cdf_statistics(const EnvironmentCDF& cdf);

// Validation tests (Stage 1)
bool test_cdf_monotonicity(const EnvironmentCDF& cdf);
bool test_cdf_normalization(const EnvironmentCDF& cdf);
bool test_uniform_environment();

// Sampling tests (Stage 3)
bool test_sample_distribution(const EnvironmentCDF& cdf, const float* image_data);
bool test_pdf_integration(const EnvironmentCDF& cdf);
bool test_pdf_consistency(const EnvironmentCDF& cdf);
bool test_single_bright_pixel();

} // namespace envsampling
