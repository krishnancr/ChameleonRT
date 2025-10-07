# Common Debugging Prompts

## Overview

Pre-written prompts for common issues during Slang integration.

**Use these when:** Something isn't working and you need help debugging.

**Pattern:** Paste the relevant prompt, add your specific error message/symptoms.

---

## Build Errors

### Debug Prompt: CMake Configuration Fails

```
Problem: CMake configuration failing

Error message:
[Paste your CMake error here]

Context:
- Step: [e.g., "01-cmake-setup Prompt 1"]
- Command: [paste command you ran]
- Expected: Configuration to succeed
- Actual: [describe what happened]

Files involved:
- CMakeLists.txt (which one?)
- [any other files]

Please help me:
1. Understand what the error means
2. Identify what's misconfigured
3. Fix the issue

Check against Migration/reference/integration_notes.txt for common issues.
```

---

### Debug Prompt: Compilation Errors

```
Problem: C++ compilation failing

Error message:
[Paste compiler error]

Context:
- File: [e.g., backends/dxr/render_dxr.cpp]
- What I changed: [describe recent changes]
- Expected: Clean compilation
- Actual: Errors

Please help me:
1. Interpret the error message
2. Identify the root cause
3. Suggest a fix

Common issues to check:
- Missing #include?
- Missing #ifdef guards?
- Type mismatch?
- Missing library link?
```

---

### Debug Prompt: Linking Errors

```
Problem: Linker errors

Error message:
[Paste linker error, e.g., "unresolved external symbol"]

Context:
- Target: [e.g., crt_dxr]
- What I changed: [recent CMake or code changes]
- Expected: Successful linking
- Actual: Unresolved symbols

Unresolved symbols:
[List them if you can identify]

Please help me:
1. Identify what's not being linked
2. Check CMakeLists.txt for missing target_link_libraries
3. Verify library dependencies

Check:
- Is slang_compiler_util being linked?
- Is Slang::Slang linked?
- Platform libraries (d3d12.lib, Vulkan, etc.)?
```

---

## Runtime Errors

### Debug Prompt: DLL Not Found

```
Problem: Application won't start - missing DLL

Error message:
"The code execution cannot proceed because [DLL_NAME] was not found"

Missing DLL: [slang.dll, gfx.dll, etc.]

Context:
- Build configuration: [Debug/Release]
- Where I'm running from: [build\Debug, etc.]
- Slang_ROOT: [path]

Please help me:
1. Verify DLL should be deployed
2. Check CMake POST_BUILD command
3. Manually deploy if needed

Show me:
- Where to find the DLL
- Where it should be copied to
- CMake command to automate deployment
```

---

### Debug Prompt: Slang Initialization Fails

```
Problem: SlangShaderCompiler initialization failing

Symptoms:
- slangCompiler.isValid() returns false
- OR: Crash when creating SlangShaderCompiler
- OR: [describe symptom]

Context:
- Backend: [DXR/Vulkan]
- Slang DLL present: [yes/no - check build directory]
- Slang version: [Debug/Release]

Error (if any):
[Paste error from getLastError() or console]

Please help me:
1. Diagnose why Slang initialization fails
2. Check DLL compatibility (Debug vs Release)
3. Verify Slang SDK is correct version

Debug steps:
- Is slang.dll in same directory as .exe?
- Does DLL version match (Debug build needs Debug DLL)?
- Try creating IGlobalSession manually to isolate issue
```

---

### Debug Prompt: Shader Compilation Fails

```
Problem: Shader compilation failing through Slang

Failed shader: [name, type]
Entry point: [entry point name]
Source language: [HLSL/GLSL/Slang]
Target: [DXIL/SPIRV]

Error from getLastError():
[Paste complete error message from slangCompiler.getLastError()]

Shader source (or relevant excerpt):
[Paste shader code, especially entry point]

Compilation code:
[Paste C++ code that calls compile function]

Please help me:
1. Interpret Slang error message
2. Identify syntax issues or mismatches
3. Fix the shader or compilation call

Common issues to check:
- Entry point name match exactly?
- [shader("stage")] attribute correct?
- Shader stage enum correct?
- Source language setting correct?
```

---

### Debug Prompt: Rendering Issues

```
Problem: Shaders compile but rendering is wrong

Symptoms:
- Black screen
- Incorrect colors
- Crash during rendering
- [describe what you see]

Context:
- Backend: [DXR/Vulkan]
- Shader(s) involved: [which shaders using Slang]
- Compilation logs: [any warnings/errors]
- Validation layers: [any errors from PIX/Vulkan validation]

Expected: [what should render]
Actual: [what actually renders]

Comparison:
- Non-Slang build: [does it work correctly?]
- Visual difference: [describe]

Please help me:
1. Diagnose rendering issue
2. Check if bytecode is valid
3. Verify pipeline setup
4. Identify shader bugs

Debug tools available:
- PIX (for D3D12): [yes/no]
- RenderDoc (for Vulkan): [yes/no]
- Validation layers enabled: [yes/no]
```

---

## Validation Errors

### Debug Prompt: D3D12 Validation Layer Errors

```
Problem: D3D12 debug layer reporting errors

Validation error message:
[Paste complete D3D12 error from debug output]

Context:
- Shader: [which shader]
- When: [during what operation: pipeline creation, rendering, etc.]
- Recent changes: [what did you change]

Please help me:
1. Interpret D3D12 validation message
2. Identify root cause
3. Fix the issue

Common D3D12 issues with Slang:
- DXIL verification failure
- Root signature mismatch
- Shader model version issue
```

---

### Debug Prompt: Vulkan Validation Errors

```
Problem: Vulkan validation layers reporting errors

Validation error message:
[Paste complete Vulkan validation error]

Context:
- Shader: [which shader]
- When: [vkCreateShaderModule, vkCreatePipeline, etc.]
- Recent changes: [what did you change]

Please help me:
1. Interpret Vulkan validation message
2. Identify issue (SPIRV invalid, binding mismatch, etc.)
3. Fix it

Common Vulkan issues with Slang:
- SPIRV validation failure
- Descriptor set binding mismatch
- Shader stage mismatch
```

---

## Performance Issues

### Debug Prompt: Slow Shader Compilation

```
Problem: Shader compilation taking too long

Symptoms:
- Compilation time: [X seconds/minutes]
- Happens: [first compile only, every time]
- Shader: [which shader, size]

Expected: [reasonable time, e.g., <1 second]
Actual: [actual time]

Context:
- Debug or Release build: [which]
- Slang DLL: [Debug or Release]
- Shader complexity: [simple/complex]

Please help me:
1. Determine if this is normal
2. Identify bottlenecks
3. Optimize if possible

Things to check:
- First compile vs subsequent (Slang initialization overhead?)
- Debug vs Release Slang DLL
- Shader caching possible?
```

---

### Debug Prompt: Runtime Performance Regression

```
Problem: Performance worse with Slang-compiled shaders

Measurements:
- Non-Slang frame time: [X ms]
- Slang frame time: [Y ms]
- Regression: [percentage]

Context:
- Backend: [DXR/Vulkan]
- Scene: [test_cube.obj, etc.]
- Build: [Debug/Release]

Please help me:
1. Determine if regression is expected
2. Identify cause (compilation overhead, shader execution, etc.)
3. Profile if needed

Note: Some overhead expected during development (dual paths, logging, etc.)
Is this within acceptable range (5-10%)?
```

---

## Integration Issues

### Debug Prompt: Dual Compilation Not Working

```
Problem: Can't build with/without Slang

Issue:
- Builds with ENABLE_SLANG=ON: [works/fails]
- Builds with ENABLE_SLANG=OFF: [works/fails]

Error (if any):
[Paste build error]

Context:
- What I changed: [recent changes]
- Expected: Both modes should work
- Actual: [which mode fails]

Please help me:
1. Check #ifdef guards are correct
2. Verify CMake conditionals
3. Fix dual-path compilation

Common issues:
- Missing #ifdef around Slang code
- CMake target_link_libraries unconditional
- Header include not guarded
```

---

### Debug Prompt: Wrong Bytecode Size

```
Problem: Compiled bytecode size seems wrong

Observation:
- Shader: [name]
- Bytecode size: [X bytes or 0]
- Expected: [reasonable size, e.g., 1-10 KB]

Compilation succeeded: [yes/no]
Error messages: [if any]

Please help me:
1. Verify compilation actually succeeded
2. Check bytecode extraction correct
3. Compare with non-Slang compilation

Debug:
- Is result->bytecode.empty() ?
- Compare with DXC/glslang output size
- Check if shader is actually compiling or using cached/default
```

---

## Using These Prompts

**Template:**
1. Copy the relevant debug prompt
2. Fill in [placeholders] with your specific info
3. Add error messages, logs, code snippets
4. Paste into Copilot Chat

**I will:**
1. Analyze the error
2. Check against Migration/reference/integration_notes.txt
3. Suggest specific fixes
4. Provide code changes if needed

**Example Usage:**

```
[You paste Debug Prompt: Shader Compilation Fails with your specific error]

[I analyze and respond]:
"The error indicates entry point 'main' not found. Your shader has:

[shader("raygen")]
void RayGenMain() { ... }

But you're calling:
compileHLSLToDXIL(source, "main", ...)
                           ^^^^
Should be "RayGenMain" to match.

Fix: [provide code change]"
```

---

**Remember:** Include as much context as possible - error messages, code snippets, recent changes, build configuration. The more info, the better I can help!
