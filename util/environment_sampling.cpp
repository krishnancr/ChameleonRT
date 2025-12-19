#include "environment_sampling.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cassert>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace envsampling {

EnvironmentCDF build_environment_cdf(const float* image_data, int width, int height) {
    EnvironmentCDF result;
    result.width = width;
    result.height = height;
    
    std::cout << "Building environment CDF for " << width << "x" << height << " map..." << std::endl;
    
    // Step 1: Calculate luminance with solid angle weighting
    std::vector<std::vector<float>> luminance(height, std::vector<float>(width));
    
    for (int v = 0; v < height; ++v) {
        float theta = (v + 0.5f) / height * M_PI;
        float sin_theta = std::sin(theta);  // Solid angle correction
        
        for (int u = 0; u < width; ++u) {
            int idx = (v * width + u) * 4;  // RGBA format
            float r = image_data[idx + 0];
            float g = image_data[idx + 1];
            float b = image_data[idx + 2];
            
            // Luminance formula (Rec. 709)
            float lum = 0.212671f * r + 0.715160f * g + 0.072169f * b;
            
            // Weight by solid angle (pixels near poles are smaller)
            luminance[v][u] = lum * sin_theta;
        }
    }
    
    // Step 2: Build conditional CDFs (one per row)
    result.conditional_cdfs.resize(height);
    std::vector<float> row_sums(height);
    
    for (int v = 0; v < height; ++v) {
        result.conditional_cdfs[v].resize(width);
        
        // Compute CDF for this row
        float sum = 0.0f;
        for (int u = 0; u < width; ++u) {
            sum += luminance[v][u];
            result.conditional_cdfs[v][u] = sum;
        }
        
        // Store row sum for marginal CDF
        row_sums[v] = sum;
        
        // Normalize to [0, 1]
        if (sum > 0) {
            for (int u = 0; u < width; ++u) {
                result.conditional_cdfs[v][u] /= sum;
            }
        } else {
            // Degenerate case: row is all zeros, make uniform
            for (int u = 0; u < width; ++u) {
                result.conditional_cdfs[v][u] = (u + 1) / float(width);
            }
        }
    }
    
    // Step 3: Build marginal CDF (sum of rows)
    result.marginal_cdf.resize(height);
    float total = 0.0f;
    
    for (int v = 0; v < height; ++v) {
        total += row_sums[v];
        result.marginal_cdf[v] = total;
    }
    
    // Normalize marginal CDF
    if (total > 0) {
        for (int v = 0; v < height; ++v) {
            result.marginal_cdf[v] /= total;
        }
    } else {
        // Degenerate case: entire environment is zero
        for (int v = 0; v < height; ++v) {
            result.marginal_cdf[v] = (v + 1) / float(height);
        }
    }
    
    std::cout << "CDF construction complete. Total luminance: " << total << std::endl;
    
    return result;
}

void print_cdf_statistics(const EnvironmentCDF& cdf) {
    std::cout << "\n=== CDF Statistics ===" << std::endl;
    std::cout << "Dimensions: " << cdf.width << "x" << cdf.height << std::endl;
    
    // Marginal CDF stats
    std::cout << "\nMarginal CDF:" << std::endl;
    std::cout << "  First value: " << cdf.marginal_cdf[0] << std::endl;
    std::cout << "  Last value: " << cdf.marginal_cdf.back() << std::endl;
    
    float marginal_min = *std::min_element(cdf.marginal_cdf.begin(), cdf.marginal_cdf.end());
    float marginal_max = *std::max_element(cdf.marginal_cdf.begin(), cdf.marginal_cdf.end());
    std::cout << "  Min: " << marginal_min << ", Max: " << marginal_max << std::endl;
    
    // Conditional CDF stats
    std::cout << "\nConditional CDFs (per-row):" << std::endl;
    float cond_min = 1.0f, cond_max = 0.0f;
    
    for (int v = 0; v < cdf.height; ++v) {
        if (!cdf.conditional_cdfs[v].empty()) {
            float row_min = *std::min_element(cdf.conditional_cdfs[v].begin(), cdf.conditional_cdfs[v].end());
            float row_max = *std::max_element(cdf.conditional_cdfs[v].begin(), cdf.conditional_cdfs[v].end());
            cond_min = std::min(cond_min, row_min);
            cond_max = std::max(cond_max, row_max);
        }
    }
    
    std::cout << "  Overall min: " << cond_min << ", max: " << cond_max << std::endl;
    
    // Sample a few rows
    std::cout << "\nSample rows (first/last values):" << std::endl;
    for (int v : {0, cdf.height / 4, cdf.height / 2, 3 * cdf.height / 4, cdf.height - 1}) {
        if (v < cdf.height && !cdf.conditional_cdfs[v].empty()) {
            std::cout << "  Row " << std::setw(4) << v << ": first=" 
                      << std::setw(10) << cdf.conditional_cdfs[v][0]
                      << ", last=" << std::setw(10) << cdf.conditional_cdfs[v].back() << std::endl;
        }
    }
    
    std::cout << "=====================\n" << std::endl;
}

bool test_cdf_monotonicity(const EnvironmentCDF& cdf) {
    std::cout << "Testing CDF monotonicity..." << std::endl;
    
    bool success = true;
    int violations = 0;
    
    // Test marginal CDF
    for (int v = 1; v < cdf.height; ++v) {
        if (cdf.marginal_cdf[v] < cdf.marginal_cdf[v - 1]) {
            std::cout << "  ERROR: Marginal CDF not monotonic at v=" << v 
                      << " (" << cdf.marginal_cdf[v - 1] << " -> " << cdf.marginal_cdf[v] << ")" << std::endl;
            success = false;
            violations++;
            if (violations >= 10) {
                std::cout << "  (stopping after 10 violations)" << std::endl;
                break;
            }
        }
    }
    
    // Test conditional CDFs
    violations = 0;
    for (int v = 0; v < cdf.height; ++v) {
        for (int u = 1; u < cdf.width; ++u) {
            if (cdf.conditional_cdfs[v][u] < cdf.conditional_cdfs[v][u - 1]) {
                std::cout << "  ERROR: Conditional CDF not monotonic at v=" << v << ", u=" << u
                          << " (" << cdf.conditional_cdfs[v][u - 1] << " -> " << cdf.conditional_cdfs[v][u] << ")" << std::endl;
                success = false;
                violations++;
                if (violations >= 10) {
                    std::cout << "  (stopping after 10 violations)" << std::endl;
                    goto done_conditional;
                }
            }
        }
    }
done_conditional:
    
    if (success) {
        std::cout << "  ✓ Monotonicity test PASSED" << std::endl;
    } else {
        std::cout << "  ✗ Monotonicity test FAILED" << std::endl;
    }
    
    return success;
}

bool test_cdf_normalization(const EnvironmentCDF& cdf) {
    std::cout << "Testing CDF normalization..." << std::endl;
    
    const float tolerance = 0.001f;
    bool success = true;
    
    // Test marginal CDF normalization
    float marginal_last = cdf.marginal_cdf.back();
    if (std::abs(marginal_last - 1.0f) >= tolerance) {
        std::cout << "  ERROR: Marginal CDF not normalized. Last value: " << marginal_last 
                  << " (expected 1.0)" << std::endl;
        success = false;
    } else {
        std::cout << "  ✓ Marginal CDF normalized to " << marginal_last << std::endl;
    }
    
    // Test conditional CDFs normalization
    int failures = 0;
    for (int v = 0; v < cdf.height; ++v) {
        if (!cdf.conditional_cdfs[v].empty()) {
            float cond_last = cdf.conditional_cdfs[v].back();
            if (std::abs(cond_last - 1.0f) >= tolerance) {
                if (failures < 5) {
                    std::cout << "  ERROR: Conditional CDF row " << v << " not normalized. Last value: " 
                              << cond_last << " (expected 1.0)" << std::endl;
                }
                success = false;
                failures++;
            }
        }
    }
    
    if (failures > 0) {
        std::cout << "  ✗ " << failures << " conditional CDF rows failed normalization" << std::endl;
    } else {
        std::cout << "  ✓ All conditional CDFs normalized" << std::endl;
    }
    
    if (success) {
        std::cout << "  ✓ Normalization test PASSED" << std::endl;
    } else {
        std::cout << "  ✗ Normalization test FAILED" << std::endl;
    }
    
    return success;
}

bool test_uniform_environment() {
    std::cout << "\nTesting uniform environment (white image)..." << std::endl;
    
    const int width = 64;
    const int height = 32;
    
    // Create uniform white image (all pixels = 1.0)
    std::vector<float> white_image(width * height * 4, 1.0f);
    
    // Build CDF
    EnvironmentCDF cdf = build_environment_cdf(white_image.data(), width, height);
    
    bool success = true;
    
    // For uniform environment with sin(theta) weighting:
    // - Marginal CDF should match integral of sin(theta) (NOT linear)
    // - Conditional CDFs should be linear (uniform per row)
    
    std::cout << "  Checking marginal CDF against sin(theta) integral..." << std::endl;
    
    // Calculate expected marginal CDF based on sin(theta) weighting
    // CDF(v) = integral from 0 to theta_v of sin(t) dt / integral from 0 to pi of sin(t) dt
    // = (1 - cos(theta_v)) / 2
    const float tolerance = 0.01f;  // 1% tolerance
    
    for (int v = 0; v < height; ++v) {
        float theta = (v + 1.0f) / height * M_PI;
        float expected = (1.0f - std::cos(theta)) / 2.0f;
        float actual = cdf.marginal_cdf[v];
        float error = std::abs(actual - expected);
        
        if (error > tolerance) {
            std::cout << "  ERROR: Marginal CDF at v=" << v << " is " << actual 
                      << ", expected ~" << expected << " (error: " << error << ")" << std::endl;
            success = false;
        }
    }
    
    std::cout << "  Checking conditional CDF linearity (should be uniform per row)..." << std::endl;
    int failures = 0;
    const float linear_tolerance = 0.001f;  // Conditional should be exactly linear
    
    for (int v = 0; v < height; ++v) {
        for (int u = 0; u < width; ++u) {
            float expected = (u + 1) / float(width);
            float actual = cdf.conditional_cdfs[v][u];
            float error = std::abs(actual - expected);
            
            if (error > linear_tolerance) {
                if (failures < 5) {
                    std::cout << "  ERROR: Conditional CDF at v=" << v << ", u=" << u 
                              << " is " << actual << ", expected ~" << expected 
                              << " (error: " << error << ")" << std::endl;
                }
                failures++;
                success = false;
            }
        }
    }
    
    if (failures > 0) {
        std::cout << "  ✗ " << failures << " pixels failed linearity check" << std::endl;
    }
    
    if (success) {
        std::cout << "  ✓ Uniform environment test PASSED" << std::endl;
        std::cout << "    (Correctly handles sin(theta) solid angle weighting)" << std::endl;
    } else {
        std::cout << "  ✗ Uniform environment test FAILED" << std::endl;
    }
    
    return success;
}

} // namespace envsampling
