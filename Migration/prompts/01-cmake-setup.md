# CMake Setup Prompts - Phase 1, Part 1

## Overview

These prompts guide you through setting up the CMake build system to support Slang shader compilation.

**Phase:** 1 (Pass-Through Compilation)  
**Part:** 1 (CMake Infrastructure)  
**Duration:** 2-4 hours  
**Reference:** `Migration/INTEGRATION_GUIDE.md` Part 1

---

## Prompt 1: Add ENABLE_SLANG Option to Root CMakeLists.txt

### Context Setup
**Files to open:**
1. `CMakeLists.txt` (root)
2. `Migration/reference/root_CMakeLists.txt` (reference)
3. `Migration/INTEGRATION_GUIDE.md` (guide, optional)

### Prompt

```
Task: Add ENABLE_SLANG CMake option and create slang_compiler_util library

Context:
- Phase 1, Step 1 of Migration/INTEGRATION_GUIDE.md
- Adding optional Slang support to build system
- Should default to OFF (not breaking existing builds)
- Need to create utility library when enabled

Current file: CMakeLists.txt (root)

Requirements:
1. Add option(ENABLE_SLANG "Use Slang for shader compilation" OFF)
   - Place near other options (after find_package calls)
   - Default to OFF for safety

2. When ENABLE_SLANG is ON:
   a. Check for SLANG_PATH environment variable, set Slang_ROOT if found
   b. find_package(Slang REQUIRED)
   c. Create slang_compiler_util STATIC library with:
      - util/slang_shader_compiler.h
      - util/slang_shader_compiler.cpp
   d. Link to Slang::Slang (PUBLIC)
   e. Include directories: util/ and ${Slang_INCLUDE_DIRS} (PUBLIC)
   f. Compile definition: CHAMELEONRT_USE_SLANG=1 (PUBLIC)

3. Platform-specific DLL deployment (Windows):
   - Copy slang.dll to executable directory via POST_BUILD command
   - Use ${Slang_ROOT}/bin/slang.dll as source
   - Target directory: $<TARGET_FILE_DIR:chameleonrt>

Pattern reference: Migration/reference/root_CMakeLists.txt

Please show:
- Exact location to add code (use replace_string_in_file with context)
- Complete if(ENABLE_SLANG) block
- DLL deployment command for Windows
```

### Expected Output

- CMake code added to root CMakeLists.txt
- `slang_compiler_util` library created conditionally
- DLL deployment configured for Windows
- Builds with and without ENABLE_SLANG

### Validation Checklist

After applying changes:

- [ ] **Configure without Slang:** `cmake -B build_test_no_slang`
  - Should succeed (ENABLE_SLANG=OFF by default)
  - No errors about Slang
  
- [ ] **Configure with Slang:** `cmake -B build_test -DENABLE_SLANG=ON -DSlang_ROOT=C:\dev\slang\build\Debug`
  - Should find Slang package
  - Should create slang_compiler_util target
  - Output: "-- Found Slang: ..."
  
- [ ] **Check target exists:** `cmake --build build_test --target slang_compiler_util --config Debug`
  - Should attempt to build (will fail if util files not copied yet, that's OK)

- [ ] **Verify option:** `cmake -B build_test -L | grep SLANG`
  - Should show ENABLE_SLANG option

---

## Prompt 2: Update backends/CMakeLists.txt for Per-Backend Slang Options

### Context Setup
**Files to open:**
1. `backends/CMakeLists.txt`
2. `Migration/reference/backends_CMakeLists.txt` (reference)

### Prompt

```
Task: Add conditional Slang options for individual backends

Context:
- Phase 1, Step 1.2 of Migration/INTEGRATION_GUIDE.md
- Need per-backend control (e.g., ENABLE_DXR_SLANG)
- Should only be available when parent options enabled
- Using CMakeDependentOption for cleaner logic

Current file: backends/CMakeLists.txt

Requirements:
1. Add include(CMakeDependentOption) at top

2. Create dependent options (before add_subdirectory calls):
   cmake_dependent_option(ENABLE_DXR_SLANG 
       "Build DXR backend with Slang shader compilation" 
       OFF 
       "ENABLE_SLANG;ENABLE_DXR" 
       OFF
   )
   
   cmake_dependent_option(ENABLE_VULKAN_SLANG 
       "Build Vulkan backend with Slang shader compilation" 
       OFF 
       "ENABLE_SLANG;ENABLE_VULKAN" 
       OFF
   )

3. Update add_subdirectory conditions:
   - DXR: if(ENABLE_DXR OR ENABLE_DXR_SLANG)
   - Vulkan: if(ENABLE_VULKAN OR ENABLE_VULKAN_SLANG)
   - Leave other backends unchanged

Pattern reference: Migration/reference/backends_CMakeLists.txt

Please show:
- Where to add include()
- Where to add dependent options
- How to update subdirectory conditions
- Use replace_string_in_file with sufficient context
```

### Expected Output

- `CMakeDependentOption` module included
- Per-backend Slang options defined
- Subdirectory conditions updated
- Options cascade correctly

### Validation Checklist

- [ ] **Configure shows options:** `cmake -B build_test -DENABLE_SLANG=ON -DENABLE_DXR=ON -L`
  - Should show ENABLE_DXR_SLANG option
  
- [ ] **Dependent logic works:**
  - `cmake -B build_test -DENABLE_DXR=ON` (no ENABLE_SLANG)
    - ENABLE_DXR_SLANG should be OFF and hidden
  - `cmake -B build_test -DENABLE_SLANG=ON -DENABLE_DXR=ON`
    - ENABLE_DXR_SLANG should be available (but OFF by default)

- [ ] **Can enable per-backend:** `cmake -B build_test -DENABLE_SLANG=ON -DENABLE_DXR=ON -DENABLE_DXR_SLANG=ON`
  - Should configure successfully

---

## Prompt 3: Copy Slang Utility Files to util/ Directory

### Context Setup
**Files to open:**
1. `Migration/util/slang_shader_compiler.h`
2. `Migration/util/slang_shader_compiler.cpp`
3. File explorer showing `util/` directory

### Prompt

```
Task: Copy SlangShaderCompiler utility class from Migration/ to util/

Context:
- Phase 1, Step 1.3 of Migration/INTEGRATION_GUIDE.md
- These files are production code, ready to use
- Need them in util/ to build slang_compiler_util library

Source files:
- Migration/util/slang_shader_compiler.h
- Migration/util/slang_shader_compiler.cpp

Destination:
- util/slang_shader_compiler.h
- util/slang_shader_compiler.cpp

Please:
1. Copy both files from Migration/util/ to util/
2. Verify #include paths are correct (should be relative, like #include "slang_shader_compiler.h")
3. Check namespace is correct (namespace chameleonrt)

Use appropriate tool to copy files (create_file with content from source).
```

### Expected Output

- `util/slang_shader_compiler.h` created
- `util/slang_shader_compiler.cpp` created
- Files identical to Migration/ versions
- Ready to compile

### Validation Checklist

- [ ] **Files exist:** Check `util/` directory contains both files

- [ ] **Build utility library:** `cmake --build build_test --target slang_compiler_util --config Debug`
  - Should compile successfully
  - May have linking warnings (Slang DLL not deployed yet), that's OK

- [ ] **Check for errors:** Build output should show no C++ compilation errors

- [ ] **Verify symbols:** On Windows: `dumpbin /exports build_test\Debug\slang_compiler_util.lib`
  - Should show SlangShaderCompiler symbols

---

## Prompt 4: Test Complete CMake Setup

### Context Setup
**Files to open:**
1. Terminal/PowerShell
2. `Migration/reference/clean_rebuild.ps1` (optional reference)

### Prompt

```
Task: Validate complete CMake setup with clean build

Context:
- Phase 1, Step 1 complete
- All CMake changes applied, utility files copied
- Need to verify full build succeeds

Test sequence:
1. Clean build with Slang enabled
2. Verify slang_compiler_util builds
3. Verify slang.dll deploys
4. Verify main executable links

Please guide me through testing:

1. Command to clean and configure:
   cmake -B build_clean -DENABLE_SLANG=ON -DENABLE_DXR=ON -DENABLE_DXR_SLANG=ON -DSlang_ROOT=C:\dev\slang\build\Debug

2. Command to build:
   cmake --build build_clean --config Debug

3. What to check in output:
   - "Found Slang" message
   - slang_compiler_util compiles
   - chameleonrt links (may have other errors, that's OK)
   - slang.dll copied to build_clean/Debug/

4. How to verify DLL deployment:
   - Check build_clean/Debug/ contains slang.dll
   - Size should match source Slang_ROOT/bin/slang.dll

If build succeeds (even with other errors unrelated to Slang), CMake setup is complete!

What issues should I watch for?
```

### Expected Output

- Build commands
- Success criteria
- Common issues to watch for
- Next steps if successful

### Validation Checklist

- [ ] **CMake configures:** No "Slang not found" errors

- [ ] **Slang_compiler_util builds:** Library created in build_clean/Debug/

- [ ] **DLL deployed:** `build_clean/Debug/slang.dll` exists

- [ ] **Executable links:** chameleonrt.exe created (even if it has runtime issues, linking is what matters)

- [ ] **Can disable:** `cmake -B build_no_slang` still works (ENABLE_SLANG=OFF)

- [ ] **Ready for backend integration:** Phase 1, Part 1 complete ✓

---

## Common Issues & Solutions

### Issue: "Could not find Slang"

**Symptom:** CMake error during configuration

**Solutions:**
```
1. Set environment variable:
   $env:SLANG_PATH = "C:\dev\slang\build\Debug"
   
2. Or pass to CMake:
   cmake -B build -DSlang_ROOT=C:\dev\slang\build\Debug
   
3. Check FindSlang.cmake is in cmake/ directory
```

### Issue: "slang_shader_compiler.cpp: No such file"

**Symptom:** Build error, can't find source files

**Solution:**
- Verify files copied to util/ directory
- Check CMakeLists.txt references correct paths:
  - util/slang_shader_compiler.h
  - util/slang_shader_compiler.cpp

### Issue: "Missing slang.dll at runtime"

**Symptom:** Executable fails to launch

**Solution:**
- Check POST_BUILD command in CMakeLists.txt
- Manually copy: `Copy-Item "$env:SLANG_PATH\bin\slang.dll" "build\Debug\"`
- Verify Slang_ROOT path is correct

### Issue: "slang_compiler_util target does not exist"

**Symptom:** Can't build target

**Solution:**
- Check ENABLE_SLANG=ON was set during configure
- Re-run cmake configuration
- Check if(ENABLE_SLANG) block is correct in CMakeLists.txt

---

## Next Steps

After CMake setup complete:

✅ **Phase 1, Part 1 done** - Build system ready

**Next:** Phase 1, Part 2 - DXR Backend Integration
- Open: `Migration/prompts/02-dxr-integration.md`
- Start with Prompt 1: Add SlangShaderCompiler to DXR backend class

---

**Estimated time for CMake setup:** 2-4 hours (including troubleshooting)

**Success indicator:** `cmake --build build_clean --target slang_compiler_util` succeeds
