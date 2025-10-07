# Validation & Code Review Prompts

## Overview

Use these prompts to validate your work at each integration phase.

**Use these when:** You've completed a set of changes and want to ensure quality before moving on.

**Pattern:** Paste the relevant validation prompt for your current phase.

---

## Phase 1: CMake Setup Validation

### Validation Prompt: CMake Configuration Complete

```
Task: Review my CMake setup for Slang integration

I've completed Phase 1 Part 1 (CMake Setup) and want to validate before moving to backend integration.

Files modified:
1. CMakeLists.txt (root)
2. backends/CMakeLists.txt
3. util/CMakeLists.txt (if exists, or created)

Please review:

1. Root CMakeLists.txt:
   - ENABLE_SLANG option added?
   - Conditional find_package(Slang)?
   - Correct placement (before add_subdirectory)?

2. backends/CMakeLists.txt:
   - Per-backend ENABLE_XXX_SLANG options?
   - Conditional add_definitions(-DUSE_SLANG_COMPILER)?
   - Options properly gated by ENABLE_SLANG?

3. util/ setup:
   - slang_shader_compiler.h/cpp present?
   - slang_compiler_util library target created?
   - Conditional on ENABLE_SLANG?
   - Exports include directory?

4. Build test:
   - Configures with ENABLE_SLANG=OFF: [yes/no]
   - Configures with ENABLE_SLANG=ON: [yes/no]
   - Builds successfully: [yes/no]

Validation checklist:
[ ] Options appear in cmake-gui/ccmake
[ ] Slang found (or graceful failure if not)
[ ] Build succeeds in both modes
[ ] No changes to existing backend code yet
[ ] Utility library compiles

Reference: Migration/INTEGRATION_GUIDE.md Part 1

Should I proceed to Part 2 (backend integration)?
```

**Expected response:** Checklist review, identification of any issues, green light to proceed.

---

## Phase 1: DXR Integration Validation

### Validation Prompt: DXR Backend Integration Complete

```
Task: Review my DXR backend Slang integration

I've completed Phase 1 Part 2 (DXR Integration) and want to validate before testing.

Changes made:
- Backend: backends/dxr/
- Files modified: [list files]
- Shaders converted: [list shaders]

Please review:

1. Header changes (render_dxr.h):
   - #include "slang_shader_compiler.h" guarded by USE_SLANG_COMPILER?
   - Member: SlangShaderCompiler slangCompiler?
   - New members only in #ifdef block?

2. CMakeLists.txt (backends/dxr/):
   - ENABLE_DXR_SLANG option exists?
   - Conditional add_definitions(-DUSE_SLANG_COMPILER)?
   - Conditional target_link_libraries(... slang_compiler_util)?
   - POST_BUILD DLL deployment (Windows)?

3. Initialization (render_dxr.cpp constructor/init):
   - SlangShaderCompiler initialized in #ifdef?
   - Error check: if (!slangCompiler.isValid())?
   - Error message helpful?

4. Shader compilation replacements:
   - Raygen shader: using compileHLSLToDXIL()?
   - Miss shader: using compileHLSLToDXIL()?
   - Closest hit shader: using compileHLSLToDXIL()?
   - All entry point names correct?
   - Shader stage enums correct (RAYGEN, MISS, CLOSEST_HIT)?
   - Bytecode extraction: result->bytecode?

5. Dual compilation:
   - Non-Slang path (#else) still intact?
   - Both paths return same type?
   - No code duplication outside #ifdef?

6. Error handling:
   - Check if result has value?
   - Log getLastError() on failure?
   - Fail gracefully (don't crash)?

7. Build test:
   - ENABLE_DXR_SLANG=OFF builds: [yes/no]
   - ENABLE_DXR_SLANG=ON builds: [yes/no]
   - DLL deployed to build directory: [yes/no]
   - Runs without crash: [yes/no]

Validation checklist:
[ ] Compiles with Slang ON and OFF
[ ] No compilation warnings (Slang-related)
[ ] DLL dependencies resolved
[ ] Initialization succeeds (isValid() true)
[ ] Renders something (even if incorrect)
[ ] Non-Slang path still works

Reference: Migration/INTEGRATION_GUIDE.md Part 2

Issues observed (if any):
[Describe any rendering differences, warnings, errors]

Should I proceed to Vulkan integration?
```

**Expected response:** Code review feedback, identification of issues, recommendations before proceeding.

---

## Phase 1: Vulkan Integration Validation

### Validation Prompt: Vulkan Backend Integration Complete

```
Task: Review my Vulkan backend Slang integration

I've completed Phase 1 Part 3 (Vulkan Integration) and want to validate before Phase 2.

Changes made:
- Backend: backends/vulkan/
- Files modified: [list files]
- Shaders converted: [list shaders]

Please review:

1. Header changes (render_vulkan.h):
   - #include "slang_shader_compiler.h" guarded?
   - Member: SlangShaderCompiler slangCompiler?

2. CMakeLists.txt (backends/vulkan/):
   - ENABLE_VULKAN_SLANG option?
   - Conditional compilation definitions?
   - Link to slang_compiler_util?

3. Shader compilation replacements:
   - Using compileGLSLToSPIRV()?
   - Shader stages correct (VERTEX, FRAGMENT, etc.)?
   - Entry points: typically "main" for GLSL?
   - Bytecode conversion: uint8_t → uint32_t?
   - Size calculation: bytecode.size() / 4?

4. SPIRV handling:
   - vkCreateShaderModule gets uint32_t*?
   - codeSize is bytecode size, not size/4?
   - SPIRV validation (spirv-val) passes?

5. Error handling:
   - Check result has value?
   - Log errors appropriately?

6. Build test:
   - ENABLE_VULKAN_SLANG=OFF: [yes/no]
   - ENABLE_VULKAN_SLANG=ON: [yes/no]
   - Vulkan validation layers happy: [yes/no]
   - Renders correctly: [yes/no]

Validation checklist:
[ ] Compiles with Slang ON and OFF
[ ] SPIRV validation passes (spirv-val)
[ ] No Vulkan validation errors
[ ] Rendering matches non-Slang
[ ] RenderDoc capture works

Reference: Migration/INTEGRATION_GUIDE.md Part 3

Rendering comparison:
- Non-Slang: [describe/screenshot]
- Slang: [describe/screenshot]
- Differences: [any?]

Should I proceed to Phase 2 (first Slang shader)?
```

**Expected response:** Verification that Vulkan integration is solid, rendering matches non-Slang path.

---

## Phase 2: First Slang Shader Validation

### Validation Prompt: Native Slang Shader Working

```
Task: Review my first native .slang shader

I've completed Phase 2 (First Slang Shader) and want to validate the cross-compilation approach.

Shader details:
- File: [path to .slang file]
- Type: [vertex/fragment, raygen/miss/etc.]
- Source lines: [approximate]

Compilation targets:
1. DXR (D3D12): compileSlangToDXIL()
2. Vulkan: compileSlangToSPIRV()

Please review:

1. Shader source (.slang):
   - Slang syntax valid?
   - [shader("stage")] attribute correct?
   - Uniform/varying declarations correct?
   - Entry point clearly defined?
   - Cross-platform (no D3D12/Vulkan-specific)?

2. D3D12 compilation:
   - compileSlangToDXIL() succeeds?
   - Bytecode size reasonable?
   - DXIL validation passes?
   - Pipeline creation succeeds?
   - Rendering correct?

3. Vulkan compilation:
   - compileSlangToSPIRV() succeeds?
   - Bytecode size reasonable?
   - SPIRV validation passes?
   - Pipeline creation succeeds?
   - Rendering correct?

4. Cross-platform validation:
   - Same source compiles to both targets?
   - No platform-specific code in shader?
   - Rendering visually identical on D3D12 and Vulkan?

5. Learnings:
   - Slang syntax differences from HLSL/GLSL?
   - Compilation time acceptable?
   - Error messages helpful?
   - Any limitations discovered?

Validation checklist:
[ ] .slang file compiles to DXIL
[ ] .slang file compiles to SPIRV
[ ] D3D12 rendering correct
[ ] Vulkan rendering correct
[ ] Renderings visually match
[ ] No platform-specific workarounds needed

Reference: Migration/INTEGRATION_GUIDE.md Part 4

Visual comparison:
- D3D12 (DXR): [describe/screenshot]
- Vulkan: [describe/screenshot]
- Match: [yes/no]

Blockers for Phase 3 (migrate more shaders):
[Any concerns about scaling this to all shaders?]

Should I proceed to Phase 3?
```

**Expected response:** Confirmation that single-source cross-compilation works, identification of any Slang language issues.

---

## Code Quality Reviews

### Validation Prompt: Code Style & Conventions

```
Task: Review code quality and consistency

Please review my recent changes for:

1. Code style:
   - Consistent with existing ChameleonRT code?
   - Naming conventions match (camelCase, snake_case)?
   - Formatting (braces, indentation)?

2. Error handling:
   - All Slang calls checked for errors?
   - getLastError() called and logged?
   - Graceful failures (no crashes)?
   - Error messages informative?

3. Resource management:
   - RAII patterns followed?
   - No memory leaks?
   - Shader blobs properly managed?

4. Documentation:
   - Comments explain why, not what?
   - Complex sections documented?
   - TODO/FIXME with context?

5. Platform compatibility:
   - #ifdef guards for Slang code?
   - Windows-specific code marked?
   - Linux/macOS considered?

6. Testing approach:
   - Manual testing done?
   - Dual compilation tested?
   - Regression testing (non-Slang)?

Files to review:
[List modified files]

Reference: .github/copilot-instructions.md (Coding Conventions)
```

---

### Validation Prompt: CMake Best Practices

```
Task: Review CMake changes for best practices

Please review my CMakeLists.txt changes:

Files:
[List CMakeLists.txt files modified]

Check for:

1. Option naming:
   - Consistent with existing (ENABLE_XXX)?
   - Documented with description strings?
   - Grouped logically?

2. Conditionals:
   - Proper if(ENABLE_XXX) guards?
   - No unconditional find_package for optional deps?
   - No unconditional linking?

3. Targets:
   - Library targets well-named?
   - Include directories exported?
   - Link dependencies complete?

4. Platform handling:
   - Windows-specific (DLL deployment) conditional?
   - Unix-specific code conditional?
   - Cross-platform paths (use ${CMAKE_BINARY_DIR})?

5. Dependency management:
   - find_package(Slang) correct?
   - Slang::Slang imported target used?
   - Slang_ROOT honored?

6. Build outputs:
   - POST_BUILD commands necessary?
   - File copies to correct locations?
   - Clean builds work?

Reference: Migration/reference/integration_notes.txt (CMake gotchas)
```

---

## Pre-Commit Validation

### Validation Prompt: Ready to Commit?

```
Task: Pre-commit validation of my changes

I'm about to commit and want to ensure quality.

Changes summary:
[Brief description of what changed]

Branch: feature/slang-integration
Affected backends: [DXR/Vulkan/both]

Pre-commit checklist:

Build:
[ ] Builds with ENABLE_SLANG=OFF (no regressions)
[ ] Builds with ENABLE_SLANG=ON (new path works)
[ ] No compiler warnings introduced
[ ] All backends build (even non-Slang ones)

Functionality:
[ ] Application runs with Slang OFF (regression test)
[ ] Application runs with Slang ON (new feature)
[ ] Rendering output correct (or known issues documented)
[ ] No crashes or errors in console

Code Quality:
[ ] Code follows project conventions
[ ] Error handling complete
[ ] No debug code left in (printf, commented-out, etc.)
[ ] Comments explain non-obvious things

Git:
[ ] Commit message descriptive
[ ] Changes logically grouped (not mixing features)
[ ] No accidental files included (.obj, .exe, build artifacts)

Documentation:
[ ] Migration/LEARNINGS.md updated if issues found
[ ] Migration/INTEGRATION_GUIDE.md updated if process changed
[ ] TODO comments added for future work

Please review and confirm I should commit, or identify any issues.
```

---

## Decision Gate Reviews

### Validation Prompt: Week 2 Decision Gate

```
Task: Review progress for Week 2 decision gate

Phase 1 (Pass-through compilation) should be complete.

Status check:

DXR Backend:
- HLSL → DXIL via Slang: [working/not working]
- All shaders compiling: [yes/no]
- Rendering correct: [yes/no]
- Dual compilation (ON/OFF): [both working]

Vulkan Backend:
- GLSL → SPIRV via Slang: [working/not working]
- All shaders compiling: [yes/no]
- Rendering correct: [yes/no]
- Dual compilation (ON/OFF): [both working]

Issues encountered:
[List any problems, workarounds, concerns]

Time investment: [actual vs planned 2 weeks]

Decision gate question: Should I proceed to Phase 2 (Slang language)?

Considerations:
- If pass-through not solid, Phase 2 will be harder
- If major blockers, may need to reconsider approach
- If smooth, green light for Phase 2

Please review and recommend: PROCEED / ITERATE / RECONSIDER

Reference: Migration/MIGRATION_PLAN.md (Decision Gates)
```

---

### Validation Prompt: Week 3 Decision Gate

```
Task: Review progress for Week 3 decision gate

Phase 2 (first native .slang shader) should be complete.

Status check:

First Slang shader:
- Shader: [name and type]
- Compiles to DXIL: [yes/no]
- Compiles to SPIRV: [yes/no]
- D3D12 rendering: [correct/issues]
- Vulkan rendering: [correct/issues]
- Cross-platform: [identical/differences]

Slang language experience:
- Syntax easy to learn: [yes/no/challenging]
- Compilation errors clear: [yes/no]
- Documentation sufficient: [yes/no]
- Confidence for more shaders: [high/medium/low]

Issues encountered:
[List any language barriers, tooling issues]

Time investment: [actual vs planned 1 week]

Decision gate question: Should I proceed to Phase 3 (migrate all shaders)?

Considerations:
- If one shader works, others should follow pattern
- If language issues, may need more learning time
- If tool issues, may need Slang team help

Please review and recommend: PROCEED / ITERATE / RECONSIDER

Reference: Migration/MIGRATION_PLAN.md (Decision Gates)
```

---

## Using These Prompts

**When to validate:**
- After completing each phase
- Before committing changes
- At decision gate milestones
- When uncertain about quality

**How to use:**
1. Copy relevant validation prompt
2. Fill in your specific status/results
3. Paste into Copilot Chat
4. I'll review against best practices

**What I'll check:**
- Completeness (did you finish the phase?)
- Correctness (does it work as intended?)
- Quality (follows conventions, no shortcuts?)
- Readiness (safe to proceed to next phase?)

**Example flow:**

Week 2 (Phase 1 complete):
1. You paste "Week 2 Decision Gate" prompt with your status
2. I review: "DXR ✓, Vulkan ✓, dual compilation ✓"
3. I recommend: "PROCEED to Phase 2 - solid foundation"

---

**Remember:** Validation is not just "does it compile" - it's "is it production-quality and maintainable". Take time to validate properly; it prevents technical debt later!
