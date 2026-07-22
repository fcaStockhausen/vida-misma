#pragma once

#include "components.h"
#include "config.h"
#include "grid.h"
#include "social.h"
#include "chronicle.h"
#include "textgen.h"
#include "production.h"
#include "metrics.h"
#include "director.h"
#include <entt/entt.hpp>
#include <random>
#include <vector>
#include <string>
#include <deque>
#include <set>
#include <cstdio>

// ============================================================
// Event log (ring buffer)
// ============================================================
struct LogEntry {
    int tick;
    int agent_id;
    std::string text;
};

static constexpr size_t MAX_LOG = 200;

class Simulation {
public:
    static constexpr int OBSERVATION_RADIUS = 12;

    struct PlaceChoice {
        int x = -1;
        int y = -1;
        float score = 0.0f;
    };

    explicit Simulation(const Config& cfg);

    int tick() const { return tick_; }
    float factory_health() const { return factory_health_; }
    const Grid& grid() const { return grid_; }
    SocialFabric& social() { return social_; }
    const SocialFabric& social() const { return social_; }
    Chronicle& chronicle() { return chronicle_; }
    const Chronicle& chronicle() const { return chronicle_; }
    entt::registry& registry() { return registry_; }
    const Config& config() const { return config_; }
    Grid& grid_mut() { return grid_; }

    // Production stats
    float total_food_produced() const { return total_food_produced_; }
    float total_output_produced() const { return total_output_produced_; }
    float total_raw_gathered() const { return total_raw_gathered_; }
    int   total_machines_built() const { return total_machines_built_; }

    // External pressure
    float total_food_shipped() const { return total_food_shipped_; }
    int   total_machines_broken() const { return total_machines_broken_; }
    float last_quota_fill() const { return last_quota_fill_; }
    float external_support() const { return external_support_; }
    float external_supply_factor() const { return external_supply_factor_; }
    const ColonyProduction& colony_production() const { return colony_prod_; }
    float current_quota() const { return current_quota_per_tick_; }
    int   total_restructures() const { return total_restructures_; }
    int   foreman_reports() const { return foreman_reports_; }
    int   artifacts_created() const { return artifacts_created_; }
    int   artifacts_active() const { return artifacts_active_; }
    int   hidden_spaces_found() const { return hidden_spaces_found_; }
    int   hidden_spaces_sealed() const { return hidden_spaces_sealed_; }
    int   space_closures() const { return space_closures_; }
    int   communities_detected() const { return communities_detected_; }
    int   sabotages_total() const { return sabotages_total_; }
    int   post_sabotage_pauses() const { return post_sabotage_pauses_; }
    int   suicides_total() const { return suicides_total_; }
    int   ever_created() const { return next_agent_id_; }
    int   peak_population() const { return peak_population_; }
    int   arrivals_admitted() const { return arrivals_admitted_; }
    int   arrival_attempts() const { return arrival_attempts_; }
    int   arrivals_blocked_capacity() const { return arrivals_blocked_capacity_; }
    int   births_total() const { return births_total_; }
    int   births_blocked_capacity() const { return births_blocked_capacity_; }
    const SimulationMetrics& metrics() const { return metrics_; }

    // Event log
    const std::deque<LogEntry>& log() const { return log_; }

    int alive_count() const;
    int built_machine_count() const;
    int count_built_machines(MachineType type) const;
    int built_conveyor_count() const;
    float total_storage_food() const;
    float total_storage_output() const;
    float total_storage_constr_mat() const;
    float total_inventory_constr_mat() const;  // c_mat carried by alive agents
    float total_inventory_raw_material() const;
    float total_source_resource(ResourceType resource) const;
    std::vector<entt::entity> alive_agents() const;

    void advance();
    void set_output_shipping_enabled(bool enabled) { output_shipping_enabled_ = enabled; }
    DirectorResult validate_director_command(const DirectorCommand& command) const;
    DirectorResult apply_director_command(const DirectorCommand& command);
    DirectorResult replay_director_event(const DirectorEvent& event);
    const std::vector<DirectorEvent>& director_log() const { return director_log_; }

private:
    entt::registry registry_;
    Config config_;
    Grid grid_;
    SocialFabric social_;
    Chronicle chronicle_;
    ColonyProduction colony_prod_;
    std::mt19937 rng_;
    SimulationMetrics metrics_;
    std::vector<uint8_t> metric_death_recorded_;
    std::set<uint64_t> previous_spatial_pairs_;
    std::set<uint64_t> previous_community_pairs_;
    bool have_spatial_sample_ = false;
    bool have_community_sample_ = false;
    std::vector<int> pending_grief_deaths_;
    int next_agent_id_ = 0;
    int peak_population_ = 0;
    int arrival_attempts_ = 0;
    int arrivals_admitted_ = 0;
    int arrivals_blocked_capacity_ = 0;
    int births_total_ = 0;
    int births_blocked_capacity_ = 0;

    // Narrative tracking
    bool first_build_done_ = false;
    bool first_death_done_ = false;
    bool first_sabotage_done_ = false;
    bool first_community_done_ = false;
    bool first_artifact_done_ = false;
    int last_population_milestone_ = 0;
    int last_crisis_tick_ = -100;
    float last_quota_milestone_ = 0.0f;

    int tick_;
    float factory_health_;

    // Production stats
    float total_food_produced_;
    float total_output_produced_;
    float total_raw_gathered_;
    int   total_machines_built_;

    // External pressure stats
    float total_food_shipped_   = 0.0f;
    int   total_machines_broken_ = 0;
    float last_quota_fill_       = 0.0f;
    float current_quota_per_tick_ = 0.0f;
    float external_support_        = 1.0f;
    float external_supply_factor_  = 1.0f;
    bool  output_shipping_enabled_ = true;
    bool  quota_manually_set_       = false;
    int   total_restructures_     = 0;
    int   foreman_reports_        = 0;
    int   artifacts_created_      = 0;
    int   artifacts_active_       = 0;
    int   hidden_spaces_found_    = 0;
    int   hidden_spaces_sealed_   = 0;
    int   space_closures_         = 0;
    int   communities_detected_   = 0;
    int   sabotages_total_        = 0;
    int   post_sabotage_pauses_   = 0;
    int   suicides_total_         = 0;

    // Event log
    std::deque<LogEntry> log_;
    std::vector<DirectorEvent> director_log_;

    DirectorResult apply_director_command_unlogged(const DirectorCommand& command);

    void emit_log(int agent_id, const std::string& text, EventType type) {
        log_.push_back({tick_, agent_id, text});
        if (log_.size() > MAX_LOG) log_.pop_front();
        chronicle_.log(tick_, type, agent_id, text);
    }

    // Rich chronicle entry (for events that need position/value/ref data)
    void chronicle(int agent_id, EventType type, const std::string& text,
                   int x = -1, int y = -1, float value = 0.0f, int ref_id = -1)
    {
        chronicle_.log(tick_, type, agent_id, text, x, y, value, ref_id);
    }

    static std::string ff2(float v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%.2f", v);
        return b;
    }

    static const char* action_name(ActionType a) {
        switch (a) {
            case ActionType::GATHER:    return "GATHER";
            case ActionType::BUILD:     return "BUILD";
            case ActionType::WORK:      return "WORK";
            case ActionType::EAT:       return "EAT";
            case ActionType::REST:      return "REST";
            case ActionType::SOCIALIZE: return "SOCIALIZE";
            case ActionType::CREATE:    return "CREATE";
            case ActionType::EXPLORE:   return "EXPLORE";
            case ActionType::GET_FOOD:  return "GET_FOOD";
            case ActionType::MAINTAIN:  return "MAINTAIN";
            case ActionType::DISMANTLE: return "DISMANTLE";
            case ActionType::SABOTAGE:  return "SABOTAGE";
            case ActionType::IDLE:      return "IDLE";
            default:                    return "?";
        }
    }

    // --- Systems (one file each in sim_*.cpp) ---
    void spawn_initial_agents();
    entt::entity spawn_agent(int x, int y, AgentOrigin origin,
                             const PersonalityComponent& personality,
                             const OpinionComponent& opinion,
                             const NeedsComponent& needs,
                             const InventoryComponent& inventory,
                             int age_at_entry, int parent_a = -1,
                             int parent_b = -1, int generation = 0);
    void ensure_agent_metrics(int agent_id);
    uint64_t lifecycle_hash(uint64_t salt, int a = 0, int b = 0) const;
    float lifecycle_unit(uint64_t salt, int a = 0, int b = 0) const;
    int lifecycle_age(const LifecycleComponent& lifecycle) const;
    void system_lifecycle();
    void system_regen_resources();
    void system_decay_needs();
    void system_compute_utility();   // sim_utility.cpp
    void system_find_targets();       // sim_targets.cpp
    bool action_feasible(entt::entity entity, ActionType action) const;
    bool work_target_feasible(entt::entity entity, int x, int y) const;
    std::pair<int, int> find_feasible_work_target(entt::entity entity) const;
    void system_move_to_targets();    // sim_movement.cpp
    void system_execute_actions();    // sim_execute.cpp
    void system_ship_out_food();      // external quota fulfillment (simulation.cpp)
    void system_factory_deterioration(); // health/machine-break (simulation.cpp)
    void system_update_factory_condition(); // aggregate mechanical condition
    void system_factory_restructure_legacy(); // strategic historical policy
    void system_factory_restructure_indifferent(); // physical canonical policy
    void system_artifact_effects();      // artifact mood boost + decay
    void system_hidden_space_exposure(); // factory seals overused hidden spaces
    void system_space_overcapacity();    // anonymous physical-capacity closure
    void system_community_detection();   // observed relationship-graph components
    void system_social_learning();       // observable copresence and collaboration
    void system_spatial_learning();      // personal place outcomes and affinity
    void system_record_emergence_metrics();
    PlaceChoice find_preferred_place(entt::entity entity, ActionType action) const;
    void system_update_stress();
    void system_check_deaths();
    bool kill_agent(entt::entity entity, EventType death_type,
                    const std::string& text);
    void apply_pending_grief();
    void system_check_dismantle_penalties();
    void system_chronicle_narrative();    // detect milestones and narrative events
    void record_metric_deaths();

    // --- Conveyor system (sim_conveyor.cpp) ---
    void system_conveyor_transport();

    // --- Movement helpers (sim_movement.cpp) ---
    void move_toward(PositionComponent& pos, int tx, int ty);
    void random_move(PositionComponent& pos, std::mt19937& random);

    // --- Storage helpers (sim_execute.cpp) ---
    bool  has_adjacent_storage_with_food(entt::entity e) const;
    float get_adjacent_raw_food(int px, int py) const;
    void  consume_adjacent_raw_food(int px, int py, float amount);
    float deposit_to_adjacent_storage(int px, int py, ResourceType type, float amount); // returns amount deposited
    float deposit_to_adjacent_conveyor(int px, int py, ResourceType type, float amount); // returns amount deposited
    float take_from_adjacent_storage(int px, int py);
    // Pulls *food* from any 8-adjacent Storage (including current tile) up to `max_amount`.
    // Returns the amount actually pulled. Used by GET_FOOD and by EAT auto-topup.
    float pull_food_from_adjacent_storage(int px, int py, float max_amount);
    // Pulls *raw_material* from any 8-adjacent Storage up to `max_amount`.
    float pull_raw_material_from_adjacent_storage(int px, int py, float max_amount);
    float pull_construction_material_from_adjacent_storage(int px, int py, float max_amount);
    // Tile predicate: is there a Machine within Manhattan <= 1 of (px, py)?
    bool  is_near_machine(int px, int py) const;
};
