#pragma once
// All ECS components and data types for La Vida Misma.
// Components are plain data structs -- no logic here.

#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <random>
#include <algorithm>  // std::clamp (for stress smoothstep helpers)
#include "path_cache.h"

// --- Tile Types ---

enum class TileType : uint8_t {
    Floor = 0,
    Wall,
    Machine,      // Factory machine (needs building, then produces food)
    Storage,      // Holds resources for communal use
    Exit,
    Entrance,     // External arrivals enter here
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
    CREATE,       // Artistic expression at a personally selected place
    EXPLORE,      // Move randomly, discover
    GET_FOOD,     // Pick up food from adjacent Storage into inventory (snack to-go)
    MAINTAIN,     // Repair a degraded Conveyor
    DISMANTLE,    // Tear down a built Conveyor (returns partial material)
    SABOTAGE,     // Irrational destruction driven by stress — damages machines/conveyors
    IDLE,
    COUNT
};

// Spatial contract: cultural actions use a scored walkable target rather than a
// semantically privileged tile type.
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
        case ActionType::CREATE:   return tile != TileType::Wall;
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
    float meaning    = 0.0f;   // [0, 1], 1 = unfulfilled. Completed creative work can satisfy it.
    float disease    = 0.0f;   // [0, 1], 0 = healthy. Raw food risk. Increases hunger decay + stress.
};

// Cultural artifact: produced by CREATE, boosts nearby agent mood
struct ArtifactComponent {
    int creator_id = -1;
    float strength = 1.0f;  // decays over time
    int age = 0;            // ticks since creation
};

struct PlaceMemoryEntry {
    int x = -1;
    int y = -1;
    float affinity = 0.0f;  // [-1, 1], learned from personal outcomes
    int exposures = 0;
    int last_tick = -1;
};

struct PlaceMemoryComponent {
    std::vector<PlaceMemoryEntry> places;
};

struct CreativeWorkComponent {
    float progress = 0.0f;  // retained fraction of the next artifact work unit
    uint64_t completed_units = 0;
};

enum class AgentOrigin : uint8_t {
    INITIAL = 0,
    ARRIVAL,
    BIRTH,
};

enum class DeathCause : uint8_t {
    NONE = 0,
    STARVATION,
    EXHAUSTION,
    BREAKDOWN,
    COLLAPSE,
    SUICIDE,
    NATURAL,
};

inline const char* agent_origin_name(AgentOrigin origin) {
    switch (origin) {
        case AgentOrigin::INITIAL: return "initial";
        case AgentOrigin::ARRIVAL: return "arrival";
        case AgentOrigin::BIRTH: return "birth";
        default: return "unknown";
    }
}

inline const char* death_cause_name(DeathCause cause) {
    switch (cause) {
        case DeathCause::STARVATION: return "starvation";
        case DeathCause::EXHAUSTION: return "exhaustion";
        case DeathCause::BREAKDOWN: return "breakdown";
        case DeathCause::COLLAPSE: return "collapse";
        case DeathCause::SUICIDE: return "suicide";
        case DeathCause::NATURAL: return "natural";
        default: return "none";
    }
}

struct LifecycleComponent {
    AgentOrigin origin = AgentOrigin::INITIAL;
    int parent_a = -1;
    int parent_b = -1;
    int entry_tick = 0;
    int age_at_entry = 0;
    int lifespan = 0;
    int cohort = 0;
    int generation = 0;
    int last_reproduction_tick = -1000000000;
    int death_tick = -1;
    int first_trusted_edge_tick = -1;
    float peak_influence = 0.0f;
};

struct RandomComponent {
    std::mt19937 engine;

    explicit RandomComponent(uint32_t seed) : engine(seed) {}
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
        default:                       return "Unclassified";
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

struct UtilityBreakdown {
    float self = 0.0f;
    float factory = 0.0f;
    float cost = 0.0f;
    float risk = 0.0f;
    float final = 0.0f;
    bool feasible = false;
};

struct ActionComponent {
    ActionType current = ActionType::IDLE;
    int target_x = -1;
    int target_y = -1;
    bool at_target = false;
    bool effected_last_tick = false;

    // Action stickiness: once an agent commits to WORK or BUILD, it stays
    // committed for this many ticks (resets when action changes).
    // Prevents agents from starting to walk to a machine, then abandoning
    // the task 1-2 ticks later when utility re-evaluates.
    int sticky_ticks = 0;  // remaining commitment ticks
    ActionType sticky_action = ActionType::IDLE;  // which action we're committed to

    // Path cache for A* — avoids recomputing the full path every tick
    PathCache path_cache;

    // Last computed utility decomposition for display and diagnostics.
    std::array<UtilityBreakdown,
               static_cast<size_t>(ActionType::COUNT)> last_utility{};
    std::array<int, static_cast<size_t>(ActionType::COUNT)> preferred_x = [] {
        std::array<int, static_cast<size_t>(ActionType::COUNT)> values{};
        values.fill(-1);
        return values;
    }();
    std::array<int, static_cast<size_t>(ActionType::COUNT)> preferred_y = [] {
        std::array<int, static_cast<size_t>(ActionType::COUNT)> values{};
        values.fill(-1);
        return values;
    }();
    std::array<float, static_cast<size_t>(ActionType::COUNT)> preferred_place_score{};
};

// Stress states — qualitative behavior changes at thresholds
enum class StressState : uint8_t {
    NORMAL = 0,       // 0.0 - 0.4: standard behavior
    DISSOCIATED,      // 0.4 - 0.7: -30% social, +30% create/explore
    HOSTILE_EUPHORIA, // 0.7 - 0.9: ignores noncompliance, artificial mood boost, -50% trust gain
    BROKEN,           // 0.9+:      point of no return — stressed utility function
};

inline const char* stress_state_name(StressState s) {
    switch (s) {
        case StressState::NORMAL:          return "Normal";
        case StressState::DISSOCIATED:     return "Dissociated";
        case StressState::HOSTILE_EUPHORIA: return "Euphoric";
        case StressState::BROKEN:          return "Broken";
        default:                           return "?";
    }
}

// ============================================================
// CONTINUOUS STRESS MODIFIERS (Phase 3 of emergence redesign)
//
// These replace the discrete 'if state == DISSOCIATED then mult = 0.7'
// branches with smoothstep functions of stress.value. The StressState enum
// becomes a derived label for display/GUI only; behavior is driven by the
// continuous curves below.
//
// Mapping (legacy FSM -> continuous equivalent):
//   NORMAL          (0.0-0.4):  all modifiers ~ 1.0 (no effect)
//   DISSOCIATED     (0.4-0.7):  gregariousness_mult 0.7, create/explore +30%
//   HOSTILE_EUPHORIA(0.7-0.9):  noncomp_mult 0.0, mood boost
//   BROKEN          (0.9+):     gregariousness_mult 0.3, work blocked
//
// smoothstep(edge0, edge1, x) ramps 0->1 as x goes edge0->edge1.
// ============================================================
inline float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Gregariousness multiplier: 1.0 at low stress, ramps to 0.3 at BROKEN.
// Legacy: DISSOCIATED=0.7, BROKEN=0.3. Continuous: smoothstep(0.4, 0.9).
inline float stress_gregariousness_mult(float stress_value) {
    // At stress=0.4: ~0.93, at 0.7: ~0.65, at 0.9: 0.3
    return 1.0f - smoothstep(0.4f, 0.9f, stress_value) * 0.7f;
}

// Create/Explore boost multiplier: 1.0 at low stress, up to 1.3 at DISSOCIATED.
// Legacy: DISSOCIATED *= 1.3. Continuous: smoothstep ramp centered at 0.55.
inline float stress_creativity_mult(float stress_value) {
    // Peaks around 0.55 (mid-DISSOCIATED), fades at BROKEN
    float ramp_up = smoothstep(0.3f, 0.55f, stress_value);
    float ramp_down = 1.0f - smoothstep(0.7f, 0.95f, stress_value);
    return 1.0f + 0.3f * ramp_up * ramp_down;
}

// Noncompliance stress multiplier: 1.0 at low stress, drops to 0 at EUPHORIC.
// Legacy: DISSOCIATED=0.5, HOSTILE_EUPHORIA=0.0. Continuous: smoothstep(0.4, 0.7).
inline float stress_noncomp_mult(float stress_value) {
    return 1.0f - smoothstep(0.4f, 0.7f, stress_value);
}

// Work suppression: 1.0 (work allowed) at low stress, drops to 0 at BROKEN.
// Legacy: BROKEN blocks WORK entirely. Continuous: smoothstep(0.85, 1.0).
inline float stress_work_mult(float stress_value) {
    return 1.0f - smoothstep(0.85f, 1.0f, stress_value);
}

// Derive the display label from stress.value (for GUI/chronicle).
inline StressState stress_state_from_value(float stress_value) {
    if (stress_value < 0.4f) return StressState::NORMAL;
    if (stress_value < 0.7f) return StressState::DISSOCIATED;
    if (stress_value < 0.9f) return StressState::HOSTILE_EUPHORIA;
    return StressState::BROKEN;
}

struct StressComponent {
    float value = 0.0f;       // [0, 1]
    float trauma = 0.0f;     // [0, 1] PERMANENT — accumulated from chronic stress
    StressState state = StressState::NORMAL;
    int ticks_in_state = 0;   // how many consecutive ticks above 0.6 (for trauma accumulation)
    int sabotage_count = 0;   // how many times this agent has sabotaged
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
    float noncompliance = 0.0f;  // [0,1] legacy policy diagnostic
    int community_id = -1;       // Observed graph community; never read by behavior.
    DeathCause death_cause = DeathCause::NONE;
    std::string cause_of_death;
};

struct InventoryComponent {
    float raw_food     = 0.0f;
    float raw_material = 0.0f;
    float food         = 0.0f;
    float construction_material = 0.0f;  // for building OutputMachine and conveyors
    float output       = 0.0f;           // hauled output product — carried to Exit-adjacent Storage
    static constexpr float CAPACITY = 10.0f;

    float total() const { return raw_food + raw_material + food + construction_material + output; }
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

    // Skills are persistent: the current model has no forgetting by disuse.

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

    float total_stored() const {
        return stored_raw_food + stored_raw_material + stored_food
             + stored_construction_material + stored_output;
    }

    std::array<float, 5> remove_stored_fraction(float fraction) {
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        std::array<float, 5> removed{
            stored_raw_food * fraction,
            stored_raw_material * fraction,
            stored_food * fraction,
            stored_construction_material * fraction,
            stored_output * fraction,
        };
        stored_raw_food -= removed[0];
        stored_raw_material -= removed[1];
        stored_food -= removed[2];
        stored_construction_material -= removed[3];
        stored_output -= removed[4];
        return removed;
    }

    // Conveyor state
    ConveyorDir conveyor_dir      = ConveyorDir::E; // flow direction
    float       conveyor_condition = 1.0f;           // [0, 1], 0 = broken
    ResourceType conveyor_contents_type = ResourceType::FOOD;
    float       conveyor_contents     = 0.0f;       // amount sitting on belt
    uint8_t     maintenance_priority = 0;           // anonymous institutional signal

    // Dismantle tracking — who tore down a conveyor here, and when.
    // Used for social penalty if not rebuilt promptly.
    int   dismantled_by     = -1;    // agent id who dismantled, -1 = never
    int   dismantled_at_tick = -1;   // when it was dismantled
    int   original_type     = 0;     // 0=conveyor frame, for rebuild tracking

    // Hidden space tracking
    int   hidden_space_occupancy = 0; // ticks of over-occupancy; factory seals at 10
    // Anonymous physical occupancy policy used by the indifferent institution.
    int   occupancy_capacity = 0;
    int   overcapacity_ticks = 0;

    bool has_data() const {
        return resource_max > 0.0f || build_cost > 0.0f || storage_capacity > 0.0f;
    }
};
