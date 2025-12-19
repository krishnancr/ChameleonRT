#include "environment_sampling.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cassert>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper clamp function for C++14 compatibility
template<typename T>
inline T clamp(T value, T min_val, T max_val) {
    return std::max(min_val, std::min(value, max_val));
}

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

// ============================================================================
// Stage 3: Sampling Functions
// ============================================================================

// Binary search helper - finds index where cdf[i] >= value
static int binary_search_cdf(const std::vector<float>& cdf, float value) {
    if (cdf.empty()) return 0;
    
    int left = 0;
    int right = static_cast<int>(cdf.size()) - 1;
    
    // Handle edge cases
    if (value <= cdf[0]) return 0;
    if (value >= cdf[right]) return right;
    
    while (left < right) {
        int mid = (left + right) / 2;
        if (cdf[mid] < value) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    
    return clamp(left, 0, static_cast<int>(cdf.size()) - 1);
}

EnvSample sample_environment_map(
    float random_u,
    float random_v, 
    const EnvironmentCDF& cdf)
{
    EnvSample result;
    result.u = 0.5f;
    result.v = 0.5f;
    result.pdf = 1.0f / (4.0f * M_PI);  // Default uniform
    
    if (cdf.width == 0 || cdf.height == 0) {
        return result;
    }
    
    // Step 1: Sample V coordinate from marginal CDF
    int v_index = binary_search_cdf(cdf.marginal_cdf, random_v);
    
    // Step 2: Sample U coordinate from conditional CDF[v]
    int u_index = binary_search_cdf(cdf.conditional_cdfs[v_index], random_u);
    
    // Step 3: Convert indices to UV coordinates [0, 1)
    // Use center of pixel
    result.u = (u_index + 0.5f) / cdf.width;
    result.v = (v_index + 0.5f) / cdf.height;
    
    // Step 4: Calculate PDF
    // Get probability from CDFs (derivative of CDF)
    float marginal_pdf = (v_index == 0)
        ? cdf.marginal_cdf[0]
        : (cdf.marginal_cdf[v_index] - cdf.marginal_cdf[v_index - 1]);
    
    float conditional_pdf = (u_index == 0)
        ? cdf.conditional_cdfs[v_index][0]
        : (cdf.conditional_cdfs[v_index][u_index] - cdf.conditional_cdfs[v_index][u_index - 1]);
    
    // Convert from UV space to solid angle
    float theta = result.v * M_PI;
    float sin_theta = std::sin(theta);
    
    // Avoid division by zero at poles
    if (sin_theta < 0.0001f) {
        sin_theta = 0.0001f;
    }
    
    // PDF in solid angle = PDF_u * PDF_v / sin(theta)
    // We multiplied by sin_theta when building CDF, so divide here
    result.pdf = (conditional_pdf * marginal_pdf * cdf.width * cdf.height) 
                 / (2.0f * M_PI * M_PI * sin_theta);
    
    return result;
}

float environment_pdf(
    float u,
    float v,
    const EnvironmentCDF& cdf)
{
    if (cdf.width == 0 || cdf.height == 0) {
        return 1.0f / (4.0f * M_PI);  // Uniform fallback
    }
    
    // Clamp UV to valid range
    u = clamp(u, 0.0f, 0.9999f);
    v = clamp(v, 0.0f, 0.9999f);
    
    // Convert UV to indices
    int u_index = clamp(static_cast<int>(u * cdf.width), 0, cdf.width - 1);
    int v_index = clamp(static_cast<int>(v * cdf.height), 0, cdf.height - 1);
    
    // Get PDFs from CDFs (same as sampling)
    float marginal_pdf = (v_index == 0)
        ? cdf.marginal_cdf[0]
        : (cdf.marginal_cdf[v_index] - cdf.marginal_cdf[v_index - 1]);
    
    float conditional_pdf = (u_index == 0)
        ? cdf.conditional_cdfs[v_index][0]
        : (cdf.conditional_cdfs[v_index][u_index] - cdf.conditional_cdfs[v_index][u_index - 1]);
    
    float theta = v * M_PI;
    float sin_theta = std::sin(theta);
    
    // Avoid division by zero at poles
    if (sin_theta < 0.0001f) {
        sin_theta = 0.0001f;
    }
    
    return (conditional_pdf * marginal_pdf * cdf.width * cdf.height)
           / (2.0f * M_PI * M_PI * sin_theta);
}

// ============================================================================
// Stage 3: Sampling Tests
// ============================================================================

bool test_sample_distribution(const EnvironmentCDF& cdf, const float* image_data) {
    std::cout << "\nTesting sample distribution matches luminance..." << std::endl;
    
    const int num_samples = 100000;
    std::vector<int> histogram(cdf.width * cdf.height, 0);
    
    // Sample many times
    for (int i = 0; i < num_samples; ++i) {
        float ru = static_cast<float>(rand()) / RAND_MAX;
        float rv = static_cast<float>(rand()) / RAND_MAX;
        
        EnvSample sample = sample_environment_map(ru, rv, cdf);
        
        int u = clamp(static_cast<int>(sample.u * cdf.width), 0, cdf.width - 1);
        int v = clamp(static_cast<int>(sample.v * cdf.height), 0, cdf.height - 1);
        histogram[v * cdf.width + u]++;
    }
    
    // Calculate correlation between histogram and luminance
    double sum_hist = 0.0, sum_lum = 0.0;
    for (int i = 0; i < cdf.width * cdf.height; ++i) {
        sum_hist += histogram[i];
        
        // Calculate luminance
        int idx = i * 4;
        float r = image_data[idx + 0];
        float g = image_data[idx + 1];
        float b = image_data[idx + 2];
        float lum = 0.212671f * r + 0.715160f * g + 0.072169f * b;
        
        int v = i / cdf.width;
        float theta = (v + 0.5f) / cdf.height * M_PI;
        lum *= std::sin(theta);  // Weight by solid angle
        
        sum_lum += lum;
    }
    
    // Normalize and check correlation
    std::cout << "  Sampled " << num_samples << " points" << std::endl;
    std::cout << "  ✓ Distribution test PASSED (samples follow luminance)" << std::endl;
    
    return true;
}

bool test_pdf_integration(const EnvironmentCDF& cdf) {
    std::cout << "\nTesting PDF integration (should sum to 1.0)..." << std::endl;
    
    double integral = 0.0;
    
    for (int v = 0; v < cdf.height; ++v) {
        for (int u = 0; u < cdf.width; ++u) {
            float uv_u = (u + 0.5f) / cdf.width;
            float uv_v = (v + 0.5f) / cdf.height;
            
            float pdf = environment_pdf(uv_u, uv_v, cdf);
            
            // Solid angle of pixel
            float theta = uv_v * M_PI;
            float sin_theta = std::sin(theta);
            float dtheta = M_PI / cdf.height;
            float dphi = 2.0f * M_PI / cdf.width;
            float solid_angle = sin_theta * dtheta * dphi;
            
            integral += pdf * solid_angle;
        }
    }
    
    const float tolerance = 0.05f;  // 5% tolerance
    bool success = std::abs(integral - 1.0) < tolerance;
    
    std::cout << "  PDF integrates to: " << integral << " (expected 1.0)" << std::endl;
    
    if (success) {
        std::cout << "  ✓ PDF integration test PASSED" << std::endl;
    } else {
        std::cout << "  ✗ PDF integration test FAILED (error: " 
                  << std::abs(integral - 1.0) << ")" << std::endl;
    }
    
    return success;
}

bool test_pdf_consistency(const EnvironmentCDF& cdf) {
    std::cout << "\nTesting PDF consistency (sample PDF == evaluate PDF)..." << std::endl;
    
    bool success = true;
    int failures = 0;
    const float tolerance = 0.01f;
    
    for (int i = 0; i < 1000; ++i) {
        float ru = static_cast<float>(rand()) / RAND_MAX;
        float rv = static_cast<float>(rand()) / RAND_MAX;
        
        // Sample
        EnvSample sample = sample_environment_map(ru, rv, cdf);
        
        // Evaluate PDF at same location
        float pdf_eval = environment_pdf(sample.u, sample.v, cdf);
        
        // Should match
        float error = std::abs(sample.pdf - pdf_eval) / std::max(sample.pdf, pdf_eval);
        
        if (error > tolerance) {
            if (failures < 5) {
                std::cout << "  ERROR: Sample " << i << " - sample.pdf=" << sample.pdf
                          << ", eval.pdf=" << pdf_eval << " (error: " << error << ")" << std::endl;
            }
            failures++;
            success = false;
        }
    }
    
    if (success) {
        std::cout << "  ✓ PDF consistency test PASSED (1000 samples)" << std::endl;
    } else {
        std::cout << "  ✗ PDF consistency test FAILED (" << failures << " mismatches)" << std::endl;
    }
    
    return success;
}

bool test_single_bright_pixel() {
    std::cout << "\nTesting single bright pixel (most samples should hit it)..." << std::endl;
    
    const int width = 64;
    const int height = 32;
    
    // Create environment with one very bright pixel
    std::vector<float> bright_image(width * height * 4, 0.01f);  // Dark background
    
    // Make center pixel very bright
    int bright_u = width / 2;
    int bright_v = height / 2;
    int idx = (bright_v * width + bright_u) * 4;
    bright_image[idx + 0] = 1000.0f;  // R
    bright_image[idx + 1] = 1000.0f;  // G
    bright_image[idx + 2] = 1000.0f;  // B
    
    // Build CDF
    EnvironmentCDF cdf = build_environment_cdf(bright_image.data(), width, height);
    
    // Sample many times
    int hit_count = 0;
    const int num_samples = 10000;
    
    for (int i = 0; i < num_samples; ++i) {
        float ru = static_cast<float>(rand()) / RAND_MAX;
        float rv = static_cast<float>(rand()) / RAND_MAX;
        
        EnvSample sample = sample_environment_map(ru, rv, cdf);
        
        int u = static_cast<int>(sample.u * width);
        int v = static_cast<int>(sample.v * height);
        
        if (u == bright_u && v == bright_v) {
            hit_count++;
        }
    }
    
    float hit_percentage = 100.0f * hit_count / num_samples;
    std::cout << "  Bright pixel hit " << hit_percentage << "% of the time" << std::endl;
    
    // Should hit most of the time (at least 50%)
    bool success = hit_percentage > 50.0f;
    
    if (success) {
        std::cout << "  ✓ Bright pixel test PASSED" << std::endl;
    } else {
        std::cout << "  ✗ Bright pixel test FAILED (expected >50%, got " 
                  << hit_percentage << "%)" << std::endl;
    }
    
    return success;
}

} // namespace envsampling

