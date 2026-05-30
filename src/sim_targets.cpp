#include "simulation.h"
#include <cstdlib>

void Simulation::system_find_targets() {
    auto view = registry_.view<ActionComponent, PositionComponent,
                               InventoryComponent, const AgentComponent>();
    for (auto e : view) {
        if (!registry_.get<AgentComponent>(e).alive) continue;

        auto& action = registry_.get<ActionComponent>(e);
        auto& pos    = registry_.get<PositionComponent>(e);
        auto& inv    = registry_.get<InventoryComponent>(e);

        int tx = -1, ty = -1;

        switch (action.current) {
            case ActionType::GATHER: {
                // Only ScrapPile is gatherable in this model (no FoodSource).
                auto scrap_target = grid_.find_nearest(TileType::ScrapPile, pos.x, pos.y);
                if (scrap_target.first >= 0) {
                    tx = scrap_target.first;
                    ty = scrap_target.second;
                }
                break;
            }

            case ActionType::BUILD: {
                // Priority: nearest unbuilt structure (Machine/Conveyor/EZ) with tie-breaking
                // toward conveyors when machines are far away. Unbuilt conveyor frames are walkable.
                auto machine_t = grid_.find_nearest_unbuilt_machine(pos.x, pos.y);
                auto conv_t    = grid_.find_nearest_conveyor_to_build(pos.x, pos.y);
                auto ez_t      = grid_.find_nearest_unbuilt_eatingzone(pos.x, pos.y);
                bool any_built_ez = grid_.find_nearest_built_eatingzone(pos.x, pos.y).first >= 0;

                // Compute distances
                int mach_dist = (machine_t.first >= 0)
                    ? std::abs(machine_t.first - pos.x) + std::abs(machine_t.second - pos.y) : 999999;
                int conv_dist = (conv_t.first >= 0)
                    ? std::abs(conv_t.first - pos.x) + std::abs(conv_t.second - pos.y) : 999999;
                int ez_dist = (ez_t.first >= 0)
                    ? std::abs(ez_t.first - pos.x) + std::abs(ez_t.second - pos.y) : 999999;

                // Prefer closest unbuilt structure; conveyors get a distance bonus
                // so they're chosen even when slightly farther than machines.
                int best_dist = 999999;
                if (machine_t.first >= 0 && mach_dist < best_dist) best_dist = mach_dist;
                if (conv_t.first >= 0 && (conv_dist - 3) < best_dist) best_dist = conv_dist - 3;
                if (ez_t.first >= 0 && ez_dist < best_dist) best_dist = ez_dist;

                if (machine_t.first >= 0 && mach_dist <= best_dist) {
                    tx = machine_t.first; ty = machine_t.second;
                } else if (conv_t.first >= 0 && (conv_dist - 3) <= best_dist) {
                    tx = conv_t.first; ty = conv_t.second;
                } else if (ez_t.first >= 0) {
                    tx = ez_t.first; ty = ez_t.second;
                } else if (!any_built_ez) {
                    auto site = grid_.find_nearest_valid_eatingzone_site(
                        pos.x, pos.y, config_.eatingzone_min_dist_machine);
                    if (site.first >= 0) {
                        tx = site.first; ty = site.second;
                    }
                }
                break;
            }

            case ActionType::GET_FOOD: {
                auto target = grid_.find_nearest_storage_with_food(pos.x, pos.y);
                if (target.first >= 0) {
                    tx = target.first; ty = target.second;
                }
                break;
            }

            case ActionType::WORK: {
                auto target = grid_.find_nearest_built_machine(pos.x, pos.y);
                tx = target.first;
                ty = target.second;
                break;
            }

            case ActionType::EAT:
            case ActionType::REST:
                tx = pos.x;
                ty = pos.y;
                break;

            case ActionType::SOCIALIZE: {
                // Find nearest other alive agent
                auto agents = alive_agents();
                int best_dist = 999999;
                for (auto other : agents) {
                    if (other == e) continue;
                    auto& opos = registry_.get<PositionComponent>(other);
                    int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
                    if (d < best_dist && d > 0) {
                        best_dist = d;
                        tx = opos.x;
                        ty = opos.y;
                    }
                }
                if (tx < 0) { tx = pos.x; ty = pos.y; }
                break;
            }

            case ActionType::CREATE: {
                auto target = grid_.find_nearest(TileType::OpenSpace, pos.x, pos.y);
                if (target.first >= 0) {
                    tx = target.first;
                    ty = target.second;
                } else {
                    tx = pos.x;
                    ty = pos.y;
                }
                break;
            }

            case ActionType::EXPLORE: {
                std::uniform_int_distribution<int> dx(2, grid_.width() - 3);
                std::uniform_int_distribution<int> dy(2, grid_.height() - 3);
                tx = dx(rng_);
                ty = dy(rng_);
                break;
            }

            case ActionType::MAINTAIN: {
                auto conv = grid_.find_nearest_conveyor_needing_maintain(pos.x, pos.y);
                if (conv.first >= 0) {
                    // Agent stands adjacent to conveyor (not on it)
                    auto adj = grid_.find_walkable_adjacent_to(
                        conv.first, conv.second, pos.x, pos.y);
                    if (adj.first >= 0) {
                        tx = adj.first; ty = adj.second;
                    }
                }
                break;
            }

            case ActionType::DISMANTLE: {
                // Find nearest conveyor that is a dead-end or blocking a path.
                // Agent walks to an adjacent walkable tile to dismantle it.
                auto dead_end = grid_.find_nearest_dead_end_conveyor(pos.x, pos.y);
                // Also consider blocking conveyors nearby (scan vicinity)
                int block_x = -1, block_y = -1, block_dist = 999999;
                for (int sy = std::max(0, pos.y - 10); sy < std::min(grid_.height(), pos.y + 10); sy++)
                    for (int sx = std::max(0, pos.x - 10); sx < std::min(grid_.width(), pos.x + 10); sx++) {
                        if (grid_.is_conveyor_blocking_path(sx, sy)) {
                            int d = std::abs(sx - pos.x) + std::abs(sy - pos.y);
                            if (d < block_dist) {
                                block_dist = d;
                                block_x = sx; block_y = sy;
                            }
                        }
                    }
                // Prefer blocking conveyor (higher urgency)
                int cx = -1, cy = -1;
                if (block_x >= 0 && block_dist < 15) {
                    cx = block_x; cy = block_y;
                } else if (dead_end.first >= 0) {
                    cx = dead_end.first; cy = dead_end.second;
                }
                if (cx >= 0) {
                    auto adj = grid_.find_walkable_adjacent_to(cx, cy, pos.x, pos.y);
                    if (adj.first >= 0) {
                        tx = adj.first; ty = adj.second;
                    }
                }
                break;
            }

            default:
                tx = pos.x;
                ty = pos.y;
                break;
        }

        action.target_x = tx;
        action.target_y = ty;
        action.at_target = (tx == pos.x && ty == pos.y);
    }
}
