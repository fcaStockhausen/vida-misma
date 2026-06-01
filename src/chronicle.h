#pragma once
// Chronicle: narrative event logging engine for La Vida Misma.
//
// Captures rich events that can reconstruct the story of the simulation.
// Supports querying by agent, event type, tick range — and per-agent
// timelines that make it possible to follow a single agent's life arc.
//
// Design goals:
//   - Every significant state change produces a chronicle event
//   - Events are queryable for reconstruction (agent follow, faction arcs, etc.)
//   - Events carry enough context to generate human-readable prose later
//   - Cheap: O(1) push, O(k) query where k = result set

#include "components.h"
#include <vector>
#include <string>
#include <deque>
#include <functional>
#include <cstdio>
#include <algorithm>

// ============================================================
// Event taxonomy
// ============================================================

enum class EventType : uint8_t {
    // Agent lifecycle
    SPAWNED,
    DIED_STARVATION,
    DIED_EXHAUSTION,
    DIED_BREAKDOWN,
    DIED_COLLAPSE,
    DIED_SUICIDE,

    // Stress & trauma
    STRESS_STATE_CHANGE,
    TRAUMA_GAINED,
    BREAKDOWN,
    REDEMPTION,
    SABOTAGE,

    // Social
    OPINION_SHIFT,
    TRUST_MILESTONE,
    FOOD_SHARED,
    FACTION_FORMED,
    FACTION_JOINED,
    FACTION_LEFT,

    // Production
    BUILT_MACHINE,
    BUILT_CONVEYOR,
    BUILT_EATING_ZONE,
    WORK_COMPLETED,
    GATHERED,
    MAINTAINED,
    DISMANTLED,

    // World events
    MACHINE_ACTIVATED,
    MACHINE_BROKE,
    FACTORY_RESTRUCTURE,
    FACTORY_CONFISCATED,
    FACTORY_SEALED_SPACE,
    ARTIFACT_CREATED,
    HIDDEN_SPACE_FOUND,
    QUOTA_MILESTONE,

    // Narrative markers (synthesized from state transitions)
    FIRST_BUILD,
    FIRST_DEATH,
    FIRST_SABOTAGE,
    FIRST_FACTION,
    FIRST_ARTIFACT,
    POPULATION_MILESTONE,
    CRISIS_PERIOD,

    COUNT
};

inline const char* event_type_name(EventType t) {
    switch (t) {
        case EventType::SPAWNED:           return "SPAWNED";
        case EventType::DIED_STARVATION:   return "DIED_STARVATION";
        case EventType::DIED_EXHAUSTION:   return "DIED_EXHAUSTION";
        case EventType::DIED_BREAKDOWN:    return "DIED_BREAKDOWN";
        case EventType::DIED_COLLAPSE:     return "DIED_COLLAPSE";
        case EventType::DIED_SUICIDE:      return "DIED_SUICIDE";
        case EventType::STRESS_STATE_CHANGE: return "STRESS_STATE";
        case EventType::TRAUMA_GAINED:     return "TRAUMA";
        case EventType::BREAKDOWN:         return "BREAKDOWN";
        case EventType::REDEMPTION:        return "REDEMPTION";
        case EventType::SABOTAGE:          return "SABOTAGE";
        case EventType::OPINION_SHIFT:     return "OPINION_SHIFT";
        case EventType::TRUST_MILESTONE:   return "TRUST_MILESTONE";
        case EventType::FOOD_SHARED:       return "FOOD_SHARED";
        case EventType::FACTION_FORMED:    return "FACTION_FORMED";
        case EventType::FACTION_JOINED:    return "FACTION_JOINED";
        case EventType::FACTION_LEFT:      return "FACTION_LEFT";
        case EventType::BUILT_MACHINE:     return "BUILT_MACHINE";
        case EventType::BUILT_CONVEYOR:    return "BUILT_CONVEYOR";
        case EventType::BUILT_EATING_ZONE: return "BUILT_EATING_ZONE";
        case EventType::WORK_COMPLETED:    return "WORK_COMPLETED";
        case EventType::GATHERED:          return "GATHERED";
        case EventType::MAINTAINED:        return "MAINTAINED";
        case EventType::DISMANTLED:        return "DISMANTLED";
        case EventType::MACHINE_ACTIVATED: return "MACHINE_ACTIVATED";
        case EventType::MACHINE_BROKE:     return "MACHINE_BROKE";
        case EventType::FACTORY_RESTRUCTURE: return "RESTRUCTURE";
        case EventType::FACTORY_CONFISCATED: return "CONFISCATED";
        case EventType::FACTORY_SEALED_SPACE: return "SEALED_SPACE";
        case EventType::ARTIFACT_CREATED:  return "ARTIFACT";
        case EventType::HIDDEN_SPACE_FOUND: return "HIDDEN_SPACE";
        case EventType::QUOTA_MILESTONE:   return "QUOTA_MILESTONE";
        case EventType::FIRST_BUILD:       return "FIRST_BUILD";
        case EventType::FIRST_DEATH:       return "FIRST_DEATH";
        case EventType::FIRST_SABOTAGE:    return "FIRST_SABOTAGE";
        case EventType::FIRST_FACTION:     return "FIRST_FACTION";
        case EventType::FIRST_ARTIFACT:    return "FIRST_ARTIFACT";
        case EventType::POPULATION_MILESTONE: return "POP_MILESTONE";
        case EventType::CRISIS_PERIOD:     return "CRISIS";
        default:                           return "?";
    }
}

// Broad categories for filtering
enum class EventCategory : uint8_t {
    LIFECYCLE,    // spawn, death
    STRESS,       // stress states, trauma, breakdown, redemption
    SOCIAL,       // opinions, trust, sharing, factions
    PRODUCTION,   // build, work, gather, maintain, dismantle
    WORLD,        // factory events, machines, artifacts
    NARRATIVE,    // milestones, firsts, crises
    COUNT
};

inline EventCategory category_of(EventType t) {
    switch (t) {
        case EventType::SPAWNED:
        case EventType::DIED_STARVATION:
        case EventType::DIED_EXHAUSTION:
        case EventType::DIED_BREAKDOWN:
        case EventType::DIED_COLLAPSE:
        case EventType::DIED_SUICIDE:
            return EventCategory::LIFECYCLE;

        case EventType::STRESS_STATE_CHANGE:
        case EventType::TRAUMA_GAINED:
        case EventType::BREAKDOWN:
        case EventType::REDEMPTION:
        case EventType::SABOTAGE:
            return EventCategory::STRESS;

        case EventType::OPINION_SHIFT:
        case EventType::TRUST_MILESTONE:
        case EventType::FOOD_SHARED:
        case EventType::FACTION_FORMED:
        case EventType::FACTION_JOINED:
        case EventType::FACTION_LEFT:
            return EventCategory::SOCIAL;

        case EventType::BUILT_MACHINE:
        case EventType::BUILT_CONVEYOR:
        case EventType::BUILT_EATING_ZONE:
        case EventType::WORK_COMPLETED:
        case EventType::GATHERED:
        case EventType::MAINTAINED:
        case EventType::DISMANTLED:
            return EventCategory::PRODUCTION;

        case EventType::MACHINE_ACTIVATED:
        case EventType::MACHINE_BROKE:
        case EventType::FACTORY_RESTRUCTURE:
        case EventType::FACTORY_CONFISCATED:
        case EventType::FACTORY_SEALED_SPACE:
        case EventType::ARTIFACT_CREATED:
        case EventType::HIDDEN_SPACE_FOUND:
        case EventType::QUOTA_MILESTONE:
            return EventCategory::WORLD;

        case EventType::FIRST_BUILD:
        case EventType::FIRST_DEATH:
        case EventType::FIRST_SABOTAGE:
        case EventType::FIRST_FACTION:
        case EventType::FIRST_ARTIFACT:
        case EventType::POPULATION_MILESTONE:
        case EventType::CRISIS_PERIOD:
            return EventCategory::NARRATIVE;

        default:
            return EventCategory::WORLD;
    }
}

// ============================================================
// ChronicleEvent: one recorded event
// ============================================================

struct ChronicleEvent {
    int         tick;
    EventType   type;
    int         agent_id;       // -1 = system/world event
    int         x = -1, y = -1; // location (-1 if not applicable)
    float       value = 0.0f;   // generic float (amount, level, etc.)
    int         ref_id = -1;    // reference to another entity (other agent, faction, etc.)
    std::string text;           // human-readable one-line description

    // Compact single-line representation for UI display
    std::string summary() const {
        char buf[256];
        if (agent_id >= 0) {
            std::snprintf(buf, sizeof(buf), "[%5d] A%-2d %s",
                tick, agent_id, text.c_str());
        } else {
            std::snprintf(buf, sizeof(buf), "[%5d] %s",
                tick, text.c_str());
        }
        return buf;
    }
};

// ============================================================
// Chronicle: the event database
// ============================================================

class Chronicle {
public:
    // --- Recording ---
    void record(ChronicleEvent ev) {
        int aid = ev.agent_id;
        events_.push_back(std::move(ev));

        // Agent index
        if (aid >= 0 && aid < MAX_AGENT_INDEX) {
            agent_index_[aid].push_back((int)events_.size() - 1);
        }

        // Type index
        int ti = (int)ev.type;
        if (ti < (int)EventType::COUNT) {
            type_index_[ti].push_back((int)events_.size() - 1);
        }
    }

    // Convenience: record with inline construction
    void log(int tick, EventType type, int agent_id,
             const std::string& text,
             int x = -1, int y = -1,
             float value = 0.0f, int ref_id = -1)
    {
        record({tick, type, agent_id, x, y, value, ref_id, text});
    }

    // --- Queries ---

    // All events for a specific agent (lifetime arc)
    std::vector<const ChronicleEvent*> by_agent(int agent_id) const {
        std::vector<const ChronicleEvent*> result;
        if (agent_id < 0 || agent_id >= MAX_AGENT_INDEX) return result;
        for (int idx : agent_index_[agent_id]) {
            result.push_back(&events_[idx]);
        }
        return result;
    }

    // All events of a specific type
    std::vector<const ChronicleEvent*> by_type(EventType type) const {
        std::vector<const ChronicleEvent*> result;
        int ti = (int)type;
        if (ti >= (int)EventType::COUNT) return result;
        for (int idx : type_index_[ti]) {
            result.push_back(&events_[idx]);
        }
        return result;
    }

    // Events in a tick range [from, to]
    std::vector<const ChronicleEvent*> by_tick_range(int from, int to) const {
        std::vector<const ChronicleEvent*> result;
        for (auto& ev : events_) {
            if (ev.tick >= from && ev.tick <= to)
                result.push_back(&ev);
        }
        return result;
    }

    // Events for an agent in a tick range
    std::vector<const ChronicleEvent*> by_agent_and_range(
        int agent_id, int from, int to) const
    {
        std::vector<const ChronicleEvent*> result;
        if (agent_id < 0 || agent_id >= MAX_AGENT_INDEX) return result;
        for (int idx : agent_index_[agent_id]) {
            if (events_[idx].tick >= from && events_[idx].tick <= to)
                result.push_back(&events_[idx]);
        }
        return result;
    }

    // Events matching a category
    std::vector<const ChronicleEvent*> by_category(EventCategory cat) const {
        std::vector<const ChronicleEvent*> result;
        for (auto& ev : events_) {
            if (category_of(ev.type) == cat)
                result.push_back(&ev);
        }
        return result;
    }

    // Last N events (for scrolling display)
    std::vector<const ChronicleEvent*> last(int n) const {
        std::vector<const ChronicleEvent*> result;
        int start = std::max(0, (int)events_.size() - n);
        for (int i = start; i < (int)events_.size(); i++) {
            result.push_back(&events_[i]);
        }
        return result;
    }

    // Last N events involving a specific agent
    std::vector<const ChronicleEvent*> last_for_agent(int agent_id, int n) const {
        auto all = by_agent(agent_id);
        if ((int)all.size() <= n) return all;
        return {all.end() - n, all.end()};
    }

    // Generic filter
    std::vector<const ChronicleEvent*> filter(
        std::function<bool(const ChronicleEvent&)> pred) const
    {
        std::vector<const ChronicleEvent*> result;
        for (auto& ev : events_) {
            if (pred(ev)) result.push_back(&ev);
        }
        return result;
    }

    // --- Stats ---
    size_t size() const { return events_.size(); }
    bool empty() const { return events_.empty(); }
    int count_for_agent(int agent_id) const {
        if (agent_id < 0 || agent_id >= MAX_AGENT_INDEX) return 0;
        return (int)agent_index_[agent_id].size();
    }
    int count_of_type(EventType type) const {
        int ti = (int)type;
        if (ti >= (int)EventType::COUNT) return 0;
        return (int)type_index_[ti].size();
    }

    // --- Narrative helpers ---

    // Generate a timeline summary for an agent (their "story so far")
    std::string agent_timeline(int agent_id, const char* archetype_name = nullptr) const {
        std::string out;
        char buf[128];
        if (archetype_name) {
            std::snprintf(buf, sizeof(buf), "--- Agent %d (%s) ---\n", agent_id, archetype_name);
        } else {
            std::snprintf(buf, sizeof(buf), "--- Agent %d ---\n", agent_id);
        }
        out += buf;

        auto events = by_agent(agent_id);
        for (auto* ev : events) {
            out += "  " + ev->summary() + "\n";
        }
        if (events.empty()) out += "  (no recorded events)\n";
        return out;
    }

    // Generate a summary of all deaths with causes
    std::string death_report() const {
        std::string out = "--- Deaths ---\n";
        auto deaths = filter([](const ChronicleEvent& e) {
            return e.type == EventType::DIED_STARVATION
                || e.type == EventType::DIED_EXHAUSTION
                || e.type == EventType::DIED_BREAKDOWN
                || e.type == EventType::DIED_COLLAPSE
                || e.type == EventType::DIED_SUICIDE;
        });
        for (auto* d : deaths) {
            out += "  " + d->summary() + "\n";
        }
        if (deaths.empty()) out += "  (no deaths)\n";
        return out;
    }

    // Generate a summary of major narrative milestones
    std::string narrative_summary() const {
        std::string out = "--- Narrative ---\n";
        auto milestones = filter([](const ChronicleEvent& e) {
            return category_of(e.type) == EventCategory::NARRATIVE
                || e.type == EventType::FACTION_FORMED
                || e.type == EventType::FIRST_DEATH
                || e.type == EventType::FIRST_SABOTAGE
                || e.type == EventType::REDEMPTION
                || e.type == EventType::DIED_SUICIDE;
        });
        for (auto* m : milestones) {
            out += "  " + m->summary() + "\n";
        }
        if (milestones.empty()) out += "  (no milestones yet)\n";
        return out;
    }

private:
    static constexpr int MAX_AGENT_INDEX = 64;
    std::deque<ChronicleEvent> events_;
    std::vector<int> agent_index_[MAX_AGENT_INDEX] = {};
    std::vector<int> type_index_[(int)EventType::COUNT] = {};
};
