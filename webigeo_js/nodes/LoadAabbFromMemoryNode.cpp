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

#include "LoadAabbFromMemoryNode.h"

#include <QDebug>
#include <format>

using namespace webgpu_engine::compute::nodes;

namespace webigeo_js::nodes {

LoadAabbFromMemoryNode::LoadAabbFromMemoryNode()
    : LoadAabbFromMemoryNode(Settings())
{
}

LoadAabbFromMemoryNode::LoadAabbFromMemoryNode(const Settings& settings)
    : Node({},
          {
              OutputSocket(*this, "region aabb", data_type<const radix::geometry::Aabb<2, double>*>(),
                  [this]() { return &m_output_bounds; }),
          })
    , m_settings(settings)
{
}

void LoadAabbFromMemoryNode::set_settings(const Settings& settings)
{
    m_settings = settings;
}

void LoadAabbFromMemoryNode::run_impl()
{
    qDebug() << "running LoadAabbFromMemoryNode ...";

    // Validate AABB
    if (m_settings.min_x >= m_settings.max_x) {
        emit run_failed(NodeRunFailureInfo(*this,
            std::format("Invalid AABB: min_x ({}) must be < max_x ({})",
                m_settings.min_x, m_settings.max_x)));
        return;
    }

    if (m_settings.min_y >= m_settings.max_y) {
        emit run_failed(NodeRunFailureInfo(*this,
            std::format("Invalid AABB: min_y ({}) must be < max_y ({})",
                m_settings.min_y, m_settings.max_y)));
        return;
    }

    qDebug() << "AABB:" << m_settings.min_x << m_settings.min_y
             << "to" << m_settings.max_x << m_settings.max_y;

    m_output_bounds = radix::geometry::Aabb<2, double> {
        { m_settings.min_x, m_settings.min_y },
        { m_settings.max_x, m_settings.max_y }
    };

    emit run_completed();
}

} // namespace webigeo_js::nodes
