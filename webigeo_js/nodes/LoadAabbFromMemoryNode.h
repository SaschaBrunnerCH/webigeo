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
#include <radix/geometry.h>

namespace webigeo_js::nodes {

/**
 * Node that provides an AABB from memory (direct values).
 * Similar to LoadRegionAabbNode but accepts values directly instead of file path.
 */
class LoadAabbFromMemoryNode : public webgpu_engine::compute::nodes::Node {
    Q_OBJECT

public:
    struct Settings {
        double min_x = 0.0;
        double min_y = 0.0;
        double max_x = 0.0;
        double max_y = 0.0;
    };

    LoadAabbFromMemoryNode();
    LoadAabbFromMemoryNode(const Settings& settings);

    void set_settings(const Settings& settings);
    const Settings& get_settings() const { return m_settings; }

public slots:
    void run_impl() override;

private:
    Settings m_settings;
    radix::geometry::Aabb<2, double> m_output_bounds;
};

} // namespace webigeo_js::nodes
