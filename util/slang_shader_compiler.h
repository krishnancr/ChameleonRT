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
    Callable
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
 * Provides unified interface for compiling shaders through Slang:
 * - Phase 1: Pass-through (HLSL→DXIL, GLSL→SPIRV)
 * - Phase 2: Slang language (Slang→DXIL/SPIRV/Metal)
 * 
 * Usage:
 *   SlangShaderCompiler compiler;
 *   auto blob = compiler.compileHLSLToDXIL(hlslSource, "main", ShaderStage::Vertex);
 *   // Use blob->bytecode in D3D12 pipeline creation
 */
class SlangShaderCompiler {
public:
    SlangShaderCompiler();
    ~SlangShaderCompiler();
    
    // Prevent copying (Slang sessions are not copyable)
    SlangShaderCompiler(const SlangShaderCompiler&) = delete;
    SlangShaderCompiler& operator=(const SlangShaderCompiler&) = delete;
    
    // === PHASE 1: Pass-through compilation ===
    
    /**
     * Compile HLSL to DXIL (for D3D12)
     * @param source HLSL shader source code
     * @param entryPoint Entry point function name
     * @param stage Shader stage (vertex, fragment, raygen, etc.)
     * @param defines Preprocessor defines (optional)
     * @return Compiled shader blob or nullopt on failure
     */
    std::optional<ShaderBlob> compileHLSLToDXIL(
        const std::string& source,
        const std::string& entryPoint,
        ShaderStage stage,
        const std::vector<std::string>& defines = {}
    );
    
    /**
     * Compile HLSL to DXIL Library (for DXR - multiple entry points)
     * This compiles each entry point separately using getEntryPointCode()
     * following the GFX layer pattern from Slang's tools/gfx/renderer-shared.cpp
     * @param source HLSL shader source code with multiple entry points
     * @param searchPaths Directories to search for #include files (optional)
     * @param defines Preprocessor defines (optional)
     * @return Vector of compiled shader blobs (one per entry point) or nullopt on failure
     */
    std::optional<std::vector<ShaderBlob>> compileHLSLToDXILLibrary(
        const std::string& source,
        const std::vector<std::string>& searchPaths = {},
        const std::vector<std::string>& defines = {}
    );
    
    /**
     * Compile GLSL to SPIRV (for Vulkan)
     * @param source GLSL shader source code
     * @param entryPoint Entry point function name
     * @param stage Shader stage
     * @param defines Preprocessor defines (optional)
     * @return Compiled shader blob or nullopt on failure
     */
    std::optional<ShaderBlob> compileGLSLToSPIRV(
        const std::string& source,
        const std::string& entryPoint,
        ShaderStage stage,
        const std::vector<std::string>& defines = {}
    );
    
    // === PHASE 2: Slang language compilation ===
    
    /**
     * Compile Slang to DXIL
     * @param source Slang shader source code
     * @param entryPoint Entry point function name
     * @param stage Shader stage
     * @param defines Preprocessor defines (optional)
     * @return Compiled shader blob or nullopt on failure
     */
    std::optional<ShaderBlob> compileSlangToDXIL(
        const std::string& source,
        const std::string& entryPoint,
        ShaderStage stage,
        const std::vector<std::string>& defines = {}
    );
    
    /**
     * Compile Slang to SPIRV
     * @param source Slang shader source code
     * @param entryPoint Entry point function name
     * @param stage Shader stage
     * @param defines Preprocessor defines (optional)
     * @return Compiled shader blob or nullopt on failure
     */
    std::optional<ShaderBlob> compileSlangToSPIRV(
        const std::string& source,
        const std::string& entryPoint,
        ShaderStage stage,
        const std::vector<std::string>& defines = {}
    );
    
    /**
     * Compile Slang to Metal
     * @param source Slang shader source code
     * @param entryPoint Entry point function name
     * @param stage Shader stage
     * @param defines Preprocessor defines (optional)
     * @return Compiled shader blob or nullopt on failure
     */
    std::optional<ShaderBlob> compileSlangToMetal(
        const std::string& source,
        const std::string& entryPoint,
        ShaderStage stage,
        const std::vector<std::string>& defines = {}
    );
    
    // === Utility functions ===
    
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
    
    /**
     * Internal compilation implementation
     * All public compile functions delegate to this
     */
    std::optional<ShaderBlob> compileInternal(
        const std::string& source,
        const std::string& entryPoint,
        ShaderStage stage,
        SlangSourceLanguage sourceLanguage,
        SlangCompileTarget targetFormat,
        const std::vector<std::string>& defines
    );
    
    /**
     * Convert ShaderStage enum to Slang stage
     */
    SlangStage toSlangStage(ShaderStage stage);
    
    /**
     * Extract reflection data from compiled program
     * (Future: populate ShaderBlob::bindings)
     */
    bool extractReflection(slang::IComponentType* program, ShaderBlob& blob);
};

} // namespace chameleonrt
