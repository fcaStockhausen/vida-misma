#pragma once

#include "components.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

enum class MetricDeathCause : uint8_t {
    Starvation = 0,
    Exhaustion,
    Breakdown,
    Suicide,
    Natural,
    Other,
    Count,
};

inline constexpr size_t METRIC_ACTION_COUNT = static_cast<size_t>(ActionType::COUNT);
inline constexpr size_t METRIC_RESOURCE_COUNT = 5;
inline constexpr size_t METRIC_MACHINE_COUNT = 3;
inline constexpr size_t METRIC_DEATH_COUNT = static_cast<size_t>(MetricDeathCause::Count);

inline constexpr size_t metric_index(ActionType value) {
    return static_cast<size_t>(value);
}

inline constexpr size_t metric_index(ResourceType value) {
    return static_cast<size_t>(value);
}

inline constexpr size_t metric_index(MachineType value) {
    return static_cast<size_t>(value);
}

inline constexpr size_t metric_index(MetricDeathCause value) {
    return static_cast<size_t>(value);
}

inline const char* metric_action_name(ActionType action) {
    switch (action) {
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
        default:                    return "UNKNOWN";
    }
}

inline const char* metric_resource_name(ResourceType resource) {
    switch (resource) {
        case ResourceType::RAW_FOOD:              return "RAW_FOOD";
        case ResourceType::RAW_MATERIAL:          return "RAW_MATERIAL";
        case ResourceType::FOOD:                  return "FOOD";
        case ResourceType::CONSTRUCTION_MATERIAL: return "CONSTRUCTION_MATERIAL";
        case ResourceType::OUTPUT:                return "OUTPUT";
        default:                                  return "UNKNOWN";
    }
}

inline const char* metric_machine_name(MachineType machine) {
    switch (machine) {
        case MachineType::Food:      return "FOOD";
        case MachineType::Materials: return "MATERIALS";
        case MachineType::Output:    return "OUTPUT";
        default:                     return "UNKNOWN";
    }
}

inline const char* metric_death_name(MetricDeathCause cause) {
    switch (cause) {
        case MetricDeathCause::Starvation: return "starvation";
        case MetricDeathCause::Exhaustion: return "exhaustion";
        case MetricDeathCause::Breakdown:  return "breakdown";
        case MetricDeathCause::Suicide:    return "suicide";
        case MetricDeathCause::Natural:    return "natural";
        case MetricDeathCause::Other:      return "other";
        default:                           return "unknown";
    }
}

struct SimulationMetrics {
    uint64_t ticks_advanced = 0;

    std::array<uint64_t, METRIC_ACTION_COUNT> action_selected{};
    std::array<uint64_t, METRIC_ACTION_COUNT> target_lookups{};
    std::array<uint64_t, METRIC_ACTION_COUNT> target_failures{};
    std::array<uint64_t, METRIC_ACTION_COUNT> plan_invalidations{};
    std::array<uint64_t, METRIC_ACTION_COUNT> target_reached{};
    std::array<uint64_t, METRIC_ACTION_COUNT> action_executed{};
    std::array<uint64_t, METRIC_ACTION_COUNT> utility_samples{};
    std::array<uint64_t, METRIC_ACTION_COUNT> feasible_samples{};
    std::array<double, METRIC_ACTION_COUNT> utility_self_sum{};
    std::array<double, METRIC_ACTION_COUNT> utility_factory_sum{};
    std::array<double, METRIC_ACTION_COUNT> utility_cost_sum{};
    std::array<double, METRIC_ACTION_COUNT> utility_risk_sum{};
    std::array<double, METRIC_ACTION_COUNT> utility_final_sum{};

    std::array<uint64_t, METRIC_DEATH_COUNT> deaths{};
    std::array<uint64_t, METRIC_MACHINE_COUNT> machines_built{};
    std::array<uint64_t, METRIC_MACHINE_COUNT> initial_machines_active{};
    uint64_t initial_conveyors_active = 0;
    uint64_t initial_storages_active = 0;
    uint64_t initial_exit_connected_outputs = 0;
    bool initial_minimum_chain_present = false;

    std::array<double, METRIC_RESOURCE_COUNT> resources_regenerated{};
    std::array<double, METRIC_RESOURCE_COUNT> regeneration_base{};
    std::array<double, METRIC_RESOURCE_COUNT> regeneration_requested{};
    std::array<double, METRIC_RESOURCE_COUNT> resources_produced{};
    std::array<double, METRIC_RESOURCE_COUNT> resources_consumed{};
    std::array<double, METRIC_RESOURCE_COUNT> resources_lost{};

    double quota_demand = 0.0;
    double output_shipped = 0.0;
    double output_hauled = 0.0;
    double external_support_sum = 0.0;
    double external_supply_factor_sum = 0.0;
    uint64_t external_support_updates = 0;
    uint64_t shipping_blocked_ticks = 0;

    // Per-agent evidence. Productive ticks and food flows remain separate units.
    std::vector<std::array<uint64_t, METRIC_ACTION_COUNT>> agent_action_ticks;
    std::vector<uint64_t> agent_productive_effect_ticks;
    std::vector<double> agent_food_shared_given;
    std::vector<double> agent_food_received;
    std::vector<double> agent_food_consumed;

    double spatial_persistence_sum = 0.0;
    uint64_t spatial_persistence_samples = 0;
    double personality_distance_delta_sum = 0.0;
    uint64_t personality_distance_samples = 0;
    double social_modularity_sum = 0.0;
    uint64_t social_modularity_samples = 0;
    double community_stability_sum = 0.0;
    uint64_t community_stability_samples = 0;

    bool operator==(const SimulationMetrics&) const = default;
};
