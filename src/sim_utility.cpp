#include "simulation.h"
#include "production.h"
#include <algorithm>
#include <cmath>

namespace {

// Factory-pressure urgency multiplier: amplifies a utility as factory_health
// falls. Same formula was inlined three times (gather_urgency k=2.0, build_urgency
// k=2.0, health_urgency k=3.0) before this helper. Preserves both constants.
float factory_pressure(float factory_health, float k) {
    return 1.0f + (1.0f - factory_health) * k;
}

// CALM-mode "comfortable" dampener: when higher needs are unmet BUT the agent
// is fed, rested, and food is abundant, productive actions are dampened so
// cultural behavior can emerge. The 4-way conjunction was duplicated 3x
// (GATHER/BUILD/WORK). Returns `multiplier` when the conjunction holds, else 1.0f.
// GATHER/BUILD used 0.3f; WORK used 0.2f — the parameter preserves both.
float calm_comfortable_dampener(bool calm_mode, float higher_unmet,
                                float hunger, float rest, float food_ratio,
                                float multiplier) {
    if (!calm_mode) return 1.0f;
    if (higher_unmet > 0.6f && hunger < 0.3f && rest < 0.3f && food_ratio > 0.8f)
        return multiplier;
    return 1.0f;
}

// CALM-mode focus dampener (WORK-specific): in CALM mode, unmet higher needs
// gradually suppress work_pull. This is distinct from the comfortable dampener
// above (which is a hard 0.3x when fully comfortable). WORK applies BOTH:
// first the gradual focus, then the comfortable 0.2x multiplier.
float calm_work_focus(bool calm_mode, float higher_unmet) {
    if (!calm_mode) return 1.0f;
    float focus = 1.0f - higher_unmet;
    return 0.3f + 0.7f * focus;
}

// Maslow boost: higher-need actions (socialize/create/explore) are amplified
// when survival needs are well-satisfied. Two tiers. Was duplicated 3x with
// inconsistent secondary tiers (EXPLORE was missing its secondary).
float maslow_boost(float hunger, float rest, float primary, float secondary) {
    if (hunger < 0.3f && rest < 0.3f) return primary;
    if (hunger < 0.5f && rest < 0.5f) return secondary;
    return 1.0f;
}

// Survival urgency curves for A/B testing (Phase 2.1 of emergence redesign).
// The legacy system used x^4 + a separate critical_spike + an eat_weight boost
// + a HARD OVERRIDE zeroing 9 utilities. These variants attempt to produce the
// same "survival dominates at need>0.85" behavior from a SINGLE well-shaped
// curve, so the three patches become unnecessary.
//
// Design target (the constraint all variants must satisfy):
//   need=0.5  → low (0.02-0.08): work/build rationally dominate
//   need=0.7  → moderate (0.1-0.3): survival starts to compete
//   need=0.85 → high (1.5-3.0): survival clearly dominates
//   need=0.9  → very high (5-10): nothing else wins
//   need=1.0  → extreme (10+): emergency
float survival_urgency_v1(float need) {
    // Variant 1: steep pure exponential. Flat until 0.6, then 4th-power ramp.
    if (need < 0.6f) return need * need * 0.1f;
    float t = (need - 0.6f) / 0.4f;  // 0..1 above 0.6
    return t * t * t * t * 10.0f;     // up to 10.0 at need=1.0
}

float survival_urgency_v2(float need) {
    // Variant 2: extra-steep. 6th power above 0.6, sharper knee.
    if (need < 0.6f) return need * need * 0.1f;
    float t = (need - 0.6f) / 0.4f;
    float t3 = t * t * t;
    return t3 * t3 * t3 * 15.0f;      // t^9 up to 15.0 — very sharp
}

float survival_urgency_v3(float need) {
    // Variant 3: sigmoid. Smooth S-curve, no hard knee. Middle ground.
    // logistic centered at 0.7 with steepness 12.
    float s = 1.0f / (1.0f + std::exp(-12.0f * (need - 0.7f)));
    return s * 8.0f;                   // scaled so max ~8.0
}

float survival_urgency_variant(int variant, float need) {
    switch (variant) {
        case 1:  return survival_urgency_v1(need);
        case 2:  return survival_urgency_v2(need);
        case 3:  return survival_urgency_v3(need);
        default: return need * need * need * need;  // variant 0: legacy x^4
    }
}

}  // namespace

void Simulation::system_compute_utility() {
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
        auto& skills     = registry_.get<SkillsComponent>(e);
        auto  pos        = registry_.get<PositionComponent>(e);
        auto  ag         = registry_.get<AgentComponent>(e);
        auto& stress     = registry_.get<StressComponent>(e);
        bool no_culture = (config_.director_mode == DirectorMode::PRODUCTION_TEST);

        // Agents observe nearby physical state; they do not read colony-wide
        // inventories or the ProductionChain assessment.
        constexpr int observation_radius = Simulation::OBSERVATION_RADIUS;
        int total_machines = 0, built_machines = 0;
        int built_food_machines = 0, built_mat = 0, built_out = 0;
        int unbuilt_resources = 0;
        bool visible_food_source = false, visible_scrap = false;
        bool free_foodsource = false, free_scrap = false;
        bool unbuilt_ez_exists = false;
        bool built_conveyor = false, unbuilt_conveyor = false;
        bool any_storage_food = false, dismantle_candidate = false;
        std::pair<int, int> near_unbuilt_machine = {-1, -1};
        std::pair<int, int> near_unbuilt_ez = {-1, -1};
        std::pair<int, int> near_degraded_conveyor = {-1, -1};
        int unbuilt_machine_dist = 999999, unbuilt_ez_dist = 999999;
        int degraded_conveyor_dist = 999999;
        int degraded_conveyor_priority = -1;
        float total_stor_food = 0.0f;
        float total_stor_raw_food = 0.0f;
        float local_output = 0.0f;
        float raw_banked = inv.raw_material;
        for (int gy = std::max(0, pos.y - observation_radius);
             gy <= std::min(grid_.height() - 1, pos.y + observation_radius); gy++)
            for (int gx = std::max(0, pos.x - observation_radius);
                 gx <= std::min(grid_.width() - 1, pos.x + observation_radius); gx++) {
                if (std::abs(gx - pos.x) + std::abs(gy - pos.y) > observation_radius) continue;
                TileType tile = grid_.at(gx, gy);
                const auto& data = grid_.data_at(gx, gy);
                if (tile == TileType::Machine) {
                    total_machines++;
                    if (data.built) {
                        built_machines++;
                        if (data.machine_type == MachineType::Food) built_food_machines++;
                        else if (data.machine_type == MachineType::Materials) built_mat++;
                        else if (data.machine_type == MachineType::Output) built_out++;
                        raw_banked += data.stored_raw_material;
                        local_output += data.stored_output;
                    } else {
                        int distance = std::abs(gx - pos.x) + std::abs(gy - pos.y);
                        if (distance < unbuilt_machine_dist) {
                            unbuilt_machine_dist = distance;
                            near_unbuilt_machine = {gx, gy};
                        }
                    }
                } else if (tile == TileType::FoodSource || tile == TileType::ScrapPile) {
                    unbuilt_resources++;
                    if (tile == TileType::FoodSource) {
                        visible_food_source |= data.resource_amount > 0.01f;
                        free_foodsource |= data.claimed_by < 0 || data.claimed_by == ag.id;
                    } else {
                        visible_scrap |= data.resource_amount > 0.01f;
                        free_scrap |= data.claimed_by < 0 || data.claimed_by == ag.id;
                    }
                } else if (tile == TileType::Storage) {
                    total_stor_food += data.stored_food;
                    total_stor_raw_food += data.stored_raw_food;
                    raw_banked += data.stored_raw_material;
                    local_output += data.stored_output;
                    any_storage_food |= data.stored_food > 0.01f;
                } else if (tile == TileType::Conveyor) {
                    built_conveyor |= data.built;
                    if (data.built && data.conveyor_contents_type == ResourceType::RAW_MATERIAL)
                        raw_banked += data.conveyor_contents;
                    if (data.built && data.conveyor_contents_type == ResourceType::OUTPUT)
                        local_output += data.conveyor_contents;
                    if (!data.built) unbuilt_conveyor = true;
                    if (data.built && data.conveyor_condition < 0.9f) {
                        int distance = std::abs(gx - pos.x) + std::abs(gy - pos.y);
                        int priority = static_cast<int>(data.maintenance_priority);
                        if (priority > degraded_conveyor_priority
                            || (priority == degraded_conveyor_priority
                                && distance < degraded_conveyor_dist)) {
                            degraded_conveyor_priority = priority;
                            degraded_conveyor_dist = distance;
                            near_degraded_conveyor = {gx, gy};
                        }
                    }
                }
                if (tile == TileType::EatingZone) {
                    if (!data.built) {
                        unbuilt_ez_exists = true;
                        int distance = std::abs(gx - pos.x) + std::abs(gy - pos.y);
                        if (distance < unbuilt_ez_dist) {
                            unbuilt_ez_dist = distance;
                            near_unbuilt_ez = {gx, gy};
                        }
                    }
                }
                dismantle_candidate |= grid_.is_dismantle_candidate(gx, gy);
            }

        int visible_agents = 1;
        auto visible_view = registry_.view<PositionComponent, const AgentComponent>();
        for (auto other : visible_view) {
            if (other == e || !registry_.get<AgentComponent>(other).alive) continue;
            const auto& other_pos = registry_.get<PositionComponent>(other);
            if (std::abs(other_pos.x - pos.x) + std::abs(other_pos.y - pos.y)
                <= observation_radius) visible_agents++;
        }

        int local_machine_baseline = std::max(1, visible_agents / 12);
        int food_machine_need = std::max(0, local_machine_baseline - built_food_machines);
        int mat_machine_need = std::max(0, local_machine_baseline - built_mat);
        int out_machine_need = std::max(0, 1 - built_out);
        int total_machine_need = food_machine_need + mat_machine_need + out_machine_need;
        int total_infra = total_machines + unbuilt_resources;
        float built_ratio = total_infra > 0
            ? static_cast<float>(built_machines) / static_cast<float>(total_infra) : 0.0f;
        float infra_gap = total_machine_need > 0
            ? std::min(1.0f, static_cast<float>(total_machine_need) / 3.0f) : 0.0f;

        float total_food_supply = total_stor_food + inv.food;
        float food_consumption_rate = visible_agents * config_.hunger_decay;
        float food_supply_ratio = food_consumption_rate > 0.001f
            ? std::min(2.0f, total_food_supply / (food_consumption_rate * 100.0f)) : 2.0f;
        float storage_pressure = std::max(0.0f, 1.0f - total_stor_food / 30.0f);
        float policy_health = config_.external_supply_variant == 0 ? factory_health_ : 1.0f;
        float external_amp = 1.0f + (1.0f - policy_health);
        float community_pressure = std::min(1.5f, storage_pressure * external_amp);
        float raw_need = std::max(0.5f, visible_agents * 0.3f);
        float raw_gap = std::min(1.0f, raw_need / std::max(0.5f, raw_banked * 0.2f));

        // ================================================================
        // STICKINESS (The Sims / RimWorld pattern):
        // Once an agent commits to WORK or BUILD, it stays committed
        // for a duration. This prevents the volatility where agents
        // walk toward a machine for 30 ticks then switch away.
        // Critical needs (EAT when starving) CAN override stickiness.
        // ================================================================
        if (action.sticky_ticks > 0 && action.current == action.sticky_action) {
            bool survival_override = (needs.hunger > 0.8f && action.sticky_action != ActionType::EAT);
            bool chain_delivery = (action.sticky_action == ActionType::WORK &&
                                   inv.construction_material > 1.5f);
            bool still_feasible = false;
            if (action.sticky_action == ActionType::WORK
                && action.target_x >= 0 && action.target_y >= 0) {
                still_feasible = work_target_feasible(e, action.target_x, action.target_y);
            } else if (action.sticky_action == ActionType::BUILD
                       && action.target_x >= 0 && action.target_y >= 0) {
                TileType target = grid_.at(action.target_x, action.target_y);
                const auto& data = grid_.data_at(action.target_x, action.target_y);
                bool has_material = target == TileType::Machine
                    && data.machine_type == MachineType::Output
                    ? inv.construction_material > 0.05f : inv.raw_material > 0.05f;
                still_feasible = config_.allow_build && has_material
                    && (target == TileType::Floor || target == TileType::FoodSource
                        || target == TileType::ScrapPile
                        || ((target == TileType::Machine || target == TileType::Conveyor
                             || target == TileType::EatingZone) && !data.built));
            } else {
                still_feasible = action_feasible(e, action.sticky_action);
            }
            if (!survival_override && !chain_delivery && still_feasible) {
                action.sticky_ticks--;
                metrics_.action_selected[metric_index(action.current)]++;
                continue;
            }
            action.sticky_ticks = 0;
        }

        PlaceChoice rest_place{pos.x, pos.y, 0.0f};
        PlaceChoice social_place{pos.x, pos.y, 0.0f};
        PlaceChoice create_place{pos.x, pos.y, 0.0f};
        if (needs.rest > 0.001f)
            rest_place = find_preferred_place(e, ActionType::REST);
        if (!no_culture && needs.social > 0.15f)
            social_place = find_preferred_place(e, ActionType::SOCIALIZE);
        if (!no_culture && needs.expression > 0.25f)
            create_place = find_preferred_place(e, ActionType::CREATE);
        for (const auto& plan : {std::pair{ActionType::REST, rest_place},
                                 std::pair{ActionType::SOCIALIZE, social_place},
                                 std::pair{ActionType::CREATE, create_place}}) {
            size_t index = metric_index(plan.first);
            action.preferred_x[index] = plan.second.x;
            action.preferred_y[index] = plan.second.y;
            action.preferred_place_score[index] = plan.second.score;
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

        // Per-need urgency with appropriate curves.
        // Variant 0 (legacy): survival_urgency(x^4) + critical_spike (separate patch).
        // Variants 1-3: single survival_urgency_variant curve, no spike needed.
        int ucv = config_.urgency_curve_variant;
        float u_hunger, u_rest;
        if (ucv == 0) {
            u_hunger = survival_urgency(needs.hunger) + critical_spike(needs.hunger);
            u_rest   = survival_urgency(needs.rest)   + critical_spike(needs.rest) * 0.5f;
        } else {
            u_hunger = survival_urgency_variant(ucv, needs.hunger);
            u_rest   = survival_urgency_variant(ucv, needs.rest);
        }
        float u_social     = urgency(needs.social);       // flat power curve
        float u_expression = urgency(needs.expression);   // flat power curve
        float u_purpose    = urgency(needs.purpose);      // flat power curve

        // B4: Meaning crisis erodes compliance.
        // Being productive but unfulfilled makes agents less willing to serve.
        // NOTE: A/B testing a smooth sigmoid replacement (Phase 2.3) caused
        // production collapse in seed=2 (44->10 alive): the gradual compliance
        // reduction starting from meaning=0 destabilizes the "compliance stays
        // high until meaning genuinely crises" regime. The kink at 0.7 is not
        // a patch — it is functionally load-bearing. Kept as-is.
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
        bool has_food     = inv.food > 0.01f
                         || inv.raw_food >= config_.eat_food_per_tick;
        bool storage_near = has_adjacent_storage_with_food(e);
        bool can_eat      = has_food || storage_near;

        // Only scrap is gatherable in this model.
        bool scrap_available = visible_scrap;

        // Are there unbuilt machines?
        bool unbuilt_exists = near_unbuilt_machine.first >= 0;

        // Are there built machines?
        bool built_exists = built_machines > 0;

        // === ACTION UTILITIES ===

        // Production test mode: zero all cultural drives
        // Personal food buffer (processed only, since FoodSource is gone).
        float food_security = std::min(1.0f, inv.food / 2.0f);

        // ================================================================
        // GATHER: base drive from incomplete infrastructure + purpose/hunger.
        // Scales with infra_gap (fewer unbuilt → less urgency to gather).
        // ADD: food urgency boost when no food production exists.
        // ONI "forage panic": when food supply is critically low and agent
        // has no inputs, GATHER gets the same survival urgency as EAT.
        // ================================================================
        float u_gather = 0.0f;
        if (scrap_available) {
            // Low material indicator: agent has nothing to build with
            float low_mat = std::max(0.0f, 1.0f - inv.raw_material / 2.0f);

            // Factory health crisis amplifies
            float gather_urgency = factory_pressure(policy_health, 2.0f);

            // Base drive: compliance × infrastructure gap × material scarcity
            // infra_gap from grid-level signal — no redundant per-agent loop
            // raw_gap: when colony has plenty banked, GATHER collapses (like infra_gap
            // for BUILD). Never fully zero — survival retains a floor.
            float gather_base = effective_compliance * infra_gap * low_mat * 1.5f;
            gather_base *= (0.2f + 0.8f * raw_gap);

            // Food urgency boost: when food is low and no food machines exist,
            // gathering is the only path to food (gather→build FOOD machine→work it)
            if (built_food_machines == 0 || food_supply_ratio < 0.5f) {
                gather_base += u_hunger * 0.5f;
            }

            float purpose_drive = effective_compliance * u_purpose * 0.4f;
            float hunger_drive = u_hunger * 0.2f;  // hungry agents know: no material → no food

            // Raw food gathering: ONI "supply chain" pattern.
            // TWO drives:
            //   1. Personal hunger → gather to eat (emergency)
            //   2. Supply-chain deficit → gather to FEED THE MACHINES (proactive)
            // The second is critical: even well-fed agents must gather raw_food
            // because FoodMachines need inputs. Without this, the supply chain
            // starves: agents eat processed food, never gather raw food, machines
            // have nothing to process, food production stops.
            float raw_food_drive = 0.0f;
            {
                bool food_source_available = visible_food_source;
                if (food_source_available) {
                    // Drive 1: personal hunger → forage to eat
                    if (inv.raw_food < 0.5f) {
                        float food_gap = std::min(1.0f, 1.0f - food_supply_ratio);
                        raw_food_drive += u_hunger * 0.8f * (0.5f + food_gap);
                        // Critical forage boost: when personal food AND storage food
                        // are both low, gathering is the ONLY path to survival
                        if (!can_eat && food_supply_ratio < 0.3f) {
                            raw_food_drive += critical_spike(needs.hunger) * 0.8f;
                        }
                    }
                    // Drive 2: supply-chain deficit → forage to feed machines.
                    // When raw_food stores are LOW (machines will starve),
                    // agents gather raw_food even if personally well-fed.
                    // This is the ONI/RimWorld "haul ingredients to workshop" pattern.
                    if (total_stor_raw_food < 3.0f && inv.raw_food < 1.0f) {
                        float supply_deficit = std::max(0.0f, 1.0f - total_stor_raw_food / 3.0f);
                        // Scale with compliance and purpose: dutiful agents maintain supply chain
                        raw_food_drive += effective_compliance * u_purpose * supply_deficit * 1.5f;
                        // Stronger pull when food machines are built (they need inputs!)
                        if (built_food_machines > 0) {
                            raw_food_drive += supply_deficit * (float)built_food_machines * 0.15f;
                        }
                    }
                }
            }

            // Scrap gathering drive: agents need raw_material to build machines
            // on resource tiles. This drive fires when there are unbuilt resource
            // tiles and the agent has no material.
            float raw_material_drive = 0.0f;
            {
                bool scrap_available_local = visible_scrap;
                bool free_foodsources = free_foodsource;
                bool free_scrappiles = free_scrap;
                if (scrap_available_local && inv.raw_material < 1.0f &&
                    (free_foodsources || free_scrappiles)) {
                    float mat_deficit = std::max(0.0f, 1.0f - inv.raw_material);
                    raw_material_drive = effective_compliance * u_purpose * mat_deficit * 2.0f;
                    // Stronger when there are many unbuilt resource tiles
                    raw_material_drive += infra_gap * mat_deficit * 1.5f;
                    // Colony-level gate: don't hoard raw when there's plenty banked
                    raw_material_drive *= (0.2f + 0.8f * raw_gap);
                }
            }

            u_gather = (gather_base + purpose_drive + hunger_drive + raw_food_drive + raw_material_drive)
                      * mood_factor * gather_urgency;
        }

        // Focus dampener for GATHER — only in CALM mode.
        {
            float hu = (needs.social + needs.expression + needs.purpose) / 3.0f;
            u_gather *= calm_comfortable_dampener(
                config_.director_mode == DirectorMode::CALM,
                hu, needs.hunger, needs.rest, food_supply_ratio, 0.3f);
        }

        // ================================================================
        // BUILD: infrastructure completion drive.
        // Scales with infra_gap (phase transition). Per-type priority
        // for FOOD machines when food_supply_ratio < 1.0.
        // ================================================================
        float u_build = 0.0f;
        // Factory health crisis amplifies BUILD urgency: infrastructure = survival.
        float build_urgency = factory_pressure(policy_health, 2.0f);

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
                auto near_m = near_unbuilt_machine;
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
                auto near_ez = near_unbuilt_ez;
                const auto& td = grid_.data_at(near_ez.first, near_ez.second);
                float finish_bonus = (td.build_cost > 0.0f)
                    ? (td.build_progress / td.build_cost) * 1.5f : 0.0f;
                u_build_ez = effective_compliance * u_purpose * 1.0f * mat_readiness + finish_bonus;
            }

            u_build = std::max(u_build_mach, u_build_ez);
        }

        // Conveyor build: connect machines to Storage/Exit.
        // Critical once machines are built — without conveyors, output doesn't reach Exit.
        // This should OVERWHELM machine building once we have enough machines.
        {
            float u_build_conv = 0.0f;
            // Check existing unbuilt conveyor frames AND new sites from Floor
            bool can_build_conveyor = unbuilt_conveyor;
            if (!can_build_conveyor && inv.raw_material > 0.05f) {
                auto conveyor_site = grid_.find_conveyor_build_site(pos.x, pos.y);
                can_build_conveyor = conveyor_site.x >= 0
                    && std::abs(conveyor_site.x - pos.x)
                       + std::abs(conveyor_site.y - pos.y) <= observation_radius;
            }
            if (can_build_conveyor && inv.raw_material > 0.05f) {
                float conv_mat = std::min(1.0f, inv.raw_material / 0.5f);

                // Count built machines and how many have conveyor connections
                int built_mach = 0, connected_mach = 0;
                for (int gy = 0; gy < grid_.height(); gy++)
                    for (int gx = 0; gx < grid_.width(); gx++) {
                        if (grid_.at(gx, gy) == TileType::Machine && grid_.data_at(gx, gy).built) {
                            built_mach++;
                            // Check if machine has adjacent built conveyor
                            for (int dy = -1; dy <= 1; dy++)
                                for (int dx = -1; dx <= 1; dx++) {
                                    int nx = gx+dx, ny = gy+dy;
                                    if (nx >= 0 && nx < grid_.width() && ny >= 0 && ny < grid_.height() &&
                                        grid_.at(nx, ny) == TileType::Conveyor && grid_.data_at(nx, ny).built) {
                                        connected_mach++;
                                        dy = 2; break; // break outer
                                    }
                                }
                        }
                    }

                int unconnected = built_mach - connected_mach;
                float connection_urgency = (built_mach > 0) ? (float)unconnected / (float)built_mach : 0.0f;

                float conv_base = effective_compliance * conv_mat * build_infra_gap * 1.5f;
                // Urgency scales with unconnected machines
                conv_base += connection_urgency * 1.5f;
                // Boost when many machines built but few connected
                if (built_mach >= 4 && unconnected >= 2) conv_base *= 1.8f;
                // CRITICAL: massive urgency when quota is failing — conveyors are the
                // only way to move output from distant machines to Exit-adjacent Storage.
                // Only fires when Output machines actually exist and have output to move.
                int n_out_machines = built_out;
                float total_output_in_system = local_output;
                if (config_.external_supply_variant == 0
                    && last_quota_fill_ < 0.5f && n_out_machines > 0
                    && total_output_in_system > 0.1f) {
                    float quota_urgency = (1.0f - last_quota_fill_) * 2.0f;
                    conv_base += quota_urgency;
                }

                float conv_sup  = effective_compliance * u_purpose * 1.0f;
                float conv_community = community_pressure * 1.2f * conv_mat;
                u_build_conv = conv_base + conv_sup + conv_community;
                u_build_conv *= mood_factor * build_urgency;
            }
            u_build = std::max(u_build, u_build_conv);
        }

        // Storage build: build storage adjacent to built machines that lack it.
        // High priority because machines can't output without nearby storage.
        {
            float u_build_storage = 0.0f;
            auto storage_site = grid_.find_storage_build_site(pos.x, pos.y);
            if (storage_site.first >= 0 && inv.raw_material > 0.05f
                && std::abs(storage_site.first - pos.x)
                   + std::abs(storage_site.second - pos.y) <= observation_radius) {
                float stor_mat = std::min(1.0f, inv.raw_material / 1.0f);
                float stor_base = effective_compliance * stor_mat * build_infra_gap * 2.5f;
                float stor_sup  = effective_compliance * u_purpose * 0.8f;
                u_build_storage = stor_base + stor_sup;
                u_build_storage *= mood_factor * build_urgency;
            }
            u_build = std::max(u_build, u_build_storage);
        }

        // FoodMachine on FoodSource: build a FoodMachine on top of a FoodSource.
        // Very high priority — this is the primary food production path.
        // Auto-gathers raw_food, no need for agents to carry inputs.
        {
            float u_build_food = 0.0f;
            if (free_foodsource && inv.raw_material > 0.05f) {
                float food_mat = std::min(1.0f, inv.raw_material / 2.0f);
                // Urgency scales inversely with food supply
                float food_urg = 1.0f + std::max(0.0f, 1.0f - food_supply_ratio) * 3.0f;
                float food_base = effective_compliance * food_mat * build_infra_gap * 3.0f * food_urg;
                float food_sup  = effective_compliance * u_purpose * 1.0f;
                u_build_food = food_base + food_sup;
                u_build_food *= mood_factor * build_urgency;
            }
            u_build = std::max(u_build, u_build_food);
        }

        // OutputMachine on Floor: build when agent has construction_material.
        // This is tier 2 — converts construction_material → output (quota).
        // High priority when quota is failing or no Output machines exist.
        {
            float u_build_output = 0.0f;
            int n_out = built_out;
            if (inv.construction_material > 0.05f && n_out < 2) {
                float out_mat = std::min(1.0f, inv.construction_material / 1.0f);
                float out_base = effective_compliance * out_mat * build_infra_gap * 2.5f;
                float out_sup  = effective_compliance * u_purpose * 1.2f;
                // Extra urgency when quota is failing or no Output machines
                if (n_out == 0) out_base += 5.0f;  // critical: no output capacity
                else if (config_.external_supply_variant == 0 && last_quota_fill_ < 0.5f) {
                    out_base += (1.0f - last_quota_fill_) * 5.0f;
                }
                u_build_output = out_base + out_sup;
                u_build_output *= mood_factor * build_urgency;
            }
            u_build = std::max(u_build, u_build_output);
        }

        // HARD GATE: BUILD collapses when colony has enough infrastructure.
        // This ALWAYS applies — it's logistics, not culture.
        u_build *= std::min(1.0f, infra_gap * 1.5f + 0.05f);

        // Focus dampener for BUILD — only when not under factory pressure.
        {
            float hu = (needs.social + needs.expression + needs.purpose) / 3.0f;
            u_build *= calm_comfortable_dampener(
                config_.director_mode == DirectorMode::CALM,
                hu, needs.hunger, needs.rest, food_supply_ratio, 0.3f);
        }
        bool build_plan_feasible = u_build > 0.0f;

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
        // Phase 3: WORK gate. Legacy hard-blocks BROKEN. Continuous variant
        // scales u_work by stress_work_mult (ramps 1->0 across stress 0.85-1.0).
        bool work_allowed = (config_.stress_model_variant == 1)
            ? (stress_work_mult(stress.value) > 0.01f)
            : (stress.state != StressState::BROKEN);
        if (work_allowed) {
        if (built_exists) {
            float health_urgency = factory_pressure(policy_health, 3.0f);

            // Input readiness: how prepared the agent is to WORK.
            // TWO components:
            //   1. Carrying resources (raw or refined) → high readiness
            //   2. BASE readiness: machines pull from adjacent storage,
            //      so even empty-handed agents can produce if storage has inputs.
            //      This prevents the "empty inventory = WORK=0 forever" death spiral.
            float raw_total = inv.raw_food + inv.raw_material + inv.construction_material;
            float input_readiness = 0.0f;
            if (inv.output > 0.05f) {
                // Hauling output: high urgency to reach Exit-adjacent Storage
                input_readiness = std::min(3.0f, 1.0f + inv.output);
            } else if (inv.construction_material > 0.05f) {
                // Refined product from MaterialsMachine: even small amounts are valuable
                input_readiness = std::min(3.0f, 1.0f + inv.construction_material);
            } else if (raw_total > 0.5f) {
                // Raw gathered resources: stock up before going to machine
                input_readiness = std::min(3.5f, raw_total);
                if (inv.raw_food > 0.1f) input_readiness += 0.5f;
            } else {
                // BASE READINESS: agent doesn't need inputs in inventory.
                // Machines pull from adjacent storage. This is the RimWorld/ONI pattern:
                // colonists walk to workstations that have ingredients stocked nearby.
                // Higher base when Output machines exist and quota is failing —
                // the factory needs workers even if they arrive empty-handed.
                input_readiness = 0.3f;
                int n_out = built_out;
                if (config_.external_supply_variant == 0
                    && n_out > 0 && last_quota_fill_ < 0.5f) {
                    input_readiness = 0.6f;  // quota failing: go work even without inputs
                }
            }

            // WORK pull: scales with built_ratio (more built = more to operate)
            float work_pull = effective_compliance * built_ratio * 2.0f;

            // FOCUS SYSTEM (Dwarf Fortress pattern):
            // Only in CALM mode do higher needs suppress WORK.
            // In NORMAL and PRODUCTION_TEST, agents work regardless.
            bool calm = (config_.director_mode == DirectorMode::CALM);
            if (calm) {
                float higher_unmet = (needs.social + needs.expression + needs.purpose) / 3.0f;
                work_pull *= calm_work_focus(true, higher_unmet);
                work_pull *= calm_comfortable_dampener(
                    true, higher_unmet, needs.hunger, needs.rest, food_supply_ratio, 0.2f);
            } else if (config_.external_supply_variant == 0
                       && config_.director_mode == DirectorMode::NORMAL
                       && last_quota_fill_ < 0.5f) {
                // Factory pressure boosts WORK
                work_pull *= 3.0f;
                input_readiness = std::max(input_readiness, 1.0f);
            }

            // Food urgency: when food supply is LOW, WORK becomes critical.
            // ONI pattern: "mealtime panic" — when food stores drop, ALL
            // duplicants prioritize food production above everything else.
            // The urgency curve is exponential, not linear:
            //   ratio > 1.5: no urgency (food is abundant)
            //   ratio = 1.0: mild urgency (100 ticks of buffer)
            //   ratio = 0.5: high urgency (50 ticks of buffer)
            //   ratio = 0.2: CRITICAL (20 ticks of buffer) -> 6x multiplier
            //   ratio = 0.0: EMERGENCY -> 10x multiplier
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
            // Phase 3: continuous WORK suppression near BROKEN (was: hard gate)
            if (config_.stress_model_variant == 1) {
                u_work *= stress_work_mult(stress.value);
            }
        }
        } // end work_allowed gate

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
            // Variant 0: the eat_weight boost was a patch to help EAT win at critical
            // hunger. Variants 1-3 rely on the steeper curve alone, so no boost.
            if (ucv == 0 && food_security > 0.3f) eat_weight = 1.8f;
            u_eat = u_hunger * eat_weight;
        }

        // REST: survival need like hunger. Near-max escalation.
        // Uses survival urgency so rest at 0.9+ overrides most actions.
        float rest_weight = 0.4f + 0.6f * personality.laziness;
        if (needs.rest > 0.7f) rest_weight *= 1.5f;
        if (needs.rest > 0.9f) rest_weight *= 2.0f;
        float u_rest_action = rest_weight * u_rest;  // u_rest already uses survival_urgency
        u_rest_action *= 1.0f + std::clamp(rest_place.score, -0.5f, 0.5f) * 0.10f;

        // SOCIALIZE: boosted by nearby trust (known agents are more attractive)
        // THRESHOLD GATE — lower threshold so social fires more readily.
        // Phase 3: continuous stress modifier replaces discrete FSM branches.
        float gregariousness_mult = (config_.stress_model_variant == 1)
            ? stress_gregariousness_mult(stress.value)
            : (stress.state == StressState::DISSOCIATED ? 0.7f
               : stress.state == StressState::BROKEN ? 0.3f : 1.0f);
        float effective_gregariousness = personality.gregariousness * gregariousness_mult
                                       * (1.0f - stress.trauma * config_.trauma_social_impact);
        float u_socialize = 0.0f;
        if (!no_culture && needs.social > 0.15f) {
            // Gate passed — social drive ramps up sharply
            float social_gate = (needs.social - 0.15f) / 0.85f;  // 0..1 above threshold
            u_socialize = effective_gregariousness * u_social * (0.8f + social_gate);
            u_socialize *= (0.5f + 0.5f * nearby_trust);
            u_socialize += soc.influence * 0.1f;
            u_socialize *= 1.0f + std::clamp(social_place.score, -0.5f, 0.5f) * 0.15f;
            // Maslow boost — stronger when fed and rested
            u_socialize *= maslow_boost(needs.hunger, needs.rest, 4.0f, 2.0f);
        }

        // CREATE: THRESHOLD GATE (The Sims pattern)
        // Below 0.25 expression = near-zero CREATE drive. Above = ramps hard.
        // This is the “creative impulse” — it doesn’t compete with survival,
        // but when it fires, it fires strong.
        float u_create = 0.0f;
        if (!no_culture && needs.expression > 0.25f) {
            float expr_gate = (needs.expression - 0.25f) / 0.75f;  // 0..1 above threshold
            u_create = personality.artistry * u_expression * (1.0f + expr_gate);
            if (needs.meaning > 0.4f)
                u_create += personality.artistry * needs.meaning * 0.8f;
            // Phase 3: continuous creativity boost (was: if DISSOCIATED *= 1.3f)
            u_create *= (config_.stress_model_variant == 1)
                ? stress_creativity_mult(stress.value)
                : (stress.state == StressState::DISSOCIATED ? 1.3f : 1.0f);
            // Maslow boost
            u_create *= maslow_boost(needs.hunger, needs.rest, 2.5f, 1.5f);
            u_create *= 1.0f + std::clamp(create_place.score, -0.5f, 0.5f) * 0.15f;
        }

        // EXPLORE: THRESHOLD GATE — curiosity fires above 0.25 purpose
        float u_explore = 0.0f;
        if (!no_culture && needs.purpose > 0.25f) {
            float explore_gate = (needs.purpose - 0.25f) / 0.75f;
            u_explore = personality.curiosity * u_purpose * (0.5f + explore_gate);
            if (needs.meaning > 0.4f)
                u_explore += personality.curiosity * needs.meaning * 0.5f;
            // Phase 3: continuous creativity boost (was: if DISSOCIATED *= 1.3f)
            u_explore *= (config_.stress_model_variant == 1)
                ? stress_creativity_mult(stress.value)
                : (stress.state == StressState::DISSOCIATED ? 1.3f : 1.0f);
            u_explore *= maslow_boost(needs.hunger, needs.rest, 2.0f, 1.0f);
        }

        // MAINTAIN: repair degraded conveyors. Compliance-driven, scales with degradation.
        float u_maintain = 0.0f;
        {
            auto conv = near_degraded_conveyor;
            if (conv.first >= 0) {
                const auto& cd = grid_.data_at(conv.first, conv.second);
                float degradation = 1.0f - cd.conveyor_condition;
                float priority_signal = 1.0f + 0.75f * cd.maintenance_priority;
                float hunger_gate = std::max(0.0f, 1.0f - u_hunger * 2.0f);
                u_maintain = (effective_compliance * degradation * u_purpose * 1.5f * hunger_gate
                           + community_pressure * degradation * 0.8f * hunger_gate)
                           * priority_signal;
                u_maintain *= mood_factor;
            }
        }

        // DISMANTLE: consider tearing down conveyors that are dead-ends or blocking paths.
        float u_dismantle = 0.0f;
        {
            bool blocking_nearby = false;
            bool dead_end_nearby = dismantle_candidate;
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
        if (stress.value >= config_.sabotage_stress_threshold) {
            float stress_drive;
            if (config_.stress_model_variant == 1) {
                // Phase 3: continuous sabotage drive. Scales with stress.value
                // above the threshold, amplified by trauma. Preserves the legacy
                // anchors: ~1.2 at EUPHORIC (stress~0.8), ~3.0 at BROKEN (stress~0.95).
                float over_threshold = (stress.value - config_.sabotage_stress_threshold)
                                     / std::max(0.001f, 1.0f - config_.sabotage_stress_threshold);
                stress_drive = over_threshold * over_threshold * 3.0f;  // quadratic, up to 3.0
            } else {
                stress_drive = 0.0f;
                if (stress.state == StressState::HOSTILE_EUPHORIA) stress_drive = 1.2f;
                if (stress.state == StressState::BROKEN) stress_drive = 3.0f;
                if (stress.state == StressState::DISSOCIATED && stress.trauma > 0.3f)
                    stress_drive = stress.trauma * 0.5f;
            }
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
            if (any_storage_food && room_in_inv > 0.1f) {
                float pocket_emptiness = room_in_inv / config_.inv_food_cap;
                u_get_food = pocket_emptiness * (0.3f + u_hunger * 0.8f);
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
        // Disabled when urgency_curve_variant != 0 (Phase 2.2: testing whether the
        // steeper survival curve + Bonabeau thresholds alone prevent convergence).
        if (ucv == 0) {
            if (worker_nearby >= 3) u_work *= 1.0f / (1.0f + (worker_nearby - 2) * 0.3f);
            if (gatherer_nearby >= 3) u_gather *= 1.0f / (1.0f + (gatherer_nearby - 2) * 0.3f);
            if (builder_nearby >= 3) u_build *= 1.0f / (1.0f + (builder_nearby - 2) * 0.3f);
        }

        // === RESPONSE THRESHOLDS (Bonabeau et al. 1996) ===
        // Each agent has per-action sensitivity derived from personality.
        // Low theta = responds eagerly (specialist). High theta = reluctant.
        // This makes agents naturally lean toward different roles.
        auto bonabeau = [](float stimulus, float theta) -> float {
            float s2 = stimulus * stimulus;
            float t2 = theta * theta;
            return s2 / (s2 + t2 + 0.001f);
        };
        float th_gather    = 0.4f - 0.3f * personality.compliance;
        float th_build     = 0.4f - 0.3f * personality.compliance;
        float th_work      = 0.4f - 0.3f * (1.0f - personality.laziness);
        float th_eat       = 0.05f;  // everyone eats eagerly (survival)
        float th_rest      = 0.4f - 0.3f * personality.laziness;
        float th_socialize = 0.4f - 0.3f * personality.gregariousness;
        float th_create    = 0.4f - 0.3f * personality.artistry;
        float th_explore   = 0.4f - 0.3f * personality.curiosity;

        u_gather     *= bonabeau(u_gather,     th_gather);
        u_build      *= bonabeau(u_build,      th_build);
        u_work       *= bonabeau(u_work,       th_work);
        u_eat        *= bonabeau(u_eat,        th_eat);
        u_rest_action *= bonabeau(u_rest_action, th_rest);
        u_socialize  *= bonabeau(u_socialize,  th_socialize);
        u_create     *= bonabeau(u_create,     th_create);
        u_explore    *= bonabeau(u_explore,    th_explore);

        u_gather *= 1.0f + skills.domestic * 0.02f;
        u_work *= 1.0f + skills.factory_work * 0.02f;
        u_socialize *= 1.0f + skills.social_skill * 0.02f;
        u_create *= 1.0f + skills.artistic * 0.02f;

        // HARD SURVIVAL OVERRIDE: when critical, zero out non-survival actions.
        // This prevents agents from building themselves to death.
        // Only active in variant 0 (legacy). Variants 1-3 rely on the steeper
        // urgency curve alone to make EAT dominate at hunger>0.85.
        if (ucv == 0) {
            if (needs.hunger > 0.85f) {
                u_build = 0.0f; u_work = 0.0f; u_socialize = 0.0f;
                u_create = 0.0f; u_explore = 0.0f; u_maintain = 0.0f;
                u_dismantle = 0.0f; u_sabotage = 0.0f; u_get_food = 0.0f;
            }
            if (needs.rest > 0.9f) {
                u_build *= 0.1f; u_work *= 0.1f; u_gather *= 0.1f;
            }
        }

        if (!config_.allow_build) u_build = 0.0f;

        bool feasible_gather = inv.total() < InventoryComponent::CAPACITY - 0.001f
            && (visible_food_source || visible_scrap);
        bool feasible_build = config_.allow_build && build_plan_feasible;
        bool feasible_work = find_feasible_work_target(e).first >= 0;
        bool feasible_eat = can_eat;
        bool feasible_rest = needs.rest > 0.001f;
        bool feasible_socialize = visible_agents > 1;
        bool feasible_create = needs.expression > 0.001f && create_place.x >= 0;
        bool feasible_explore = action_feasible(e, ActionType::EXPLORE);
        bool feasible_get_food = inv.food < config_.inv_food_cap - 0.001f
            && any_storage_food;
        bool feasible_maintain = near_degraded_conveyor.first >= 0;
        bool feasible_dismantle = dismantle_candidate;
        bool feasible_sabotage = built_machines > 0 || built_conveyor;

        if (!feasible_gather) u_gather = 0.0f;
        if (!feasible_build) u_build = 0.0f;
        if (!feasible_work) u_work = 0.0f;
        if (!feasible_eat) u_eat = 0.0f;
        if (!feasible_rest) u_rest_action = 0.0f;
        if (!feasible_socialize) u_socialize = 0.0f;
        if (!feasible_create) u_create = 0.0f;
        if (!feasible_explore) u_explore = 0.0f;
        if (!feasible_get_food) u_get_food = 0.0f;
        if (!feasible_maintain) u_maintain = 0.0f;
        if (!feasible_dismantle) u_dismantle = 0.0f;
        if (!feasible_sabotage) u_sabotage = 0.0f;
        constexpr float u_idle = 0.02f;

        // Pick best action
        // === BOLTZMANN ACTION SELECTION ===
        // Replace greedy argmax with probabilistic softmax selection.
        // Agents USUALLY pick the highest-utility action but have a
        // temperature-controlled chance of picking alternatives.
        // This prevents all 24 agents from choosing the same action.
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
            {ActionType::IDLE,      u_idle},
        };
        constexpr int N = sizeof(options) / sizeof(options[0]);

        float tau = config_.selection_temperature;
        if (tau <= 0.001f) {
            // Degenerate: greedy argmax (legacy behavior)
            action.current = ActionType::IDLE;
            float best_score = 0.0f;
            for (auto& opt : options)
                if (opt.score > best_score) {
                    best_score = opt.score;
                    action.current = opt.type;
                }
        } else {
            // Impossible or inactive actions have zero weight. A conventional
            // softmax would give utility-zero actions a nonzero chance.
            float max_u = 0.0f;
            for (int i = 0; i < N; i++)
                if (options[i].score > max_u) max_u = options[i].score;

            float weights[N];
            float sum_w = 0.0f;
            for (int i = 0; i < N; i++) {
                float u = options[i].score;
                weights[i] = u > 0.0f ? std::exp((u - max_u) / tau) : 0.0f;
                sum_w += weights[i];
            }

            // Preserve one selection draw per decision even when IDLE is the
            // only feasible result, keeping the RNG stream stable.
            std::uniform_real_distribution<float> pick(0.0f, sum_w > 0.0f ? sum_w : 1.0f);
            float r = pick(registry_.get<RandomComponent>(e).engine);
            action.current = ActionType::IDLE;
            if (sum_w > 0.0f) {
                float cumulative = 0.0f;
                for (int i = 0; i < N; i++) {
                    if (weights[i] <= 0.0f) continue;
                    cumulative += weights[i];
                    if (r <= cumulative) {
                        action.current = options[i].type;
                        break;
                    }
                }
            }
        }

        metrics_.action_selected[metric_index(action.current)]++;

        // Set stickiness for ALL actions (The Sims / DF pattern).
        // Agents commit to actions for a minimum duration — they don't
        // reconsider every tick. Duration modulated by personality:
        // an Artisan stays longer in CREATE, a Foreman in WORK.
        // Survival overrides (hunger > 0.8) still break through.
        if (action.sticky_action != action.current || action.sticky_ticks <= 0) {
            action.sticky_action = action.current;
            int dist = 30;
            if (action.target_x >= 0 && action.target_y >= 0) {
                dist = std::abs(pos.x - action.target_x) + std::abs(pos.y - action.target_y);
            }
            switch (action.current) {
                case ActionType::WORK:
                    action.sticky_ticks = dist + 15 + (int)(personality.compliance * 15);
                    break;
                case ActionType::SOCIALIZE:
                    action.sticky_ticks = std::max(15, dist) + (int)(personality.gregariousness * 20);
                    break;
                case ActionType::CREATE:
                    action.sticky_ticks = 20 + (int)(personality.artistry * 40);
                    break;
                case ActionType::GATHER:
                    action.sticky_ticks = 10 + (int)(personality.compliance * 10);
                    break;
                case ActionType::BUILD:
                    action.sticky_ticks = 10 + (int)(personality.compliance * 10);
                    break;
                case ActionType::REST:
                    action.sticky_ticks = 20 + (int)(personality.laziness * 20);
                    break;
                case ActionType::EXPLORE:
                    action.sticky_ticks = 15 + (int)(personality.curiosity * 25);
                    break;
                case ActionType::EAT:
                    action.sticky_ticks = 5;
                    break;
                default:
                    action.sticky_ticks = 8;
                    break;
            }
        }

        auto record_utility = [&](ActionType type, float score, bool factory_action,
                                  bool feasible) {
            size_t index = metric_index(type);
            auto& breakdown = action.last_utility[index];
            breakdown = {};
            if (factory_action) breakdown.factory = score;
            else breakdown.self = score;
            breakdown.final = score;
            breakdown.feasible = feasible;
            metrics_.utility_samples[index]++;
            if (breakdown.feasible) metrics_.feasible_samples[index]++;
            metrics_.utility_self_sum[index] += breakdown.self;
            metrics_.utility_factory_sum[index] += breakdown.factory;
            metrics_.utility_cost_sum[index] += breakdown.cost;
            metrics_.utility_risk_sum[index] += breakdown.risk;
            metrics_.utility_final_sum[index] += breakdown.final;
        };
        record_utility(ActionType::GATHER, u_gather, true, feasible_gather);
        record_utility(ActionType::BUILD, u_build, true, feasible_build);
        record_utility(ActionType::WORK, u_work, true, feasible_work);
        record_utility(ActionType::EAT, u_eat, false, feasible_eat);
        record_utility(ActionType::REST, u_rest_action, false, feasible_rest);
        record_utility(ActionType::SOCIALIZE, u_socialize, false, feasible_socialize);
        record_utility(ActionType::CREATE, u_create, false, feasible_create);
        record_utility(ActionType::EXPLORE, u_explore, false, feasible_explore);
        record_utility(ActionType::GET_FOOD, u_get_food, false, feasible_get_food);
        record_utility(ActionType::MAINTAIN, u_maintain, true, feasible_maintain);
        record_utility(ActionType::DISMANTLE, u_dismantle, true, feasible_dismantle);
        record_utility(ActionType::SABOTAGE, u_sabotage, false, feasible_sabotage);
        record_utility(ActionType::IDLE, u_idle, false, true);
    }
}
