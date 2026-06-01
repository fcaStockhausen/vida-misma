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
        auto& stress = registry_.get<StressComponent>(e);
        auto& personality = registry_.get<PersonalityComponent>(e);

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
                // Agent stands on their own tile and builds whatever is here.
                // Special case for Floor: may create a new EatingZone or build an adjacent conveyor.
                if (here == TileType::Floor) {
                    // Check for adjacent unbuilt conveyor to build from beside
                    int conv_x = -1, conv_y = -1;
                    float conv_progress = -1.0f;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            if (dx == 0 && dy == 0) continue;
                            int nx = pos.x + dx, ny = pos.y + dy;
                            if (grid_.at(nx, ny) == TileType::Conveyor) {
                                const auto& cd = grid_.data_at(nx, ny);
                                if (!cd.built && cd.build_progress > conv_progress) {
                                    conv_x = nx; conv_y = ny;
                                    conv_progress = cd.build_progress;
                                }
                            }
                        }
                    if (conv_x >= 0) {
                        auto& td = grid_.data_at(conv_x, conv_y);
                        float needed = td.build_cost - td.build_progress;
                        float use = std::min({config_.build_rate, needed, inv.raw_material});
                        if (use > 0.0f) {
                            inv.raw_material -= use;
                            td.build_progress += use;
                            if (td.build_progress >= td.build_cost) {
                                td.built = true;
                                emit_log(agent.id, "BUILT a conveyor at (" +
                                         std::to_string(conv_x) + "," + std::to_string(conv_y) + ")");
                            } else {
                                emit_log(agent.id, "building conveyor at (" +
                                         std::to_string(conv_x) + "," + std::to_string(conv_y) +
                                         ") " + ff2(td.build_progress) + "/" + ff2(td.build_cost));
                            }
                        }
                        break;
                    }

                    // No adjacent conveyor — try starting a new EatingZone (max 1)
                    if (grid_.built_eatingzone_count() >= 1) {
                        emit_log(agent.id, "no need for another eating zone");
                        break;
                    }
                    if (grid_.min_distance_to_any_machine(pos.x, pos.y)
                            < config_.eatingzone_min_dist_machine) {
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

                // Build Machine / EatingZone / Conveyor the agent is standing ON.
                // (Unbuilt conveyor frames are walkable — agent walks onto them to build.)
                // COLLABORATIVE BUILD: nearby agents also building boost the rate.
                auto& td = grid_.data_at(pos.x, pos.y);
                if (!td.built && td.build_progress < td.build_cost) {
                    float needed = td.build_cost - td.build_progress;
                    float collab_rate = config_.build_rate;
                    // Check if other agents are building the same tile or adjacent tiles
                    {
                        auto builders = alive_agents();
                        int co_builders = 0;
                        for (auto ob : builders) {
                            if (ob == e) continue;
                            auto& oact = registry_.get<ActionComponent>(ob);
                            if (oact.current != ActionType::BUILD) continue;
                            auto& opos = registry_.get<PositionComponent>(ob);
                            // Same tile or adjacent to the same build target
                            int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
                            if (d <= 2) {
                                auto& oag = registry_.get<AgentComponent>(ob);
                                const auto& rel = social_.get_rel(agent.id, oag.id);
                                // Trust amplifies collaboration
                                float trust_mod = 0.5f + 0.5f * std::max(0.0f, rel.trust);
                                co_builders++;
                                collab_rate += config_.build_rate * 0.3f * trust_mod;
                            }
                        }
                        // Cap at 2.5x solo build rate (prevents runaway)
                        collab_rate = std::min(collab_rate, config_.build_rate * 2.5f);
                    }
                    float use = std::min({collab_rate, needed, inv.raw_material});
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

                    float deposited = 0.0f;
                    std::string log_detail;

                    switch (td.machine_type) {
                        case MachineType::Food: {
                            // FoodMachine: raw_food → processed_food
                            float food_produced = config_.machine_output * collab;
                            float food_dep = deposit_to_adjacent_storage(pos.x, pos.y,
                                ResourceType::FOOD, food_produced);
                            float food_left = food_produced - food_dep;
                            if (food_left > 0.01f) {
                                food_dep += deposit_to_adjacent_conveyor(pos.x, pos.y,
                                    ResourceType::FOOD, food_left);
                            }
                            deposited = food_dep;
                            total_food_produced_ += food_dep;
                            log_detail = "+" + ff2(food_dep) + " food";
                            break;
                        }
                        case MachineType::Materials: {
                            // MaterialsMachine: raw_material → construction_material + scrap byproduct
                            float mat_produced = config_.machine_mat_output * collab;
                            float mat_dep = deposit_to_adjacent_storage(pos.x, pos.y,
                                ResourceType::CONSTRUCTION_MATERIAL, mat_produced);
                            float mat_left = mat_produced - mat_dep;
                            if (mat_left > 0.01f) {
                                mat_dep += deposit_to_adjacent_conveyor(pos.x, pos.y,
                                    ResourceType::CONSTRUCTION_MATERIAL, mat_left);
                            }

                            // Scrap byproduct: feed back into nearby ScrapPile (recycling loop)
                            float scrap = config_.machine_mat_output * 0.3f * collab;
                            for (int dy = -3; dy <= 3; dy++)
                                for (int dx = -3; dx <= 3; dx++) {
                                    int nx = pos.x + dx, ny = pos.y + dy;
                                    if (grid_.at(nx, ny) == TileType::ScrapPile) {
                                        auto& sd = grid_.data_at(nx, ny);
                                        float add = std::min(scrap, sd.resource_max - sd.resource_amount);
                                        sd.resource_amount += add;
                                        scrap -= add;
                                        if (scrap < 0.001f) break;
                                    }
                                    if (scrap < 0.001f) break;
                                }

                            deposited = mat_dep;
                            log_detail = "+" + ff2(mat_dep) + " constr_mat";
                            break;
                        }
                        case MachineType::Output: {
                            // OutputMachine: construction_material → factory health restoration
                            // Pull construction_material from adjacent storage
                            float pulled = pull_construction_material_from_adjacent_storage(
                                pos.x, pos.y, 0.05f);
                            if (pulled > 0.001f) {
                                float health_gain = pulled * 8.0f; // amplify: small mat → meaningful health
                                factory_health_ = std::min(1.0f, factory_health_ + health_gain);
                                deposited = pulled;
                                log_detail = "+" + ff2(health_gain) + " hp (used " + ff2(pulled) + " mat)";
                            } else {
                                log_detail = "no mat available";
                            }
                            break;
                        }
                    }

                    if (deposited > 0.0f || !log_detail.empty()) {
                        emit_log(agent.id, "worked [" + std::string(
                            td.machine_type == MachineType::Food ? "FOOD" :
                            td.machine_type == MachineType::Materials ? "MAT" : "OUT") +
                            "] " + log_detail +
                            (collab > 1.01f ? " (collab x" + ff2(collab) + ")" : ""));
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
                // Grab food and/or raw_material from 8-adjacent Storage.
                float room = config_.inv_food_cap - inv.food;
                if (room > 0.001f) {
                    float pulled = pull_food_from_adjacent_storage(pos.x, pos.y, room);
                    if (pulled > 0.0f) {
                        inv.food += pulled;
                        emit_log(agent.id, "grabbed " + ff2(pulled) + " food (snack to-go)");
                    }
                }
                // Also pick up raw_material from storage if needed for building.
                {
                    float mat_room = InventoryComponent::CAPACITY - inv.total();
                    if (mat_room > 0.5f) {
                        float mat_pulled = pull_raw_material_from_adjacent_storage(
                            pos.x, pos.y, std::min(mat_room, 2.0f));
                        if (mat_pulled > 0.0f) {
                            inv.raw_material += mat_pulled;
                        }
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

                    // FOOD SHARING: if this agent has excess food and the neighbor is hungry,
                    // share some. Trust grows from generosity.
                    {
                        auto& oinv = registry_.get<InventoryComponent>(neighbor);
                        auto& oneeds = registry_.get<NeedsComponent>(neighbor);
                        float excess = inv.food - 0.5f;  // keep at least 0.5 for self
                        float neighbor_need = config_.inv_food_cap - oinv.food;
                        // Faction members share more generously
                        float share_threshold = 0.3f;
                        if (agent.faction_id >= 0 &&
                            registry_.get<AgentComponent>(neighbor).faction_id == agent.faction_id)
                            share_threshold = 0.1f;
                        if (excess > share_threshold && neighbor_need > 0.3f && oneeds.hunger > 0.4f) {
                            float share = std::min({excess * 0.5f, neighbor_need, 1.0f});
                            inv.food -= share;
                            oinv.food += share;
                            // Generosity builds trust
                            social_.process_interaction(agent.id, neighbor_id, tick_);
                            emit_log(agent.id, "shared " + ff2(share) + " food with A" +
                                     std::to_string(neighbor_id));
                        }
                    }

                    // B4: Faction interaction satisfies meaning
                    if (agent.faction_id >= 0 &&
                        registry_.get<AgentComponent>(neighbor).faction_id == agent.faction_id) {
                        needs.meaning = std::max(0.0f, needs.meaning - 0.02f);
                    }
                    // Opinion exchange: bounded confidence (Hegselmann-Krause, doc §8.5)
                    {
                        auto& my_op = registry_.get<OpinionComponent>(e);
                        auto& their_op = registry_.get<OpinionComponent>(neighbor);
                        auto& my_soc = registry_.get<SocialComponent>(e);
                        auto& their_soc = registry_.get<SocialComponent>(neighbor);
                        social_.exchange_opinions(
                            agent.id, neighbor_id,
                            my_op, their_op,
                            my_soc.influence, their_soc.influence);
                    }
                    // Faction trust modulation
                    social_.apply_faction_trust_modulation(
                        agent.id, neighbor_id, tick_,
                        agent.faction_id,
                        registry_.get<AgentComponent>(neighbor).faction_id);
                    // S5: High-trust social contact reduces stress
                    float trust = social_.get_rel(agent.id, neighbor_id).trust;
                    stress.value = std::max(0.0f, stress.value - std::max(0.0f, trust) * 0.003f);
                    // Redeemed agents provide extra stress relief to neighbors
                    if (stress.state == StressState::REDEEMED) {
                        if (registry_.all_of<StressComponent>(neighbor)) {
                            auto& ns = registry_.get<StressComponent>(neighbor);
                            ns.value = std::max(0.0f, ns.value - 0.004f);
                        }
                    }
                } else {
                    needs.social = std::max(0.0f, needs.social - 0.002f);
                }
                break;
            }

            case ActionType::CREATE:
                needs.expression = std::max(0.0f,
                    needs.expression - config_.create_satisfaction);
                // B1: Spawn a cultural artifact at this location
                {
                    auto artifact = registry_.create();
                    registry_.emplace<PositionComponent>(artifact, pos.x, pos.y);
                    registry_.emplace<ArtifactComponent>(artifact, agent.id, 1.0f, 0);
                    artifacts_created_++;
                    artifacts_active_++;
                }
                // B4: CREATE satisfies meaning
                needs.meaning = std::max(0.0f, needs.meaning - 0.03f);
                // S5: CREATE reduces stress — the only real cure
                stress.value = std::max(0.0f, stress.value - 0.008f);
                break;

            case ActionType::EXPLORE:
                needs.purpose = std::max(0.0f,
                    needs.purpose - config_.explore_satisfaction);
                // S5: EXPLORE reduces stress — escape is a form of relief
                stress.value = std::max(0.0f, stress.value - 0.005f);
                random_move(pos);
                // B2: Chance to discover a hidden space
                {
                    auto& personality = registry_.get<PersonalityComponent>(e);
                    // Check current tile AND random move destination
                    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
                    float discovery_chance = personality.curiosity * 0.08f;
                    // Current position
                    for (auto [px, py] : {std::make_pair(pos.x, pos.y)}) {
                        if (grid_.at(px, py) == TileType::OpenSpace ||
                            grid_.at(px, py) == TileType::Floor) {
                            if (roll(rng_) < discovery_chance) {
                                grid_.set(px, py, TileType::HiddenSpace);
                                grid_.data_at(px, py).hidden_space_occupancy = 0;
                                hidden_spaces_found_++;
                                emit_log(agent.id, "discovered a hidden space");
                                needs.meaning = std::max(0.0f, needs.meaning - 0.15f);
                                break;
                            }
                        }
                    }
                }
                break;

            case ActionType::MAINTAIN: {
                // Agent stands ADJACENT to the conveyor (conveyors not walkable).
                // Scan 8-neighborhood for a built conveyor needing maintenance.
                int maint_x = -1, maint_y = -1;
                float worst_condition = 1.0f;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = pos.x + dx, ny = pos.y + dy;
                        if (grid_.at(nx, ny) == TileType::Conveyor) {
                            const auto& cd = grid_.data_at(nx, ny);
                            if (cd.built && cd.conveyor_condition < worst_condition) {
                                worst_condition = cd.conveyor_condition;
                                maint_x = nx; maint_y = ny;
                            }
                        }
                    }
                if (maint_x >= 0) {
                    auto& td = grid_.data_at(maint_x, maint_y);
                    float restored = std::min(config_.maintain_rate, 1.0f - td.conveyor_condition);
                    td.conveyor_condition += restored;
                    needs.purpose = std::max(0.0f,
                        needs.purpose - config_.work_purpose_gain * 0.5f);
                    if (restored > 0.001f)
                        emit_log(agent.id, "maintained conveyor at (" +
                                 std::to_string(maint_x) + "," + std::to_string(maint_y) +
                                 ") cond=" + ff2(td.conveyor_condition));
                }
                break;
            }

            case ActionType::DISMANTLE: {
                // Agent stands ADJACENT to the conveyor.
                // Scan 8-neighborhood for a built conveyor that is either:
                //  a) a dead-end (flow goes nowhere useful), or
                //  b) blocking a path (walkable tiles on opposite sides).
                // If found, tear it down: return partial material to inventory,
                // convert tile to Floor, record who dismantled it and when.
                int dism_x = -1, dism_y = -1;
                float best_score = 0.0f;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = pos.x + dx, ny = pos.y + dy;
                        if (grid_.at(nx, ny) != TileType::Conveyor) continue;
                        const auto& cd = grid_.data_at(nx, ny);
                        if (!cd.built) continue;
                        float score = 0.0f;
                        if (grid_.is_conveyor_blocking_path(nx, ny))
                            score += 2.0f;  // strong reason: blocks passage
                        // Check dead-end
                        auto [tx, ty] = grid_.conveyor_target(nx, ny);
                        if (tx >= 0 && tx < grid_.width() && ty >= 0 && ty < grid_.height()) {
                            TileType tt = grid_.at(tx, ty);
                            bool useful = (tt == TileType::Storage || tt == TileType::Exit ||
                                          (tt == TileType::Conveyor && grid_.data_at(tx, ty).built));
                            if (!useful) score += 1.0f;  // dead-end
                        }
                        if (score > best_score) {
                            best_score = score;
                            dism_x = nx; dism_y = ny;
                        }
                    }
                if (dism_x >= 0 && best_score > 0.0f) {
                    float cost = grid_.dismantle_conveyor(dism_x, dism_y);
                    if (cost > 0.0f) {
                        // Return partial material
                        float refund = cost * config_.dismantle_return;
                        inv.raw_material = std::min(10.0f,
                                                    inv.raw_material + refund);
                        // Record who dismantled and when
                        auto& td = grid_.data_at(dism_x, dism_y);
                        td.dismantled_by = agent.id;
                        td.dismantled_at_tick = tick_;
                        td.original_type = 0;  // was conveyor
                        // Small purpose boost — they're improving the factory
                        needs.purpose = std::max(0.0f,
                            needs.purpose - config_.work_purpose_gain * 0.3f);
                        emit_log(agent.id, "DISMANTLED conveyor at (" +
                                 std::to_string(dism_x) + "," + std::to_string(dism_y) +
                                 ") refund=" + ff2(refund));
                    }
                }
                break;
            }

            case ActionType::SABOTAGE: {
                // S3+S4: Irrational destruction driven by stress.
                // Find nearest built infrastructure and damage it.
                // On each tick of sabotage, roll for redemption or suicide.
                int sab_x = -1, sab_y = -1;
                // Priority: conveyors > machines
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = pos.x + dx, ny = pos.y + dy;
                        if (nx < 0 || nx >= grid_.width() || ny < 0 || ny >= grid_.height()) continue;
                        if (grid_.at(nx, ny) == TileType::Conveyor && grid_.data_at(nx, ny).built) {
                            if (sab_x < 0) { sab_x = nx; sab_y = ny; }
                        }
                        if (grid_.at(nx, ny) == TileType::Machine && grid_.data_at(nx, ny).built) {
                            // Prefer machines — they hurt the factory more
                            sab_x = nx; sab_y = ny;
                        }
                    }

                if (sab_x >= 0) {
                    auto& td = grid_.data_at(sab_x, sab_y);
                    if (grid_.at(sab_x, sab_y) == TileType::Conveyor) {
                        // Damage conveyor condition
                        td.conveyor_condition = std::max(0.0f, td.conveyor_condition - 0.15f);
                        if (td.conveyor_condition < 0.1f && td.built) {
                            td.built = false;
                            td.conveyor_contents = 0.0f;
                        }
                    } else if (grid_.at(sab_x, sab_y) == TileType::Machine) {
                        // Damage machine: revert build progress
                        td.build_progress = std::max(0.0f, td.build_progress - 0.3f);
                        if (td.build_progress <= 0.0f) {
                            td.built = false;
                        }
                    }

                    stress.sabotage_count++;
                    sabotages_total_++;

                    // Social damage: nearby agents notice and lose trust
                    auto nearby = registry_.view<PositionComponent, AgentComponent>();
                    for (auto ne : nearby) {
                        if (ne == e) continue;
                        auto& na = registry_.get<AgentComponent>(ne);
                        if (!na.alive) continue;
                        auto& np = registry_.get<PositionComponent>(ne);
                        int d = std::abs(np.x - pos.x) + std::abs(np.y - pos.y);
                        if (d <= 3) {
                            social_.negative_interaction(agent.id, na.id, tick_);
                            // Witness stress contagion: seeing sabotage is disturbing
                            if (registry_.all_of<StressComponent>(ne)) {
                                auto& ns = registry_.get<StressComponent>(ne);
                                ns.value = std::min(1.0f, ns.value + 0.005f);
                            }
                        }
                    }

                    emit_log(agent.id, "SABOTAGED infrastructure at (" +
                             std::to_string(sab_x) + "," + std::to_string(sab_y) + ")");

                    // S4: Redemption roll — the epiphany
                    // "In the act of destruction, they see the collective suffering."
                    stress.can_redeem = true;
                    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
                    float redemption_roll = roll(rng_);
                    float suicide_roll = roll(rng_);

                    if (redemption_roll < config_.redemption_chance) {
                        // REDEMPTION — the agent is transformed
                        stress.state = StressState::REDEEMED;
                        stress.value = std::max(0.0f, stress.value - 0.5f);
                        // Permanent personality shift: high collectivism, low compliance
                        // They help others selflessly but don't serve the factory
                        personality.compliance *= 0.5f;
                        personality.gregariousness = std::min(1.0f, personality.gregariousness + 0.3f);
                        // Meaning burst — they found purpose in the collective
                        needs.meaning = std::max(0.0f, needs.meaning - 0.5f);
                        redemptions_total_++;
                        emit_log(agent.id, "REDEEMED — saw collective suffering, became a martyr");
                    } else if (suicide_roll < config_.suicide_chance) {
                        // SUICIDE — the agent self-destructs
                        // Their death impacts nearby agents deeply
                        agent.alive = false;
                        agent.cause_of_death = "suicide";
                        suicides_total_++;
                        // Nearby agents are traumatized by witnessing it
                        for (auto ne : nearby) {
                            if (ne == e) continue;
                            auto& na = registry_.get<AgentComponent>(ne);
                            if (!na.alive) continue;
                            auto& np = registry_.get<PositionComponent>(ne);
                            int d = std::abs(np.x - pos.x) + std::abs(np.y - pos.y);
                            if (d <= 3 && registry_.all_of<StressComponent>(ne)) {
                                auto& ns = registry_.get<StressComponent>(ne);
                                ns.trauma = std::min(1.0f, ns.trauma + 0.05f);
                                ns.value = std::min(1.0f, ns.value + 0.1f);
                                social_.negative_interaction(agent.id, na.id, tick_);
                            }
                        }
                        emit_log(agent.id, "SUICIDE — destroyed by the machine");
                    }
                }
                break;
            }

            case ActionType::IDLE:
            default:
                break;
        }

        // A3: Noncompliance tracking — the factory watches.
        // Productive actions reduce noncompliance; everything else increases it.
        // HiddenSpace tiles are sanctuaries: noncompliance doesn't accumulate there.
        bool on_hidden_space = (grid_.at(pos.x, pos.y) == TileType::HiddenSpace);
        if (!on_hidden_space) {
            bool productive = (action.current == ActionType::GATHER ||
                               action.current == ActionType::BUILD ||
                               action.current == ActionType::WORK ||
                               action.current == ActionType::MAINTAIN);
            if (productive) {
                agent.noncompliance = std::max(0.0f, agent.noncompliance - 0.02f);
            } else {
                // Faction members near other faction members get a shield
                bool faction_shield = false;
                if (agent.faction_id >= 0) {
                    auto nearby = registry_.view<PositionComponent, const AgentComponent>();
                    int faction_neighbors = 0;
                    for (auto ne : nearby) {
                        if (ne == e) continue;
                        auto& na = registry_.get<AgentComponent>(ne);
                        if (!na.alive || na.faction_id != agent.faction_id) continue;
                        auto& np = registry_.get<PositionComponent>(ne);
                        if (std::abs(np.x - pos.x) + std::abs(np.y - pos.y) <= 2)
                            faction_neighbors++;
                    }
                    if (faction_neighbors >= 2) faction_shield = true;
                }
                if (!faction_shield) {
                    float nc_increase = 0.01f;
                    // SABOTAGE is explicitly anti-factory — heavy noncompliance
                    if (action.current == ActionType::SABOTAGE) nc_increase = 0.05f;
                    agent.noncompliance = std::min(1.0f, agent.noncompliance + nc_increase);
                }
            }
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
                float stored_total = d.stored_food + d.stored_raw_food
                                   + d.stored_raw_material + d.stored_construction_material;
                float space = d.storage_capacity - stored_total;
                if (space > 0.001f) {
                    float deposit = std::min(remaining, space);
                    if (type == ResourceType::FOOD) {
                        d.stored_food += deposit;
                    } else if (type == ResourceType::RAW_FOOD) {
                        d.stored_raw_food += deposit;
                    } else if (type == ResourceType::CONSTRUCTION_MATERIAL) {
                        d.stored_construction_material += deposit;
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

float Simulation::pull_raw_material_from_adjacent_storage(int px, int py, float max_amount) {
    float remaining = max_amount;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (remaining <= 0.001f) return max_amount - remaining;
            int nx = px + dx, ny = py + dy;
            if (grid_.at(nx, ny) != TileType::Storage) continue;
            auto& d = grid_.data_at(nx, ny);
            float take = std::min(remaining, d.stored_raw_material);
            if (take > 0.0f) {
                d.stored_raw_material -= take;
                remaining -= take;
            }
        }
    return max_amount - remaining;
}

float Simulation::pull_construction_material_from_adjacent_storage(int px, int py, float max_amount) {
    float remaining = max_amount;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (remaining <= 0.001f) return max_amount - remaining;
            int nx = px + dx, ny = py + dy;
            if (grid_.at(nx, ny) != TileType::Storage) continue;
            auto& d = grid_.data_at(nx, ny);
            float take = std::min(remaining, d.stored_construction_material);
            if (take > 0.0f) {
                d.stored_construction_material -= take;
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
