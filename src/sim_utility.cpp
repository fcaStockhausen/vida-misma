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
                               const AgentComponent>();
    for (auto e : view) {
        if (!registry_.get<AgentComponent>(e).alive) continue;

        auto& needs      = registry_.get<NeedsComponent>(e);
        auto& personality = registry_.get<PersonalityComponent>(e);
        auto& inv        = registry_.get<InventoryComponent>(e);
        auto& action     = registry_.get<ActionComponent>(e);
        auto& soc        = registry_.get<SocialComponent>(e);
        auto  pos        = registry_.get<PositionComponent>(e);
        auto  ag         = registry_.get<AgentComponent>(e);

        float alpha = config_.urgency_alpha;
        auto urgency = [alpha](float need) -> float {
            return std::pow(need, alpha);
        };

        float u_hunger     = urgency(needs.hunger);
        float u_rest       = urgency(needs.rest);
        float u_social     = urgency(needs.social);
        float u_expression = urgency(needs.expression);
        float u_purpose    = urgency(needs.purpose);

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

        // GATHER: only raw_material remains (no wild food sources in this model).
        // Mood modulates productivity — unhappy agents are less motivated.
        float u_gather = 0.0f;
        if (scrap_available) {
            float low_mat_indicator = (inv.raw_material < 1.0f) ? 1.0f : 0.0f;
            u_gather = personality.compliance * u_purpose * 0.5f * (1.0f + low_mat_indicator)
                      * mood_factor;
        }

        // BUILD: doc formula + community pressure + "finish what you started" + EatingZone need.
        // Three sub-targets:
        //   1. Unbuilt Machine frame — existing infrastructure to complete.
        //   2. Unbuilt EatingZone frame — partial eating place that someone started.
        //   3. A new EatingZone — if NO built EatingZone exists yet and the agent has materials,
        //      they're motivated to initiate one on a Floor tile ≥ min_dist from any Machine.
        float u_build = 0.0f;
        bool unbuilt_ez_exists = grid_.find_nearest_unbuilt_eatingzone(pos.x, pos.y).first >= 0;
        bool built_ez_exists   = grid_.find_nearest_built_eatingzone(pos.x, pos.y).first >= 0;

        if (inv.raw_material > 0.1f) {
            float mat_readiness = std::min(1.0f, inv.raw_material / 2.0f);

            // Sub 1: machine
            float u_build_mach = 0.0f;
            if (unbuilt_exists) {
                auto near_m = grid_.find_nearest_unbuilt_machine(pos.x, pos.y);
                float finish_bonus = 0.0f;
                if (near_m.first >= 0) {
                    const auto& td = grid_.data_at(near_m.first, near_m.second);
                    if (td.build_cost > 0.0f) {
                        finish_bonus = (td.build_progress / td.build_cost) * 1.5f;
                    }
                }
                u_build_mach = (personality.compliance * u_purpose * 1.2f
                              + personality.compliance * community_pressure * 0.8f) * mat_readiness
                             + finish_bonus;
                u_build_mach *= mood_factor;
            }

            // Sub 2: continue an unbuilt EatingZone frame (high finish_bonus pull)
            float u_build_ez = 0.0f;
            if (unbuilt_ez_exists) {
                auto near_ez = grid_.find_nearest_unbuilt_eatingzone(pos.x, pos.y);
                const auto& td = grid_.data_at(near_ez.first, near_ez.second);
                float finish_bonus = (td.build_cost > 0.0f)
                    ? (td.build_progress / td.build_cost) * 1.5f : 0.0f;
                u_build_ez = personality.compliance * u_purpose * 1.0f * mat_readiness + finish_bonus;
            }

            // Sub 3: initiate a new EatingZone — only when none exist (built OR being built)
            //        and a valid site is reachable.
            float u_build_new_ez = 0.0f;
            if (!built_ez_exists && !unbuilt_ez_exists) {
                auto site = grid_.find_nearest_valid_eatingzone_site(
                    pos.x, pos.y, config_.eatingzone_min_dist_machine);
                if (site.first >= 0) {
                    // Mirror compliance × purpose weighting; this is collective infrastructure.
                    u_build_new_ez = personality.compliance * u_purpose * 1.0f * mat_readiness;
                }
            }

            u_build = std::max({u_build_mach, u_build_ez, u_build_new_ez});
        }

        // WORK: mood modulates productivity. Influenced agents may follow herd.
        float u_work = 0.0f;
        if (built_exists) {
            u_work = (personality.compliance * u_hunger * 0.8f
                + (1.0f - personality.laziness) * u_purpose * 0.3f
                + personality.compliance * community_pressure * 0.6f)
                * mood_factor;
            // Herding: if nearby high-influence agents are working, boost WORK
            if (nearby_count > 0) {
                float herd_work = 0.0f;
                auto alive_view2 = registry_.view<PositionComponent, const AgentComponent,
                                                   SocialComponent, ActionComponent>();
                for (auto other : alive_view2) {
                    if (other == e) continue;
                    if (!registry_.get<AgentComponent>(other).alive) continue;
                    auto& opos = registry_.get<PositionComponent>(other);
                    int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
                    if (d <= 3) {
                        auto& osoc = registry_.get<SocialComponent>(other);
                        auto& oact = registry_.get<ActionComponent>(other);
                        if (oact.current == ActionType::WORK) {
                            herd_work += osoc.influence;
                        }
                    }
                }
                u_work += herd_work * 0.3f * personality.compliance;
            }
        }

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
        float u_socialize = personality.gregariousness * u_social;
        u_socialize *= (0.5f + 0.5f * nearby_trust);  // high trust = more rewarding
        u_socialize += soc.influence * 0.1f;           // influential agents socialize more

        // CREATE: only viable if there's an OpenSpace tile to reach
        bool open_space_available = grid_.find_nearest(TileType::OpenSpace,
            pos.x, pos.y).first >= 0;
        float u_create = 0.0f;
        if (open_space_available) {
            u_create = personality.artistry * u_expression;
        }

        // EXPLORE
        float u_explore = personality.curiosity * u_purpose * 0.3f;

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
            std::uniform_int_distribution<int> pick(0, 8);
            ActionType random_actions[] = {
                ActionType::GATHER, ActionType::BUILD, ActionType::WORK,
                ActionType::EAT, ActionType::REST, ActionType::SOCIALIZE,
                ActionType::CREATE, ActionType::EXPLORE, ActionType::GET_FOOD
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
