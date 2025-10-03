# ChameleonRT Slang Ray Tracing Implementation Plan

**Date:** September 18, 2025  
**Target:** Transform current rasterization triangle to ray-traced cube  
**Reference:** Slang `ray-tracing-pipeline` example  
**Estimated Duration:** 9.5 days  

## Executive Summary

This document outlines the complete implementation plan for adding ray tracing capabilities to ChameleonRT's Slang backend. The goal is to transition from the current flat-shaded rasterized triangle to a ray-traced cube with proper lighting, closely following the Slang `ray-tracing-pipeline` example while working within our architectural constraints.

## Current State Analysis

### Existing Implementation
- **Current Shader:** `triangle_shader.slang` - Basic rasterization with hardcoded triangle
- **Graphics API:** Slang-GFX (confirmed production-ready for ray tracing)
- **Platform:** Windows with Vulkan backend
- **Math Library:** GLM for vector operations
- **Architecture:** Standalone backend without internal Slang dependencies

### Target Implementation
- **Geometry:** Cube with 24 vertices, 36 indices, 12 triangles
- **Rendering:** Hardware ray tracing pipeline
- **Lighting:** Directional light with shadows
- **Output:** Ray-traced image with proper shading

## Architectural Constraints & Solutions

### Constraints
1. **No Internal Slang Includes:** Cannot use `core/slang-basic.h`, `platform/`, etc.
2. **No Platform Library:** Must reuse existing windowing ability or reimplement windowing utilities that are required
3. **GLM Integration:** Use Slang's vector math as long as they are a part of the public-api for slang if not use GLM for math needs like the Vulkan and D3D backends do in the repo. 
4. **Standalone Design:** Self-contained within ChameleonRT architecture

### Solutions
1. **Custom Utilities:** Create `SlangRayTracingUtils.h/cpp` for helper functions
2. **GLM Math:** Use `glm::vec3`, `glm::vec4`, `glm::mat4` throughout
3. **Error Handling:** Implement custom diagnostic and error reporting
4. **Resource Management:** Manual ComPtr-like reference counting where needed

---

## Implementation Phases

## Phase 1: Foundation & Dependencies (1 Day)

### 1.1 Ray Tracing Capability Detection (3 hours)
**Priority:** Critical  
**File:** `slangdisplay.cpp`

**Implementation:**
```cpp
// Add after device creation (around line 35)
void SlangDisplay::verifyRayTracingSupport() {
    const gfx::DeviceInfo& deviceInfo = device->getDeviceInfo();
    
    if (!deviceInfo.features.rayTracing) {
        throw std::runtime_error("❌ Device doesn't support hardware ray tracing");
    }
    
    std::cout << "✅ Ray tracing support confirmed" << std::endl;
    std::cout << "   Device: " << deviceInfo.adapterName << std::endl;
    std::cout << "   API: " << gfxGetDeviceTypeName(deviceInfo.deviceType) << std::endl;
}
```

**Integration Point:**
Call `verifyRayTracingSupport()` immediately after successful device creation in the constructor.

### 1.2 Create Utility Header (2 hours)
**File:** `backends/slang/SlangRayTracingUtils.h`

```cpp
#pragma once
#include <slang-gfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <stdexcept>

namespace SlangRT {
    // Error handling utilities
    inline void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob) {
        if (diagnosticsBlob != nullptr) {
            std::string message((const char*)diagnosticsBlob->getBufferPointer());
            std::cout << "Slang Diagnostic: " << message << std::endl;
#ifdef _WIN32
            OutputDebugStringA(message.c_str());
#endif
        }
    }
    
    // GLM to float array conversion helpers
    inline void glmToFloat4(const glm::vec3& vec, float out[4]) {
        out[0] = vec.x; out[1] = vec.y; out[2] = vec.z; out[3] = 0.0f;
    }
    
    inline void glmToFloat4(const glm::vec4& vec, float out[4]) {
        out[0] = vec.x; out[1] = vec.y; out[2] = vec.z; out[3] = vec.w;
    }
    
    // Resource loading helpers
    std::string loadShaderSource(const std::string& filename);
    
    // Error checking macros
    #define SLANG_RT_CHECK(expr) \
        do { \
            auto result = (expr); \
            if (SLANG_FAILED(result)) { \
                throw std::runtime_error("Slang RT operation failed: " #expr); \
            } \
        } while(0)
}
```

### 1.3 Update CMakeLists.txt (1 hour)
**File:** `backends/slang/CMakeLists.txt`

```cmake
# Add GLM dependency
find_package(glm REQUIRED)

# Update target link libraries
target_link_libraries(chameleonrt_slang PRIVATE 
    glm::glm
    # ... existing libraries
)

# Add new source files
target_sources(chameleonrt_slang PRIVATE
    SlangRayTracingUtils.cpp
    CubeGeometry.cpp
    # ... existing sources
)
```

### 1.4 Shader Resource Loading (3 hours)
**File:** `backends/slang/SlangRayTracingUtils.cpp`

```cpp
#include "SlangRayTracingUtils.h"
#include <fstream>
#include <sstream>
#include <filesystem>

std::string SlangRT::loadShaderSource(const std::string& filename) {
    // Look for shader in multiple possible locations
    std::vector<std::string> searchPaths = {
        "backends/slang/shaders/" + filename,
        "../backends/slang/shaders/" + filename,
        "../../backends/slang/shaders/" + filename,
        filename  // Direct path
    };
    
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            std::ifstream file(path);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                return buffer.str();
            }
        }
    }
    
    throw std::runtime_error("Could not find shader file: " + filename);
}
```

---

## Phase 2: Geometry & Scene Setup (1.5 Days)

### 2.1 Cube Geometry Data Structure (4 hours)
**File:** `backends/slang/CubeGeometry.h`

```cpp
#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace CubeGeometry {
    struct Vertex {
        glm::vec3 position;
        
        Vertex(float x, float y, float z) : position(x, y, z) {}
    };
    
    struct Primitive {
        glm::vec4 normal_data;  // xyz = normal, w = unused
        glm::vec4 color;        // rgba color
        
        Primitive(const glm::vec3& normal, const glm::vec3& col) 
            : normal_data(normal.x, normal.y, normal.z, 0.0f)
            , color(col.x, col.y, col.z, 1.0f) {}
    };
    
    // Cube geometry constants
    static constexpr int kVertexCount = 24;    // 4 vertices per face * 6 faces
    static constexpr int kIndexCount = 36;     // 2 triangles per face * 3 indices * 6 faces  
    static constexpr int kPrimitiveCount = 12; // 2 triangles per face * 6 faces
    
    // Geometry data arrays
    extern const Vertex vertices[kVertexCount];
    extern const uint32_t indices[kIndexCount];
    extern const Primitive primitives[kPrimitiveCount];
    
    // Helper functions
    void getVertexData(float* outData, size_t& outSize);
    void getIndexData(uint32_t* outData, size_t& outSize);
    void getPrimitiveData(float* outData, size_t& outSize);  // Interleaved normal+color
}
```

### 2.2 Cube Geometry Implementation (4 hours)
**File:** `backends/slang/CubeGeometry.cpp`

```cpp
#include "CubeGeometry.h"

namespace CubeGeometry {
    // Cube positioned at origin, 2x2x2 units
    const Vertex vertices[kVertexCount] = {
        // Bottom face (y = -1)
        Vertex(-1.0f, -1.0f,  1.0f), Vertex( 1.0f, -1.0f,  1.0f),
        Vertex( 1.0f, -1.0f, -1.0f), Vertex(-1.0f, -1.0f, -1.0f),
        
        // Top face (y = +1) 
        Vertex(-1.0f,  1.0f,  1.0f), Vertex( 1.0f,  1.0f,  1.0f),
        Vertex( 1.0f,  1.0f, -1.0f), Vertex(-1.0f,  1.0f, -1.0f),
        
        // Front face (z = +1)
        Vertex(-1.0f, -1.0f,  1.0f), Vertex(-1.0f,  1.0f,  1.0f),
        Vertex( 1.0f,  1.0f,  1.0f), Vertex( 1.0f, -1.0f,  1.0f),
        
        // Back face (z = -1)
        Vertex(-1.0f, -1.0f, -1.0f), Vertex( 1.0f, -1.0f, -1.0f),
        Vertex( 1.0f,  1.0f, -1.0f), Vertex(-1.0f,  1.0f, -1.0f),
        
        // Left face (x = -1)
        Vertex(-1.0f, -1.0f, -1.0f), Vertex(-1.0f,  1.0f, -1.0f),
        Vertex(-1.0f,  1.0f,  1.0f), Vertex(-1.0f, -1.0f,  1.0f),
        
        // Right face (x = +1)
        Vertex( 1.0f, -1.0f, -1.0f), Vertex( 1.0f, -1.0f,  1.0f),
        Vertex( 1.0f,  1.0f,  1.0f), Vertex( 1.0f,  1.0f, -1.0f)
    };
    
    const uint32_t indices[kIndexCount] = {
        // Bottom face
        0, 1, 2,   0, 2, 3,
        // Top face  
        4, 7, 6,   4, 6, 5,
        // Front face
        8, 9, 10,  8, 10, 11,
        // Back face
        12, 15, 14, 12, 14, 13,
        // Left face
        16, 17, 18, 16, 18, 19,
        // Right face
        20, 23, 22, 20, 22, 21
    };
    
    const Primitive primitives[kPrimitiveCount] = {
        // Bottom face (2 triangles) - Gray
        Primitive(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.7f, 0.7f, 0.7f)),
        Primitive(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.7f, 0.7f, 0.7f)),
        
        // Top face (2 triangles) - Light gray
        Primitive(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.9f, 0.9f, 0.9f)),
        Primitive(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.9f, 0.9f, 0.9f)),
        
        // Front face (2 triangles) - Red
        Primitive(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.8f, 0.2f, 0.2f)),
        Primitive(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.8f, 0.2f, 0.2f)),
        
        // Back face (2 triangles) - Green
        Primitive(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.2f, 0.8f, 0.2f)),
        Primitive(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.2f, 0.8f, 0.2f)),
        
        // Left face (2 triangles) - Blue
        Primitive(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.2f, 0.2f, 0.8f)),
        Primitive(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.2f, 0.2f, 0.8f)),
        
        // Right face (2 triangles) - Yellow
        Primitive(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.8f, 0.8f, 0.2f)),
        Primitive(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.8f, 0.8f, 0.2f))
    };
}
```

### 2.3 Buffer Creation Integration (4 hours)
**Modify:** `slangdisplay.cpp`

Add new member variables to `SlangDisplay` class:
```cpp
// Add to slangdisplay.h
private:
    // Ray tracing resources
    Slang::ComPtr<gfx::IBufferResource> cubeVertexBuffer;
    Slang::ComPtr<gfx::IBufferResource> cubeIndexBuffer;
    Slang::ComPtr<gfx::IBufferResource> cubePrimitiveBuffer;
    Slang::ComPtr<gfx::IAccelerationStructure> bottomLevelAS;
    Slang::ComPtr<gfx::IAccelerationStructure> topLevelAS;
```

Add buffer creation method:
```cpp
void SlangDisplay::createCubeBuffers() {
    // Vertex buffer
    gfx::IBufferResource::Desc vertexBufferDesc = {};
    vertexBufferDesc.type = gfx::IResource::Type::Buffer;
    vertexBufferDesc.sizeInBytes = sizeof(CubeGeometry::vertices);
    vertexBufferDesc.defaultState = gfx::ResourceState::AccelerationStructureBuildInput;
    vertexBufferDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::AccelerationStructureBuildInput,
        gfx::ResourceState::CopyDestination
    );
    
    SLANG_RT_CHECK(device->createBufferResource(
        vertexBufferDesc, 
        (void*)CubeGeometry::vertices, 
        cubeVertexBuffer.writeRef()
    ));
    
    // Index buffer
    gfx::IBufferResource::Desc indexBufferDesc = {};
    indexBufferDesc.type = gfx::IResource::Type::Buffer;
    indexBufferDesc.sizeInBytes = sizeof(CubeGeometry::indices);
    indexBufferDesc.defaultState = gfx::ResourceState::AccelerationStructureBuildInput;
    indexBufferDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::AccelerationStructureBuildInput,
        gfx::ResourceState::CopyDestination
    );
    
    SLANG_RT_CHECK(device->createBufferResource(
        indexBufferDesc,
        (void*)CubeGeometry::indices,
        cubeIndexBuffer.writeRef()
    ));
    
    // Primitive data buffer (for materials/normals)
    gfx::IBufferResource::Desc primitiveBufferDesc = {};
    primitiveBufferDesc.type = gfx::IResource::Type::Buffer;
    primitiveBufferDesc.sizeInBytes = sizeof(CubeGeometry::primitives);
    primitiveBufferDesc.defaultState = gfx::ResourceState::ShaderResource;
    primitiveBufferDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::ShaderResource,
        gfx::ResourceState::CopyDestination
    );
    
    SLANG_RT_CHECK(device->createBufferResource(
        primitiveBufferDesc,
        (void*)CubeGeometry::primitives,
        cubePrimitiveBuffer.writeRef()
    ));
    
    std::cout << "✅ Cube geometry buffers created successfully" << std::endl;
}
```

---

## Phase 3: Ray Tracing Shader Implementation (2 Days)

### 3.1 Ray Tracing Shader Structure (4 hours)
**File:** `backends/slang/shaders/cube_raytracing_shader.slang`

```hlsl
// cube_raytracing_shader.slang
//
// Ray tracing pipeline implementation for cube rendering
// Based on Slang ray-tracing-pipeline example

// Uniform data structure matching C++ layout
struct Uniforms {
    float screenWidth, screenHeight;
    float focalLength, frameHeight;
    float4 cameraDir;
    float4 cameraUp;
    float4 cameraRight;
    float4 cameraPosition;
    float4 lightDir;
};

// Primitive data structure for cube faces
struct CubePrimitive {
    float4 normal_data;  // xyz = normal, w = unused
    float4 color;        // rgba color
    
    float3 getNormal() { return normal_data.xyz; }
    float3 getColor() { return color.xyz; }
};

// Ray payload structure for communication between shaders
struct [raypayload] RayPayload {
    float4 color : read(caller) : write(caller, closesthit, miss);
};

// Global resources - bound from C++ code
uniform RWTexture2D<float4> resultTexture;
uniform RaytracingAccelerationStructure sceneBVH;
uniform StructuredBuffer<CubePrimitive> primitiveBuffer;
uniform Uniforms uniforms;

// ============================================================================
// RAY GENERATION SHADER
// ============================================================================
[shader("raygeneration")]
void rayGenShader() {
    // Get current pixel coordinates
    uint2 threadIdx = DispatchRaysIndex().xy;
    
    // Early exit for pixels outside render target
    if (threadIdx.x >= (int)uniforms.screenWidth) return;
    if (threadIdx.y >= (int)uniforms.screenHeight) return;
    
    // Convert screen coordinates to camera ray
    float frameWidth = uniforms.screenWidth / uniforms.screenHeight * uniforms.frameHeight;
    float imageY = (threadIdx.y / uniforms.screenHeight - 0.5f) * uniforms.frameHeight;
    float imageX = (threadIdx.x / uniforms.screenWidth - 0.5f) * frameWidth;
    float imageZ = uniforms.focalLength;
    
    // Calculate ray direction in world space
    float3 rayDir = normalize(
        uniforms.cameraDir.xyz * imageZ - 
        uniforms.cameraUp.xyz * imageY + 
        uniforms.cameraRight.xyz * imageX
    );
    
    // Setup ray descriptor
    RayDesc ray;
    ray.Origin = uniforms.cameraPosition.xyz;
    ray.Direction = rayDir;
    ray.TMin = 0.001f;  // Avoid self-intersection
    ray.TMax = 10000.0f;  // Far plane
    
    // Initialize payload
    RayPayload payload = { float4(0, 0, 0, 0) };
    
    // Trace primary ray
    TraceRay(
        sceneBVH,           // Acceleration structure
        RAY_FLAG_NONE,      // Ray flags
        ~0,                 // Instance inclusion mask
        0,                  // Ray contribution to hit group index
        0,                  // Multiplier for geometry index
        0,                  // Miss shader index
        ray,                // Ray descriptor
        payload             // Ray payload
    );
    
    // Write result to output texture
    resultTexture[threadIdx.xy] = payload.color;
}

// ============================================================================
// MISS SHADER
// ============================================================================
[shader("miss")]
void missShader(inout RayPayload payload) {
    // Sky/background color - soft blue gradient
    float3 rayDir = WorldRayDirection();
    float t = 0.5f * (rayDir.y + 1.0f);  // Convert y from [-1,1] to [0,1]
    float3 skyColor = lerp(float3(1.0f, 1.0f, 1.0f), float3(0.5f, 0.7f, 1.0f), t);
    payload.color = float4(skyColor, 1.0f);
}

// ============================================================================
// CLOSEST HIT SHADER
// ============================================================================
[shader("closesthit")]
void closestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    // Get primitive information
    uint primitiveIndex = PrimitiveIndex();
    float3 normal = primitiveBuffer[primitiveIndex].getNormal();
    float3 albedo = primitiveBuffer[primitiveIndex].getColor();
    
    // Calculate world position of hit point
    float3 hitLocation = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    
    // Basic Lambertian lighting
    float3 lightDir = normalize(uniforms.lightDir.xyz);
    float ndotl = max(0.0f, dot(normal, lightDir));
    
    // Simple shadow ray
    RayDesc shadowRay;
    shadowRay.Origin = hitLocation + normal * 0.001f;  // Offset to avoid self-intersection
    shadowRay.Direction = lightDir;
    shadowRay.TMin = 0.001f;
    shadowRay.TMax = 10000.0f;
    
    RayPayload shadowPayload = { float4(0, 0, 0, 0) };
    TraceRay(
        sceneBVH, 
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        ~0, 
        1,  // Use shadow hit group
        0, 
        0, 
        shadowRay, 
        shadowPayload
    );
    
    // Apply shadow
    float shadow = 1.0f - shadowPayload.color.x;
    float lightIntensity = shadow * ndotl * 0.8f + 0.2f;  // Ambient + diffuse
    
    // Final color
    payload.color = float4(albedo * lightIntensity, 1.0f);
}

// ============================================================================
// SHADOW RAY HIT SHADER
// ============================================================================
[shader("closesthit")]
void shadowRayHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    // Mark that shadow ray hit something
    payload.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
}
```

### 3.2 Display Copy Shaders (2 hours)
Add to the same shader file:

```hlsl
// ============================================================================
// DISPLAY COPY SHADERS (for copying RT result to swapchain)
// ============================================================================

// Full-screen triangle vertex data (procedural)
[shader("vertex")]
float4 vertexMain(uint vertexId : SV_VertexID) : SV_Position {
    // Generate full-screen triangle procedurally
    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };
    return float4(positions[vertexId], 0.5f, 1.0f);
}

// Simple copy from ray tracing result texture
[shader("fragment")]
float4 fragmentMain(
    float4 sv_position : SV_Position,
    uniform Texture2D<float4> rtResultTexture
) : SV_Target {
    uint2 pixelCoord = uint2(sv_position.xy);
    return rtResultTexture.Load(uint3(pixelCoord, 0));
}
```

### 3.3 Shader Loading and Compilation (8 hours)
**Modify:** `slangdisplay.cpp`

```cpp
// Replace existing shader loading with RT shader support
Slang::ComPtr<gfx::IShaderProgram> rtShaderProgram;
Slang::ComPtr<gfx::IShaderProgram> displayShaderProgram;

gfx::Result SlangDisplay::loadRayTracingShaders() {
    ComPtr<slang::ISession> slangSession = device->getSlangSession();
    ComPtr<slang::IBlob> diagnosticsBlob;
    
    // Load the shader module
    std::string shaderPath = "backends/slang/shaders/cube_raytracing_shader.slang";
    slang::IModule* module = slangSession->loadModule(
        shaderPath.c_str(), 
        diagnosticsBlob.writeRef()
    );
    SlangRT::diagnoseIfNeeded(diagnosticsBlob);
    if (!module) {
        return SLANG_FAIL;
    }
    
    // Create RT pipeline program
    {
        Slang::List<slang::IComponentType*> componentTypes;
        componentTypes.add(module);
        
        // Add RT entry points
        ComPtr<slang::IEntryPoint> entryPoint;
        
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("rayGenShader", entryPoint.writeRef()));
        componentTypes.add(entryPoint);
        
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("missShader", entryPoint.writeRef()));
        componentTypes.add(entryPoint);
        
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("closestHitShader", entryPoint.writeRef()));
        componentTypes.add(entryPoint);
        
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("shadowRayHitShader", entryPoint.writeRef()));
        componentTypes.add(entryPoint);
        
        // Link the program
        ComPtr<slang::IComponentType> linkedProgram;
        SlangResult result = slangSession->createCompositeComponentType(
            componentTypes.getBuffer(),
            componentTypes.getCount(),
            linkedProgram.writeRef(),
            diagnosticsBlob.writeRef()
        );
        SlangRT::diagnoseIfNeeded(diagnosticsBlob);
        SLANG_RETURN_ON_FAIL(result);
        
        // Create the shader program
        gfx::IShaderProgram::Desc programDesc = {};
        programDesc.slangGlobalScope = linkedProgram;
        SLANG_RETURN_ON_FAIL(device->createProgram(programDesc, rtShaderProgram.writeRef()));
    }
    
    // Create display pipeline program  
    {
        Slang::List<slang::IComponentType*> componentTypes;
        componentTypes.add(module);
        
        ComPtr<slang::IEntryPoint> entryPoint;
        
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("vertexMain", entryPoint.writeRef()));
        componentTypes.add(entryPoint);
        
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("fragmentMain", entryPoint.writeRef()));
        componentTypes.add(entryPoint);
        
        // Link and create display program
        ComPtr<slang::IComponentType> linkedProgram;
        SlangResult result = slangSession->createCompositeComponentType(
            componentTypes.getBuffer(),
            componentTypes.getCount(),
            linkedProgram.writeRef(),
            diagnosticsBlob.writeRef()
        );
        SlangRT::diagnoseIfNeeded(diagnosticsBlob);
        SLANG_RETURN_ON_FAIL(result);
        
        gfx::IShaderProgram::Desc programDesc = {};
        programDesc.slangGlobalScope = linkedProgram;
        SLANG_RETURN_ON_FAIL(device->createProgram(programDesc, displayShaderProgram.writeRef()));
    }
    
    std::cout << "✅ Ray tracing shaders compiled successfully" << std::endl;
    return SLANG_OK;
}
```

---

## Phase 4: Acceleration Structure Implementation (2 Days)

### 4.1 Bottom Level Acceleration Structure (8 hours)
**Add to:** `slangdisplay.cpp`

```cpp
void SlangDisplay::createBottomLevelAS() {
    // Define geometry description
    gfx::IAccelerationStructure::GeometryDesc geometryDesc = {};
    geometryDesc.type = gfx::IAccelerationStructure::GeometryType::Triangles;
    geometryDesc.flags = gfx::IAccelerationStructure::GeometryFlags::Opaque;
    
    // Vertex data description
    geometryDesc.content.triangles.vertexData = cubeVertexBuffer;
    geometryDesc.content.triangles.vertexStride = sizeof(CubeGeometry::Vertex);
    geometryDesc.content.triangles.vertexCount = CubeGeometry::kVertexCount;
    geometryDesc.content.triangles.vertexFormat = gfx::Format::R32G32B32_FLOAT;
    
    // Index data description
    geometryDesc.content.triangles.indexData = cubeIndexBuffer;
    geometryDesc.content.triangles.indexFormat = gfx::Format::R32_UINT;
    geometryDesc.content.triangles.indexCount = CubeGeometry::kIndexCount;
    
    // Build input structure
    gfx::IAccelerationStructure::BuildInputs buildInputs = {};
    buildInputs.kind = gfx::IAccelerationStructure::Kind::BottomLevel;
    buildInputs.flags = gfx::IAccelerationStructure::BuildFlags::PreferFastTrace;
    buildInputs.descCount = 1;
    buildInputs.geometryDescs = &geometryDesc;
    
    // Get prebuild info to determine buffer sizes
    gfx::IAccelerationStructure::PrebuildInfo prebuildInfo = {};
    device->getAccelerationStructurePrebuildInfo(buildInputs, &prebuildInfo);
    
    // Create acceleration structure buffer
    gfx::IBufferResource::Desc asBufferDesc = {};
    asBufferDesc.type = gfx::IResource::Type::Buffer;
    asBufferDesc.sizeInBytes = prebuildInfo.resultDataMaxSize;
    asBufferDesc.defaultState = gfx::ResourceState::AccelerationStructure;
    asBufferDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::AccelerationStructure
    );
    
    Slang::ComPtr<gfx::IBufferResource> blasBuffer;
    SLANG_RT_CHECK(device->createBufferResource(
        asBufferDesc, 
        nullptr, 
        blasBuffer.writeRef()
    ));
    
    // Create scratch buffer for build process
    gfx::IBufferResource::Desc scratchBufferDesc = {};
    scratchBufferDesc.type = gfx::IResource::Type::Buffer;
    scratchBufferDesc.sizeInBytes = prebuildInfo.scratchDataSize;
    scratchBufferDesc.defaultState = gfx::ResourceState::UnorderedAccess;
    scratchBufferDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::UnorderedAccess
    );
    
    Slang::ComPtr<gfx::IBufferResource> scratchBuffer;
    SLANG_RT_CHECK(device->createBufferResource(
        scratchBufferDesc,
        nullptr,
        scratchBuffer.writeRef()
    ));
    
    // Create the acceleration structure object
    gfx::IAccelerationStructure::CreateDesc createDesc = {};
    createDesc.kind = gfx::IAccelerationStructure::Kind::BottomLevel;
    createDesc.buffer = blasBuffer;
    createDesc.size = prebuildInfo.resultDataMaxSize;
    
    SLANG_RT_CHECK(device->createAccelerationStructure(
        createDesc,
        bottomLevelAS.writeRef()
    ));
    
    // Build the acceleration structure
    auto commandBuffer = transientHeap->createCommandBuffer();
    auto encoder = commandBuffer->encodeComputeCommands();
    
    gfx::IAccelerationStructure::BuildDesc buildDesc = {};
    buildDesc.inputs = buildInputs;
    buildDesc.scratchData = scratchBuffer;
    buildDesc.dest = bottomLevelAS;
    
    encoder->buildAccelerationStructure(buildDesc, 0, nullptr);
    encoder->endEncoding();
    commandBuffer->close();
    queue->executeCommandBuffer(commandBuffer);
    queue->waitOnHost();  // Synchronous build for simplicity
    
    std::cout << "✅ Bottom Level Acceleration Structure built successfully" << std::endl;
}
```

### 4.2 Top Level Acceleration Structure (4 hours)
```cpp
void SlangDisplay::createTopLevelAS() {
    // Create instance description for single cube
    gfx::IAccelerationStructure::InstanceDesc instanceDesc = {};
    
    // Identity transform matrix (cube at origin)
    float identityMatrix[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f}
    };
    memcpy(instanceDesc.transform, identityMatrix, sizeof(identityMatrix));
    
    instanceDesc.instanceID = 0;
    instanceDesc.instanceMask = 0xFF;
    instanceDesc.instanceContributionToHitGroupIndex = 0;
    instanceDesc.flags = 0;
    instanceDesc.accelerationStructureReference = bottomLevelAS->getDeviceAddress();
    
    // Create instance buffer
    gfx::IBufferResource::Desc instanceBufferDesc = {};
    instanceBufferDesc.type = gfx::IResource::Type::Buffer;
    instanceBufferDesc.sizeInBytes = sizeof(instanceDesc);
    instanceBufferDesc.defaultState = gfx::ResourceState::AccelerationStructureBuildInput;
    
    Slang::ComPtr<gfx::IBufferResource> instanceBuffer;
    SLANG_RT_CHECK(device->createBufferResource(
        instanceBufferDesc,
        &instanceDesc,
        instanceBuffer.writeRef()
    ));
    
    // Setup TLAS build inputs
    gfx::IAccelerationStructure::BuildInputs buildInputs = {};
    buildInputs.kind = gfx::IAccelerationStructure::Kind::TopLevel;
    buildInputs.flags = gfx::IAccelerationStructure::BuildFlags::PreferFastTrace;
    buildInputs.descCount = 1;
    buildInputs.instanceDescs = instanceBuffer;
    
    // Get prebuild info and create buffers
    gfx::IAccelerationStructure::PrebuildInfo prebuildInfo = {};
    device->getAccelerationStructurePrebuildInfo(buildInputs, &prebuildInfo);
    
    // Create TLAS buffer
    gfx::IBufferResource::Desc tlasBufferDesc = {};
    tlasBufferDesc.type = gfx::IResource::Type::Buffer;
    tlasBufferDesc.sizeInBytes = prebuildInfo.resultDataMaxSize;
    tlasBufferDesc.defaultState = gfx::ResourceState::AccelerationStructure;
    
    Slang::ComPtr<gfx::IBufferResource> tlasBuffer;
    SLANG_RT_CHECK(device->createBufferResource(
        tlasBufferDesc,
        nullptr,
        tlasBuffer.writeRef()
    ));
    
    // Create TLAS object
    gfx::IAccelerationStructure::CreateDesc createDesc = {};
    createDesc.kind = gfx::IAccelerationStructure::Kind::TopLevel;
    createDesc.buffer = tlasBuffer;
    createDesc.size = prebuildInfo.resultDataMaxSize;
    
    SLANG_RT_CHECK(device->createAccelerationStructure(
        createDesc,
        topLevelAS.writeRef()
    ));
    
    // Build TLAS (similar to BLAS building...)
    // [Build process implementation similar to BLAS]
    
    std::cout << "✅ Top Level Acceleration Structure built successfully" << std::endl;
}
```

### 4.3 AS Integration into Render Pipeline (4 hours)
Add AS building to initialization sequence in constructor.

---

## Phase 5: Camera and Uniform System (1 Day)

### 5.1 Uniform Data Structure (2 hours)
**Add to:** `slangdisplay.h`

```cpp
// Ray tracing uniform data structure
struct RTUniforms {
    float screenWidth, screenHeight;
    float focalLength = 24.0f;
    float frameHeight = 24.0f;
    float cameraDir[4];
    float cameraUp[4]; 
    float cameraRight[4];
    float cameraPosition[4];
    float lightDir[4];
};

// Member variables
RTUniforms rtUniforms;
Slang::ComPtr<gfx::IBufferResource> uniformBuffer;
```

### 5.2 Camera Parameter Conversion (4 hours)
```cpp
void SlangDisplay::updateCameraUniforms(const glm::vec3& pos, const glm::vec3& dir, 
                                        const glm::vec3& up, float fovy) {
    rtUniforms.screenWidth = static_cast<float>(fb_width);
    rtUniforms.screenHeight = static_cast<float>(fb_height);
    
    // Convert field of view to frame height (perspective projection)
    rtUniforms.focalLength = 24.0f;  // Standard camera focal length
    rtUniforms.frameHeight = 2.0f * tan(fovy * 0.5f) * rtUniforms.focalLength;
    
    // Compute camera basis vectors
    glm::vec3 cameraDir = glm::normalize(dir);
    glm::vec3 cameraRight = glm::normalize(glm::cross(cameraDir, up));
    glm::vec3 cameraUp = glm::normalize(glm::cross(cameraRight, cameraDir));
    
    // Copy to uniform structure
    SlangRT::glmToFloat4(cameraDir, rtUniforms.cameraDir);
    SlangRT::glmToFloat4(cameraUp, rtUniforms.cameraUp);
    SlangRT::glmToFloat4(cameraRight, rtUniforms.cameraRight);
    SlangRT::glmToFloat4(pos, rtUniforms.cameraPosition);
    
    // Setup directional light
    glm::vec3 lightDirection = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
    SlangRT::glmToFloat4(lightDirection, rtUniforms.lightDir);
    
    // Update uniform buffer
    updateUniformBuffer();
}

void SlangDisplay::updateUniformBuffer() {
    if (uniformBuffer) {
        void* mappedData = nullptr;
        uniformBuffer->map(nullptr, &mappedData);
        memcpy(mappedData, &rtUniforms, sizeof(RTUniforms));
        uniformBuffer->unmap(nullptr);
    }
}
```

### 5.3 Uniform Buffer Creation (2 hours)
```cpp
void SlangDisplay::createUniformBuffer() {
    gfx::IBufferResource::Desc bufferDesc = {};
    bufferDesc.type = gfx::IResource::Type::Buffer;
    bufferDesc.sizeInBytes = sizeof(RTUniforms);
    bufferDesc.defaultState = gfx::ResourceState::ConstantBuffer;
    bufferDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::ConstantBuffer,
        gfx::ResourceState::CopyDestination
    );
    bufferDesc.cpuAccessFlags = gfx::CpuAccessFlag::Write;
    
    SLANG_RT_CHECK(device->createBufferResource(
        bufferDesc,
        nullptr,
        uniformBuffer.writeRef()
    ));
}
```

---

## Phase 6: Pipeline Integration & Testing (1.5 Days)

### 6.1 Ray Tracing Pipeline State (6 hours)
```cpp
Slang::ComPtr<gfx::IRayTracingPipelineState> rtPipelineState;

void SlangDisplay::createRayTracingPipeline() {
    gfx::RayTracingPipelineStateDesc pipelineDesc = {};
    pipelineDesc.program = rtShaderProgram;
    pipelineDesc.maxRecursionDepth = 2;  // Primary + shadow rays
    
    SLANG_RT_CHECK(device->createRayTracingPipelineState(
        pipelineDesc,
        rtPipelineState.writeRef()
    ));
    
    std::cout << "✅ Ray tracing pipeline state created" << std::endl;
}
```

### 6.2 Render Target Setup (4 hours)
```cpp
Slang::ComPtr<gfx::ITextureResource> rtOutputTexture;
Slang::ComPtr<gfx::IResourceView> rtOutputUAV;

void SlangDisplay::createRenderTargets() {
    // Create render target texture for ray tracing output
    gfx::ITextureResource::Desc textureDesc = {};
    textureDesc.type = gfx::IResource::Type::Texture2D;
    textureDesc.size.width = fb_width;
    textureDesc.size.height = fb_height;
    textureDesc.size.depth = 1;
    textureDesc.format = gfx::Format::R8G8B8A8_UNORM;
    textureDesc.defaultState = gfx::ResourceState::UnorderedAccess;
    textureDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::UnorderedAccess,
        gfx::ResourceState::ShaderResource
    );
    
    SLANG_RT_CHECK(device->createTextureResource(
        textureDesc,
        nullptr,
        rtOutputTexture.writeRef()
    ));
    
    // Create UAV for writing from ray tracing
    gfx::IResourceView::Desc uavDesc = {};
    uavDesc.type = gfx::IResourceView::Type::UnorderedAccess;
    uavDesc.format = gfx::Format::R8G8B8A8_UNORM;
    
    SLANG_RT_CHECK(device->createTextureView(
        rtOutputTexture,
        uavDesc,
        rtOutputUAV.writeRef()
    ));
}
```

### 6.3 Main Render Loop Integration (2 hours)
**Modify:** `render_slang.cpp`

```cpp
RenderStats RenderSlang::render(const glm::vec3 &pos,
                               const glm::vec3 &dir,
                               const glm::vec3 &up,
                               const float fovy,
                               const bool camera_changed,
                               const bool readback_framebuffer) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Update camera uniforms if camera changed
    if (camera_changed && display) {
        display->updateCameraUniforms(pos, dir, up, fovy);
    }
    
    // Perform ray tracing
    if (display) {
        display->renderFrame();
    }
    
    // Readback for CPU-side image buffer
    if (readback_framebuffer && display) {
        display->readbackFramebuffer(img);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    RenderStats stats;
    stats.render_time = duration.count() / 1000.0f;  // Convert to milliseconds
    stats.rays_per_second = (fb_width * fb_height) / (stats.render_time / 1000.0f);
    
    return stats;
}
```

---

## Phase 7: Debugging & Optimization (1 Day)

### 7.1 Common Issues and Solutions (4 hours)

**Issue: Black Screen**
```cpp
void SlangDisplay::debugRayTracing() {
    std::cout << "=== Ray Tracing Debug Info ===" << std::endl;
    std::cout << "RT Output Texture: " << (rtOutputTexture ? "Created" : "NULL") << std::endl;
    std::cout << "TLAS: " << (topLevelAS ? "Built" : "NULL") << std::endl;
    std::cout << "BLAS: " << (bottomLevelAS ? "Built" : "NULL") << std::endl;
    std::cout << "Pipeline State: " << (rtPipelineState ? "Created" : "NULL") << std::endl;
    std::cout << "Uniform Buffer: " << (uniformBuffer ? "Created" : "NULL") << std::endl;
    
    // Test a simple pixel
    if (rtOutputTexture) {
        // Try to read back one pixel to verify texture state
        // [Implementation for texture readback verification]
    }
}
```

**Issue: Incorrect Geometry**
- Verify vertex buffer data with debug output
- Check index buffer alignment
- Validate acceleration structure build

**Issue: No Lighting**
- Verify uniform buffer updates
- Check camera basis vector calculation
- Validate light direction

### 7.2 Performance Optimization (4 hours)

**Optimization Targets:**
1. **AS Build Optimization**: Use optimal build flags
2. **Memory Management**: Proper resource state transitions
3. **Command Buffer Efficiency**: Minimize state changes
4. **Shader Optimization**: Reduce ray tracing depth where possible

```cpp
void SlangDisplay::optimizePerformance() {
    // Use fast trace preference for static geometry
    buildInputs.flags = gfx::IAccelerationStructure::BuildFlags::PreferFastTrace;
    
    // Batch resource state transitions
    // [Implementation for batched transitions]
    
    // Profile ray tracing dispatch times
    // [Implementation for timing measurements]
}
```

---

## Integration Checklist

### Pre-Implementation Verification
- [ ] Slang-GFX version supports ray tracing
- [ ] Target GPU supports hardware ray tracing
- [ ] GLM library is available and linked
- [ ] Build system can find shader files

### Implementation Milestones
- [ ] Phase 1: Foundation setup complete
- [ ] Phase 2: Cube geometry buffers created  
- [ ] Phase 3: Ray tracing shaders compile
- [ ] Phase 4: Acceleration structures build successfully
- [ ] Phase 5: Camera uniforms update correctly
- [ ] Phase 6: Ray tracing pipeline executes
- [ ] Phase 7: Rendered cube visible with correct lighting

### Testing Verification
- [ ] Green cube visible in center of screen
- [ ] Camera movement updates view correctly
- [ ] Lighting responds to surface normals
- [ ] No crashes or GPU timeouts
- [ ] Performance acceptable (target: >30 FPS at 1920x1080)

---

## File Structure Summary

```
ChameleonRT/
├── backends/slang/
│   ├── SlangRayTracingUtils.h         # [NEW] Utility functions
│   ├── SlangRayTracingUtils.cpp       # [NEW] Utility implementations  
│   ├── CubeGeometry.h                 # [NEW] Cube geometry data
│   ├── CubeGeometry.cpp               # [NEW] Cube geometry implementation
│   ├── render_slang.cpp               # [MODIFIED] Render loop integration
│   ├── render_slang.h                 # [MODIFIED] Interface updates
│   ├── slangdisplay.cpp               # [MODIFIED] Main RT implementation
│   ├── slangdisplay.h                 # [MODIFIED] RT resource declarations
│   ├── CMakeLists.txt                 # [MODIFIED] Add new dependencies
│   └── shaders/
│       ├── triangle_shader.slang      # [EXISTING] Original raster shader
│       └── cube_raytracing_shader.slang # [NEW] Ray tracing shaders
└── docs/
    └── ray-tracing-implementation-plan.md # [THIS DOCUMENT]
```

---

## Risk Assessment & Mitigation

### High Risk Items
1. **Acceleration Structure Building**: Complex API with many failure points
   - *Mitigation*: Implement extensive error checking and debug output
   
2. **Shader Compilation**: RT shaders more complex than raster shaders  
   - *Mitigation*: Start with minimal shader, add features incrementally
   
3. **Resource Binding**: Many resources need correct binding
   - *Mitigation*: Create verification functions for each resource type

### Medium Risk Items
1. **Performance**: Ray tracing may be slow on older hardware
   - *Mitigation*: Implement fallback to rasterization if RT unavailable
   
2. **Memory Usage**: AS building requires significant GPU memory
   - *Mitigation*: Monitor memory usage, implement cleanup procedures

### Low Risk Items  
1. **Camera Integration**: Well-understood coordinate transformations
2. **Display Pipeline**: Similar to existing rasterization path

---

## Success Metrics

### Functional Requirements
- ✅ Ray-traced cube renders correctly
- ✅ Camera controls work as expected  
- ✅ Lighting appears realistic
- ✅ No visual artifacts or crashes

### Performance Requirements
- ✅ Maintains >30 FPS at 1920x1080 on RTX 3060 or better
- ✅ Memory usage remains under 2GB GPU memory
- ✅ Startup time under 5 seconds

### Code Quality Requirements
- ✅ Clean separation between RT and raster backends
- ✅ Comprehensive error handling
- ✅ Maintainable code structure
- ✅ Good documentation and comments

This implementation plan provides a clear roadmap for transitioning ChameleonRT's Slang backend from rasterization to ray tracing, with detailed code examples and concrete milestones for tracking progress.
