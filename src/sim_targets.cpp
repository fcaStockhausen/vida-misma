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
                // Priority: existing unbuilt Machine > existing unbuilt EatingZone > new EatingZone site.
                // The utility scoring already decided this is the best action; here we just pick where.
                auto machine_t = grid_.find_nearest_unbuilt_machine(pos.x, pos.y);
                auto ez_t      = grid_.find_nearest_unbuilt_eatingzone(pos.x, pos.y);
                bool any_built_ez = grid_.find_nearest_built_eatingzone(pos.x, pos.y).first >= 0;

                if (machine_t.first >= 0) {
                    tx = machine_t.first; ty = machine_t.second;
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
