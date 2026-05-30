#pragma once
// All ECS components and data types for La Vida Misma.
// Components are plain data structs -- no logic here.

#include <cstdint>
#include <string>

// --- Tile Types ---

enum class TileType : uint8_t {
    Floor = 0,
    Wall,
    Machine,      // Factory machine (needs building, then produces food)
    Storage,      // Holds resources for communal use (machine output lands here)
    Entrance,
    Exit,
    OpenSpace,    // Social/creative area
    FoodSource,   // Wild food (legacy, unused in current model)
    ScrapPile,    // Raw material deposit -- finite or slow regen
    EatingZone,   // Designated eating place (agent-built, must be ≥5 from any Machine)
};

// --- Actions ---

enum class ActionType : uint8_t {
    GATHER = 0,   // Collect raw resources from ScrapPile
    BUILD,        // Construct an unbuilt Machine or EatingZone frame (or place a new EatingZone)
    WORK,         // Operate a built Machine (produces food into adjacent Storage)
    EAT,          // Consume food (from inventory or adjacent Storage / EatingZone)
    REST,
    SOCIALIZE,
    CREATE,       // Artistic expression (needs OpenSpace)
    EXPLORE,      // Move randomly, discover
    GET_FOOD,     // Pick up food from adjacent Storage into inventory (snack to-go)
    IDLE,
    COUNT
};

// Spatial contract: which tile an action requires the agent to stand on.
// EAT/REST/SOCIALIZE/EXPLORE/IDLE/GET_FOOD have no hard tile requirement (return true).
inline bool is_valid_action_tile(ActionType action, TileType tile) {
    switch (action) {
        case ActionType::GATHER: return tile == TileType::ScrapPile;
        case ActionType::BUILD:  return tile == TileType::Machine
                                     || tile == TileType::EatingZone
                                     || tile == TileType::Floor; // Floor: start a new EatingZone here
        case ActionType::WORK:   return tile == TileType::Machine;
        case ActionType::CREATE: return tile == TileType::OpenSpace;
        default:                 return true;
    }
}

// --- Resource Types ---

enum class ResourceType : uint8_t {
    RAW_FOOD = 0,
    RAW_MATERIAL,
    FOOD,
};

// --- Components ---

struct NeedsComponent {
    float hunger     = 0.0f;   // [0, 1], 1 = critical
    float rest       = 0.0f;
    float social     = 0.0f;
    float expression = 0.0f;
    float purpose    = 0.0f;
};

struct PersonalityComponent {
    float compliance     = 0.5f;  // [0, 1], immutable
    float laziness       = 0.5f;
    float artistry       = 0.5f;
    float gregariousness = 0.5f;
    float resilience     = 0.5f;
    float curiosity      = 0.5f;
};

struct PositionComponent {
    int x = 0;
    int y = 0;
};

struct ActionComponent {
    ActionType current = ActionType::IDLE;
    int target_x = -1;
    int target_y = -1;
    bool at_target = false;

    // Last computed utilities (for display/debugging)
    float last_utility_gather    = 0.0f;
    float last_utility_build     = 0.0f;
    float last_utility_work      = 0.0f;
    float last_utility_eat       = 0.0f;
    float last_utility_rest      = 0.0f;
    float last_utility_socialize = 0.0f;
    float last_utility_create    = 0.0f;
    float last_utility_explore   = 0.0f;
    float last_utility_get_food  = 0.0f;
};

struct StressComponent {
    float value = 0.0f;  // [0, 1]
};

struct AgentComponent {
    int id = 0;
    bool alive = true;
    int ticks_at_max_hunger = 0;
    int ticks_at_max_rest = 0;
    std::string cause_of_death;
};

struct InventoryComponent {
    float raw_food     = 0.0f;
    float raw_material = 0.0f;
    float food         = 0.0f;
    static constexpr float CAPACITY = 10.0f;

    float total() const { return raw_food + raw_material + food; }
    bool can_carry(float amount) const { return total() + amount <= CAPACITY; }
};

struct SkillsComponent {
    float factory_work = 0.0f;
    float domestic     = 0.0f;
    float artistic     = 0.0f;
    float social_skill = 0.0f;
};

// --- Per-tile production data ---

struct TileData {
    // Resource sources (FoodSource, ScrapPile tiles)
    float resource_amount = 0.0f;   // current available
    float resource_max    = 0.0f;   // capacity
    float resource_regen  = 0.0f;   // per tick (0 = finite)

    // Machine state
    bool  built          = false;   // starts unbuilt
    float build_progress = 0.0f;    // [0, build_cost]
    float build_cost     = 0.0f;    // raw_material needed

    // Storage contents
    float stored_food         = 0.0f;
    float stored_raw_food     = 0.0f;
    float stored_raw_material = 0.0f;
    float storage_capacity    = 0.0f;

    bool has_data() const {
        return resource_max > 0.0f || build_cost > 0.0f || storage_capacity > 0.0f;
    }
};
