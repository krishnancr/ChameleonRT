#include <algorithm>
#include <array>
#include <iostream>
#ifdef _WIN32
#include <intrin.h>
#elif !defined(__aarch64__)
#include <cpuid.h>
#endif
#include "util.h"
#include <glm/ext.hpp>

#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

std::string pretty_print_count(const double count)
{
    const double giga = 1000000000;
    const double mega = 1000000;
    const double kilo = 1000;
    if (count > giga) {
        return std::to_string(count / giga) + " G";
    } else if (count > mega) {
        return std::to_string(count / mega) + " M";
    } else if (count > kilo) {
        return std::to_string(count / kilo) + " K";
    }
    return std::to_string(count);
}

uint64_t align_to(uint64_t val, uint64_t align)
{
    return ((val + align - 1) / align) * align;
}

void ortho_basis(glm::vec3 &v_x, glm::vec3 &v_y, const glm::vec3 &n)
{
    v_y = glm::vec3(0);

    if (n.x < 0.6f && n.x > -0.6f) {
        v_y.x = 1.f;
    } else if (n.y < 0.6f && n.y > -0.6f) {
        v_y.y = 1.f;
    } else if (n.z < 0.6f && n.z > -0.6f) {
        v_y.z = 1.f;
    } else {
        v_y.x = 1.f;
    }
    v_x = glm::normalize(glm::cross(v_y, n));
    v_y = glm::normalize(glm::cross(n, v_x));
}

void canonicalize_path(std::string &path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
}

std::string get_file_extension(const std::string &fname)
{
    const size_t fnd = fname.find_last_of('.');
    if (fnd == std::string::npos) {
        return "";
    }
    return fname.substr(fnd + 1);
}

std::string get_cpu_brand()
{
#if defined(__APPLE__) && defined(__aarch64__)
    return "Apple M1";
#else
    std::string brand = "Unspecified";
    std::array<int32_t, 4> regs;
#ifdef _WIN32
    __cpuid(regs.data(), 0x80000000);
#else
    __cpuid(0x80000000, regs[0], regs[1], regs[2], regs[3]);
#endif
    if (regs[0] >= 0x80000004) {
        char b[64] = {0};
        for (int i = 0; i < 3; ++i) {
#ifdef _WIN32
            __cpuid(regs.data(), 0x80000000 + i + 2);
#else
            __cpuid(0x80000000 + i + 2, regs[0], regs[1], regs[2], regs[3]);
#endif
            std::memcpy(b + i * sizeof(regs), regs.data(), sizeof(regs));
        }
        brand = b;
    }
    return brand;
#endif
}

float srgb_to_linear(float x)
{
    if (x <= 0.04045f) {
        return x / 12.92f;
    }
    return std::pow((x + 0.055f) / 1.055f, 2.4);
}

float linear_to_srgb(float x)
{
    if (x <= 0.0031308f) {
        return 12.92f * x;
    }
    return 1.055f * pow(x, 1.f / 2.4f) - 0.055f;
}

float luminance(const glm::vec3 &c)
{
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

glm::vec4 HDRImage::sample_bilinear(float u, float v) const {
    if (!data) return glm::vec4(0);
    
    // Wrap U (horizontal), clamp V (vertical)
    u = u - std::floor(u);  // Wrap to [0, 1]
    v = glm::clamp(v, 0.0f, 1.0f);
    
    // Convert to pixel coordinates
    float x = u * (width - 1);
    float y = v * (height - 1);
    
    int x0 = (int)std::floor(x);
    int y0 = (int)std::floor(y);
    int x1 = (x0 + 1) % width;   // Wrap horizontally
    int y1 = std::min(y0 + 1, height - 1);  // Clamp vertically
    
    float fx = x - x0;
    float fy = y - y0;
    
    // Bilinear interpolation
    glm::vec4 p00 = get_pixel(x0, y0);
    glm::vec4 p10 = get_pixel(x1, y0);
    glm::vec4 p01 = get_pixel(x0, y1);
    glm::vec4 p11 = get_pixel(x1, y1);
    
    glm::vec4 p0 = glm::mix(p00, p10, fx);
    glm::vec4 p1 = glm::mix(p01, p11, fx);
    
    return glm::mix(p0, p1, fy);
}

HDRImage load_environment_map(const std::string& filename) {
    HDRImage img;
    std::string ext = get_file_extension(filename);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    std::cout << "Loading environment map: " << filename << std::endl;
    
    if (ext == "exr") {
        // Load HDR using TinyEXR
        const char* err = nullptr;
        
        int ret = LoadEXR(&img.data, &img.width, &img.height, 
                          filename.c_str(), &err);
        
        if (ret != TINYEXR_SUCCESS) {
            std::string error_msg = "Failed to load EXR file: " + filename;
            if (err) {
                error_msg += "\n  Reason: " + std::string(err);
                FreeEXRErrorMessage(err);
            }
            throw std::runtime_error(error_msg);
        }
        
        std::cout << "  Loaded EXR: " << img.width << "x" << img.height << std::endl;
        
        // Verify HDR data
        float max_value = 0.0f;
        for (int i = 0; i < img.width * img.height * 4; i++) {
            max_value = std::max(max_value, img.data[i]);
        }
        std::cout << "  Max value: " << max_value 
                  << (max_value > 1.0 ? " (HDR)" : " (LDR?)") << std::endl;
        
    } else if (ext == "jpg" || ext == "jpeg" || ext == "png") {
        // Fallback: Load LDR using stb_image and convert to float
        std::cout << "  WARNING: Loading LDR image (" << ext 
                  << ") - will have limited dynamic range for IBL" << std::endl;
        
        int channels;
        unsigned char* ldr_data = stbi_load(filename.c_str(), 
                                           &img.width, &img.height, 
                                           &channels, 4);
        
        if (!ldr_data) {
            throw std::runtime_error("Failed to load image: " + filename + 
                                   "\n  Reason: " + std::string(stbi_failure_reason()));
        }
        
        // Convert to float RGBA
        size_t pixel_count = img.width * img.height;
        img.data = (float*)malloc(pixel_count * 4 * sizeof(float));
        
        for (size_t i = 0; i < pixel_count * 4; i++) {
            // Convert sRGB [0,255] to linear [0,1] float
            img.data[i] = srgb_to_linear(ldr_data[i] / 255.0f);
        }
        
        stbi_image_free(ldr_data);
        
        std::cout << "  Loaded LDR (converted to float): " 
                  << img.width << "x" << img.height << std::endl;
        
    } else {
        throw std::runtime_error("Unsupported environment map format: " + ext + 
                               " (supported: .exr, .jpg, .png)");
    }
    
    return img;
}
