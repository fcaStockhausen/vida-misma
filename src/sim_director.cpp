#include "simulation.h"

#include "recipes.h"

#include <cmath>
#include <string>
#include <type_traits>

namespace {

bool in_bounds(const Grid& grid, int x, int y) {
    return x >= 0 && x < grid.width() && y >= 0 && y < grid.height();
}

const char* machine_name(MachineType type) {
    switch (type) {
        case MachineType::Food: return "food machine";
        case MachineType::Materials: return "materials machine";
        case MachineType::Output: return "output machine";
    }
    return "machine";
}

void invalidate_agent_paths(entt::registry& registry) {
    auto actions = registry.view<ActionComponent>();
    for (auto entity : actions) registry.get<ActionComponent>(entity).path_cache = {};
}

}  // namespace

DirectorResult Simulation::validate_director_command(const DirectorCommand& command) const {
    return std::visit([&](const auto& value) -> DirectorResult {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, DirectorSetQuota>) {
            if (config_.director_mode == DirectorMode::CALM)
                return {DirectorError::DisabledInCalm, 0};
            if (!std::isfinite(value.quota_per_tick) || value.quota_per_tick < 0.0f)
                return {DirectorError::InvalidValue, 0};
            return {DirectorError::None, 0};
        } else {
            if (!in_bounds(grid_, value.x, value.y))
                return {DirectorError::OutsideGrid, 0};

            TileType tile = grid_.at(value.x, value.y);
            const auto& data = grid_.data_at(value.x, value.y);
            if constexpr (std::is_same_v<T, DirectorSetZone>) {
                if (value.occupancy_capacity < 0 || value.occupancy_capacity > 8)
                    return {DirectorError::InvalidValue, 0};
                if (tile != TileType::Floor && tile != TileType::OpenSpace)
                    return {DirectorError::IncompatibleSite, 0};
            } else if constexpr (std::is_same_v<T, DirectorPlaceStructure>) {
                if (value.structure > DirectorStructure::Conveyor
                    || value.machine_type > MachineType::Output
                    || value.conveyor_direction > ConveyorDir::W)
                    return {DirectorError::InvalidValue, 0};
                TileType required = TileType::Floor;
                if (value.structure == DirectorStructure::Machine) {
                    if (value.machine_type == MachineType::Food)
                        required = TileType::FoodSource;
                    else if (value.machine_type == MachineType::Materials)
                        required = TileType::ScrapPile;
                }
                if (tile != required) return {DirectorError::IncompatibleSite, 0};
            } else if constexpr (std::is_same_v<T, DirectorRemoveStructure>) {
                if (tile == TileType::Entrance || tile == TileType::Exit)
                    return {DirectorError::ProtectedStructure, 0};
                if (tile != TileType::Wall && tile != TileType::Machine
                    && tile != TileType::Storage && tile != TileType::Conveyor)
                    return {DirectorError::NothingToRemove, 0};
            } else if constexpr (std::is_same_v<T, DirectorSetMaintenancePriority>) {
                if (value.priority > MaintenancePriority::High)
                    return {DirectorError::InvalidValue, 0};
                if (tile != TileType::Conveyor || !data.built)
                    return {DirectorError::IncompatibleSite, 0};
            }
            return {DirectorError::None, 1};
        }
    }, command);
}

DirectorResult Simulation::apply_director_command_unlogged(const DirectorCommand& command) {
    DirectorResult validation = validate_director_command(command);
    if (!validation.applied()) return validation;

    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, DirectorSetQuota>) {
            current_quota_per_tick_ = value.quota_per_tick;
            quota_manually_set_ = true;
            emit_log(-1, "DIRECTOR set quota to " + ff2(value.quota_per_tick),
                     EventType::DIRECTOR_INTERVENTION);
        } else if constexpr (std::is_same_v<T, DirectorSetZone>) {
            auto& data = grid_.data_at(value.x, value.y);
            data.occupancy_capacity = value.occupancy_capacity;
            data.overcapacity_ticks = 0;
            emit_log(-1, "DIRECTOR set occupancy capacity "
                     + std::to_string(value.occupancy_capacity) + " at ("
                     + std::to_string(value.x) + "," + std::to_string(value.y) + ")",
                     EventType::DIRECTOR_INTERVENTION);
        } else if constexpr (std::is_same_v<T, DirectorPlaceStructure>) {
            TileData previous = grid_.data_at(value.x, value.y);
            TileData replacement{};
            TileType tile = TileType::Floor;
            std::string description;
            if (value.structure == DirectorStructure::Wall) {
                tile = TileType::Wall;
                description = "wall";
            } else if (value.structure == DirectorStructure::Storage) {
                tile = TileType::Storage;
                replacement.built = true;
                replacement.storage_capacity = 20.0f;
                description = "storage";
            } else if (value.structure == DirectorStructure::Conveyor) {
                tile = TileType::Conveyor;
                replacement.built = true;
                replacement.build_cost = 1.5f;
                replacement.build_progress = replacement.build_cost;
                replacement.conveyor_dir = value.conveyor_direction;
                replacement.conveyor_condition = 1.0f;
                description = "conveyor";
            } else {
                tile = TileType::Machine;
                replacement.built = true;
                replacement.machine_type = value.machine_type;
                replacement.built_on_resource = value.machine_type != MachineType::Output;
                const Recipe& recipe = value.machine_type == MachineType::Food
                    ? Recipes::FOOD
                    : value.machine_type == MachineType::Materials
                        ? Recipes::MATERIALS : Recipes::OUTPUT;
                replacement.build_cost = recipe.input_amount;
                replacement.build_progress = replacement.build_cost;
                if (replacement.built_on_resource) {
                    replacement.resource_amount = previous.resource_amount;
                    replacement.resource_max = previous.resource_max;
                    replacement.resource_regen = previous.resource_regen;
                }
                description = machine_name(value.machine_type);
            }
            grid_.set(value.x, value.y, tile);
            grid_.data_at(value.x, value.y) = replacement;
            invalidate_agent_paths(registry_);
            emit_log(-1, "DIRECTOR placed " + description + " at ("
                     + std::to_string(value.x) + "," + std::to_string(value.y) + ")",
                     EventType::DIRECTOR_INTERVENTION);
        } else if constexpr (std::is_same_v<T, DirectorRemoveStructure>) {
            TileType removed = grid_.at(value.x, value.y);
            auto& data = grid_.data_at(value.x, value.y);
            auto lost = data.remove_stored_fraction(1.0f);
            for (size_t i = 0; i < lost.size(); i++) metrics_.resources_lost[i] += lost[i];
            if (removed == TileType::Conveyor && data.conveyor_contents > 0.0f) {
                metrics_.resources_lost[metric_index(data.conveyor_contents_type)]
                    += data.conveyor_contents;
            }

            TileType replacement_type = TileType::Floor;
            TileData replacement{};
            if (removed == TileType::Machine && data.built_on_resource) {
                replacement_type = data.machine_type == MachineType::Food
                    ? TileType::FoodSource : TileType::ScrapPile;
                replacement.resource_amount = data.resource_amount;
                replacement.resource_max = data.resource_max;
                replacement.resource_regen = data.resource_regen;
            }
            grid_.set(value.x, value.y, replacement_type);
            grid_.data_at(value.x, value.y) = replacement;
            invalidate_agent_paths(registry_);
            emit_log(-1, "DIRECTOR removed structure at ("
                     + std::to_string(value.x) + "," + std::to_string(value.y) + ")",
                     EventType::DIRECTOR_INTERVENTION);
        } else if constexpr (std::is_same_v<T, DirectorSetMaintenancePriority>) {
            grid_.data_at(value.x, value.y).maintenance_priority =
                static_cast<uint8_t>(value.priority);
            emit_log(-1, "DIRECTOR set maintenance priority "
                     + std::string(maintenance_priority_name(value.priority)) + " at ("
                     + std::to_string(value.x) + "," + std::to_string(value.y) + ")",
                     EventType::DIRECTOR_INTERVENTION);
        }
    }, command);
    return validation;
}

DirectorResult Simulation::apply_director_command(const DirectorCommand& command) {
    DirectorResult result = apply_director_command_unlogged(command);
    if (result.applied()) {
        director_log_.push_back({tick_, director_log_.size(), command});
    }
    return result;
}

DirectorResult Simulation::replay_director_event(const DirectorEvent& event) {
    if (event.tick != tick_) return {DirectorError::WrongTick, 0};
    if (event.sequence != director_log_.size())
        return {DirectorError::WrongSequence, 0};
    DirectorResult result = apply_director_command_unlogged(event.command);
    if (result.applied()) director_log_.push_back(event);
    return result;
}
