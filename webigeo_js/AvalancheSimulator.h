/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2025
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#pragma once

#include <QObject>
#include <memory>

#include <emscripten/val.h>
#include <webgpu/webgpu.h>

#include <webgpu_engine/Context.h>
#include <webgpu_engine/PipelineManager.h>
#include <webgpu_engine/compute/nodes/NodeGraph.h>

#include "Settings.h"
#include "nodes/LoadTextureFromMemoryNode.h"
#include "nodes/LoadAabbFromMemoryNode.h"
#include "nodes/TextureReadbackNode.h"

// Forward declaration
namespace webgpu_engine::compute::nodes {
class ComputeAvalancheTrajectoriesNode;
class BufferToTextureNode;
}

namespace webigeo_js {

/**
 * Main class for running avalanche simulation from JavaScript.
 * Provides async API via Promises.
 */
class AvalancheSimulator : public QObject {
    Q_OBJECT

public:
    AvalancheSimulator();
    ~AvalancheSimulator();

    /**
     * Initialize WebGPU. Must be called before run().
     * @return Promise that resolves when initialization is complete.
     */
    emscripten::val init();

    /**
     * Run the avalanche simulation.
     * @param input JavaScript object with input data (aabb, heightmap, releaseCells)
     * @param settings JavaScript object with simulation settings (optional)
     * @return Promise that resolves with simulation output.
     */
    emscripten::val run(emscripten::val input, emscripten::val settings);

    /**
     * Check if WebGPU is supported.
     * @return Promise that resolves to boolean.
     */
    static emscripten::val isSupported();

    /**
     * Clean up resources.
     */
    void destroy();

    /**
     * Complete initialization after WebGPU device is obtained.
     * Called from JavaScript after requestDevice() succeeds.
     */
    void complete_init();

    /**
     * Start running the simulation.
     * Called from JavaScript when run() Promise is created.
     */
    void start_run();

    /**
     * Process WebGPU events.
     * Called from JavaScript polling loop to fire WebGPU callbacks.
     */
    void process_events();

    /**
     * Get readback buffer info for JavaScript-based async buffer mapping.
     * Returns an object with: { bufferPtr, bufferSize, width, height, paddedBytesPerRow, unpaddedBytesPerRow }
     */
    emscripten::val get_readback_buffer_info();

    /**
     * Set the readback data from JavaScript after async buffer mapping completes.
     * @param data Uint8Array with RGBA8 pixel data
     */
    void set_readback_data(emscripten::val data);

    /**
     * Update color map bounds for a specific layer.
     * @param layerName Name of the layer (zdelta, cellCounts, travelLength, travelAngle, heightDifference)
     * @param minValue Minimum value for color mapping
     * @param maxValue Maximum value for color mapping
     */
    void set_color_map_bounds(const std::string& layerName, float minValue, float maxValue);

    /**
     * Get default color map bounds for all layers.
     * @return JavaScript object with layer names as keys and {min, max} objects as values
     */
    static emscripten::val get_default_color_map_bounds();

public slots:
    void on_run_completed();
    void on_run_failed(webgpu_engine::compute::nodes::GraphRunFailureInfo info);

private:
    /**
     * Create the compute graph with memory-based I/O nodes.
     */
    std::unique_ptr<webgpu_engine::compute::nodes::NodeGraph> create_js_compute_graph();

    /**
     * Parse JavaScript input object and configure nodes.
     */
    bool configure_input(emscripten::val input);

    /**
     * Parse JavaScript settings object and configure simulation parameters.
     */
    void configure_settings(emscripten::val settings);

    /**
     * Collect output from readback nodes and return as JavaScript object.
     */
    emscripten::val collect_output();

private:
    // WebGPU handles
    WGPUInstance m_instance = nullptr;
    WGPUAdapter m_adapter = nullptr;
    WGPUDevice m_device = nullptr;

    // Flag to track if simulation is running
    bool m_running = false;

    // Engine context
    std::unique_ptr<webgpu_engine::Context> m_context;

    // Compute graph
    std::unique_ptr<webgpu_engine::compute::nodes::NodeGraph> m_node_graph;

    // Pointers to specific nodes for configuration (owned by node graph)
    nodes::LoadTextureFromMemoryNode* m_heightmap_node = nullptr;
    nodes::LoadTextureFromMemoryNode* m_release_cells_node = nullptr;
    nodes::LoadAabbFromMemoryNode* m_aabb_node = nullptr;

    // Pointer to trajectories node for output dimensions
    webgpu_engine::compute::nodes::ComputeAvalancheTrajectoriesNode* m_trajectories_node = nullptr;

    // Pointers to texture readback nodes for each layer
    nodes::TextureReadbackNode* m_zdelta_readback_node = nullptr;
    nodes::TextureReadbackNode* m_cellCounts_readback_node = nullptr;
    nodes::TextureReadbackNode* m_travelLength_readback_node = nullptr;
    nodes::TextureReadbackNode* m_travelAngle_readback_node = nullptr;
    nodes::TextureReadbackNode* m_heightDifference_readback_node = nullptr;

    // Pointers to buffer-to-texture nodes for color map configuration
    webgpu_engine::compute::nodes::BufferToTextureNode* m_zdelta_b2t_node = nullptr;
    webgpu_engine::compute::nodes::BufferToTextureNode* m_cellCounts_b2t_node = nullptr;
    webgpu_engine::compute::nodes::BufferToTextureNode* m_travelLength_b2t_node = nullptr;
    webgpu_engine::compute::nodes::BufferToTextureNode* m_travelAngle_b2t_node = nullptr;
    webgpu_engine::compute::nodes::BufferToTextureNode* m_heightDifference_b2t_node = nullptr;

    // Currently selected layer for readback (0-4)
    int m_current_layer = 0;

    // Promise callbacks
    emscripten::val m_resolve_callback;
    emscripten::val m_reject_callback;

    bool m_initialized = false;
};

} // namespace webigeo_js
