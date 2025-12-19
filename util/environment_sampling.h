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

// Print statistics about the CDF for debugging
void print_cdf_statistics(const EnvironmentCDF& cdf);

// Validation tests
bool test_cdf_monotonicity(const EnvironmentCDF& cdf);
bool test_cdf_normalization(const EnvironmentCDF& cdf);
bool test_uniform_environment();

} // namespace envsampling
