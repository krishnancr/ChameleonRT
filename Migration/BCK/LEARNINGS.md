# Learnings from Slang-GFX Experiment

## Executive Summary

**Experiment:** Build ChameleonRT ray tracer using Slang-GFX abstraction layer  
**Duration:** ~4 weeks  
**Status:** Pivoting to direct APIs with Slang shader compilation only  
**Outcome:** Valuable learning experience; confirmed direct API approach is better for our goals

---

## What We Achieved

### ✅ Successes

1. **Rasterization Pipeline Working**
   - D3D12 backend functional through slang-gfx
   - Vulkan backend functional through slang-gfx
   - Basic resource binding working
   - ImGui rendering integrated

2. **Slang Integration Knowledge**
   - CMake build system for Slang
   - Library linking (slang.dll, gfx.dll)
   - Session creation and device initialization
   - Basic shader compilation pipeline

3. **Cross-Platform Infrastructure**
   - Conditional compilation (D3D12 vs Vulkan)
   - DLL deployment automation
   - CMake option-based backend selection

4. **Vendor Utilities**
   - Successfully integrated shader-cursor from gfx-util
   - Understood Slang's parameter binding approach

### 📚 Technical Skills Gained

- Slang API usage patterns
- GFX device abstraction model
- Shader reflection in Slang
- COM-style reference counting (ComPtr)
- Cross-compilation target management

---

## What Didn't Work

### ❌ Blockers & Challenges

1. **Ray Tracing Complexity**
   - Ray tracing pipeline creation significantly more complex than raster
   - Slang-gfx abstractions don't map cleanly to DXR/VkRayTracing concepts
   - Shader binding tables particularly confusing through abstraction

2. **Debugging Difficulty**
   - Three layers to debug: App → Slang-GFX → Native API
   - Error messages often unclear about which layer failed
   - Hard to distinguish slang-gfx bugs from our code bugs

3. **Metal Support**
   - Marked "experimental" in Slang repository
   - Ray tracing returns `SLANG_E_NOT_IMPLEMENTED`
   - Would require significant slang-gfx contributions to make work

4. **Learning Impedance**
   - Abstraction hides API differences we *should* understand
   - Can't learn Vulkan synchronization when slang-gfx does it
   - Can't compare D3D12 vs Vulkan binding models directly

5. **Performance Uncertainty**
   - Unknown overhead from abstraction layer
   - Difficult to attribute performance differences
   - Benchmarking validity questionable

### ⚠️ Specific Technical Issues

**Issue 1: Pipeline State Creation**
- Slang-gfx uses generic PipelineDesc
- Doesn't expose all D3D12/Vulkan options
- Some optimizations inaccessible

**Issue 2: Resource Binding**
- Automatic binding layout sometimes conflicts with manual control
- Hard to optimize for specific GPU architectures
- Descriptor set management opaque

**Issue 3: Synchronization**
- Slang-gfx handles fences/semaphores internally
- Can't fine-tune for specific scenarios
- Learning opportunity lost

---

## Key Insights

### 1. Abstraction Timing Matters

**Too Early:** We abstracted before understanding the problem space  
**Better:** First master APIs directly, *then* abstract common patterns

### 2. Raster ≠ Ray Tracing

Rasterization working felt like progress, but:
- Ray tracing is fundamentally different architecture
- Acceleration structures, shader tables, recursion
- Raster success didn't transfer to RT complexity

### 3. Learning vs. Shipping

For a **learning project**, abstraction is counterproductive:
- Hides the details we want to understand
- Reduces debugging skills development
- Obscures API architectural differences

For a **shipping product**, slang-gfx makes sense:
- Faster iteration
- Less boilerplate
- Broader platform support (if mature)

### 4. Metal Reality Check

Experimental status means:
- Unpredictable stability
- Missing features (RT not implemented)
- Potential API changes
- Community support limited

**Decision:** Start with D3D12/Vulkan; add Metal later if justified

---

## Valuable Patterns to Keep

Despite pivoting away from slang-gfx, these patterns remain useful:

### 1. Slang Compilation Pipeline

```cpp
// This pattern works regardless of API:
GlobalSession → Session → Module → EntryPoint → LinkedProgram → TargetCode
```

### 2. CMake Slang Integration

```cmake
find_package(Slang REQUIRED)
target_link_libraries(my_target PRIVATE Slang::Slang)
# DLL deployment automation
```

### 3. Cross-Compilation Strategy

```cpp
// Single shader source → multiple targets
compileToSPIRV(slangSource, "main");  // Vulkan
compileToDXIL(slangSource, "main");   // D3D12
compileToMetal(slangSource, "main");  // Metal
```

### 4. Shader Reflection

```cpp
// Extract binding information from compiled shaders
slang::IComponentType::getLayout()
// Useful for automatic descriptor set creation
```

---

## Why We're Pivoting

### For Learning Goals:

| Goal | Slang-GFX | Direct APIs |
|------|-----------|-------------|
| Understand D3D12 pipeline creation | ❌ Hidden | ✅ Explicit |
| Learn Vulkan synchronization | ❌ Abstracted | ✅ Manual |
| Compare API binding models | ❌ Unified | ✅ Side-by-side |
| Debug graphics issues | ❌ 3 layers | ✅ 2 layers |
| Platform-specific optimizations | ❌ Limited | ✅ Full access |

### For Benchmarking Goals:

| Requirement | Slang-GFX | Direct APIs |
|-------------|-----------|-------------|
| Predictable performance | ❌ Unknown overhead | ✅ Transparent |
| Attribute differences | ❌ Ambiguous | ✅ Clear |
| Optimize for GPU | ❌ Limited | ✅ Full control |
| Trust results | ❌ Questionable | ✅ Confident |

### For Practical Concerns:

- **Time investment:** 6-10 more weeks for slang-gfx RT vs. 4-6 for direct DXR
- **Metal support:** Experimental vs. would need it working
- **Debugging experience:** Frustrating vs. educational
- **Skill transferability:** Limited vs. highly marketable

---

## What We're Taking Forward

### Reusable Code

1. **`FindSlang.cmake`** - Production-ready CMake module
2. **Slang compilation utilities** - Wrapper around Slang API
3. **Shader reflection helpers** - Extract binding info
4. **Build automation scripts** - Clean rebuild, DLL deployment

### Knowledge

1. **Slang compiler API** - Will use for shader compilation
2. **Cross-compilation patterns** - Write once, compile to many targets
3. **CMake integration** - How to find and link Slang
4. **Parameter binding concepts** - Though implemented differently per API

### Architecture Insights

1. **When to abstract** - After understanding problem, not before
2. **Layering costs** - Each layer adds debugging complexity
3. **API philosophy differences** - D3D12 (validation) vs Vulkan (explicit)
4. **Platform maturity matters** - Experimental = risky for timelines

---

## Recommendations for Future Projects

### When to Use Slang-GFX:

✅ **Use slang-gfx if:**
- Shipping commercial multi-platform product
- Team already knows underlying APIs well
- Targeting mature platforms (D3D12, Vulkan compute)
- Prioritizing time-to-market over learning
- Willing to contribute to slang-gfx for missing features

### When to Use Direct APIs:

✅ **Use direct APIs if:**
- Learning graphics programming (like us)
- Benchmarking/research requiring predictable performance
- Targeting experimental platforms (Metal RT, new extensions)
- Need platform-specific optimizations
- Small team that can manage multiple backends

### Hybrid Approach (Our Choice):

✅ **Use Slang for shaders, direct APIs for everything else:**
- Best of both worlds
- Single shader source (Slang language)
- Full control over pipeline/binding/sync
- Clear performance attribution
- Deep API learning
- Future Metal support feasible

---

## Conclusion

The slang-gfx experiment wasn't a failure—it was reconnaissance.

**We learned:**
- How Slang compilation works
- Where abstraction helps and hurts
- What our actual priorities are
- How to structure a gradual migration

**We're keeping:**
- Slang shader compilation knowledge
- Working CMake integration
- Build automation infrastructure
- Architectural patterns

**We're changing:**
- Pipeline creation → native per API
- Resource binding → native per API
- Synchronization → native per API
- Learning approach → direct before abstract

The weeks invested taught us *what questions to ask* and *what tools we need*. That's valuable even if the code doesn't ship.

---

**Lessons Learned:**
1. Abstract after mastering, not before
2. Raster success doesn't predict RT success
3. Three-layer debugging is 10x harder than two-layer
4. Experimental features are high risk for timelines
5. Learning goals and shipping goals need different architectures

**Next Phase:**  
Direct D3D12 DXR with Slang shaders → Master one API deeply → Expand to Vulkan → Evaluate Metal

---

*"The best code is code you delete after learning what you needed to know."*
