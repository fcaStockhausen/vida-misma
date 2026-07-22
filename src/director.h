#pragma once

#include "components.h"
#include "config.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

struct DirectorSetQuota {
    float quota_per_tick = 0.0f;
    bool operator==(const DirectorSetQuota&) const = default;
};

struct DirectorSetZone {
    int x = -1;
    int y = -1;
    int occupancy_capacity = 0;
    bool operator==(const DirectorSetZone&) const = default;
};

enum class DirectorStructure : uint8_t {
    Wall,
    Storage,
    Machine,
    Conveyor,
};

struct DirectorPlaceStructure {
    int x = -1;
    int y = -1;
    DirectorStructure structure = DirectorStructure::Wall;
    MachineType machine_type = MachineType::Output;
    ConveyorDir conveyor_direction = ConveyorDir::E;
    bool operator==(const DirectorPlaceStructure&) const = default;
};

struct DirectorRemoveStructure {
    int x = -1;
    int y = -1;
    bool operator==(const DirectorRemoveStructure&) const = default;
};

enum class MaintenancePriority : uint8_t {
    Normal = 0,
    High = 1,
};

struct DirectorSetMaintenancePriority {
    int x = -1;
    int y = -1;
    MaintenancePriority priority = MaintenancePriority::Normal;
    bool operator==(const DirectorSetMaintenancePriority&) const = default;
};

using DirectorCommand = std::variant<
    DirectorSetQuota,
    DirectorSetZone,
    DirectorPlaceStructure,
    DirectorRemoveStructure,
    DirectorSetMaintenancePriority>;

struct DirectorEvent {
    int tick = 0;
    uint64_t sequence = 0;
    DirectorCommand command;
    bool operator==(const DirectorEvent&) const = default;
};

enum class DirectorError : uint8_t {
    None,
    WrongTick,
    WrongSequence,
    InvalidValue,
    OutsideGrid,
    IncompatibleSite,
    ProtectedStructure,
    NothingToRemove,
    DisabledInCalm,
};

struct DirectorResult {
    DirectorError error = DirectorError::None;
    int affected_cells = 0;

    bool applied() const { return error == DirectorError::None; }
    bool operator==(const DirectorResult&) const = default;
};

const char* director_error_name(DirectorError error);
const char* director_structure_name(DirectorStructure structure);
const char* maintenance_priority_name(MaintenancePriority priority);

bool fingerprint_config_source(const std::string& path, uint64_t& fingerprint,
                               std::string& error);
bool write_director_log(const std::string& path, int seed, DirectorMode mode,
                         uint64_t config_fingerprint,
                         const std::vector<DirectorEvent>& events,
                         std::string& error);
bool read_director_log(const std::string& path, int& seed, DirectorMode& mode,
                        uint64_t& config_fingerprint,
                        std::vector<DirectorEvent>& events,
                        std::string& error);
