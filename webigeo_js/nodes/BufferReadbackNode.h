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
#include <webgpu/raii/RawBuffer.h>

#include <vector>

namespace webigeo_js::nodes {

/**
 * Node that reads back GPU buffer data to CPU memory.
 * Similar to BufferExportNode but stores data in memory instead of writing to file.
 */
class BufferReadbackNode : public webgpu_engine::compute::nodes::Node {
    Q_OBJECT

public:
    BufferReadbackNode(WGPUDevice device);

    /**
     * Get the readback data after run completed.
     * Data is stored as raw uint32_t values (same format as GPU buffer).
     */
    const std::vector<uint32_t>& get_data() const { return m_readback_data; }

    /**
     * Get the readback data converted to float values.
     * Uses the same encoding as BufferExportNode: [-10000, 10000] range.
     */
    std::vector<float> get_data_as_float() const;

    /**
     * Get dimensions of the buffer (from input socket).
     */
    glm::uvec2 get_dimensions() const { return m_dimensions; }

public slots:
    void run_impl() override;

private:
    WGPUDevice m_device;
    std::vector<uint32_t> m_readback_data;
    glm::uvec2 m_dimensions = { 0, 0 };
};

} // namespace webigeo_js::nodes
