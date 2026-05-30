#include "simulation.h"
#include <algorithm>

// ============================================================
// SYSTEM: Conveyor Transport
// Moves resources along conveyor chains each tick.
// Process downstream-first to avoid double-moving.
// ============================================================

void Simulation::system_conveyor_transport() {
    float decay = config_.conveyor_decay_rate;
    float throughput = config_.conveyor_throughput;

    // Collect all built conveyor tiles
    struct ConvTile { int x, y; int dist_to_exit; };
    std::vector<ConvTile> convs;

    int exit_x = grid_.width() - 1;
    int mid_y  = grid_.height() / 2;

    for (int y = 1; y < grid_.height() - 1; y++)
        for (int x = 1; x < grid_.width() - 1; x++) {
            if (grid_.at(x, y) != TileType::Conveyor) continue;
            auto& d = grid_.data_at(x, y);
            if (!d.built) continue;
            int dist = std::abs(x - exit_x) + std::abs(y - mid_y);
            convs.push_back({x, y, dist});
        }

    // Process nearest-to-Exit first so items don't double-move
    std::sort(convs.begin(), convs.end(),
        [](const ConvTile& a, const ConvTile& b) { return a.dist_to_exit < b.dist_to_exit; });

    for (auto& c : convs) {
        auto& d = grid_.data_at(c.x, c.y);

        // Degrade condition
        d.conveyor_condition = std::max(0.0f, d.conveyor_condition - decay);
        if (d.conveyor_condition < 0.2f) continue;  // broken

        if (d.conveyor_contents < 0.001f) continue;  // empty belt

        auto [tx, ty] = grid_.conveyor_target(c.x, c.y);
        TileType target = grid_.at(tx, ty);
        float amount = std::min(d.conveyor_contents, throughput);

        if (target == TileType::Storage) {
            auto& sd = grid_.data_at(tx, ty);
            float room = sd.storage_capacity - sd.stored_food - sd.stored_raw_food - sd.stored_raw_material;
            float deposit = std::min(amount, room);
            if (deposit > 0.0f) {
                if (d.conveyor_contents_type == ResourceType::FOOD)
                    sd.stored_food += deposit;
                else if (d.conveyor_contents_type == ResourceType::RAW_MATERIAL)
                    sd.stored_raw_material += deposit;
                else
                    sd.stored_raw_food += deposit;
                d.conveyor_contents -= deposit;
            }
        } else if (target == TileType::Exit) {
            total_food_shipped_ += amount;
            d.conveyor_contents -= amount;
        } else if (target == TileType::Conveyor) {
            auto& nd = grid_.data_at(tx, ty);
            if (!nd.built || nd.conveyor_condition < 0.2f) continue;
            float room = throughput - nd.conveyor_contents;
            float transfer = std::min(amount, std::max(0.0f, room));
            if (transfer > 0.0f) {
                nd.conveyor_contents += transfer;
                nd.conveyor_contents_type = d.conveyor_contents_type;
                d.conveyor_contents -= transfer;
            }
        }
        // Floor/Wall/Machine: contents stay, belt backs up
    }
}
