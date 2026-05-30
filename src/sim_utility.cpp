#include "simulation.h"
#include <algorithm>
#include <cmath>

void Simulation::system_compute_utility() {
    // Community pressure: storage buffer × external supply-chain health.
    // Empty storage → 1, abundant → 0. Amplified when factory_health is low
    // (the external pressure documented in §13 / §14).
    float community_food   = total_storage_food();
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

        float alpha = config_.urgency_alpha;
        auto urgency = [alpha](float need) -> float {
            return std::pow(need, alpha);
        };

        float u_hunger     = urgency(needs.hunger);
        float u_rest       = urgency(needs.rest);
        float u_social     = urgency(needs.social);
        float u_expression = urgency(needs.expression);
        float u_purpose    = urgency(needs.purpose);

        // B4: Meaning crisis erodes compliance.
        // Being productive but unfulfilled makes agents less willing to serve.
        float effective_compliance = personality.compliance;
        if (needs.meaning > 0.7f) {
            effective_compliance *= (1.0f - (needs.meaning - 0.7f) * 1.5f);
            // At meaning=1.0, compliance is reduced by 45%
        }

        // === SOCIAL MODIFIERS ===
        // Mood affects productivity: low mood = less effective work
        // mood ∈ [0,1], 1=happy. Productive actions scaled by (0.5 + 0.5*mood)
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
        // raw_food field is unused (kept for ABI; legacy FoodSource gathering disabled).
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

        // GATHER: base drive from incomplete infrastructure + purpose/hunger.
        // Key insight: agents in a factory with unbuilt machines should WANT to gather
        // raw material even when purpose is low. The unbuilt-machine ratio provides a
        // constant "the job isn't done" signal that doesn't depend on personal needs.
        float u_gather = 0.0f;
        if (scrap_available) {
            // Infrastructure gap: how much of the factory is unbuilt?
            // Provides a constant 0-1 drive independent of personal needs.
            int total_machines = 0, built_machines = 0;
            for (int gy = 0; gy < grid_.height(); gy++)
                for (int gx = 0; gx < grid_.width(); gx++)
                    if (grid_.at(gx, gy) == TileType::Machine) {
                        total_machines++;
                        if (grid_.data_at(gx, gy).built) built_machines++;
                    }
            float infra_gap = (total_machines > 0)
                ? 1.0f - (float)built_machines / (float)total_machines : 0.0f;

            // Low material indicator: agent has nothing to build with
            float low_mat = std::max(0.0f, 1.0f - inv.raw_material / 2.0f);

            // Factory health crisis amplifies
            float gather_urgency = 1.0f + (1.0f - factory_health_) * 2.0f;

            // Base drive: compliance × infrastructure gap × material scarcity
            // Purpose adds extra but isn't required — the factory itself creates urgency.
            float base_drive = effective_compliance * infra_gap * low_mat * 1.5f;
            float purpose_drive = effective_compliance * u_purpose * 0.4f;
            float hunger_drive = u_hunger * 0.2f;  // hungry agents know: no material → no food

            u_gather = (base_drive + purpose_drive + hunger_drive)
                      * mood_factor * gather_urgency;
        }

        // BUILD: infrastructure completion drive.
        // Two components: (1) "I have material, let me build" and (2) "the factory needs this".
        // The infra_gap provides constant urgency even when purpose is low.
        float u_build = 0.0f;
        bool unbuilt_ez_exists = grid_.find_nearest_unbuilt_eatingzone(pos.x, pos.y).first >= 0;
        bool built_ez_exists   = grid_.find_nearest_built_eatingzone(pos.x, pos.y).first >= 0;
        bool unbuilt_conveyor  = grid_.find_nearest_conveyor_to_build(pos.x, pos.y).first >= 0;

        // Factory health crisis amplifies BUILD urgency: infrastructure = survival.
        float build_urgency = 1.0f + (1.0f - factory_health_) * 4.0f; // 1x at full, 5x at zero

        // Material availability boost: having material should STRONGLY push toward BUILD
        // vs other actions. This creates the gather→build→work cycle.
        float mat_readiness = std::min(1.0f, inv.raw_material / 2.0f);

        // Compute infra_gap for BUILD (same as GATHER — could cache but perf is fine for 24 agents)
        int total_mach = 0, built_mach = 0;
        for (int gy2 = 0; gy2 < grid_.height(); gy2++)
            for (int gx2 = 0; gx2 < grid_.width(); gx2++)
                if (grid_.at(gx2, gy2) == TileType::Machine) {
                    total_mach++;
                    if (grid_.data_at(gx2, gy2).built) built_mach++;
                }
        float build_infra_gap = (total_mach > 0)
            ? 1.0f - (float)built_mach / (float)total_mach : 0.0f;

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
                    // Mirror compliance × purpose weighting; this is collective infrastructure.
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
                // Conveyors are infrastructure too — gap drives them
                float conv_base = effective_compliance * conv_mat * build_infra_gap * 1.5f;
                float conv_sup  = effective_compliance * u_purpose * 1.0f;
                float conv_community = community_pressure * 1.2f * conv_mat;
                u_build_conv = conv_base + conv_sup + conv_community;
                u_build_conv *= mood_factor * build_urgency;
            }
            u_build = std::max(u_build, u_build_conv);
        }

        // WORK: gather raw_food from a FoodSource tile. Produces for the factory.
        // BROKEN agents refuse to work for the factory.
        float u_work = 0.0f;
        if (stress.state != StressState::BROKEN) {
        if (built_exists) {
            float health_urgency = 1.0f + (1.0f - factory_health_) * 3.0f; // 1x at full health, 4x at zero
            u_work = (effective_compliance * u_hunger * 0.8f
                + (1.0f - personality.laziness) * u_purpose * 0.3f
                + effective_compliance * community_pressure * 0.6f)
                * mood_factor * health_urgency;
        }
        } // end BROKEN gate

        // EAT
        float u_eat = 0.0f;
        if (can_eat) {
            float eat_weight = 1.3f;
            if (food_security > 0.3f) eat_weight = 1.8f;
            u_eat = u_hunger * eat_weight;
        }

        // REST: doc formula + a near-max escalation. The doc's intent is "prevents
        // agents from working themselves to death"; the literal 1.5x is too weak
        // against EAT's 1.8x at the extreme.
        float rest_weight = 0.4f + 0.6f * personality.laziness;
        if (needs.rest > 0.7f) rest_weight *= 1.5f;
        if (needs.rest > 0.9f) rest_weight *= 2.0f;
        float u_rest_action = rest_weight * u_rest;

        // SOCIALIZE: boosted by nearby trust (known agents are more attractive)
        // Influential agents draw others to socialize
        // S2: Stress state modifiers on social utility
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
                // Only maintain when degradation is significant AND agent isn't hungry
                float hunger_gate = std::max(0.0f, 1.0f - u_hunger * 2.0f);
                u_maintain = effective_compliance * degradation * u_purpose * 1.5f * hunger_gate
                           + community_pressure * degradation * 0.8f * hunger_gate;
                u_maintain *= mood_factor;
            }
        }

        // DISMANTLE: consider tearing down conveyors that are dead-ends or blocking paths.
        // Only high-compliance agents who see a clear improvement will do this.
        // Hunger-gated: don't dismantle when starving. Low stress needed (calm judgment).
        float u_dismantle = 0.0f;
        {
            // Check if there's a blocking conveyor nearby (strong reason)
            bool blocking_nearby = false;
            bool dead_end_nearby = grid_.find_nearest_dead_end_conveyor(pos.x, pos.y).first >= 0;
            for (int sy = std::max(0, pos.y - 8); sy < std::min(grid_.height(), pos.y + 8); sy++)
                for (int sx = std::max(0, pos.x - 8); sx < std::min(grid_.width(), pos.x + 8); sx++)
                    if (grid_.is_conveyor_blocking_path(sx, sy)) blocking_nearby = true;

            if (blocking_nearby || dead_end_nearby) {
                float reason_strength = blocking_nearby ? 1.5f : 0.6f;
                float hunger_gate = std::max(0.0f, 1.0f - u_hunger * 3.0f);  // strong hunger gate
                float calm_gate = std::max(0.0f, soc.mood * 2.0f - 0.5f); // need calm (high mood) to dismantle
                // High compliance agents more willing to improve the factory layout
                // Low laziness helps (they're willing to do the extra work)
                u_dismantle = effective_compliance * reason_strength
                            * hunger_gate * calm_gate * (1.0f - personality.laziness * 0.5f)
                            * mood_factor;
                // If agent already has raw_material, less incentive to dismantle for refund
                if (inv.raw_material > 2.0f) u_dismantle *= 0.3f;
            }
        }

        // S3: SABOTAGE — irrational destruction driven by chronic stress.
        // Not DISMANTLE (rational). This is a broken agent lashing out.
        // BROKEN agents have massive SABOTAGE utility. HOSTILE_EUPHORIA too.
        // Redeemed agents NEVER sabotage.
        float u_sabotage = 0.0f;
        if (stress.value >= config_.sabotage_stress_threshold
            && stress.state != StressState::REDEEMED) {
            // Base drive scales with how broken the agent is
            float stress_drive = 0.0f;
            if (stress.state == StressState::HOSTILE_EUPHORIA) stress_drive = 1.2f;
            if (stress.state == StressState::BROKEN) stress_drive = 3.0f;
            // Even DISSOCIATED agents can lash out if trauma is high
            if (stress.state == StressState::DISSOCIATED && stress.trauma > 0.3f)
                stress_drive = stress.trauma * 0.5f;
            // Trauma amplifies: more damaged = more desperate
            stress_drive *= (1.0f + stress.trauma);
            // Low compliance agents sabotage more easily
            stress_drive *= (1.0f - personality.compliance * 0.3f);
            // Hunger gates it slightly — starving agents are too weak
            float hunger_gate = std::max(0.0f, 1.0f - u_hunger * 1.5f);
            u_sabotage = stress_drive * hunger_gate;
        }

        // GET_FOOD: agent considers fetching a "vianda" from Storage when their inv.food
        // is low and they're not at the bottom of immediate hunger urgency. Higher when an
        // EatingZone exists and the agent isn't currently next to Storage.
        float u_get_food = 0.0f;
        {
            float room_in_inv = std::max(0.0f, config_.inv_food_cap - inv.food);
            bool any_storage_food = grid_.find_nearest_storage_with_food(pos.x, pos.y).first >= 0;
            if (any_storage_food && room_in_inv > 0.1f) {
                // Drive: anticipating hunger plus "I should bring food to the eating zone".
                // Scales with how empty the pocket is and a modest hunger anticipation.
                float pocket_emptiness = room_in_inv / config_.inv_food_cap;
                u_get_food = pocket_emptiness * (0.3f + u_hunger * 0.8f);
                // If a built EatingZone exists, bringing food back there is more valuable.
                if (built_ez_exists) u_get_food *= 1.3f;
            }
        }

        // SOCIAL LEARNING: agents observe what trusted neighbors are doing
        // and get a bonus for copying productive actions. This creates
        // emergent coordination: a trusted agent gathering → others gather nearby.
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

                // Weight: trust * influence * proximity decay
                float trust_w = std::max(0.0f, rel.trust);
                float prox = 1.0f / (1.0f + (float)d);
                float weight = trust_w * (0.3f + 0.7f * osoc.influence) * prox;

                if (weight < 0.01f) continue;

                // Boost the matching action
                switch (oact.current) {
                    case ActionType::GATHER:   u_gather  += weight * 0.4f; break;
                    case ActionType::BUILD:    u_build   += weight * 0.5f; break;
                    case ActionType::WORK:     u_work    += weight * 0.3f; break;
                    case ActionType::MAINTAIN: u_maintain += weight * 0.3f; break;
                    case ActionType::GET_FOOD: u_get_food += weight * 0.2f; break;
                    default: break;
                }
            }
        }

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
