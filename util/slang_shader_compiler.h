#pragma once

#include <slang.h>
#include <slang-com-ptr.h>
#include <string>
#include <vector>
#include <optional>

namespace chameleonrt {

// Shader compilation target
enum class ShaderTarget {
    DXIL,        // D3D12
    SPIRV,       // Vulkan
    MetalIR,     // Metal intermediate
    MetalLib     // Metal compiled library
};

// Shader stage type
enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
    RayGen,
    ClosestHit,
    AnyHit,
    Miss,
    Intersection,
    Callable,
    Library      // For shader libraries containing multiple entry points
};

// Compilation result
struct ShaderBlob {
    std::vector<uint8_t> bytecode;
    std::string entryPoint;
    ShaderStage stage;
    ShaderTarget target;
    
    // Reflection data (future expansion)
    struct Binding {
        std::string name;
        uint32_t binding;
        uint32_t set;  // Vulkan descriptor set, or space for D3D12
        uint32_t count;
    };
    std::vector<Binding> bindings;
};

/**
 * Slang shader compiler utility
 * 
 * Provides unified interface for compiling Slang shaders to multiple targets.
 * Currently supports:
 * - Slang → DXIL (DirectX Ray Tracing)
 * - Slang → SPIRV (Vulkan Ray Tracing)
 * - Slang → Metal (EXPERIMENTAL/UNTESTED)
 * 
 * Usage:
 *   SlangShaderCompiler compiler;
 *   auto blobs = compiler.compileSlangToDXILLibrary(slangSource);
 *   // Use blobs[i].bytecode in D3D12 ray tracing pipeline
 */
class SlangShaderCompiler {
public:
    SlangShaderCompiler();
    ~SlangShaderCompiler();
    
    // Prevent copying (Slang sessions are not copyable)
    SlangShaderCompiler(const SlangShaderCompiler&) = delete;
    SlangShaderCompiler& operator=(const SlangShaderCompiler&) = delete;
    
    /**
     * Compile Slang to DXIL Library (for DirectX Ray Tracing)
     * Compiles all ray tracing entry points (RayGen, Miss, ShadowMiss, ClosestHit)
     * @param source Slang shader source code with multiple entry points
     * @param searchPaths Directories to search for imported modules (optional)
     * @param defines Preprocessor defines (optional)
     * @return Vector of compiled shader blobs (one per entry point) or nullopt on failure
     */
    std::optional<std::vector<ShaderBlob>> compileSlangToDXILLibrary(
        const std::string& source,
        const std::vector<std::string>& searchPaths = {},
        const std::vector<std::string>& defines = {}
    );
    
    /**
     * Compile Slang to SPIRV Library (for Vulkan Ray Tracing)
     * Compiles all ray tracing entry points (RayGen, Miss, ShadowMiss, ClosestHit)
     * @param source Slang shader source code containing multiple entry points
     * @param searchPaths Directories to search for imported modules (optional)
     * @param defines Preprocessor defines (optional)
     * @return Vector of compiled shader blobs (one per entry point) or nullopt on failure
     */
    std::optional<std::vector<ShaderBlob>> compileSlangToSPIRVLibrary(
        const std::string& source,
        const std::vector<std::string>& searchPaths = {},
        const std::vector<std::string>& defines = {}
    );
    
    /**
     * Compile Slang to DXIL Compute Shader (for DirectX Compute)
     * @param source Slang shader source code with compute shader entry point
     * @param entryPoint Entry point function name (e.g., "main")
     * @param searchPaths Directories to search for imported modules (optional)
     * @param defines Preprocessor defines (optional)
     * @return Compiled shader blob or nullopt on failure
     */
    std::optional<ShaderBlob> compileSlangToComputeDXIL(
        const std::string& source,
        const std::string& entryPoint,
        const std::vector<std::string>& searchPaths = {},
        const std::vector<std::string>& defines = {}
    );
    
    /**
     * Compile Slang to Metal (EXPERIMENTAL - NOT TESTED)
     * @param source Slang shader source code
     * @param entryPoint Entry point function name
     * @param stage Shader stage
     * @param searchPaths Directories to search for imported modules (optional)
     * @param defines Preprocessor defines (optional)
     * @return Compiled shader blob or nullopt on failure
     */
    std::optional<ShaderBlob> compileSlangToMetal(
        const std::string& source,
        const std::string& entryPoint,
        ShaderStage stage,
        const std::vector<std::string>& searchPaths = {},
        const std::vector<std::string>& defines = {}
    );
    
    /**
     * Load shader source from file
     * @param filepath Path to shader file
     * @return File contents or nullopt if file not found
     */
    static std::optional<std::string> loadShaderSource(const std::string& filepath);
    
    /**
     * Get last error message
     * @return Error string from last failed operation
     */
    const std::string& getLastError() const { return lastError; }
    
    /**
     * Check if compiler is initialized correctly
     * @return true if ready to compile shaders
     */
    bool isValid() const { return globalSession != nullptr; }

private:
    Slang::ComPtr<slang::IGlobalSession> globalSession;
    std::string lastError;
};

} // namespace chameleonrt
