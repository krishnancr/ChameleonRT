#include "slang_shader_compiler.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace chameleonrt {

SlangShaderCompiler::SlangShaderCompiler() {
    // Create global session
    SlangResult result = slang::createGlobalSession(&globalSession);
    if (SLANG_FAILED(result)) {
        lastError = "Failed to create Slang global session";
        std::cerr << "ERROR: " << lastError << std::endl;
    }
}

SlangShaderCompiler::~SlangShaderCompiler() = default;

std::optional<ShaderBlob> SlangShaderCompiler::compileHLSLToDXIL(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    const std::vector<std::string>& defines
) {
    return compileInternal(
        source, 
        entryPoint, 
        stage, 
        SLANG_SOURCE_LANGUAGE_HLSL, 
        SLANG_DXIL, 
        defines
    );
}

std::optional<ShaderBlob> SlangShaderCompiler::compileGLSLToSPIRV(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    const std::vector<std::string>& defines
) {
    return compileInternal(
        source, 
        entryPoint, 
        stage,
        SLANG_SOURCE_LANGUAGE_GLSL,
        SLANG_SPIRV,
        defines
    );
}

std::optional<ShaderBlob> SlangShaderCompiler::compileSlangToDXIL(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    const std::vector<std::string>& defines
) {
    return compileInternal(
        source, 
        entryPoint, 
        stage,
        SLANG_SOURCE_LANGUAGE_SLANG,
        SLANG_DXIL,
        defines
    );
}

std::optional<ShaderBlob> SlangShaderCompiler::compileSlangToSPIRV(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    const std::vector<std::string>& defines
) {
    return compileInternal(
        source, 
        entryPoint, 
        stage,
        SLANG_SOURCE_LANGUAGE_SLANG,
        SLANG_SPIRV,
        defines
    );
}

std::optional<ShaderBlob> SlangShaderCompiler::compileSlangToMetal(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    const std::vector<std::string>& defines
) {
    return compileInternal(
        source, 
        entryPoint, 
        stage,
        SLANG_SOURCE_LANGUAGE_SLANG,
        SLANG_METAL,
        defines
    );
}

std::optional<ShaderBlob> SlangShaderCompiler::compileInternal(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    SlangSourceLanguage sourceLanguage,
    SlangCompileTarget targetFormat,
    const std::vector<std::string>& defines
) {
    if (!globalSession) {
        lastError = "Global session not initialized";
        return std::nullopt;
    }

    // Create session description
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = targetFormat;
    
    // Use shader model 6.5 for broad compatibility (6.6 for newer features)
    targetDesc.profile = globalSession->findProfile("sm_6_5");
    
    // For SPIRV, set Vulkan 1.2 as target (adjust as needed)
    if (targetFormat == SLANG_SPIRV) {
        targetDesc.profile = globalSession->findProfile("spirv_1_5");
    }
    
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    
    // Set search paths for #include directives (if needed)
    // sessionDesc.searchPaths = ...;
    
    // Create session
    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
        lastError = "Failed to create Slang session";
        return std::nullopt;
    }
    
    // Create module from source
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module;
    
    {
        // Create source blob
        Slang::ComPtr<slang::IBlob> sourceBlob;
        sourceBlob = Slang::ComPtr<slang::IBlob>(
            slang::UnownedRawBlob::create(source.c_str(), source.size())
        );
        
        // Determine file extension based on source language
        const char* pathName = "shader";
        const char* moduleNameStr = "shader";
        switch (sourceLanguage) {
            case SLANG_SOURCE_LANGUAGE_HLSL:
                pathName = "shader.hlsl";
                break;
            case SLANG_SOURCE_LANGUAGE_GLSL:
                pathName = "shader.glsl";
                break;
            case SLANG_SOURCE_LANGUAGE_SLANG:
                pathName = "shader.slang";
                break;
            default:
                pathName = "shader.txt";
                break;
        }
        
        // Load module
        module = session->loadModuleFromSource(
            moduleNameStr,
            pathName,
            sourceBlob,
            diagnostics.writeRef()
        );
        
        // Check diagnostics
        if (diagnostics) {
            const char* diagText = (const char*)diagnostics->getBufferPointer();
            if (diagText && strlen(diagText) > 0) {
                lastError = std::string("Slang compilation diagnostics:\n") + diagText;
                std::cerr << lastError << std::endl;
                if (!module) {
                    return std::nullopt;
                }
            }
        }
        
        if (!module) {
            if (lastError.empty()) {
                lastError = "Failed to load module from source";
            }
            return std::nullopt;
        }
    }
    
    // Find entry point
    Slang::ComPtr<slang::IEntryPoint> entryPointObj;
    SlangResult findResult = module->findEntryPointByName(
        entryPoint.c_str(), 
        entryPointObj.writeRef()
    );
    
    if (SLANG_FAILED(findResult)) {
        lastError = "Failed to find entry point: " + entryPoint;
        return std::nullopt;
    }
    
    // Create composite component for linking
    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module.get());
    componentTypes.push_back(entryPointObj.get());
    
    // Link program
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    SlangResult linkResult = session->createCompositeComponentType(
        componentTypes.data(),
        (SlangInt)componentTypes.size(),
        linkedProgram.writeRef(),
        diagnostics.writeRef()
    );
    
    if (SLANG_FAILED(linkResult)) {
        if (diagnostics) {
            const char* diagText = (const char*)diagnostics->getBufferPointer();
            lastError = std::string("Link error:\n") + (diagText ? diagText : "Unknown error");
        } else {
            lastError = "Failed to link program";
        }
        return std::nullopt;
    }
    
    // Get target code
    Slang::ComPtr<slang::IBlob> targetCode;
    SlangResult getCodeResult = linkedProgram->getTargetCode(
        0,  // Target index
        targetCode.writeRef(),
        diagnostics.writeRef()
    );
    
    if (SLANG_FAILED(getCodeResult)) {
        if (diagnostics) {
            const char* diagText = (const char*)diagnostics->getBufferPointer();
            lastError = std::string("Code generation error:\n") + (diagText ? diagText : "Unknown error");
        } else {
            lastError = "Failed to generate target code";
        }
        return std::nullopt;
    }
    
    // Create result blob
    ShaderBlob result;
    result.entryPoint = entryPoint;
    result.stage = stage;
    
    // Determine target from format
    switch (targetFormat) {
        case SLANG_DXIL:
        case SLANG_DXBC:
            result.target = ShaderTarget::DXIL;
            break;
        case SLANG_SPIRV:
            result.target = ShaderTarget::SPIRV;
            break;
        case SLANG_METAL:
            result.target = ShaderTarget::MetalIR;
            break;
        default:
            result.target = ShaderTarget::DXIL;
            break;
    }
    
    // Copy bytecode
    const uint8_t* codeData = (const uint8_t*)targetCode->getBufferPointer();
    size_t codeSize = targetCode->getBufferSize();
    result.bytecode.assign(codeData, codeData + codeSize);
    
    // Extract reflection data (optional, for future use)
    extractReflection(linkedProgram, result);
    
    return result;
}

SlangStage SlangShaderCompiler::toSlangStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:
            return SLANG_STAGE_VERTEX;
        case ShaderStage::Fragment:
            return SLANG_STAGE_FRAGMENT;
        case ShaderStage::Compute:
            return SLANG_STAGE_COMPUTE;
        case ShaderStage::RayGen:
            return SLANG_STAGE_RAY_GENERATION;
        case ShaderStage::ClosestHit:
            return SLANG_STAGE_CLOSEST_HIT;
        case ShaderStage::AnyHit:
            return SLANG_STAGE_ANY_HIT;
        case ShaderStage::Miss:
            return SLANG_STAGE_MISS;
        case ShaderStage::Intersection:
            return SLANG_STAGE_INTERSECTION;
        case ShaderStage::Callable:
            return SLANG_STAGE_CALLABLE;
        default:
            return SLANG_STAGE_NONE;
    }
}

bool SlangShaderCompiler::extractReflection(
    slang::IComponentType* program, 
    ShaderBlob& blob
) {
    // TODO: Implement reflection data extraction
    // This would parse the Slang reflection API to extract:
    // - Uniform buffer bindings
    // - Texture bindings
    // - Sampler bindings
    // - Push constants
    // etc.
    
    // For now, just return success (not extracting reflection data yet)
    // Future enhancement: populate blob.bindings
    
    (void)program;  // Suppress unused parameter warning
    (void)blob;
    
    return true;
}

std::optional<std::string> SlangShaderCompiler::loadShaderSource(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace chameleonrt
