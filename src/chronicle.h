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

    // First-person narrative from agent's POV
    std::string narrative(Archetype arch, StressState state) const {
        if (agent_id < 0) return "> " + text;

        const char* mood = "";
        switch (state) {
            case StressState::BROKEN:          mood = "... "; break;
            case StressState::DISSOCIATED:     mood = ""; break;
            case StressState::HOSTILE_EUPHORIA: mood = "* "; break;
            case StressState::REDEEMED:        mood = "~ "; break;
            default: break;
        }

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s%s", mood, narrative_text(arch));
        return buf;
    }

private:
    const char* narrative_text(Archetype arch) const {
        (void)arch;
        switch (type) {
            case EventType::SPAWNED:        return "I woke up. The factory hums around me.";
            case EventType::DIED_STARVATION:return "I can't... the hunger took me.";
            case EventType::DIED_EXHAUSTION:return "So tired... I just need to close my eyes.";
            case EventType::DIED_BREAKDOWN: return "I can't take this anymore.";
            case EventType::DIED_COLLAPSE:  return "The ceiling... it's coming down!";
            case EventType::DIED_SUICIDE:   return "This machine ate everything I was.";

            case EventType::STRESS_STATE_CHANGE: return text.c_str();
            case EventType::TRAUMA_GAINED:  return "Something broke inside me.";
            case EventType::BREAKDOWN:      return "I can't... I just can't.";
            case EventType::REDEMPTION:     return "I see them suffering. I have to help.";
            case EventType::SABOTAGE:       return "If I break it, maybe they'll listen.";

            case EventType::OPINION_SHIFT:  return "I'm starting to see things differently.";
            case EventType::TRUST_MILESTONE:return "I think I can trust them now.";
            case EventType::FOOD_SHARED:    return "Here. Take this. We're in this together.";
            case EventType::FACTION_FORMED: return "We found each other. We stand together.";
            case EventType::FACTION_JOINED: return "I'm not alone anymore.";
            case EventType::FACTION_LEFT:   return "I have to walk my own path.";

            case EventType::BUILT_MACHINE:     return "I built something real today.";
            case EventType::BUILT_CONVEYOR:    return "Another piece of the machine takes shape.";
            case EventType::BUILT_EATING_ZONE: return "A place to eat. That's something.";
            case EventType::WORK_COMPLETED:    return "Done. Another shift, another quota.";
            case EventType::GATHERED:          return "Found something useful out there.";
            case EventType::MAINTAINED:        return "I keep things running. That's my job.";
            case EventType::DISMANTLED:        return "We don't need this anymore.";

            case EventType::MACHINE_ACTIVATED:  return "It lives. The machine breathes.";
            case EventType::MACHINE_BROKE:      return "Another one down. We're losing ground.";
            case EventType::FACTORY_RESTRUCTURE: return "The walls are shifting again.";
            case EventType::FACTORY_CONFISCATED: return "They took it. They always take.";
            case EventType::FACTORY_SEALED_SPACE: return "There are places here we can't go.";
            case EventType::ARTIFACT_CREATED:    return "I made something... beautiful.";
            case EventType::HIDDEN_SPACE_FOUND:  return "What's behind here?";
            case EventType::QUOTA_MILESTONE:     return "We made quota. For once.";

            case EventType::FIRST_BUILD:      return "The first thing anyone built here.";
            case EventType::FIRST_DEATH:       return "Someone died. The first, but not the last.";
            case EventType::FIRST_SABOTAGE:    return "Someone fought back.";
            case EventType::FIRST_FACTION:     return "A group formed. Strength in numbers.";
            case EventType::FIRST_ARTIFACT:    return "Someone created art in this place.";
            case EventType::POPULATION_MILESTONE: return "More of us now. Or fewer.";
            case EventType::CRISIS_PERIOD:     return "Everything is falling apart.";

            default: return text.c_str();
        }
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

    // One-line arc summary for an agent
    std::string agent_arc(int agent_id, const char* archetype_name = nullptr) const {
        auto evs = by_agent(agent_id);
        if (evs.empty()) return "(no events)";

        int built = 0, gathered = 0, worked = 0, shared = 0, sabotaged = 0;
        int first_tick = evs.front()->tick;
        int last_tick = evs.back()->tick;
        std::string death;

        for (auto* ev : evs) {
            switch (ev->type) {
                case EventType::BUILT_MACHINE:
                case EventType::BUILT_CONVEYOR:
                case EventType::BUILT_EATING_ZONE:
                    built++; break;
                case EventType::GATHERED:     gathered++; break;
                case EventType::WORK_COMPLETED: worked++; break;
                case EventType::FOOD_SHARED:  shared++; break;
                case EventType::SABOTAGE:     sabotaged++; break;
                case EventType::DIED_STARVATION: death = "starvation"; break;
                case EventType::DIED_EXHAUSTION: death = "exhaustion"; break;
                case EventType::DIED_BREAKDOWN:  death = "breakdown"; break;
                case EventType::DIED_COLLAPSE:   death = "collapse"; break;
                case EventType::DIED_SUICIDE:    death = "suicide"; break;
                default: break;
            }
        }

        char buf[256];
        const char* arch = archetype_name ? archetype_name : "?";
        if (death.empty()) {
            std::snprintf(buf, sizeof(buf),
                "%s. Lived %d ticks. Built %d, gathered %d, worked %d, shared %d.%s%s",
                arch, last_tick - first_tick,
                built, gathered, worked, shared,
                sabotaged > 0 ? " Sabotaged." : "",
                evs.size() > 50 ? " (eventful life)" : "");
        } else {
            std::snprintf(buf, sizeof(buf),
                "%s. Lived %d ticks. Built %d, gathered %d, worked %d. Died of %s at tick %d.%s%s",
                arch, last_tick - first_tick,
                built, gathered, worked, death.c_str(), last_tick,
                sabotaged > 0 ? " Sabotaged." : "",
                shared > 0 ? " Shared food." : "");
        }
        return buf;
    }

    // Narrative timeline for an agent (first-person), limited lines
    std::string agent_journal(int agent_id, int head = 3, int tail = 15) const {
        auto evs = by_agent(agent_id);
        std::string out;
        if (evs.empty()) return "  (no memories)\n";

        // Show first N and last M events, with ellipsis
        int shown_head = std::min(head, (int)evs.size());
        int shown_tail = std::min(tail, (int)evs.size() - shown_head);
        if (shown_tail < 0) shown_tail = 0;

        char buf[64];
        for (int i = 0; i < shown_head; i++) {
            std::snprintf(buf, sizeof(buf), "[%5d] ", evs[i]->tick);
            out += "  " + std::string(buf) + evs[i]->text + "\n";
        }
        if ((int)evs.size() > shown_head + shown_tail) {
            std::snprintf(buf, sizeof(buf), "  ... (%d more) ...\n",
                (int)evs.size() - shown_head - shown_tail);
            out += buf;
        }
        for (int i = (int)evs.size() - shown_tail; i < (int)evs.size(); i++) {
            if (i < shown_head) continue;
            std::snprintf(buf, sizeof(buf), "[%5d] ", evs[i]->tick);
            out += "  " + std::string(buf) + evs[i]->text + "\n";
        }
        return out;
    }

    // Ex-post analysis: faction arcs
    std::string faction_arcs() const {
        std::string out = "--- Faction Arcs ---\n";
        auto faction_events = filter([](const ChronicleEvent& e) {
            return e.type == EventType::FACTION_FORMED
                || e.type == EventType::FACTION_JOINED
                || e.type == EventType::FACTION_LEFT;
        });
        if (faction_events.empty()) { out += "  (no factions formed)\n"; return out; }
        for (auto* ev : faction_events) {
            out += "  " + ev->summary() + "\n";
        }
        return out;
    }

    // Ex-post analysis: crisis periods (STRESS_STATE_CHANGE events, breakdowns)
    std::string crisis_timeline() const {
        std::string out = "--- Crisis Timeline ---\n";
        auto crises = filter([](const ChronicleEvent& e) {
            return e.type == EventType::BREAKDOWN
                || e.type == EventType::SABOTAGE
                || e.type == EventType::DIED_SUICIDE
                || e.type == EventType::CRISIS_PERIOD;
        });
        if (crises.empty()) { out += "  (no crises recorded)\n"; return out; }
        for (auto* ev : crises) {
            out += "  " + ev->summary() + "\n";
        }
        return out;
    }

    // Ex-post analysis: event type distribution
    std::string event_distribution() const {
        std::string out = "--- Event Distribution ---\n";
        char buf[128];
        for (int i = 0; i < (int)EventType::COUNT; i++) {
            int c = (int)type_index_[i].size();
            if (c == 0) continue;
            std::snprintf(buf, sizeof(buf), "  %-22s %4d\n", event_type_name((EventType)i), c);
            out += buf;
        }
        return out;
    }

    // JSONL: one JSON object per event, for external analysis
    std::string to_jsonl() const {
        std::string out;
        out.reserve(events_.size() * 200);
        char buf[512];
        for (auto& ev : events_) {
            // Escape quotes in text
            std::string escaped;
            escaped.reserve(ev.text.size());
            for (char c : ev.text) {
                if (c == '"') escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else escaped += c;
            }
            const char* cat_name = "WORLD";
            switch (category_of(ev.type)) {
                case EventCategory::LIFECYCLE:  cat_name = "LIFECYCLE"; break;
                case EventCategory::STRESS:     cat_name = "STRESS"; break;
                case EventCategory::SOCIAL:     cat_name = "SOCIAL"; break;
                case EventCategory::PRODUCTION: cat_name = "PRODUCTION"; break;
                case EventCategory::WORLD:      cat_name = "WORLD"; break;
                case EventCategory::NARRATIVE:  cat_name = "NARRATIVE"; break;
                default: break;
            }
            std::snprintf(buf, sizeof(buf),
                "{\"tick\":%d,\"type\":\"%s\",\"category\":\"%s\",\"agent\":%d,"
                "\"x\":%d,\"y\":%d,\"value\":%.4f,\"ref\":%d,\"text\":\"%s\"}\n",
                ev.tick, event_type_name(ev.type), cat_name, ev.agent_id,
                ev.x, ev.y, ev.value, ev.ref_id, escaped.c_str());
            out += buf;
        }
        return out;
    }

private:
    static constexpr int MAX_AGENT_INDEX = 64;
    std::deque<ChronicleEvent> events_;
    std::vector<int> agent_index_[MAX_AGENT_INDEX] = {};
    std::vector<int> type_index_[(int)EventType::COUNT] = {};
};
