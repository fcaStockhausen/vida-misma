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
        auto& agent  = registry_.get<AgentComponent>(e);

        // Release claims when switching away from WORK or BUILD
        if (action.current != ActionType::WORK &&
            action.current != ActionType::BUILD &&
            action.target_x >= 0 && action.target_y >= 0) {
            auto& td = grid_.data_at(action.target_x, action.target_y);
            int my_id = registry_.get<AgentComponent>(e).id;
            if (td.claimed_by == my_id) td.claimed_by = -1;
        }

        int tx = -1, ty = -1;

        switch (action.current) {
            case ActionType::GATHER: {
                // Prefer FoodSource if agent has no raw_food and low food
                // (food chain priority). Otherwise prefer ScrapPile (for building).
                auto food_src = grid_.find_nearest(TileType::FoodSource, pos.x, pos.y);
                auto scrap_target = grid_.find_nearest(TileType::ScrapPile, pos.x, pos.y);
                
                int food_dist = (food_src.first >= 0)
                    ? std::abs(food_src.first - pos.x) + std::abs(food_src.second - pos.y) : 999999;
                int scrap_dist = (scrap_target.first >= 0)
                    ? std::abs(scrap_target.first - pos.x) + std::abs(scrap_target.second - pos.y) : 999999;

                // Priority logic:
                // - Bootstrapping: if no machines built yet, prefer ScrapPile (need to BUILD first)
                // - If carrying raw_food but no raw_material: prefer ScrapPile
                // - If carrying raw_material but no raw_food: prefer FoodSource
                // - If both empty: prefer ScrapPile (industry first, food can wait)
                // - If both have some: prefer whatever has less (balance)
                bool prefer_food = false;
                // Use cached built_machine_count() instead of scanning grid
                bool any_built = built_machine_count() > 0;

                if (!any_built) {
                    prefer_food = false;  // Bootstrap: need raw_material to BUILD machines
                } else if (inv.raw_food < 0.1f && inv.raw_material > 0.1f) {
                    prefer_food = true;  // need raw_food to work FoodMachine
                } else if (inv.raw_food > 0.1f && inv.raw_material < 0.1f) {
                    prefer_food = false;  // need raw_material for building/mat machines
                } else {
                    prefer_food = false;  // Default: industry first
                }

                if (prefer_food && food_src.first >= 0) {
                    tx = food_src.first;
                    ty = food_src.second;
                } else if (scrap_target.first >= 0) {
                    tx = scrap_target.first;
                    ty = scrap_target.second;
                } else if (food_src.first >= 0) {
                    tx = food_src.first;
                    ty = food_src.second;
                }
                break;
            }

            case ActionType::BUILD: {
                // Priority: nearest unbuilt structure with strategic bias.
                auto machine_t = grid_.find_nearest_unbuilt_machine(pos.x, pos.y);
                auto conv_t    = grid_.find_nearest_conveyor_to_build(pos.x, pos.y);
                auto ez_t      = grid_.find_nearest_unbuilt_eatingzone(pos.x, pos.y);
                auto stor_t    = grid_.find_storage_build_site(pos.x, pos.y);
                auto fs_t      = grid_.find_nearest_free_foodsource(pos.x, pos.y, agent.id);
                auto sp_t      = grid_.find_nearest_free_scrappile(pos.x, pos.y, agent.id);
                bool any_built_ez = grid_.find_nearest_built_eatingzone(pos.x, pos.y).first >= 0;

                // Compute distances
                int mach_dist = (machine_t.first >= 0)
                    ? std::abs(machine_t.first - pos.x) + std::abs(machine_t.second - pos.y) : 999999;
                int conv_dist = (conv_t.first >= 0)
                    ? std::abs(conv_t.first - pos.x) + std::abs(conv_t.second - pos.y) : 999999;
                int ez_dist = (ez_t.first >= 0)
                    ? std::abs(ez_t.first - pos.x) + std::abs(ez_t.second - pos.y) : 999999;
                int stor_dist = (stor_t.first >= 0)
                    ? std::abs(stor_t.first - pos.x) + std::abs(stor_t.second - pos.y) : 999999;
                int fs_dist = (fs_t.first >= 0)
                    ? std::abs(fs_t.first - pos.x) + std::abs(fs_t.second - pos.y) : 999999;
                int sp_dist = (sp_t.first >= 0)
                    ? std::abs(sp_t.first - pos.x) + std::abs(sp_t.second - pos.y) : 999999;

                // Bonuses (negative = higher priority)
                int conv_bonus = (total_storage_food() < 3.0f) ? -10 : -3;
                int stor_bonus = -5;
                int fs_bonus = -8;  // FoodMachine on FoodSource
                int sp_bonus = -8;  // OutputMachine on ScrapPile

                int best_dist = 999999;
                if (machine_t.first >= 0 && mach_dist < best_dist) best_dist = mach_dist;
                if (fs_t.first >= 0 && (fs_dist + fs_bonus) < best_dist) best_dist = fs_dist + fs_bonus;
                if (sp_t.first >= 0 && (sp_dist + sp_bonus) < best_dist) best_dist = sp_dist + sp_bonus;
                if (stor_t.first >= 0 && (stor_dist + stor_bonus) < best_dist) best_dist = stor_dist + stor_bonus;
                if (conv_t.first >= 0 && (conv_dist + conv_bonus) < best_dist) best_dist = conv_dist + conv_bonus;
                if (ez_t.first >= 0 && ez_dist < best_dist) best_dist = ez_dist;

                if (machine_t.first >= 0 && mach_dist <= best_dist) {
                    tx = machine_t.first; ty = machine_t.second;
                } else if (fs_t.first >= 0 && (fs_dist + fs_bonus) <= best_dist) {
                    tx = fs_t.first; ty = fs_t.second;
                } else if (sp_t.first >= 0 && (sp_dist + sp_bonus) <= best_dist) {
                    tx = sp_t.first; ty = sp_t.second;
                } else if (stor_t.first >= 0 && (stor_dist + stor_bonus) <= best_dist) {
                    tx = stor_t.first; ty = stor_t.second;
                } else if (conv_t.first >= 0 && (conv_dist + conv_bonus) <= best_dist) {
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

                // Claim resource tiles when chosen as BUILD target
                // (prevents other agents from targeting the same tile)
                if (tx >= 0 && ty >= 0) {
                    TileType tt = grid_.at(tx, ty);
                    if (tt == TileType::FoodSource || tt == TileType::ScrapPile) {
                        auto& td = grid_.data_at(tx, ty);
                        td.claimed_by = agent.id;
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
                // Prefer machines matching what the agent has in inventory
                // Agent with raw_food → FoodMachine
                // Agent with raw_material → MaterialsMachine
                // Otherwise use production chain priority
                bool prefer_food = false;
                bool prefer_output = false;
                bool prefer_materials = false;

                if (inv.raw_food > 0.1f) {
                    prefer_food = true;  // carry raw_food → work FoodMachine
                } else if (inv.construction_material > 0.1f) {
                    prefer_output = true; // carry constr_mat → work OutputMachine
                } else if (inv.raw_material > 0.1f) {
                    prefer_materials = true;  // carry scrap → work MaterialsMachine
                } else {
                    // No inputs in inventory — fall back to production chain priority
                    prefer_food = total_storage_food() < 15.0f;
                    if (!prefer_food) {
                        float constr_mat = total_storage_constr_mat();
                        if (constr_mat > 0.3f || factory_health_ < 0.5f) {
                            prefer_output = true;
                        } else {
                            prefer_materials = true;
                        }
                    }
                }
                auto target = grid_.find_nearest_built_machine(
                    pos.x, pos.y, prefer_food, prefer_output, prefer_materials);
                tx = target.first;
                ty = target.second;
                // Claim the target machine (RimWorld/DF pattern)
                if (tx >= 0 && ty >= 0) {
                    int my_id = registry_.get<AgentComponent>(e).id;
                    // Release old claim if target changed
                    if (action.target_x >= 0 && action.target_y >= 0 &&
                        (action.target_x != tx || action.target_y != ty)) {
                        auto& old_td = grid_.data_at(action.target_x, action.target_y);
                        if (old_td.claimed_by == my_id) old_td.claimed_by = -1;
                    }
                    auto& td = grid_.data_at(tx, ty);
                    if (td.claimed_by < 0 || td.claimed_by == my_id) {
                        td.claimed_by = my_id;
                    }
                }
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

            case ActionType::SABOTAGE: {
                // Find nearest built infrastructure to destroy.
                // Prefer machines (more damage), fallback to conveyors.
                auto machine = grid_.find_nearest_built_machine(pos.x, pos.y);
                int mx = machine.first, my = machine.second;
                int mach_dist = (mx >= 0) ? std::abs(mx - pos.x) + std::abs(my - pos.y) : 999999;

                // Find nearest built conveyor
                int cx = -1, cy = -1, conv_dist = 999999;
                for (int sy = 0; sy < grid_.height(); sy++)
                    for (int sx = 0; sx < grid_.width(); sx++) {
                        if (grid_.at(sx, sy) == TileType::Conveyor && grid_.data_at(sx, sy).built) {
                            int d = std::abs(sx - pos.x) + std::abs(sy - pos.y);
                            if (d < conv_dist) {
                                conv_dist = d;
                                cx = sx; cy = sy;
                            }
                        }
                    }

                // Prefer machine if close enough; otherwise conveyor
                int infra_x = -1, infra_y = -1;
                if (mx >= 0 && mach_dist <= conv_dist + 5) {
                    infra_x = mx; infra_y = my;
                } else if (cx >= 0) {
                    infra_x = cx; infra_y = cy;
                }

                if (infra_x >= 0) {
                    // Walk to adjacent tile (can't stand on machine/conveyor)
                    auto adj = grid_.find_walkable_adjacent_to(infra_x, infra_y, pos.x, pos.y);
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
