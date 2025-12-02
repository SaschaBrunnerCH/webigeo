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

#include "BufferReadbackNode.h"

#include <QDebug>
#include <glm/glm.hpp>
#include <limits>

using namespace webgpu_engine::compute::nodes;

namespace webigeo_js::nodes {

BufferReadbackNode::BufferReadbackNode(WGPUDevice device)
    : Node(
          {
              InputSocket(*this, "buffer", data_type<webgpu::raii::RawBuffer<uint32_t>*>()),
              InputSocket(*this, "dimensions", data_type<glm::uvec2>()),
          },
          {})
    , m_device(device)
{
}

std::vector<float> BufferReadbackNode::get_data_as_float() const
{
    std::vector<float> result;
    result.reserve(m_readback_data.size());

    // Same encoding as BufferExportNode
    const float FLOAT_MIN_ENCODING = -10000.0f;
    const float FLOAT_MAX_ENCODING = 10000.0f;

    for (uint32_t value : m_readback_data) {
        // Decode: uint32 -> normalized -> float in [-10000, 10000]
        double normalized = static_cast<double>(value) / static_cast<double>(std::numeric_limits<uint32_t>::max());
        float decoded = static_cast<float>(normalized * (FLOAT_MAX_ENCODING - FLOAT_MIN_ENCODING) + FLOAT_MIN_ENCODING);
        result.push_back(decoded);
    }

    return result;
}

void BufferReadbackNode::run_impl()
{
    qDebug() << "running BufferReadbackNode ...";

    m_dimensions = std::get<data_type<glm::uvec2>()>(input_socket("dimensions").get_connected_data());
    auto& buffer = *std::get<data_type<webgpu::raii::RawBuffer<uint32_t>*>()>(input_socket("buffer").get_connected_data());

    // Check if buffer size matches expected dimensions
    const size_t expected_size = static_cast<size_t>(m_dimensions.x) * m_dimensions.y;
    if (buffer.size() != expected_size) {
        qWarning() << "Buffer size mismatch. Expected:" << expected_size << "Got:" << buffer.size();
        m_readback_data.clear();
        emit this->run_completed();
        return;
    }

    buffer.read_back_async(m_device, [this](WGPUMapAsyncStatus status, std::vector<uint32_t> data) {
        if (status != WGPUMapAsyncStatus_Success) {
            qWarning() << "Buffer readback failed with status:" << status;
            m_readback_data.clear();
            emit this->run_completed();
            return;
        }

        // Verify data size matches expected dimensions
        const size_t expected = static_cast<size_t>(m_dimensions.x) * m_dimensions.y;
        if (data.size() != expected) {
            qWarning() << "Readback data size mismatch. Expected:" << expected << "Got:" << data.size();
            m_readback_data.clear();
            emit this->run_completed();
            return;
        }

        qDebug() << "Buffer readback successful:" << data.size() << "elements";
        m_readback_data = std::move(data);
        emit this->run_completed();
    });
}

} // namespace webigeo_js::nodes
