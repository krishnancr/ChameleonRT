#pragma once

#include <string>
#include <glm/glm.hpp>

// Format the count as #G, #M, #K, depending on its magnitude
std::string pretty_print_count(const double count);

uint64_t align_to(uint64_t val, uint64_t align);

void ortho_basis(glm::vec3 &v_x, glm::vec3 &v_y, const glm::vec3 &n);

void canonicalize_path(std::string &path);

std::string get_file_extension(const std::string &fname);

std::string get_cpu_brand();

float srgb_to_linear(const float x);

float linear_to_srgb(const float x);

float luminance(const glm::vec3 &c);

// HDR image loaded from .exr file (or fallback to LDR formats)
struct HDRImage {
    float* data;      // RGBA float data (width * height * 4)
    int width;
    int height;
    
    HDRImage() : data(nullptr), width(0), height(0) {}
    
    ~HDRImage() {
        if (data) {
            free(data);  // TinyEXR uses malloc, so use free
        }
    }
    
    // Prevent copying (data pointer ownership)
    HDRImage(const HDRImage&) = delete;
    HDRImage& operator=(const HDRImage&) = delete;
    
    // Allow moving
    HDRImage(HDRImage&& other) noexcept 
        : data(other.data), width(other.width), height(other.height) {
        other.data = nullptr;
    }
    
    HDRImage& operator=(HDRImage&& other) noexcept {
        if (this != &other) {
            if (data) free(data);
            data = other.data;
            width = other.width;
            height = other.height;
            other.data = nullptr;
        }
        return *this;
    }
    
    // Get pixel at (x, y) - returns RGBA
    glm::vec4 get_pixel(int x, int y) const {
        if (!data || x < 0 || x >= width || y < 0 || y >= height) {
            return glm::vec4(0);
        }
        int idx = (y * width + x) * 4;
        return glm::vec4(data[idx], data[idx+1], data[idx+2], data[idx+3]);
    }
    
    // Sample with bilinear filtering
    glm::vec4 sample_bilinear(float u, float v) const;
};

// Load environment map from .exr file (or fallback to jpg/png)
HDRImage load_environment_map(const std::string& filename);
