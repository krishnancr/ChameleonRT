# Plan A: Unified Slang GFX Backend Implementation

This plan implements ImGui rendering entirely through Slang GFX abstraction, providing a single codebase that works across Vulkan, D3D12, and future APIs.

## Overview

**Approach**: Pure Slang GFX implementation
**Benefits**:
 - Single codebase for all graphics APIs
 - Architectural consistency with SlangDisplay
 - Future-proof for new APIs (Metal, WebGPU)
 - Educational value in graphics pipeline implementation

**Architecture**: Custom ImGui renderer using Slang GFX resources and command encoding


## Stage 0: Baseline Raster Validation ✅ COMPLETED

**Objective**: Prove out the Slang raster pipeline by presenting a hard-coded RGB triangle on both D3D12 and Vulkan, confirming that swap chain management, transient heaps, and render-pass layout are stable enough to host ImGui.

**Implementation Status**: ✅ **COMPLETED**  
**Commit SHA**: `41cdca57c90bf9161ee10b466fd109afac9f07ed`  
**Completion Date**: `September 10, 2025`

**What was done**
- Replaced legacy scene rendering paths with a minimal Slang graphics pipeline that outputs a single triangle with red, green, and blue vertices.  
- Verified command-buffer submission, transient-heap reset, and present loop across both backends.  
- Captured RenderDoc/PIX frames to ensure render-pass state transitions match expectations.

**Validation Results**
- Triangle renders consistently on D3D12 and Vulkan builds (Debug configuration).  
- No warnings from Slang validation layers, DX debug layer, or Vulkan validation.  
- Window interaction (resize, minimize, close) remains stable for extended runs (>30 s).  
- Latest capture artifacts stored in `captures/2025-09-10-rgb-triangle/` for reference.

**Next Stage**: Stage 1 – renderer scaffolding (completed below).

## Reference Inspiration

We’re following the structure shown in Hyiker’s “Slang backend for imgui” gist (`Gui.cpp` + `Gui.raster.slang`). The gist demonstrates the sequence of steps needed to render ImGui through Slang GFX—shader setup, buffer uploads, font atlas creation, command encoding, and input plumbing. Because the gist is unlicensed and tightly coupled to the Voluma framework, we’re **re‑implementing** the logic in ChameleonRT instead of copying it verbatim. Each stage below notes which ideas come from the gist so we can stay aligned without inheriting incompatible code.
## Stage 1: Renderer Scaffolding ✅ COMPLETED

**Objective**: Introduce a `SlangImGuiRenderer` class skeleton that owns device references and exposes `initialize`, `render`, and `shutdown` entry points.

**Implementation Status**: ✅ **COMPLETED**  
**Completion Date**: `October 1, 2025`

**What was done**
- Added `SlangImGuiRenderer.h/.cpp` with member storage for device, pipeline, buffer, texture, and sampler placeholders.  
- Implemented `initialize()` to capture device references, emit stub-status logging, and return `false` until Stage 2 wires real work.  
- Ensured `render()` is a guarded no-op with single-shot logging and `shutdown()` clears all resources so repeated init/shutdown cycles are safe.  
- Integrated the renderer into `SlangDisplay` behind `CRT_ENABLE_SLANG_IMGUI_RENDERER`, exercising the stub each frame without impacting existing rendering.

**Validation Results**
- `cmake -B build` / `cmake --build build --config Debug` / `.\build\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"` succeed, with console output confirming the stub initializes once and gracefully reports the unimplemented path.  
- No new warnings observed during runtime; the application shuts down cleanly with the stub active.

**Next Stage**: Stage 3 – dynamic buffer layer.

---

## Stage 2: Shader & Pipeline Bootstrap ✅ COMPLETED

**Objective**: Embed the ImGui vertex/pixel shaders, compile them through Slang, and create the graphics pipeline state.

**Implementation Status**: ✅ **COMPLETED**  
**Completion Date**: `October 1, 2025`

**What was done**
- Inlined the ImGui vertex and fragment shaders inside `SlangImGuiRenderer.cpp`, exposing `vertexMain`/`fragmentMain` entry points that mirror the official ImGui raster pipeline.  
- Added helper routines that invoke `gfx::IDevice::createProgram2` on the embedded source, build a matching `IInputLayout` for `ImDrawVert`, and assemble a `GraphicsPipelineStateDesc` with ImGui’s standard alpha-blend state.  
- Guarded initialization with clear diagnostics so we surface shader compiler output and pipeline failures directly in the console.

**Validation Results**
- `cmake --build build --config Debug` succeeds after the changes, producing `build\Debug\chameleonrt.exe`.  
- Runtime logging now prints `[SlangImGuiRenderer] Stage 2 setup complete (shader + pipeline built); awaiting buffer/font stages`, verifying the program/pipeline path executed without warnings.

**Next Stage**: Stage 3 – dynamic buffer layer.

---

## Stage 3: Dynamic Buffer Layer ✅ COMPLETED

**Objective**: Manage per-frame vertex and index upload buffers sized to the current ImGui draw data.

**Implementation Status**: ✅ **COMPLETED**  
**Completion Date**: `October 1, 2025`

**What was done**
- Added capacity helpers that grow the vertex/index upload buffers geometrically and tag them with debug names for easy PIX/RenderDoc inspection.  
- Implemented a unified upload path in `render()` that maps once per frame, copies all `ImDrawList` data, and unmaps safely even on failure paths.  
- Created an identity-populated constant buffer so upcoming stages can drop projection data without reworking initialization.

**Validation Results**
- `cmake --build build --config Debug` completes cleanly with the new buffer logic compiled in.  
- Runtime logging now reports buffer growth (e.g., `[SlangImGuiRenderer] Resized vertex buffer to … bytes`) when ImGui draw data size increases, confirming the doubling strategy engages.

**Next Stage**: Stage 4 – font atlas & sampler.

---

## Stage 4: Font Atlas & Sampler ✅ COMPLETED

**Objective**: Upload ImGui’s font atlas into a Slang texture and create the sampler used during rendering.

**Implementation Status**: ✅ **COMPLETED**  
**Completion Date**: `October 2, 2025`

**What was done**
- Added `createFontResources()` to `SlangImGuiRenderer`, translating ImGui’s RGBA32 atlas into a `gfx::Texture2D` and tagging it with debug names for inspection.
- Created a shader-resource view plus sampler state, then wired the resulting handle back into ImGui via `SetTexID`, mirroring the official GUI sample flow.
- Cleared ImGui’s CPU-side atlas data after upload to match the DXR/Vulkan backends’ lifetime expectations.

**Validation Results**
- `cmake --build build --config Debug` succeeds with the new texture/sampler path compiled in (logged atlas upload sizes during initialization).  
- Console output now includes `[SlangImGuiRenderer] Uploaded font atlas of size …` followed by the Stage 4 completion banner, confirming the resources are live.

**Next Stage**: Stage 5 – draw encoding.

---

## Stage 5: Draw Encoding ✅ COMPLETED

**Objective**: Implement `render()` so it consumes ImGui draw lists, sets up an orthographic projection, and issues indexed draws with proper scissor rectangles.

**Implementation Status**: ✅ **COMPLETED**  
**Completion Date**: `October 2, 2025`

**What was done**
- Added a full draw path that updates the Stage 3 constant buffer with ImGui’s orthographic matrix, caches the values, and feeds them into the Slang root object before each frame.  
- Bound the font texture/sampler (or any per-command `TextureId`) through `setResource`/`setSampler`, then walked every ImGui draw list to configure scissors and issue `drawIndexed` calls with proper base offsets.  
- Implemented resilience for user callbacks, including `ImDrawCallback_ResetRenderState`, and logged a one-time Stage 5 summary once commands execute.

**Validation Results**
- `cmake --build build --config Debug` compiles with the new draw encoding enabled.  
- When launching `chameleonrt.exe slang …`, console output now includes `[SlangImGuiRenderer] Stage 5 draw encoding active: …` with live draw statistics, confirming the command buffers execute.  
- Scissor rectangles and atlas bindings were inspected via logging; RenderDoc/PIX capture is queued for follow-up in Stage 7.

**Next Stage**: Stage 6 – integrate with `SlangDisplay` lifecycle nuances (resize, shutdown, multi-API smoke tests).

**Gist Parallels**: Mirrors the draw loop in `gui.cpp`, adapted to the Slang command encoder APIs and our persistent upload buffers.

---

## Stage 6: SlangDisplay Integration

**Objective**: Wire the renderer into `SlangDisplay` so ImGui rendering happens alongside the existing raster path, with correct lifecycle management.

**Tasks**
- Instantiate `SlangImGuiRenderer` inside `SlangDisplay`, initializing it on first use and shutting down during destruction.  
- Call `render()` after we finish scene work (currently the RGB triangle) but before presenting.  
- Handle per-backend render-pass nuances (e.g., Vulkan render-pass layouts vs. D3D12 null layout).

**Validation**
- Launch ChameleonRT (D3D12 + Vulkan Debug) and interact with ImGui demo widgets.  
- Exercise window resize, minimise/restore, and multi-monitor scenarios.  
- Check for resource leaks or dangling references during shutdown.

**Acceptance Criteria**
- ImGui responds to input and overlays the scene correctly in both APIs.  
- No new warnings from validation layers or SDL event handling.  
- Shutdown path releases all renderer resources deterministically.

**Gist Parallels**: Aligns with `Gui::beginFrame` / `Gui::endFrame` usage, ported to ChameleonRT’s `SlangDisplay` workflow.

---

## Stage 7: Cross-API Regression Suite

**Objective**: Lock in a repeatable test matrix so future changes don’t regress ImGui rendering.

**Tasks**
- Define test cases covering D3D12 Debug/Release and Vulkan Debug/Release builds.  
- Document manual checks: window resize, HiDPI scaling, ImGui demo toggles, camera controls.  
- Capture reference screenshots or notes for diffing.

**Validation**
- Execute the matrix and record pass/fail results in a checklist.  
- Investigate any backend-specific anomalies and file follow-up issues if needed.

**Acceptance Criteria**
- Consistent visuals and performance across both graphics APIs (≤10 % frame-time delta attributable to ImGui).  
- Checklist stored with date stamps and build hashes.  
- Regressions can be surfaced quickly during PR review.

---

## Stage 8: Documentation & Follow-ups

**Objective**: Capture the state of the integration and triage any optional enhancements once the core renderer ships.

**Tasks**
- Update this plan with completion dates and lessons learned.  
- Extend developer documentation (e.g., `docs/` or README) with steps to enable/test the Slang ImGui path.  
- Gather wishlist items (input devices, docking, theming, controller support) and rank them.

**Validation**
- Docs reference accurate build flags, asset locations, and troubleshooting tips.  
- Future work items tracked either in this doc or issue tracker.

**Acceptance Criteria**
- New contributors can follow the documentation to build and validate the ImGui renderer.  
- Follow-up tasks are clearly scoped, with owners or status notes.

---

## Quick Build & Test Commands

```powershell
# D3D12 (default)
cd C:\dev\ChameleonRT
cmake -B build
cmake --build build --config Debug
cd build
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"

# Vulkan
cd C:\dev\ChameleonRT
cmake -B build_vulkan -DUSE_VULKAN=ON
cmake --build build_vulkan --config Debug
cd build_vulkan
.\Debug\chameleonrt.exe slang "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"

```

