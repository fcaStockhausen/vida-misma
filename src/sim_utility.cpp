#include "simulation.h"
#include <algorithm>
#include <cmath>

void Simulation::system_compute_utility() {
    // ================================================================
    // GRID-LEVEL SUPPLY-CHAIN SIGNALS (computed once, used by all agents)
    // Replaces 5 redundant per-agent loops with a single scan.
    // ================================================================
    int total_machines = 0, built_machines = 0;
    int built_food_machines = 0;
    float total_stor_food = 0.0f;
    for (int gy = 0; gy < grid_.height(); gy++)
        for (int gx = 0; gx < grid_.width(); gx++) {
            if (grid_.at(gx, gy) == TileType::Machine) {
                total_machines++;
                if (grid_.data_at(gx, gy).built) {
                    built_machines++;
                    if (grid_.data_at(gx, gy).machine_type == MachineType::Food)
                        built_food_machines++;
                }
            } else if (grid_.at(gx, gy) == TileType::Storage) {
                total_stor_food += grid_.data_at(gx, gy).stored_food;
            }
        }

    float built_ratio = (total_machines > 0)
        ? (float)built_machines / (float)total_machines : 0.0f;
    float infra_gap = 1.0f - built_ratio;

    // Food supply ratio: how much food we have vs how much we need.
    // Includes BOTH storage food AND agent inventory food, since agents
    // carry bootstrap food and that's real supply even if not in storages.
    float total_agent_food = 0.0f;
    {
        auto food_view = registry_.view<InventoryComponent, const AgentComponent>();
        for (auto fe : food_view) {
            if (registry_.get<AgentComponent>(fe).alive)
                total_agent_food += registry_.get<InventoryComponent>(fe).food;
        }
    }
    float total_food_supply = total_stor_food + total_agent_food;
    float food_consumption_rate = (float)alive_count() * config_.hunger_decay;
    // Ratio: total food / (consumption rate * 100 ticks buffer)
    // 2.0 = abundant, 1.0 = ~100 ticks of buffer, <1.0 = deficit
    float food_supply_ratio = (food_consumption_rate > 0.001f)
        ? std::min(2.0f, total_food_supply / (food_consumption_rate * 100.0f))
        : 2.0f;

    // Community pressure: storage buffer × external supply-chain health.
    // Empty storage → 1, abundant → 0. Amplified when factory_health is low.
    float community_food   = total_stor_food;  // reuse the pre-computed value
    float storage_pressure = std::max(0.0f, 1.0f - community_food / 30.0f);
    float external_amp     = 1.0f + (1.0f - factory_health_);  // 1 at full, 2 at zero health
    float community_pressure = std::min(1.5f, storage_pressure * external_amp);

    auto view = registry_.view<NeedsComponent, PersonalityComponent,
                               InventoryComponent, ActionComponent,
                               const AgentComponent, StressComponent>();
    for (auto e : view) {
        if (!registry_.get<AgentComponent>(e).alive) continue;

        auto& needs      = registry_.get<NeedsComponent>(e);
        auto& personality = registry_.get<PersonalityComponent>(e);
        auto& inv        = registry_.get<InventoryComponent>(e);
        auto& action     = registry_.get<ActionComponent>(e);
        auto& soc        = registry_.get<SocialComponent>(e);
        auto  pos        = registry_.get<PositionComponent>(e);
        auto  ag         = registry_.get<AgentComponent>(e);
        auto& stress     = registry_.get<StressComponent>(e);

        // ================================================================
        // STICKINESS (The Sims / RimWorld pattern):
        // Once an agent commits to WORK or BUILD, it stays committed
        // for a duration. This prevents the volatility where agents
        // walk toward a machine for 30 ticks then switch away.
        // Critical needs (EAT when starving) CAN override stickiness.
        // ================================================================
        if (action.sticky_ticks > 0 && action.current == action.sticky_action) {
            // Only critical survival needs can break commitment:
            // hunger > 0.8 is "starving" — override anything
            bool survival_override = (needs.hunger > 0.8f && action.sticky_action != ActionType::EAT);
            if (!survival_override) {
                action.sticky_ticks--;
                // Store last utilities for debugging but keep current action
                action.last_utility_gather = 0.0f;
                action.last_utility_build  = 0.0f;
                action.last_utility_work   = 0.0f;
                action.last_utility_eat    = 0.0f;
                continue;  // skip re-evaluation entirely
            }
            // Survival override: break stickiness
            action.sticky_ticks = 0;
        }

        float alpha = config_.urgency_alpha;
        auto urgency = [alpha](float need) -> float {
            return std::pow(need, alpha);
        };

        // ================================================================
        // ONI-STYLE URGENCY CURVES (Oxygen Not Included / The Sims pattern)
        //
        // The old flat urgency(need) = need^2 works for non-critical needs
        // but fails for survival needs. The problem:
        //   urgency(0.5) = 0.25, urgency(0.8) = 0.64  (only 2.5x)
        //   GATHER/BUILD easily dominate even when agents are starving.
        //
        // ONI solution: each need type has its own urgency curve shape.
        // Survival needs (hunger, rest) have S-curves that stay LOW at
        // moderate levels (agents tolerate some hunger) but EXPLODE at
        // critical levels (hunger > 0.7 becomes emergency).
        //
        // Non-survival needs (social, expression, purpose) use the flat
        // power curve — they're important but never override survival.
        //
        // The sigmoid/exponential shapes come from ONI's "bladder" and
        // "hunger" meters: barely noticeable at 30-50%, oppressive at 70%,
        // game-ending at 90%.
        // ================================================================

        // SURVIVAL URGENCY: S-curve (sigmoid) — stays low until threshold,
        // then spikes exponentially. This creates the "tolerate then panic"
        // behavior that ONI uses.
        //   sigmoid(x) = x^4 for x in [0,1]: very flat until 0.7, then steep
        //   At 0.3 → 0.008, 0.5 → 0.063, 0.7 → 0.240, 0.8 → 0.410, 0.9 → 0.656
        //   Compare old: 0.3→0.09, 0.5→0.25, 0.7→0.49, 0.8→0.64, 0.9→0.81
        //   Key: old system has urgency(0.5)/urgency(0.9) = 0.31 (too close)
        //        new system has urgency(0.5)/urgency(0.9) = 0.10 (bigger gap)
        auto survival_urgency = [](float need) -> float {
            return need * need * need * need;  // x^4: S-curve
        };

        // CRITICAL ESCALATION: when need > 0.75, add an exponential spike.
        // This is the ONI "red alert" zone where nothing else matters.
        // Pattern from The Sims: bladder/hunger at critical levels override
        // all other actions regardless of their utility.
        //   spike(x) = 0 for x < 0.75
        //   spike(0.8) = 0.5, spike(0.9) = 2.0, spike(0.95) = 4.5
        auto critical_spike = [](float need) -> float {
            if (need < 0.75f) return 0.0f;
            float t = (need - 0.75f) / 0.25f;  // 0..1 in danger zone
            return t * t * 5.0f;  // exponential: up to 5.0 at need=1.0
        };

        // Per-need urgency with appropriate curves:
        float u_hunger     = survival_urgency(needs.hunger) + critical_spike(needs.hunger);
        float u_rest       = survival_urgency(needs.rest)   + critical_spike(needs.rest) * 0.5f;
        float u_social     = urgency(needs.social);       // flat power curve
        float u_expression = urgency(needs.expression);   // flat power curve
        float u_purpose    = urgency(needs.purpose);      // flat power curve

        // B4: Meaning crisis erodes compliance.
        // Being productive but unfulfilled makes agents less willing to serve.
        float effective_compliance = personality.compliance;
        if (needs.meaning > 0.7f) {
            effective_compliance *= (1.0f - (needs.meaning - 0.7f) * 1.5f);
            // At meaning=1.0, compliance is reduced by 45%
        }

        // === SOCIAL MODIFIERS ===
        // Mood affects productivity: low mood = less effective work
        float mood_factor = 0.5f + 0.5f * soc.mood;

        // Compute average trust with nearby agents (for SOCIALIZE bonus)
        float nearby_trust = 0.0f;
        int nearby_count = 0;
        {
            auto alive_view = registry_.view<PositionComponent, const AgentComponent>();
            for (auto other : alive_view) {
                if (other == e) continue;
                if (!registry_.get<AgentComponent>(other).alive) continue;
                auto& opos = registry_.get<PositionComponent>(other);
                int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
                if (d <= 3) {
                    int oid = registry_.get<AgentComponent>(other).id;
                    nearby_trust += social_.get_rel(ag.id, oid).trust;
                    nearby_count++;
                }
            }
            if (nearby_count > 0) nearby_trust /= nearby_count;
            else nearby_trust = 0.5f;
        }

        // Food only comes from machines now → only check inventory food and storage.
        bool has_food     = inv.food > 0.01f;
        bool storage_near = has_adjacent_storage_with_food(e);
        bool can_eat      = has_food || storage_near;

        // Only scrap is gatherable in this model.
        bool scrap_available = grid_.find_nearest(TileType::ScrapPile,
            pos.x, pos.y).first >= 0;

        // Are there unbuilt machines?
        bool unbuilt_exists = grid_.find_nearest_unbuilt_machine(pos.x, pos.y).first >= 0;

        // Are there built machines?
        bool built_exists = grid_.find_nearest_built_machine(pos.x, pos.y).first >= 0;

        // === ACTION UTILITIES ===

        // Personal food buffer (processed only, since FoodSource is gone).
        float food_security = std::min(1.0f, inv.food / 2.0f);

        // ================================================================
        // GATHER: base drive from incomplete infrastructure + purpose/hunger.
        // Scales with infra_gap (fewer unbuilt → less urgency to gather).
        // ADD: food urgency boost when no food production exists.
        // ================================================================
        float u_gather = 0.0f;
        if (scrap_available) {
            // Low material indicator: agent has nothing to build with
            float low_mat = std::max(0.0f, 1.0f - inv.raw_material / 2.0f);

            // Factory health crisis amplifies
            float gather_urgency = 1.0f + (1.0f - factory_health_) * 2.0f;

            // Base drive: compliance × infrastructure gap × material scarcity
            // infra_gap from grid-level signal — no redundant per-agent loop
            float gather_base = effective_compliance * infra_gap * low_mat * 1.5f;

            // Food urgency boost: when food is low and no food machines exist,
            // gathering is the only path to food (gather→build FOOD machine→work it)
            if (built_food_machines == 0 || food_supply_ratio < 0.5f) {
                gather_base += u_hunger * 0.5f;
            }

            float purpose_drive = effective_compliance * u_purpose * 0.4f;
            float hunger_drive = u_hunger * 0.2f;  // hungry agents know: no material → no food

            // Raw food gathering: when food is low and FoodSource tiles exist
            float raw_food_drive = 0.0f;
            if (inv.raw_food < 0.5f) {
                bool food_source_available = grid_.find_nearest(TileType::FoodSource,
                    pos.x, pos.y).first >= 0;
                if (food_source_available) {
                    float food_gap = std::min(1.0f, 1.0f - food_supply_ratio);
                    raw_food_drive = u_hunger * 0.6f * (0.5f + food_gap);
                }
            }

            u_gather = (gather_base + purpose_drive + hunger_drive + raw_food_drive)
                      * mood_factor * gather_urgency;
        }

        // ================================================================
        // BUILD: infrastructure completion drive.
        // Scales with infra_gap (phase transition). Per-type priority
        // for FOOD machines when food_supply_ratio < 1.0.
        // ================================================================
        float u_build = 0.0f;
        bool unbuilt_ez_exists = grid_.find_nearest_unbuilt_eatingzone(pos.x, pos.y).first >= 0;
        bool built_ez_exists   = grid_.find_nearest_built_eatingzone(pos.x, pos.y).first >= 0;
        bool unbuilt_conveyor  = grid_.find_nearest_conveyor_to_build(pos.x, pos.y).first >= 0;

        // Factory health crisis amplifies BUILD urgency: infrastructure = survival.
        float build_urgency = 1.0f + (1.0f - factory_health_) * 4.0f; // 1x at full, 5x at zero

        // Material availability boost: having material should STRONGLY push toward BUILD
        float mat_readiness = std::min(1.0f, inv.raw_material / 2.0f);

        // Use grid-level build_infra_gap — no redundant per-agent loop
        float build_infra_gap = infra_gap;

        // Per-type food priority bonus: when food is scarce, building FOOD machines
        // is more valuable than building other types. This replaces the old blunt
        // infra_gap halving with a targeted per-type signal.
        float food_build_priority = 1.0f;
        if (food_supply_ratio < 1.0f && built_food_machines < 6) {
            // Up to 2x priority at zero food supply, scaling with how many food
            // machines are still needed (6 = full complement for 24 agents)
            float food_machine_gap = 1.0f - (float)built_food_machines / 6.0f;
            food_build_priority = 1.0f + food_machine_gap * (1.0f - food_supply_ratio);
        }

        if (inv.raw_material > 0.1f) {
            // Sub 1: machine — strongest build target. Having material + unbuilt machines
            // is the clearest "do this now" signal in the game.
            float u_build_mach = 0.0f;
            if (unbuilt_exists) {
                auto near_m = grid_.find_nearest_unbuilt_machine(pos.x, pos.y);
                float finish_bonus = 0.0f;
                if (near_m.first >= 0) {
                    const auto& td = grid_.data_at(near_m.first, near_m.second);
                    if (td.build_cost > 0.0f) {
                        finish_bonus = (td.build_progress / td.build_cost) * 2.0f;
                    }
                }
                // Base: strong compliance × material readiness × infrastructure gap
                float base = effective_compliance * mat_readiness * build_infra_gap * 2.0f;

                // Apply food-build priority if nearest unbuilt machine is FOOD type
                if (near_m.first >= 0) {
                    const auto& td = grid_.data_at(near_m.first, near_m.second);
                    if (td.machine_type == MachineType::Food) {
                        base *= food_build_priority;
                    }
                }

                // Purpose supplement (not primary driver)
                float purpose_sup = effective_compliance * u_purpose * 0.8f;
                // Community pressure
                float community = effective_compliance * community_pressure * 0.6f;

                u_build_mach = (base + purpose_sup + community) + finish_bonus;
                u_build_mach *= mood_factor * build_urgency;
            }

            // Sub 2: continue an unbuilt EatingZone frame (high finish_bonus pull)
            float u_build_ez = 0.0f;
            if (unbuilt_ez_exists) {
                auto near_ez = grid_.find_nearest_unbuilt_eatingzone(pos.x, pos.y);
                const auto& td = grid_.data_at(near_ez.first, near_ez.second);
                float finish_bonus = (td.build_cost > 0.0f)
                    ? (td.build_progress / td.build_cost) * 1.5f : 0.0f;
                u_build_ez = effective_compliance * u_purpose * 1.0f * mat_readiness + finish_bonus;
            }

            // Sub 3: initiate a new EatingZone — only when none exist (built OR being built)
            //        and a valid site is reachable.
            float u_build_new_ez = 0.0f;
            if (!built_ez_exists && !unbuilt_ez_exists) {
                auto site = grid_.find_nearest_valid_eatingzone_site(
                    pos.x, pos.y, config_.eatingzone_min_dist_machine);
                if (site.first >= 0) {
                    u_build_new_ez = effective_compliance * u_purpose * 1.0f * mat_readiness;
                }
            }

            u_build = std::max({u_build_mach, u_build_ez, u_build_new_ez});
        }

        // Conveyor build: cheaper, independent sub-target.
        // Boosted by infrastructure gap — conveyors connect the factory.
        {
            float u_build_conv = 0.0f;
            if (unbuilt_conveyor && inv.raw_material > 0.05f) {
                float conv_mat = std::min(1.0f, inv.raw_material / 1.0f);
                float conv_base = effective_compliance * conv_mat * build_infra_gap * 1.5f;
                float conv_sup  = effective_compliance * u_purpose * 1.0f;
                float conv_community = community_pressure * 1.2f * conv_mat;
                u_build_conv = conv_base + conv_sup + conv_community;
                u_build_conv *= mood_factor * build_urgency;
            }
            u_build = std::max(u_build, u_build_conv);
        }

        // ================================================================
        // WORK: operate built machines to produce food/material/output.
        // Supply-chain-aware pull replaces the old flat duty_drive.
        //
        // Key design:
        //   - work_pull scales with built_ratio (more built = more to operate)
        //   - food_work_urgency spikes when food_supply_ratio is LOW
        //   - At built_ratio < 0.3, WORK stays below GATHER/BUILD naturally
        //   - At built_ratio > 0.5 with food stress, WORK dominates
        // ================================================================
        float u_work = 0.0f;
        if (stress.state != StressState::BROKEN) {
        if (built_exists) {
            float health_urgency = 1.0f + (1.0f - factory_health_) * 3.0f;

            // Input readiness: WORK is only valuable when inputs are available.
            // Two tiers:
            //   1. Carrying raw_food or raw_material (gathered resources) → need >0.5 total
            //   2. Carrying construction_material (refined product) → ANY amount counts
            //      because it comes from MaterialsMachine in small per-tick amounts.
            float raw_total = inv.raw_food + inv.raw_material + inv.construction_material;
            float input_readiness = 0.0f;
            if (inv.construction_material > 0.05f) {
                // Refined product from MaterialsMachine: even small amounts are valuable
                // (OutputMachine pulls from storage, agent just needs to be there)
                input_readiness = 1.0f + inv.construction_material * 5.0f;
            } else if (raw_total > 0.5f) {
                // Raw gathered resources: stock up before going to machine
                input_readiness = std::min(3.5f, raw_total);
                if (inv.raw_food > 0.1f) input_readiness += 0.5f;
            }

            // WORK pull: scales with built_ratio (more built = more to operate)
            float work_pull = effective_compliance * built_ratio * 2.0f;

            // Food urgency: when food supply is LOW, WORK becomes critical.
            // ONI pattern: "mealtime panic" — when food stores drop, ALL
            // duplicants prioritize food production above everything else.
            // The urgency curve is exponential, not linear:
            //   ratio > 1.5: no urgency (food is abundant)
            //   ratio = 1.0: mild urgency (100 ticks of buffer)
            //   ratio = 0.5: high urgency (50 ticks of buffer)
            //   ratio = 0.2: CRITICAL (20 ticks of buffer) → 6x multiplier
            //   ratio = 0.0: EMERGENCY → 10x multiplier
            float food_work_urgency = 1.0f;
            if (built_food_machines > 0 && food_supply_ratio < 1.5f) {
                // Exponential urgency: steeper than linear
                float deficit = 1.5f - food_supply_ratio;  // 0..1.5
                food_work_urgency = 1.0f + deficit * deficit * 4.0f;  // 1.0..10.0
            }

            // Personal hunger adds to work urgency (secondary driver)
            // Only if agent has meaningful inputs
            float personal_hunger = (input_readiness > 0.0f) ? (u_hunger * 0.5f) : 0.0f;

            // Purpose and community drives — only meaningful if can actually produce
            float purpose_drive = (input_readiness > 0.0f)
                ? ((1.0f - personality.laziness) * u_purpose * 0.3f) : 0.0f;
            float community = (input_readiness > 0.0f)
                ? (effective_compliance * community_pressure * 0.6f) : 0.0f;

            u_work = (work_pull * food_work_urgency * input_readiness
                      + personal_hunger + purpose_drive + community)
                     * mood_factor * health_urgency;
        }
        } // end BROKEN gate

        // EAT: survival-critical action. Uses urgency curve directly.
        // The ONI/Sims pattern: eating at low hunger is a preference,
        // eating at critical hunger is MANDATORY and overrides everything.
        // Old: u_eat = u_hunger * 1.3 = 0.49 * 1.3 = 0.64 at hunger=0.7
        //      GATHER could be 0.8+ and win — agents die while gathering.
        // New: u_eat at hunger=0.9 = 0.656 + 2.0 = 2.656 * 1.3 = 3.45
        //      Nothing else reaches 3.0 — eating ALWAYS wins at critical.
        float u_eat = 0.0f;
        if (can_eat) {
            float eat_weight = 1.3f;
            if (food_security > 0.3f) eat_weight = 1.8f;
            u_eat = u_hunger * eat_weight;
        }

        // REST: survival need like hunger. Near-max escalation.
        // Uses survival urgency so rest at 0.9+ overrides most actions.
        float rest_weight = 0.4f + 0.6f * personality.laziness;
        if (needs.rest > 0.7f) rest_weight *= 1.5f;
        if (needs.rest > 0.9f) rest_weight *= 2.0f;
        float u_rest_action = rest_weight * u_rest;  // u_rest already uses survival_urgency

        // SOCIALIZE: boosted by nearby trust (known agents are more attractive)
        float gregariousness_mult = 1.0f;
        if (stress.state == StressState::DISSOCIATED) gregariousness_mult = 0.7f;
        if (stress.state == StressState::BROKEN) gregariousness_mult = 0.3f;
        float effective_gregariousness = personality.gregariousness * gregariousness_mult
                                       * (1.0f - stress.trauma * config_.trauma_social_impact);
        float u_socialize = effective_gregariousness * u_social;
        u_socialize *= (0.5f + 0.5f * nearby_trust);  // high trust = more rewarding
        u_socialize += soc.influence * 0.1f;           // influential agents socialize more

        // CREATE: only viable if there's an OpenSpace tile to reach
        bool open_space_available = grid_.find_nearest(TileType::OpenSpace,
            pos.x, pos.y).first >= 0;
        float u_create = 0.0f;
        if (open_space_available) {
            u_create = personality.artistry * u_expression;
            // B4: CREATE is more attractive when meaning is unfulfilled
            if (needs.meaning > 0.4f) {
                u_create += personality.artistry * needs.meaning * 0.3f;
            }
            // S2: DISSOCIATED agents are drawn to CREATE (retreat into art)
            if (stress.state == StressState::DISSOCIATED) {
                u_create *= 1.3f;
            }
        }

        // EXPLORE
        float u_explore = personality.curiosity * u_purpose * 0.5f;
        // B4: Agents with unfulfilled meaning are drawn to explore
        if (needs.meaning > 0.4f) {
            u_explore += personality.curiosity * needs.meaning * 0.3f;
        }
        // S2: DISSOCIATED agents seek escape through exploration
        if (stress.state == StressState::DISSOCIATED) {
            u_explore *= 1.3f;
        }

        // MAINTAIN: repair degraded conveyors. Compliance-driven, scales with degradation.
        float u_maintain = 0.0f;
        {
            auto conv = grid_.find_nearest_conveyor_needing_maintain(pos.x, pos.y, 0.9f);
            if (conv.first >= 0) {
                const auto& cd = grid_.data_at(conv.first, conv.second);
                float degradation = 1.0f - cd.conveyor_condition;
                float hunger_gate = std::max(0.0f, 1.0f - u_hunger * 2.0f);
                u_maintain = effective_compliance * degradation * u_purpose * 1.5f * hunger_gate
                           + community_pressure * degradation * 0.8f * hunger_gate;
                u_maintain *= mood_factor;
            }
        }

        // DISMANTLE: consider tearing down conveyors that are dead-ends or blocking paths.
        float u_dismantle = 0.0f;
        {
            bool blocking_nearby = false;
            bool dead_end_nearby = grid_.find_nearest_dead_end_conveyor(pos.x, pos.y).first >= 0;
            for (int sy = std::max(0, pos.y - 8); sy < std::min(grid_.height(), pos.y + 8); sy++)
                for (int sx = std::max(0, pos.x - 8); sx < std::min(grid_.width(), pos.x + 8); sx++)
                    if (grid_.is_conveyor_blocking_path(sx, sy)) blocking_nearby = true;

            if (blocking_nearby || dead_end_nearby) {
                float reason_strength = blocking_nearby ? 1.5f : 0.6f;
                float hunger_gate = std::max(0.0f, 1.0f - u_hunger * 3.0f);
                float calm_gate = std::max(0.0f, soc.mood * 2.0f - 0.5f);
                u_dismantle = effective_compliance * reason_strength
                            * hunger_gate * calm_gate * (1.0f - personality.laziness * 0.5f)
                            * mood_factor;
                if (inv.raw_material > 2.0f) u_dismantle *= 0.3f;
            }
        }

        // S3: SABOTAGE — irrational destruction driven by chronic stress.
        float u_sabotage = 0.0f;
        if (stress.value >= config_.sabotage_stress_threshold
            && stress.state != StressState::REDEEMED) {
            float stress_drive = 0.0f;
            if (stress.state == StressState::HOSTILE_EUPHORIA) stress_drive = 1.2f;
            if (stress.state == StressState::BROKEN) stress_drive = 3.0f;
            if (stress.state == StressState::DISSOCIATED && stress.trauma > 0.3f)
                stress_drive = stress.trauma * 0.5f;
            stress_drive *= (1.0f + stress.trauma);
            stress_drive *= (1.0f - personality.compliance * 0.3f);
            float hunger_gate = std::max(0.0f, 1.0f - u_hunger * 1.5f);
            u_sabotage = stress_drive * hunger_gate;
        }

        // GET_FOOD: agent considers fetching a "vianda" from Storage when their inv.food
        // is low and they're not at the bottom of immediate hunger urgency.
        float u_get_food = 0.0f;
        {
            float room_in_inv = std::max(0.0f, config_.inv_food_cap - inv.food);
            bool any_storage_food = grid_.find_nearest_storage_with_food(pos.x, pos.y).first >= 0;
            if (any_storage_food && room_in_inv > 0.1f) {
                float pocket_emptiness = room_in_inv / config_.inv_food_cap;
                u_get_food = pocket_emptiness * (0.3f + u_hunger * 0.8f);
                if (built_ez_exists) u_get_food *= 1.3f;
            }
        }

        // SOCIAL LEARNING: agents observe what trusted neighbors are doing
        int worker_nearby = 0, gatherer_nearby = 0, builder_nearby = 0;
        {
            auto alive_view3 = registry_.view<PositionComponent, const AgentComponent,
                                               SocialComponent, ActionComponent>();
            for (auto other : alive_view3) {
                if (other == e) continue;
                if (!registry_.get<AgentComponent>(other).alive) continue;
                auto& opos = registry_.get<PositionComponent>(other);
                int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
                if (d > 3) continue;

                auto& oact = registry_.get<ActionComponent>(other);
                auto& osoc = registry_.get<SocialComponent>(other);
                int oid = registry_.get<AgentComponent>(other).id;
                const auto& rel = social_.get_rel(ag.id, oid);

                float trust_w = std::max(0.0f, rel.trust);
                float prox = 1.0f / (1.0f + (float)d);
                float weight = trust_w * (0.3f + 0.7f * osoc.influence) * prox;

                if (weight < 0.01f) continue;

                switch (oact.current) {
                    case ActionType::GATHER:   u_gather  += weight * 0.4f; break;
                    case ActionType::BUILD:    u_build   += weight * 0.5f; break;
                    case ActionType::WORK:     u_work    += weight * 0.3f; break;
                    case ActionType::MAINTAIN: u_maintain += weight * 0.3f; break;
                    case ActionType::GET_FOOD: u_get_food += weight * 0.2f; break;
                    default: break;
                }
                if (oact.current == ActionType::WORK) worker_nearby++;
                if (oact.current == ActionType::GATHER) gatherer_nearby++;
                if (oact.current == ActionType::BUILD) builder_nearby++;
            }
        }

        // Niche dampening: if too many nearby agents are doing the same action,
        // reduce its utility to encourage role diversity.
        if (worker_nearby >= 3) u_work *= 1.0f / (1.0f + (worker_nearby - 2) * 0.3f);
        if (gatherer_nearby >= 3) u_gather *= 1.0f / (1.0f + (gatherer_nearby - 2) * 0.3f);
        if (builder_nearby >= 3) u_build *= 1.0f / (1.0f + (builder_nearby - 2) * 0.3f);

        // Pick best action
        struct Scored { ActionType type; float score; };
        Scored options[] = {
            {ActionType::GATHER,    u_gather},
            {ActionType::BUILD,     u_build},
            {ActionType::WORK,      u_work},
            {ActionType::EAT,       u_eat},
            {ActionType::REST,      u_rest_action},
            {ActionType::SOCIALIZE, u_socialize},
            {ActionType::CREATE,    u_create},
            {ActionType::EXPLORE,   u_explore},
            {ActionType::GET_FOOD,  u_get_food},
            {ActionType::MAINTAIN,  u_maintain},
            {ActionType::DISMANTLE, u_dismantle},
            {ActionType::SABOTAGE,  u_sabotage},
        };

        float best_score = -1.0f;
        ActionType best_action = ActionType::IDLE;
        for (auto& opt : options) {
            if (opt.score > best_score) {
                best_score = opt.score;
                best_action = opt.type;
            }
        }

        // Small noise: random action
        std::uniform_real_distribution<float> noise(0.0f, 1.0f);
        if (noise(rng_) < 0.02f) {
            std::uniform_int_distribution<int> pick(0, 10);
            ActionType random_actions[] = {
                ActionType::GATHER, ActionType::BUILD, ActionType::WORK,
                ActionType::EAT, ActionType::REST, ActionType::SOCIALIZE,
                ActionType::CREATE, ActionType::EXPLORE, ActionType::GET_FOOD,
                ActionType::MAINTAIN, ActionType::DISMANTLE
            };
            best_action = random_actions[pick(rng_)];
        }

        action.current = best_action;

        // Set stickiness for WORK actions (The Sims pattern):
        // WORK requires walking to machines, then sustained execution.
        // BUILD doesn't need stickiness (it has build_progress for sustained execution).
        // GATHER doesn't need it (agents gather where they stand).
        if (best_action == ActionType::WORK) {
            if (action.sticky_action != best_action || action.sticky_ticks <= 0) {
                action.sticky_action = best_action;
                // Estimate distance to target machine for commitment duration.
                // Walk time + 15 ticks of actual work.
                // If no target yet, default to 30.
                int dist = 30;
                if (action.target_x >= 0 && action.target_y >= 0) {
                    dist = std::abs(pos.x - action.target_x) + std::abs(pos.y - action.target_y);
                }
                action.sticky_ticks = dist + 15;
            }
        } else {
            // Clear stickiness when switching away from WORK
            if (action.sticky_action == ActionType::WORK) {
                action.sticky_ticks = 0;
                action.sticky_action = ActionType::IDLE;
            }
        }

        action.last_utility_gather    = u_gather;
        action.last_utility_build     = u_build;
        action.last_utility_work      = u_work;
        action.last_utility_eat       = u_eat;
        action.last_utility_rest      = u_rest_action;
        action.last_utility_socialize = u_socialize;
        action.last_utility_create    = u_create;
        action.last_utility_explore   = u_explore;
        action.last_utility_get_food  = u_get_food;
    }
}
