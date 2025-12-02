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

#include "AvalancheSimulator.h"

#include <QDebug>
#include <emscripten.h>
#include <emscripten/bind.h>

#include <webgpu_engine/compute/nodes/ComputeAvalancheTrajectoriesNode.h>
#include <webgpu_engine/compute/nodes/ComputeNormalsNode.h>
#include <webgpu_engine/compute/nodes/HeightDecodeNode.h>
#include <webgpu_engine/compute/nodes/BufferToTextureNode.h>
#include <webgpu/webgpu_interface.hpp>

// External function from emdawnwebgpu to get the preinitialized device
extern "C" WGPUDevice emscripten_webgpu_get_device();

using namespace webgpu_engine::compute::nodes;

namespace webigeo_js {

AvalancheSimulator::AvalancheSimulator()
    : m_resolve_callback(emscripten::val::undefined())
    , m_reject_callback(emscripten::val::undefined())
{
}

AvalancheSimulator::~AvalancheSimulator()
{
    destroy();
}

emscripten::val AvalancheSimulator::init()
{
    // Call _createInitPromise which returns an executor function, then pass it to Promise constructor
    auto executor = emscripten::val::module_property("_createInitPromise")(reinterpret_cast<uintptr_t>(this));
    return emscripten::val::global("Promise").new_(executor);
}

emscripten::val AvalancheSimulator::run(emscripten::val input, emscripten::val settings)
{
    EM_ASM({ console.log('[C++] run() called, m_initialized =', $0); }, m_initialized ? 1 : 0);

    if (!m_initialized) {
        EM_ASM({ console.error('[C++] run() - not initialized!'); });
        return emscripten::val::global("Promise").call<emscripten::val>("reject",
            emscripten::val("Simulator not initialized. Call init() first."));
    }

    EM_ASM({ console.log('[C++] Configuring input...'); });

    // Configure input
    if (!configure_input(input)) {
        EM_ASM({ console.error('[C++] Failed to configure input'); });
        return emscripten::val::global("Promise").call<emscripten::val>("reject",
            emscripten::val("Failed to configure input"));
    }

    EM_ASM({ console.log('[C++] Input configured successfully'); });

    // Configure settings if provided
    if (!settings.isUndefined() && !settings.isNull()) {
        EM_ASM({ console.log('[C++] Configuring settings...'); });
        configure_settings(settings);
    }

    EM_ASM({ console.log('[C++] Creating run promise...'); });

    // Return promise that will resolve when simulation completes
    auto executor = emscripten::val::module_property("_createRunPromise")(reinterpret_cast<uintptr_t>(this));
    return emscripten::val::global("Promise").new_(executor);
}

emscripten::val AvalancheSimulator::isSupported()
{
    auto executor = emscripten::val::module_property("_createSupportCheckPromise")();
    return emscripten::val::global("Promise").new_(executor);
}

void AvalancheSimulator::destroy()
{
    m_node_graph.reset();
    m_context.reset();

    if (m_device) {
        wgpuDeviceRelease(m_device);
        m_device = nullptr;
    }
    if (m_adapter) {
        wgpuAdapterRelease(m_adapter);
        m_adapter = nullptr;
    }
    if (m_instance) {
        wgpuInstanceRelease(m_instance);
        m_instance = nullptr;
    }

    m_initialized = false;
}

void AvalancheSimulator::complete_init()
{
    EM_ASM({ console.log('[C++] complete_init() called'); });

    // Create a WebGPU instance for event processing
    WGPUInstanceDescriptor instance_desc = {};
    instance_desc.nextInChain = nullptr;
    m_instance = wgpuCreateInstance(&instance_desc);

    if (!m_instance) {
        EM_ASM({ console.error('[C++] Failed to create WebGPU instance'); });
        return;
    }

    // Get the device from JavaScript using emscripten_webgpu_get_device()
    // This retrieves the device that was stored in Module.preinitializedWebGPUDevice
    m_device = emscripten_webgpu_get_device();

    if (!m_device) {
        EM_ASM({ console.error('[C++] Failed to get WebGPU device from JavaScript'); });
        return;
    }

    EM_ASM({ console.log('[C++] Got WebGPU device, creating context...'); });

    // Create engine context and set the device
    m_context = std::make_unique<webgpu_engine::Context>();
    m_context->set_webgpu_device(m_device);
    m_context->set_webgpu_instance(m_instance);

    EM_ASM({ console.log('[C++] Initializing context (shaders, pipelines)...'); });

    // Initialize the context - this creates shader modules and pipelines
    m_context->initialise();

    EM_ASM({ console.log('[C++] Context initialized, creating node graph...'); });

    // Create the compute graph
    m_node_graph = create_js_compute_graph();

    m_initialized = true;
    EM_ASM({ console.log('[C++] Initialization complete, m_initialized = true'); });
}

void AvalancheSimulator::start_run()
{
    EM_ASM({ console.log('[C++] start_run() called'); });

    if (!m_initialized || !m_node_graph) {
        EM_ASM({ console.error('[C++] Cannot start run: not initialized'); });
        // Reject will be handled by the caller checking m_initialized
        return;
    }

    EM_ASM({ console.log('[C++] Starting node graph execution...'); });

    m_running = true;

    // Run the node graph synchronously - Qt signals don't work in Emscripten
    // so we use run_sync() which directly calls nodes in topological order
    m_node_graph->run_sync();

    EM_ASM({ console.log('[C++] node_graph->run_sync() returned'); });

    // Try to complete texture readback (uses emscripten_sleep to poll for buffer mapping)
    if (m_texture_readback_node && m_texture_readback_node->is_readback_pending()) {
        EM_ASM({ console.log('[C++] Attempting texture readback...'); });
        bool readback_success = m_texture_readback_node->try_complete_readback();
        EM_ASM({ console.log('[C++] Texture readback result:', $0); }, readback_success ? 1 : 0);
    }

    // Since run_sync() completes synchronously, call on_run_completed directly
    on_run_completed();
}

static int s_process_events_count = 0;

void AvalancheSimulator::process_events()
{
    if (!m_running || !m_instance) {
        return;
    }

    // Process WebGPU events - this fires any pending callbacks from wgpuQueueOnSubmittedWorkDone
    wgpuInstanceProcessEvents(m_instance);

    s_process_events_count++;
    if (s_process_events_count % 60 == 0) {
        EM_ASM({ console.log('[C++] process_events called', $0, 'times'); }, s_process_events_count);
    }
}

emscripten::val AvalancheSimulator::get_readback_buffer_info()
{
    emscripten::val info = emscripten::val::object();

    if (!m_texture_readback_node || !m_texture_readback_node->is_readback_pending()) {
        info.set("valid", false);
        return info;
    }

    WGPUBuffer buffer = m_texture_readback_node->get_staging_buffer();
    if (!buffer) {
        info.set("valid", false);
        return info;
    }

    info.set("valid", true);
    // Pass the buffer handle as a pointer value that JavaScript can use with emscripten's WebGPU
    info.set("bufferPtr", reinterpret_cast<uintptr_t>(buffer));
    info.set("bufferSize", static_cast<double>(m_texture_readback_node->get_buffer_size()));
    info.set("width", m_texture_readback_node->get_width());
    info.set("height", m_texture_readback_node->get_height());
    info.set("paddedBytesPerRow", m_texture_readback_node->get_padded_bytes_per_row());
    info.set("unpaddedBytesPerRow", m_texture_readback_node->get_unpadded_bytes_per_row());

    EM_ASM({ console.log('[C++] get_readback_buffer_info: buffer=' + $0 + ', size=' + $1 + ', dims=' + $2 + 'x' + $3); },
           reinterpret_cast<uintptr_t>(buffer),
           m_texture_readback_node->get_buffer_size(),
           m_texture_readback_node->get_width(),
           m_texture_readback_node->get_height());

    return info;
}

void AvalancheSimulator::set_readback_data(emscripten::val data)
{
    if (!m_texture_readback_node) {
        EM_ASM({ console.error('[C++] set_readback_data: no readback node'); });
        return;
    }

    // Convert JavaScript Uint8Array to std::vector
    std::vector<uint8_t> vec = emscripten::vecFromJSArray<uint8_t>(data);
    EM_ASM({ console.log('[C++] set_readback_data: received ' + $0 + ' bytes'); }, vec.size());

    m_texture_readback_node->set_readback_data(std::move(vec));
}

void AvalancheSimulator::on_run_completed()
{
    EM_ASM({ console.log('[C++] on_run_completed() called'); });

    m_running = false;

    // Collect output and resolve promise
    emscripten::val output = collect_output();

    EM_ASM({ console.log('[C++] Output collected, resolving promise...'); });

    // Call JavaScript to resolve the promise
    emscripten::val::module_property("_resolveRun")(
        reinterpret_cast<uintptr_t>(this),
        output
    );

    EM_ASM({ console.log('[C++] Promise resolved'); });
}

void AvalancheSimulator::on_run_failed(GraphRunFailureInfo info)
{
    m_running = false;

    std::string error_msg = "Simulation failed at node '" + info.node_name() +
        "': " + info.node_run_failure_info().message();

    EM_ASM({ console.error('[C++] on_run_failed():', UTF8ToString($0)); }, error_msg.c_str());

    // Call JavaScript to reject the promise
    emscripten::val::module_property("_rejectRun")(
        reinterpret_cast<uintptr_t>(this),
        emscripten::val(error_msg)
    );
}

std::unique_ptr<NodeGraph> AvalancheSimulator::create_js_compute_graph()
{
    auto node_graph = std::make_unique<NodeGraph>("js_avalanche_compute_graph");
    const auto& manager = *m_context->pipeline_manager();

    // Input nodes (memory-based)
    auto heightmap_node = std::make_unique<nodes::LoadTextureFromMemoryNode>(m_device);
    auto release_cells_node = std::make_unique<nodes::LoadTextureFromMemoryNode>(m_device);
    auto aabb_node = std::make_unique<nodes::LoadAabbFromMemoryNode>();

    m_heightmap_node = heightmap_node.get();
    m_release_cells_node = release_cells_node.get();
    m_aabb_node = aabb_node.get();

    node_graph->add_node("load_heights_node", std::move(heightmap_node));
    node_graph->add_node("load_rp_node", std::move(release_cells_node));
    node_graph->add_node("load_aabb_node", std::move(aabb_node));

    // Height decode node
    HeightDecodeNode::HeightDecodeSettings height_decode_settings = {
        .texture_usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
                         WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc,
    };
    auto height_decode_node = std::make_unique<HeightDecodeNode>(manager, m_device, height_decode_settings);
    height_decode_node->input_socket("region aabb").connect(m_aabb_node->output_socket("region aabb"));
    height_decode_node->input_socket("encoded texture").connect(m_heightmap_node->output_socket("texture"));
    HeightDecodeNode* height_decode_ptr = height_decode_node.get();
    node_graph->add_node("height_decode_node", std::move(height_decode_node));

    // Normals compute node
    ComputeNormalsNode::NormalSettings normals_settings {
        .format = WGPUTextureFormat_RGBA8Unorm,
        .usage = static_cast<WGPUTextureUsage>(WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
                                               WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc),
    };
    auto normal_compute_node = std::make_unique<ComputeNormalsNode>(manager, m_device);
    normal_compute_node->set_settings(normals_settings);
    normal_compute_node->input_socket("bounds").connect(m_aabb_node->output_socket("region aabb"));
    normal_compute_node->input_socket("height texture").connect(height_decode_ptr->output_socket("decoded texture"));
    ComputeNormalsNode* normal_compute_ptr = normal_compute_node.get();
    node_graph->add_node("compute_normals_node", std::move(normal_compute_node));

    // Trajectories compute node
    auto trajectories_node = std::make_unique<ComputeAvalancheTrajectoriesNode>(manager, m_device);
    trajectories_node->input_socket("region aabb").connect(m_aabb_node->output_socket("region aabb"));
    trajectories_node->input_socket("normal texture").connect(normal_compute_ptr->output_socket("normal texture"));
    trajectories_node->input_socket("height texture").connect(height_decode_ptr->output_socket("decoded texture"));
    trajectories_node->input_socket("release point texture").connect(m_release_cells_node->output_socket("texture"));
    ComputeAvalancheTrajectoriesNode* trajectories_ptr = trajectories_node.get();
    node_graph->add_node("compute_avalanche_trajectories_node", std::move(trajectories_node));

    // Buffer to texture node (for color-mapped output)
    BufferToTextureNode::BufferToTextureSettings buffer_to_texture_settings {
        .texture_format = WGPUTextureFormat_RGBA8Unorm,
        .texture_usage = static_cast<WGPUTextureUsage>(WGPUTextureUsage_StorageBinding |
                                                       WGPUTextureUsage_TextureBinding |
                                                       WGPUTextureUsage_CopySrc),
    };
    auto buffer_to_texture_node = std::make_unique<BufferToTextureNode>(manager, m_device, buffer_to_texture_settings);
    buffer_to_texture_node->input_socket("raster dimensions").connect(trajectories_ptr->output_socket("raster dimensions"));
    buffer_to_texture_node->input_socket("storage buffer").connect(trajectories_ptr->output_socket("layer1_zdelta"));
    buffer_to_texture_node->input_socket("transparency buffer").connect(trajectories_ptr->output_socket("layer2_cellCounts"));
    BufferToTextureNode* buffer_to_texture_ptr = buffer_to_texture_node.get();
    node_graph->add_node("buffer_to_texture_node", std::move(buffer_to_texture_node));

    // Texture readback node (for reading color-mapped output back to CPU)
    auto texture_readback_node = std::make_unique<nodes::TextureReadbackNode>(m_device);
    texture_readback_node->input_socket("texture").connect(buffer_to_texture_ptr->output_socket("texture"));
    m_texture_readback_node = texture_readback_node.get();
    node_graph->add_node("texture_readback_node", std::move(texture_readback_node));

    // Store trajectories node pointer for output collection
    m_trajectories_node = trajectories_ptr;

    // Compute topological ordering for run_sync() - we don't use Qt signals in Emscripten
    EM_ASM({ console.log('[C++] Computing topological ordering...'); });
    node_graph->connect_node_signals_and_slots();  // This computes topological ordering

    // Note: We don't connect Qt signals here because they don't work in Emscripten.
    // Instead, we use run_sync() which directly calls nodes in order, and
    // on_run_completed() is called explicitly after run_sync() returns.

    EM_ASM({ console.log('[C++] Node graph created'); });

    return node_graph;
}

bool AvalancheSimulator::configure_input(emscripten::val input)
{
    try {
        EM_ASM({ console.log('[C++] configure_input() called'); });

        // Parse AABB
        emscripten::val aabb_js = input["aabb"];
        nodes::LoadAabbFromMemoryNode::Settings aabb_settings;
        aabb_settings.min_x = aabb_js["minX"].as<double>();
        aabb_settings.min_y = aabb_js["minY"].as<double>();
        aabb_settings.max_x = aabb_js["maxX"].as<double>();
        aabb_settings.max_y = aabb_js["maxY"].as<double>();
        m_aabb_node->set_settings(aabb_settings);
        EM_ASM({ console.log('[C++] AABB parsed'); });

        // Parse heightmap
        emscripten::val heightmap_js = input["heightmap"];
        nodes::LoadTextureFromMemoryNode::Settings heightmap_settings;
        heightmap_settings.width = heightmap_js["width"].as<uint32_t>();
        heightmap_settings.height = heightmap_js["height"].as<uint32_t>();
        EM_ASM({ console.log('[C++] Heightmap dimensions:', $0, 'x', $1); },
               heightmap_settings.width, heightmap_settings.height);

        // Calculate expected size from dimensions
        size_t heightmap_size = heightmap_settings.width * heightmap_settings.height * 4; // RGBA
        heightmap_settings.data.resize(heightmap_size);

        emscripten::val heightmap_data = heightmap_js["data"];
        // Use vecFromJSArray for efficient copy
        std::vector<uint8_t> heightmap_vec = emscripten::vecFromJSArray<uint8_t>(heightmap_data);
        heightmap_settings.data = std::move(heightmap_vec);
        m_heightmap_node->set_settings(heightmap_settings);
        EM_ASM({ console.log('[C++] Heightmap parsed, size:', $0); }, heightmap_settings.data.size());

        // Parse release cells
        emscripten::val release_cells_js = input["releaseCells"];
        nodes::LoadTextureFromMemoryNode::Settings release_cells_settings;
        release_cells_settings.width = release_cells_js["width"].as<uint32_t>();
        release_cells_settings.height = release_cells_js["height"].as<uint32_t>();
        // Release cells use RGBA8Unorm format
        release_cells_settings.format = WGPUTextureFormat_RGBA8Unorm;
        EM_ASM({ console.log('[C++] Release cells dimensions:', $0, 'x', $1); },
               release_cells_settings.width, release_cells_settings.height);

        emscripten::val release_cells_data = release_cells_js["data"];
        std::vector<uint8_t> release_cells_vec = emscripten::vecFromJSArray<uint8_t>(release_cells_data);
        release_cells_settings.data = std::move(release_cells_vec);
        m_release_cells_node->set_settings(release_cells_settings);
        EM_ASM({ console.log('[C++] Release cells parsed, size:', $0); }, release_cells_settings.data.size());

        EM_ASM({ console.log('[C++] configure_input() complete'); });
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Failed to parse input:" << e.what();
        EM_ASM({ console.error('[C++] configure_input() failed:', UTF8ToString($0)); }, e.what());
        return false;
    }
}

void AvalancheSimulator::configure_settings(emscripten::val settings)
{
    auto& trajectories_node = m_node_graph->get_node_as<ComputeAvalancheTrajectoriesNode>("compute_avalanche_trajectories_node");
    ComputeAvalancheTrajectoriesNode::AvalancheTrajectoriesSettings traj_settings = trajectories_node.get_settings();

    // Set conservative defaults for browser environment to avoid GPU hangs
    traj_settings.resolution_multiplier = 1;  // Don't upsample
    traj_settings.num_runs = 1;
    traj_settings.num_paths_per_release_cell = 64;  // Reduced from 1024
    traj_settings.num_steps = 1000;  // Reduced from 10000

    // Parse settings from JavaScript object (overrides defaults)
    if (settings.hasOwnProperty("resolutionMultiplier")) {
        traj_settings.resolution_multiplier = settings["resolutionMultiplier"].as<uint32_t>();
    }
    if (settings.hasOwnProperty("numSimulationRuns")) {
        traj_settings.num_runs = settings["numSimulationRuns"].as<uint32_t>();
    }
    if (settings.hasOwnProperty("numParticlesPerReleaseCell")) {
        traj_settings.num_paths_per_release_cell = settings["numParticlesPerReleaseCell"].as<uint32_t>();
    }
    if (settings.hasOwnProperty("numSimulationSteps")) {
        traj_settings.num_steps = settings["numSimulationSteps"].as<uint32_t>();
    }
    if (settings.hasOwnProperty("simulationStepLength")) {
        traj_settings.step_length = settings["simulationStepLength"].as<float>();
    }
    if (settings.hasOwnProperty("randomSeed")) {
        traj_settings.random_seed = settings["randomSeed"].as<uint32_t>();
    }
    if (settings.hasOwnProperty("maxRandomDeviation")) {
        traj_settings.random_contribution = settings["maxRandomDeviation"].as<float>();
    }
    if (settings.hasOwnProperty("persistence")) {
        traj_settings.persistence_contribution = settings["persistence"].as<float>();
    }
    if (settings.hasOwnProperty("maxRunoutAngle")) {
        traj_settings.runout_flowpy.alpha = glm::radians(settings["maxRunoutAngle"].as<float>());
    }

    EM_ASM({ console.log('[C++] Trajectory settings: particles=' + $0 + ', steps=' + $1 + ', resolution=' + $2); },
           traj_settings.num_paths_per_release_cell, traj_settings.num_steps, traj_settings.resolution_multiplier);

    trajectories_node.set_settings(traj_settings);
}

emscripten::val AvalancheSimulator::collect_output()
{
    emscripten::val output = emscripten::val::object();

    output.set("success", true);
    output.set("message", emscripten::val("Simulation completed successfully"));

    // Check if we have texture readback data
    if (m_texture_readback_node && !m_texture_readback_node->get_data().empty()) {
        const auto& data = m_texture_readback_node->get_data();
        uint32_t width = m_texture_readback_node->get_width();
        uint32_t height = m_texture_readback_node->get_height();

        EM_ASM({ console.log('[C++] Texture readback data available:', $0, 'x', $1, ', size:', $2); },
               width, height, data.size());

        output.set("width", width);
        output.set("height", height);

        // Create a Uint8Array from the data
        // Use typed_memory_view to create a view, then copy to a new Uint8Array
        emscripten::val memory_view = emscripten::val(emscripten::typed_memory_view(data.size(), data.data()));
        emscripten::val uint8_array = emscripten::val::global("Uint8Array").new_(memory_view);
        output.set("imageData", uint8_array);

        EM_ASM({ console.log('[C++] Image data set in output'); });
    } else {
        EM_ASM({ console.log('[C++] No texture readback data available'); });
    }

    return output;
}

} // namespace webigeo_js

// Helper functions for Promise-based async operations (called from JavaScript)
extern "C" {

EMSCRIPTEN_KEEPALIVE
void webigeo_init_callback(uintptr_t simulator_ptr, bool success, const char* error_msg)
{
    auto* simulator = reinterpret_cast<webigeo_js::AvalancheSimulator*>(simulator_ptr);
    // This would be called after WebGPU initialization
    // Implementation depends on how we handle async WebGPU init
}

EMSCRIPTEN_KEEPALIVE
void webigeo_run_callback(uintptr_t simulator_ptr)
{
    auto* simulator = reinterpret_cast<webigeo_js::AvalancheSimulator*>(simulator_ptr);
    // Start the node graph execution
    // The run_completed/run_failed signals will handle promise resolution
}

} // extern "C"
