#include "simulation.h"
#include <algorithm>

// ============================================================
// Construction & lifecycle
// ============================================================

Simulation::Simulation(const Config& cfg)
    : config_(cfg)
    , grid_(cfg.grid_width, cfg.grid_height)
    , rng_(cfg.seed)
    , tick_(0)
    , factory_health_(1.0f)
    , total_food_produced_(0.0f)
    , total_raw_gathered_(0.0f)
    , total_machines_built_(0)
    , social_(cfg.initial_population)
{
    grid_.generate_default();
    spawn_initial_agents();
}

void Simulation::advance() {
    system_regen_resources();
    system_decay_needs();
    system_compute_utility();
    system_find_targets();
    system_move_to_targets();
    system_execute_actions();
    system_conveyor_transport();   // move resources along conveyor chains
    system_ship_out_food();        // external pressure: shipping food through Exit tiles
    system_factory_deterioration();// machine breaks when health is low
    system_update_stress();
    system_check_deaths();

    // Social systems (every tick)
    auto alive = alive_agents();
    social_.apply_contagion(registry_, alive);
    social_.update_influence(registry_, alive);
    social_.update_mood(registry_, alive);
    social_.decay_relationships(tick_);

    // Social penalty: agents who dismantled conveyors that weren't rebuilt
    // within the window lose trust with everyone who notices.
    system_check_dismantle_penalties();

    tick_++;
}

// ============================================================
// Public queries
// ============================================================

int Simulation::alive_count() const {
    int count = 0;
    auto view = registry_.view<const AgentComponent>();
    for (auto e : view) {
        if (registry_.get<AgentComponent>(e).alive) count++;
    }
    return count;
}

int Simulation::built_machine_count() const {
    int count = 0;
    auto machines = grid_.find_all(TileType::Machine);
    for (auto [x,y] : machines) {
        if (grid_.data_at(x, y).built) count++;
    }
    return count;
}

float Simulation::total_storage_food() const {
    float total = 0.0f;
    auto storages = grid_.find_all(TileType::Storage);
    for (auto [x,y] : storages) {
        total += grid_.data_at(x, y).stored_food;
        total += grid_.data_at(x, y).stored_raw_food;
    }
    return total;
}

std::vector<entt::entity> Simulation::alive_agents() const {
    std::vector<entt::entity> result;
    auto view = registry_.view<const AgentComponent>();
    for (auto e : view) {
        if (registry_.get<AgentComponent>(e).alive) {
            result.push_back(e);
        }
    }
    return result;
}

// ============================================================
// SPAWNING
// ============================================================

void Simulation::spawn_initial_agents() {
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

        // Personality from archetype with per-agent jitter.
        // Distribution: ~4 Foremen, ~4 Networkers, ~3 Workers, ~3 Artisans,
        //               ~4 Explorers, ~4 Survivors, then cycle.
        static const Archetype distribution[] = {
            Archetype::FOREMAN,  Archetype::FOREMAN,
            Archetype::NETWORKER, Archetype::NETWORKER,
            Archetype::STEADY_WORKER,
            Archetype::ARTISAN, Archetype::ARTISAN,
            Archetype::EXPLORER, Archetype::EXPLORER,
            Archetype::SURVIVOR, Archetype::SURVIVOR,
            Archetype::FOREMAN,
            Archetype::NETWORKER,
            Archetype::STEADY_WORKER,
            Archetype::ARTISAN,
            Archetype::EXPLORER,
            Archetype::SURVIVOR,
            Archetype::STEADY_WORKER,
            Archetype::EXPLORER,
            Archetype::FOREMAN,
            Archetype::NETWORKER,
            Archetype::SURVIVOR,
            Archetype::ARTISAN,
            Archetype::STEADY_WORKER,
        };
        Archetype at = (i < 24) ? distribution[i]
                      : static_cast<Archetype>(i % (int)Archetype::COUNT);
        auto base = archetype_traits(at);

        auto jitter = [&](float center, float j) -> float {
            std::uniform_real_distribution<float> d(center - j, center + j);
            return std::clamp(d(rng_), 0.05f, 0.95f);
        };

        PersonalityComponent personality;
        personality.compliance     = jitter(base.compliance, base.jitter);
        personality.laziness       = jitter(base.laziness, base.jitter);
        personality.artistry       = jitter(base.artistry, base.jitter);
        personality.gregariousness = jitter(base.gregariousness, base.jitter);
        personality.resilience     = jitter(base.resilience, base.jitter);
        personality.curiosity      = jitter(base.curiosity, base.jitter);
        personality.archetype      = at;
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
        registry_.emplace<SocialComponent>(entity);
        InventoryComponent inv;
        inv.food = config_.initial_food_per_agent;  // bootstrap until the factory runs
        registry_.emplace<InventoryComponent>(entity, inv);
    }
}

// ============================================================
// SYSTEM: Ship Out Food (external quota / production line)
// ============================================================
// Each tick, drain up to quota_per_tick of food from any Storage that is 8-adjacent
// to an Exit tile. Track how much we shipped. If the quota wasn't met,
// factory_health drops; meeting it pushes health back up.

void Simulation::system_ship_out_food() {
    float quota = config_.quota_per_tick;
    float to_ship = quota;

    // Drain food from any Storage near an Exit (radius 3 first, then all storages).
    // The "external demand" represents the supply chain — in practice, any storage
    // that has food is part of the drainable pool.
    auto exits = grid_.find_all(TileType::Exit);
    for (int radius = 1; radius <= 3 && to_ship > 0.001f; radius++) {
        for (auto [ex, ey] : exits) {
            if (to_ship <= 0.001f) break;
            for (int dy = -radius; dy <= radius && to_ship > 0.001f; dy++)
                for (int dx = -radius; dx <= radius && to_ship > 0.001f; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                    int nx = ex + dx, ny = ey + dy;
                    if (grid_.at(nx, ny) != TileType::Storage) continue;
                    auto& d = grid_.data_at(nx, ny);
                    float take = std::min(to_ship, d.stored_food);
                    if (take > 0.0f) {
                        d.stored_food -= take;
                        to_ship -= take;
                    }
                }
        }
    }

    // Fallback: if Exit-adjacent storages are empty, drain from ANY storage on the map.
    // Represents the external supply chain reaching deeper into the factory.
    if (to_ship > 0.001f) {
        auto storages = grid_.find_all(TileType::Storage);
        for (auto [sx, sy] : storages) {
            if (to_ship <= 0.001f) break;
            auto& d = grid_.data_at(sx, sy);
            float take = std::min(to_ship, d.stored_food);
            if (take > 0.0f) {
                d.stored_food -= take;
                to_ship -= take;
            }
        }
    }

    float shipped = quota - to_ship;
    total_food_shipped_ += shipped;
    last_quota_fill_ = (quota > 0.0f) ? (shipped / quota) : 1.0f;

    if (shipped + 0.001f < quota) {
        factory_health_ = std::max(0.0f, factory_health_ - config_.health_decay_per_miss);
    } else {
        factory_health_ = std::min(1.0f, factory_health_ + config_.health_recovery_per_hit);
    }
}

// ============================================================
// SYSTEM: Factory Deterioration
// ============================================================
// When factory_health falls below threshold, built machines have a small per-tick
// probability of breaking (reverting to unbuilt, build_progress=0). The factory
// pressure forces the community to keep producing — or watch their infrastructure crumble.

void Simulation::system_factory_deterioration() {
    if (factory_health_ >= config_.machine_break_threshold) return;
    // Sharpening: the lower the health, the more aggressive breaks become.
    float severity = (config_.machine_break_threshold - factory_health_)
                   / std::max(0.0001f, config_.machine_break_threshold);
    float prob = config_.machine_break_prob * (1.0f + 2.0f * severity);

    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    auto machines = grid_.find_all(TileType::Machine);
    for (auto [x, y] : machines) {
        auto& d = grid_.data_at(x, y);
        if (!d.built) continue;
        if (roll(rng_) < prob) {
            d.built = false;
            d.build_progress = 0.0f;
            total_machines_broken_++;
            emit_log(-1, "MACHINE at (" + std::to_string(x) + "," + std::to_string(y) +
                     ") broke down (supply chain contraction)");
        }
    }
}

// ============================================================
// SYSTEM: Resource Regeneration
// ============================================================

void Simulation::system_regen_resources() {
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

// ============================================================
// SYSTEM: Need Decay
// ============================================================

void Simulation::system_decay_needs() {
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

// ============================================================
// SYSTEM: Stress
// ============================================================

void Simulation::system_update_stress() {
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

// ============================================================
// SYSTEM: Death Check
// ============================================================

void Simulation::system_check_deaths() {
    std::vector<int> newly_dead;
    auto view = registry_.view<NeedsComponent, AgentComponent, StressComponent, PersonalityComponent>();
    for (auto e : view) {
        if (!registry_.get<AgentComponent>(e).alive) continue;

        auto& needs  = registry_.get<NeedsComponent>(e);
        auto& agent  = registry_.get<AgentComponent>(e);
        auto& stress = registry_.get<StressComponent>(e);
        auto& personality = registry_.get<PersonalityComponent>(e);

        // Starvation
        if (needs.hunger >= 1.0f) {
            agent.ticks_at_max_hunger++;
            if (agent.ticks_at_max_hunger >= config_.starvation_ticks) {
                agent.alive = false;
                agent.cause_of_death = "starvation";
                emit_log(agent.id, "DIED of starvation after " +
                         std::to_string(config_.starvation_ticks) + " ticks without food");
                newly_dead.push_back(agent.id);
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
                emit_log(agent.id, "DIED of exhaustion after " +
                         std::to_string(config_.exhaustion_ticks) + " ticks without rest");
                newly_dead.push_back(agent.id);
            }
        } else {
            agent.ticks_at_max_rest = 0;
        }

        // Breakdown
        if (stress.value >= config_.breakdown_threshold) {
            agent.alive = false;
            agent.cause_of_death = "breakdown";
            emit_log(agent.id, "had a BREAKDOWN (stress=" + ff2(stress.value) + ")");
            newly_dead.push_back(agent.id);
        }

        // Factory collapse: when factory_health == 0, the crumbling factory
        // kills agents progressively. Lower resilience = faster death.
        // This creates a ticking clock: fix the factory or everyone dies.
        if (factory_health_ <= 0.0f) {
            float collapse_prob = 0.01f * (1.0f - personality.resilience * 0.8f);
            std::uniform_real_distribution<float> roll(0.0f, 1.0f);
            if (roll(rng_) < collapse_prob) {
                agent.alive = false;
                agent.cause_of_death = "collapse";
                emit_log(agent.id, "DIED in factory collapse");
                newly_dead.push_back(agent.id);
            }
        }
    }

    // Grief cascades: survivors who knew the deceased receive stress
    if (!newly_dead.empty()) {
        auto alive_now = alive_agents();
        for (int dead_id : newly_dead) {
            social_.apply_grief(registry_, dead_id, alive_now);
        }
    }
}

// ============================================================
// Social penalty for dismantle-without-rebuild
// ============================================================

void Simulation::system_check_dismantle_penalties() {
    // Scan all tiles for dismantled conveyors past the rebuild window.
    // If a tile was a conveyor, was dismantled, and hasn't been rebuilt
    // (still Floor), the dismantler loses trust with nearby agents.
    auto alive = alive_agents();
    for (int y = 0; y < grid_.height(); y++)
        for (int x = 0; x < grid_.width(); x++) {
            // We check tiles that are now Floor but have dismantle tracking data
            if (grid_.at(x, y) != TileType::Floor) continue;
            const auto& d = grid_.data_at(x, y);
            if (d.dismantled_by < 0) continue;  // never dismantled here
            if (d.dismantled_at_tick < 0) continue;

            int ticks_since = tick_ - d.dismantled_at_tick;
            if (ticks_since < config_.dismantle_rebuild_window) continue;
            if (ticks_since > config_.dismantle_rebuild_window + 50) {
                // Penalty already applied, stop checking this tile.
                continue;
            }

            // Find the dismantler among alive agents
            int dismantler_id = d.dismantled_by;
            bool dismantler_alive = false;
            for (auto e : alive) {
                if (registry_.get<AgentComponent>(e).id == dismantler_id) {
                    dismantler_alive = true;
                    break;
                }
            }
            if (!dismantler_alive) continue;

            // Apply trust penalty: all agents who can "see" the torn-down belt
            // (within manhattan distance 6) lose trust in the dismantler.
            for (auto e : alive) {
                auto& ag = registry_.get<AgentComponent>(e);
                if (ag.id == dismantler_id) continue;
                auto& opos = registry_.get<PositionComponent>(e);
                int dist = std::abs(opos.x - x) + std::abs(opos.y - y);
                if (dist > 6) continue;

                // Proximity-based severity: closer agents care more
                float severity = 0.05f * (1.0f - dist / 7.0f);
                social_.negative_interaction(ag.id, dismantler_id, tick_, severity);

                // Witness gets a small stress bump (disorder is stressful)
                auto& st = registry_.get<StressComponent>(e);
                st.value = std::clamp(st.value + 0.003f, 0.0f, 1.0f);
            }
        }
}
