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
    system_ship_out_food();        // external pressure: shipping food through Exit tiles
    system_factory_deterioration();// machine breaks when health is low
    system_update_stress();
    system_check_deaths();
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
    auto exits = grid_.find_all(TileType::Exit);

    for (auto [ex, ey] : exits) {
        if (to_ship <= 0.001f) break;
        for (int dy = -1; dy <= 1 && to_ship > 0.001f; dy++)
            for (int dx = -1; dx <= 1 && to_ship > 0.001f; dx++) {
                if (dx == 0 && dy == 0) continue;
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
                emit_log(agent.id, "DIED of starvation after " +
                         std::to_string(config_.starvation_ticks) + " ticks without food");
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
            }
        } else {
            agent.ticks_at_max_rest = 0;
        }

        // Breakdown
        if (stress.value >= config_.breakdown_threshold) {
            agent.alive = false;
            agent.cause_of_death = "breakdown";
            emit_log(agent.id, "had a BREAKDOWN (stress=" + ff2(stress.value) + ")");
        }
    }
}
