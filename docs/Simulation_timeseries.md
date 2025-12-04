# Time-Series Flow Height H(x,y,t) and Deposition D(x,y,t) Output

This document describes the time-series output features for weBIGeo's avalanche simulation, which records:

- **Flow height H(x,y,t)**: Particle density over time (particles currently moving)
- **Deposition D(x,y,t)**: Particles that have stopped at each time frame (snow accumulation)

## Overview

The time-series feature extends the avalanche simulation to capture temporal dynamics of the flow. Instead of only recording maximum values (z-delta, cell counts, etc.), it records particle counts at regular time intervals throughout the simulation, enabling visualization of how the avalanche progresses over time.

### Flow vs Deposition

- **Flow height H(x,y,t)**: Represents particles actively moving through a cell at time t
- **Deposition D(x,y,t)**: Represents particles that stopped in a cell at time t
- **Total snow**: The visualization combines flow + cumulative deposition for realistic snow accumulation display

## Flow Height Estimation Method

The flow height is estimated using a **particle density proxy** method:

```
H(x,y,t) = (particle_count(x,y,t) / particles_per_cell) * reference_height
```

Where:

- `particle_count(x,y,t)` - Number of particles in cell (x,y) at time frame t
- `particles_per_cell` - Number of particles released per release cell (simulation setting)
- `reference_height` - Reference slab thickness in meters (default: 1.0m)

This approach assumes that particle density is proportional to flow height, which is a reasonable approximation for the Monte Carlo particle-based simulation model.

## Time Tracking

Time is estimated from particle velocity during the simulation:

```
dt = step_length / velocity
velocity = sqrt(2 * g * z_delta)
```

Where:

- `step_length` - Length of one simulation step in meters
- `g` - Gravitational acceleration (9.81 m/s²)
- `z_delta` - Height difference from release point (energy proxy)

A minimum velocity of 1.0 m/s is enforced to prevent division by zero for stationary particles.

### Time Frame Recording

Particles record their position to the time-series buffer when:

1. Time-series is enabled
2. The current time frame index changes (based on `total_time / time_interval`)
3. The time frame index is within the maximum frames limit

## Data Layout

The time-series data is stored in a flat buffer with the following layout:

```
data[t * width * height + y * width + x] = particle_count
```

Where:

- `t` - Time frame index (0 to maxFrames-1)
- `width` - Output grid width in pixels
- `height` - Output grid height in pixels
- `x`, `y` - Cell coordinates

Total buffer size: `width * height * maxFrames * sizeof(uint32_t)` bytes

## Settings

### JavaScript API

```typescript
const result = await simulator.run(input, {
  numParticlesPerReleaseCell: 64,
  // ... other settings ...
  timeSeries: {
    enabled: true, // Enable time-series recording
    maxFrames: 100, // Maximum number of time frames to store
    timeInterval: 1.0, // Time interval between snapshots (seconds)
    referenceHeight: 1.0, // Reference flow height for normalization (meters)
  },
});
```

### Settings Description

| Setting           | Type    | Default | Description                                          |
| ----------------- | ------- | ------- | ---------------------------------------------------- |
| `enabled`         | boolean | false   | Enable/disable time-series recording                 |
| `maxFrames`       | number  | 100     | Maximum number of time frames to store               |
| `timeInterval`    | number  | 1.0     | Time interval between snapshots in seconds           |
| `referenceHeight` | number  | 1.0     | Reference slab thickness for flow height calculation |

## Output

When time-series is enabled, the simulation result includes a `timeSeries` object:

```typescript
interface TimeSeriesOutput {
  enabled: boolean; // Time-series was enabled
  maxFrames: number; // Maximum frames stored
  timeInterval: number; // Time interval (seconds)
  referenceHeight: number; // Reference height (meters)
  width: number; // Output grid width
  height: number; // Output grid height
  data: Uint32Array; // Flow particle counts [t * w * h + y * w + x]
  depositionData: Uint32Array; // Deposition counts [t * w * h + y * w + x]
}
```

### Accessing Flow Height and Deposition

To convert particle counts to flow height and deposition:

```javascript
const { width, height, maxFrames, data, depositionData, referenceHeight } =
  result.timeSeries;
const particlesPerCell = settings.numParticlesPerReleaseCell;
const frameSize = width * height;

function getFlowHeight(x, y, t) {
  const count = data[t * frameSize + y * width + x];
  return (count / particlesPerCell) * referenceHeight;
}

function getDeposition(x, y, t) {
  const count = depositionData[t * frameSize + y * width + x];
  return (count / particlesPerCell) * referenceHeight;
}

// Get cumulative deposition (total snow that has stopped up to time t)
function getCumulativeDeposition(x, y, t) {
  let cumulative = 0;
  for (let f = 0; f <= t; f++) {
    cumulative += depositionData[f * frameSize + y * width + x];
  }
  return (cumulative / particlesPerCell) * referenceHeight;
}

// Total snow = flow + cumulative deposition
function getTotalSnow(x, y, t) {
  return getFlowHeight(x, y, t) + getCumulativeDeposition(x, y, t);
}
```

## Implementation Details

### Shader (WGSL)

The time-series recording is implemented in `avalanche_trajectories_compute.wgsl`:

1. **Settings struct** includes time-series parameters:

   ```wgsl
   timeseries_enabled: u32,
   timeseries_max_frames: u32,
   timeseries_interval: f32,
   timeseries_reference_height: f32,
   ```

2. **Storage buffers** at bindings 12 and 13:

   ```wgsl
   @group(0) @binding(12) var<storage, read_write> output_flow_height_timeseries: array<atomic<u32>>;
   @group(0) @binding(13) var<storage, read_write> output_deposition_timeseries: array<atomic<u32>>;
   ```

3. **Recording functions**:

   ```wgsl
   fn record_flow_height_snapshot(current_uv: vec2f, time_frame: u32) {
       if (settings.timeseries_enabled == 0u || time_frame >= settings.timeseries_max_frames) {
           return;
       }
       let cell_pos = vec2u(floor(current_uv * vec2f(settings.output_resolution)));
       if (cell_pos.x >= settings.output_resolution.x || cell_pos.y >= settings.output_resolution.y) {
           return;
       }
       let frame_offset = time_frame * settings.output_resolution.x * settings.output_resolution.y;
       let cell_offset = cell_pos.y * settings.output_resolution.x + cell_pos.x;
       atomicAdd(&output_flow_height_timeseries[frame_offset + cell_offset], 1u);
   }

   fn record_deposition(current_uv: vec2f, time_frame: u32) {
       if (settings.timeseries_enabled == 0u || time_frame >= settings.timeseries_max_frames) {
           return;
       }
       let cell_pos = vec2u(floor(current_uv * vec2f(settings.output_resolution)));
       if (cell_pos.x >= settings.output_resolution.x || cell_pos.y >= settings.output_resolution.y) {
           return;
       }
       let frame_offset = time_frame * settings.output_resolution.x * settings.output_resolution.y;
       let cell_offset = cell_pos.y * settings.output_resolution.x + cell_pos.x;
       atomicAdd(&output_deposition_timeseries[frame_offset + cell_offset], 1u);
   }
   ```

4. **Time tracking** in Model 0 (PHYSICS_SIMPLE) simulation loop:

   ```wgsl
   var total_time: f32 = 0.0;
   var last_recorded_time_frame: u32 = 0xFFFFFFFFu;

   // In simulation loop:
   if (settings.timeseries_enabled != 0u) {
       let min_velocity: f32 = 1.0;
       let effective_velocity = max(velocity_magnitude, min_velocity);
       let step_dt = length(relative_trajectory) / effective_velocity;
       total_time += step_dt;

       let current_time_frame = u32(total_time / settings.timeseries_interval);
       if (current_time_frame != last_recorded_time_frame &&
           current_time_frame < settings.timeseries_max_frames) {
           record_flow_height_snapshot(current_uv, current_time_frame);
           last_recorded_time_frame = current_time_frame;
       }
   }
   ```

5. **Deposition recording** when particles stop (z_delta <= 0 or no movement):

   ```wgsl
   // When particle stops due to z_delta <= 0:
   if (z_delta <= 0) {
       if (settings.timeseries_enabled != 0u) {
           let stop_time_frame = u32(total_time / settings.timeseries_interval);
           record_deposition(current_uv, stop_time_frame);
       }
       break;
   }

   // When particle stops due to insufficient movement:
   if (dir_magnitude < 0.001) {
       if (settings.timeseries_enabled != 0u) {
           let stop_time_frame = u32(total_time / settings.timeseries_interval);
           record_deposition(current_uv, stop_time_frame);
       }
       break;
   }
   ```

### C++ Node

`ComputeAvalancheTrajectoriesNode` manages the time-series buffers:

1. **Buffer creation** with appropriate size:

   ```cpp
   size_t timeseries_buffer_size = m_settings.timeseries.enabled
       ? (width * height * m_settings.timeseries.max_frames)
       : 1;  // Placeholder when disabled

   m_flow_height_timeseries_buffer = std::make_unique<RawBuffer<uint32_t>>(
       device,
       WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
       timeseries_buffer_size,
       "avalanche flow height time-series storage");

   m_deposition_timeseries_buffer = std::make_unique<RawBuffer<uint32_t>>(
       device,
       WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
       timeseries_buffer_size,
       "avalanche deposition time-series storage");
   ```

2. **Output sockets** for downstream access:

   ```cpp
   OutputSocket(*this, "flow_height_timeseries",
                data_type<webgpu::raii::RawBuffer<uint32_t>*>(),
                [this]() { return m_flow_height_timeseries_buffer.get(); })

   OutputSocket(*this, "deposition_timeseries",
                data_type<webgpu::raii::RawBuffer<uint32_t>*>(),
                [this]() { return m_deposition_timeseries_buffer.get(); })
   ```

### JavaScript Bindings

The `bindings.cpp` file handles async buffer readback:

1. **Buffer info** is provided via `get_readback_buffer_info()`:

   ```javascript
   bufferInfo.timeSeries = {
     valid: true,
     bufferPtr: <GPU buffer pointer>,
     bufferSize: width * height * maxFrames * 4,
     width, height, maxFrames, timeInterval, referenceHeight
   };
   ```

2. **Async readback** creates a staging buffer and copies data:

   ```javascript
   // Create staging buffer with MAP_READ
   var stagingBuffer = device.createBuffer({
     size: bufferSize,
     usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
   });

   // Copy from storage to staging
   commandEncoder.copyBufferToBuffer(
     srcBuffer,
     0,
     stagingBuffer,
     0,
     bufferSize
   );
   device.queue.submit([commands]);

   // Map and read
   await device.queue.onSubmittedWorkDone();
   await stagingBuffer.mapAsync(GPUMapMode.READ);
   var tsData = new Uint32Array(stagingBuffer.getMappedRange());
   ```

## Memory Budget

For a typical simulation:

- Grid: 256 x 192 = 49,152 cells
- 100 frames at 1.0s interval = 100 seconds of simulation
- Flow height buffer: 49,152 × 100 × 4 bytes = **19.7 MB**
- Deposition buffer: 49,152 × 100 × 4 bytes = **19.7 MB**
- **Total: ~39.4 MB**

Maximum recommended:

- 500 frames × 512 × 512 grid × 2 buffers = **1,048 MB** (exceeds GPU limits)
- Practical limit: ~250 frames at 512 × 512 for ~500 MB total

## Limitations

1. **Model 0 only** - Time-series is only implemented for PHYSICS_SIMPLE model. Model 1 (PHYSICS_LESS_SIMPLE) is not currently working.

2. **Memory usage** - Large grids with many frames can consume significant GPU memory.

3. **Time estimation** - Time is estimated from velocity, which is derived from z-delta. This is an approximation and may not match real-world timing exactly.

4. **Sparse data** - Early time frames may have sparse data as particles haven't traveled far yet. Later frames may also be sparse if particles have stopped.

## ArcGIS Example

The `webigeo_js/js-example-arcgis/index.html` include time-series visualization:

1. Enable "Enable Time-Series H(x,y,t)" checkbox in Simulation Settings
2. Run the simulation
3. Select "Flow Height H(x,y,t)" from the Output Layer dropdown
4. Use the animation controls:
   - Frame slider to scrub through time
   - Play/Stop buttons for animation
   - Playback speed selector (0.5x, 1x, 2x, 4x)

### Visualization Modes

#### Flat Overlay Mode (Default)

A 2D color-mapped overlay draped on the terrain surface.

#### 3D Mesh Mode

Enable "Show as 3D Mesh" checkbox to visualize flow height as a terrain-aware 3D mesh:

- Mesh vertices are positioned at ground elevation + flow height
- Height is exaggerated for visibility (1x, 2x, 5x, 10x options)
- Smoothing filter available (None, Light, Medium, Heavy) using box blur

### 3D Mesh Settings

| Setting             | Options                                                   | Default | Description                                      |
| ------------------- | --------------------------------------------------------- | ------- | ------------------------------------------------ |
| Height Exaggeration | 1x, 2x, 5x, 10x                                           | 5x      | Vertical scale factor for flow height visibility |
| Smoothing           | None, Light (1 pass), Medium (2 passes), Heavy (3 passes) | Light   | Box blur filter to smooth jagged mesh surfaces   |

### Visualization Colors

**Flat Overlay Mode** combines flow height and cumulative deposition:

- **Active flow** (particles moving): Yellow → Orange → Red gradient
- **Deposited snow** (particles stopped): Light blue/white gradient
- **Mixed flow + deposition**: Blended colors showing both moving and settled snow

**3D Mesh Mode** uses a dedicated color ramp based on flow height:

- Light blue (0.01m) → Cyan → Green → Yellow → Orange → Red (2.5m+)

This allows users to see:

- Where the avalanche is currently flowing (warm colors)
- Where snow has accumulated (cool blue/white colors)
- The total snow depth at any point in time

### Grid Multiplier

The simulation supports a **Grid Multiplier** (1x, 2x, 3x, 4x) to increase output resolution:

- Higher multipliers produce finer detail in the output
- Memory usage scales with multiplier squared
- Default: 1x (standard resolution)

## Related Files

| File                                                                     | Description                             |
| ------------------------------------------------------------------------ | --------------------------------------- |
| `webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl` | Shader with time-series recording       |
| `webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.h`         | C++ node header with TimeSeriesSettings |
| `webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.cpp`       | C++ node implementation                 |
| `webigeo_js/AvalancheSimulator.cpp`                                      | JS bindings with time-series output     |
| `webigeo_js/bindings.cpp`                                                | Async buffer readback implementation    |
| `webigeo_js/webigeo.d.ts`                                                | TypeScript type definitions             |
| `webigeo_js/js-example-arcgis/index.html`                                | Main example with 3D mesh visualization |
| `webigeo_js/js-example-arcgis/arcgis-example.html`                       | Alternative example with animation UI   |

## References

- [FlowPy Model](https://doi.org/10.5194/nhess-17-1483-2017) - Basis for runout angle stopping criterion
- [RAMMS](https://ramms.slf.ch/) - Reference for flow height concepts (see `docs/Simulation_RAMMS.md`)
