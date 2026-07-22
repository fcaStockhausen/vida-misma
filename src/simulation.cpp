#include "simulation.h"
#include "textgen.h"
#include "production.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

// ============================================================
// Construction & lifecycle
// ============================================================

Simulation::Simulation(const Config& cfg)
    : config_(cfg)
    , grid_(cfg.grid_width, cfg.grid_height)
    , social_(cfg.max_population, cfg.social_learning_enabled)
    , rng_(cfg.seed)
    , metric_death_recorded_(std::max(cfg.max_population, cfg.initial_population), 0)
    , tick_(0)
    , factory_health_(1.0f)
    , total_food_produced_(0.0f)
    , total_output_produced_(0.0f)
    , total_raw_gathered_(0.0f)
    , total_machines_built_(0)
    , current_quota_per_tick_(cfg.quota_per_tick)
{
    if (cfg.max_population <= 0 || cfg.initial_population < 0
        || cfg.initial_population > cfg.max_population) {
        throw std::invalid_argument(
            "population config requires 0 <= initial_population <= max_population");
    }
    grid_.generate_wfc(cfg.seed);
    for (int y = 0; y < grid_.height(); y++)
        for (int x = 0; x < grid_.width(); x++) {
            const auto& data = grid_.data_at(x, y);
            if (grid_.at(x, y) == TileType::Machine && data.built) {
                metrics_.initial_machines_active[metric_index(data.machine_type)]++;
            } else if (grid_.at(x, y) == TileType::Conveyor && data.built) {
                metrics_.initial_conveyors_active++;
            } else if (grid_.at(x, y) == TileType::Storage && data.built) {
                metrics_.initial_storages_active++;
            }
        }
    metrics_.initial_exit_connected_outputs =
        static_cast<uint64_t>(grid_.exit_connected_output_machine_count());
    metrics_.initial_minimum_chain_present = grid_.minimum_chain_present();
    metrics_.agent_action_ticks.resize(cfg.max_population);
    metrics_.agent_productive_effect_ticks.resize(cfg.max_population);
    metrics_.agent_food_shared_given.resize(cfg.max_population);
    metrics_.agent_food_received.resize(cfg.max_population);
    metrics_.agent_food_consumed.resize(cfg.max_population);
    spawn_initial_agents();
    float initial_planner_fill = config_.external_supply_variant == 0 ? 0.0f : 1.0f;
    colony_prod_ = ProductionChain::assess(
        grid_, alive_count(), initial_planner_fill, 0.0f);
}

void Simulation::advance() {
    system_regen_resources();
    system_decay_needs();

    bool calm = (config_.director_mode == DirectorMode::CALM);

    // A1: Quota escalation — the factory demands more over time.
    // Disabled in CALM mode: no external pressure.
    if (calm) {
        current_quota_per_tick_ = 0.0f;
        factory_health_ = 1.0f;  // no decay in calm mode
    } else if (!quota_manually_set_) {
        float quota_cap = config_.quota_per_tick * 3.0f;
        current_quota_per_tick_ = std::min(quota_cap,
            current_quota_per_tick_ + config_.quota_growth_rate);
    }

    system_compute_utility();
    system_find_targets();
    system_move_to_targets();
    system_execute_actions();
    system_social_learning();
    system_spatial_learning();
    system_conveyor_transport();
    system_ship_out_food();

    // Assess production chain state (post-tick for next tick's decisions)
    // Sum c_mat carried by agents — the planner needs to see it to route workers
    // to Output machines instead of perpetually sending them to Materials.
    float agent_c_mat = 0.0f;
    {
        auto v = registry_.view<InventoryComponent, const AgentComponent>();
        for (auto e : v) {
            if (!registry_.get<AgentComponent>(e).alive) continue;
            agent_c_mat += registry_.get<InventoryComponent>(e).construction_material;
        }
    }
    float planner_quota_fill = config_.external_supply_variant == 0
        ? last_quota_fill_ : 1.0f;
    colony_prod_ = ProductionChain::assess(
        grid_, alive_count(), planner_quota_fill, agent_c_mat);

    // Pressure systems — disabled in CALM mode.
    // The factory doesn't deteriorate, restructure, or seal spaces.
    if (!calm) {
        if (config_.external_supply_variant == 0) system_factory_deterioration();
        if (config_.external_policy_variant == 0) {
            system_factory_restructure_legacy();
            system_hidden_space_exposure();
        } else {
            // Keep the common rejected-gate RNG cadence for useful policy A/B
            // comparisons; canonical target selection itself uses no behavioral RNG.
            if (tick_ % config_.restructure_interval == 0) {
                std::uniform_real_distribution<float> compatibility_roll(0.0f, 1.0f);
                (void)compatibility_roll(rng_);
            }
            system_factory_restructure_indifferent();
            system_space_overcapacity();
        }
    }
    if (!calm && config_.external_supply_variant == 1) system_update_factory_condition();

    system_artifact_effects();
    system_community_detection();
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
    system_record_emergence_metrics();
    system_chronicle_narrative();

    record_metric_deaths();
    system_lifecycle();
    metrics_.ticks_advanced++;
    tick_++;
}

bool Simulation::kill_agent(entt::entity entity, EventType death_type,
                            const std::string& text) {
    const char* cause = nullptr;
    DeathCause cause_code = DeathCause::NONE;
    switch (death_type) {
        case EventType::DIED_STARVATION:
            cause = "starvation"; cause_code = DeathCause::STARVATION; break;
        case EventType::DIED_EXHAUSTION:
            cause = "exhaustion"; cause_code = DeathCause::EXHAUSTION; break;
        case EventType::DIED_BREAKDOWN:
            cause = "breakdown"; cause_code = DeathCause::BREAKDOWN; break;
        case EventType::DIED_COLLAPSE:
            cause = "collapse"; cause_code = DeathCause::COLLAPSE; break;
        case EventType::DIED_SUICIDE:
            cause = "suicide"; cause_code = DeathCause::SUICIDE; break;
        case EventType::DIED_NATURAL:
            cause = "natural"; cause_code = DeathCause::NATURAL; break;
        default: return false;
    }

    auto& agent = registry_.get<AgentComponent>(entity);
    if (!agent.alive) return false;

    agent.alive = false;
    agent.death_cause = cause_code;
    agent.cause_of_death = cause;
    if (registry_.all_of<LifecycleComponent>(entity))
        registry_.get<LifecycleComponent>(entity).death_tick = tick_;
    for (int y = 0; y < grid_.height(); y++)
        for (int x = 0; x < grid_.width(); x++)
            if (grid_.data_at(x, y).claimed_by == agent.id)
                grid_.data_at(x, y).claimed_by = -1;
    if (death_type == EventType::DIED_SUICIDE) suicides_total_++;
    emit_log(agent.id, text, death_type);
    pending_grief_deaths_.push_back(agent.id);
    return true;
}

void Simulation::apply_pending_grief() {
    if (pending_grief_deaths_.empty()) return;
    auto survivors = alive_agents();
    for (int dead_id : pending_grief_deaths_) {
        social_.apply_grief(registry_, dead_id, survivors);
    }
    pending_grief_deaths_.clear();
}

void Simulation::record_metric_deaths() {
    auto view = registry_.view<const AgentComponent>();
    for (auto e : view) {
        const auto& agent = registry_.get<AgentComponent>(e);
        if (agent.alive || agent.id < 0) continue;
        size_t id = static_cast<size_t>(agent.id);
        if (id >= metric_death_recorded_.size()) {
            metric_death_recorded_.resize(id + 1, 0);
        }
        if (metric_death_recorded_[id]) continue;

        MetricDeathCause cause = MetricDeathCause::Other;
        if (agent.death_cause == DeathCause::STARVATION) cause = MetricDeathCause::Starvation;
        else if (agent.death_cause == DeathCause::EXHAUSTION) cause = MetricDeathCause::Exhaustion;
        else if (agent.death_cause == DeathCause::BREAKDOWN) cause = MetricDeathCause::Breakdown;
        else if (agent.death_cause == DeathCause::SUICIDE) cause = MetricDeathCause::Suicide;
        else if (agent.death_cause == DeathCause::NATURAL) cause = MetricDeathCause::Natural;
        metrics_.deaths[metric_index(cause)]++;
        metric_death_recorded_[id] = 1;
    }
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

int Simulation::count_built_machines(MachineType type) const {
    int count = 0;
    auto machines = grid_.find_all(TileType::Machine);
    for (auto [x,y] : machines) {
        const auto& d = grid_.data_at(x, y);
        if (d.built && d.machine_type == type) count++;
    }
    return count;
}

int Simulation::built_conveyor_count() const {
    int count = 0;
    auto conveyors = grid_.find_all(TileType::Conveyor);
    for (auto [x,y] : conveyors) {
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

float Simulation::total_storage_output() const {
    float total = 0.0f;
    auto storages = grid_.find_all(TileType::Storage);
    for (auto [x,y] : storages) {
        total += grid_.data_at(x, y).stored_output;
    }
    return total;
}

float Simulation::total_storage_constr_mat() const {
    float total = 0.0f;
    auto storages = grid_.find_all(TileType::Storage);
    for (auto [x,y] : storages) {
        total += grid_.data_at(x, y).stored_construction_material;
    }
    return total;
}

float Simulation::total_inventory_constr_mat() const {
    float total = 0.0f;
    auto view = registry_.view<InventoryComponent, const AgentComponent>();
    for (auto e : view)
        if (registry_.get<AgentComponent>(e).alive)
            total += registry_.get<InventoryComponent>(e).construction_material;
    return total;
}

float Simulation::total_inventory_raw_material() const {
    float total = 0.0f;
    auto view = registry_.view<InventoryComponent, const AgentComponent>();
    for (auto e : view)
        if (registry_.get<AgentComponent>(e).alive)
            total += registry_.get<InventoryComponent>(e).raw_material;
    return total;
}

float Simulation::total_source_resource(ResourceType resource) const {
    float total = 0.0f;
    for (int y = 0; y < grid_.height(); y++)
        for (int x = 0; x < grid_.width(); x++) {
            TileType type = grid_.at(x, y);
            const auto& data = grid_.data_at(x, y);
            if (resource == ResourceType::RAW_FOOD
                && (type == TileType::FoodSource
                    || (type == TileType::Machine && data.built_on_resource
                        && data.machine_type == MachineType::Food))) {
                total += data.resource_amount;
            } else if (resource == ResourceType::RAW_MATERIAL
                       && (type == TileType::ScrapPile
                           || (type == TileType::Machine && data.built_on_resource
                               && data.machine_type == MachineType::Materials))) {
                total += data.resource_amount;
            }
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

    if (spawn_tiles.empty() && config_.initial_population > 0)
        throw std::runtime_error("generated map has no initial spawn tiles");
    std::uniform_int_distribution<int> pick_tile(
        0, std::max(0, static_cast<int>(spawn_tiles.size()) - 1));

    for (int i = 0; i < config_.initial_population; i++) {
        auto [sx, sy] = spawn_tiles[pick_tile(rng_)];

        std::uniform_int_distribution<int> pick_archetype(
            0, static_cast<int>(Archetype::COUNT) - 1);
        Archetype at = static_cast<Archetype>(pick_archetype(rng_));
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

        // Needs start staggered — wider jitter desynchronizes agent cycles
        NeedsComponent needs;
        std::uniform_real_distribution<float> nd(0.0f, 0.25f);
        needs.hunger    = nd(rng_);
        needs.rest      = nd(rng_);
        needs.social    = nd(rng_);
        needs.expression = nd(rng_);
        needs.purpose   = nd(rng_);
        // Opinion priors from archetype + per-agent noise
        OpinionComponent op = archetype_opinion_priors(at);
        std::uniform_real_distribution<float> op_jitter(-0.10f, 0.10f);
        for (int d = 0; d < OpinionComponent::DIMS; d++)
            op.values[d] = std::clamp(op.values[d] + op_jitter(rng_), 0.05f, 0.95f);
        InventoryComponent inv;
        inv.food = config_.initial_food_per_agent;  // bootstrap until the factory runs
        int age_span = config_.founder_age_max_ticks - config_.founder_age_min_ticks;
        int age = config_.founder_age_min_ticks + static_cast<int>(
            lifecycle_unit(0x464f554e44455241ULL, i) * (age_span + 1));
        spawn_agent(sx, sy, AgentOrigin::INITIAL, personality, op, needs, inv, age);
    }
}

// ============================================================
// SYSTEM: Ship Out Output (external quota / production line)
// ============================================================
// Each tick, drain up to quota_per_tick of OUTPUT from any Storage that is 8-adjacent
// to an Exit tile. Track how much we shipped. If the quota wasn't met,
// factory_health drops; meeting it pushes health back up.
// Surplus (over-delivering) gives bonus health recovery.

void Simulation::system_ship_out_food() {
    float quota = current_quota_per_tick_;
    metrics_.quota_demand += quota;
    float to_ship = quota;

    if (output_shipping_enabled_) {
        // Exit-adjacent Storage is the sole drain point.
        auto exits = grid_.find_all(TileType::Exit);
        for (int radius = 1; radius <= 3 && to_ship > 0.001f; radius++) {
            for (auto [ex, ey] : exits) {
                if (to_ship <= 0.001f) break;
                for (int dy = -radius; dy <= radius && to_ship > 0.001f; dy++)
                    for (int dx = -radius; dx <= radius && to_ship > 0.001f; dx++) {
                        if (std::abs(dx) + std::abs(dy) != radius) continue;
                        int nx = ex + dx, ny = ey + dy;
                        if (nx < 0 || nx >= grid_.width() || ny < 0 || ny >= grid_.height()) continue;
                        if (grid_.at(nx, ny) != TileType::Storage) continue;
                        auto& d = grid_.data_at(nx, ny);
                        float take = std::min(to_ship, d.stored_output);
                        if (take > 0.0f) {
                            d.stored_output -= take;
                            to_ship -= take;
                        }
                    }
            }
        }
    } else {
        metrics_.shipping_blocked_ticks++;
    }

    // Phase 2 (REMOVED): previously drained from ANY Storage on the map.
    // This was magical transport — output teleported from distant Storage to Exit
    // without conveyors or hauling. Now the ONLY paths to Exit are:
    //   1. Conveyor chains physically carrying output from Machine → Exit-adjacent Storage
    //   2. Agents hauling output to Exit-adjacent Storage
    // Phase 1 (Exit-adjacent Storage) is the sole drain point.

    // Phase 3 (REMOVED): previously drained stored_output directly from machines.
    // Same problem — bypassed conveyor/storage logistics entirely.

    float shipped = quota - to_ship;
    total_food_shipped_ += shipped;
    metrics_.output_shipped += shipped;
    last_quota_fill_ = (quota > 0.0f) ? (shipped / quota) : 1.0f;

    if (config_.director_mode == DirectorMode::CALM) {
        external_support_ = 1.0f;
        external_supply_factor_ = 1.0f;
    } else {
        float response = std::max(1.0f, config_.external_supply_response_ticks);
        float alpha = 1.0f - std::exp(-1.0f / response);
        external_support_ += alpha
            * (std::clamp(last_quota_fill_, 0.0f, 1.0f) - external_support_);

        if (config_.external_supply_variant == 1) {
            float low = std::clamp(config_.external_supply_low, 0.0f, 0.9999f);
            float high = std::clamp(config_.external_supply_high, low + 0.0001f, 1.0f);
            float x = std::clamp((external_support_ - low) / (high - low), 0.0f, 1.0f);
            float shaped = x * x * (3.0f - 2.0f * x);
            float floor = std::clamp(config_.external_supply_floor, 0.0f, 1.0f);
            external_supply_factor_ = floor + (1.0f - floor) * shaped;
        } else {
            external_supply_factor_ = 1.0f;
        }
    }

    metrics_.external_support_sum += external_support_;
    metrics_.external_supply_factor_sum += external_supply_factor_;
    metrics_.external_support_updates++;

    if (config_.external_supply_variant == 0
        || (config_.external_policy_variant == 0
            && config_.director_mode == DirectorMode::CALM)) {
        if (shipped + 0.001f < quota) {
            factory_health_ = std::max(0.0f, factory_health_ - config_.health_decay_per_miss);
        } else {
            factory_health_ = std::min(1.0f, factory_health_ + config_.health_recovery_per_hit);
        }
    }
}

// ============================================================
// SYSTEM: Factory Deterioration
// ============================================================
// When factory_health falls below threshold, built machines have a small per-tick
// probability of breaking (reverting to unbuilt, build_progress=0). The factory
// pressure forces the community to keep producing — or watch their infrastructure crumble.
// Health floor at 0.10: below this, machines stop breaking (breaks the death spiral,
// gives agents a chance to recover if they can still produce output).

void Simulation::system_factory_deterioration() {
    if (factory_health_ >= config_.machine_break_threshold) return;
    if (factory_health_ < 0.10f) return;  // death spiral escape: stop breaking below 10%
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
                     ") broke down (supply chain contraction)", EventType::MACHINE_BROKE);
        }
    }
}

void Simulation::system_update_factory_condition() {
    float condition_sum = 0.0f;
    int infrastructure = 0;
    for (int y = 0; y < grid_.height(); y++)
        for (int x = 0; x < grid_.width(); x++) {
            TileType t = grid_.at(x, y);
            const auto& d = grid_.data_at(x, y);
            if (t == TileType::Machine && d.built) {
                condition_sum += 1.0f;
                infrastructure++;
            } else if (t == TileType::Conveyor && d.built) {
                condition_sum += std::clamp(d.conveyor_condition, 0.0f, 1.0f);
                infrastructure++;
            }
        }
    factory_health_ = infrastructure > 0 ? condition_sum / infrastructure : 1.0f;
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
                if (d.resource_regen > 0.0f) {
                    ResourceType resource = (t == TileType::FoodSource)
                        ? ResourceType::RAW_FOOD : ResourceType::RAW_MATERIAL;
                    float requested = d.resource_regen * external_supply_factor_;
                    metrics_.regeneration_base[metric_index(resource)] += d.resource_regen;
                    metrics_.regeneration_requested[metric_index(resource)] += requested;
                    if (d.resource_amount < d.resource_max) {
                        float before = d.resource_amount;
                        d.resource_amount = std::min(d.resource_max,
                            d.resource_amount + requested);
                        metrics_.resources_regenerated[metric_index(resource)] +=
                            d.resource_amount - before;
                    }
                }
            }
            // Machine on resource tile: auto-gathers from the tile it sits on.
            // FoodMachine on FoodSource → auto-gathers raw_food into stored_raw_food
            // MaterialsMachine on ScrapPile → auto-gathers raw_material into stored_raw_material
            if (t == TileType::Machine) {
                auto& d = grid_.data_at(x, y);
                if (d.built_on_resource) {
                    // Regen the underlying resource
                    if (d.resource_regen > 0.0f) {
                        ResourceType resource = (d.machine_type == MachineType::Food)
                            ? ResourceType::RAW_FOOD : ResourceType::RAW_MATERIAL;
                        float requested = d.resource_regen * external_supply_factor_;
                        metrics_.regeneration_base[metric_index(resource)] += d.resource_regen;
                        metrics_.regeneration_requested[metric_index(resource)] += requested;
                        if (d.resource_amount < d.resource_max) {
                            float before = d.resource_amount;
                            d.resource_amount = std::min(d.resource_max,
                                d.resource_amount + requested);
                            metrics_.resources_regenerated[metric_index(resource)] +=
                                d.resource_amount - before;
                        }
                    }
                    // Auto-gather into appropriate stored resource
                    if (d.built && d.resource_amount > 0.01f) {
                        float gather = std::min(d.resource_amount, 0.15f);
                        d.resource_amount -= gather;
                        if (d.machine_type == MachineType::Food) {
                            d.stored_raw_food += gather;
                            metrics_.resources_produced[metric_index(ResourceType::RAW_FOOD)] += gather;
                        } else {
                            d.stored_raw_material += gather;
                            metrics_.resources_produced[metric_index(ResourceType::RAW_MATERIAL)] += gather;
                        }
                    }
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

        // Disease amplifies hunger decay — sick agents get hungry faster
        float hunger_decay = config_.hunger_decay * (1.0f + needs.disease * (config_.disease_hunger_mult - 1.0f));
        needs.hunger     = std::min(1.0f, needs.hunger    + hunger_decay);
        needs.rest       = std::min(1.0f, needs.rest      + config_.rest_decay);
        needs.social     = std::min(1.0f, needs.social    + config_.social_decay);
        needs.expression = std::min(1.0f, needs.expression + config_.expression_decay);
        needs.purpose    = std::min(1.0f, needs.purpose   + config_.purpose_decay);
        // B4: Meaning decays every tick — factory work doesn't fill it
        needs.meaning    = std::min(1.0f, needs.meaning   + 0.001f);
        // Disease natural recovery (immune system)
        needs.disease    = std::max(0.0f, needs.disease   - config_.disease_recovery);
    }
}

// ============================================================
// SYSTEM: Stress
// ============================================================

void Simulation::system_update_stress() {
    auto view = registry_.view<StressComponent, NeedsComponent,
                               PersonalityComponent, AgentComponent>();
    for (auto e : view) {
        if (!registry_.get<AgentComponent>(e).alive) continue;

        auto& stress = registry_.get<StressComponent>(e);
        auto& needs  = registry_.get<NeedsComponent>(e);
        auto& personality = registry_.get<PersonalityComponent>(e);
        auto& agent  = registry_.get<AgentComponent>(e);

        // === STRESS INPUT ===
        // Stress has traceable physical/social causes. Higher needs affect
        // utility and mood, but their former nominal stress terms were always
        // dominated by passive decay and are intentionally omitted.
        float stress_input = 0.0f;
        if (needs.hunger > 0.7f)     stress_input += config_.stress_high_need * (needs.hunger - 0.7f);
        if (needs.rest > 0.7f)       stress_input += config_.stress_high_need * (needs.rest - 0.7f);
        // Disease causes stress — being sick in the factory is miserable
        if (needs.disease > 0.3f)    stress_input += config_.stress_high_need * needs.disease * 0.5f;

        // A3: Noncompliance stress — the factory's gaze weighs on you
        // DISSOCIATED agents feel this less; HOSTILE_EUPHORIA agents ignore it
        // Phase 3: continuous modifier (was: discrete FSM branches)
        float noncomp_mult = (config_.stress_model_variant == 1)
            ? stress_noncomp_mult(stress.value)
            : (stress.state == StressState::DISSOCIATED ? 0.5f
               : stress.state == StressState::HOSTILE_EUPHORIA ? 0.0f : 1.0f);
        if (config_.external_policy_variant == 0) {
            stress_input += config_.noncompliance_stress * agent.noncompliance * noncomp_mult;
        }

        // Trauma reduces effective resilience — the more damaged you are, the faster stress builds
        float effective_resilience = personality.resilience * (1.0f - stress.trauma * config_.trauma_resilience_impact);
        stress_input *= (1.0f - effective_resilience * 0.7f);

        // Stress decay: proportional to basic need satisfaction.
        // B: When hunger and rest are low (needs met), stress decays faster.
        // This rewards balanced lifestyles — agents who eat and rest recover from stress.
        // When basic needs are unmet, decay is minimal (stress persists).
        float basic_satisfaction = (1.0f - needs.hunger) * (1.0f - needs.rest);
        float decay = config_.stress_decay * (1.0f + basic_satisfaction * 8.0f);
        // Additional decay when actively socializing/creating (positive outlets)
        // These are handled in sim_execute.cpp — this is just passive background decay.
        stress.value = std::min(1.0f, stress.value + stress_input);
        stress.value = std::max(0.0f, stress.value - decay);

        // === TRAUMA ACCUMULATION ===
        // Chronic stress (above 0.5) permanently damages the agent
        if (stress.value > 0.5f) {
            stress.ticks_in_state++;
            stress.trauma = std::min(1.0f, stress.trauma + config_.trauma_accumulation_rate);
        } else {
            stress.ticks_in_state = 0;
        }

        // === STRESS STATE TRANSITIONS ===
        // Phase 3: in continuous mode, the state is a derived display label
        // (computed from stress.value), not a behavioral driver. The chronicle
        // still logs transitions for narrative continuity.
        StressState old_state = stress.state;
        StressState new_state = stress_state_from_value(stress.value);
        if (new_state != old_state) {
            stress.state = new_state;
            char buf[80];
            std::snprintf(buf, sizeof(buf), "%s -> %s (stress %.2f)",
                stress_state_name(old_state), stress_state_name(new_state), stress.value);
            chronicle(agent.id, EventType::STRESS_STATE_CHANGE, buf,
                -1, -1, stress.value);
        }

        // === HOSTILE EUPHORIA: artificial mood boost ===
        // The agent appears happy but is disconnected from reality.
        // Phase 3: continuous — mood boost scales with stress in the EUPHORIC band.
        // Legacy: flat +0.005 if state==HOSTILE_EUPHORIA.
        if (config_.stress_model_variant == 1) {
            // Boost peaks in the 0.7-0.9 band, fades at BROKEN
            float euphoric_band = smoothstep(0.6f, 0.75f, stress.value)
                                * (1.0f - smoothstep(0.85f, 1.0f, stress.value));
            if (euphoric_band > 0.001f) {
                auto& soc = registry_.get<SocialComponent>(e);
                soc.mood = std::min(1.0f, soc.mood + 0.005f * euphoric_band);
            }
        } else if (stress.state == StressState::HOSTILE_EUPHORIA) {
            auto& soc = registry_.get<SocialComponent>(e);
            soc.mood = std::min(1.0f, soc.mood + 0.005f); // fake happiness
        }

        // === SOCIAL CONTAGION: stressed agents repel others ===
        // DISSOCIATED/BROKEN agents lose social connections; others avoid them.
        // Phase 3: continuous — mood drain scales with stress above 0.4.
        if (config_.stress_model_variant == 1) {
            float drain = smoothstep(0.4f, 0.9f, stress.value) * 0.003f;
            if (drain > 0.0001f) {
                auto& soc = registry_.get<SocialComponent>(e);
                soc.mood = std::max(0.0f, soc.mood - drain);
            }
        } else if (stress.state == StressState::DISSOCIATED || stress.state == StressState::BROKEN) {
            auto& soc = registry_.get<SocialComponent>(e);
            soc.mood = std::max(0.0f, soc.mood - 0.003f); // internal drain
        }
    }
}

// ============================================================
// SYSTEM: Death Check
// ============================================================

void Simulation::system_check_deaths() {
    auto view = registry_.view<NeedsComponent, AgentComponent, StressComponent, PersonalityComponent>();
    for (auto e : view) {
        auto& agent  = registry_.get<AgentComponent>(e);
        if (!agent.alive) continue;

        auto& needs  = registry_.get<NeedsComponent>(e);
        auto& stress = registry_.get<StressComponent>(e);

        bool starvation_death = false;
        if (needs.hunger >= 1.0f) {
            agent.ticks_at_max_hunger++;
            starvation_death = agent.ticks_at_max_hunger >= config_.starvation_ticks;
        } else {
            agent.ticks_at_max_hunger = 0;
        }

        bool exhaustion_death = false;
        if (needs.rest >= 1.0f) {
            agent.ticks_at_max_rest++;
            exhaustion_death = agent.ticks_at_max_rest >= config_.exhaustion_ticks;
        } else {
            agent.ticks_at_max_rest = 0;
        }

        // Breakdown: stress kills, but not instantly.
        // BROKEN agents survive longer — they have time to sabotage or redeem.
        // Normal agents at breakdown threshold die slowly.
        // Phase 3: continuous death_chance scales down as stress approaches 1.0
        // (BROKEN agents linger more). Legacy uses discrete state branches.
        bool breakdown_death = false;
        if (stress.value >= config_.breakdown_threshold) {
            float death_chance;
            if (config_.stress_model_variant == 1) {
                // Continuous: 0.005 at threshold, ramps to 0.003 at stress=1.0
                float broken_ramp = smoothstep(config_.breakdown_threshold, 1.0f, stress.value);
                death_chance = 0.005f - broken_ramp * 0.002f;
            } else {
                death_chance = 0.005f;
                if (stress.state == StressState::BROKEN) death_chance = 0.003f;
            }
            std::uniform_real_distribution<float> roll(0.0f, 1.0f);
            breakdown_death = roll(registry_.get<RandomComponent>(e).engine) < death_chance;
        }

        bool natural_death = false;
        if (config_.natural_mortality_enabled) {
            const auto& lifecycle = registry_.get<LifecycleComponent>(e);
            natural_death = lifecycle_age(lifecycle) >= lifecycle.lifespan;
        }

        bool died = false;
        if (starvation_death) {
            died = kill_agent(e, EventType::DIED_STARVATION,
                "DIED of starvation after " +
                std::to_string(config_.starvation_ticks) + " ticks without food");
        } else if (exhaustion_death) {
            died = kill_agent(e, EventType::DIED_EXHAUSTION,
                "DIED of exhaustion after " +
                std::to_string(config_.exhaustion_ticks) + " ticks without rest");
        } else if (breakdown_death) {
            died = kill_agent(e, EventType::DIED_BREAKDOWN,
                "had a BREAKDOWN (stress=" + ff2(stress.value) + ")");
        } else if (natural_death) {
            died = kill_agent(e, EventType::DIED_NATURAL,
                "died of natural causes at age "
                + std::to_string(lifecycle_age(
                    registry_.get<LifecycleComponent>(e))));
        }

        // Factory collapse: when factory_health == 0, the crumbling factory
        // increases stress on all agents but does NOT kill them directly.
        // The factory is the environment, not the executioner.
        // Agents die from hunger, stress breakdown, or exhaustion — not the building.
        if (!died
            && (config_.external_supply_variant == 0
                || (config_.external_policy_variant == 0
                    && config_.director_mode == DirectorMode::CALM))
            && factory_health_ <= 0.0f) {
            stress.value = std::min(1.0f, stress.value + 0.002f);  // environmental dread
        }
    }

    apply_pending_grief();
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
                social_.record_negative_observation(
                    ag.id, dismantler_id, tick_, severity);

                // Witness gets a small stress bump (disorder is stressful)
                auto& st = registry_.get<StressComponent>(e);
                st.value = std::clamp(st.value + 0.003f, 0.0f, 1.0f);
            }
        }
}

// ============================================================
// A2: SYSTEM: Factory Restructure (adversarial best-response)
// ============================================================
// The factory reconfigures itself periodically. Unlike the previous
// uniform-random implementation, this version makes the factory an
// EVALUATOR (doc/adversarial_utility_agents.md): it scores every candidate
// target by physical strategic value (how much output/throughput it would
// lose), then softmax-selects without access to social labels.
//
// adversary_intensity α ∈ [0,1] blends the strategic score with uniform
// randomness: α=0 reproduces the old random baseline exactly; α=1 is pure
// best-response; intermediate values sit at the "edge of chaos".
//
// Audit: every attack logs its target and physical score.

void Simulation::system_factory_restructure_legacy() {
    if (tick_ % config_.restructure_interval != 0) return;
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    if (roll(rng_) > config_.restructure_probability) return;

    // ---------------------------------------------------------------
    // 1. Collect candidates across all targetable infrastructure.
    //    Each candidate carries (x, y, kind, strategic_score).
    // ---------------------------------------------------------------
    enum class TargetKind : uint8_t { CONVEYOR, MACHINE, STORAGE };
    struct Candidate { int x, y; TargetKind kind; float strategic; };

    std::vector<Candidate> candidates;

    for (auto [cx, cy] : grid_.find_all(TileType::Conveyor)) {
        const auto& d = grid_.data_at(cx, cy);
        if (!d.built) continue;
        // Strategic value of a conveyor = what it's carrying right now
        // + a bonus if it sits on a live production chain to the Exit.
        float s = d.conveyor_contents;
        // Bonus: does this belt carry output toward shipping?
        auto [tx, ty] = grid_.conveyor_target(cx, cy);
        if (tx >= 0 && (grid_.at(tx, ty) == TileType::Storage ||
                        grid_.at(tx, ty) == TileType::Exit)) s += 0.5f;
        candidates.push_back({cx, cy, TargetKind::CONVEYOR, s});
    }
    for (auto [mx, my] : grid_.find_all(TileType::Machine)) {
        const auto& d = grid_.data_at(mx, my);
        if (!d.built) continue;
        // Strategic value = banked output + construction material on the tile.
        // Extra weight if the machine is connected to the Exit (hurting it
        // directly damages quota fulfillment).
        float s = d.stored_output + d.stored_construction_material * 0.5f;
        if (d.machine_type == MachineType::Output &&
            grid_.machine_connected_to_exit(mx, my)) s += 1.0f;
        // Food machines are strategically critical when supply is low.
        if (config_.external_supply_variant == 0
            && d.machine_type == MachineType::Food && last_quota_fill_ < 0.5f) {
            s += 0.75f;
        }
        candidates.push_back({mx, my, TargetKind::MACHINE, s});
    }
    for (auto [sx, sy] : grid_.find_all(TileType::Storage)) {
        const auto& d = grid_.data_at(sx, sy);
        if (d.stored_output <= 0.01f) continue;
        candidates.push_back({sx, sy, TargetKind::STORAGE, d.stored_output});
    }

    if (candidates.empty()) return;

    // Score each candidate from physical strategic value only. Even this legacy
    // policy cannot observe graph-community labels.
    float alpha = std::clamp(config_.adversary_intensity, 0.0f, 1.0f);
    std::vector<float> scores(candidates.size());
    float max_score = 0.0f;
    for (size_t i = 0; i < candidates.size(); i++) {
        float strat = config_.strategic_weight * candidates[i].strategic;
        float adversarial = strat;
        float uniform = 1.0f;  // every candidate is equally likely under baseline
        scores[i] = alpha * adversarial + (1.0f - alpha) * uniform;
        if (scores[i] > max_score) max_score = scores[i];
    }

    // ---------------------------------------------------------------
    // 4. Softmax selection over the blended scores.
    //    temperature → 0 collapses to argmax (pure best-response).
    // ---------------------------------------------------------------
    float tau = std::max(0.001f, config_.restructure_temperature);
    std::vector<float> weights(candidates.size());
    float sum_w = 0.0f;
    for (size_t i = 0; i < candidates.size(); i++) {
        weights[i] = std::exp((scores[i] - max_score) / tau);
        sum_w += weights[i];
    }
    std::uniform_real_distribution<float> pick(0.0f, sum_w);
    float r = pick(rng_);
    size_t chosen = 0;
    float cum = 0.0f;
    for (size_t i = 0; i < candidates.size(); i++) {
        cum += weights[i];
        if (r <= cum) { chosen = i; break; }
    }

    // Apply the attack and audit log.
    auto& target = candidates[chosen];
    float strat_score = config_.strategic_weight * target.strategic;

    switch (target.kind) {
        case TargetKind::CONVEYOR: {
            auto& d = grid_.data_at(target.x, target.y);
            int dir = static_cast<int>(d.conveyor_dir);
            d.conveyor_dir = static_cast<ConveyorDir>((dir + 2) % 4);
            std::string msg = "FACTORY restructured: conveyor at (" +
                std::to_string(target.x) + "," + std::to_string(target.y) +
                ") reversed [strat=" + ff2(strat_score) + "]";
            emit_log(-1, msg, EventType::FACTORY_RESTRUCTURE);
            total_restructures_++;
            break;
        }
        case TargetKind::MACHINE: {
            auto& d = grid_.data_at(target.x, target.y);
            d.build_progress = std::max(0.0f, d.build_progress - d.build_cost * 0.3f);
            if (d.build_progress <= 0.0f) {
                d.built = false;
                d.build_progress = 0.0f;
            }
            std::string msg = "FACTORY restructured: machine at (" +
                std::to_string(target.x) + "," + std::to_string(target.y) +
                ") damaged [strat=" + ff2(strat_score) + "]";
            emit_log(-1, msg, EventType::FACTORY_RESTRUCTURE);
            total_restructures_++;
            break;
        }
        case TargetKind::STORAGE: {
            auto& d = grid_.data_at(target.x, target.y);
            float confiscated = d.stored_output * 0.5f;
            d.stored_output -= confiscated;
            metrics_.resources_lost[metric_index(ResourceType::OUTPUT)] += confiscated;
            std::string msg = "FACTORY confiscated " + ff2(confiscated) +
                " output from storage at (" +
                std::to_string(target.x) + "," + std::to_string(target.y) +
                ") [strat=" + ff2(strat_score) + "]";
            emit_log(-1, msg, EventType::FACTORY_CONFISCATED);
            total_restructures_++;
            break;
        }
    }
}

// ============================================================
// A3: Noncompliance is tracked in sim_execute.cpp
//     Stress from noncompliance is in system_update_stress() below
// ============================================================
// (noncompliance field on AgentComponent, accumulated in execute,
//  applied as stress in system_update_stress)

// ============================================================
// B1: SYSTEM: Artifact Effects (mood boost + decay)
// ============================================================

void Simulation::system_artifact_effects() {
    // Process all artifacts: boost nearby agents' mood, then decay
    auto artifact_view = registry_.view<PositionComponent, struct ArtifactComponent>();
    std::vector<entt::entity> to_remove;

    for (auto ae : artifact_view) {
        auto& apos = registry_.get<PositionComponent>(ae);
        auto& aart = registry_.get<struct ArtifactComponent>(ae);

        // Artifact response is personal rather than an objective beauty score.
        auto alive_view = registry_.view<PositionComponent, SocialComponent, const AgentComponent>();
        for (auto e : alive_view) {
            if (!registry_.get<AgentComponent>(e).alive) continue;
            auto& p = registry_.get<PositionComponent>(e);
            int d = std::abs(p.x - apos.x) + std::abs(p.y - apos.y);
            if (config_.artifact_effects_enabled && d <= 2) {
                auto& soc = registry_.get<SocialComponent>(e);
                const auto& personality = registry_.get<PersonalityComponent>(e);
                float response = 0.002f + personality.artistry * 0.006f;
                soc.mood = std::min(1.0f, soc.mood + aart.strength * response);
            }
        }

        // Decay artifact
        aart.strength -= 0.001f;
        aart.age++;
        if (aart.strength <= 0.0f) {
            to_remove.push_back(ae);
        }
    }

    for (auto ae : to_remove) {
        registry_.destroy(ae);
        artifacts_active_--;
    }
}

// ============================================================
// B2: SYSTEM: Hidden Space Exposure
// ============================================================
// If more than 2 agents stand on the same HiddenSpace for 10+ ticks,
// the factory notices and seals it (reverts to Floor + stress spike).

void Simulation::system_hidden_space_exposure() {
    auto hidden = grid_.find_all(TileType::HiddenSpace);
    for (auto [hx, hy] : hidden) {
        auto& d = grid_.data_at(hx, hy);

        // Count agents on this tile
        int agents_here = 0;
        auto alive_view = registry_.view<PositionComponent, const AgentComponent>();
        for (auto e : alive_view) {
            if (!registry_.get<AgentComponent>(e).alive) continue;
            auto& p = registry_.get<PositionComponent>(e);
            if (p.x == hx && p.y == hy) agents_here++;
        }

        if (agents_here > 2) {
            d.hidden_space_occupancy++;
        } else {
            d.hidden_space_occupancy = std::max(0, d.hidden_space_occupancy - 1);
        }

        if (d.hidden_space_occupancy >= 10) {
            // Factory seals the space
            grid_.set(hx, hy, TileType::Floor);
            emit_log(-1, "FACTORY sealed a hidden space at (" +
                     std::to_string(hx) + "," + std::to_string(hy) + ")",
                     EventType::FACTORY_SEALED_SPACE);
            hidden_spaces_sealed_++;

            // Stress spike for agents on the tile
            for (auto e : alive_view) {
                if (!registry_.get<AgentComponent>(e).alive) continue;
                auto& p = registry_.get<PositionComponent>(e);
                if (p.x == hx && p.y == hy) {
                    auto& st = registry_.get<StressComponent>(e);
                    st.value = std::min(1.0f, st.value + 0.15f);
                }
            }
        }
    }
}

// ============================================================
// Observable social evidence and graph communities
// ============================================================

void Simulation::system_social_learning() {
    if (!config_.social_learning_enabled) return;
    auto alive = alive_agents();
    for (size_t i = 0; i < alive.size(); i++) {
        const auto& pos_i = registry_.get<PositionComponent>(alive[i]);
        const auto& action_i = registry_.get<ActionComponent>(alive[i]);
        int id_i = registry_.get<AgentComponent>(alive[i]).id;
        for (size_t j = i + 1; j < alive.size(); j++) {
            const auto& pos_j = registry_.get<PositionComponent>(alive[j]);
            int distance = std::abs(pos_i.x - pos_j.x) + std::abs(pos_i.y - pos_j.y);
            if (distance > 2) continue;

            int id_j = registry_.get<AgentComponent>(alive[j]).id;
            social_.record_copresence(id_i, id_j, tick_);

            const auto& action_j = registry_.get<ActionComponent>(alive[j]);
            bool collaborative = action_i.current == ActionType::BUILD
                              || action_i.current == ActionType::WORK
                              || action_i.current == ActionType::CREATE;
            if (collaborative && action_i.current == action_j.current
                && action_i.effected_last_tick && action_j.effected_last_tick) {
                social_.record_collaboration(id_i, id_j, tick_);
            }
        }
    }
}

void Simulation::system_spatial_learning() {
    if (!config_.spatial_affinity_enabled) return;

    auto alive = alive_agents();
    for (auto entity : alive) {
        const auto& pos = registry_.get<PositionComponent>(entity);
        const auto& action = registry_.get<ActionComponent>(entity);
        const auto& stress = registry_.get<StressComponent>(entity);
        auto& memory = registry_.get<PlaceMemoryComponent>(entity);

        int crowd = 0;
        for (auto other : alive) {
            if (other == entity) continue;
            const auto& other_pos = registry_.get<PositionComponent>(other);
            if (std::abs(other_pos.x - pos.x) + std::abs(other_pos.y - pos.y) <= 2)
                crowd++;
        }

        float outcome = -stress.value * 0.12f
                      - std::max(0, crowd - 4) * 0.03f;
        if (action.effected_last_tick) {
            switch (action.current) {
                case ActionType::EAT:       outcome += 0.60f; break;
                case ActionType::REST:      outcome += 0.40f; break;
                case ActionType::SOCIALIZE: outcome += 0.45f; break;
                case ActionType::CREATE:    outcome += 0.45f; break;
                case ActionType::WORK:
                case ActionType::BUILD:     outcome += 0.15f; break;
                case ActionType::SABOTAGE:  outcome -= 0.50f; break;
                default:                    outcome += 0.05f; break;
            }
        } else if (stress.value < 0.5f && crowd <= 4) {
            continue;
        }
        outcome = std::clamp(outcome, -1.0f, 1.0f);

        auto found = std::find_if(memory.places.begin(), memory.places.end(),
            [&](const PlaceMemoryEntry& place) {
                return place.x == pos.x && place.y == pos.y;
            });
        if (found == memory.places.end()) {
            if (memory.places.size() >= 24) {
                found = std::min_element(memory.places.begin(), memory.places.end(),
                    [](const PlaceMemoryEntry& a, const PlaceMemoryEntry& b) {
                        return a.last_tick < b.last_tick;
                    });
                *found = {pos.x, pos.y, outcome, 1, tick_};
            } else {
                memory.places.push_back({pos.x, pos.y, outcome, 1, tick_});
            }
        } else {
            found->exposures++;
            float rate = 1.0f / std::min(12, found->exposures);
            found->affinity = std::clamp(
                found->affinity + (outcome - found->affinity) * rate,
                -1.0f, 1.0f);
            found->last_tick = tick_;
        }
    }
}

void Simulation::system_record_emergence_metrics() {
    if (tick_ % 50 != 0) return;

    auto alive = alive_agents();
    std::sort(alive.begin(), alive.end(), [&](entt::entity a, entt::entity b) {
        return registry_.get<AgentComponent>(a).id
             < registry_.get<AgentComponent>(b).id;
    });
    if (alive.size() < 2) return;

    auto pair_key = [](int a, int b) {
        uint32_t low = static_cast<uint32_t>(std::min(a, b));
        uint32_t high = static_cast<uint32_t>(std::max(a, b));
        return (static_cast<uint64_t>(low) << 32) | high;
    };
    auto jaccard = [](const std::set<uint64_t>& a,
                      const std::set<uint64_t>& b) {
        size_t intersection = 0;
        auto ia = a.begin();
        auto ib = b.begin();
        while (ia != a.end() && ib != b.end()) {
            if (*ia == *ib) { intersection++; ia++; ib++; }
            else if (*ia < *ib) ia++;
            else ib++;
        }
        size_t union_size = a.size() + b.size() - intersection;
        return union_size > 0
            ? static_cast<double>(intersection) / union_size : 0.0;
    };

    std::set<uint64_t> spatial_pairs;
    std::set<uint64_t> community_pairs;
    for (size_t i = 0; i < alive.size(); i++) {
        const auto& pos_i = registry_.get<PositionComponent>(alive[i]);
        const auto& agent_i = registry_.get<AgentComponent>(alive[i]);
        for (size_t j = i + 1; j < alive.size(); j++) {
            const auto& pos_j = registry_.get<PositionComponent>(alive[j]);
            const auto& agent_j = registry_.get<AgentComponent>(alive[j]);
            if (std::abs(pos_i.x - pos_j.x) + std::abs(pos_i.y - pos_j.y) <= 3)
                spatial_pairs.insert(pair_key(agent_i.id, agent_j.id));
            if (agent_i.community_id >= 0 && agent_i.community_id == agent_j.community_id)
                community_pairs.insert(pair_key(agent_i.id, agent_j.id));
        }
    }
    if (have_spatial_sample_) {
        metrics_.spatial_persistence_sum += jaccard(
            previous_spatial_pairs_, spatial_pairs);
        metrics_.spatial_persistence_samples++;
    }
    if (have_community_sample_) {
        metrics_.community_stability_sum += jaccard(
            previous_community_pairs_, community_pairs);
        metrics_.community_stability_samples++;
    }
    previous_spatial_pairs_ = std::move(spatial_pairs);
    previous_community_pairs_ = std::move(community_pairs);
    have_spatial_sample_ = true;
    have_community_sample_ = true;

    auto trait_distance = [&](size_t i, size_t j, size_t offset) {
        const auto& a = registry_.get<PersonalityComponent>(
            alive[(i + offset) % alive.size()]);
        const auto& b = registry_.get<PersonalityComponent>(
            alive[(j + offset) % alive.size()]);
        float sum = 0.0f;
        for (auto [left, right] : {
                 std::pair{a.compliance, b.compliance},
                 std::pair{a.laziness, b.laziness},
                 std::pair{a.artistry, b.artistry},
                 std::pair{a.gregariousness, b.gregariousness},
                 std::pair{a.resilience, b.resilience},
                 std::pair{a.curiosity, b.curiosity}}) {
            float delta = left - right;
            sum += delta * delta;
        }
        return sum;
    };
    auto nearest_trait_distance = [&](size_t offset) {
        double spatial_sum = 0.0;
        for (size_t i = 0; i < alive.size(); i++) {
            size_t nearest = i == 0 ? 1 : 0;
            float best = trait_distance(i, nearest, offset);
            for (size_t j = 0; j < alive.size(); j++) {
                if (i == j) continue;
                float candidate = trait_distance(i, j, offset);
                if (candidate < best) { best = candidate; nearest = j; }
            }
            const auto& a = registry_.get<PositionComponent>(alive[i]);
            const auto& b = registry_.get<PositionComponent>(alive[nearest]);
            spatial_sum += std::abs(a.x - b.x) + std::abs(a.y - b.y);
        }
        return spatial_sum / alive.size();
    };
    size_t shuffle_offset = 1 + (tick_ / 50) % (alive.size() - 1);
    metrics_.personality_distance_delta_sum +=
        nearest_trait_distance(shuffle_offset) - nearest_trait_distance(0);
    metrics_.personality_distance_samples++;

    std::vector<double> degree(alive.size(), 0.0);
    std::vector<std::vector<double>> weights(
        alive.size(), std::vector<double>(alive.size(), 0.0));
    double total_weight = 0.0;
    for (size_t i = 0; i < alive.size(); i++) {
        int id_i = registry_.get<AgentComponent>(alive[i]).id;
        for (size_t j = i + 1; j < alive.size(); j++) {
            int id_j = registry_.get<AgentComponent>(alive[j]).id;
            const auto& ij = social_.get_rel(id_i, id_j);
            const auto& ji = social_.get_rel(id_j, id_i);
            double weight = std::max(0.0f, (ij.trust + ji.trust) * 0.5f)
                          * std::min(ij.familiarity, ji.familiarity);
            weights[i][j] = weights[j][i] = weight;
            degree[i] += weight;
            degree[j] += weight;
            total_weight += weight;
        }
    }
    double modularity = 0.0;
    if (total_weight > 0.0) {
        double denominator = 2.0 * total_weight;
        for (size_t i = 0; i < alive.size(); i++) {
            int community_i = registry_.get<AgentComponent>(alive[i]).community_id;
            if (community_i < 0) community_i = -1 - registry_.get<AgentComponent>(alive[i]).id;
            for (size_t j = 0; j < alive.size(); j++) {
                int community_j = registry_.get<AgentComponent>(alive[j]).community_id;
                if (community_j < 0) community_j = -1 - registry_.get<AgentComponent>(alive[j]).id;
                if (community_i != community_j) continue;
                modularity += weights[i][j] - degree[i] * degree[j] / denominator;
            }
        }
        modularity /= denominator;
    }
    metrics_.social_modularity_sum += modularity;
    metrics_.social_modularity_samples++;
}

void Simulation::system_community_detection() {
    // Run every 50 ticks (expensive)
    if (tick_ % 50 != 0) return;

    auto alive = alive_agents();
    int n = (int)alive.size();
    if (n < 3) return;

    std::vector<int> old_community(n, -1);
    for (int i = 0; i < n; i++) {
        old_community[i] = registry_.get<AgentComponent>(alive[i]).community_id;
    }

    // IDs are rebuilt from graph evidence and remain observational.
    for (auto e : alive) {
        registry_.get<AgentComponent>(e).community_id = -1;
    }

    // Derive connected components from the relationship graph for observation.
    // The resulting label is never consumed by behavior.
    int next_community = 0;
    std::vector<int> component(n, -1);

    for (int i = 0; i < n; i++) {
        if (component[i] >= 0) continue;

        // BFS: find all agents reachable through reciprocal social evidence.
        std::vector<int> cluster;
        std::vector<int> queue;
        queue.push_back(i);
        component[i] = next_community;
        cluster.push_back(i);

        while (!queue.empty()) {
            int cur = queue.back();
            queue.pop_back();
            int cid = registry_.get<AgentComponent>(alive[cur]).id;

            for (int j = 0; j < n; j++) {
                if (component[j] >= 0) continue;
                int oid = registry_.get<AgentComponent>(alive[j]).id;
                const auto& rel_ab = social_.get_rel(cid, oid);
                const auto& rel_ba = social_.get_rel(oid, cid);

                // Both agents must know and trust one another.
                bool trust_ok = rel_ab.trust > 0.3f && rel_ba.trust > 0.3f
                             && rel_ab.familiarity > 0.2f
                             && rel_ba.familiarity > 0.2f;
                if (!trust_ok) continue;

                component[j] = next_community;
                cluster.push_back(j);
                queue.push_back(j);
            }
        }

        // Components smaller than three remain unlabelled.
        if ((int)cluster.size() >= 3) {
            for (int idx : cluster) {
                registry_.get<AgentComponent>(alive[idx]).community_id = next_community;
            }
            next_community++;
        }
    }

    communities_detected_ = next_community;

    std::vector<int> community_sizes(next_community, 0);
    for (int i = 0; i < n; i++) {
        int fid = registry_.get<AgentComponent>(alive[i]).community_id;
        if (fid >= 0 && fid < next_community) community_sizes[fid]++;
    }

    // Emit factual records of observed component changes.
    std::set<int> formed_logged;
    for (int i = 0; i < n; i++) {
        int new_fid = registry_.get<AgentComponent>(alive[i]).community_id;
        int old_fid = old_community[i];

        if (new_fid >= 0 && old_fid < 0) {
            int aid = registry_.get<AgentComponent>(alive[i]).id;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "entered graph community %d", new_fid);
            chronicle(aid, EventType::COMMUNITY_ENTERED, buf, -1, -1, 0.0f, new_fid);

            if (formed_logged.insert(new_fid).second) {
                char fbuf[64];
                std::snprintf(fbuf, sizeof(fbuf),
                    "graph community %d detected (%d members)",
                    new_fid, community_sizes[new_fid]);
                chronicle_.log(tick_, EventType::COMMUNITY_DETECTED, -1, fbuf);
            }
        } else if (old_fid >= 0 && new_fid < 0) {
            int aid = registry_.get<AgentComponent>(alive[i]).id;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "left graph community %d", old_fid);
            chronicle(aid, EventType::COMMUNITY_LEFT, buf, -1, -1, 0.0f, old_fid);
        }
    }
}

// ============================================================
// SYSTEM: Chronicle narrative milestones
// ============================================================

void Simulation::system_chronicle_narrative() {
    // Run every tick (cheap: just checks counters)

    int alive = alive_count();

    // Firsts
    if (!first_build_done_ && total_machines_built_ >= 1) {
        first_build_done_ = true;
        chronicle_.log(tick_, EventType::FIRST_BUILD, -1,
            "the inhabitants complete their first repair or expansion");
    }
    if (!first_death_done_ && chronicle_.count_of_type(EventType::DIED_STARVATION) +
                               chronicle_.count_of_type(EventType::DIED_EXHAUSTION) +
                               chronicle_.count_of_type(EventType::DIED_BREAKDOWN) +
                               chronicle_.count_of_type(EventType::DIED_COLLAPSE) +
                               chronicle_.count_of_type(EventType::DIED_SUICIDE) +
                               chronicle_.count_of_type(EventType::DIED_NATURAL) >= 1) {
        first_death_done_ = true;
        chronicle_.log(tick_, EventType::FIRST_DEATH, -1,
            "first recorded agent death");
    }
    if (!first_sabotage_done_ && sabotages_total_ >= 1) {
        first_sabotage_done_ = true;
        chronicle_.log(tick_, EventType::FIRST_SABOTAGE, -1,
            "first completed sabotage");
    }
    if (!first_community_done_ && communities_detected_ >= 1) {
        first_community_done_ = true;
        chronicle_.log(tick_, EventType::FIRST_COMMUNITY, -1,
            "first relationship-graph community detected");
    }
    if (!first_artifact_done_ && artifacts_created_ >= 1) {
        first_artifact_done_ = true;
        chronicle_.log(tick_, EventType::FIRST_ARTIFACT, -1,
            "first completed artifact");
    }

    // Population milestones
    if (alive > 0 && alive % 5 == 0 && alive != last_population_milestone_) {
        last_population_milestone_ = alive;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d agents survive", alive);
        chronicle_.log(tick_, EventType::POPULATION_MILESTONE, -1, buf);
    }

    // Crisis detection: factory health < 0.25 and hasn't fired in 200 ticks
    if (config_.director_mode != DirectorMode::CALM
        && factory_health_ < 0.25f && tick_ - last_crisis_tick_ >= 200) {
        last_crisis_tick_ = tick_;
        char buf[64];
        std::snprintf(buf, sizeof(buf),
            "factory health critical (%.0f%%) — the machine falters", factory_health_ * 100);
        chronicle_.log(tick_, EventType::CRISIS_PERIOD, -1, buf,
            -1, -1, factory_health_);
    }

    // Quota milestones
    float qf = last_quota_fill_;
    if (config_.director_mode != DirectorMode::CALM
        && qf >= 1.0f && last_quota_milestone_ < 1.0f) {
        chronicle_.log(tick_, EventType::QUOTA_MILESTONE, -1,
            "quota met — the factory is satisfied");
    }
    last_quota_milestone_ = qf;
}
