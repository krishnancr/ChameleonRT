# ChameleonRT Rendering Pipeline

This document provides detailed visual diagrams of ChameleonRT's path tracing pipeline, including the overall rendering loop, path tracing with Multiple Importance Sampling (MIS), and progressive sample accumulation.

## Table of Contents
- [High-Level Overview](#high-level-overview)
- [Detailed Path Tracing Loop](#detailed-path-tracing-loop)
- [Sample Accumulation Strategy](#sample-accumulation-strategy)
- [Rendering Quality Timeline](#rendering-quality-timeline)

---

## High-Level Overview

This diagram shows the top-level rendering loop executed each frame:

```mermaid
graph TD
    A[Start Frame] --> B[For each pixel]
    B --> C[For each sample per pixel]
    C --> D[Generate camera ray with jitter]
    D --> E[Path Tracing Loop]
    E --> F[Accumulate sample result]
    F --> G{More samples?}
    G -->|Yes| C
    G -->|No| H{More pixels?}
    H -->|Yes| B
    H -->|No| I[Apply firefly clamping]
    I --> J[Blend with accumulated buffer]
    J --> K[Display tonemapped result]
```

**Key Points:**
- Multiple samples per pixel (SPP) for anti-aliasing
- Each sample gets a jittered camera ray for MSAA
- Results are progressively accumulated across frames
- Firefly clamping prevents extreme outliers on glossy surfaces

---

## Detailed Path Tracing Loop

This is the core path tracing algorithm with Multiple Importance Sampling (MIS):

```mermaid
graph TD
    A[Ray Origin + Direction] --> B[Trace Ray]
    B --> C{Hit Surface?}
    
    C -->|No Hit| D[Sample Environment Map]
    D --> D1{Bounce > 0?}
    D1 -->|Yes| D2[Apply MIS weight]
    D1 -->|No| D3[Direct contribution]
    D2 --> Z[Return Illumination]
    D3 --> Z
    
    C -->|Hit| E[Get Material Properties]
    E --> F[Direct Lighting: Sample All Lights]
    F --> F1[For each quad light]
    F1 --> F2[Sample light position]
    F2 --> F3[Evaluate BSDF]
    F3 --> F4[Trace shadow ray]
    F4 --> F5{Visible?}
    F5 -->|Yes| F6[Compute MIS weight]
    F6 --> F7[Add light contribution]
    F5 -->|No| F1
    F7 --> G
    
    F1 --> G[Direct Lighting: Sample Environment]
    G --> G1{Env map loaded?}
    G1 -->|Yes| G2[Importance sample env map]
    G2 --> G3[Evaluate BSDF]
    G3 --> G4[Compute MIS weight]
    G4 --> G5[Add env contribution]
    G5 --> H
    G1 -->|No| H
    
    H[Indirect Lighting: Sample BRDF]
    H --> I[Generate new ray direction]
    I --> J[Check if hits lights]
    J --> J1{Hit quad light?}
    J1 -->|Yes| J2[Compute MIS weight]
    J2 --> J3[Add contribution]
    J1 -->|No| K
    J3 --> K
    
    K{Russian Roulette}
    K -->|Continue| L[Update path throughput]
    L --> M[Update ray origin/direction]
    M --> N{Max bounces?}
    N -->|No| B
    N -->|Yes| Z
    K -->|Terminate| Z
    
    Z[Return Illumination]
```

**Algorithm Breakdown:**

### 1. Ray Intersection
- Cast ray into scene using hardware ray tracing (DXR/Vulkan RT)
- If miss: sample environment map (with MIS if not primary ray)
- If hit: proceed to lighting calculations

### 2. Direct Lighting (Multiple Importance Sampling)

**Strategy 1: Light Sampling**
- For each quad light in scene:
  - Randomly sample a point on the light surface
  - Evaluate BRDF at the surface for light direction
  - Trace shadow ray to check visibility
  - If visible: compute MIS weight and add contribution

**Strategy 2: Environment Map Sampling**
- If environment map is loaded:
  - Use importance sampling (CDF-based) to pick direction
  - Evaluate BRDF for that direction
  - Compute MIS weight
  - Add weighted contribution

### 3. Indirect Lighting (BRDF Sampling)

**BRDF-based bounce:**
- Sample Disney BRDF to generate new ray direction
- Check if sampled direction hits any emissive surfaces
- If hit light: compute MIS weight and add contribution
- Use Russian roulette to probabilistically terminate path

### 4. Path Continuation
- Update path throughput by BRDF value and cosine term
- Russian roulette termination (probability based on throughput)
- Continue up to maximum bounce limit (typically 4-8 bounces)

**MIS (Multiple Importance Sampling) Benefits:**
- Combines light sampling and BRDF sampling strategies
- Uses power heuristic to weight contributions
- Reduces variance (less noise) compared to single-strategy sampling
- Especially effective for glossy surfaces and bright light sources

---
