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
 * Node that creates a GPU texture from raw RGBA8 memory data.
 * Similar to LoadTextureNode but accepts data directly instead of file path.
 */
class LoadTextureFromMemoryNode : public webgpu_engine::compute::nodes::Node {
    Q_OBJECT

public:
    struct Settings {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> data; // RGBA8 data, size = width * height * 4

        // WebGPU texture parameters
        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Uint;
        WGPUTextureUsage usage = static_cast<WGPUTextureUsage>(
            WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc);
    };

    LoadTextureFromMemoryNode(WGPUDevice device);
    LoadTextureFromMemoryNode(WGPUDevice device, const Settings& settings);

    void set_settings(const Settings& settings);
    const Settings& get_settings() const { return m_settings; }

public slots:
    void run_impl() override;

private:
    static std::unique_ptr<webgpu::raii::TextureWithSampler> create_texture(
        WGPUDevice device, uint32_t width, uint32_t height,
        WGPUTextureFormat format, WGPUTextureUsage usage);

private:
    WGPUDevice m_device;
    WGPUQueue m_queue;
    Settings m_settings;
    std::unique_ptr<webgpu::raii::TextureWithSampler> m_output_texture;
};

} // namespace webigeo_js::nodes
