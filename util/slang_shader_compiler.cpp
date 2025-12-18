#include "slang_shader_compiler.h"
#include <fstream>
#include <sstream>

namespace chameleonrt {

SlangShaderCompiler::SlangShaderCompiler() {
    SlangGlobalSessionDesc sessionDesc = {};
    sessionDesc.structureSize = sizeof(SlangGlobalSessionDesc);
    
    SlangResult result = slang::createGlobalSession(&sessionDesc, globalSession.writeRef());
    if (SLANG_FAILED(result)) {
        lastError = "Failed to create Slang global session";
        return;
    }
    
    // Configure DXC compiler path for DXIL signing (Windows SDK)
    std::vector<std::string> sdkVersions = {
        "10.0.26100.0",
        "10.0.22621.0", 
        "10.0.22000.0",
        "10.0.20348.0",
        "10.0.19041.0",
        "10.0.18362.0",
        "10.0.17763.0"
    };
    
    for (const auto& sdkVersion : sdkVersions) {
        std::string dxcBinDir = "C:/Program Files (x86)/Windows Kits/10/bin/" + sdkVersion + "/x64";
        std::string dxcExePath = dxcBinDir + "/dxc.exe";
        std::ifstream dxcFile(dxcExePath);
        if (dxcFile.good()) {
            dxcFile.close();
            globalSession->setDownstreamCompilerPath(SLANG_PASS_THROUGH_DXC, dxcBinDir.c_str());
            break;
        }
    }
}

SlangShaderCompiler::~SlangShaderCompiler() = default;

std::optional<std::vector<ShaderBlob>> SlangShaderCompiler::compileSlangToDXILLibrary(
    const std::string& source,
    const std::vector<std::string>& searchPaths,
    const std::vector<std::string>& defines
) {
    if (!globalSession) {
        lastError = "Global session not initialized";
        return std::nullopt;
    }
    
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_DXIL;
    targetDesc.profile = globalSession->findProfile("lib_6_6");
    
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    
    std::vector<const char*> searchPathPtrs;
    if (!searchPaths.empty()) {
        searchPathPtrs.reserve(searchPaths.size());
        for (const auto& path : searchPaths) {
            searchPathPtrs.push_back(path.c_str());
        }
        sessionDesc.searchPaths = searchPathPtrs.data();
        sessionDesc.searchPathCount = (SlangInt)searchPathPtrs.size();
    }
    
    Slang::ComPtr<slang::ISession> compileSession;
    SlangResult result = globalSession->createSession(sessionDesc, compileSession.writeRef());
    if (SLANG_FAILED(result)) {
        lastError = "Failed to create Slang session";
        return std::nullopt;
    }

    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    Slang::ComPtr<slang::IModule> module;
    
    std::string sourceWithDefines = "";
    for (const auto& define : defines) {
        sourceWithDefines += "#define " + define + "\n";
    }
    sourceWithDefines += source;
    
    module = compileSession->loadModuleFromSourceString(
        "shader",
        "shader.slang",
        sourceWithDefines.c_str(),
        diagnosticsBlob.writeRef()
    );

    if (!module) {
        lastError = "Slang module compilation failed";
        if (diagnosticsBlob) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnosticsBlob->getBufferPointer();
        }
        return std::nullopt;
    }
    
    struct EntryPointInfo {
        std::string name;
        ShaderStage stage;
    };
    
    std::vector<EntryPointInfo> entryPointInfos;
    entryPointInfos.push_back({"RayGen", ShaderStage::RayGen});
    entryPointInfos.push_back({"Miss", ShaderStage::Miss});
    entryPointInfos.push_back({"ShadowMiss", ShaderStage::Miss});
    entryPointInfos.push_back({"ClosestHit", ShaderStage::ClosestHit});
    
    std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPointObjs;
    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module.get());
    
    for (const auto& epInfo : entryPointInfos) {
        Slang::ComPtr<slang::IEntryPoint> ep;
        SlangResult findResult = module->findEntryPointByName(epInfo.name.c_str(), ep.writeRef());
        if (SLANG_SUCCEEDED(findResult)) {
            entryPointObjs.push_back(ep);
            componentTypes.push_back(ep.get());
        }
    }
    
    if (entryPointObjs.empty()) {
        lastError = "No entry points found in Slang shader";
        return std::nullopt;
    }
    
    Slang::ComPtr<slang::IComponentType> compositeProgram;
    SlangResult linkResult = compileSession->createCompositeComponentType(
        componentTypes.data(),
        (SlangInt)componentTypes.size(),
        compositeProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    
    if (SLANG_FAILED(linkResult)) {
        lastError = "Failed to create composite";
        if (diagnosticsBlob) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnosticsBlob->getBufferPointer();
        }
        return std::nullopt;
    }

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    SlangResult linkResult2 = compositeProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
    
    if (SLANG_FAILED(linkResult2)) {
        lastError = "Failed to link composite";
        if (diagnosticsBlob) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnosticsBlob->getBufferPointer();
        }
        return std::nullopt;
    }
    
    std::vector<ShaderBlob> entryPointBlobs;
    for (size_t i = 0; i < entryPointObjs.size(); ++i) {
        Slang::ComPtr<slang::IBlob> entryPointCode;
        Slang::ComPtr<slang::IBlob> entryPointDiagnostics;
        
        SlangResult codeResult = linkedProgram->getEntryPointCode(
            (SlangInt)i,
            0,
            entryPointCode.writeRef(),
            entryPointDiagnostics.writeRef()
        );
        
        if (SLANG_FAILED(codeResult) || !entryPointCode) {
            lastError = "Entry point code generation failed for " + entryPointInfos[i].name;
            if (entryPointDiagnostics) {
                lastError += "\nDiagnostics:\n";
                lastError += (const char*)entryPointDiagnostics->getBufferPointer();
            }
            return std::nullopt;
        }

        ShaderBlob shaderBlob;
        shaderBlob.target = ShaderTarget::DXIL;
        shaderBlob.entryPoint = entryPointInfos[i].name;
        shaderBlob.stage = entryPointInfos[i].stage;
        
        const uint8_t* codeData = (const uint8_t*)entryPointCode->getBufferPointer();
        size_t codeSize = entryPointCode->getBufferSize();
        shaderBlob.bytecode.assign(codeData, codeData + codeSize);
        
        entryPointBlobs.push_back(std::move(shaderBlob));
    }

    return entryPointBlobs;
}

std::optional<std::vector<ShaderBlob>> SlangShaderCompiler::compileSlangToSPIRVLibrary(
    const std::string& source,
    const std::vector<std::string>& searchPaths,
    const std::vector<std::string>& defines
) {
    if (!globalSession) {
        lastError = "Global session not initialized";
        return std::nullopt;
    }
    
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");
    
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    
    std::vector<const char*> searchPathPtrs;
    if (!searchPaths.empty()) {
        searchPathPtrs.reserve(searchPaths.size());
        for (const auto& path : searchPaths) {
            searchPathPtrs.push_back(path.c_str());
        }
        sessionDesc.searchPaths = searchPathPtrs.data();
        sessionDesc.searchPathCount = (SlangInt)searchPathPtrs.size();
    }
    
    Slang::ComPtr<slang::ISession> compileSession;
    SlangResult result = globalSession->createSession(sessionDesc, compileSession.writeRef());
    if (SLANG_FAILED(result)) {
        lastError = "Failed to create Slang session";
        return std::nullopt;
    }

    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    Slang::ComPtr<slang::IModule> module;
    
    std::string sourceWithDefines = "";
    for (const auto& define : defines) {
        sourceWithDefines += "#define " + define + "\n";
    }
    sourceWithDefines += source;
    
    module = compileSession->loadModuleFromSourceString(
        "shader",
        "shader.slang",
        sourceWithDefines.c_str(),
        diagnosticsBlob.writeRef()
    );

    if (!module) {
        lastError = "Slang module compilation failed";
        if (diagnosticsBlob) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnosticsBlob->getBufferPointer();
        }
        return std::nullopt;
    }
    
    struct EntryPointInfo {
        std::string name;
        ShaderStage stage;
    };
    
    std::vector<EntryPointInfo> entryPointInfos;
    entryPointInfos.push_back({"RayGen", ShaderStage::RayGen});
    entryPointInfos.push_back({"Miss", ShaderStage::Miss});
    entryPointInfos.push_back({"ShadowMiss", ShaderStage::Miss});
    entryPointInfos.push_back({"ClosestHit", ShaderStage::ClosestHit});
    
    std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPointObjs;
    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module.get());
    
    for (const auto& epInfo : entryPointInfos) {
        Slang::ComPtr<slang::IEntryPoint> ep;
        SlangResult findResult = module->findEntryPointByName(epInfo.name.c_str(), ep.writeRef());
        if (SLANG_SUCCEEDED(findResult)) {
            entryPointObjs.push_back(ep);
            componentTypes.push_back(ep.get());
        }
    }
    
    if (entryPointObjs.empty()) {
        lastError = "No entry points found in Slang shader";
        return std::nullopt;
    }
    
    Slang::ComPtr<slang::IComponentType> compositeProgram;
    SlangResult linkResult = compileSession->createCompositeComponentType(
        componentTypes.data(),
        (SlangInt)componentTypes.size(),
        compositeProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    
    if (SLANG_FAILED(linkResult)) {
        lastError = "Failed to create composite";
        if (diagnosticsBlob) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnosticsBlob->getBufferPointer();
        }
        return std::nullopt;
    }

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    SlangResult linkResult2 = compositeProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
    
    if (SLANG_FAILED(linkResult2)) {
        lastError = "Failed to link composite";
        if (diagnosticsBlob) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnosticsBlob->getBufferPointer();
        }
        return std::nullopt;
    }
    
    std::vector<ShaderBlob> entryPointBlobs;
    for (size_t i = 0; i < entryPointObjs.size(); ++i) {
        Slang::ComPtr<slang::IBlob> entryPointCode;
        Slang::ComPtr<slang::IBlob> entryPointDiagnostics;
        
        SlangResult codeResult = linkedProgram->getEntryPointCode(
            (SlangInt)i,
            0,
            entryPointCode.writeRef(),
            entryPointDiagnostics.writeRef()
        );
        
        if (SLANG_FAILED(codeResult) || !entryPointCode) {
            lastError = "Entry point code generation failed for " + entryPointInfos[i].name;
            if (entryPointDiagnostics) {
                lastError += "\nDiagnostics:\n";
                lastError += (const char*)entryPointDiagnostics->getBufferPointer();
            }
            return std::nullopt;
        }

        ShaderBlob shaderBlob;
        shaderBlob.target = ShaderTarget::SPIRV;
        shaderBlob.entryPoint = entryPointInfos[i].name;
        shaderBlob.stage = entryPointInfos[i].stage;
        
        const uint8_t* codeData = (const uint8_t*)entryPointCode->getBufferPointer();
        size_t codeSize = entryPointCode->getBufferSize();
        shaderBlob.bytecode.assign(codeData, codeData + codeSize);
        
        entryPointBlobs.push_back(std::move(shaderBlob));
    }

    return entryPointBlobs;
}

std::optional<ShaderBlob> SlangShaderCompiler::compileSlangToComputeDXIL(
    const std::string& source,
    const std::string& entryPoint,
    const std::vector<std::string>& searchPaths,
    const std::vector<std::string>& defines
) {
    if (!globalSession) {
        lastError = "Global session not initialized";
        return std::nullopt;
    }
    
    // Setup DXIL target with compute shader profile (sm_6_0)
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_DXIL;
    targetDesc.profile = globalSession->findProfile("sm_6_0");  // Compute shader profile
    
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    
    // Add search paths
    std::vector<const char*> searchPathPtrs;
    if (!searchPaths.empty()) {
        searchPathPtrs.reserve(searchPaths.size());
        for (const auto& path : searchPaths) {
            searchPathPtrs.push_back(path.c_str());
        }
        sessionDesc.searchPaths = searchPathPtrs.data();
        sessionDesc.searchPathCount = (SlangInt)searchPathPtrs.size();
    }
    
    // Create compilation session
    Slang::ComPtr<slang::ISession> session;
    SlangResult sessionResult = globalSession->createSession(sessionDesc, session.writeRef());
    if (SLANG_FAILED(sessionResult)) {
        lastError = "Failed to create Slang session for compute shader";
        return std::nullopt;
    }
    
    // Prepend defines to source
    std::string sourceWithDefines = "";
    for (const auto& define : defines) {
        sourceWithDefines += "#define " + define + "\n";
    }
    sourceWithDefines += source;
    
    // Load module from source
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module;
    
    module = session->loadModuleFromSourceString(
        "shader",
        "shader.slang",
        sourceWithDefines.c_str(),
        diagnostics.writeRef()
    );
    
    if (!module) {
        lastError = "Module compilation failed";
        if (diagnostics) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnostics->getBufferPointer();
        }
        return std::nullopt;
    }
    
    // Find the entry point
    Slang::ComPtr<slang::IEntryPoint> entryPointObj;
    SlangResult findResult = module->findEntryPointByName(entryPoint.c_str(), entryPointObj.writeRef());
    
    if (SLANG_FAILED(findResult)) {
        lastError = "Entry point '" + entryPoint + "' not found in compute shader";
        return std::nullopt;
    }
    
    // Create composite program with module + entry point
    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module.get());
    componentTypes.push_back(entryPointObj.get());
    
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    SlangResult linkResult = session->createCompositeComponentType(
        componentTypes.data(),
        (SlangInt)componentTypes.size(),
        linkedProgram.writeRef(),
        diagnostics.writeRef()
    );
    
    if (SLANG_FAILED(linkResult)) {
        lastError = "Failed to link compute shader program";
        if (diagnostics) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnostics->getBufferPointer();
        }
        return std::nullopt;
    }
    
    // Generate target code (DXIL bytecode)
    Slang::ComPtr<slang::IBlob> targetCode;
    SlangResult getCodeResult = linkedProgram->getTargetCode(
        0,
        targetCode.writeRef(),
        diagnostics.writeRef()
    );
    
    if (SLANG_FAILED(getCodeResult)) {
        lastError = "Failed to generate DXIL bytecode for compute shader";
        if (diagnostics) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnostics->getBufferPointer();
        }
        return std::nullopt;
    }
    
    // Create shader blob
    ShaderBlob shaderBlob;
    shaderBlob.target = ShaderTarget::DXIL;
    shaderBlob.entryPoint = entryPoint;
    shaderBlob.stage = ShaderStage::Compute;
    
    const uint8_t* codeData = (const uint8_t*)targetCode->getBufferPointer();
    size_t codeSize = targetCode->getBufferSize();
    shaderBlob.bytecode.assign(codeData, codeData + codeSize);
    
    return shaderBlob;
}

std::optional<ShaderBlob> SlangShaderCompiler::compileSlangToComputeSPIRV(
    const std::string& source,
    const std::string& entryPoint,
    const std::vector<std::string>& searchPaths,
    const std::vector<std::string>& defines
) {
    if (!globalSession) {
        lastError = "Global session not initialized";
        return std::nullopt;
    }
    
    // Setup SPIRV target with compute shader profile (glsl_450 for Vulkan compute)
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("glsl_450");  // Vulkan compute shader profile
    
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    
    // Add search paths
    std::vector<const char*> searchPathPtrs;
    if (!searchPaths.empty()) {
        searchPathPtrs.reserve(searchPaths.size());
        for (const auto& path : searchPaths) {
            searchPathPtrs.push_back(path.c_str());
        }
        sessionDesc.searchPaths = searchPathPtrs.data();
        sessionDesc.searchPathCount = (SlangInt)searchPathPtrs.size();
    }
    
    // Create compilation session
    Slang::ComPtr<slang::ISession> session;
    SlangResult sessionResult = globalSession->createSession(sessionDesc, session.writeRef());
    if (SLANG_FAILED(sessionResult)) {
        lastError = "Failed to create Slang session for SPIRV compute shader";
        return std::nullopt;
    }
    
    // Prepend defines to source (add VULKAN define for Vulkan bindings)
    std::string sourceWithDefines = "#define VULKAN\n";
    for (const auto& define : defines) {
        sourceWithDefines += "#define " + define + "\n";
    }
    sourceWithDefines += source;
    
    // Load module from source
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module;
    
    module = session->loadModuleFromSourceString(
        "shader",
        "shader.slang",
        sourceWithDefines.c_str(),
        diagnostics.writeRef()
    );
    
    if (!module) {
        lastError = "Module compilation failed for SPIRV compute";
        if (diagnostics) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnostics->getBufferPointer();
        }
        return std::nullopt;
    }
    
    // Find the entry point
    Slang::ComPtr<slang::IEntryPoint> entryPointObj;
    SlangResult findResult = module->findEntryPointByName(entryPoint.c_str(), entryPointObj.writeRef());
    
    if (SLANG_FAILED(findResult)) {
        lastError = "Entry point '" + entryPoint + "' not found in SPIRV compute shader";
        return std::nullopt;
    }
    
    // Create composite program with module + entry point
    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module.get());
    componentTypes.push_back(entryPointObj.get());
    
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    SlangResult linkResult = session->createCompositeComponentType(
        componentTypes.data(),
        (SlangInt)componentTypes.size(),
        linkedProgram.writeRef(),
        diagnostics.writeRef()
    );
    
    if (SLANG_FAILED(linkResult)) {
        lastError = "Failed to link SPIRV compute shader program";
        if (diagnostics) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnostics->getBufferPointer();
        }
        return std::nullopt;
    }
    
    // Generate target code (SPIRV bytecode)
    Slang::ComPtr<slang::IBlob> targetCode;
    SlangResult getCodeResult = linkedProgram->getTargetCode(
        0,
        targetCode.writeRef(),
        diagnostics.writeRef()
    );
    
    if (SLANG_FAILED(getCodeResult)) {
        lastError = "Failed to generate SPIRV bytecode for compute shader";
        if (diagnostics) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnostics->getBufferPointer();
        }
        return std::nullopt;
    }
    
    // Create shader blob
    ShaderBlob shaderBlob;
    shaderBlob.target = ShaderTarget::SPIRV;
    shaderBlob.entryPoint = entryPoint;
    shaderBlob.stage = ShaderStage::Compute;
    
    const uint8_t* codeData = (const uint8_t*)targetCode->getBufferPointer();
    size_t codeSize = targetCode->getBufferSize();
    shaderBlob.bytecode.assign(codeData, codeData + codeSize);
    
    return shaderBlob;
}

std::optional<ShaderBlob> SlangShaderCompiler::compileSlangToMetal(
    const std::string& source,
    const std::string& entryPoint,
    ShaderStage stage,
    const std::vector<std::string>& searchPaths,
    const std::vector<std::string>& defines
) {
    if (!globalSession) {
        lastError = "Global session not initialized";
        return std::nullopt;
    }

    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_METAL;
    targetDesc.profile = globalSession->findProfile("sm_6_5");
    
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    
    std::vector<const char*> searchPathPtrs;
    if (!searchPaths.empty()) {
        searchPathPtrs.reserve(searchPaths.size());
        for (const auto& path : searchPaths) {
            searchPathPtrs.push_back(path.c_str());
        }
        sessionDesc.searchPaths = searchPathPtrs.data();
        sessionDesc.searchPathCount = (SlangInt)searchPathPtrs.size();
    }
    
    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
        lastError = "Failed to create Slang session";
        return std::nullopt;
    }
    
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module;
    
    std::string sourceWithDefines = "";
    for (const auto& define : defines) {
        sourceWithDefines += "#define " + define + "\n";
    }
    sourceWithDefines += source;
    
    module = session->loadModuleFromSourceString(
        "shader",
        "shader.slang",
        sourceWithDefines.c_str(),
        diagnostics.writeRef()
    );
    
    if (!module) {
        lastError = "Module compilation failed";
        if (diagnostics) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnostics->getBufferPointer();
        }
        return std::nullopt;
    }
    
    Slang::ComPtr<slang::IEntryPoint> entryPointObj;
    SlangResult findResult = module->findEntryPointByName(entryPoint.c_str(), entryPointObj.writeRef());
    
    if (SLANG_FAILED(findResult)) {
        lastError = "Entry point '" + entryPoint + "' not found";
        return std::nullopt;
    }
    
    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module.get());
    componentTypes.push_back(entryPointObj.get());
    
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    SlangResult linkResult = session->createCompositeComponentType(
        componentTypes.data(),
        (SlangInt)componentTypes.size(),
        linkedProgram.writeRef(),
        diagnostics.writeRef()
    );
    
    if (SLANG_FAILED(linkResult)) {
        lastError = "Failed to link program";
        if (diagnostics) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnostics->getBufferPointer();
        }
        return std::nullopt;
    }
    
    Slang::ComPtr<slang::IBlob> targetCode;
    SlangResult getCodeResult = linkedProgram->getTargetCode(
        0,
        targetCode.writeRef(),
        diagnostics.writeRef()
    );
    
    if (SLANG_FAILED(getCodeResult)) {
        lastError = "Failed to generate target code";
        if (diagnostics) {
            lastError += "\nDiagnostics:\n";
            lastError += (const char*)diagnostics->getBufferPointer();
        }
        return std::nullopt;
    }
    
    ShaderBlob result;
    result.entryPoint = entryPoint;
    result.stage = stage;
    result.target = ShaderTarget::MetalIR;
    
    const uint8_t* codeData = (const uint8_t*)targetCode->getBufferPointer();
    size_t codeSize = targetCode->getBufferSize();
    result.bytecode.assign(codeData, codeData + codeSize);
    
    return result;
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
