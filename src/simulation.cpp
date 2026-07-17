#include "simulation.h"
#include "textgen.h"
#include "production.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

// Static members for ChronicleEvent CFG text generation
TextGen* ChronicleEvent::s_textgen = nullptr;
std::mt19937* ChronicleEvent::s_rng = nullptr;

// ============================================================
// Construction & lifecycle
// ============================================================

Simulation::Simulation(const Config& cfg)
    : config_(cfg)
    , grid_(cfg.grid_width, cfg.grid_height)
    , social_(cfg.max_population)
    , textgen_(make_narrative_grammar())
    , rng_(cfg.seed)
    , tick_(0)
    , factory_health_(1.0f)
    , total_food_produced_(0.0f)
    , total_output_produced_(0.0f)
    , total_raw_gathered_(0.0f)
    , total_machines_built_(0)
    , current_quota_per_tick_(cfg.quota_per_tick)
{
    grid_.generate_wfc(cfg.seed);
    // Connect CFG text generator to ChronicleEvent
    ChronicleEvent::s_textgen = &textgen_;
    ChronicleEvent::s_rng = &rng_;
    spawn_initial_agents();
}

void Simulation::advance() {
    system_regen_resources();
    system_decay_needs();

    bool calm = (config_.director_mode == DirectorMode::CALM);

    // A1: Quota escalation — the factory demands more over time.
    // Disabled in CALM mode: no external pressure.
    if (!calm) {
        float quota_cap = config_.quota_per_tick * 3.0f;
        current_quota_per_tick_ = std::min(quota_cap,
            current_quota_per_tick_ + config_.quota_growth_rate);
    } else {
        current_quota_per_tick_ = 0.0f;
        factory_health_ = 1.0f;  // no decay in calm mode
    }

    system_compute_utility();
    system_find_targets();
    system_move_to_targets();
    system_execute_actions();
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
    colony_prod_ = ProductionChain::assess(grid_, alive_count(), last_quota_fill_, agent_c_mat);

    // Pressure systems — disabled in CALM mode.
    // The factory doesn't deteriorate, restructure, or seal spaces.
    if (!calm) {
        system_factory_deterioration();
        system_factory_restructure();
        system_hidden_space_exposure();
    }

    system_artifact_effects();
    system_hidden_space_exposure();// B2: factory seals overused hidden spaces
    system_faction_formation();    // B3: trust clusters become factions
    system_update_stress();
    system_check_deaths();

    // Social systems (every tick)
    auto alive = alive_agents();
    social_.apply_contagion(registry_, alive);
    social_.update_influence(registry_, alive);
    social_.update_mood(registry_, alive);
    social_.decay_relationships(tick_);
    social_.leader_opinion_pull(alive, registry_, factions_formed_);

    // Social penalty: agents who dismantled conveyors that weren't rebuilt
    // within the window lose trust with everyone who notices.
    system_check_dismantle_penalties();
    system_chronicle_narrative();

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

        // Balanced archetype distribution, cycles proportionally for any N.
        // Ratios: 4 Foreman, 4 Networker, 4 Worker, 4 Artisan, 4 Explorer, 4 Survivor
        // (equal split across 6 archetypes with slight variety)
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
        constexpr int dist_size = sizeof(distribution) / sizeof(distribution[0]);
        Archetype at = distribution[i % dist_size];
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

        // Needs start staggered — wider jitter desynchronizes agent cycles
        NeedsComponent needs;
        std::uniform_real_distribution<float> nd(0.0f, 0.25f);
        needs.hunger    = nd(rng_);
        needs.rest      = nd(rng_);
        needs.social    = nd(rng_);
        needs.expression = nd(rng_);
        needs.purpose   = nd(rng_);
        registry_.emplace<NeedsComponent>(entity, needs);

        registry_.emplace<ActionComponent>(entity, ActionType::IDLE);
        registry_.emplace<StressComponent>(entity, 0.0f);
        registry_.emplace<SocialComponent>(entity);
        // Opinion priors from archetype + per-agent noise
        OpinionComponent op = archetype_opinion_priors(at);
        std::uniform_real_distribution<float> op_jitter(-0.10f, 0.10f);
        for (int d = 0; d < OpinionComponent::DIMS; d++)
            op.values[d] = std::clamp(op.values[d] + op_jitter(rng_), 0.05f, 0.95f);
        registry_.emplace<OpinionComponent>(entity, op);
        InventoryComponent inv;
        inv.food = config_.initial_food_per_agent;  // bootstrap until the factory runs
        registry_.emplace<InventoryComponent>(entity, inv);
        registry_.emplace<SkillsComponent>(entity);

        const char* spawn_phrase = "";
        switch (at) {
            case Archetype::FOREMAN:       spawn_phrase = "arrived — sleeves rolled, ready to run the floor"; break;
            case Archetype::NETWORKER:     spawn_phrase = "walked in looking for people"; break;
            case Archetype::ARTISAN:       spawn_phrase = "appeared with something to make"; break;
            case Archetype::SURVIVOR:      spawn_phrase = "stumbled in — just wants to see tomorrow"; break;
            case Archetype::EXPLORER:      spawn_phrase = "arrived already looking for exits"; break;
            case Archetype::STEADY_WORKER: spawn_phrase = "showed up quietly, hands ready"; break;
            default:                       spawn_phrase = "arrived"; break;
        }
        chronicle_.log(tick_, EventType::SPAWNED, i,
            std::string(archetype_name(at)) + " — " + spawn_phrase,
            sx, sy);
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
    float to_ship = quota;

    // Phase 1: drain from Exit-adjacent Storage (radius 3).
    // This is the primary, physically-connected pipeline.
    auto exits = grid_.find_all(TileType::Exit);
    for (int radius = 1; radius <= 3 && to_ship > 0.001f; radius++) {
        for (auto [ex, ey] : exits) {
            if (to_ship <= 0.001f) break;
            for (int dy = -radius; dy <= radius && to_ship > 0.001f; dy++)
                for (int dx = -radius; dx <= radius && to_ship > 0.001f; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
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
    last_quota_fill_ = (quota > 0.0f) ? (shipped / quota) : 1.0f;

    if (shipped + 0.001f < quota) {
        factory_health_ = std::max(0.0f, factory_health_ - config_.health_decay_per_miss);
    } else {
        factory_health_ = std::min(1.0f, factory_health_ + config_.health_recovery_per_hit);
        // Surplus bonus: over-delivering heals extra.
        // Reward production capacity exceeding demand.
        float surplus_ratio = (quota > 0.0f) ? (shipped / quota - 1.0f) : 0.0f;
        if (surplus_ratio > 0.0f) {
            factory_health_ = std::min(1.0f, factory_health_ + surplus_ratio * 0.001f);
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
            // Machine on resource tile: auto-gathers from the tile it sits on.
            // FoodMachine on FoodSource → auto-gathers raw_food into stored_raw_food
            // OutputMachine on ScrapPile → auto-gathers raw_material into stored_raw_material
            if (t == TileType::Machine) {
                auto& d = grid_.data_at(x, y);
                if (d.built && d.built_on_resource) {
                    // Regen the underlying resource
                    if (d.resource_regen > 0.0f && d.resource_amount < d.resource_max) {
                        d.resource_amount = std::min(d.resource_max,
                            d.resource_amount + d.resource_regen);
                    }
                    // Auto-gather into appropriate stored resource
                    if (d.resource_amount > 0.01f) {
                        float gather = std::min(d.resource_amount, 0.15f);
                        d.resource_amount -= gather;
                        if (d.machine_type == MachineType::Food) {
                            d.stored_raw_food += gather;
                        } else {
                            d.stored_raw_material += gather;
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

        // Skip redeemed agents — they are immune to stress accumulation
        if (stress.state == StressState::REDEEMED) {
            stress.value = std::max(0.0f, stress.value - 0.01f);
            continue;
        }

        // === STRESS INPUT ===
        // Only survival needs (hunger, rest) cause significant stress.
        // Upper needs (social, expression, purpose) cause mild stress —
        // they affect mood and behavior but should NOT kill the agent.
        float stress_input = 0.0f;
        if (needs.hunger > 0.7f)     stress_input += config_.stress_high_need * (needs.hunger - 0.7f);
        if (needs.rest > 0.7f)       stress_input += config_.stress_high_need * (needs.rest - 0.7f);
        if (needs.social > 0.85f)    stress_input += config_.stress_high_need * (needs.social - 0.85f) * 0.15f;
        if (needs.expression > 0.85f) stress_input += config_.stress_high_need * (needs.expression - 0.85f) * personality.artistry * 0.15f;
        if (needs.purpose > 0.85f)   stress_input += config_.stress_high_need * (needs.purpose - 0.85f) * 0.15f;
        // Disease causes stress — being sick in the factory is miserable
        if (needs.disease > 0.3f)    stress_input += config_.stress_high_need * needs.disease * 0.5f;

        // B4: Meaning crisis — being productive but unfulfilled is tragic
        if (needs.meaning > 0.7f && personality.compliance > 0.7f) {
            stress_input += 0.003f; // "burnout from meaninglessness"
        }

        // A3: Noncompliance stress — the factory's gaze weighs on you
        // DISSOCIATED agents feel this less; HOSTILE_EUPHORIA agents ignore it
        float noncomp_mult = 1.0f;
        if (stress.state == StressState::DISSOCIATED) noncomp_mult = 0.5f;
        if (stress.state == StressState::HOSTILE_EUPHORIA) noncomp_mult = 0.0f;
        stress_input += config_.noncompliance_stress * agent.noncompliance * noncomp_mult;

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
        if (stress.state != StressState::REDEEMED) {
            StressState old_state = stress.state;
            StressState new_state;
            if (stress.value < 0.4f)       new_state = StressState::NORMAL;
            else if (stress.value < 0.7f)  new_state = StressState::DISSOCIATED;
            else if (stress.value < 0.9f)  new_state = StressState::HOSTILE_EUPHORIA;
            else                           new_state = StressState::BROKEN;
            if (new_state != old_state) {
                stress.state = new_state;
                char buf[80];
                std::snprintf(buf, sizeof(buf), "%s -> %s (stress %.2f)",
                    stress_state_name(old_state), stress_state_name(new_state), stress.value);
                chronicle(agent.id, EventType::STRESS_STATE_CHANGE, buf,
                    -1, -1, stress.value);
            }
        }

        // === HOSTILE EUPHORIA: artificial mood boost ===
        // The agent appears happy but is disconnected from reality
        if (stress.state == StressState::HOSTILE_EUPHORIA) {
            auto& soc = registry_.get<SocialComponent>(e);
            soc.mood = std::min(1.0f, soc.mood + 0.005f); // fake happiness
        }

        // === SOCIAL CONTAGION: stressed agents repel others ===
        // DISSOCIATED agents lose social connections; others avoid them
        if (stress.state == StressState::DISSOCIATED || stress.state == StressState::BROKEN) {
            auto& soc = registry_.get<SocialComponent>(e);
            soc.mood = std::max(0.0f, soc.mood - 0.003f); // internal drain
        }
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

        // Breakdown: stress kills, but not instantly.
        // BROKEN agents survive longer — they have time to sabotage or redeem.
        // Normal agents at breakdown threshold die slowly.
        if (stress.value >= config_.breakdown_threshold) {
            float death_chance = 0.005f; // 0.5% per tick (was 2% — too lethal)
            if (stress.state == StressState::BROKEN) death_chance = 0.003f; // BROKEN agents linger
            if (stress.state == StressState::REDEEMED) death_chance = 0.0f;  // Redeemed are immune
            std::uniform_real_distribution<float> roll(0.0f, 1.0f);
            if (roll(rng_) < death_chance) {
                agent.alive = false;
                agent.cause_of_death = "breakdown";
                emit_log(agent.id, "had a BREAKDOWN (stress=" + ff2(stress.value) + ")");
                newly_dead.push_back(agent.id);
            }
        }

        // Factory collapse: when factory_health == 0, the crumbling factory
        // increases stress on all agents but does NOT kill them directly.
        // The factory is the environment, not the executioner.
        // Agents die from hunger, stress breakdown, or exhaustion — not the building.
        if (factory_health_ <= 0.0f) {
            stress.value = std::min(1.0f, stress.value + 0.002f);  // environmental dread
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

// ============================================================
// A2: SYSTEM: Factory Restructure (adversarial best-response)
// ============================================================
// The factory reconfigures itself periodically. Unlike the previous
// uniform-random implementation, this version makes the factory an
// EVALUATOR (doc/adversarial_utility_agents.md): it scores every candidate
// target by strategic value (how much output/throughput it would lose) and
// by proximity to the largest faction, then softmax-selects. This兑现 the
// comments that previously promised but never implemented faction targeting.
//
// adversary_intensity α ∈ [0,1] blends the strategic score with uniform
// randomness: α=0 reproduces the old random baseline exactly; α=1 is pure
// best-response; intermediate values sit at the "edge of chaos".
//
// Audit: every attack logs (target, score, faction_proximity). When the
// faction bonus was the dominant factor, a FACTORY_TARGETED_FACTION event
// is emitted so the run can be distinguished from random post-hoc.

void Simulation::system_factory_restructure() {
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
        if (d.machine_type == MachineType::Food && last_quota_fill_ < 0.5f) s += 0.75f;
        candidates.push_back({mx, my, TargetKind::MACHINE, s});
    }
    for (auto [sx, sy] : grid_.find_all(TileType::Storage)) {
        const auto& d = grid_.data_at(sx, sy);
        if (d.stored_output <= 0.01f) continue;
        candidates.push_back({sx, sy, TargetKind::STORAGE, d.stored_output});
    }

    if (candidates.empty()) return;

    // ---------------------------------------------------------------
    // 2. Faction heatmap — find the largest faction's centroid, then
    //    score each candidate by how many of its members sit within
    //    Manhattan ≤ 4. This is the "factory targets resistance" signal.
    // ---------------------------------------------------------------
    // Pick the largest faction id (recompute cheaply; factions update every
    // 50 ticks so this is at most slightly stale).
    std::map<int,int> faction_size;
    auto agent_view = registry_.view<const AgentComponent>();
    for (auto e : agent_view) {
        const auto& ag = registry_.get<AgentComponent>(e);
        if (ag.alive && ag.faction_id >= 0) faction_size[ag.faction_id]++;
    }
    int largest_faction = -1;
    int largest_size = 0;
    for (auto& [fid, sz] : faction_size) {
        if (sz > largest_size) { largest_size = sz; largest_faction = fid; }
    }

    // Gather positions of the largest faction's members (for proximity scoring).
    std::vector<std::pair<int,int>> faction_positions;
    if (largest_faction >= 0) {
        for (auto e : agent_view) {
            const auto& ag = registry_.get<AgentComponent>(e);
            if (ag.alive && ag.faction_id == largest_faction) {
                const auto& p = registry_.get<PositionComponent>(e);
                faction_positions.push_back({p.x, p.y});
            }
        }
    }

    auto faction_proximity = [&](int x, int y) -> float {
        if (faction_positions.empty()) return 0.0f;
        int near = 0;
        for (auto& [fx, fy] : faction_positions)
            if (std::abs(fx - x) + std::abs(fy - y) <= 4) near++;
        return std::min(1.0f, (float)near / 3.0f);  // saturates at 3 nearby members
    };

    // ---------------------------------------------------------------
    // 3. Score each candidate: blend strategic value + faction bonus,
    //    then interpolate with uniform-random via adversary_intensity α.
    // ---------------------------------------------------------------
    float alpha = std::clamp(config_.adversary_intensity, 0.0f, 1.0f);
    std::vector<float> scores(candidates.size());
    float max_score = 0.0f;
    for (size_t i = 0; i < candidates.size(); i++) {
        float strat = config_.strategic_weight * candidates[i].strategic;
        float fac = config_.faction_target_bonus * faction_proximity(candidates[i].x, candidates[i].y);
        float adversarial = strat + fac;
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

    // ---------------------------------------------------------------
    // 5. Apply the attack + audit log. Flag faction-targeted strikes.
    // ---------------------------------------------------------------
    auto& target = candidates[chosen];
    float fac_score = config_.faction_target_bonus * faction_proximity(target.x, target.y);
    float strat_score = config_.strategic_weight * target.strategic;
    bool faction_driven = (fac_score > 0.0f && fac_score >= strat_score * 0.5f);

    switch (target.kind) {
        case TargetKind::CONVEYOR: {
            auto& d = grid_.data_at(target.x, target.y);
            int dir = static_cast<int>(d.conveyor_dir);
            d.conveyor_dir = static_cast<ConveyorDir>((dir + 2) % 4);
            std::string msg = "FACTORY restructured: conveyor at (" +
                std::to_string(target.x) + "," + std::to_string(target.y) +
                ") reversed [strat=" + ff2(strat_score) +
                " fac=" + ff2(fac_score) + "]";
            if (faction_driven) {
                msg += " — targeted faction";
                emit_log(-1, msg);
                restructures_targeting_factions_++;
            } else {
                emit_log(-1, msg);
            }
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
                ") damaged [strat=" + ff2(strat_score) +
                " fac=" + ff2(fac_score) + "]";
            if (faction_driven) {
                msg += " — targeted faction";
                emit_log(-1, msg);
                restructures_targeting_factions_++;
            } else {
                emit_log(-1, msg);
            }
            total_restructures_++;
            break;
        }
        case TargetKind::STORAGE: {
            auto& d = grid_.data_at(target.x, target.y);
            float confiscated = d.stored_output * 0.5f;
            d.stored_output -= confiscated;
            std::string msg = "FACTORY confiscated " + ff2(confiscated) +
                " output from storage at (" +
                std::to_string(target.x) + "," + std::to_string(target.y) +
                ") [strat=" + ff2(strat_score) +
                " fac=" + ff2(fac_score) + "]";
            if (faction_driven) {
                msg += " — targeted faction";
                emit_log(-1, msg);
                restructures_targeting_factions_++;
            } else {
                emit_log(-1, msg);
            }
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

        // Boost mood of nearby agents
        auto alive_view = registry_.view<PositionComponent, SocialComponent, const AgentComponent>();
        for (auto e : alive_view) {
            if (!registry_.get<AgentComponent>(e).alive) continue;
            auto& p = registry_.get<PositionComponent>(e);
            int d = std::abs(p.x - apos.x) + std::abs(p.y - apos.y);
            if (d <= 2) {
                auto& soc = registry_.get<SocialComponent>(e);
                soc.mood = std::min(1.0f, soc.mood + aart.strength * 0.03f);
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
                     std::to_string(hx) + "," + std::to_string(hy) + ")");
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
// B3: SYSTEM: Faction Formation
// ============================================================
// A faction forms when 3+ agents all have mutual trust > 0.4.
// Faction members get collaboration bonus and noncompliance shield.
// Factory may target large factions during restructure.

void Simulation::system_faction_formation() {
    // Run every 50 ticks (expensive)
    if (tick_ % 50 != 0) return;

    auto alive = alive_agents();
    int n = (int)alive.size();
    if (n < 3) return;

    // Snapshot old faction assignments for delta detection
    std::vector<int> old_faction(n, -1);
    for (int i = 0; i < n; i++) {
        old_faction[i] = registry_.get<AgentComponent>(alive[i]).faction_id;
    }

    // Reset faction IDs
    for (auto e : alive) {
        registry_.get<AgentComponent>(e).faction_id = -1;
    }

    // Find trust+opinion clusters: connected components where edges have
    // mutual trust > 0.3 AND opinion distance < 0.5 (bounded confidence).
    int next_faction = 0;
    std::vector<int> component(n, -1);

    for (int i = 0; i < n; i++) {
        if (component[i] >= 0) continue;

        auto& op_i = registry_.get<OpinionComponent>(alive[i]);

        // BFS: find all agents reachable via mutual trust + opinion proximity
        std::vector<int> cluster;
        std::vector<int> queue;
        queue.push_back(i);
        component[i] = next_faction;
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

                // Must have mutual trust > 0.3 and familiarity > 0.2
                bool trust_ok = rel_ab.trust > 0.3f && rel_ba.trust > 0.3f
                             && rel_ab.familiarity > 0.2f;
                if (!trust_ok) continue;

                // Opinion distance must be below threshold (bounded confidence)
                auto& op_j = registry_.get<OpinionComponent>(alive[j]);
                float op_dist = SocialFabric::opinion_distance(op_i, op_j);
                if (op_dist > 0.5f) continue;

                component[j] = next_faction;
                cluster.push_back(j);
                queue.push_back(j);
            }
        }

        // If cluster has 3+ members, it's a faction
        if ((int)cluster.size() >= 3) {
            for (int idx : cluster) {
                registry_.get<AgentComponent>(alive[idx]).faction_id = next_faction;
            }
            next_faction++;
        }
    }

    factions_formed_ = next_faction;

    // Count members per new faction
    std::vector<int> faction_sizes(next_faction, 0);
    for (int i = 0; i < n; i++) {
        int fid = registry_.get<AgentComponent>(alive[i]).faction_id;
        if (fid >= 0 && fid < next_faction) faction_sizes[fid]++;
    }

    // Emit chronicle events for faction changes
    std::set<int> formed_logged;
    for (int i = 0; i < n; i++) {
        int new_fid = registry_.get<AgentComponent>(alive[i]).faction_id;
        int old_fid = old_faction[i];

        if (new_fid >= 0 && old_fid < 0) {
            int aid = registry_.get<AgentComponent>(alive[i]).id;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "joined faction %d", new_fid);
            chronicle(aid, EventType::FACTION_JOINED, buf, -1, -1, 0.0f, new_fid);

            if (formed_logged.insert(new_fid).second) {
                char fbuf[64];
                std::snprintf(fbuf, sizeof(fbuf),
                    "faction %d formed (%d members)", new_fid, faction_sizes[new_fid]);
                chronicle_.log(tick_, EventType::FACTION_FORMED, -1, fbuf);
            }
        } else if (old_fid >= 0 && new_fid < 0) {
            int aid = registry_.get<AgentComponent>(alive[i]).id;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "left faction %d", old_fid);
            chronicle(aid, EventType::FACTION_LEFT, buf, -1, -1, 0.0f, old_fid);
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
            "the first machine is built — the factory stirs");
    }
    if (!first_death_done_ && chronicle_.count_of_type(EventType::DIED_STARVATION) +
                              chronicle_.count_of_type(EventType::DIED_EXHAUSTION) +
                              chronicle_.count_of_type(EventType::DIED_COLLAPSE) >= 1) {
        first_death_done_ = true;
        chronicle_.log(tick_, EventType::FIRST_DEATH, -1,
            "the first agent dies — innocence lost");
    }
    if (!first_sabotage_done_ && sabotages_total_ >= 1) {
        first_sabotage_done_ = true;
        chronicle_.log(tick_, EventType::FIRST_SABOTAGE, -1,
            "the first act of sabotage — resistance begins");
    }
    if (!first_faction_done_ && factions_formed_ >= 1) {
        first_faction_done_ = true;
        chronicle_.log(tick_, EventType::FIRST_FACTION, -1,
            "the first faction forms — unity fractures");
    }
    if (!first_artifact_done_ && artifacts_created_ >= 1) {
        first_artifact_done_ = true;
        chronicle_.log(tick_, EventType::FIRST_ARTIFACT, -1,
            "the first artifact is created — beauty amid machinery");
    }

    // Population milestones
    if (alive > 0 && alive % 5 == 0 && alive != last_population_milestone_) {
        last_population_milestone_ = alive;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d agents survive", alive);
        chronicle_.log(tick_, EventType::POPULATION_MILESTONE, -1, buf);
    }

    // Crisis detection: factory health < 0.25 and hasn't fired in 200 ticks
    if (factory_health_ < 0.25f && tick_ - last_crisis_tick_ >= 200) {
        last_crisis_tick_ = tick_;
        char buf[64];
        std::snprintf(buf, sizeof(buf),
            "factory health critical (%.0f%%) — the machine falters", factory_health_ * 100);
        chronicle_.log(tick_, EventType::CRISIS_PERIOD, -1, buf,
            -1, -1, factory_health_);
    }

    // Quota milestones
    float qf = last_quota_fill_;
    if (qf >= 1.0f && last_quota_milestone_ < 1.0f) {
        chronicle_.log(tick_, EventType::QUOTA_MILESTONE, -1,
            "quota met — the factory is satisfied");
    }
    last_quota_milestone_ = qf;
}
