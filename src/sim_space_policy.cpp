#include "simulation.h"

#include <algorithm>
#include <string>

void Simulation::system_space_overcapacity() {
    for (int y = 0; y < grid_.height(); y++)
        for (int x = 0; x < grid_.width(); x++) {
            auto& data = grid_.data_at(x, y);
            if (data.occupancy_capacity <= 0) continue;

            int occupants = 0;
            auto alive_view = registry_.view<PositionComponent, const AgentComponent>();
            for (auto entity : alive_view) {
                if (!registry_.get<AgentComponent>(entity).alive) continue;
                const auto& position = registry_.get<PositionComponent>(entity);
                if (position.x == x && position.y == y) occupants++;
            }

            if (occupants > data.occupancy_capacity) {
                data.overcapacity_ticks++;
            } else {
                data.overcapacity_ticks = std::max(0, data.overcapacity_ticks - 1);
            }

            if (data.overcapacity_ticks < 10) continue;
            grid_.set(x, y, TileType::Floor);
            data.occupancy_capacity = 0;
            data.overcapacity_ticks = 0;
            space_closures_++;
            total_restructures_++;
            emit_log(-1, "SAFETY closure at (" + std::to_string(x) + ","
                + std::to_string(y) + ") after sustained overcapacity",
                EventType::FACTORY_RESTRUCTURE);
        }
}
