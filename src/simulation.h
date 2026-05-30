#pragma once

#include "components.h"
#include "config.h"
#include "grid.h"
#include <entt/entt.hpp>
#include <random>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>

class Simulation {
public:
    Simulation(const Config& cfg)
        : config_(cfg)
        , grid_(cfg.grid_width, cfg.grid_height)
        , rng_(cfg.seed)
        , tick_(0)
        , factory_health_(1.0f)
        , total_food_produced_(0.0f)
        , total_raw_gathered_(0.0f)
        , total_machines_built_(0)
    {
        grid_.generate_default();
        spawn_initial_agents();
    }

    int tick() const { return tick_; }
    float factory_health() const { return factory_health_; }
    const Grid& grid() const { return grid_; }
    entt::registry& registry() { return registry_; }
    const Config& config() const { return config_; }
    Grid& grid_mut() { return grid_; }

    // Production stats
    float total_food_produced() const { return total_food_produced_; }
    float total_raw_gathered() const { return total_raw_gathered_; }
    int   total_machines_built() const { return total_machines_built_; }

    int alive_count() const {
        int count = 0;
        auto view = registry_.view<const AgentComponent>();
        for (auto e : view) {
            if (registry_.get<AgentComponent>(e).alive) count++;
        }
        return count;
    }

    // Count built machines
    int built_machine_count() const {
        int count = 0;
        auto machines = grid_.find_all(TileType::Machine);
        for (auto [x,y] : machines) {
            if (grid_.data_at(x, y).built) count++;
        }
        return count;
    }

    // Count total food in all storages
    float total_storage_food() const {
        float total = 0.0f;
        auto storages = grid_.find_all(TileType::Storage);
        for (auto [x,y] : storages) {
            total += grid_.data_at(x, y).stored_food;
            total += grid_.data_at(x, y).stored_raw_food;
        }
        return total;
    }

    void advance() {
        system_regen_resources();
        system_decay_needs();
        system_compute_utility();
        system_find_targets();
        system_move_to_targets();
        system_execute_actions();
        system_update_stress();
        system_check_deaths();
        tick_++;
    }

    std::vector<entt::entity> alive_agents() const {
        std::vector<entt::entity> result;
        auto view = registry_.view<const AgentComponent>();
        for (auto e : view) {
            if (registry_.get<AgentComponent>(e).alive) {
                result.push_back(e);
            }
        }
        return result;
    }

private:
    entt::registry registry_;
    Config config_;
    Grid grid_;
    std::mt19937 rng_;
    int tick_;
    float factory_health_;

    // Production stats
    float total_food_produced_;
    float total_raw_gathered_;
    int   total_machines_built_;

    // ==========================================
    // SPAWNING
    // ==========================================

    void spawn_initial_agents() {
        // Spawn agents across the entire map on walkable tiles
        std::vector<std::pair<int,int>> spawn_tiles;
        for (int y = 2; y < config_.grid_height - 2; y++)
            for (int x = 2; x < config_.grid_width - 2; x++) {
                TileType t = grid_.at(x, y);
                if (t == TileType::Floor || t == TileType::OpenSpace) {
                    spawn_tiles.push_back({x, y});
                }
            }

        std::uniform_int_distribution<int> pick_tile(0, (int)spawn_tiles.size() - 1);

        for (int i = 0; i < config_.initial_population; i++) {
            auto entity = registry_.create();

            auto [sx, sy] = spawn_tiles[pick_tile(rng_)];
            registry_.emplace<PositionComponent>(entity, sx, sy);
            registry_.emplace<AgentComponent>(entity, i, true);

            // Random personality
            auto rand_rang = [&](const float (&r)[2]) -> float {
                std::uniform_real_distribution<float> d(r[0], r[1]);
                return d(rng_);
            };

            PersonalityComponent personality;
            personality.compliance     = rand_rang(config_.compliance_range);
            personality.laziness       = rand_rang(config_.laziness_range);
            personality.artistry       = rand_rang(config_.artistry_range);
            personality.gregariousness = rand_rang(config_.gregariousness_range);
            personality.resilience     = rand_rang(config_.resilience_range);
            personality.curiosity      = rand_rang(config_.curiosity_range);
            registry_.emplace<PersonalityComponent>(entity, personality);

            // Needs start near zero
            NeedsComponent needs;
            std::uniform_real_distribution<float> nd(0.0f, 0.15f);
            needs.hunger    = nd(rng_);
            needs.rest      = nd(rng_);
            needs.social    = nd(rng_);
            needs.expression = nd(rng_);
            needs.purpose   = nd(rng_);
            registry_.emplace<NeedsComponent>(entity, needs);

            registry_.emplace<ActionComponent>(entity, ActionType::IDLE);
            registry_.emplace<StressComponent>(entity, 0.0f);
            registry_.emplace<InventoryComponent>(entity);
        }
    }

    // ==========================================
    // SYSTEM: Resource Regeneration
    // ==========================================

    void system_regen_resources() {
        for (int y = 0; y < grid_.height(); y++)
            for (int x = 0; x < grid_.width(); x++) {
                TileType t = grid_.at(x, y);
                if (t == TileType::FoodSource || t == TileType::ScrapPile) {
                    auto& d = grid_.data_at(x, y);
                    if (d.resource_regen > 0.0f && d.resource_amount < d.resource_max) {
                        d.resource_amount = std::min(d.resource_max,
                            d.resource_amount + d.resource_regen);
                    }
                }
            }
    }

    // ==========================================
    // SYSTEM: Need Decay
    // ==========================================

    void system_decay_needs() {
        auto view = registry_.view<NeedsComponent, const AgentComponent>();
        for (auto e : view) {
            auto& needs = registry_.get<NeedsComponent>(e);
            if (!registry_.get<AgentComponent>(e).alive) continue;

            needs.hunger     = std::min(1.0f, needs.hunger    + config_.hunger_decay);
            needs.rest       = std::min(1.0f, needs.rest      + config_.rest_decay);
            needs.social     = std::min(1.0f, needs.social    + config_.social_decay);
            needs.expression = std::min(1.0f, needs.expression + config_.expression_decay);
            needs.purpose    = std::min(1.0f, needs.purpose   + config_.purpose_decay);
        }
    }

    // ==========================================
    // SYSTEM: Utility Computation
    // ==========================================

    void system_compute_utility() {
        auto view = registry_.view<NeedsComponent, PersonalityComponent,
                                   InventoryComponent, ActionComponent,
                                   const AgentComponent>();
        for (auto e : view) {
            if (!registry_.get<AgentComponent>(e).alive) continue;

            auto& needs      = registry_.get<NeedsComponent>(e);
            auto& personality = registry_.get<PersonalityComponent>(e);
            auto& inv        = registry_.get<InventoryComponent>(e);
            auto& action     = registry_.get<ActionComponent>(e);

            float alpha = config_.urgency_alpha;
            auto urgency = [alpha](float need) -> float {
                return std::pow(need, alpha);
            };

            float u_hunger    = urgency(needs.hunger);
            float u_rest      = urgency(needs.rest);
            float u_social    = urgency(needs.social);
            float u_expression = urgency(needs.expression);
            float u_purpose   = urgency(needs.purpose);

            // How much food is accessible to this agent?
            bool has_food     = inv.food > 0.01f;
            bool has_raw_food = inv.raw_food > 0.01f;
            bool storage_near = has_adjacent_storage_with_food(e);
            bool can_eat      = has_food || has_raw_food || storage_near;

            // Are there resources to gather?
            bool food_sources_available  = grid_.find_nearest(TileType::FoodSource,
                registry_.get<PositionComponent>(e).x,
                registry_.get<PositionComponent>(e).y).first >= 0;
            bool scrap_available = grid_.find_nearest(TileType::ScrapPile,
                registry_.get<PositionComponent>(e).x,
                registry_.get<PositionComponent>(e).y).first >= 0;

            // Are there unbuilt machines?
            auto pos = registry_.get<PositionComponent>(e);
            bool unbuilt_exists = grid_.find_nearest_unbuilt_machine(pos.x, pos.y).first >= 0;

            // Are there built machines?
            bool built_exists = grid_.find_nearest_built_machine(pos.x, pos.y).first >= 0;

            // === ACTION UTILITIES ===

            // How much food agent has (0-1 normalized)
            float food_security = std::min(1.0f, (inv.food + inv.raw_food * 0.5f) / 2.0f);

            // GATHER: high when hungry and food is low, OR when materials needed
            float gather_food_score = 0.0f;
            if (food_sources_available) {
                // Urgency scales with hunger, drops as food security rises
                gather_food_score = u_hunger * (1.0f - food_security) * 1.5f;
            }
            float gather_material_score = 0.0f;
            if (scrap_available && unbuilt_exists) {
                gather_material_score = personality.compliance * u_purpose * 0.5f
                    * (1.0f + (inv.raw_material < 1.0f ? 0.5f : 0.0f));
            }
            float u_gather = std::max(gather_food_score, gather_material_score);

            // BUILD: high when have materials and unbuilt machines exist
            float u_build = 0.0f;
            if (unbuilt_exists && inv.raw_material > 0.5f) {
                u_build = personality.compliance * u_purpose * 1.2f
                    * std::min(1.0f, inv.raw_material / 2.0f);
            }

            // WORK: high when machines exist and community needs food
            float u_work = 0.0f;
            if (built_exists) {
                u_work = personality.compliance * u_hunger * 0.8f
                    + (1.0f - personality.laziness) * u_purpose * 0.3f;
            }

            // EAT: high when hungry AND food is available
            // Eating is urgent -- without food you die
            float u_eat = 0.0f;
            if (can_eat) {
                float eat_weight = 1.3f;
                // When carrying lots of food, eating becomes more urgent
                if (food_security > 0.3f) eat_weight = 1.8f;
                u_eat = u_hunger * eat_weight;
            }

            // REST: driven by fatigue, amplified by laziness
            float rest_weight = 0.4f + 0.6f * personality.laziness;
            // Stronger pull when exhaustion is critical
            if (needs.rest > 0.7f) rest_weight *= 1.5f;
            float u_rest_action = rest_weight * u_rest;

            // SOCIALIZE
            float u_socialize = personality.gregariousness * u_social;

            // CREATE
            float u_create = personality.artistry * u_expression;

            // EXPLORE
            float u_explore = personality.curiosity * u_purpose * 0.3f;

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
                std::uniform_int_distribution<int> pick(0, 7);
                ActionType random_actions[] = {
                    ActionType::GATHER, ActionType::BUILD, ActionType::WORK,
                    ActionType::EAT, ActionType::REST, ActionType::SOCIALIZE,
                    ActionType::CREATE, ActionType::EXPLORE
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
        }
    }

    // ==========================================
    // SYSTEM: Find Targets
    // ==========================================

    void system_find_targets() {
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
                    // Pick food source or scrap pile based on need
                    bool need_food = inv.raw_food < 2.0f && inv.food < 1.0f;
                    bool need_material = grid_.find_nearest_unbuilt_machine(
                        pos.x, pos.y).first >= 0;

                    auto food_target = grid_.find_nearest(TileType::FoodSource, pos.x, pos.y);
                    auto scrap_target = grid_.find_nearest(TileType::ScrapPile, pos.x, pos.y);

                    // Prefer food if hungry, material if not
                    bool pick_food = true;
                    if (food_target.first < 0) pick_food = false;
                    else if (scrap_target.first >= 0 && !need_food && need_material
                             && inv.raw_material < 2.0f) {
                        pick_food = false;
                    }

                    if (pick_food && food_target.first >= 0) {
                        tx = food_target.first;
                        ty = food_target.second;
                    } else if (scrap_target.first >= 0) {
                        tx = scrap_target.first;
                        ty = scrap_target.second;
                    }
                    break;
                }

                case ActionType::BUILD: {
                    auto target = grid_.find_nearest_unbuilt_machine(pos.x, pos.y);
                    tx = target.first;
                    ty = target.second;
                    break;
                }

                case ActionType::WORK: {
                    auto target = grid_.find_nearest_built_machine(pos.x, pos.y);
                    tx = target.first;
                    ty = target.second;
                    break;
                }

                case ActionType::EAT: {
                    // No movement needed - eat from inventory or adjacent storage
                    tx = pos.x;
                    ty = pos.y;
                    break;
                }

                case ActionType::REST: {
                    tx = pos.x;
                    ty = pos.y;
                    break;
                }

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
                    // Random target
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

    // ==========================================
    // SYSTEM: Move to Targets
    // ==========================================

    void system_move_to_targets() {
        auto view = registry_.view<ActionComponent, PositionComponent,
                                   const AgentComponent>();
        for (auto e : view) {
            if (!registry_.get<AgentComponent>(e).alive) continue;

            auto& action = registry_.get<ActionComponent>(e);
            auto& pos    = registry_.get<PositionComponent>(e);

            if (action.at_target) continue;
            if (action.target_x < 0 || action.target_y < 0) continue;

            // Check if already there
            if (pos.x == action.target_x && pos.y == action.target_y) {
                action.at_target = true;
                continue;
            }

            // Noise: sometimes random step
            std::uniform_real_distribution<float> noise_roll(0.0f, 1.0f);
            if (noise_roll(rng_) < config_.movement_noise) {
                random_move(pos);
                continue;
            }

            // Greedy step toward target
            move_toward(pos, action.target_x, action.target_y);

            // Check arrival
            if (pos.x == action.target_x && pos.y == action.target_y) {
                action.at_target = true;
            }
        }
    }

    // ==========================================
    // SYSTEM: Execute Actions
    // ==========================================

    void system_execute_actions() {
        auto view = registry_.view<ActionComponent, NeedsComponent,
                                   InventoryComponent, PositionComponent,
                                   const AgentComponent>();
        for (auto e : view) {
            if (!registry_.get<AgentComponent>(e).alive) continue;

            auto& action = registry_.get<ActionComponent>(e);
            auto& needs  = registry_.get<NeedsComponent>(e);
            auto& inv    = registry_.get<InventoryComponent>(e);
            auto& pos    = registry_.get<PositionComponent>(e);

            // Only execute if at target (or action doesn't need movement)
            if (!action.at_target) continue;

            switch (action.current) {

                case ActionType::GATHER: {
                    TileType tile = grid_.at(pos.x, pos.y);
                    if (tile == TileType::FoodSource || tile == TileType::ScrapPile) {
                        auto& td = grid_.data_at(pos.x, pos.y);
                        float available = td.resource_amount;
                        if (available > 0.01f) {
                            float amount = std::min(config_.gather_rate, available);
                            if (tile == TileType::FoodSource) {
                                if (inv.can_carry(amount)) {
                                    inv.raw_food += amount;
                                    td.resource_amount -= amount;
                                    total_raw_gathered_ += amount;
                                }
                            } else { // ScrapPile
                                if (inv.can_carry(amount)) {
                                    inv.raw_material += amount;
                                    td.resource_amount -= amount;
                                    total_raw_gathered_ += amount;
                                }
                            }
                        }
                    }
                    break;
                }

                case ActionType::BUILD: {
                    TileType tile = grid_.at(pos.x, pos.y);
                    if (tile == TileType::Machine) {
                        auto& td = grid_.data_at(pos.x, pos.y);
                        if (!td.built && td.build_progress < td.build_cost) {
                            float needed = td.build_cost - td.build_progress;
                            float use = std::min({config_.build_rate, needed, inv.raw_material});
                            if (use > 0.0f) {
                                inv.raw_material -= use;
                                td.build_progress += use;
                                if (td.build_progress >= td.build_cost) {
                                    td.built = true;
                                    total_machines_built_++;
                                }
                            }
                        }
                    }
                    break;
                }

                case ActionType::WORK: {
                    TileType tile = grid_.at(pos.x, pos.y);
                    if (tile == TileType::Machine) {
                        auto& td = grid_.data_at(pos.x, pos.y);
                        if (td.built) {
                            // Get raw_food from inventory or adjacent storage
                            float raw_needed = config_.machine_input;
                            float raw_available = inv.raw_food;

                            // Also check adjacent storage for raw_food
                            float adj_raw = get_adjacent_raw_food(pos.x, pos.y);

                            if (raw_available + adj_raw >= raw_needed) {
                                // Consume from inventory first, then storage
                                float to_consume = raw_needed;
                                if (inv.raw_food >= to_consume) {
                                    inv.raw_food -= to_consume;
                                    to_consume = 0.0f;
                                } else {
                                    to_consume -= inv.raw_food;
                                    inv.raw_food = 0.0f;
                                    consume_adjacent_raw_food(pos.x, pos.y, to_consume);
                                }

                                // Produce food into inventory or adjacent storage
                                float produced = config_.machine_output;
                                total_food_produced_ += produced;

                                if (inv.can_carry(produced)) {
                                    inv.food += produced;
                                } else {
                                    // Deposit in adjacent storage
                                    deposit_to_adjacent_storage(pos.x, pos.y,
                                        ResourceType::FOOD, produced);
                                }

                                // Work satisfies purpose slightly and tires
                                needs.purpose = std::max(0.0f,
                                    needs.purpose - config_.work_purpose_gain);
                                needs.hunger = std::min(1.0f, needs.hunger + 0.001f);
                                needs.rest   = std::min(1.0f, needs.rest   + 0.001f);
                            }
                        }
                    }
                    break;
                }

                case ActionType::EAT: {
                    // Try processed food first (better efficiency)
                    if (inv.food >= config_.eat_food_per_tick) {
                        inv.food -= config_.eat_food_per_tick;
                        needs.hunger = std::max(0.0f,
                            needs.hunger - config_.eat_satisfaction);
                    }
                    // Try raw food (worse efficiency)
                    else if (inv.raw_food >= config_.eat_food_per_tick) {
                        inv.raw_food -= config_.eat_food_per_tick;
                        needs.hunger = std::max(0.0f,
                            needs.hunger - config_.eat_satisfaction * config_.eat_raw_efficiency);
                    }
                    // Try adjacent storage
                    else {
                        float taken = take_from_adjacent_storage(pos.x, pos.y);
                        if (taken > 0.0f) {
                            needs.hunger = std::max(0.0f,
                                needs.hunger - config_.eat_satisfaction * taken);
                        }
                    }
                    break;
                }

                case ActionType::REST: {
                    needs.rest = std::max(0.0f, needs.rest - config_.rest_recovery);
                    break;
                }

                case ActionType::SOCIALIZE: {
                    // Check if another agent is adjacent
                    bool has_neighbor = false;
                    auto agents = alive_agents();
                    for (auto other : agents) {
                        if (other == e) continue;
                        auto& opos = registry_.get<PositionComponent>(other);
                        int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
                        if (d <= 2) { has_neighbor = true; break; }
                    }
                    if (has_neighbor) {
                        needs.social = std::max(0.0f,
                            needs.social - config_.social_satisfaction);
                    } else {
                        // Still slight benefit from trying
                        needs.social = std::max(0.0f, needs.social - 0.002f);
                    }
                    break;
                }

                case ActionType::CREATE: {
                    needs.expression = std::max(0.0f,
                        needs.expression - config_.create_satisfaction);
                    break;
                }

                case ActionType::EXPLORE: {
                    needs.purpose = std::max(0.0f,
                        needs.purpose - config_.explore_satisfaction);
                    random_move(pos);
                    break;
                }

                case ActionType::IDLE:
                    break;
            }
        }
    }

    // ==========================================
    // SYSTEM: Stress
    // ==========================================

    void system_update_stress() {
        auto view = registry_.view<StressComponent, NeedsComponent,
                                   PersonalityComponent, const AgentComponent>();
        for (auto e : view) {
            if (!registry_.get<AgentComponent>(e).alive) continue;

            auto& stress = registry_.get<StressComponent>(e);
            auto& needs  = registry_.get<NeedsComponent>(e);
            auto& personality = registry_.get<PersonalityComponent>(e);

            float stress_input = 0.0f;
            if (needs.hunger > 0.7f)     stress_input += config_.stress_high_need * (needs.hunger - 0.7f);
            if (needs.rest > 0.7f)       stress_input += config_.stress_high_need * (needs.rest - 0.7f);
            if (needs.social > 0.7f)     stress_input += config_.stress_high_need * (needs.social - 0.7f) * 0.5f;
            if (needs.expression > 0.7f) stress_input += config_.stress_high_need * (needs.expression - 0.7f) * personality.artistry;
            if (needs.purpose > 0.7f)    stress_input += config_.stress_high_need * (needs.purpose - 0.7f) * 0.5f;

            stress_input *= (1.0f - personality.resilience * 0.7f);

            stress.value = std::min(1.0f, stress.value + stress_input);
            stress.value = std::max(0.0f, stress.value - config_.stress_decay);
        }
    }

    // ==========================================
    // SYSTEM: Death Check
    // ==========================================

    void system_check_deaths() {
        auto view = registry_.view<NeedsComponent, AgentComponent, StressComponent>();
        for (auto e : view) {
            if (!registry_.get<AgentComponent>(e).alive) continue;

            auto& needs  = registry_.get<NeedsComponent>(e);
            auto& agent  = registry_.get<AgentComponent>(e);
            auto& stress = registry_.get<StressComponent>(e);

            // Starvation
            if (needs.hunger >= 1.0f) {
                agent.ticks_at_max_hunger++;
                if (agent.ticks_at_max_hunger >= config_.starvation_ticks) {
                    agent.alive = false;
                    agent.cause_of_death = "starvation";
                }
            } else {
                agent.ticks_at_max_hunger = 0;
            }

            // Exhaustion
            if (needs.rest >= 1.0f) {
                agent.ticks_at_max_rest++;
                if (agent.ticks_at_max_rest >= config_.exhaustion_ticks) {
                    agent.alive = false;
                    agent.cause_of_death = "exhaustion";
                }
            } else {
                agent.ticks_at_max_rest = 0;
            }

            // Breakdown
            if (stress.value >= config_.breakdown_threshold) {
                agent.alive = false;
                agent.cause_of_death = "breakdown";
            }
        }
    }

    // ==========================================
    // HELPERS
    // ==========================================

    void move_toward(PositionComponent& pos, int tx, int ty) {
        int dx = tx - pos.x;
        int dy = ty - pos.y;
        if (dx == 0 && dy == 0) return;

        int current_dist = std::abs(dx) + std::abs(dy);

        // Try all 4 cardinal directions, pick the one that reduces Manhattan distance most
        struct Dir { int x; int y; };
        Dir dirs[] = {{1,0}, {-1,0}, {0,1}, {0,-1}};

        int best_dist = current_dist;
        int best_nx = pos.x, best_ny = pos.y;
        bool found = false;

        for (auto& d : dirs) {
            int nx = pos.x + d.x;
            int ny = pos.y + d.y;
            if (!grid_.is_walkable(nx, ny)) continue;
            int nd = std::abs(tx - nx) + std::abs(ty - ny);
            if (nd < best_dist) {
                best_dist = nd;
                best_nx = nx;
                best_ny = ny;
                found = true;
            }
        }

        if (found) {
            pos.x = best_nx;
            pos.y = best_ny;
        } else {
            // No progress possible -- try random move
            random_move(pos);
        }
    }

    void random_move(PositionComponent& pos) {
        std::uniform_int_distribution<int> dir(-1, 1);
        int nx = pos.x + dir(rng_);
        int ny = pos.y + dir(rng_);
        nx = std::clamp(nx, 0, grid_.width() - 1);
        ny = std::clamp(ny, 0, grid_.height() - 1);
        if (grid_.is_walkable(nx, ny)) {
            pos.x = nx;
            pos.y = ny;
        }
    }

    bool has_adjacent_storage_with_food(entt::entity e) const {
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

    float get_adjacent_raw_food(int px, int py) const {
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

    void consume_adjacent_raw_food(int px, int py, float amount) {
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

    void deposit_to_adjacent_storage(int px, int py, ResourceType type, float amount) {
        float remaining = amount;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                if (remaining <= 0.001f) return;
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
    }

    // Returns effective satisfaction multiplier based on what was eaten
    float take_from_adjacent_storage(int px, int py) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = px + dx, ny = py + dy;
                if (grid_.at(nx, ny) == TileType::Storage) {
                    auto& d = grid_.data_at(nx, ny);
                    // Prefer processed food
                    if (d.stored_food >= config_.eat_food_per_tick) {
                        d.stored_food -= config_.eat_food_per_tick;
                        return 1.0f;  // full efficiency
                    }
                    // Fall back to raw food
                    if (d.stored_raw_food >= config_.eat_food_per_tick) {
                        d.stored_raw_food -= config_.eat_food_per_tick;
                        return config_.eat_raw_efficiency;
                    }
                }
            }
        return 0.0f;  // nothing to eat
    }
};
