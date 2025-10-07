# Prompt Templates for Slang Integration

## How to Use These Templates

This directory contains detailed, copy-paste-ready prompt templates for each phase of the Slang integration.

### Organization

- **`01-cmake-setup.md`** - CMake configuration and build system integration
- **`02-dxr-integration.md`** - DXR (D3D12) backend integration prompts
- **`03-vulkan-integration.md`** - Vulkan backend integration prompts
- **`04-first-slang-shader.md`** - Migrating to native Slang language
- **`05-debugging-common.md`** - Common errors and debugging prompts
- **`06-validation-review.md`** - Code review and validation checklists

### Workflow

**Step 1: Open Context Files**
Each prompt template tells you which files to have open in VS Code. This gives Copilot Chat (me) the right context.

**Step 2: Copy Prompt**
Copy the prompt template that matches your current step. They're numbered sequentially.

**Step 3: Customize (if needed)**
Replace placeholders like `[your file name]` or `[paste code here]` with your specific content.

**Step 4: Paste into Copilot Chat**
Send the prompt to me in this chat interface.

**Step 5: Review & Apply**
I'll generate the code/changes using my tools. Review before testing.

**Step 6: Validate**
Use the validation checklist at the end of each prompt to verify success.

### Prompt Structure

Each prompt template includes:

1. **Context Setup** - Which files to open
2. **Background** - What phase/step of migration you're on
3. **The Prompt** - Detailed, copy-paste ready
4. **Expected Output** - What I should generate
5. **Validation Checklist** - How to verify it worked

### Tips for Best Results

✅ **Be specific:** If prompt says `[paste current code]`, actually paste it  
✅ **Have files open:** I can see what's in your editor  
✅ **One step at a time:** Don't combine multiple prompts  
✅ **Validate before proceeding:** Check each step works before next  
✅ **Reference errors:** If something fails, paste the error message  

### Customizing Prompts

As you learn what works well, feel free to:
- Add more context that helps
- Remove sections that aren't needed
- Create your own prompt variations
- Commit your modifications to track what works

### Progressive Disclosure

**Don't load everything at once.** Focus on the current prompt file:

- **Starting CMake setup?** Open `01-cmake-setup.md`
- **Integrating DXR?** Open `02-dxr-integration.md`
- **Debugging?** Open `05-debugging-common.md`

Keep related reference files open (like `Migration/INTEGRATION_GUIDE.md`) but not all prompts simultaneously.

### When You Get Stuck

1. **Check the debugging prompts:** `05-debugging-common.md`
2. **Reference integration notes:** `Migration/reference/integration_notes.txt`
3. **Ask for help:** "I'm stuck on [step]. What should I try?"

### Example Usage

```
You (in VS Code):
1. Open: CMakeLists.txt
2. Open: Migration/prompts/01-cmake-setup.md
3. Copy: "Prompt 1: Add ENABLE_SLANG Option"
4. Open Copilot Chat
5. Paste the prompt

Me (in chat):
- Read copilot-instructions.md (base knowledge) ✓
- Read copilot-instructions-slang-integration.md (task) ✓
- See CMakeLists.txt is open ✓
- See the specific prompt ✓
- Generate the CMake changes using tools ✓

You:
- Review the changes
- Build: cmake -B build -DENABLE_SLANG=ON
- Validate: Does it configure without errors?
- If yes → Move to Prompt 2
- If no → Use debugging prompt
```

---

## Current Phase

You're likely starting with **Phase 1: CMake Setup** (`01-cmake-setup.md`).

Follow prompts in order:
1. CMake setup (01)
2. DXR integration (02)
3. Vulkan integration (03)
4. Slang language migration (04)

Use 05 and 06 as needed throughout.

---

**Ready to begin? Start with `01-cmake-setup.md`!**
