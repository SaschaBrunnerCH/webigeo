# RAMMS Comparison and Integration Feasibility

This document compares weBIGeo's avalanche simulation with [RAMMS (Rapid Mass Movement Simulation)](https://ramms.ch/ramms-avalanche/) and analyzes the feasibility of integrating RAMMS-style physics into weBIGeo's WebGPU architecture.

## RAMMS Overview

[RAMMS](https://ramms.ch/ramms-avalanche/) is a professional avalanche simulation software developed by WSL/SLF Switzerland. It focuses on "accurate prediction of avalanche runout distances, flow velocities and impact pressures in natural three-dimensional terrain." It is the industry standard for hazard assessment in Swiss snow engineering.

### Key Capabilities

- Model terrain dynamics in realistic 3D landscapes
- Predict runout distances
- Estimate flow velocities
- Calculate impact pressures for engineering design

## Mathematical Model Comparison

| Aspect | RAMMS | weBIGeo |
|--------|-------|---------|
| **Governing Equations** | [Depth-averaged mass & momentum conservation](https://ramms.ch/ramms-avalanche/mathematical-model/) (Shallow Water Equations) | Particle-based trajectory simulation |
| **Flow Representation** | Continuum fluid (flow height H, velocity U) | Discrete particles (position, velocity) |
| **Spatial Discretization** | Finite volume mesh | Grid cells with interpolation |
| **Time Integration** | Forward in time for H and U | Step-by-step particle advection |
| **Density** | Constant assumed (typically 300 kg/m³) | Not explicitly modeled |
| **Flow Height** | Computed variable H(x,y,t) | Not computed (particles have no volume) |

### RAMMS Depth-Averaged Equations

RAMMS solves the shallow water equations with Voellmy friction. The [mathematical model](https://ramms.ch/ramms-avalanche/mathematical-model/) is based on:

**Mass conservation:**
```
∂H/∂t + ∇·(HU) = Q̇
```
Where Q̇ is the entrainment/deposition rate.

**Momentum conservation:**
```
∂(HU)/∂t + ∇·(HU⊗U) + ∇(½gH²) = gH·sin(θ) - S/ρ
```
Where S is the frictional resistance.

**weBIGeo** does not solve these continuum equations. Instead, it tracks individual particles following terrain gradients with simplified physics.

### Shallow Flow Justification

Snow avalanches exhibit a shallow flow geometry, meaning that the shallowness parameter defined by the characteristic flow height (h ≈ 1.0 m) and characteristic length (l ≈ 200 m), ε := h/l, is small (ε << 1). This geometrical property justifies a model formulation in terms of depth-averaged field variables.

## Friction Model Comparison

Both use the [Voellmy friction model](https://ramms.ch/ramms-avalanche/friction-parameters/), but with different implementations:

### RAMMS Voellmy Equation

```
S = μN + ρg·u²/ξ

Where:
  N = ρhg·cos(φ)  (normal stress)
  μ = dry Coulomb friction coefficient (velocity-independent)
  ξ = turbulent friction coefficient (m/s²) (velocity-dependent)
  ρ = density
  h = flow height
  u = velocity
```

The Voellmy model divides frictional resistance into:
- **Coulomb friction (μ)**: Dominates when flow is slow/stopping
- **Turbulent friction (ξ)**: Dominates when flow is fast

### Extended Voellmy with Cohesion (RAMMS v1.6.20+)

```
S = μN + ρg·u²/ξ + (1-μ)N₀ - (1-μ)N₀·e^(-N/N₀)
```

Where N₀ is the yield stress for modeling cohesive materials like mud and wet snow.

### weBIGeo Voellmy Implementation

Model 1 in [`acceleration_by_friction()`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl#L373):

```wgsl
// Simplified - no flow height dependency
friction_acceleration = μ * g * cos(slope) + (g * v²) / ξ
```

### Parameter Comparison

| Parameter | RAMMS | weBIGeo |
|-----------|-------|---------|
| **μ (mu)** | 0.05 - 0.40 (calibrated by Swiss guidelines) | 0.155 (default) |
| **ξ (xi)** | 400 - 3000 m/s² (terrain-dependent) | 4000 m/s² (default) |
| **Flow height (h)** | Computed dynamically | Not used (no continuum) |
| **Cohesion (N₀)** | Optional yield stress term | Not implemented |
| **Curvature effects** | Centrifugal forces included | Not implemented |

### RAMMS Parameter Calibration

RAMMS provides automatic friction parameter classification based on:
- Topographic data (slope angle, altitude, curvature)
- Forest information
- Return period
- Avalanche volume

Calibration data comes from the Vallée de la Sionne test site in Switzerland.

## Release Zone Comparison

| Aspect | [RAMMS Release](https://ramms.ch/ramms-avalanche/release/) | weBIGeo |
|--------|-------|---------|
| **Definition** | Polygon areas with release depth | Binary mask (release cells) |
| **Depth specification** | Perpendicular to ground surface | Not specified (point particles) |
| **Volume calculation** | Area × depth = release volume | Number of particles × particles_per_cell |
| **Entrainment** | Snow pickup during flow (Q̇ > 0) | Not modeled |
| **Deposition** | Snow deposition (Q̇ < 0) | Implicit (particle stops) |

> **Important**: RAMMS documentation emphasizes that "the definition of release areas and release depths (always perpendicular to the ground) have a very strong impact on the results."

## Numerical Solution Comparison

| Aspect | [RAMMS Numerical](https://ramms.ch/ramms-avalanche/numerical-solution/) | weBIGeo |
|--------|-------|---------|
| **Method** | Finite volume / finite difference | Monte Carlo particle tracking |
| **Grid** | Triangular or rectangular mesh | Regular raster grid |
| **Time stepping** | Adaptive CFL condition | Fixed step length |
| **Parallelization** | CPU (multi-threaded) | GPU (WebGPU compute shaders) |
| **Output** | Flow height, velocity, pressure fields | Statistical accumulation (max, count) |

### RAMMS Numerical Approach

The system is "solved forward in time for height H and velocity U" using the Voellmy-Salm (VS) model. The VS model has proven to be:
- Simple (only two flow parameters μ and ξ)
- Numerically accurate
- Well-suited for practical applications

## Output Comparison

| Output | RAMMS | weBIGeo |
|--------|-------|---------|
| **Flow height H(x,y,t)** | ✅ Time series | ❌ Not computed |
| **Velocity U(x,y,t)** | ✅ Time series | ⚠️ Z-delta proxy only |
| **Impact pressure** | ✅ Computed from ρU² | ❌ Not computed |
| **Runout distance** | ✅ From flow front position | ✅ From travel length layer |
| **Probability maps** | ⚠️ Via parameter variation | ✅ Cell counts layer |
| **Animation** | ✅ Full time evolution | ❌ Final state only |

## Calibration and Validation

| Aspect | RAMMS | weBIGeo |
|--------|-------|---------|
| **Calibration data** | Vallée de la Sionne test site (Switzerland) | Not specifically calibrated |
| **Parameter guidance** | Swiss guidelines, automatic classification | User-defined or defaults |
| **Validation** | Extensive back-calculation studies | Limited (research tool) |
| **Certification** | Used in official Swiss hazard mapping | Not certified |

## When to Use Which

| Use Case | Recommended Tool |
|----------|------------------|
| Official hazard mapping | RAMMS |
| Engineering design (pressure, barriers) | RAMMS |
| Quick visual assessment | weBIGeo |
| Interactive exploration | weBIGeo |
| Browser-based visualization | weBIGeo |
| Probabilistic runout estimation | Both |
| Detailed flow dynamics | RAMMS |
| Education and outreach | weBIGeo |

---

# Integration Feasibility: RAMMS-Style Physics in WebGPU

## Can RAMMS-style physics be integrated into weBIGeo?

**Short answer: Yes, but it would require significant architectural changes.**

## Current weBIGeo vs. RAMMS Requirements

| Requirement | Current weBIGeo | Needed for RAMMS-style |
|-------------|-----------------|------------------------|
| **Flow height field H(x,y)** | ❌ No grid storage | ✅ 2D texture/buffer per timestep |
| **Velocity field U(x,y)** | ❌ Only particle velocity | ✅ 2D texture/buffer per timestep |
| **Mass conservation** | ❌ Particles don't have mass | ✅ ∂H/∂t + ∇·(HU) = Q̇ |
| **Momentum conservation** | ⚠️ Simplified per-particle | ✅ Full PDE solver |
| **Numerical scheme** | Monte Carlo sampling | Finite volume/difference |
| **Time stepping** | Fixed step length | Adaptive CFL condition |
| **Entrainment/Deposition** | ❌ Not modeled | ✅ Q̇ source term |

## Implementation Approach for WebGPU

A RAMMS-style shallow water solver would require multiple compute shader passes per timestep:

```
Per timestep (compute shader passes):

1. Compute fluxes at cell boundaries
   - Read H, U from current state textures
   - Compute F = HU (mass flux)
   - Compute momentum flux with pressure term ½gH²

2. Update height field (mass conservation)
   - H_new = H_old - dt * ∇·(HU) + dt * Q̇

3. Update velocity field (momentum conservation)
   - Apply momentum equation
   - Apply Voellmy friction: S = μN + ρgu²/ξ
   - Include curvature effects (centrifugal forces)

4. Enforce CFL condition
   - dt = CFL * dx / max(|U| + sqrt(gH))
   - Reduce timestep if needed for stability

5. Check stopping criterion
   - Continue until flow stops or max time reached

6. Repeat steps 1-5 for next timestep
```

### Shader Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Shallow Water Solver                          │
│                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │ H(x,y) tex   │    │ U(x,y) tex   │    │ Flux buffers │       │
│  │ (height)     │    │ (velocity)   │    │ (F_x, F_y)   │       │
│  └──────┬───────┘    └──────┬───────┘    └──────┬───────┘       │
│         │                   │                   │                │
│         └───────────────────┴───────────────────┘                │
│                             │                                    │
│                    ┌────────▼────────┐                          │
│                    │  Flux Compute   │  Pass 1                  │
│                    │    Shader       │                          │
│                    └────────┬────────┘                          │
│                             │                                    │
│                    ┌────────▼────────┐                          │
│                    │ Height Update   │  Pass 2                  │
│                    │    Shader       │                          │
│                    └────────┬────────┘                          │
│                             │                                    │
│                    ┌────────▼────────┐                          │
│                    │ Velocity Update │  Pass 3                  │
│                    │  + Friction     │                          │
│                    └────────┬────────┘                          │
│                             │                                    │
│                    ┌────────▼────────┐                          │
│                    │  CFL Timestep   │  Pass 4                  │
│                    │   Reduction     │                          │
│                    └────────┬────────┘                          │
│                             │                                    │
│                             ▼                                    │
│                    Loop until stopped                            │
└─────────────────────────────────────────────────────────────────┘
```

## Feasibility Assessment

| Aspect | Feasibility | Notes |
|--------|-------------|-------|
| **GPU parallelization** | ✅ Excellent | Shallow water equations are well-suited for GPU (embarrassingly parallel per cell) |
| **WebGPU capability** | ✅ Sufficient | Compute shaders can handle PDE solvers |
| **Memory** | ⚠️ Higher | Need H, U, flux buffers (vs. just accumulator buffers) |
| **Complexity** | ⚠️ Significant | New solver architecture, not just parameter changes |
| **Performance** | ⚠️ Different tradeoff | Fewer total operations than particles, but more per-timestep |
| **Numerical stability** | ⚠️ Requires care | CFL condition, well-balanced schemes for wet/dry fronts |
| **Browser compatibility** | ✅ Good | WebGPU handles iterative compute well |

## What Would Need to Be Built

### 1. New Compute Nodes

| Node | Purpose |
|------|---------|
| `ShallowWaterSolverNode` | Main PDE solver orchestrating timesteps |
| `FluxComputeNode` | Compute cell boundary fluxes |
| `HeightUpdateNode` | Apply mass conservation |
| `VelocityUpdateNode` | Apply momentum + friction |
| `CFLTimestepNode` | Adaptive time stepping with GPU reduction |
| `EntrainmentNode` | Optional snow pickup/deposition |

### 2. New WGSL Shaders

| Shader | Purpose |
|--------|---------|
| `shallow_water_flux.wgsl` | Compute F = HU at cell faces |
| `shallow_water_height.wgsl` | Update H with ∇·(HU) |
| `shallow_water_momentum.wgsl` | Update U with pressure + gravity |
| `voellmy_friction_field.wgsl` | Apply S = μN + ρgu²/ξ to velocity field |
| `cfl_reduction.wgsl` | Parallel reduction to find max velocity |
| `wet_dry_treatment.wgsl` | Handle shoreline/front conditions |

### 3. New GPU Buffers/Textures

| Buffer | Type | Size | Purpose |
|--------|------|------|---------|
| `height_field` | float32 texture | W × H | Current flow height H(x,y) |
| `height_field_new` | float32 texture | W × H | Next timestep height (ping-pong) |
| `velocity_field` | vec2 texture | W × H | Current velocity U(x,y) |
| `velocity_field_new` | vec2 texture | W × H | Next timestep velocity (ping-pong) |
| `flux_x` | float32 texture | W × H | Mass flux in x direction |
| `flux_y` | float32 texture | W × H | Mass flux in y direction |
| `momentum_flux_x` | vec2 texture | W × H | Momentum flux in x direction |
| `momentum_flux_y` | vec2 texture | W × H | Momentum flux in y direction |
| `cfl_buffer` | float32 buffer | W × H | Per-cell CFL values for reduction |

### 4. Time Evolution Loop

Unlike the current single-pass particle simulation, RAMMS-style requires:

```cpp
// Pseudo-code for time evolution
while (!stopped && t < t_max) {
    // Compute adaptive timestep (GPU reduction)
    dt = compute_cfl_timestep();

    // Compute fluxes at cell boundaries
    dispatch_flux_compute_shader();

    // Update height field
    dispatch_height_update_shader();

    // Update velocity field with friction
    dispatch_velocity_update_shader();

    // Swap ping-pong buffers
    std::swap(height_field, height_field_new);
    std::swap(velocity_field, velocity_field_new);

    // Optional: store snapshot for animation
    if (should_store_frame) {
        copy_to_animation_buffer();
    }

    t += dt;
}
```

## Estimated Development Effort

| Component | Effort | Description |
|-----------|--------|-------------|
| Shallow water solver (basic) | 2-3 weeks | Core flux and update shaders |
| Voellmy friction integration | 1 week | Port friction model to field-based |
| Entrainment/deposition | 1 week | Q̇ source term handling |
| Numerical stability (well-balanced) | 1-2 weeks | Wet/dry fronts, slope source terms |
| CFL adaptive timestep | 3-5 days | GPU parallel reduction |
| Time animation support | 1 week | Snapshot storage, playback |
| Testing & validation | 2+ weeks | Compare with RAMMS/AvaFrame results |
| **Total** | **8-12 weeks** | For production-quality implementation |

## Alternative: Hybrid Approach

A simpler intermediate option would enhance the current particle model with RAMMS-inspired features without full PDE solver:

### Hybrid Enhancements

| Enhancement | Effort | Benefit |
|-------------|--------|---------|
| **Add mass to particles** | 2-3 days | Track particle "weight" for deposition |
| **Flow height proxy** | 1 week | Estimate h from local particle density |
| **Full Voellmy with h** | 3-5 days | Use estimated h in friction |
| **Entrainment** | 1 week | Spawn new particles in flow path |
| **Impact pressure estimate** | 2-3 days | p = ρv² from particle velocity |

This wouldn't match RAMMS fidelity but would improve physical realism significantly with ~3-4 weeks effort.

## Recommended Path Forward

### Option A: Full RAMMS-Style Solver
- **Pros**: Physical accuracy, industry-standard approach, time evolution
- **Cons**: Major development effort, new architecture
- **Best for**: If weBIGeo aims to become a professional tool

### Option B: Enhanced Particle Model (Hybrid)
- **Pros**: Incremental improvement, keeps GPU particle efficiency
- **Cons**: Still not full continuum physics
- **Best for**: Improving current approach without rewrite

### Option C: Expose Model 1 to JavaScript First
- **Pros**: Quick win, already implemented in native
- **Cons**: Doesn't add RAMMS features
- **Best for**: Immediate improvement with minimal effort

### Suggested Sequence

1. **Phase 1** (1-2 days): Expose Model 1 (physics-based) to JavaScript bindings
2. **Phase 2** (3-4 weeks): Implement hybrid enhancements (mass, entrainment, pressure)
3. **Phase 3** (8-12 weeks): Full shallow water solver (if needed)

---

## Summary

**RAMMS** is a physics-based continuum model solving depth-averaged shallow water equations with calibrated Voellmy friction parameters. It produces detailed time-evolving fields of flow height, velocity, and pressure.

**weBIGeo** is currently a simplified particle-based model optimized for GPU execution in web browsers. It provides rapid visualization of potential avalanche reach and probability.

**Integration is feasible** but requires significant development:
- Full RAMMS-style: ~8-12 weeks for shallow water PDE solver
- Hybrid approach: ~3-4 weeks for enhanced particles
- WebGPU is technically capable of both approaches

The models serve complementary purposes:
- **RAMMS**: Professional hazard assessment and engineering design
- **weBIGeo (current)**: Interactive exploration, education, quick screening
- **weBIGeo (enhanced)**: Could bridge the gap with RAMMS-inspired physics

## References

- [RAMMS::Avalanche](https://ramms.ch/ramms-avalanche/) - Professional avalanche simulation software
- [RAMMS Mathematical Model](https://ramms.ch/ramms-avalanche/mathematical-model/) - Depth-averaged equations
- [RAMMS Friction Parameters](https://ramms.ch/ramms-avalanche/friction-parameters/) - Voellmy model parameters
- [RAMMS Numerical Solution](https://ramms.ch/ramms-avalanche/numerical-solution/) - Computational methods
- [RAMMS Release Zones](https://ramms.ch/ramms-avalanche/release/) - Release area definition
- [WSL RAMMS Friction Parameters](https://ramms.slf.ch/en/modules/avalanche/theory/friction-parameters.html) - Detailed parameter documentation
- [RAMMS Paper (ScienceDirect)](https://www.sciencedirect.com/science/article/abs/pii/S0165232X10000844) - Scientific publication
- [Shallow Water Equations (Wikipedia)](https://en.wikipedia.org/wiki/Shallow_water_equations) - Mathematical background
- [Well-balanced schemes for avalanches (Cambridge)](https://www.cambridge.org/core/journals/journal-of-glaciology/article/numerical-modelling-of-dense-snow-avalanches-with-a-wellbalanced-scheme-based-on-the-2d-shallow-water-equations/28BA0273C2D74764E082E1A8F215EB04) - Numerical methods
