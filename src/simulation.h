#pragma once

#include "components.h"
#include "config.h"
#include "grid.h"
#include "social.h"
#include "chronicle.h"
#include <entt/entt.hpp>
#include <random>
#include <vector>
#include <string>
#include <deque>
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
    float last_quota_fill() const { return last_quota_fill_; } // 0..1, last tick's quota completion
    float current_quota() const { return current_quota_per_tick_; }
    int   total_restructures() const { return total_restructures_; }
    int   artifacts_created() const { return artifacts_created_; }
    int   artifacts_active() const { return artifacts_active_; }
    int   hidden_spaces_found() const { return hidden_spaces_found_; }
    int   hidden_spaces_sealed() const { return hidden_spaces_sealed_; }
    int   factions_formed() const { return factions_formed_; }
    int   sabotages_total() const { return sabotages_total_; }
    int   redemptions_total() const { return redemptions_total_; }
    int   suicides_total() const { return suicides_total_; }

    // Event log
    const std::deque<LogEntry>& log() const { return log_; }

    int alive_count() const;
    int built_machine_count() const;
    float total_storage_food() const;
    float total_storage_output() const;
    float total_storage_constr_mat() const;
    std::vector<entt::entity> alive_agents() const;

    void advance();

private:
    entt::registry registry_;
    Config config_;
    Grid grid_;
    SocialFabric social_;
    Chronicle chronicle_;
    std::mt19937 rng_;

    // Narrative tracking
    bool first_build_done_ = false;
    bool first_death_done_ = false;
    bool first_sabotage_done_ = false;
    bool first_faction_done_ = false;
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
    int   total_restructures_     = 0;
    int   artifacts_created_      = 0;
    int   artifacts_active_       = 0;
    int   hidden_spaces_found_    = 0;
    int   hidden_spaces_sealed_   = 0;
    int   factions_formed_        = 0;
    int   sabotages_total_        = 0;
    int   redemptions_total_      = 0;
    int   suicides_total_         = 0;

    // Event log
    std::deque<LogEntry> log_;

    void emit_log(int agent_id, const std::string& text) {
        log_.push_back({tick_, agent_id, text});
        if (log_.size() > MAX_LOG) log_.pop_front();

        // Auto-generate chronicle entry from text keywords
        chronicle_.log(tick_, classify_event(text), agent_id, text);
    }

    // Rich chronicle entry (for events that need position/value/ref data)
    void chronicle(int agent_id, EventType type, const std::string& text,
                   int x = -1, int y = -1, float value = 0.0f, int ref_id = -1)
    {
        chronicle_.log(tick_, type, agent_id, text, x, y, value, ref_id);
    }

    static EventType classify_event(const std::string& text) {
        if (text.find("DIED of starvation") != std::string::npos)  return EventType::DIED_STARVATION;
        if (text.find("DIED of exhaustion") != std::string::npos)  return EventType::DIED_EXHAUSTION;
        if (text.find("BREAKDOWN") != std::string::npos)           return EventType::BREAKDOWN;
        if (text.find("DIED in factory") != std::string::npos)     return EventType::DIED_COLLAPSE;
        if (text.find("SUICIDE") != std::string::npos)             return EventType::DIED_SUICIDE;
        if (text.find("REDEEMED") != std::string::npos)            return EventType::REDEMPTION;
        if (text.find("SABOTAGED") != std::string::npos)           return EventType::SABOTAGE;
        if (text.find("shared") != std::string::npos)              return EventType::FOOD_SHARED;
        if (text.find("BUILT a machine") != std::string::npos)     return EventType::BUILT_MACHINE;
        if (text.find("BUILT a conveyor") != std::string::npos)    return EventType::BUILT_CONVEYOR;
        if (text.find("BUILT an eating") != std::string::npos)     return EventType::BUILT_EATING_ZONE;
        if (text.find("worked") != std::string::npos)              return EventType::WORK_COMPLETED;
        if (text.find("gathered") != std::string::npos)            return EventType::GATHERED;
        if (text.find("salvaged") != std::string::npos)            return EventType::GATHERED;
        if (text.find("maintained") != std::string::npos)          return EventType::MAINTAINED;
        if (text.find("DISMANTLED") != std::string::npos)          return EventType::DISMANTLED;
        if (text.find("MACHINE") != std::string::npos)             return EventType::MACHINE_ACTIVATED;
        if (text.find("restructured") != std::string::npos)        return EventType::FACTORY_RESTRUCTURE;
        if (text.find("confiscated") != std::string::npos)         return EventType::FACTORY_CONFISCATED;
        if (text.find("sealed") != std::string::npos)              return EventType::FACTORY_SEALED_SPACE;
        if (text.find("hidden space") != std::string::npos)        return EventType::HIDDEN_SPACE_FOUND;
        return EventType::COUNT;  // unclassified
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
            case ActionType::IDLE:      return "IDLE";
            default:                    return "?";
        }
    }

    // --- Systems (one file each in sim_*.cpp) ---
    void spawn_initial_agents();
    void system_regen_resources();
    void system_decay_needs();
    void system_compute_utility();   // sim_utility.cpp
    void system_find_targets();       // sim_targets.cpp
    void system_move_to_targets();    // sim_movement.cpp
    void system_execute_actions();    // sim_execute.cpp
    void system_ship_out_food();      // external quota fulfillment (simulation.cpp)
    void system_factory_deterioration(); // health/machine-break (simulation.cpp)
    void system_factory_restructure();   // periodic factory reconfiguration
    void system_artifact_effects();      // artifact mood boost + decay
    void system_hidden_space_exposure(); // factory seals overused hidden spaces
    void system_faction_formation();     // trust clusters become factions
    void system_update_stress();
    void system_check_deaths();
    void system_check_dismantle_penalties();
    void system_chronicle_narrative();    // detect milestones and narrative events

    // --- Conveyor system (sim_conveyor.cpp) ---
    void system_conveyor_transport();

    // --- Movement helpers (sim_movement.cpp) ---
    void move_toward(PositionComponent& pos, int tx, int ty);
    void random_move(PositionComponent& pos);

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
