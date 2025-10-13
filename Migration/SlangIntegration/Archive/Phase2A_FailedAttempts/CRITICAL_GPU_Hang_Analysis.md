# CRITICAL: GPU Hang Analysis - Phase 2A.2 DXR

**Date:** 2025-10-11  
**Status:** ⚠️ CRITICAL - System hang on render  
**Severity:** HIGH - Requires hard reset

---

## Incident Report

### Symptoms
- Application launches successfully
- ImGui window appears
- **Complete system freeze** - no Task Manager, requires hard reset
- Occurs during first render frame

### Root Cause Hypothesis

**Primary Suspect: Invalid Null Descriptor Creation**

In `render_dxr.cpp` lines ~1110-1130, we create null SRVs for unused texture slots:

```cpp
} else {
    // Null SRV for unused slots
    device->CreateShaderResourceView(nullptr, nullptr, heap_handle);
}
```

**Problem:** D3D12 does NOT allow completely null descriptors (both resource and desc are nullptr).

According to D3D12 specification:
- To create a null descriptor: Pass valid `D3D12_SHADER_RESOURCE_VIEW_DESC` with `nullptr` resource
- Passing `nullptr` for both causes **undefined behavior** → GPU fault

### Why This Causes System Hang

1. Shader tries to access texture array at runtime
2. GPU hardware encounters invalid descriptor
3. GPU page fault or infinite retry loop
4. TDR (Timeout Detection and Recovery) fails to recover
5. System becomes unresponsive

---

## Safe Recovery Plan

### Phase 1: Disable Dangerous Code (DO NOT RUN YET)

**Option A: Revert to Unbounded Array (Recommended)**
- Change shader back to `Texture2D textures[]` (unbounded)
- Use dynamic resource binding (SM 6.6) if available
- Fall back to large but reasonable bound (e.g., 1024) with proper null descriptors

**Option B: Create Dummy Texture for Null Slots**
- Create a 1x1 white texture at initialization
- Use this dummy texture for all unused slots
- Safer than null descriptors

**Option C: Use Descriptor Indexing Extension**
- Use `ResourceDescriptorHeap` in HLSL (requires SM 6.6+)
- Bindless resources - no fixed array size needed

### Phase 2: Add Safety Checks

Before ANY testing:

1. **Add Descriptor Validation**
   ```cpp
   // Validate descriptor creation succeeded
   if (FAILED(device->CreateShaderResourceView(...))) {
       std::cerr << "ERROR: Failed to create SRV\n";
       return;
   }
   ```

2. **Add GPU Timeout Override (Development Only)**
   ```cpp
   // Registry setting to disable TDR during development
   // HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\GraphicsDrivers
   // TdrDelay = REG_DWORD (2-60 seconds, default 2)
   // WARNING: Only for development, revert after
   ```

3. **Add Frame Timeout**
   ```cpp
   // In render(), add timeout
   auto start = std::chrono::high_resolution_clock::now();
   // ... render ...
   auto elapsed = std::chrono::high_resolution_clock::now() - start;
   if (elapsed > std::chrono::seconds(5)) {
       std::cerr << "TIMEOUT: Render took >5s, likely GPU hang\n";
       abort();
   }
   ```

4. **Enable D3D12 Debug Layer Messages**
   ```cpp
   // In create_device_objects(), add:
   ComPtr<ID3D12InfoQueue> info_queue;
   device->QueryInterface(IID_PPV_ARGS(&info_queue));
   info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
   info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
   ```

### Phase 3: Test Incrementally

**DO NOT run full renderer yet.** Test in stages:

1. **Test descriptor heap creation only** (no render)
2. **Test pipeline creation only** (no dispatch)
3. **Test single-frame render with fence** (can abort if timeout)
4. **Test with PIX capture** (can inspect GPU state without hang)

---

## Immediate Fix Implementation

### Fix 1: Create Proper Null Descriptors (SAFEST)

```cpp
// Write the SRVs for the textures
// Phase 2A.2: Allocate all 64 texture slots to match shader declaration
for (size_t i = 0; i < 64; ++i) {
    if (i < textures.size()) {
        // Real texture
        auto &t = textures[i];
        D3D12_SHADER_RESOURCE_VIEW_DESC tex_desc = {0};
        tex_desc.Format = t.pixel_format();
        tex_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        tex_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        tex_desc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(t.get(), &tex_desc, heap_handle);
    } else {
        // PROPER null SRV for unused slots
        D3D12_SHADER_RESOURCE_VIEW_DESC null_desc = {0};
        null_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        null_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_desc.Texture2D.MipLevels = 1;
        null_desc.Texture2D.MostDetailedMip = 0;
        device->CreateShaderResourceView(nullptr, &null_desc, heap_handle);  // nullptr resource, VALID desc
    }
    heap_handle.ptr +=
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}
```

### Fix 2: Revert to Unbounded Array (ALTERNATIVE)

This avoids the null descriptor problem entirely:

**shader:**
```hlsl
Texture2D textures[] : register(t3);  // Unbounded, back to original
```

**C++:**
```cpp
.add_srv_range(!textures.empty() ? textures.size() : 1, 3, 0)  // Dynamic size
```

**Problem:** This puts us back to the overlap issue. **Solution:** Move global buffers even higher (t1000, t1001, t1002).

---

## Recommended Action Plan

### Step 1: Apply Safe Fix (Choose ONE)

**Recommendation: Fix 1 (Proper Null Descriptors)** - Safest, smallest change

**Alternative: Fix 2 + Move Global Buffers to t1000+** - Avoids null descriptors entirely

### Step 2: Add Safety Checks

Before testing, add ALL of these:

1. D3D12 debug layer breaks on error
2. Validation of CreateShaderResourceView return values
3. Frame timeout detection
4. PIX capture ability

### Step 3: Test with PIX First

**DO NOT run normally.** Use PIX for Windows:

```powershell
# Launch with PIX
"C:\Program Files\Microsoft PIX\PIX.exe"
# File → Attach → Select chameleonrt.exe
# GPU Capture → Capture single frame
# If GPU hangs, PIX will show which draw call caused it
```

### Step 4: Incremental Testing

Only AFTER PIX capture succeeds:

1. Run with fence timeout (5 second limit)
2. Run with single frame, then exit
3. Run normally

---

## Prevention for Future

1. **Always test with PIX first** after shader changes
2. **Use D3D12 debug layer** during development
3. **Add fence timeouts** for all GPU work
4. **Validate all descriptor creation** with error checks
5. **Test on separate machine** when possible (to avoid main machine crashes)

---

## Current Status

- ⚠️ **DO NOT RUN RENDERER** until fix applied
- ⚠️ **Apply Fix 1 or Fix 2** before any testing
- ⚠️ **Test with PIX** before normal execution
- ⚠️ **Add safety checks** listed above

---

## Next Steps

**Waiting for user confirmation before proceeding with fix.**

Which approach do you prefer?
- **A**: Fix null descriptor creation (proper D3D12_SHADER_RESOURCE_VIEW_DESC)
- **B**: Revert to unbounded array + move global buffers to t1000+
- **C**: Create dummy 1x1 texture for unused slots
- **D**: Other suggestion
