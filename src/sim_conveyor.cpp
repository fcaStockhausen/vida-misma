#include "simulation.h"
#include <algorithm>

// ============================================================
// SYSTEM: Conveyor Transport
// Moves resources along conveyor chains each tick.
// Process downstream-first (nearest to Exit first) to avoid double-moving.
// ============================================================

void Simulation::system_conveyor_transport() {
    float decay = config_.conveyor_decay_rate;
    float throughput = config_.conveyor_throughput;

    // Find actual Exit tile positions (not hardcoded estimate)
    auto exits = grid_.find_all(TileType::Exit);
    if (exits.empty()) return;

    // Collect all built conveyor tiles
    struct ConvTile { int x, y; int min_dist_to_exit; };
    std::vector<ConvTile> convs;

    for (int y = 1; y < grid_.height() - 1; y++)
        for (int x = 1; x < grid_.width() - 1; x++) {
            if (grid_.at(x, y) != TileType::Conveyor) continue;
            auto& d = grid_.data_at(x, y);
            if (!d.built) continue;
            // Distance to nearest Exit tile
            int best_dist = 999999;
            for (auto& [ex, ey] : exits) {
                int dist = std::abs(x - ex) + std::abs(y - ey);
                if (dist < best_dist) best_dist = dist;
            }
            convs.push_back({x, y, best_dist});
        }

    // Process nearest-to-Exit first so items don't double-move
    std::sort(convs.begin(), convs.end(),
        [](const ConvTile& a, const ConvTile& b) { return a.min_dist_to_exit < b.min_dist_to_exit; });

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
            // Deposit into Storage ONLY if this Storage is near an Exit.
            // Output must reach Exit-adjacent Storage to count for quota.
            // Depositing in intermediate Storage traps the output.
            auto& sd = grid_.data_at(tx, ty);
            bool near_exit = false;
            for (auto& [ex, ey] : exits) {
                if (std::abs(tx - ex) + std::abs(ty - ey) <= 3) {
                    near_exit = true;
                    break;
                }
            }
            if (!near_exit) {
                // Not near Exit — only accept non-output resources (food, materials)
                if (d.conveyor_contents_type != ResourceType::OUTPUT) {
                    float room = sd.storage_capacity - sd.stored_food - sd.stored_raw_food - sd.stored_raw_material
                               - sd.stored_output - sd.stored_construction_material;
                    float deposit = std::min(amount, room);
                    if (deposit > 0.0f) {
                        if (d.conveyor_contents_type == ResourceType::FOOD)
                            sd.stored_food += deposit;
                        else if (d.conveyor_contents_type == ResourceType::RAW_MATERIAL)
                            sd.stored_raw_material += deposit;
                        else if (d.conveyor_contents_type == ResourceType::CONSTRUCTION_MATERIAL)
                            sd.stored_construction_material += deposit;
                        else
                            sd.stored_raw_food += deposit;
                        d.conveyor_contents -= deposit;
                    }
                }
                // Output on belt continues past non-Exit Storage
            } else {
                // Near Exit — accept all resources including output
                float room = sd.storage_capacity - sd.stored_food - sd.stored_raw_food - sd.stored_raw_material
                           - sd.stored_output - sd.stored_construction_material;
                float deposit = std::min(amount, room);
                if (deposit > 0.0f) {
                    if (d.conveyor_contents_type == ResourceType::FOOD)
                        sd.stored_food += deposit;
                    else if (d.conveyor_contents_type == ResourceType::RAW_MATERIAL)
                        sd.stored_raw_material += deposit;
                    else if (d.conveyor_contents_type == ResourceType::OUTPUT)
                        sd.stored_output += deposit;
                    else if (d.conveyor_contents_type == ResourceType::CONSTRUCTION_MATERIAL)
                        sd.stored_construction_material += deposit;
                    else
                        sd.stored_raw_food += deposit;
                    d.conveyor_contents -= deposit;
                }
            }
        } else if (target == TileType::Exit) {
            // Conveyor dumps into Exit — deposit into adjacent Storage instead
            // so the quota system (system_ship_out_food) can pick it up.
            // If no adjacent Storage, contents stay on belt (backs up).
            bool deposited = false;
            constexpr int ddx[] = {1, -1, 0, 0};
            constexpr int ddy[] = {0, 0, 1, -1};
            for (int i = 0; i < 4 && amount > 0.001f; i++) {
                int sx = tx + ddx[i], sy = ty + ddy[i];
                if (grid_.at(sx, sy) != TileType::Storage) continue;
                auto& sd = grid_.data_at(sx, sy);
                float room = sd.storage_capacity - sd.stored_food - sd.stored_raw_food - sd.stored_raw_material
                           - sd.stored_output - sd.stored_construction_material;
                float dep = std::min(amount, room);
                if (dep > 0.0f) {
                    if (d.conveyor_contents_type == ResourceType::OUTPUT)
                        sd.stored_output += dep;
                    else if (d.conveyor_contents_type == ResourceType::FOOD)
                        sd.stored_food += dep;
                    else if (d.conveyor_contents_type == ResourceType::CONSTRUCTION_MATERIAL)
                        sd.stored_construction_material += dep;
                    else
                        sd.stored_raw_material += dep;
                    amount -= dep;
                    deposited = true;
                }
            }
            if (deposited) {
                d.conveyor_contents -= std::min(d.conveyor_contents, throughput);
            }
            // If no adjacent Storage, contents stay on belt — will try again next tick
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
