#pragma once
// All ECS components and data types for La Vida Misma.
// Components are plain data structs -- no logic here.

#include <cstdint>
#include <string>
#include "path_cache.h"

// --- Tile Types ---

enum class TileType : uint8_t {
    Floor = 0,
    Wall,
    Machine,      // Factory machine (needs building, then produces food)
    Storage,      // Holds resources for communal use
    Entrance,     // DEPRECATED: no longer placed, kept for enum compatibility
    Exit,
    OpenSpace,    // Social/creative area
    FoodSource,   // Wild food (legacy, unused in current model)
    ScrapPile,    // Raw material deposit -- finite or slow regen
    EatingZone,   // Designated eating place (agent-built, must be ≥5 from any Machine)
    Conveyor,     // Directional transport tile — moves resources automatically
    HiddenSpace,  // Discovered by explorers; sanctuary from factory surveillance
};

// Machine subtypes: each Machine tile has a specific function.
// This determines what WORK produces and what inputs it consumes.
enum class MachineType : uint8_t {
    Food       = 0,  // raw_food → processed_food
    Materials  = 1,  // raw_material → construction_material + scrap byproduct
    Output     = 2,  // construction_material → output product (shipped as quota)
};

// Conveyor flow direction
enum class ConveyorDir : uint8_t {
    N = 0,  // up (y-1)
    S,      // down (y+1)
    E,      // right (x+1)
    W,      // left (x-1)
};

// --- Actions ---

enum class ActionType : uint8_t {
    GATHER = 0,   // Collect raw resources from ScrapPile
    BUILD,        // Construct an unbuilt Machine, EatingZone, or Conveyor
    WORK,         // Operate a built Machine (produces food into adjacent Storage)
    EAT,          // Consume food (from inventory or adjacent Storage / EatingZone)
    REST,
    SOCIALIZE,
    CREATE,       // Artistic expression (needs OpenSpace)
    EXPLORE,      // Move randomly, discover
    GET_FOOD,     // Pick up food from adjacent Storage into inventory (snack to-go)
    MAINTAIN,     // Repair a degraded Conveyor
    DISMANTLE,    // Tear down a built Conveyor (returns partial material)
    SABOTAGE,     // Irrational destruction driven by stress — damages machines/conveyors
    IDLE,
    COUNT
};

// Spatial contract: which tile an action requires the agent to stand on.
// EAT/REST/SOCIALIZE/EXPLORE/IDLE/GET_FOOD have no hard tile requirement (return true).
inline bool is_valid_action_tile(ActionType action, TileType tile) {
    switch (action) {
        case ActionType::GATHER:   return tile == TileType::ScrapPile || tile == TileType::FoodSource;
        case ActionType::BUILD:    return tile == TileType::Machine
                                         || tile == TileType::EatingZone
                                         || tile == TileType::Floor
                                         || tile == TileType::Conveyor
                                         || tile == TileType::FoodSource
                                         || tile == TileType::ScrapPile;
        case ActionType::WORK:     return tile == TileType::Machine;
        case ActionType::CREATE:   return tile == TileType::OpenSpace;
        case ActionType::MAINTAIN: return true;  // agent stands ADJACENT to conveyor
        case ActionType::DISMANTLE: return true; // agent stands ADJACENT to conveyor to tear down
        case ActionType::SABOTAGE:  return true; // agent stands ADJACENT to target
        default:                   return true;
    }
}

// --- Resource Types ---

enum class ResourceType : uint8_t {
    RAW_FOOD = 0,
    RAW_MATERIAL,
    FOOD,
    CONSTRUCTION_MATERIAL,
    OUTPUT,
};

// --- Components ---

struct NeedsComponent {
    float hunger     = 0.0f;   // [0, 1], 1 = critical
    float rest       = 0.0f;
    float social     = 0.0f;
    float expression = 0.0f;
    float purpose    = 0.0f;
    float meaning    = 0.0f;   // [0, 1], 1 = unfulfilled. Factory work doesn't satisfy.
                               // Only CREATE (artifacts), factions, hidden spaces fill this.
    float disease    = 0.0f;   // [0, 1], 0 = healthy. Raw food risk. Increases hunger decay + stress.
};

// Cultural artifact: produced by CREATE, boosts nearby agent mood
struct ArtifactComponent {
    int creator_id = -1;
    float strength = 1.0f;  // decays over time
    int age = 0;            // ticks since creation
};

// ============================================================
// Personality Archetypes
// Based on: Big Five (Costa & McCrae, 1992),
//           Belbin Team Roles (Belbin, 1981),
//           Holland RIASEC (Holland, 1959).
//
// Each archetype defines base trait values with per-spawn
// noise (±jitter) so no two agents are identical.
// ============================================================

enum class Archetype : uint8_t {
    FOREMAN,        // Belbin: Shaper/Coordinator. Drives construction, pulls others.
    NETWORKER,      // Belbin: Resource Investigator. Social glue, shares food, builds trust.
    ARTISAN,        // Belbin: Plant. Expression-driven, explores, creates variety.
    SURVIVOR,       // High resilience, low social. Outlasts crises. Ancho del grupo.
    EXPLORER,       // Belbin: Monitor-Evaluator. Curious, maps resources, tests limits.
    STEADY_WORKER,  // Belbin: Implementer. Works tirelessly, productive backbone.
    COUNT
};

inline const char* archetype_name(Archetype a) {
    switch (a) {
        case Archetype::FOREMAN:       return "Foreman";
        case Archetype::NETWORKER:     return "Networker";
        case Archetype::ARTISAN:       return "Artisan";
        case Archetype::SURVIVOR:      return "Survivor";
        case Archetype::EXPLORER:      return "Explorer";
        case Archetype::STEADY_WORKER: return "Worker";
        default:                       return "?";
    }
}

struct ArchetypeTraits {
    float compliance, laziness, artistry, gregariousness, resilience, curiosity;
    float jitter;  // ± noise applied to each trait on spawn
};

inline ArchetypeTraits archetype_traits(Archetype a) {
    switch (a) {
        //                         comp  lazy  art   greg  resi  cur   jitter
        case Archetype::FOREMAN:
            return {0.85f, 0.15f, 0.15f, 0.60f, 0.70f, 0.20f, 0.08f};
        case Archetype::NETWORKER:
            return {0.55f, 0.40f, 0.30f, 0.85f, 0.50f, 0.60f, 0.10f};
        case Archetype::ARTISAN:
            return {0.35f, 0.30f, 0.80f, 0.25f, 0.40f, 0.70f, 0.10f};
        case Archetype::SURVIVOR:
            return {0.40f, 0.50f, 0.10f, 0.15f, 0.90f, 0.20f, 0.08f};
        case Archetype::EXPLORER:
            return {0.30f, 0.25f, 0.50f, 0.45f, 0.55f, 0.85f, 0.10f};
        case Archetype::STEADY_WORKER:
            return {0.75f, 0.20f, 0.10f, 0.35f, 0.60f, 0.15f, 0.08f};
        default:
            return {0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.10f};
    }
}

struct PersonalityComponent {
    float compliance     = 0.5f;  // [0, 1], immutable
    float laziness       = 0.5f;
    float artistry       = 0.5f;
    float gregariousness = 0.5f;
    float resilience     = 0.5f;
    float curiosity      = 0.5f;
    Archetype archetype  = Archetype::COUNT;  // for display
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

    // Action stickiness: once an agent commits to WORK or BUILD, it stays
    // committed for this many ticks (resets when action changes).
    // Prevents agents from starting to walk to a machine, then abandoning
    // the task 1-2 ticks later when utility re-evaluates.
    int sticky_ticks = 0;  // remaining commitment ticks
    ActionType sticky_action = ActionType::IDLE;  // which action we're committed to

    // Path cache for A* — avoids recomputing the full path every tick
    PathCache path_cache;

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

// Stress states — qualitative behavior changes at thresholds
enum class StressState : uint8_t {
    NORMAL = 0,       // 0.0 - 0.4: standard behavior
    DISSOCIATED,      // 0.4 - 0.7: -30% social, +30% create/explore
    HOSTILE_EUPHORIA, // 0.7 - 0.9: ignores noncompliance, artificial mood boost, -50% trust gain
    BROKEN,           // 0.9+:      point of no return — stressed utility function
    REDEEMED,         // post-sabotage epiphany — collectivist martyr
};

inline const char* stress_state_name(StressState s) {
    switch (s) {
        case StressState::NORMAL:          return "Normal";
        case StressState::DISSOCIATED:     return "Dissociated";
        case StressState::HOSTILE_EUPHORIA: return "Euphoric";
        case StressState::BROKEN:          return "Broken";
        case StressState::REDEEMED:        return "Redeemed";
        default:                           return "?";
    }
}

struct StressComponent {
    float value = 0.0f;       // [0, 1]
    float trauma = 0.0f;     // [0, 1] PERMANENT — accumulated from chronic stress
    StressState state = StressState::NORMAL;
    int ticks_in_state = 0;   // how many consecutive ticks above 0.6 (for trauma accumulation)
    int sabotage_count = 0;   // how many times this agent has sabotaged
    bool can_redeem = false;  // set true after first sabotage; enables redemption roll
};

// Opinion dynamics: cultural beliefs that diverge via bounded confidence (doc §8.5)
// Each dimension is [0, 1] — 0 and 1 represent opposing stances.
struct OpinionComponent {
    static constexpr int DIMS = 4;
    float values[DIMS] = {0.5f, 0.5f, 0.5f, 0.5f};
    // [0] work_ethic:   0=slacker,      1=workaholic
    // [1] risk_tolerance:0=cautious,      1=reckless
    // [2] tradition:     0=innovative,    1=traditionalist
    // [3] solidarity:    0=selfish,       1=collectivist
};

inline OpinionComponent archetype_opinion_priors(Archetype a) {
    OpinionComponent op;
    switch (a) {
        //                           ethic  risk   trad   solid
        case Archetype::FOREMAN:       op = {{0.85f, 0.30f, 0.70f, 0.80f}}; break;
        case Archetype::NETWORKER:     op = {{0.50f, 0.50f, 0.30f, 0.90f}}; break;
        case Archetype::ARTISAN:       op = {{0.30f, 0.60f, 0.10f, 0.40f}}; break;
        case Archetype::SURVIVOR:      op = {{0.60f, 0.20f, 0.50f, 0.30f}}; break;
        case Archetype::EXPLORER:      op = {{0.40f, 0.85f, 0.15f, 0.35f}}; break;
        case Archetype::STEADY_WORKER: op = {{0.80f, 0.15f, 0.80f, 0.65f}}; break;
        default:                       op = {{0.50f, 0.50f, 0.50f, 0.50f}}; break;
    }
    return op;
}

inline const char* opinion_dim_name(int d) {
    switch (d) {
        case 0: return "ethic";
        case 1: return "risk";
        case 2: return "trad";
        case 3: return "solid";
        default: return "?";
    }
}

struct AgentComponent {
    int id = 0;
    bool alive = true;
    int ticks_at_max_hunger = 0;
    int ticks_at_max_rest = 0;
    float noncompliance = 0.0f;  // [0,1] how much the factory "notices" this agent slacking
    int faction_id = -1;         // -1 = no faction
    std::string cause_of_death;
};

struct InventoryComponent {
    float raw_food     = 0.0f;
    float raw_material = 0.0f;
    float food         = 0.0f;
    float construction_material = 0.0f;  // for building OutputMachine and conveyors
    static constexpr float CAPACITY = 10.0f;

    float total() const { return raw_food + raw_material + food + construction_material; }
    bool can_carry(float amount) const { return total() + amount <= CAPACITY; }
};

struct SkillsComponent {
    // Skill levels [0, 5] — improve with practice
    float factory_work = 0.0f;
    float domestic     = 0.0f;
    float artistic     = 0.0f;
    float social_skill = 0.0f;

    // XP accumulation — level = xp / 10, capped at 5
    float xp_factory = 0.0f;
    float xp_domestic = 0.0f;
    float xp_art     = 0.0f;
    float xp_social  = 0.0f;

    static float xp_to_level(float xp) { return std::min(5.0f, xp / 10.0f); }
    static float level_bonus(float level) { return 1.0f + level * 0.15f; }  // +15% per level
};

// --- Per-tile production data ---

struct TileData {
    // Resource sources (FoodSource, ScrapPile tiles)
    float resource_amount = 0.0f;   // current available
    float resource_max    = 0.0f;   // capacity
    float resource_regen  = 0.0f;   // per tick (0 = finite)

    // Machine / EatingZone state
    bool  built          = false;   // starts unbuilt
    float build_progress = 0.0f;    // [0, build_cost]
    float build_cost     = 0.0f;    // raw_material needed
    MachineType machine_type = MachineType::Food;  // subtype: Food, Materials, or Output
    bool  built_on_resource = false;  // Machine sits atop a resource tile (FoodSource/ScrapPile): auto-gathers
    int claimed_by = -1;  // agent ID that claimed this machine (-1 = unclaimed)
                         // Pattern from RimWorld/DF: prevents all agents from
                         // targeting the same machine. Soft claim: other agents
                         // can still work it but with reduced utility.

    // Storage contents
    float stored_food         = 0.0f;
    float stored_raw_food     = 0.0f;
    float stored_raw_material = 0.0f;
    float stored_construction_material = 0.0f;  // from MaterialsMachine
    float stored_output               = 0.0f;  // from OutputMachine — shipped as quota
    float storage_capacity    = 0.0f;

    // Conveyor state
    ConveyorDir conveyor_dir      = ConveyorDir::E; // flow direction
    float       conveyor_condition = 1.0f;           // [0, 1], 0 = broken
    ResourceType conveyor_contents_type = ResourceType::FOOD;
    float       conveyor_contents     = 0.0f;       // amount sitting on belt

    // Dismantle tracking — who tore down a conveyor here, and when.
    // Used for social penalty if not rebuilt promptly.
    int   dismantled_by     = -1;    // agent id who dismantled, -1 = never
    int   dismantled_at_tick = -1;   // when it was dismantled
    int   original_type     = 0;     // 0=conveyor frame, for rebuild tracking

    // Hidden space tracking
    int   hidden_space_occupancy = 0; // ticks of over-occupancy; factory seals at 10

    bool has_data() const {
        return resource_max > 0.0f || build_cost > 0.0f || storage_capacity > 0.0f;
    }
};
