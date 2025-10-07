# Using Copilot Chat for Slang Integration

## Overview

This guide explains how to use VS Code Copilot Chat with Claude Sonnet 4.5 to successfully complete the Slang integration migration for ChameleonRT.

**Goal:** Maximize productivity by leveraging persistent context + detailed prompts.

**Your setup:**
- **Copilot Chat** with **Claude Sonnet 4.5** backend (in VS Code)
- **Persistent instructions:** `.github/copilot-instructions.md` (loaded every session)
- **Task-specific context:** `.github/copilot-instructions-slang-integration.md` (referenced when needed)
- **Prompt templates:** `Migration/prompts/*.md` (copy-paste ready)

---

## How It Works

### Persistent Context (Always Loaded)

**File:** `.github/copilot-instructions.md`

**Contains:**
- Project overview (ChameleonRT purpose, architecture)
- Tech stack (C++17, CMake, DXR, Vulkan, etc.)
- **Critical constraints:** "No abstraction layers", "Direct APIs only"
- Coding conventions
- File structure

**How it helps:**
- I already know your project architecture
- I know what NOT to suggest (slang-gfx, abstractions)
- I understand your learning goals
- I know your coding style

**You don't need to:**
- Explain project every time
- Remind me about constraints
- Describe architecture

**Example:**

```
You: "How do I add Slang to the DXR backend?"

Me: "I know you're using direct D3D12 API (no slang-gfx).
     Here's how to integrate SlangShaderCompiler..."
     [Provides code using direct API]
```

I automatically apply project constraints without you asking.

---

### Task-Specific Context (Reference When Needed)

**File:** `.github/copilot-instructions-slang-integration.md`

**Contains:**
- SlangShaderCompiler API reference
- Migration phases (1-4)
- Dual compilation patterns
- CMake options
- Common patterns
- What NOT to do (anti-patterns)
- Decision gates

**How it helps:**
- Detailed reference for current work
- API signatures ready
- Pattern examples
- Troubleshooting tips

**When to reference:**
- When asking Slang-specific questions
- When you need API details
- When troubleshooting compilation

**How to reference:**

```
You: "Check .github/copilot-instructions-slang-integration.md
     for SlangShaderCompiler API. How do I compile HLSL to DXIL?"

Me: [Uses API reference from file]
    "Use compileHLSLToDXIL(source, entryPoint, stage):
    
    auto result = slangCompiler.compileHLSLToDXIL(
        shaderSource,
        "RayGenMain",
        ShaderStage::RAYGEN
    );
    
    if (!result) {
        std::cerr << slangCompiler.getLastError() << std::endl;
        return false;
    }
    
    bytecode = result->bytecode;
    ..."
```

**Pro tip:** You don't always need to reference explicitly - I can access this file when relevant. But if you want to ensure I use specific info, mention it.

---

### Prompt Templates (Your Workflow Guides)

**Location:** `Migration/prompts/*.md`

**Files:**
1. `01-cmake-setup.md` - CMake build system integration
2. `02-dxr-integration.md` - DXR backend integration
3. `03-vulkan-integration.md` - Vulkan backend integration
4. `04-first-slang-shader.md` - First native .slang shader
5. `05-debugging-common.md` - Debugging common errors
6. `06-validation-review.md` - Code quality validation

**Structure of each template:**
- **Prompt X:** Specific task (e.g., "Add ENABLE_SLANG option")
- **Context:** What to have open, what you've done
- **Prompt text:** Copy-paste ready prompt
- **Expected output:** What I'll provide
- **Validation:** How to verify success

**How to use:**

1. **Open the relevant prompt file** (e.g., `01-cmake-setup.md`)
2. **Find the prompt** you need (e.g., "Prompt 1: Add ENABLE_SLANG Option")
3. **Read the context** (which files to open, what to check)
4. **Copy the prompt text**
5. **Fill in any placeholders** (if needed)
6. **Paste into Copilot Chat**
7. **I respond** with code/instructions
8. **Validate** using the checklist
9. **Move to next prompt**

---

## Workflow Examples

### Example 1: Starting Phase 1 (CMake Setup)

**Your steps:**

1. Open `Migration/prompts/01-cmake-setup.md`
2. Read "Prompt 1: Add ENABLE_SLANG Option"
3. Open `CMakeLists.txt` (root) in editor
4. Copy prompt text from template:

```
I'm starting Phase 1 of Slang integration for ChameleonRT.

Task: Add ENABLE_SLANG CMake option to root CMakeLists.txt

Current file: CMakeLists.txt (open in editor)

Please help me:
1. Find the right location (probably with other ENABLE_* options)
2. Add ENABLE_SLANG option (default OFF for safety)
3. Add conditional find_package(Slang REQUIRED) when enabled
4. Show me the complete code to add

Requirements:
- Option should appear in cmake-gui/ccmake
- find_package only when ENABLE_SLANG=ON
- Use Slang_ROOT if set (e.g., C:\dev\slang\build\Debug)
- Add informative message if Slang not found

Reference: Migration/INTEGRATION_GUIDE.md Part 1
```

5. Paste into Copilot Chat
6. I respond with code to add
7. You apply the changes
8. Validate using checklist from template:
   ```
   [ ] Option visible in cmake-gui
   [ ] Configure works with OFF
   [ ] Configure works with ON (finds Slang)
   ```

9. Move to Prompt 2 (Update backends/CMakeLists.txt)

---

### Example 2: Debugging a Compilation Error

**Scenario:** Shader compilation failing

**Your steps:**

1. Open `Migration/prompts/05-debugging-common.md`
2. Find "Debug Prompt: Shader Compilation Fails"
3. Copy template and fill in your details:

```
Problem: Shader compilation failing through Slang

Failed shader: raygen.hlsl
Entry point: RayGenMain
Source language: HLSL
Target: DXIL

Error from getLastError():
"Entry point 'main' not found in module"

Shader source (entry point):
[shader("raygeneration")]
void RayGenMain()
{
    // ... shader code
}

Compilation code:
auto result = slangCompiler.compileHLSLToDXIL(
    raygenSource,
    "main",  // <-- I think the issue is here
    ShaderStage::RAYGEN
);

Please help me:
1. Interpret Slang error message
2. Identify syntax issues or mismatches
3. Fix the shader or compilation call
```

4. Paste into Copilot Chat
5. I analyze: "Entry point name mismatch. You're asking for 'main' but shader defines 'RayGenMain'. Change line X to use 'RayGenMain'."
6. You fix the code
7. Test again

---

### Example 3: Validation Before Committing

**Your steps:**

1. Open `Migration/prompts/06-validation-review.md`
2. Find "Validation Prompt: Ready to Commit?"
3. Fill in checklist based on your testing
4. Copy and paste with your status:

```
Task: Pre-commit validation of my changes

Changes summary:
Added SlangShaderCompiler to DXR backend, compiling raygen and miss shaders

Branch: feature/slang-integration
Affected backends: DXR

Pre-commit checklist:

Build:
[✓] Builds with ENABLE_SLANG=OFF (no regressions)
[✓] Builds with ENABLE_SLANG=ON (new path works)
[✓] No compiler warnings introduced
[✓] All backends build (even non-Slang ones)

Functionality:
[✓] Application runs with Slang OFF (regression test)
[✓] Application runs with Slang ON (new feature)
[?] Rendering output correct - black screen, investigating
[ ] No crashes or errors in console - see warning below

Issues:
- Warning: "D3D12 WARNING: DXIL validation failed: ..."
- Rendering: black screen with Slang, correct without

Code Quality:
[✓] Code follows project conventions
[✓] Error handling complete
[✓] No debug code left in

Should I commit now or fix the DXIL warning first?
```

5. I review: "DXIL validation failure is critical. Don't commit yet. Let's debug..." [provides debugging steps]

---

## Advanced Usage

### Combining References

You can reference multiple sources:

```
You: "Check Migration/INTEGRATION_GUIDE.md Part 2 for DXR integration steps,
     and .github/copilot-instructions-slang-integration.md for 
     SlangShaderCompiler API.
     
     I need to replace the raygen shader compilation in render_dxr.cpp."

Me: [Uses both references to provide complete solution]
```

---

### Iterating on Prompts

Templates are starting points. You can modify:

```
Template says:
"Task: Add ENABLE_SLANG option"

You modify:
"Task: Add ENABLE_SLANG option
NOTE: I already have ENABLE_DXR and ENABLE_VULKAN options at line 23-25.
Should I add ENABLE_SLANG near those?"

Me: "Yes, add it right after ENABLE_VULKAN for consistency:
     [provides code with line numbers]"
```

---

### Asking Follow-Up Questions

After I provide code, you can ask follow-ups:

```
You: [paste Prompt 1 from 01-cmake-setup.md]

Me: [provides CMake code to add]

You: "Why did you use 'REQUIRED' in find_package? 
     What happens if user doesn't have Slang?"

Me: "Good question. REQUIRED means CMake errors if not found.
     Since find_package is inside if(ENABLE_SLANG), user won't
     hit this unless they explicitly enable it. If you want
     more graceful handling, use:
     
     find_package(Slang)
     if(NOT Slang_FOUND)
         message(WARNING ...)
         set(ENABLE_SLANG OFF)
     endif()
     
     Trade-off: ..."
```

---

### Requesting Code Review

You can paste code and ask for review:

```
You: "I added SlangShaderCompiler to render_dxr.h. 
     Can you review for issues?
     
     [paste code]
     
     Check against coding conventions in 
     .github/copilot-instructions.md"

Me: [Reviews code]
    "Issues found:
    1. Missing #ifdef USE_SLANG_COMPILER guard
    2. Member should be initialized in constructor
    3. Consider adding error check in initialization
    
    Suggested fixes: ..."
```

---

## Best Practices

### DO:

✅ **Use prompt templates** - They're optimized for this workflow  
✅ **Fill in placeholders** - Provide specific details (file names, line numbers, errors)  
✅ **Validate each step** - Use checklists before moving on  
✅ **Ask follow-ups** - If unclear, ask me to explain  
✅ **Reference docs** - Mention specific files when relevant  
✅ **Test both paths** - Always verify ENABLE_SLANG=ON and OFF  
✅ **Commit incrementally** - Don't save up weeks of changes  

### DON'T:

❌ **Skip validation** - Catch issues early  
❌ **Assume I know latest changes** - Tell me what you just did  
❌ **Mix multiple phases** - Complete Phase 1 before Phase 2  
❌ **Ignore warnings** - Even if it "works"  
❌ **Copy code blindly** - Understand what I provide  
❌ **Rush decision gates** - Take time to evaluate  

---

## Tips for Success

### Provide Context

**Less helpful:**
```
You: "It doesn't compile"
```

**More helpful:**
```
You: "Phase 1, Prompt 2 (backends/CMakeLists.txt).
     Added ENABLE_DXR_SLANG option, but CMake configure fails:
     [paste error]"
```

I can help much better with context.

---

### Use the Migration Docs

The `Migration/` directory is your knowledge base:

- **README.md** - Overview, quick start
- **LEARNINGS.md** - Why we pivoted (motivation)
- **MIGRATION_PLAN.md** - Strategic roadmap, decision gates
- **INTEGRATION_GUIDE.md** - Technical steps
- **prompts/*.md** - Workflow guides (this is where you are most of the time)
- **reference/*.md** - Working code examples

When stuck, check these first. Then ask me.

---

### Iterate on Errors

If first solution doesn't work:

```
You: [paste Debug Prompt: Shader Compilation Fails with error]

Me: [suggests fix A]

You: "Tried fix A, now different error: [new error message]"

Me: [suggests fix B, more targeted]
```

Don't give up after first try. Debugging is iterative.

---

### Decision Gates Are Important

At Weeks 2, 3, 6 - STOP and validate:

1. Use validation prompt (06-validation-review.md)
2. Honestly assess progress
3. Ask me: "Should I proceed or iterate?"
4. Don't rush forward if foundation is shaky

**It's okay to iterate!** Better to spend extra time on Phase 1 than struggle in Phase 3.

---

## Workflow Summary

**Daily workflow:**

1. **Open prompt file** for current phase (e.g., `02-dxr-integration.md`)
2. **Read prompt context** - what to have open
3. **Copy prompt text**
4. **Paste into Copilot Chat**
5. **I provide code/instructions**
6. **You implement**
7. **Validate using checklist**
8. **Commit if step complete**
9. **Move to next prompt**

**When stuck:**

1. **Check** `Migration/INTEGRATION_GUIDE.md` for troubleshooting
2. **Use debug prompt** from `05-debugging-common.md`
3. **Provide error details**
4. **I help diagnose**
5. **Iterate until fixed**

**Before committing:**

1. **Use validation prompt** from `06-validation-review.md`
2. **Fill in checklist honestly**
3. **I review**
4. **Fix any issues I find**
5. **Commit with descriptive message**

**At decision gates (Weeks 2, 3, 6):**

1. **Use decision gate prompt** from `06-validation-review.md`
2. **Assess progress honestly**
3. **Ask me: proceed, iterate, or reconsider?**
4. **Make informed decision**

---

## Troubleshooting the Workflow

### "I'm not sure which prompt to use"

Check `Migration/prompts/README.md` - it has an overview.

Or ask me:
```
You: "I've finished CMake setup and want to integrate Slang into DXR backend.
     Which prompt file should I use?"

Me: "Use Migration/prompts/02-dxr-integration.md, starting with Prompt 1."
```

---

### "The prompt doesn't fit my situation exactly"

Modify it!

```
Template says: "Task: Add SlangShaderCompiler to render_dxr.h"

Your situation: "I already added it but compilation fails"

Modified prompt:
"I tried to add SlangShaderCompiler to render_dxr.h (from Prompt 2)
but get compilation error: [paste error]
Here's what I added: [paste code]
Please help me fix it."
```

---

### "I want to try something not in the templates"

Ask me!

```
You: "Migration plan says Phase 2 is 'first Slang shader'.
     I want to try converting the miss shader first instead of raygen.
     Is that okay, or will it cause issues?"

Me: [Analyzes]
    "Miss shader is simpler, so good choice for first attempt.
    Go ahead, but note that raygen is more commonly used example.
    Here's how to approach miss shader: ..."
```

Templates are guides, not rigid rules.

---

### "I'm overwhelmed by the amount of documentation"

Start small:

**Phase 1 Part 1 (CMake):**
- Use: `01-cmake-setup.md` only
- Reference: `INTEGRATION_GUIDE.md` Part 1 if stuck

**Phase 1 Part 2 (DXR):**
- Use: `02-dxr-integration.md` only
- Reference: `INTEGRATION_GUIDE.md` Part 2 if stuck

Don't try to read everything upfront. Use docs just-in-time.

---

## What I Bring to the Table

**I automatically:**
- Apply project constraints (no abstractions, direct APIs)
- Follow coding conventions (from copilot-instructions.md)
- Reference migration docs when relevant
- Provide complete code (not "...existing code..." placeholders)
- Check against best practices
- Warn about common pitfalls

**You can trust me to:**
- Understand ChameleonRT architecture
- Know Slang integration approach (shader compilation only)
- Provide buildable code
- Spot issues in your code
- Guide you through decision gates

**But you need to:**
- Provide specific error messages
- Tell me what phase/prompt you're on
- Validate my suggestions (test before committing)
- Ask follow-ups if unclear
- Make final decisions at decision gates

---

## Example Session (End-to-End)

**Week 1, Day 1:**

```
You: [Opens Migration/prompts/01-cmake-setup.md]
     [Copies Prompt 1]
     [Pastes into Copilot Chat]

Me: [Provides CMake code for ENABLE_SLANG option]

You: [Applies code]
     [Tests: cmake -B build -DENABLE_SLANG=ON]
     ✓ Works!
     
     [Copies Prompt 2: Update backends/CMakeLists.txt]
     [Pastes into Copilot Chat]

Me: [Provides backend CMake changes]

You: [Applies code]
     [Tests: cmake -B build -DENABLE_SLANG=ON -DENABLE_DXR_SLANG=ON]
     ✓ Works!
     
     [Continues through Prompt 3, 4]
     
     [Uses Validation Prompt: CMake Configuration Complete]
     "Task: Review my CMake setup for Slang integration
      [fills in checklist]"

Me: [Reviews checklist]
    "✓ All checks pass. Proceed to Part 2 (DXR integration)."

You: [Commits: "feat: add Slang CMake integration"]
     [Opens 02-dxr-integration.md]
```

**Week 1, Day 3:**

```
You: [Working on Prompt 5: Replace raygen shader compilation]
     [Shader compilation fails]
     
     [Opens 05-debugging-common.md]
     [Finds Debug Prompt: Shader Compilation Fails]
     [Fills in error details]
     [Pastes into Copilot Chat]

Me: "Entry point mismatch. Your entry point is 'RayGenMain' 
     but you're passing 'main'. Change line 234 to:
     
     auto result = slangCompiler.compileHLSLToDXIL(
         raygenSource,
         \"RayGenMain\",  // <-- was \"main\"
         ShaderStage::RAYGEN
     );"

You: [Applies fix]
     [Tests again]
     ✓ Compiles!
     
     [Continues through remaining prompts]
```

**Week 2, End:**

```
You: [DXR and Vulkan both complete]
     [Opens 06-validation-review.md]
     [Uses Validation Prompt: Week 2 Decision Gate]
     [Fills in status: both backends working]

Me: "✓ DXR working
     ✓ Vulkan working
     ✓ Dual compilation tested
     ✓ Rendering correct on both
     
     PROCEED to Phase 2 (first native Slang shader).
     Excellent foundation!"

You: [Commits Phase 1 work]
     [Opens 04-first-slang-shader.md]
     [Starts Phase 2]
```

---

## Summary

**You have:**
- Persistent project context (I always know ChameleonRT)
- Task-specific reference (SlangShaderCompiler API, patterns)
- Step-by-step prompts (copy-paste ready)
- Debugging prompts (for when things break)
- Validation prompts (for quality assurance)

**Workflow:**
1. Open prompt file for current phase
2. Copy-paste prompts (modify as needed)
3. I provide code/guidance
4. You implement and validate
5. Move to next step

**Keys to success:**
- Use the prompts (they're optimized)
- Provide context (errors, file names, what you tried)
- Validate each step (don't skip)
- Commit incrementally (don't save up weeks)
- Respect decision gates (iterate if needed)

**You're not alone:** I'm here every step of the way. Ask questions, request reviews, debug together. Let's build this!

---

Ready to start? Open `Migration/prompts/01-cmake-setup.md` and let's go! 🚀
