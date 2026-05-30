#pragma once

#include "components.h"
#include "config.h"
#include "grid.h"
#include "social.h"
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
    entt::registry& registry() { return registry_; }
    const Config& config() const { return config_; }
    Grid& grid_mut() { return grid_; }

    // Production stats
    float total_food_produced() const { return total_food_produced_; }
    float total_raw_gathered() const { return total_raw_gathered_; }
    int   total_machines_built() const { return total_machines_built_; }

    // External pressure
    float total_food_shipped() const { return total_food_shipped_; }
    int   total_machines_broken() const { return total_machines_broken_; }
    float last_quota_fill() const { return last_quota_fill_; } // 0..1, last tick's quota completion

    // Event log
    const std::deque<LogEntry>& log() const { return log_; }

    int alive_count() const;
    int built_machine_count() const;
    float total_storage_food() const;
    std::vector<entt::entity> alive_agents() const;

    void advance();

private:
    entt::registry registry_;
    Config config_;
    Grid grid_;
    SocialFabric social_;
    std::mt19937 rng_;
    int tick_;
    float factory_health_;

    // Production stats
    float total_food_produced_;
    float total_raw_gathered_;
    int   total_machines_built_;

    // External pressure stats
    float total_food_shipped_   = 0.0f;
    int   total_machines_broken_ = 0;
    float last_quota_fill_       = 0.0f;

    // Event log
    std::deque<LogEntry> log_;

    void emit_log(int agent_id, const std::string& text) {
        log_.push_back({tick_, agent_id, text});
        if (log_.size() > MAX_LOG) log_.pop_front();
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
    void system_update_stress();
    void system_check_deaths();

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
    // Tile predicate: is there a Machine within Manhattan ≤ 1 of (px, py)?
    bool  is_near_machine(int px, int py) const;
};
