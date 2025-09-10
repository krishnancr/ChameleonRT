# GitHub Copilot Usage Guide for ImGui Implementation Plans

This guide explains how to effectively use the implementation plans with GitHub Copilot Chat in new sessions.

## File Organization

The implementation plans are organized as follows:

```
c:\dev\ChameleonRT\docs\imgui-implementation-plans\
├── plan-a-unified-slang.md          # Pure Slang GFX implementation
├── plan-b-falcor-approach.md        # Native backends with Slang interop
├── github-copilot-usage-guide.md    # This file
└── README.md                        # Overview and quick reference
```

## Starting a New Session

### 1. Provide Workspace Context
Start every new Copilot session by giving it context about your workspace:

```
@workspace I'm working on implementing ImGui integration for ChameleonRT's Slang backend. 
I have detailed implementation plans in docs/imgui-implementation-plans/. 
I want to implement [Plan A/Plan B] Stage [N].
```

### 2. Reference Specific Plans
When referencing a plan, be specific:

```
I want to implement Stage 3 from plan-a-unified-slang.md. 
Please read that stage and help me implement the buffer management system.
```

or

```
I'm following plan-b-falcor-approach.md Stage 1. 
Help me create the native handle extraction infrastructure.
```

### 3. Share Current Code State
Always provide the current state of files you're modifying:

```
Here's my current slangdisplay.cpp file: [paste code]
I want to add the ImGui renderer integration from Stage 6 of Plan A.
```

## Effective Prompting Strategies

### For Implementation
```
Generate the [specific method/class] from Stage [N] of [plan-a/plan-b].
Make sure to include:
- All error handling shown in the plan
- The exact logging messages
- Proper resource management
- The specified function signatures
```

### For Debugging
```
I'm having trouble with [specific issue] while implementing Stage [N].
The plan shows [expected behavior] but I'm getting [actual behavior].
Here's my current code: [paste code]
```

### For Testing
```
Create a test for the checkpoint "[checkpoint description]" from Stage [N].
The test should verify [specific functionality] and follow the test scenario described in the plan.
```

### For Code Review
```
Review my implementation of Stage [N] against the plan.
Check if I've met all the checkpoints and if the code follows the implementation details.
Here's what I implemented: [paste code]
```

## Working Through Implementation Stages

### Stage-by-Stage Approach
1. **Read the entire stage** before starting implementation
2. **Implement in order** - don't skip ahead as stages build on each other
3. **Verify all checkpoints** before moving to the next stage
4. **Test each stage individually** before integration

### Checkpoint Validation
For each checkpoint, create a specific test:

```
I need to verify this checkpoint from Stage [N]: 
"[checkbox item from plan]"

Create a minimal test that validates this specific requirement.
```

### Error Handling
When encountering errors:

```
I'm getting [specific error] during Stage [N] implementation.
According to the plan, this should [expected behavior].
Help me debug this issue. Here's the relevant code: [paste code]
```

## Code Generation Best Practices

### Request Complete Methods
```
Generate the complete `updateBuffers` method from Plan A Stage 3.
Include all the error checking, logging, and resource management shown in the plan.
```

### Ask for Specific Files
```
Create the complete slang_imgui_renderer.h file from Plan A Stage 1.
Include all the private members, public methods, and includes shown in the implementation details.
```

### Request Integration Code
```
Show me how to integrate the SlangImGuiRenderer into SlangDisplay.
Follow the integration steps from Plan A Stage 6, including the header changes and method implementations.
```

## Testing and Validation

### Create Test Commands
```
Create the build command to compile Stage [N] implementation.
Create a test command to verify the [specific functionality] works.
Add this to the "Test Commands" section of the plan.
```

### Generate Acceptance Criteria
```
Based on Stage [N] implementation, create specific acceptance criteria.
Include performance requirements, functional requirements, and quality gates.
Format it for the "Final Acceptance Criteria" section.
```

### Debug Test Failures
```
My test for Stage [N] is failing with [error].
The test scenario says [expected result] but I get [actual result].
Help me debug this based on the implementation details in the plan.
```

## Advanced Usage Patterns

### Cross-Reference Plans
```
I'm implementing Plan A but encountering [specific issue].
Would Plan B's approach to [same functionality] work better?
Compare the two approaches for [specific stage/component].
```

### Optimization Requests
```
My Stage [N] implementation works but seems slow.
Based on the performance considerations in the plan, suggest optimizations.
Here's my current implementation: [paste code]
```

### Architecture Questions
```
I'm unclear about [specific architectural decision] in Stage [N].
Explain why the plan chose [approach A] over [approach B].
What are the tradeoffs?
```

## Common Copilot Prompts

### Starting Implementation
```
I'm ready to implement Plan [A/B] Stage [N]. 
Please read the stage details and help me create the [specific component].
Start with the header file structure.
```

### Continuing from Previous Session
```
I previously implemented Stage [N-1] of Plan [A/B]. 
Here's my current code: [paste relevant files]
Now I want to implement Stage [N]. What's the next step?
```

### Troubleshooting Build Issues
```
My Stage [N] implementation won't compile. 
I get these errors: [paste errors]
The plan shows [expected setup]. Help me fix the build issues.
```

### Integration Testing
```
I've completed Stage [N]. Before moving to Stage [N+1], 
help me create integration tests based on the checkpoints.
The tests should verify [list checkpoints].
```

## File Management Tips

### Save Progress Frequently
- Commit after each stage completion
- Tag commits with stage numbers (e.g., "Plan-A-Stage-3-Complete")
- Keep plan files updated with your test commands and acceptance criteria

### Document Deviations
If you deviate from the plan:
```
I needed to modify the approach in Stage [N] because [reason].
Instead of [plan approach], I used [modified approach].
Document this change for future reference.
```

### Track Completion
```
Mark Stage [N] as complete in my plan.
Update the checkpoints with ✅ and add my test commands.
Prepare summary for Stage [N+1] kickoff.
```

## Session Continuity

### Ending a Session
```
Summarize what we completed in this session:
- Stages implemented: [list]
- Current status: [describe]
- Next session should start with: [next steps]
Create a handoff note for the next session.
```

### Starting Next Session
```
I'm continuing ImGui implementation from previous session.
Last session completed: [stage/component]
Current issue: [if any]
Next goal: Implement Stage [N] from Plan [A/B].
Here's current code state: [paste relevant files]
```

## Quality Assurance

### Code Review Requests
```
Review my Stage [N] implementation for:
- Compliance with plan specifications
- Code quality and best practices
- Error handling completeness
- Performance considerations
- Memory management
```

### Integration Validation
```
Validate that my Stage [N] implementation integrates properly with:
- Previous stages [list dependencies]
- SlangDisplay architecture
- Existing ChameleonRT systems
Test integration points and suggest improvements.
```

## Tips for Success

1. **Be Specific**: Reference exact stage numbers, plan names, and sections
2. **Provide Context**: Always share current code state and previous progress
3. **Follow Order**: Implement stages sequentially - they build on each other
4. **Test Frequently**: Validate each checkpoint before proceeding
5. **Document Changes**: Update plans with your actual test commands and criteria
6. **Ask for Help**: Don't hesitate to ask for clarification on plan details
7. **Review Progress**: Regularly check implementation against plan requirements

## Example Complete Session Flow

```
Session Start:
"@workspace I want to implement Plan A Stage 3 from plan-a-unified-slang.md. 
I completed Stage 2 yesterday. Here's my current slang_imgui_renderer.h and .cpp files: [paste]"

Implementation:
"Generate the updateBuffers method following Stage 3 specifications.
Include all error handling and logging as shown in the plan."

Testing:
"Create tests for the Stage 3 checkpoints:
- Buffer creation succeeds with correct usage flags
- Buffer resizing logic handles growth correctly
[etc.]"

Validation:
"Review my Stage 3 implementation against the plan requirements.
Are all checkpoints satisfied? Any issues with the approach?"

Session End:
"Mark Stage 3 complete and prepare handoff for Stage 4 implementation.
What should the next session focus on?"
```

This structured approach will help you effectively use GitHub Copilot to implement the ImGui integration plans efficiently and correctly.
