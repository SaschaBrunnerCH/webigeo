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

#include <webgpu_engine/compute/nodes/Node.h>
#include <webgpu/raii/TextureWithSampler.h>

#include <vector>

namespace webigeo_js::nodes {

/**
 * Node that reads back a GPU texture to CPU memory as RGBA8 data.
 * Copies the texture to a staging buffer, then reads it via buffer mapping.
 *
 * Due to Emscripten async callback issues, the readback is done in two phases:
 * 1. run_impl() - copies texture to staging buffer and submits to GPU
 * 2. try_complete_readback() - polls for buffer mapping completion and copies data
 *
 * Call try_complete_readback() after run_sync() completes to get the data.
 */
class TextureReadbackNode : public webgpu_engine::compute::nodes::Node {
    Q_OBJECT

public:
    TextureReadbackNode(WGPUDevice device);
    ~TextureReadbackNode() { cleanup_staging_buffer(); }

    /**
     * Get the readback data after try_complete_readback() succeeds.
     * Data is RGBA8 format, size = width * height * 4 bytes.
     */
    const std::vector<uint8_t>& get_data() const { return m_readback_data; }

    /**
     * Get dimensions of the texture.
     */
    uint32_t get_width() const { return m_width; }
    uint32_t get_height() const { return m_height; }

    /**
     * Check if readback is pending (texture copied but not yet read back).
     */
    bool is_readback_pending() const { return m_readback_pending; }

    /**
     * Get staging buffer info for JavaScript-based readback.
     * Returns the raw WGPUBuffer handle that can be used with JS WebGPU API.
     */
    WGPUBuffer get_staging_buffer() const { return m_staging_buffer; }
    size_t get_buffer_size() const { return m_buffer_size; }
    uint32_t get_padded_bytes_per_row() const { return m_padded_bytes_per_row; }
    uint32_t get_unpadded_bytes_per_row() const { return m_unpadded_bytes_per_row; }

    /**
     * Set the readback data from JavaScript after async buffer mapping completes.
     * @param data The RGBA8 pixel data (width * height * 4 bytes)
     */
    void set_readback_data(std::vector<uint8_t> data);

    /**
     * Attempt to complete the readback operation (C++ polling - may not work in browser).
     * For browser use, prefer JavaScript-based readback via get_staging_buffer().
     * @return true if readback succeeded, false otherwise.
     */
    bool try_complete_readback();

public slots:
    void run_impl() override;

private:
    void cleanup_staging_buffer();

private:
    WGPUDevice m_device;
    WGPUQueue m_queue;

    // Output data
    std::vector<uint8_t> m_readback_data;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // Staging buffer for async readback
    WGPUBuffer m_staging_buffer = nullptr;
    size_t m_buffer_size = 0;
    uint32_t m_padded_bytes_per_row = 0;
    uint32_t m_unpadded_bytes_per_row = 0;
    bool m_readback_pending = false;
};

} // namespace webigeo_js::nodes
