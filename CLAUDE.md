# weBIGeo

Real-time 3D terrain visualization and GPU-accelerated avalanche simulation platform built on WebGPU.

## Quick Reference

- **Language**: C++20, WGSL (shaders), TypeScript (i18n)
- **Build**: CMake 3.25+, Qt 6.9.2+, Ninja
- **Platforms**: Web (Emscripten/WASM), Native Windows (Dawn/SDL2)
- **License**: GPL-3.0
- **Demo**: https://webigeo.alpinemaps.org/

## Project Structure

```
nucleus/           Core library (camera, tiles, tracks, utilities)
webgpu/            WebGPU abstraction layer
webgpu_engine/     Main renderer with compute node graph
  └─ compute/nodes/  17+ compute nodes for simulation pipeline
  └─ wgsl_shaders/   31 WGSL shader files
webgpu_app/        Main application entry point
webigeo_eval/      CLI tool for headless simulation
gl_engine/         Legacy OpenGL renderer (optional)
unittests/         Catch2 unit tests
docs/              Documentation (Setup, Usage, Technical, CLI)
cmake/             CMake helper modules
```

## Build Commands

```bash
# Configure (native Windows)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run tests
ctest --test-dir build

# Key CMake options
-DALP_WEBGPU_APP=ON       # Main app (default ON)
-DALP_WEBIGEO_EVAL=OFF    # CLI tool
-DALP_UNITTESTS=ON        # Unit tests
-DALP_GL_ENGINE=OFF       # Legacy OpenGL
```

## Code Style

Follow [AlpineMaps.org code style](https://github.com/AlpineMapsOrg/renderer#code-style):

- **Classes**: PascalCase (`TileGeometry`, `HeightDecodeNode`)
- **Functions**: snake_case (`run_impl()`, `tile_geometry()`)
- **Members**: `m_` prefix (`m_webgpu_device`, `m_tile_geometry`)
- **Constants**: UPPER_SNAKE_CASE
- **Namespaces**: lowercase (`nucleus::tile`, `webgpu_engine::compute::nodes`)
- **Headers**: `#pragma once`
- **Error handling**: `tl::expected<T, Error>`
- **Memory**: Smart pointers (`std::shared_ptr`, `std::unique_ptr`)

All files require GPL-3.0 license header:
```cpp
/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 [Author]
 * This program is free software: ...GPL-3.0 text...
 *****************************************************************************/
```

## Architecture

### Rendering Pipeline
1. **Atmosphere Pass** - Sky/atmosphere rendering
2. **Geometry Pass** - Terrain mesh rendering
3. **Compose Pass** - Final composition with post-processing

### Compute Node Graph
Graph-based GPU compute system for avalanche simulation:
- `TileSelectNode` / `TileRequestNode` / `TileStitchNode` - Tile management
- `HeightDecodeNode` / `ComputeNormalsNode` - Terrain processing
- `ComputeReleasePointsNode` - Avalanche start points
- `ComputeAvalancheTrajectoriesNode` - Monte Carlo trajectory simulation
- `ComputeSnowNode` - Snow effects
- `FxaaNode` - Anti-aliasing

## Avalanche Simulation

Monte Carlo particle-based simulation using WebGPU compute shaders. See `docs/Simulation.md` for full details.

### Pipeline Stages
1. **Input**: Heightmap (RGBA8, 16-bit encoded) + Release Mask (A > 0 marks start cells)
2. **Preprocessing**: `HeightDecodeNode` → `ComputeNormalsNode`
3. **Simulation**: `ComputeAvalancheTrajectoriesNode` - parallel particle trajectories
4. **Output**: `BufferToTextureNode` - color-mapped visualization

### Physics Models (`PhysicsModelType` enum)
| Model | Name | Description |
|-------|------|-------------|
| 0 | `PHYSICS_SIMPLE` | FlowPy-style with alpha-angle runout (default) |
| 1 | `PHYSICS_LESS_SIMPLE` | Full velocity integration with friction models |
| 2-5 | `GRADIENT`, `D8_*` | Alternative routing methods |

### Output Layers
| Layer | Operation | Description |
|-------|-----------|-------------|
| `layer1_zdelta` | atomicMax | Energy height → velocity proxy |
| `layer2_cellCounts` | atomicAdd | Particle count (probability) |
| `layer3_travelLength` | atomicMax | Maximum travel distance (m) |
| `layer4_travelAngle` | atomicMax | Travel angle gamma (degrees) |
| `layer5_altitudeDifference` | atomicMax | Altitude drop (m) |

### Key Parameters
| Parameter | Default | Description |
|-----------|---------|-------------|
| `num_steps` | 1000 | Max simulation steps per particle |
| `step_length` | 2.0 | Distance per step (meters) |
| `num_paths_per_release_cell` | 64 | Particles per release cell |
| `runout_flowpy_alpha` | 25° | Runout angle threshold |
| `persistence_contribution` | 0.9 | Direction smoothing (0-1) |

### Key Files
| Component | Path |
|-----------|------|
| Trajectory shader | `webgpu_engine/wgsl_shaders/compute/avalanche_trajectories_compute.wgsl` |
| Trajectories node | `webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.cpp` |
| Buffer to texture | `webgpu_engine/compute/nodes/BufferToTextureNode.cpp` |
| JS bindings | `webigeo_js/AvalancheSimulator.cpp` |

### AvaFrame Relationship
Based on [AvaFrame](https://avaframe.org/) open-source framework:
- **Model 0**: Implements com4FlowPy (Z-delta routing with alpha-angle)
- **Model 1**: Simplified com1DFA (Lagrangian particles with Coulomb/Voellmy friction)

## Key Entry Points

| Component | Entry Point |
|-----------|-------------|
| Main App | `webgpu_app/main.cpp` → `TerrainRenderer` |
| CLI Tool | `webigeo_eval/main.cpp` → `WebigeoApp` |
| Engine | `webgpu_engine/Context.h/cpp` |
| Core | `nucleus/EngineContext.h` |

## Dependencies (auto-fetched)

- **Qt 6.9.2+** - GUI framework
- **Dawn** - WebGPU implementation (native)
- **SDL2** - Window management (native)
- **Catch2** - Testing
- **GLM** - Math library
- **zpp_bits** - Serialization
- **tl_expected** - Error handling
- **radix** - AlpineMaps dependency

## Testing

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test
./build/unittests/nucleus_unittests
```

Tests located in `unittests/` covering nucleus, webgpu_engine, and gl_engine.

## Documentation

- `docs/Setup.md` - Build instructions (Qt, Emscripten, Dawn, SDL2)
- `docs/Usage.md` - User guide and features
- `docs/Technical.md` - Architecture and pipelines
- `docs/CLI.md` - CLI tool reference and JSON schema
- `docs/Evaluation.md` - Model validation results
