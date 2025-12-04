# Avalanche Simulation Pipeline

This document explains how the weBIGeo avalanche simulation works, from input data to visualization output.

> **Source Code**: The main simulation shader is [`avalanche_trajectories_compute.wgsl`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl)

## Overview

The simulation uses a **Monte Carlo particle-based approach** where thousands of particles are released from designated release zones and flow downhill following terrain gradients. The GPU computes trajectories in parallel using WebGPU compute shaders.

```
Input Data                    GPU Simulation                 Output Layers
┌──────────────┐             ┌──────────────┐              ┌──────────────┐
│ Heightmap    │────────────▶│  Particle    │─────────────▶│ Z-Delta      │
│ (elevation)  │             │  Trajectory  │              │ Cell Counts  │
├──────────────┤             │  Simulation  │              │ Travel Length│
│ Release Mask │────────────▶│              │              │ Travel Angle │
│ (start zones)│             │  (WGSL)      │              │ Height Diff  │
└──────────────┘             └──────────────┘              └──────────────┘
```

## Pipeline Stages

### Stage 1: Input Preparation

| Input | Description | Format |
|-------|-------------|--------|
| **Heightmap** | Terrain elevation data | RGBA8 texture (16-bit encoded in R+G channels) |
| **Release Mask** | Cells where avalanches start | RGBA8 texture (A > 0 marks release cell) |
| **AABB** | Bounding box in world coordinates | minX, minY, maxX, maxY (Web Mercator) |

The heightmap encoding: `elevation = (R * 256 + G) / 8.0` gives ~0.125m precision.

### Stage 2: Preprocessing Nodes

Before simulation, the compute graph runs these preprocessing nodes:

1. **HeightDecodeNode**: Decodes RGBA8 heightmap to float32 elevation texture
   - Source: [`HeightDecodeNode.cpp`](../webgpu_engine/compute/nodes/HeightDecodeNode.cpp)
   - Shader: [`height_decode_compute.wgsl`](../webgpu_engine/wgsl_shaders/compute/height_decode_compute.wgsl)

2. **ComputeNormalsNode**: Computes surface normals from elevation (slope direction)
   - Source: [`ComputeNormalsNode.cpp`](../webgpu_engine/compute/nodes/ComputeNormalsNode.cpp)
   - Shader: [`compute_normals.wgsl`](../webgpu_engine/wgsl_shaders/compute/compute_normals.wgsl)

### Stage 3: Particle Trajectory Simulation

The core simulation runs in [`avalanche_trajectories_compute.wgsl`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl).

- C++ Node: [`ComputeAvalancheTrajectoriesNode.cpp`](../webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.cpp)

#### Thread Structure
- Each GPU thread simulates **one particle**
- Thread ID (x, y) = release cell position
- Thread ID (z) = particle index within that cell
- Total particles = release_cells × particles_per_cell

#### Simulation Loop (per particle)

> See [`trajectory_overlay()`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl#L197) function

```
initialize:
    position = release_cell_center + random_offset
    start_height = sample_heightmap(position)
    travel_distance = 0
    velocity = 0

for step in 0..num_steps:
    1. Sample terrain at current position
       current_height = sample_heightmap(position)
       normal = sample_normalmap(position)

    2. Calculate physical quantities
       height_difference = start_height - current_height
       z_alpha = tan(runout_angle) × travel_distance
       z_delta = height_difference - z_alpha
       gamma = atan(height_difference / travel_distance)

    3. Check stopping condition
       if z_delta <= 0:
           break  // Runout reached

    4. Record values in output buffers
       draw_line(last_position, current_position, values...)

    5. Update position
       direction = blend(slope_direction, random_perturbation, persistence)
       position += direction × step_length
       travel_distance += step_length
```

#### Physical Models

The simulation supports multiple models controlled by `model_type` (aka `active_model`).

> See model implementations in [`trajectory_overlay()`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl#L288-L342)

##### Model Types

Defined in [`ComputeAvalancheTrajectoriesNode.h:36-43`](../webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.h#L36-L43):

```cpp
enum PhysicsModelType : uint32_t {
    PHYSICS_SIMPLE = 0,       // Model 0: FlowPy-style simple flow
    PHYSICS_LESS_SIMPLE = 1,  // Model 1: Physics-based with friction
    GRADIENT = 2,
    DISCRETIZED_GRADIENT = 3,
    D8_NO_WEIGHTS = 4,
    D8_WEIGHTS = 5,
};
```

##### Model 0 - PHYSICS_SIMPLE (FlowPy-style)
- Direction based on slope gradient
- Random perturbation for spread (`random_contribution`)
- Persistence to maintain flow direction (`persistence_contribution`)
- Runout based on alpha angle (`runout_flowpy_alpha`, default 25°)

##### Model 1 - PHYSICS_LESS_SIMPLE (Physics-based)
- Full velocity integration with timestep control
- Configurable parameters: gravity, mass, friction coefficient, drag coefficient
- Friction models: Coulomb, Voellmy, VoellmyMinShear, samosAT
- See [`acceleration_by_friction()`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl#L373)

##### Where to Configure Model Type

**1. Native weBIGeo App (ImGui UI)** - [`Window.cpp:688-690`](../webgpu_engine/Window.cpp#L688-L690)

Currently commented out, but the UI code exists:
```cpp
// Currently commented out in the UI:
/*if (ImGui::Combo("Model", (int*)&m_compute_pipeline_settings.model_type,
                   "Default\0physics_less_simple\0")) {
    update_settings_and_rerun_pipeline("compute_avalanche_trajectories_node");
}*/
```

**2. Pipeline Settings** - [`PipelineSettings.h:41`](../webgpu_engine/PipelineSettings.h#L41)

```cpp
compute::nodes::ComputeAvalancheTrajectoriesNode::PhysicsModelType model_type
    = compute::nodes::ComputeAvalancheTrajectoriesNode::PhysicsModelType::PHYSICS_SIMPLE;
```

**3. Settings Struct** - [`ComputeAvalancheTrajectoriesNode.h:112`](../webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.h#L112)

```cpp
struct AvalancheTrajectoriesSettings {
    // ...
    PhysicsModelType active_model;  // Set this to change model
    ModelPhysicsSimpleParams model1;
    ModelPhysicsLessSimpleParams model2;
    // ...
};
```

**4. Applied to GPU** - [`Window.cpp:1220`](../webgpu_engine/Window.cpp#L1220)

```cpp
trajectory_settings.active_model = m_compute_pipeline_settings.model_type;
```

##### Model Parameters UI

When **Model 0 (PHYSICS_SIMPLE)** is selected - [`Window.cpp:714-728`](../webgpu_engine/Window.cpp#L714-L728):
- Randomness slider (0-90°)
- Persistence slider (0-0.99)
- Alpha angle for runout (0-90°)

When **Model 1 (PHYSICS_LESS_SIMPLE)** is selected - [`Window.cpp:730-771`](../webgpu_engine/Window.cpp#L730-L771):
- Gravity (0-15 m/s²)
- Mass (0-100 kg)
- Drag coefficient (1-10000)
- Friction coefficient (0-0.5)
- Friction model dropdown: Coulomb, Voellmy, Voellmy Min Shear, SamosAt, None

##### JavaScript Bindings (Not Yet Exposed)

Currently, the JavaScript bindings in [`AvalancheSimulator.cpp`](../webigeo_js/AvalancheSimulator.cpp) do **not** expose `model_type`. Only Model 0 (PHYSICS_SIMPLE) is used with hardcoded settings.

To add model selection to JS, you would need to:
1. Add `modelType` to the JavaScript settings parsing in `configure_settings()`
2. Map it to `trajectory_settings.active_model`

### Stage 4: Output Layer Accumulation

As each particle traverses cells, values are recorded using atomic operations.

#### Where Layers Are Created

**1. GPU Buffer Allocation** - [`ComputeAvalancheTrajectoriesNode.cpp:133-151`](../webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.cpp#L133-L151)

```cpp
m_layer1_zdelta_buffer = std::make_unique<webgpu::raii::RawBuffer<uint32_t>>(...);
m_layer2_cellCounts_buffer = std::make_unique<webgpu::raii::RawBuffer<uint32_t>>(...);
m_layer3_travelLength_buffer = std::make_unique<webgpu::raii::RawBuffer<uint32_t>>(...);
m_layer4_travelAngle_buffer = std::make_unique<webgpu::raii::RawBuffer<uint32_t>>(...);
m_layer5_altitudeDifference_buffer = std::make_unique<webgpu::raii::RawBuffer<uint32_t>>(...);
```

**2. Output Sockets** - [`ComputeAvalancheTrajectoriesNode.cpp:44-48`](../webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.cpp#L44-L48)

```cpp
OutputSocket(*this, "layer1_zdelta", ...),
OutputSocket(*this, "layer2_cellCounts", ...),
OutputSocket(*this, "layer3_travelLength", ...),
OutputSocket(*this, "layer4_travelAngle", ...),
OutputSocket(*this, "layer5_altitudeDifference", ...),
```

**3. Shader Buffer Bindings** - [`avalanche_trajectories_compute.wgsl:94-98`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl#L94-L98)

```wgsl
@group(0) @binding(7) var<storage, read_write> output_layer1_zdelta: array<atomic<u32>>;
@group(0) @binding(8) var<storage, read_write> output_layer2_cellCounts: array<atomic<u32>>;
@group(0) @binding(9) var<storage, read_write> output_layer3_travelLength: array<atomic<u32>>;
@group(0) @binding(10) var<storage, read_write> output_layer4_travelAngle: array<atomic<u32>>;
@group(0) @binding(11) var<storage, read_write> output_layer5_altitudeDifference: array<atomic<u32>>;
```

**4. Value Writing** - [`draw_line_pos()`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl#L135-L181)

```wgsl
// For each cell the particle passes through:
if (settings.layer1_zdelta_enabled != 0) {
    atomicMax(&output_layer1_zdelta[buffer_index], u32(z_delta * layer_scaling));
}
if (settings.layer2_cellCounts_enabled != 0) {
    atomicAdd(&output_layer2_cellCounts[buffer_index], 1);
}
if (settings.layer3_travelLength_enabled != 0) {
    atomicMax(&output_layer3_travelLength[buffer_index], u32(travel_length * layer_scaling));
}
if (settings.layer4_travelAngle_enabled != 0) {
    atomicMax(&output_layer4_travelAngle[buffer_index], u32(degrees(travel_angle) * layer_scaling));
}
if (settings.layer5_altitudeDifference_enabled != 0) {
    atomicMax(&output_layer5_altitudeDifference[buffer_index], u32(altitude_difference * layer_scaling));
}
```

**5. Value Calculation** - [`trajectory_overlay()`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl#L258-L283)

```wgsl
// Inside the simulation loop, before draw_line_uv():
let height_difference = start_point_height - current_height;  // Layer 5
let z_alpha = tan(settings.runout_flowpy_alpha) * world_space_travel_distance;
let z_gamma = height_difference;
z_delta = z_gamma - z_alpha;  // Layer 1
let gamma = atan(height_difference / world_space_travel_distance);  // Layer 4 (travel_angle)

// world_space_travel_distance is accumulated each step → Layer 3
// cellCounts is simply incremented by 1 for each visit → Layer 2

draw_line_uv(last_uv, current_uv, trajectory_value, z_delta, world_space_travel_distance, gamma, height_difference);
```

#### Layer Summary Table

| Layer | Variable | Operation | Description |
|-------|----------|-----------|-------------|
| **layer1_zdelta** | `z_delta` | `atomicMax` | Maximum energy height (m) |
| **layer2_cellCounts** | `1` | `atomicAdd` | Number of particles passing through |
| **layer3_travelLength** | `travel_distance` | `atomicMax` | Maximum travel distance (m) |
| **layer4_travelAngle** | `gamma` | `atomicMax` | Maximum travel angle (degrees) |
| **layer5_altitudeDifference** | `height_difference` | `atomicMax` | Maximum altitude drop (m) |

The `draw_line_pos()` function uses Bresenham's line algorithm to write values to all cells between consecutive particle positions.

#### Atomic Operations Explained

- **atomicMax**: Keeps the highest value from all particles. Used for intensity/risk metrics.
- **atomicAdd**: Accumulates counts. Used for probability/frequency metrics.

#### How Layers Are Used in Native weBIGeo App

The native weBIGeo desktop application provides an ImGui-based UI for layer selection:

**1. Layer Definition** - [`Window.h:217-223`](../webgpu_engine/Window.h#L217-L223)

```cpp
const std::vector<computeLayer> m_compute_overlay_layers = {
    { "Speed", "layer1_zdelta", " m/s" },
    { "Cell Counts", "layer2_cellCounts", "" },
    { "Travel Length", "layer3_travelLength", " m" },
    { "Travel Angle", "layer4_travelAngle", " °" },
    { "Altitude Difference", "layer5_altitudeDifference", " hm" },
};
```

**2. Layer Selection UI** - [`Window.cpp:799-826`](../webgpu_engine/Window.cpp#L799-L826)

The app provides two dropdowns:
- **Color Layer**: Which layer determines the color (default: Speed/zdelta)
- **Alpha Layer**: Which layer determines transparency (default: Cell Counts)

```cpp
// User selects from dropdown
if (ImGui::BeginCombo("Color Layer", current_color_layer)) {
    for (size_t i = 0; i < m_compute_overlay_layers.size(); ++i) {
        if (ImGui::Selectable(m_compute_overlay_layers[i].name.c_str(), is_selected)) {
            m_current_compute_color_layer_index = i;
            rewire_buffer_to_texture_node();  // Reconnect the node graph
            rerun_buffer_to_texture = true;
        }
    }
}
```

**3. Dynamic Rewiring** - [`Window.cpp:434-447`](../webgpu_engine/Window.cpp#L434-L447)

When the user changes layers, the node graph is rewired:

```cpp
void Window::rewire_buffer_to_texture_node() {
    const std::string& current_color_socket = m_compute_overlay_layers[m_current_compute_color_layer_index].socket_name;
    const std::string& current_alpha_socket = m_compute_overlay_layers[m_current_compute_alpha_layer_index].socket_name;

    // Reconnect BufferToTextureNode inputs to different layer buffers
    buffer_to_texture_node.input_socket("storage buffer").connect(
        trajectories_node.output_socket(current_color_socket));  // e.g., "layer3_travelLength"
    buffer_to_texture_node.input_socket("transparency buffer").connect(
        trajectories_node.output_socket(current_alpha_socket));  // e.g., "layer2_cellCounts"
}
```

**4. Color Map Configuration** - [`Window.cpp:793-796`](../webgpu_engine/Window.cpp#L793-L796)

Each layer has configurable min/max bounds for color mapping:

```cpp
const std::string& unit = m_compute_overlay_layers[m_current_compute_color_layer_index].unit;
paint_legend_gui(m_compute_pipeline_settings.color_map_bounds.x,  // min value
                 m_compute_pipeline_settings.color_map_bounds.y,  // max value
                 m_compute_pipeline_settings.use_bin_interpolation,
                 unit);
```

#### Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    ComputeAvalancheTrajectoriesNode                      │
│                                                                          │
│  Simulation Loop → draw_line_pos() writes to 5 GPU buffers:             │
│                                                                          │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐    │
│  │layer1_zdelta │ │layer2_counts │ │layer3_length │ │layer4_angle  │... │
│  │ (atomicMax)  │ │ (atomicAdd)  │ │ (atomicMax)  │ │ (atomicMax)  │    │
│  └──────┬───────┘ └──────┬───────┘ └──────┬───────┘ └──────┬───────┘    │
│         │                │                │                │             │
│         └────────────────┴────────────────┴────────────────┘             │
│                                   │                                      │
│                          Output Sockets                                  │
└──────────────────────────────────┬───────────────────────────────────────┘
                                   │
                    User selects layer (ImGui dropdown)
                                   │
                                   ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                        BufferToTextureNode                               │
│                                                                          │
│  Inputs:                                                                 │
│    storage buffer ──────► Selected color layer (e.g., layer1_zdelta)    │
│    transparency buffer ─► Selected alpha layer (e.g., layer2_cellCounts)│
│                                                                          │
│  Processing:                                                             │
│    1. Read uint32 values from buffers                                   │
│    2. Apply velocity formula: sqrt(value * 2 * g)  ← Problem!           │
│    3. Normalize to [0,1] using color_map_bounds                         │
│    4. Apply color_mapping_flowpy() palette                              │
│    5. Apply alpha from transparency buffer                              │
│                                                                          │
│  Output: RGBA8 texture                                                  │
└──────────────────────────────────┬───────────────────────────────────────┘
                                   │
                                   ▼
                           3D Terrain Overlay
```

### Stage 5: Buffer to Texture Conversion

The raw uint32 buffers are converted to color-mapped RGBA textures by `BufferToTextureNode`:

- C++ Node: [`BufferToTextureNode.cpp`](../webgpu_engine/compute/nodes/BufferToTextureNode.cpp)
- Shader: [`buffer_to_texture_compute.wgsl`](../webgpu_engine/wgsl_shaders/compute/buffer_to_texture_compute.wgsl)

```wgsl
// Get raw value from buffer
value = f32(buffer[index])

// Optional: Convert z_delta to velocity (for zdelta layer only)
if (calculate_velocity) {
    value = sqrt(value * 2.0 * 9.81)  // v = sqrt(2 * g * h)
}

// Normalize to [0,1] range
normalized = (value - min) / (max - min)

// Apply color mapping
color = color_mapping_flowpy(normalized)
```

#### Color Mapping (FlowPy palette)

The [`color_mapping_flowpy()`](../webgpu_engine/wgsl_shaders/util/color_mapping.wgsl#L66) function uses a 20-bin gradient:

```
Low values                                              High values
    │                                                        │
    ▼                                                        ▼
#404043 → #6f4b96 → #9f5ba1 → #d16b97 → #f78e85 → #fec79c → #fdfecf
(dark)   (purple)   (pink)    (salmon)  (orange)  (peach)   (cream)
```

## Output Layer Details

### Layer 1: Z-Delta (Velocity Proxy)

**Physical meaning**: The "energy line" height, representing potential + kinetic energy.

```
z_delta = height_difference - tan(alpha) × travel_distance
```

Where `alpha` is the runout angle (typically 25°).

**Interpretation**:
- High z_delta = high velocity/energy at this point
- z_delta = 0 marks the runout limit
- z_delta < 0 = particle has stopped

**Velocity conversion**: `v = sqrt(2 × g × z_delta)` gives velocity in m/s.

**Typical range**: 0 - 40 m/s (after velocity conversion)

### Layer 2: Cell Counts (Probability)

**Physical meaning**: Number of particles that passed through each cell.

**Interpretation**:
- High count = high probability of being affected
- Useful for hazard mapping
- Can be normalized by total particles for probability

**Typical range**: 0 - 1000+ counts (depends on settings)

### Layer 3: Travel Length

**Physical meaning**: Maximum distance traveled by any particle reaching this cell.

**Interpretation**:
- Shows how far avalanche material traveled
- Higher at runout zone
- Lower near release zone

**Typical range**: 0 - 2000+ meters

### Layer 4: Travel Angle (Gamma)

**Physical meaning**: Angle from horizontal to the line connecting release point to current cell.

```
gamma = atan(height_difference / travel_distance)
```

**Interpretation**:
- Steep angle near release zone
- Shallow angle at runout
- Related to avalanche intensity

**Typical range**: 0 - 45 degrees

### Layer 5: Height Difference (Altitude Drop)

**Physical meaning**: Elevation drop from release point to this cell.

```
height_difference = start_height - current_height
```

**Interpretation**:
- Monotonically increasing downhill
- Correlates with potential energy
- Direct measure of vertical drop

**Typical range**: 0 - 1000+ meters

## Simulation Parameters

### Key Settings

> Defined in [`AvalancheTrajectoriesSettings`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl#L41-L80) struct

| Parameter | Default | Description |
|-----------|---------|-------------|
| `num_steps` | 1000 | Maximum simulation steps per particle |
| `step_length` | 2.0 | Distance per step (meters) |
| `num_paths_per_release_cell` | 64 | Particles per release cell |
| `max_perturbation` | 25° | Random deviation angle |
| `persistence_contribution` | 0.9 | Direction smoothing (0-1) |
| `runout_flowpy_alpha` | 25° | Runout angle threshold |

### Performance Considerations

- **Grid resolution**: Higher resolution = more cells = more computation
- **Particles per cell**: Linear scaling with particle count
- **Step count**: Maximum path length = steps × step_length
- **GPU memory**: 5 uint32 buffers × width × height × 4 bytes

## Known Issues

### Velocity Calculation for Non-Z-Delta Layers

Currently, the `BufferToTextureNode` applies the velocity formula `sqrt(value × 2 × g)` to ALL layers. This is only correct for z_delta and distorts other layers.

> See [`buffer_to_texture_compute.wgsl:60`](../webgpu_engine/wgsl_shaders/compute/buffer_to_texture_compute.wgsl#L60) - `calculate_velocity` is hardcoded to `true`

| Layer | Effect of sqrt(v × 2g) |
|-------|----------------------|
| zdelta | Correct - converts to velocity |
| cellCounts | Wrong - sqrt of count meaningless |
| travelLength | Wrong - sqrt of distance distorts |
| travelAngle | Wrong - sqrt of degrees distorts |
| heightDifference | Wrong - sqrt of meters distorts |

**Workaround**: The color mapping bounds can partially compensate, but the mapping is non-linear.

**Fix needed**: Add a `calculate_velocity` flag per layer in BufferToTextureSettings.

## File Locations

| Component | Path |
|-----------|------|
| Trajectory shader | [webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl) |
| Buffer to texture shader | [webgpu_engine/wgsl_shaders/compute/buffer_to_texture_compute.wgsl](../webgpu_engine/wgsl_shaders/compute/buffer_to_texture_compute.wgsl) |
| Color mapping | [webgpu_engine/wgsl_shaders/util/color_mapping.wgsl](../webgpu_engine/wgsl_shaders/util/color_mapping.wgsl) |
| Height decode shader | [webgpu_engine/wgsl_shaders/compute/height_decode_compute.wgsl](../webgpu_engine/wgsl_shaders/compute/height_decode_compute.wgsl) |
| Normals shader | [webgpu_engine/wgsl_shaders/compute/compute_normals.wgsl](../webgpu_engine/wgsl_shaders/compute/compute_normals.wgsl) |
| Trajectories node | [webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.cpp](../webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.cpp) |
| Buffer to texture node | [webgpu_engine/compute/nodes/BufferToTextureNode.cpp](../webgpu_engine/compute/nodes/BufferToTextureNode.cpp) |
| Height decode node | [webgpu_engine/compute/nodes/HeightDecodeNode.cpp](../webgpu_engine/compute/nodes/HeightDecodeNode.cpp) |
| Normals node | [webgpu_engine/compute/nodes/ComputeNormalsNode.cpp](../webgpu_engine/compute/nodes/ComputeNormalsNode.cpp) |
| JS bindings | [webigeo_js/AvalancheSimulator.cpp](../webigeo_js/AvalancheSimulator.cpp) |
| JS readback | [webigeo_js/bindings.cpp](../webigeo_js/bindings.cpp) |
| Texture readback node | [webigeo_js/nodes/TextureReadbackNode.cpp](../webigeo_js/nodes/TextureReadbackNode.cpp) |

## Relationship to AvaFrame

weBIGeo's avalanche simulation is inspired by the [AvaFrame](https://avaframe.org/) open-source avalanche simulation framework. This section explains how weBIGeo relates to AvaFrame's computational modules.

### AvaFrame Computational Modules Overview

| Module | Name | Description |
|--------|------|-------------|
| [com1DFA](https://docs.avaframe.org/en/latest/moduleCom1DFA.html) | DFA-Kernel | Dense Flow Avalanche - full physics simulation with depth-averaged equations |
| [com2AB](https://docs.avaframe.org/en/latest/moduleCom2AB.html) | Alpha-Beta Model | Statistical runout prediction based on terrain geometry |
| [com3Hybrid](https://docs.avaframe.org/en/latest/moduleCom3Hybrid.html) | Hybrid Modeling | Combines com1DFA and com2AB approaches |
| [com4FlowPy](https://docs.avaframe.org/en/latest/moduleCom4FlowPy.html) | Flow-Py | Probabilistic flow routing with alpha-angle runout |
| [com5SnowSlide](https://docs.avaframe.org/en/latest/moduleCom5SnowSlide.html) | Snow Slide | Small snow slide simulation |
| [com6RockAvalanche](https://docs.avaframe.org/en/latest/moduleCom6RockAvalanche.html) | Rock Avalanche | Rock avalanche modeling |
| [com7Regional](https://docs.avaframe.org/en/latest/moduleCom7Regional.html) | Regional Modeling | Large-scale regional analysis |
| [com8MoTPSA](https://docs.avaframe.org/en/latest/moduleCom8MoTPSA.html) | NGI MoT-PSA | Norwegian Geotechnical Institute model |
| [com9MoTVoellmy](https://docs.avaframe.org/en/latest/moduleCom9MoTVoellmy.html) | NGI MoT-Voellmy | NGI model with Voellmy friction |

### weBIGeo Implementation

weBIGeo implements a **hybrid approach** combining elements from multiple AvaFrame modules:

#### From com4FlowPy (Primary Influence)

weBIGeo's **Model 0** is directly based on [Flow-Py theory](https://docs.avaframe.org/en/latest/theoryCom4FlowPy.html):

| Flow-Py Concept | weBIGeo Implementation |
|-----------------|------------------------|
| **Z-delta (Z^δ)** | `z_delta = height_difference - tan(alpha) * travel_distance` |
| **Alpha angle runout** | `runout_flowpy_alpha` setting (default 25°) |
| **Routing persistence** | `persistence_contribution` blends slope direction with previous direction |
| **Random perturbation** | `max_perturbation` adds stochastic spread |
| **Cell-based routing** | Particles traverse grid cells, recording values |

**Z-delta explained**:
```
Z^δ = Z^γ - Z^α

Where:
  Z^γ = altitude difference (start_height - current_height)
  Z^α = tan(α) × travel_distance (energy dissipation line)

When Z^δ ≤ 0 → particle stops (runout reached)
```

#### From com1DFA (Physics-based Model)

weBIGeo's **Model 1** incorporates physics from the [DFA-Kernel](https://docs.avaframe.org/en/latest/moduleCom1DFA.html):

| DFA Concept | weBIGeo Implementation |
|-------------|------------------------|
| **Lagrangian particles** | Each GPU thread = one particle |
| **Velocity integration** | `velocity += acceleration * dt` |
| **CFL condition** | `dt = cfl * dx / velocity` for stability |
| **Friction models** | Coulomb, Voellmy, samosAT implemented |

**Friction models** (see [`acceleration_by_friction()`](../webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl#L373)):

| Model | Formula | Use Case |
|-------|---------|----------|
| **Coulomb** | `τ = μ × σ` | Simple sliding friction |
| **Voellmy** | `τ = μ × σ + ρg × v² / ξ` | Adds velocity-dependent drag |
| **Voellmy + min shear** | `τ = τ_min + μ × σ + ρg × v² / ξ` | Prevents creep on flat terrain |
| **samosAT** | Complex formula with yield stress | Realistic deceleration |

#### From com2AB (Alpha-Beta Statistics)

The **alpha angle** concept comes from the [Alpha-Beta model](https://docs.avaframe.org/en/latest/moduleCom2AB.html):

- **Beta angle (β)**: Angle from start to the "10° point" (where slope first drops below 10°)
- **Alpha angle (α)**: Angle from start to stopping point
- Statistical relationship: `α = k₁β + k₂y'' + k₃H₀ + k₄`

weBIGeo uses a simplified constant alpha angle (typically 25°) rather than the full statistical model.

### Key Differences from AvaFrame

| Aspect | AvaFrame | weBIGeo |
|--------|----------|---------|
| **Platform** | Python/CPU | WebGPU/GPU (browser-based) |
| **Execution** | Sequential | Massively parallel |
| **Grid approach** | Cell-to-cell routing | Particle trajectories with interpolation |
| **Output** | Multiple analysis formats (rasters, reports, statistics) | On-demand visualization layers (5 layer types) |
| **Physics detail** | Full depth-averaged equations | Simplified particle-based |
| **Use case** | Professional hazard analysis | Interactive exploration and quick assessment |

### Model Selection Guide

| weBIGeo Model | Best For | AvaFrame Equivalent |
|---------------|----------|---------------------|
| **Model 0** (simple) | Quick runout estimation, probabilistic spread | com4FlowPy |
| **Model 1** (physics) | Velocity-dependent behavior, realistic deceleration | com1DFA (simplified) |

### Friction Model Parameters

From the [com1DFA friction theory](https://docs.avaframe.org/en/latest/theoryCom1DFA.html):

| Parameter | Symbol | Typical Value | Description |
|-----------|--------|---------------|-------------|
| Friction coefficient | μ (mu) | 0.155 | Coulomb friction angle tangent |
| Turbulent friction | ξ (xi) | 4000 m/s² | Voellmy turbulent coefficient |
| Density | ρ | 200 kg/m³ | Snow density |
| Slab thickness | h | 1 m | Release slab depth |

## Comparison with RAMMS

For a detailed comparison with [RAMMS (Rapid Mass Movement Simulation)](https://ramms.ch/ramms-avalanche/) and analysis of integrating RAMMS-style physics into weBIGeo, see:

**[Simulation_RAMMS.md](Simulation_RAMMS.md)** - Includes:
- Mathematical model comparison (shallow water equations vs. particle-based)
- Voellmy friction model differences
- Release zone and numerical solution comparison
- Integration feasibility analysis
- Implementation roadmap for RAMMS-style physics in WebGPU

## References

- [AvaFrame Documentation](https://docs.avaframe.org/en/latest/) - Open source avalanche simulation framework
- [Flow-Py Theory](https://docs.avaframe.org/en/latest/theoryCom4FlowPy.html) - Alpha-angle routing model
- [DFA-Kernel Theory](https://docs.avaframe.org/en/latest/theoryCom1DFA.html) - Dense flow avalanche physics
- [Alpha-Beta Model](https://docs.avaframe.org/en/latest/moduleCom2AB.html) - Statistical runout prediction
- [com1DFA Paper (GMD)](https://gmd.copernicus.org/articles/16/7013/2023/) - Scientific publication on AvaFrame DFA module
- [RAMMS Comparison & Integration](Simulation_RAMMS.md) - Detailed comparison with RAMMS and WebGPU integration feasibility
