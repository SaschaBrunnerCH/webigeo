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

#include "TextureReadbackNode.h"

#include <QDebug>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace webgpu_engine::compute::nodes;

namespace webigeo_js::nodes {

TextureReadbackNode::TextureReadbackNode(WGPUDevice device)
    : Node(
          {
              InputSocket(*this, "texture", data_type<const webgpu::raii::TextureWithSampler*>()),
          },
          {})
    , m_device(device)
    , m_queue(wgpuDeviceGetQueue(device))
{
}

void TextureReadbackNode::run_impl()
{
    qDebug() << "running TextureReadbackNode ...";

    const auto* texture_with_sampler = std::get<data_type<const webgpu::raii::TextureWithSampler*>()>(
        input_socket("texture").get_connected_data());

    if (!texture_with_sampler) {
        qWarning() << "TextureReadbackNode: No input texture";
        m_readback_data.clear();
        emit run_completed();
        return;
    }

    WGPUTexture texture = texture_with_sampler->texture().handle();

    // Get texture dimensions
    m_width = wgpuTextureGetWidth(texture);
    m_height = wgpuTextureGetHeight(texture);

    qDebug() << "TextureReadbackNode: Reading texture" << m_width << "x" << m_height;

    // Calculate buffer size with proper row alignment
    // WebGPU requires bytesPerRow to be a multiple of 256
    const uint32_t bytes_per_pixel = 4; // RGBA8
    const uint32_t unpadded_bytes_per_row = m_width * bytes_per_pixel;
    const uint32_t align = 256;
    const uint32_t padded_bytes_per_row = (unpadded_bytes_per_row + align - 1) / align * align;
    const size_t buffer_size = static_cast<size_t>(padded_bytes_per_row) * m_height;

    // Create staging buffer for readback
    WGPUBufferDescriptor buffer_desc = {};
    buffer_desc.label = WGPUStringView { .data = "texture readback staging buffer", .length = WGPU_STRLEN };
    buffer_desc.size = buffer_size;
    buffer_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    buffer_desc.mappedAtCreation = false;

    WGPUBuffer staging_buffer = wgpuDeviceCreateBuffer(m_device, &buffer_desc);

    // Copy texture to staging buffer
    WGPUCommandEncoderDescriptor encoder_desc = {};
    encoder_desc.label = WGPUStringView { .data = "texture readback command encoder", .length = WGPU_STRLEN };
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encoder_desc);

    WGPUTexelCopyTextureInfo source = {};
    source.texture = texture;
    source.mipLevel = 0;
    source.origin = { 0, 0, 0 };
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = {};
    destination.buffer = staging_buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = padded_bytes_per_row;
    destination.layout.rowsPerImage = m_height;

    WGPUExtent3D copy_size = { m_width, m_height, 1 };

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copy_size);

    WGPUCommandBufferDescriptor cmd_buffer_desc = {};
    cmd_buffer_desc.label = WGPUStringView { .data = "texture readback command buffer", .length = WGPU_STRLEN };
    WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(encoder, &cmd_buffer_desc);

    wgpuQueueSubmit(m_queue, 1, &command_buffer);
    wgpuCommandBufferRelease(command_buffer);
    wgpuCommandEncoderRelease(encoder);

    // Store readback info for later retrieval
    // Since wgpuBufferMapAsync callbacks don't fire reliably in Emscripten,
    // we store the buffer and dimensions so collect_output() can try to read it
    m_staging_buffer = staging_buffer;
    m_buffer_size = buffer_size;
    m_padded_bytes_per_row = padded_bytes_per_row;
    m_unpadded_bytes_per_row = unpadded_bytes_per_row;
    m_readback_pending = true;

    qDebug() << "TextureReadbackNode: Copy submitted, buffer stored for later readback";

    emit run_completed();
}

bool TextureReadbackNode::try_complete_readback()
{
    // C++ polling doesn't work in browser due to ASYNCIFY limitations.
    // Use JavaScript-based readback via get_staging_buffer() instead.
    // This method is kept for potential native builds.

    if (!m_readback_pending || !m_staging_buffer) {
        return false;
    }

    qDebug() << "TextureReadbackNode: C++ readback skipped - use JavaScript readback instead";

    // Don't clean up the buffer - JavaScript will use it
    return false;
}

void TextureReadbackNode::set_readback_data(std::vector<uint8_t> data)
{
    m_readback_data = std::move(data);
    m_readback_pending = false;

    qDebug() << "TextureReadbackNode: Received" << m_readback_data.size() << "bytes from JavaScript";

    // Clean up staging buffer now that we have the data
    if (m_staging_buffer) {
        wgpuBufferRelease(m_staging_buffer);
        m_staging_buffer = nullptr;
    }
}

void TextureReadbackNode::cleanup_staging_buffer()
{
    if (m_staging_buffer) {
        wgpuBufferRelease(m_staging_buffer);
        m_staging_buffer = nullptr;
    }
    m_readback_pending = false;
}

} // namespace webigeo_js::nodes
