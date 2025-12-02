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

#include <cstdint>
#include <vector>

namespace webigeo_js {

/**
 * Axis-aligned bounding box in Web Mercator (EPSG:3857)
 */
struct AABB {
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;

    double width() const { return max_x - min_x; }
    double height() const { return max_y - min_y; }
};

/**
 * Raw texture data (RGBA8)
 */
struct TextureData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data; // RGBA8, size = width * height * 4
};

/**
 * Simulation input data
 */
struct SimulationInput {
    AABB aabb;
    TextureData heightmap;      // Height encoded in R+G channels (16-bit)
    TextureData release_cells;  // Release cells mask (A > 0 = release)
};

/**
 * Simulation settings (matching CLI parameters)
 */
struct SimulationSettings {
    // Hyper parameters
    uint32_t resolution_multiplier = 1u;
    uint32_t num_simulation_runs = 1u;
    uint32_t num_particles_per_release_cell = 1024u;
    uint32_t num_simulation_steps = 10000u;
    float simulation_step_length = 0.1f;
    uint32_t random_seed = 1u;

    // Model parameters
    float max_random_deviation = 25.0f;  // degrees (theta)
    float persistence = 0.9f;            // [0.0, 1.0]
    float max_runout_angle = 25.0f;      // degrees (alpha)
};

/**
 * Simulation output data
 */
struct SimulationOutput {
    uint32_t width = 0;
    uint32_t height = 0;

    // Color-mapped trajectories (RGBA8)
    std::vector<uint8_t> trajectories;

    // Per-cell simulation layers (float32)
    std::vector<float> zdelta;            // Max Z-delta (velocity proxy)
    std::vector<float> cell_counts;       // Particle pass counts
    std::vector<float> travel_length;     // Max travel distance
    std::vector<float> travel_angle;      // Max local travel angle
    std::vector<float> height_difference; // Max altitude difference
};

} // namespace webigeo_js
