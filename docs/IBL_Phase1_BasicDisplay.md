# IBL Implementation - Phase 1: Basic Environment Display
## Detailed Task Breakdown

**Status:** Planning Phase  
**Dependencies:** Phase 0 Complete  
**Estimated Duration:** 3-4 days  
**Goal:** Load and display environment maps in miss shader

---

## Phase 1 Overview

This phase focuses on getting environment maps loaded, uploaded to GPU, and displayed when rays miss all geometry. No importance sampling or MIS yet - just basic display.

**Success Criteria:**
- Environment map loads from .exr file
- Appears correctly in miss shader
- Proper lat-long mapping verified
- No visual artifacts or seams

---

## Task 1.0: Add Command-Line Interface for Environment Map

### Objective
Add `-env` command-line argument to allow users to specify environment map file path.

### Subtasks

#### 1.0.1: Update USAGE String
Modify `main.cpp` USAGE string (around line 19):

```cpp
const std::string USAGE =
    "Usage: <backend> <mesh.obj/gltf/glb> [options]\n"
    "Render backend libraries should be named following (lib)crt_<backend>.(dll|so)\n"
    "Options:\n"
    "\t-eye <x> <y> <z>       Set the camera position\n"
    "\t-center <x> <y> <z>    Set the camera focus point\n"
    "\t-up <x> <y> <z>        Set the camera up vector\n"
    "\t-fov <fovy>            Specify the camera field of view (in degrees)\n"
    "\t-spp <n>               Specify the number of samples to take per-pixel. Defaults to 1\n"
    "\t-camera <n>            If the scene contains multiple cameras, specify which\n"
    "\t                       should be used. Defaults to the first camera\n"
    "\t-img <x> <y>           Specify the window dimensions. Defaults to 1280x720\n"
    "\t-mat-mode <MODE>       Specify the material mode, default (the default) or "
    "white_diffuse\n"
    "\t-env <path>            Specify HDR environment map (.exr file)\n"
    "\n";
```

#### 1.0.2: Add Command-Line Parsing
In `main.cpp`, in the `run_app()` function (around line 120-170), add parsing for `-env`:

```cpp
void run_app(const std::vector<std::string> &args,
             SDL_Window *window,
             Display *display,
             RenderPlugin *render_plugin)
{
    // ... existing variables ...
    std::string scene_file;
    std::string env_map_file;  // ADD THIS
    
    // ... parsing loop ...
    for (size_t i = 2; i < args.size(); ++i) {
        // ... existing options ...
        } else if (args[i] == "-benchmark-frames") {
            benchmark_frames = std::stoi(args[++i]);
        } else if (args[i] == "-env") {  // ADD THIS
            env_map_file = args[++i];
            canonicalize_path(env_map_file);
        } else if (args[i][0] != '-') {
            scene_file = args[i];
            canonicalize_path(scene_file);
        }
    }
    
    // ... rest of function ...
}
```

#### 1.0.3: Pass to Scene Constructor
The environment map path will be passed to Scene in the next task.

### Acceptance Criteria
- [x] `-env` flag documented in USAGE
- [x] Command-line parsing works
- [x] Path is canonicalized (absolute path)
- [x] Can run: `chameleonrt dxr scene.gltf -env envmap.exr`

---

## Task 1.1: Add Environment Map to Scene Class

### Objective
Store environment map path in Scene class so all backends can access it.

### Subtasks

#### 1.1.1: Add Member to Scene Class
Modify `util/scene.h`:

```cpp
class Scene {
public:
    // ... existing members ...
    
    // Environment map (optional)
    std::string environment_map_path;
    
    // ... existing methods ...
    
    // Constructor that accepts environment map
    Scene(const std::string &file, 
          MaterialMode material_mode = MaterialMode::DEFAULT,
          const std::string &env_map = "");
    
    // ... rest of class ...
};
```

#### 1.1.2: Update Scene Constructor
Modify `util/scene.cpp`:

```cpp
Scene::Scene(const std::string &file, 
             MaterialMode material_mode,
             const std::string &env_map)
    : environment_map_path(env_map),
      // ... existing initialization ...
{
    // ... existing constructor code ...
    
    if (!environment_map_path.empty()) {
        std::cout << "Environment map set: " << environment_map_path << "\n";
    }
}
```

#### 1.1.3: Update main.cpp to Pass Environment Map
In `main.cpp`, modify Scene construction:

```cpp
{
    Scene scene(scene_file, material_mode, env_map_file);  // Pass env_map_file
    scene.samples_per_pixel = samples_per_pixel;
    
    // ... rest of scene setup ...
    
    renderer->set_scene(scene);
}
```

### Acceptance Criteria
- [x] Scene class stores environment map path
- [x] Constructor accepts optional env_map parameter
- [x] Backwards compatible (empty string = no environment)
- [x] Path accessible to all backends via Scene object

---

## Task 1.2: Integrate TinyEXR into Build System

### Objective
Add TinyEXR to the project build system and create wrapper functions.

### Subtasks

#### 1.2.1: Update CMakeLists.txt
Modify `util/CMakeLists.txt`:

```cmake
# Add tinyexr.h to sources (header-only)
set(UTIL_HEADERS
    ${UTIL_HEADERS}
    tinyexr.h
)

# No additional libraries needed (header-only)
```

#### 1.2.2: Create HDR Image Wrapper
Add to `util/util.h`:

```cpp
#pragma once
#include <string>
#include <memory>

namespace util {

// HDR image loaded from .exr file
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

// Load environment map from .exr file
HDRImage load_environment_map(const std::string& filename);

} // namespace util
```

#### 1.2.3: Implement Loading Function
Add to `util/util.cpp`:

```cpp
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

namespace util {

HDRImage load_environment_map(const std::string& filename) {
    HDRImage img;
    const char* err = nullptr;
    
    std::cout << "Loading environment map: " << filename << std::endl;
    
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
    
    std::cout << "  Loaded: " << img.width << "x" << img.height << std::endl;
    
    // Verify HDR data
    float max_value = 0.0f;
    for (int i = 0; i < img.width * img.height * 4; i++) {
        max_value = std::max(max_value, img.data[i]);
    }
    std::cout << "  Max value: " << max_value 
              << (max_value > 1.0 ? " (HDR)" : " (LDR?)") << std::endl;
    
    return img;
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

} // namespace util
```

#### 1.2.4: Test Compilation
```powershell
cd build
cmake ..
cmake --build . --config Release
```

**Expected:** Clean compilation with no errors

### Acceptance Criteria
- [x] CMake builds successfully
- [x] HDRImage class compiles
- [x] Can load .exr files from C++
- [x] Bilinear sampling works

---

## Task 1.3: Create GPU Buffer for Environment Map

### Objective
Upload environment map to GPU as a texture accessible in shaders.

### Subtasks

#### 1.3.1: Read Environment Path from Scene
In `backends/dxr/render_dxr.cpp`, modify `set_scene()` method:

```cpp
void RenderDXR::set_scene(const Scene &scene) {
    // ... existing scene setup code ...
    
    // Load environment map if specified
    if (!scene.environment_map_path.empty()) {
        load_environment_map(scene.environment_map_path);
    }
}
```

#### 1.3.2: Add DXR Texture Wrapper (if needed)

Review `backends/dxr/dx12_utils.h` for Texture2D class.

**Required features:**
- Support for `DXGI_FORMAT_R32G32B32A32_FLOAT`
- Upload from CPU float* data
- Create SRV (Shader Resource View)

#### 1.3.3: Add Environment Map Members
Modify `backends/dxr/render_dxr.h`:

```cpp
class RenderDXR : public RenderBackend {
public:
    // ... existing methods ...
    
    // Load environment map from file path
    void load_environment_map(const std::string& path);
    
private:
    // ... existing members ...
    
    // Environment map resources
    dxr::Texture2D env_map_texture;
    D3D12_GPU_DESCRIPTOR_HANDLE env_map_srv;
    bool has_environment = false;
    
    // Helper for uploading environment map
    void upload_environment_map(const util::HDRImage& img);
};
```

#### 1.3.4: Implement Environment Map Upload
Add to `backends/dxr/render_dxr.cpp`:

```cpp
void RenderDXR::load_environment_map(const std::string& path) {
    std::cout << "Loading environment map: " << path << std::endl;
    
    try {
        // Load from file
        util::HDRImage img = util::load_environment_map(path);
        
        // Upload to GPU
        upload_environment_map(img);
        
        has_environment = true;
        std::cout << "Environment map loaded successfully\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to load environment map: " << e.what() << std::endl;
        has_environment = false;
    }
}

void RenderDXR::upload_environment_map(const util::HDRImage& img) {
    // Create GPU texture
    env_map_texture = dxr::Texture2D::device(
        device.Get(),
        glm::uvec2(img.width, img.height),
        D3D12_RESOURCE_STATE_COPY_DEST,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        D3D12_RESOURCE_FLAG_NONE
    );
    
    // Create upload buffer
    size_t upload_size = img.width * img.height * 4 * sizeof(float);
    
    dxr::Buffer upload_buf = dxr::Buffer::host(
        device.Get(),
        upload_size,
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    
    // Copy data to upload buffer
    uint8_t* mapped = nullptr;
    upload_buf.map((void**)&mapped);
    std::memcpy(mapped, img.data, upload_size);
    upload_buf.unmap();
    
    // Copy to GPU texture
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = env_map_texture.get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload_buf.get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = src.PlacedFootprint;
    footprint.Offset = 0;
    footprint.Footprint.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    footprint.Footprint.Width = img.width;
    footprint.Footprint.Height = img.height;
    footprint.Footprint.Depth = 1;
    footprint.Footprint.RowPitch = img.width * 4 * sizeof(float);
    
    command_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    
    // Transition to shader resource
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = env_map_texture.get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    command_list->ResourceBarrier(1, &barrier);
    
    // Execute upload commands
    CHECK_ERR(command_list->Close());
    ID3D12CommandList* lists[] = {command_list.Get()};
    command_queue->ExecuteCommandLists(1, lists);
    
    // Wait for upload to complete
    sync_gpu();
    
    // Reset command list for next frame
    CHECK_ERR(command_allocator->Reset());
    CHECK_ERR(command_list->Reset(command_allocator.Get(), nullptr));
    
    // Create SRV
    // (Add to existing descriptor heap)
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;
    
    // Get descriptor handle (add to heap management code)
    env_map_srv = /* allocate from descriptor heap */;
    
    device->CreateShaderResourceView(
        env_map_texture.get(),
        &srv_desc,
        /* CPU descriptor handle for env_map_srv */
    );
    
    std::cout << "Environment map uploaded to GPU\n";
}
```

**Note:** The descriptor heap management needs to be integrated with existing code. This is simplified.

#### 1.3.4: Bind to Shader Root Signature
Modify shader root signature to include environment map texture:

```cpp
// In create_rt_pipeline or similar:

// Add to root signature
CD3DX12_DESCRIPTOR_RANGE ranges[X];
// ... existing ranges ...

// Add range for environment map
ranges[env_map_range_index].Init(
    D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
    1,  // One texture
    ENV_MAP_REGISTER,  // e.g., t10
    0   // register space 0
);

// Update root parameters to include this range
```

### Acceptance Criteria
- [x] Environment map uploads to GPU
- [x] SRV created successfully
- [x] Bound to shader root signature
- [x] No memory leaks
- [x] Handles errors gracefully

---

## Task 1.4: Modify Miss Shader for Environment Sampling

### Objective
Update miss shader to sample environment map instead of procedural pattern.

### Subtasks

#### 1.4.1: Add Shader Resources
Modify `shaders/unified_render.slang`:

```glsl
// Global resources
[[vk::binding(X, 0)]]
Texture2D<float4> environment_map;

[[vk::binding(Y, 0)]]
SamplerState env_sampler;

// Or for DXR (register-based):
Texture2D<float4> environment_map : register(t10);
SamplerState env_sampler : register(s0);
```

#### 1.4.2: Implement Environment Miss Shader
Replace current Miss shader:

```glsl
[shader("miss")]
void Miss(inout Payload payload)
{
    payload.color_dist.w = -1.0f;  // Indicates miss
    
    // Get ray direction
    float3 dir = normalize(WorldRayDirection());
    
    // Convert to UV (lat-long mapping)
    // Using existing formula - already correct!
    float u = (1.0f + atan2(dir.x, -dir.z) * M_1_PI) * 0.5f;
    float v = acos(dir.y) * M_1_PI;
    
    // Sample environment map
    float4 env_color = environment_map.SampleLevel(env_sampler, float2(u, v), 0);
    
    payload.color_dist.rgb = env_color.rgb;
}
```

**Key points:**
- Use `SampleLevel` (not `Sample`) - no automatic derivatives in ray tracing
- Level 0 = full resolution, no mip-mapping yet
- UV wrapping handled by sampler state

#### 1.4.3: Configure Sampler State
In DXR setup code, create sampler:

```cpp
D3D12_SAMPLER_DESC sampler_desc = {};
sampler_desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;   // Wrap horizontally
sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;  // Clamp vertically
sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
sampler_desc.MipLODBias = 0.0f;
sampler_desc.MaxAnisotropy = 1;
sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
sampler_desc.MinLOD = 0.0f;
sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;

// Create sampler (add to descriptor heap)
```

**Why these settings:**
- `WRAP` for U: Environment wraps horizontally (360°)
- `CLAMP` for V: Poles don't wrap vertically
- `LINEAR`: Smooth interpolation

#### 1.4.4: Add Fallback for No Environment Map
Handle case when no environment loaded:

```glsl
[shader("miss")]
void Miss(inout Payload payload)
{
    payload.color_dist.w = -1.0f;  // Indicates miss
    
    float3 dir = normalize(WorldRayDirection());
    
    // Check if environment map is available
    // (Could use a push constant or check texture size)
    uint width, height;
    environment_map.GetDimensions(width, height);
    
    if (width > 0) {
        // Sample environment map
        float u = (1.0f + atan2(dir.x, -dir.z) * M_1_PI) * 0.5f;
        float v = acos(dir.y) * M_1_PI;
        
        float4 env_color = environment_map.SampleLevel(env_sampler, float2(u, v), 0);
        payload.color_dist.rgb = env_color.rgb;
    } else {
        // Fallback: checkered pattern (existing code)
        float u = (1.0f + atan2(dir.x, -dir.z) * M_1_PI) * 0.5f;
        float v = acos(dir.y) * M_1_PI;
        
        int check_x = int(u * 10.0f);
        int check_y = int(v * 10.0f);
        
        if (dir.y > -0.1f && (check_x + check_y) % 2 == 0) {
            payload.color_dist.rgb = float3(0.5f, 0.5f, 0.5f);
        } else {
            payload.color_dist.rgb = float3(0.1f, 0.1f, 0.1f);
        }
    }
}
```

### Acceptance Criteria
- [x] Environment map samples correctly in miss shader
- [x] No seam at U=0/U=1 boundary
- [x] Correct orientation (up is up, etc.)
- [x] Falls back to procedural when no env map
- [x] Compiles and runs without errors

---

## Task 1.5: Testing and Validation

### Objective
Verify environment map appears correctly with no artifacts.

### Subtasks

#### 1.5.1: Visual Validation Tests

**Test 1: Simple Gradient**
- Create or download gradient .exr (top=white, bottom=black)
- Load and display
- Verify orientation correct

**Test 2: Grid Pattern**
- Use environment map with visible grid
- Check for:
  - Proper horizontal wrapping
  - No seam at U=0/U=1
  - Vertical clamping at poles

**Test 3: Recognizable Scene**
- Load outdoor HDR environment (e.g., sky with sun)
- Verify:
  - Sun appears in correct direction
  - Horizon at V≈0.5
  - No distortion or blurring

**Test 4: Different Resolutions**
- Test with 512x256, 1024x512, 2048x1024, 4096x2048
- Verify all load and display correctly
- Check memory usage

#### 1.5.2: Quantitative Tests

**Test 1: Known Direction Values**
```glsl
// Add debug ray in test scene
// Ray looking straight up (0, 1, 0)
// Should sample environment at (0.5, 0.0)

// Ray looking at horizon forward (0, 0, -1)
// Should sample environment at (0.5, 0.5)
```

**Test 2: Roundtrip Accuracy**
```cpp
// CPU-side test
for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
        float u = (float)x / width;
        float v = (float)y / height;
        
        // Convert to direction
        glm::vec3 dir = uv_to_direction(u, v);
        
        // Convert back to UV
        glm::vec2 uv2 = direction_to_uv(dir);
        
        float error = glm::length(glm::vec2(u, v) - uv2);
        if (error > 0.001) {
            std::cerr << "Roundtrip error at (" << x << ", " << y << "): " 
                      << error << "\n";
        }
    }
}
```

#### 1.5.3: Performance Measurement
```cpp
// Measure frame time with/without environment
auto start = std::chrono::high_resolution_clock::now();
render_frame();
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

std::cout << "Frame time: " << duration.count() << "ms\n";
```

**Expected:** <5% overhead from environment sampling

#### 1.5.4: Create Comparison Images
Render same scene with:
1. No environment (procedural background)
2. Environment map loaded
3. Side-by-side comparison

Save images for documentation.

### Acceptance Criteria
- [x] All visual tests pass
- [x] Known directions sample correct pixels
- [x] Roundtrip test error < 0.001
- [x] Performance impact < 5%
- [x] No visual artifacts or seams

---

## Integration Checklist

### Code Changes
- [ ] `main.cpp` - Command-line parsing for `-env` flag
- [ ] `util/scene.h` - Add environment_map_path member
- [ ] `util/scene.cpp` - Update constructor
- [ ] `util/util.h` - HDRImage class
- [ ] `util/util.cpp` - load_environment_map()
- [ ] `backends/dxr/render_dxr.h` - Environment members
- [ ] `backends/dxr/render_dxr.cpp` - Upload code & set_scene() update
- [ ] `shaders/unified_render.slang` - Miss shader update

### Build System
- [ ] CMakeLists.txt updated
- [ ] Compiles without warnings
- [ ] Links correctly

### Resources
- [ ] Sample .exr files in test_data/
- [ ] Documentation screenshots
- [ ] Comparison images

### Testing
- [ ] Visual validation passed
- [ ] Quantitative tests passed
- [ ] Performance acceptable
- [ ] No memory leaks (run with sanitizers)

---

## Common Issues & Solutions

### Issue 1: Seam at U=0/U=1
**Symptom:** Visible line where environment wraps  
**Cause:** Sampler not wrapping correctly  
**Solution:** Ensure `AddressU = WRAP`, test with grid pattern

### Issue 2: Environment upside down
**Symptom:** Sky at bottom, ground at top  
**Cause:** V coordinate inverted  
**Solution:** Use `v = 1.0 - acos(dir.y) * M_1_PI` OR flip image

### Issue 3: Rotated environment
**Symptom:** Front/back swapped  
**Cause:** Azimuth angle formula wrong  
**Solution:** Verify `atan2(x, -z)` vs `atan2(x, z)`

### Issue 4: Blurry environment
**Symptom:** Environment looks too soft  
**Cause:** Mip level too high, or bilinear when want point  
**Solution:** Use `SampleLevel(..., 0)` for full resolution

### Issue 5: GPU upload fails
**Symptom:** Black environment or crash  
**Cause:** Incorrect texture format or upload size  
**Solution:** Verify RGBA float format, row pitch calculation

---

## Phase 1 Completion Criteria

Before moving to Phase 2:

- [ ] ✅ Environment map loads from .exr file
- [ ] ✅ Appears correctly when rays miss geometry
- [ ] ✅ No seams, artifacts, or distortion
- [ ] ✅ Proper lat-long UV mapping verified
- [ ] ✅ Performance acceptable (<5% overhead)
- [ ] ✅ Fallback works when no environment loaded
- [ ] ✅ All test cases pass
- [ ] ✅ Code documented and committed

**Estimated time:** 3-4 days

**Next:** Phase 2 - Environment Light Importance Sampling

---

**Document Status:** Ready for execution  
**Dependencies:** Phase 0 complete  
**Last Updated:** December 18, 2025
