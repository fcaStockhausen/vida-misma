#include "simulation.h"
#include <algorithm>
#include <cstdlib>

// ============================================================
// SYSTEM: Execute Actions
// ============================================================

void Simulation::system_execute_actions() {
    auto view = registry_.view<ActionComponent, NeedsComponent,
                               InventoryComponent, PositionComponent,
                               const AgentComponent>();
    for (auto e : view) {
        if (!registry_.get<AgentComponent>(e).alive) continue;

        auto& action = registry_.get<ActionComponent>(e);
        auto& needs  = registry_.get<NeedsComponent>(e);
        auto& inv    = registry_.get<InventoryComponent>(e);
        auto& pos    = registry_.get<PositionComponent>(e);
        auto& agent  = registry_.get<AgentComponent>(e);

        // Only execute if at target (or action doesn't need movement)
        if (!action.at_target) continue;

        // Spatial gate: positional actions need the right tile under the agent.
        TileType here = grid_.at(pos.x, pos.y);
        if (!is_valid_action_tile(action.current, here)) {
            emit_log(agent.id, std::string("tried ") + action_name(action.current) +
                     " on wrong tile at (" + std::to_string(pos.x) + "," +
                     std::to_string(pos.y) + ")");
            continue;
        }

        switch (action.current) {

            case ActionType::GATHER: {
                auto& td = grid_.data_at(pos.x, pos.y);
                float available = td.resource_amount;
                if (available > 0.01f) {
                    float amount = std::min(config_.gather_rate, available);
                    if (here == TileType::FoodSource) {
                        if (inv.can_carry(amount)) {
                            inv.raw_food += amount;
                            td.resource_amount -= amount;
                            total_raw_gathered_ += amount;
                            emit_log(agent.id, "gathered " + ff2(amount) + " raw food at (" +
                                     std::to_string(pos.x) + "," + std::to_string(pos.y) + ")");
                        }
                    } else { // ScrapPile
                        if (inv.can_carry(amount)) {
                            inv.raw_material += amount;
                            td.resource_amount -= amount;
                            total_raw_gathered_ += amount;
                            emit_log(agent.id, "salvaged " + ff2(amount) + " scrap at (" +
                                     std::to_string(pos.x) + "," + std::to_string(pos.y) + ")");
                        }
                    }
                }
                break;
            }

            case ActionType::BUILD: {
                // Floor tile: agent initiates a new EatingZone here if the distance
                // constraint holds. Otherwise (on Machine / EatingZone frame), continue building.
                if (here == TileType::Floor) {
                    if (grid_.min_distance_to_any_machine(pos.x, pos.y)
                            < config_.eatingzone_min_dist_machine) {
                        // too close to machinery — refuse to build the eating zone here
                        emit_log(agent.id, "rejected eating-zone site (too close to a machine)");
                        break;
                    }
                    grid_.set(pos.x, pos.y, TileType::EatingZone);
                    auto& nd = grid_.data_at(pos.x, pos.y);
                    nd.built          = false;
                    nd.build_progress = 0.0f;
                    nd.build_cost     = config_.eatingzone_build_cost;
                    emit_log(agent.id, "started an eating zone at (" +
                             std::to_string(pos.x) + "," + std::to_string(pos.y) + ")");
                    // Fall through into the regular BUILD-progress block below.
                }
                auto& td = grid_.data_at(pos.x, pos.y);
                if (!td.built && td.build_progress < td.build_cost) {
                    float needed = td.build_cost - td.build_progress;
                    float use = std::min({config_.build_rate, needed, inv.raw_material});
                    if (use > 0.0f) {
                        inv.raw_material -= use;
                        td.build_progress += use;
                        if (td.build_progress >= td.build_cost) {
                            td.built = true;
                            TileType t_after = grid_.at(pos.x, pos.y);
                            if (t_after == TileType::Machine) {
                                total_machines_built_++;
                                emit_log(agent.id, "BUILT a machine at (" +
                                         std::to_string(pos.x) + "," + std::to_string(pos.y) + ")!");
                            } else if (t_after == TileType::Conveyor) {
                                emit_log(agent.id, "BUILT a conveyor at (" +
                                         std::to_string(pos.x) + "," + std::to_string(pos.y) + ")");
                            } else {
                                emit_log(agent.id, "BUILT an eating zone at (" +
                                         std::to_string(pos.x) + "," + std::to_string(pos.y) + ")");
                            }
                        }
                    }
                }
                break;
            }

            case ActionType::WORK: {
                auto& td = grid_.data_at(pos.x, pos.y);
                if (td.built) {
                    // Collaboration bonus: friends working adjacent boost output
                    auto alive_list = alive_agents();
                    float collab = social_.collaboration_bonus(
                        agent.id, registry_, alive_list, e);
                    float produced = config_.machine_output * collab;

                    // Try storage first, then conveyor
                    float deposited = deposit_to_adjacent_storage(pos.x, pos.y,
                        ResourceType::FOOD, produced);
                    float leftover = produced - deposited;
                    if (leftover > 0.01f) {
                        deposited += deposit_to_adjacent_conveyor(pos.x, pos.y,
                            ResourceType::FOOD, leftover);
                    }

                    if (deposited > 0.0f) {
                        total_food_produced_ += deposited;
                        emit_log(agent.id, "worked, +" + ff2(deposited) + " food"
                                 + (collab > 1.01f ? " (collab x" + ff2(collab) + ")" : ""));
                    }

                    // Work satisfies purpose slightly and tires the worker.
                    needs.purpose = std::max(0.0f,
                        needs.purpose - config_.work_purpose_gain);
                    needs.hunger = std::min(1.0f, needs.hunger + 0.001f);
                    needs.rest   = std::min(1.0f, needs.rest   + 0.001f);
                }
                break;
            }

            case ActionType::EAT: {
                // Two food sources: agent's inventory (snack) and 8-adjacent Storage.
                // Eating ≤1 from any Machine counts as "eating at work" → social penalty.
                bool ate = false;
                float satisfaction_mul = 0.0f;
                bool from_storage = false;

                if (inv.food >= config_.eat_food_per_tick) {
                    inv.food -= config_.eat_food_per_tick;
                    satisfaction_mul = 1.0f;
                    ate = true;
                } else {
                    float taken_mul = take_from_adjacent_storage(pos.x, pos.y);
                    if (taken_mul > 0.0f) {
                        satisfaction_mul = taken_mul;
                        ate = true;
                        from_storage = true;
                    }
                }

                if (ate) {
                    needs.hunger = std::max(0.0f,
                        needs.hunger - config_.eat_satisfaction * satisfaction_mul);

                    // P5(c): eating at Storage also tops up the snack for to-go.
                    if (from_storage) {
                        float room = config_.inv_food_cap - inv.food;
                        if (room > 0.001f) {
                            float pulled = pull_food_from_adjacent_storage(pos.x, pos.y, room);
                            inv.food += pulled;
                        }
                    }

                    // Social penalty: "eating at work" hurts the factory and the transgressor,
                    // BUT only fires when another agent is close enough to witness/report it.
                    // No witness, no penalty — the agent gets away with it.
                    if (is_near_machine(pos.x, pos.y)) {
                        int witness_id = -1;
                        int r = config_.eat_at_work_witness_radius;
                        auto agents_alive = alive_agents();
                        for (auto other : agents_alive) {
                            if (other == e) continue;
                            auto& opos = registry_.get<PositionComponent>(other);
                            int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
                            if (d <= r) {
                                witness_id = registry_.get<AgentComponent>(other).id;
                                break;
                            }
                        }
                        if (witness_id >= 0) {
                            factory_health_ = std::max(0.0f,
                                factory_health_ - config_.eat_at_work_health_decay);
                            auto& st = registry_.get<StressComponent>(e);
                            st.value = std::min(1.0f, st.value + config_.eat_at_work_stress);
                            emit_log(agent.id, "ate at work — reported by A" +
                                     std::to_string(witness_id));
                            // Witness loses trust in transgressor (antagonism mechanic)
                            social_.negative_interaction(witness_id, agent.id, tick_, 0.05f);
                        }
                    }
                }
                break;
            }

            case ActionType::GET_FOOD: {
                // Grab a snack from 8-adjacent Storage (no eating, no hunger change).
                // Capped at inv_food_cap.
                float room = config_.inv_food_cap - inv.food;
                if (room > 0.001f) {
                    float pulled = pull_food_from_adjacent_storage(pos.x, pos.y, room);
                    if (pulled > 0.0f) {
                        inv.food += pulled;
                        emit_log(agent.id, "grabbed " + ff2(pulled) + " food (snack to-go)");
                    }
                }
                break;
            }

            case ActionType::REST:
                needs.rest = std::max(0.0f, needs.rest - config_.rest_recovery);
                break;

            case ActionType::SOCIALIZE: {
                auto& personality = registry_.get<PersonalityComponent>(e);
                bool has_neighbor = false;
                entt::entity neighbor = entt::null;
                int neighbor_id = -1;
                auto agents = alive_agents();
                for (auto other : agents) {
                    if (other == e) continue;
                    auto& opos = registry_.get<PositionComponent>(other);
                    int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
                    if (d <= 2) {
                        has_neighbor = true;
                        neighbor = other;
                        neighbor_id = registry_.get<AgentComponent>(other).id;
                        break;
                    }
                }
                if (has_neighbor) {
                    // Satisfaction weighted by gregariousness (doc §15/§17)
                    float satisfaction = config_.social_satisfaction
                                       * (0.5f + 0.5f * personality.gregariousness);
                    needs.social = std::max(0.0f, needs.social - satisfaction);
                    // Process social interaction
                    social_.process_interaction(agent.id, neighbor_id, tick_);
                } else {
                    needs.social = std::max(0.0f, needs.social - 0.002f);
                }
                break;
            }

            case ActionType::CREATE:
                needs.expression = std::max(0.0f,
                    needs.expression - config_.create_satisfaction);
                break;

            case ActionType::EXPLORE:
                needs.purpose = std::max(0.0f,
                    needs.purpose - config_.explore_satisfaction);
                random_move(pos);
                break;

            case ActionType::MAINTAIN: {
                auto& td = grid_.data_at(pos.x, pos.y);
                if (grid_.at(pos.x, pos.y) == TileType::Conveyor && td.built) {
                    float restored = std::min(config_.maintain_rate, 1.0f - td.conveyor_condition);
                    td.conveyor_condition += restored;
                    needs.purpose = std::max(0.0f,
                        needs.purpose - config_.work_purpose_gain * 0.5f);
                    if (restored > 0.001f)
                        emit_log(agent.id, "maintained conveyor (" + ff2(td.conveyor_condition) + ")");
                }
                break;
            }

            case ActionType::IDLE:
            default:
                break;
        }
    }
}

// ============================================================
// Adjacent storage helpers
// ============================================================

bool Simulation::has_adjacent_storage_with_food(entt::entity e) const {
    auto& pos = registry_.get<PositionComponent>(e);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = pos.x + dx, ny = pos.y + dy;
            if (grid_.at(nx, ny) == TileType::Storage) {
                const auto& d = grid_.data_at(nx, ny);
                if (d.stored_food > 0.01f || d.stored_raw_food > 0.01f)
                    return true;
            }
        }
    return false;
}

float Simulation::get_adjacent_raw_food(int px, int py) const {
    float total = 0.0f;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = px + dx, ny = py + dy;
            if (grid_.at(nx, ny) == TileType::Storage) {
                total += grid_.data_at(nx, ny).stored_raw_food;
            }
        }
    return total;
}

void Simulation::consume_adjacent_raw_food(int px, int py, float amount) {
    float remaining = amount;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            if (remaining <= 0.0f) return;
            int nx = px + dx, ny = py + dy;
            if (grid_.at(nx, ny) == TileType::Storage) {
                auto& d = grid_.data_at(nx, ny);
                float take = std::min(remaining, d.stored_raw_food);
                d.stored_raw_food -= take;
                remaining -= take;
            }
        }
}

float Simulation::deposit_to_adjacent_storage(int px, int py, ResourceType type, float amount) {
    float remaining = amount;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            if (remaining <= 0.001f) return amount - remaining;
            int nx = px + dx, ny = py + dy;
            if (grid_.at(nx, ny) == TileType::Storage) {
                auto& d = grid_.data_at(nx, ny);
                float stored_total = d.stored_food + d.stored_raw_food + d.stored_raw_material;
                float space = d.storage_capacity - stored_total;
                if (space > 0.001f) {
                    float deposit = std::min(remaining, space);
                    if (type == ResourceType::FOOD) {
                        d.stored_food += deposit;
                    } else if (type == ResourceType::RAW_FOOD) {
                        d.stored_raw_food += deposit;
                    } else {
                        d.stored_raw_material += deposit;
                    }
                    remaining -= deposit;
                }
            }
        }
    return amount - remaining;
}

float Simulation::deposit_to_adjacent_conveyor(int px, int py, ResourceType type, float amount) {
    float remaining = amount;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            if (remaining <= 0.001f) return amount - remaining;
            int nx = px + dx, ny = py + dy;
            if (grid_.at(nx, ny) == TileType::Conveyor) {
                auto& d = grid_.data_at(nx, ny);
                if (!d.built || d.conveyor_condition < 0.2f) continue;
                float space = config_.conveyor_throughput - d.conveyor_contents;
                if (space > 0.001f) {
                    float deposit = std::min(remaining, space);
                    d.conveyor_contents += deposit;
                    d.conveyor_contents_type = type;
                    remaining -= deposit;
                }
            }
        }
    return amount - remaining;
}

// Returns effective satisfaction multiplier based on what was eaten
float Simulation::take_from_adjacent_storage(int px, int py) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = px + dx, ny = py + dy;
            if (grid_.at(nx, ny) == TileType::Storage) {
                auto& d = grid_.data_at(nx, ny);
                if (d.stored_food >= config_.eat_food_per_tick) {
                    d.stored_food -= config_.eat_food_per_tick;
                    return 1.0f;
                }
                if (d.stored_raw_food >= config_.eat_food_per_tick) {
                    d.stored_raw_food -= config_.eat_food_per_tick;
                    return config_.eat_raw_efficiency;
                }
            }
        }
    return 0.0f;
}

// Pulls processed food from any 8-adjacent Storage (and the current tile if it is a
// Storage), up to `max_amount`. Returns the amount actually transferred.
float Simulation::pull_food_from_adjacent_storage(int px, int py, float max_amount) {
    float remaining = max_amount;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (remaining <= 0.001f) return max_amount - remaining;
            int nx = px + dx, ny = py + dy;
            if (grid_.at(nx, ny) != TileType::Storage) continue;
            auto& d = grid_.data_at(nx, ny);
            float take = std::min(remaining, d.stored_food);
            if (take > 0.0f) {
                d.stored_food -= take;
                remaining -= take;
            }
        }
    return max_amount - remaining;
}

bool Simulation::is_near_machine(int px, int py) const {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (grid_.at(px + dx, py + dy) == TileType::Machine) return true;
        }
    return false;
}
