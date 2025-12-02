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

#include "LoadTextureFromMemoryNode.h"

#include <QDebug>
#include <format>

using namespace webgpu_engine::compute::nodes;

namespace webigeo_js::nodes {

LoadTextureFromMemoryNode::LoadTextureFromMemoryNode(WGPUDevice device)
    : LoadTextureFromMemoryNode(device, Settings())
{
}

LoadTextureFromMemoryNode::LoadTextureFromMemoryNode(WGPUDevice device, const Settings& settings)
    : Node({},
          {
              OutputSocket(*this, "texture", data_type<const webgpu::raii::TextureWithSampler*>(),
                  [this]() { return m_output_texture.get(); }),
          })
    , m_device(device)
    , m_queue(wgpuDeviceGetQueue(device))
    , m_settings(settings)
{
}

void LoadTextureFromMemoryNode::set_settings(const Settings& settings)
{
    m_settings = settings;
}

void LoadTextureFromMemoryNode::run_impl()
{
    qDebug() << "running LoadTextureFromMemoryNode ...";

    // Validate input
    if (m_settings.width == 0 || m_settings.height == 0) {
        emit run_failed(NodeRunFailureInfo(*this, "Invalid texture dimensions (width or height is 0)"));
        return;
    }

    const size_t expected_size = static_cast<size_t>(m_settings.width) * m_settings.height * 4;
    if (m_settings.data.size() != expected_size) {
        emit run_failed(NodeRunFailureInfo(*this,
            std::format("Data size mismatch: expected {} bytes ({}x{}x4), got {} bytes",
                expected_size, m_settings.width, m_settings.height, m_settings.data.size())));
        return;
    }

    qDebug() << "Creating texture from memory:" << m_settings.width << "x" << m_settings.height;

    // Create texture
    m_output_texture = create_texture(m_device, m_settings.width, m_settings.height,
        m_settings.format, m_settings.usage);

    // Write data to texture
    WGPUExtent3D write_size = { m_settings.width, m_settings.height, 1 };
    WGPUTexelCopyBufferLayout data_layout = {};
    data_layout.offset = 0;
    data_layout.bytesPerRow = m_settings.width * 4;
    data_layout.rowsPerImage = m_settings.height;

    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_output_texture->texture().handle();
    destination.mipLevel = 0;
    destination.origin = { 0, 0, 0 };
    destination.aspect = WGPUTextureAspect_All;

    wgpuQueueWriteTexture(m_queue, &destination, m_settings.data.data(),
        m_settings.data.size(), &data_layout, &write_size);

    emit run_completed();
}

std::unique_ptr<webgpu::raii::TextureWithSampler> LoadTextureFromMemoryNode::create_texture(
    WGPUDevice device, uint32_t width, uint32_t height,
    WGPUTextureFormat format, WGPUTextureUsage usage)
{
    WGPUTextureDescriptor texture_desc = {};
    texture_desc.label = WGPUStringView { .data = "LoadTextureFromMemoryNode output texture", .length = WGPU_STRLEN };
    texture_desc.dimension = WGPUTextureDimension_2D;
    texture_desc.size = { width, height, 1 };
    texture_desc.mipLevelCount = 1;
    texture_desc.sampleCount = 1;
    texture_desc.format = format;
    texture_desc.usage = usage;

    WGPUSamplerDescriptor sampler_desc = {};
    sampler_desc.label = WGPUStringView { .data = "LoadTextureFromMemoryNode sampler", .length = WGPU_STRLEN };
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeW = WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    sampler_desc.lodMinClamp = 0.0f;
    sampler_desc.lodMaxClamp = 1.0f;
    sampler_desc.compare = WGPUCompareFunction_Undefined;
    sampler_desc.maxAnisotropy = 1;

    return std::make_unique<webgpu::raii::TextureWithSampler>(device, texture_desc, sampler_desc);
}

} // namespace webigeo_js::nodes
