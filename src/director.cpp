#include "director.h"

#include <toml++/toml.hpp>

#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

const char* mode_name(DirectorMode mode) {
    switch (mode) {
        case DirectorMode::CALM: return "calm";
        case DirectorMode::NORMAL: return "normal";
        case DirectorMode::PRODUCTION_TEST: return "production";
    }
    return "normal";
}

std::optional<DirectorMode> parse_mode(std::string_view value) {
    if (value == "calm") return DirectorMode::CALM;
    if (value == "normal") return DirectorMode::NORMAL;
    if (value == "production") return DirectorMode::PRODUCTION_TEST;
    return std::nullopt;
}

const char* machine_name(MachineType type) {
    switch (type) {
        case MachineType::Food: return "food";
        case MachineType::Materials: return "materials";
        case MachineType::Output: return "output";
    }
    return "output";
}

std::optional<MachineType> parse_machine(std::string_view value) {
    if (value == "food") return MachineType::Food;
    if (value == "materials") return MachineType::Materials;
    if (value == "output") return MachineType::Output;
    return std::nullopt;
}

const char* direction_name(ConveyorDir direction) {
    switch (direction) {
        case ConveyorDir::N: return "north";
        case ConveyorDir::S: return "south";
        case ConveyorDir::E: return "east";
        case ConveyorDir::W: return "west";
    }
    return "east";
}

std::optional<ConveyorDir> parse_direction(std::string_view value) {
    if (value == "north") return ConveyorDir::N;
    if (value == "south") return ConveyorDir::S;
    if (value == "east") return ConveyorDir::E;
    if (value == "west") return ConveyorDir::W;
    return std::nullopt;
}

std::optional<DirectorStructure> parse_structure(std::string_view value) {
    if (value == "wall") return DirectorStructure::Wall;
    if (value == "storage") return DirectorStructure::Storage;
    if (value == "machine") return DirectorStructure::Machine;
    if (value == "conveyor") return DirectorStructure::Conveyor;
    return std::nullopt;
}

std::optional<MaintenancePriority> parse_priority(std::string_view value) {
    if (value == "normal") return MaintenancePriority::Normal;
    if (value == "high") return MaintenancePriority::High;
    return std::nullopt;
}

template <typename T>
std::optional<T> required_value(const toml::table& table, std::string_view key) {
    return table[key].value<T>();
}

bool parse_position(const toml::table& table, int& x, int& y) {
    auto parsed_x = required_value<int64_t>(table, "x");
    auto parsed_y = required_value<int64_t>(table, "y");
    if (!parsed_x || !parsed_y
        || *parsed_x < std::numeric_limits<int>::min()
        || *parsed_x > std::numeric_limits<int>::max()
        || *parsed_y < std::numeric_limits<int>::min()
        || *parsed_y > std::numeric_limits<int>::max()) {
        return false;
    }
    x = static_cast<int>(*parsed_x);
    y = static_cast<int>(*parsed_y);
    return true;
}

}  // namespace

const char* director_error_name(DirectorError error) {
    switch (error) {
        case DirectorError::None: return "applied";
        case DirectorError::WrongTick: return "wrong tick";
        case DirectorError::WrongSequence: return "wrong sequence";
        case DirectorError::InvalidValue: return "invalid value";
        case DirectorError::OutsideGrid: return "outside grid";
        case DirectorError::IncompatibleSite: return "incompatible site";
        case DirectorError::ProtectedStructure: return "protected structure";
        case DirectorError::NothingToRemove: return "nothing to remove";
        case DirectorError::DisabledInCalm: return "disabled in calm mode";
    }
    return "unknown error";
}

const char* director_structure_name(DirectorStructure structure) {
    switch (structure) {
        case DirectorStructure::Wall: return "wall";
        case DirectorStructure::Storage: return "storage";
        case DirectorStructure::Machine: return "machine";
        case DirectorStructure::Conveyor: return "conveyor";
    }
    return "wall";
}

const char* maintenance_priority_name(MaintenancePriority priority) {
    return priority == MaintenancePriority::High ? "high" : "normal";
}

bool fingerprint_config_source(const std::string& path, uint64_t& fingerprint,
                               std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open configuration for fingerprinting: " + path;
        return false;
    }

    uint64_t hash = 14695981039346656037ULL;
    auto mix = [&](unsigned char byte) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    };
    char buffer[4096];
    bool pending_carriage_return = false;
    while (input) {
        input.read(buffer, sizeof(buffer));
        for (std::streamsize i = 0; i < input.gcount(); i++) {
            unsigned char byte = static_cast<unsigned char>(buffer[i]);
            if (pending_carriage_return) {
                if (byte == '\n') {
                    mix('\n');
                    pending_carriage_return = false;
                    continue;
                }
                mix('\r');
                pending_carriage_return = false;
            }
            if (byte == '\r') pending_carriage_return = true;
            else mix(byte);
        }
    }
    if (!input.eof()) {
        error = "failed while fingerprinting configuration: " + path;
        return false;
    }
    if (pending_carriage_return) mix('\r');
    fingerprint = hash;
    return true;
}

bool write_director_log(const std::string& path, int seed, DirectorMode mode,
                         uint64_t config_fingerprint,
                         const std::vector<DirectorEvent>& events,
                         std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot open intervention log for writing: " + path;
        return false;
    }

    output << "format = \"vida-interventions\"\n"
           << "schema_version = 2\n"
           << "seed = " << seed << "\n"
           << "director_mode = \"" << mode_name(mode) << "\"\n"
           << "tick_phase = \"before_advance\"\n"
           << "config_fingerprint = \"" << std::hex << std::setw(16)
           << std::setfill('0') << config_fingerprint << std::dec << "\"\n";
    output << std::setprecision(std::numeric_limits<float>::max_digits10);

    for (const auto& event : events) {
        output << "\n[[event]]\n"
               << "tick = " << event.tick << "\n"
               << "sequence = " << event.sequence << "\n";
        std::visit([&](const auto& command) {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, DirectorSetQuota>) {
                output << "kind = \"set_quota\"\n"
                       << "quota_per_tick = " << command.quota_per_tick << "\n";
            } else if constexpr (std::is_same_v<T, DirectorSetZone>) {
                output << "kind = \"set_zone\"\n"
                       << "x = " << command.x << "\n"
                       << "y = " << command.y << "\n"
                       << "occupancy_capacity = " << command.occupancy_capacity << "\n";
            } else if constexpr (std::is_same_v<T, DirectorPlaceStructure>) {
                output << "kind = \"place_structure\"\n"
                       << "x = " << command.x << "\n"
                       << "y = " << command.y << "\n"
                       << "structure = \"" << director_structure_name(command.structure) << "\"\n";
                if (command.structure == DirectorStructure::Machine)
                    output << "machine_type = \"" << machine_name(command.machine_type) << "\"\n";
                if (command.structure == DirectorStructure::Conveyor)
                    output << "direction = \"" << direction_name(command.conveyor_direction) << "\"\n";
            } else if constexpr (std::is_same_v<T, DirectorRemoveStructure>) {
                output << "kind = \"remove_structure\"\n"
                       << "x = " << command.x << "\n"
                       << "y = " << command.y << "\n";
            } else if constexpr (std::is_same_v<T, DirectorSetMaintenancePriority>) {
                output << "kind = \"set_maintenance_priority\"\n"
                       << "x = " << command.x << "\n"
                       << "y = " << command.y << "\n"
                       << "priority = \"" << maintenance_priority_name(command.priority) << "\"\n";
            }
        }, event.command);
    }

    if (!output) {
        error = "failed while writing intervention log: " + path;
        return false;
    }
    return true;
}

bool read_director_log(const std::string& path, int& seed, DirectorMode& mode,
                        uint64_t& config_fingerprint,
                        std::vector<DirectorEvent>& events,
                        std::string& error) {
    try {
        toml::table document = toml::parse_file(path);
        auto format = document["format"].value<std::string_view>();
        auto schema = document["schema_version"].value<int64_t>();
        auto parsed_seed = document["seed"].value<int64_t>();
        auto parsed_mode_name = document["director_mode"].value<std::string_view>();
        auto tick_phase = document["tick_phase"].value<std::string_view>();
        auto parsed_config_fingerprint =
            document["config_fingerprint"].value<std::string_view>();
        if (!format || *format != "vida-interventions" || !schema || *schema != 2
            || !parsed_seed || !parsed_mode_name || !tick_phase
            || !parsed_config_fingerprint
            || *tick_phase != "before_advance") {
            error = "invalid intervention log header";
            return false;
        }
        if (*parsed_seed < std::numeric_limits<int>::min()
            || *parsed_seed > std::numeric_limits<int>::max()) {
            error = "intervention seed is outside integer range";
            return false;
        }
        auto parsed_mode = parse_mode(*parsed_mode_name);
        if (!parsed_mode) {
            error = "unknown director mode";
            return false;
        }
        uint64_t parsed_fingerprint = 0;
        auto fingerprint_result = std::from_chars(
            parsed_config_fingerprint->data(),
            parsed_config_fingerprint->data() + parsed_config_fingerprint->size(),
            parsed_fingerprint, 16);
        if (parsed_config_fingerprint->size() != 16
            || fingerprint_result.ec != std::errc{}
            || fingerprint_result.ptr
               != parsed_config_fingerprint->data() + parsed_config_fingerprint->size()) {
            error = "invalid configuration fingerprint";
            return false;
        }

        std::vector<DirectorEvent> parsed_events;
        int previous_tick = -1;
        uint64_t expected_sequence = 0;
        if (document.contains("event") && !document["event"].is_array()) {
            error = "intervention event ledger must be an array of tables";
            return false;
        }
        if (const toml::array* array = document["event"].as_array()) {
            for (const auto& node : *array) {
                const toml::table* table = node.as_table();
                if (!table) {
                    error = "each intervention event must be a table";
                    return false;
                }
                auto tick = required_value<int64_t>(*table, "tick");
                auto sequence = required_value<int64_t>(*table, "sequence");
                auto kind = required_value<std::string_view>(*table, "kind");
                if (!tick || !sequence || !kind || *tick < 0
                    || *tick > std::numeric_limits<int>::max()
                    || *sequence < 0
                    || static_cast<uint64_t>(*sequence) != expected_sequence
                    || *tick < previous_tick) {
                    error = "invalid or out-of-order intervention event";
                    return false;
                }

                DirectorCommand command;
                if (*kind == "set_quota") {
                    auto quota = required_value<double>(*table, "quota_per_tick");
                    if (!quota || !std::isfinite(*quota)
                        || *quota < 0.0 || *quota > std::numeric_limits<float>::max()) {
                        error = "invalid quota event";
                        return false;
                    }
                    command = DirectorSetQuota{static_cast<float>(*quota)};
                } else if (*kind == "set_zone") {
                    DirectorSetZone zone;
                    auto capacity = required_value<int64_t>(*table, "occupancy_capacity");
                    if (!parse_position(*table, zone.x, zone.y) || !capacity
                        || *capacity < std::numeric_limits<int>::min()
                        || *capacity > std::numeric_limits<int>::max()) {
                        error = "invalid zone event";
                        return false;
                    }
                    zone.occupancy_capacity = static_cast<int>(*capacity);
                    command = zone;
                } else if (*kind == "place_structure") {
                    DirectorPlaceStructure placement;
                    auto structure_name = required_value<std::string_view>(*table, "structure");
                    if (!parse_position(*table, placement.x, placement.y) || !structure_name) {
                        error = "invalid placement event";
                        return false;
                    }
                    auto structure = parse_structure(*structure_name);
                    if (!structure) {
                        error = "unknown structure type";
                        return false;
                    }
                    placement.structure = *structure;
                    if (*structure == DirectorStructure::Machine) {
                        auto value = required_value<std::string_view>(*table, "machine_type");
                        auto machine = value ? parse_machine(*value) : std::nullopt;
                        if (!machine) {
                            error = "unknown machine type";
                            return false;
                        }
                        placement.machine_type = *machine;
                    } else if (*structure == DirectorStructure::Conveyor) {
                        auto value = required_value<std::string_view>(*table, "direction");
                        auto direction = value ? parse_direction(*value) : std::nullopt;
                        if (!direction) {
                            error = "unknown conveyor direction";
                            return false;
                        }
                        placement.conveyor_direction = *direction;
                    }
                    command = placement;
                } else if (*kind == "remove_structure") {
                    DirectorRemoveStructure removal;
                    if (!parse_position(*table, removal.x, removal.y)) {
                        error = "invalid removal event";
                        return false;
                    }
                    command = removal;
                } else if (*kind == "set_maintenance_priority") {
                    DirectorSetMaintenancePriority maintenance;
                    auto value = required_value<std::string_view>(*table, "priority");
                    auto priority = value ? parse_priority(*value) : std::nullopt;
                    if (!parse_position(*table, maintenance.x, maintenance.y) || !priority) {
                        error = "invalid maintenance event";
                        return false;
                    }
                    maintenance.priority = *priority;
                    command = maintenance;
                } else {
                    error = "unknown intervention kind";
                    return false;
                }

                parsed_events.push_back({static_cast<int>(*tick), expected_sequence, command});
                previous_tick = static_cast<int>(*tick);
                expected_sequence++;
            }
        }

        seed = static_cast<int>(*parsed_seed);
        mode = *parsed_mode;
        config_fingerprint = parsed_fingerprint;
        events = std::move(parsed_events);
        return true;
    } catch (const toml::parse_error& parse_error) {
        error = std::string("cannot parse intervention log: ") + parse_error.what();
        return false;
    }
}
