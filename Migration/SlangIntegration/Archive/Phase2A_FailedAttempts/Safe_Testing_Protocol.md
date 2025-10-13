# Safe Testing Protocol for DXR Phase 2A.2

## Status Summary

**Code Fix Status:** ✅ **ALREADY APPLIED**
- Proper null descriptor creation is ALREADY in the code (lines 1112-1130)
- Descriptor heap allocation matches shader registers (t100-t102)
- Null descriptors use valid `D3D12_SHADER_RESOURCE_VIEW_DESC` structure

**Issue:** GPU hang occurred DESPITE the fix being present. This suggests the problem may be:
1. Different than initially diagnosed
2. Related to shader logic accessing uninitialized data
3. Related to resource state/synchronization
4. Related to memory access patterns in the shader

## Root Cause Analysis

### What We Know
1. **Null descriptors are CORRECT** - code at lines 1125-1127 properly creates null SRVs
2. **Register numbers are CORRECT** - shader uses t100-t102, heap allocates at t100
3. **GPU hung during first frame render** - not during initialization
4. **Complete system freeze** - indicates severe GPU fault (TDR failure)

### Potential New Issues

#### Issue A: Shader Buffer Access Out of Bounds
```hlsl
uint meshID = InstanceID();  // Line 324
MeshDesc mesh = meshDescs[meshID];  // Could be OOB!
```

**Problem:** If `InstanceID()` returns a value >= `mesh_descriptors.size()`, this is undefined behavior.

**Check:** How many MeshDesc entries exist? How many instances in TLAS?

#### Issue B: Index Buffer Access Out of Bounds
```hlsl
uint triIndex = mesh.ibOffset + PrimitiveIndex() * 3;  // Line 327
uint idx0 = globalIndices[triIndex + 0];
```

**Problem:** If `mesh.ibOffset` or `mesh.numIndices` is corrupted/incorrect, could read beyond buffer.

#### Issue C: Vertex Buffer Access Out of Bounds
```hlsl
Vertex v0 = globalVertices[mesh.vbOffset + idx0];  // Line 332
```

**Problem:** If `mesh.vbOffset` + index exceeds vertex buffer size, GPU fault.

#### Issue D: Resource State Mismatch
The global buffers are transitioned to `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` but DXR shaders need `D3D12_RESOURCE_STATE_COMMON` or proper ray tracing states.

## Safety Testing Phases

### Phase 1: Pre-Flight Validation (NO GPU EXECUTION)

**Goal:** Verify data integrity before running shader

```cpp
// Add to RenderDXR::render() BEFORE ExecuteCommandLists
std::cout << "=== PRE-RENDER VALIDATION ===\n";
std::cout << "Scene statistics:\n";
std::cout << "  Meshes: " << scene.meshes.size() << "\n";
std::cout << "  Parameterized meshes: " << scene.parameterized_meshes.size() << "\n";
std::cout << "  Instances: " << scene.instances.size() << "\n";
std::cout << "  MeshDesc buffer entries: " << scene.mesh_descriptors.size() << "\n";
std::cout << "  Global vertices: " << scene.global_vertices.size() / sizeof(Vertex) << "\n";
std::cout << "  Global indices: " << scene.global_indices.size() / sizeof(uint32_t) << "\n";

// Validate MeshDesc entries
for (size_t i = 0; i < scene.mesh_descriptors.size(); ++i) {
    auto& desc = scene.mesh_descriptors[i];
    std::cout << "  MeshDesc[" << i << "]: vbOff=" << desc.vbOffset 
              << " ibOff=" << desc.ibOffset 
              << " numVerts=" << desc.numVertices 
              << " numIdx=" << desc.numIndices << "\n";
    
    // Bounds check
    uint32_t max_vertex_idx = desc.vbOffset + desc.numVertices;
    uint32_t max_index_idx = desc.ibOffset + desc.numIndices;
    uint32_t total_vertices = scene.global_vertices.size() / sizeof(Vertex);
    uint32_t total_indices = scene.global_indices.size() / sizeof(uint32_t);
    
    if (max_vertex_idx > total_vertices) {
        std::cerr << "ERROR: MeshDesc[" << i << "] vertex range [" 
                  << desc.vbOffset << ", " << max_vertex_idx 
                  << ") exceeds buffer size " << total_vertices << "\n";
        abort();
    }
    if (max_index_idx > total_indices) {
        std::cerr << "ERROR: MeshDesc[" << i << "] index range [" 
                  << desc.ibOffset << ", " << max_index_idx 
                  << ") exceeds buffer size " << total_indices << "\n";
        abort();
    }
}
std::cout << "=== VALIDATION PASSED ===\n";
```

### Phase 2: D3D12 Debug Layer Validation

**Goal:** Enable maximum GPU debugging before execution

```cpp
// Add to RenderDXR constructor, BEFORE creating device
ComPtr<ID3D12Debug> debug_controller;
if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
    debug_controller->EnableDebugLayer();
    std::cout << "[DXR] D3D12 Debug Layer enabled\n";
    
    // Enable GPU-based validation (CRITICAL for OOB detection)
    ComPtr<ID3D12Debug1> debug1;
    if (SUCCEEDED(debug_controller.As(&debug1))) {
        debug1->SetEnableGPUBasedValidation(TRUE);
        std::cout << "[DXR] GPU-Based Validation enabled\n";
    }
}

// After device creation, set break-on-error
ComPtr<ID3D12InfoQueue> info_queue;
if (SUCCEEDED(device.As(&info_queue))) {
    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);  // Too noisy
    std::cout << "[DXR] InfoQueue break-on-error enabled\n";
}
```

### Phase 3: Fence Timeout Protection

**Goal:** Detect GPU hangs without freezing system

```cpp
// Add to RenderDXR::render() AFTER ExecuteCommandLists
std::cout << "Waiting for GPU with 10-second timeout...\n";
UINT64 fence_value = ++fence_counter;
cmd_queue->Signal(fence.Get(), fence_value);

if (fence->GetCompletedValue() < fence_value) {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(fence_value, event);
    
    DWORD wait_result = WaitForSingleObject(event, 10000);  // 10 second timeout
    CloseHandle(event);
    
    if (wait_result == WAIT_TIMEOUT) {
        std::cerr << "CRITICAL: GPU execution timeout! Likely infinite loop or hang.\n";
        std::cerr << "Check:\n";
        std::cerr << "  1. Shader buffer access patterns\n";
        std::cerr << "  2. Resource states\n";
        std::cerr << "  3. Descriptor heap bindings\n";
        abort();  // Exit before TDR
    }
}
std::cout << "GPU completed successfully\n";
```

### Phase 4: PIX Capture (MANDATORY FIRST TEST)

**Before running normally, capture with PIX:**

1. Launch PIX for Windows
2. File → Attach to running process OR Launch new process
3. Select `chameleonrt.exe`
4. Click "Take GPU Capture"
5. **CRITICAL:** PIX will abort on GPU errors WITHOUT system hang
6. Inspect:
   - Resource states
   - Descriptor heap contents
   - Shader buffer bindings
   - GPU timeline (look for infinite loops)

### Phase 5: Single-Frame Exit Test

**Goal:** Execute one frame then exit immediately

```cpp
// Add to main.cpp or render loop
static int frame_count = 0;
if (++frame_count == 1) {
    std::cout << "Single frame test complete - exiting\n";
    exit(0);
}
```

### Phase 6: Graduated Testing

1. **Test 1:** Run with single-triangle OBJ (simplest case)
2. **Test 2:** Run with test_cube.obj (8 vertices, 12 triangles)
3. **Test 3:** Run with complex scene only if Tests 1-2 pass

## Decision Tree

```
START
  ↓
Enable D3D12 Debug Layer + GPU Validation
  ↓
Add Pre-Flight Data Validation
  ↓
Rebuild (Debug config)
  ↓
Test in PIX (DO NOT RUN NORMALLY YET)
  ↓
PIX Success? ─── NO ──→ Inspect PIX errors, fix, repeat
  ↓ YES
Add Fence Timeout
  ↓
Rebuild
  ↓
Run with Single-Frame Exit + test_cube.obj
  ↓
Success? ─── NO ──→ Check timeout output, debug data validation
  ↓ YES
Remove Single-Frame Exit
  ↓
Run continuous with test_cube.obj
  ↓
Success? ─── NO ──→ Multiple-frame issue, check per-frame state
  ↓ YES
Test with complex scene
  ↓
DONE
```

## Code Changes Required

### Change 1: Add Debug Layer (render_dxr.cpp, before device creation)

### Change 2: Add Pre-Flight Validation (render_dxr.cpp, in render() method)

### Change 3: Add Fence Timeout (render_dxr.cpp, in render() method)

### Change 4: Fix Resource States (if needed)

Check if global buffers need different state for DXR:
```cpp
// Current state (line ~211):
D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE

// DXR may need:
D3D12_RESOURCE_STATE_COMMON  // Or
D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE  // For AS only
```

**Note:** StructuredBuffers typically work fine in NON_PIXEL_SHADER_RESOURCE for DXR.

## Next Steps

**DO NOT RUN** until:
1. ✅ Debug layer enabled
2. ✅ Pre-flight validation added
3. ✅ Fence timeout added
4. ✅ PIX capture successful
5. ✅ Single-frame test successful

**Recommended Order:**
1. Add all three safety measures (Debug, Validation, Timeout)
2. Rebuild
3. Test with PIX FIRST
4. If PIX succeeds, test with single-frame exit
5. If single-frame succeeds, test continuous
6. If continuous succeeds, declare success

## Abort Conditions

**STOP IMMEDIATELY if:**
- PIX shows GPU errors
- Fence timeout triggers
- Pre-flight validation fails
- Any D3D12 debug layer error appears
- System becomes unresponsive (should not happen with timeout)

## Success Criteria

Phase 2A.2 is complete when:
1. PIX capture shows no errors
2. Application renders correctly (matches Phase 2A.0 screenshots)
3. No debug layer messages
4. Fence completes within normal time (<1 second for test_cube)
5. Continuous rendering stable for 30+ seconds
