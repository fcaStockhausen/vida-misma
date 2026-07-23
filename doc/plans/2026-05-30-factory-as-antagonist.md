# La Vida Misma — The Factory as Antagonist

> [!WARNING]
> **HISTORICAL AND SUPERSEDED PLAN.** This document preserves the May 2026
> proposal as a decision record; it is not the current implementation plan or
> canonical ontology. Its strategic factory, Watcher, faction privileges and
> targeted retaliation were superseded by
> `2026-07-21-alineacion-diseno-implementacion.md`. Canonical policy is an
> indifferent institution that cannot inspect identities, relationships,
> communities or the meaning of actions. Do not implement the tasks below as
> current requirements; `external.policy_variant = 0` retains only selected
> legacy behavior for explicit historical A/B comparisons.

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Transform the factory from a passive backdrop into an active antagonist. Make CREATE, EXPLORE, and SOCIALIZE into acts of resistance, not leisure. Every mechanic must reinforce the central tension: the factory demands output, the agents want to live.

**Architecture:** Three layers of change. (A) The factory escalates — it demands more, restructures itself, punishes non-compliance. (B) Agent drives become subversive — expression and curiosity actively erode factory control. (C) Emergent narrative — the clash between these forces produces stories: slowdowns, cultural movements, factory retributions.

**Tech Stack:** C++20, EnTT ECS, existing codebase. No new dependencies.

---

## Diagnosis: What's Wrong Now

The simulation currently rewards compliance. Agents who GATHER, BUILD, and WORK survive. Agents who CREATE, EXPLORE, or SOCIALIZE die. The factory is a puzzle to be solved, not a force to be resisted. The "productivity trap" (agents work themselves to death) is a balance problem, not a design feature.

**The fix is not tuning numbers. The fix is making the factory's demands and the agents' humanity structurally opposed.**

---

## Phase A: The Factory Escalates

The factory should feel like it has a will of its own — not intelligent, but relentless and indifferent.

### Task A1: Quota Escalation Over Time

**Objective:** The factory demands more as time passes. It never stops asking for more.

**Files:**
- Modify: `src/config.h` — add `quota_growth_rate`
- Modify: `src/simulation.h` — add `current_quota_per_tick` field
- Modify: `src/simulation.cpp` — constructor initializes `current_quota_per_tick = config_.quota_per_tick`; tick loop grows it

**Step 1:** Add to `Config`:
```cpp
float quota_growth_rate = 0.0001f; // quota increases per tick
```

**Step 2:** Add to `Simulation`:
```cpp
float current_quota_per_tick_;
```

**Step 3:** In `advance()`, before `system_ship_out_food()`:
```cpp
current_quota_per_tick_ += config_.quota_growth_rate;
```

**Step 4:** Change `system_ship_out_food()` to use `current_quota_per_tick_` instead of `config_.quota_per_tick`.

**Step 5:** In `batch_main.cpp`, display current quota in timeline:
```cpp
std::printf("Quota: %.3f\n", sim.current_quota());
```

**Verification:** Run `./build/vida_batch 3000`. Confirm quota increases from 0.10 to ~0.40 over 3000 ticks. Agents should start failing quota around tick 1000-1500.

**Commit:** `feat: quota escalation — factory demands more over time`

---

### Task A2: Factory Restructuring Events

**Objective:** Periodically, the factory reconfigures itself — conveyor directions change, storages relocate, machines break. Agents cannot predict or control this.

**Files:**
- Modify: `src/config.h` — add `restructure_interval`, `restructure_probability`
- Modify: `src/simulation.h` — add `system_factory_restructure()` declaration
- Modify: `src/simulation.cpp` — implement restructure system, call in `advance()`

**Step 1:** Add to `Config`:
```cpp
int restructure_interval = 500;  // ticks between restructure checks
float restructure_probability = 0.3f; // chance per check
```

**Step 2:** Implement `system_factory_restructure()`:
```cpp
void Simulation::system_factory_restructure() {
    if (tick_ % config_.restructure_interval != 0) return;
    if (rng_(0.0f, 1.0f) > config_.restructure_probability) return;

    // Pick a random restructuring effect:
    // 1. Reverse a random conveyor direction
    // 2. Degrade a random machine (build_progress -= 0.3)
    // 3. Drain food from a random storage (the factory "confiscates")
    int effect = rng_(0, 2);
    // ... implementation per effect
}
```

**Step 3:** Call in `advance()` after `system_factory_deterioration()`.

**Verification:** Run batch, observe log messages like "factory restructured: conveyor at (23,7) reversed". Agents should need to adapt.

**Commit:** `feat: factory restructuring — the factory reconfigures itself`

---

### Task A3: The Watcher — Compliance Surveillance

**Objective:** An abstract surveillance mechanic. Agents who spend too many consecutive ticks on non-productive actions (CREATE, EXPLORE, SOCIALIZE, REST when not exhausted) accumulate "noncompliance". High noncompliance increases stress and reduces trust with nearby high-compliance agents (Foremen report them).

**Files:**
- Modify: `src/components.h` — add `noncompliance` float to AgentComponent
- Modify: `src/sim_execute.cpp` — non-productive actions increase noncompliance, productive actions decrease it
- Modify: `src/social.h` — high-compliance agents nearby reduce trust with high-noncompliance agents

**Step 1:** Add to `AgentComponent`:
```cpp
float noncompliance = 0.0f;  // [0, 1] how much the factory "notices" this agent slacking
```

**Step 2:** In `system_execute_actions()`, at the end of each action:
```cpp
bool productive = (action.current == ActionType::GATHER ||
                   action.current == ActionType::BUILD ||
                   action.current == ActionType::WORK ||
                   action.current == ActionType::MAINTAIN);
if (productive) {
    agent.noncompliance = std::max(0.0f, agent.noncompliance - 0.02f);
} else {
    agent.noncompliance = std::min(1.0f, agent.noncompliance + 0.01f);
}
```

**Step 3:** High-noncompliance agents get extra stress:
```cpp
// In system_update_stress():
stress_input += config_.stress_noncompliance * agent.noncompliance;
```

**Step 4:** Foremen (high compliance + high influence) notice and distrust noncompliant neighbors:
```cpp
// In social system: if foreman near noncompliant agent, trust drops
if (osoc.influence > 0.3f && personality.compliance > 0.7f && agent.noncompliance > 0.5f) {
    social_.negative_interaction(oag.id, ag.id, tick_, 0.02f);
}
```

**Verification:** Agents who CREATE/EXPLORE frequently should show rising noncompliance and stress. Foremen should distrust them.

**Commit:** `feat: the watcher — surveillance and noncompliance tracking`

---

## Phase B: Agent Resistance

The agents' "human" actions (CREATE, EXPLORE, SOCIALIZE) should not be leisure — they should be how agents push back against the factory.

### Task B1: CREATE Produces Cultural Artifacts

**Objective:** When an agent CREATES, it produces an "artifact" at its location. Artifacts spread through the social network: nearby agents who encounter artifacts get mood boosts that persist even under factory pressure. Artifacts are invisible to the factory (don't affect quota, don't take tile space).

**Files:**
- Modify: `src/components.h` — add `ArtifactComponent` (position, creator_id, strength, age)
- Modify: `src/sim_execute.cpp` — CREATE spawns artifact
- Modify: `src/social.h` — `process_encounter_with_artifact()` boosts mood
- Modify: `src/simulation.cpp` — new system `system_artifact_decay()` and `system_artifact_effects()`

**Step 1:** Add component:
```cpp
struct ArtifactComponent {
    int creator_id = -1;
    float strength = 1.0f;  // decays over time
    int age = 0;            // ticks since creation
};
```

**Step 2:** In CREATE execution:
```cpp
auto artifact = registry_.create();
registry_.emplace<PositionComponent>(artifact, pos.x, pos.y);
registry_.emplace<ArtifactComponent>(artifact, agent.id, 1.0f, 0);
```

**Step 3:** New system `system_artifact_effects()`:
```cpp
// Agents near artifacts get mood boost
for (auto [ae, apos, aart] : artifacts) {
    for (auto e : alive) {
        auto& p = registry_.get<PositionComponent>(e);
        int d = std::abs(p.x - apos.x) + std::abs(p.y - apos.y);
        if (d <= 2) {
            auto& soc = registry_.get<SocialComponent>(e);
            // Mood boost that resists factory pressure
            soc.mood = std::min(1.0f, soc.mood + aart.strength * 0.05f);
        }
    }
}
```

**Step 4:** Artifacts decay: strength -= 0.002 per tick. Remove when strength <= 0.

**Verification:** After implementing, run batch. Agents near artifacts should maintain higher mood. CREATE becomes a community act, not selfish expression.

**Commit:** `feat: cultural artifacts — CREATE produces mood-boosting objects`

---

### Task B2: EXPLORE Discovers Hidden Spaces

**Objective:** When an agent EXPLORES, it has a chance to "discover" a hidden space — a tile that becomes a sanctuary where factory noncompliance doesn't accumulate and stress decays faster. Hidden spaces are fragile: if too many agents cluster there, the factory "notices" and the space reverts.

**Files:**
- Modify: `src/components.h` — add `TileType::HiddenSpace`
- Modify: `src/grid.h` — hidden space tile type
- Modify: `src/sim_execute.cpp` — EXPLORE action can create HiddenSpace
- Modify: `src/sim_utility.cpp` — agents on HiddenSpace get noncompliance freeze + stress decay
- Modify: `src/simulation.cpp` — new system `system_hidden_space_exposure()`

**Step 1:** Add tile type:
```cpp
HiddenSpace,  // discovered by explorers; sanctuary from factory
```

**Step 2:** In EXPLORE execution:
```cpp
if (grid_.at(pos.x, pos.y) == TileType::OpenSpace) {
    float discovery_chance = personality.curiosity * 0.02f;
    if (rng_(0.0f, 1.0f) < discovery_chance) {
        grid_.set(pos.x, pos.y, TileType::HiddenSpace);
        emit_log(agent.id, "discovered a hidden space");
    }
}
```

**Step 3:** Agents on HiddenSpace tiles:
```cpp
// In execute: noncompliance doesn't accumulate
// In stress update: stress decays 3x faster
```

**Step 4:** Factory exposure — if more than 2 agents stand on same HiddenSpace for 10+ ticks:
```cpp
// The factory notices: tile reverts to Floor, stress spike for agents there
emit_log(-1, "the factory sealed a hidden space at (...)");
```

**Verification:** Explorers should create hidden spaces. Agents who find them should have lower stress. Spaces should be destroyed if overused.

**Commit:** `feat: hidden spaces — explorers discover sanctuaries from the factory`

---

### Task B3: SOCIALIZE Builds Organized Resistance

**Objective:** When agents with high mutual trust socialize repeatedly, they form a "faction". Faction members share food more generously, get collaboration bonuses, and can collectively ignore the factory's demands (noncompliance doesn't accumulate when surrounded by faction members). The factory notices large factions and may restructure to disrupt them.

**Files:**
- Modify: `src/social.h` — add faction detection and tracking
- Modify: `src/components.h` — add `faction_id` to AgentComponent
- Modify: `src/sim_execute.cpp` — faction food sharing bonus, faction noncompliance shield
- Modify: `src/simulation.cpp` — new system `system_faction_formation()`

**Step 1:** Add to `AgentComponent`:
```cpp
int faction_id = -1;  // -1 = no faction
```

**Step 2:** Faction formation — in social system, check if trust cluster exists:
```cpp
// A faction forms when 3+ agents all have mutual trust > 0.4
// Assign same faction_id to all members
```

**Step 3:** Faction benefits:
- Food sharing threshold reduced (share with faction members even when not very hungry)
- Noncompliance shield: if 2+ faction members are adjacent, noncompliance doesn't increase
- Collaboration bonus between faction members is doubled

**Step 4:** Factory retaliation — if faction has 5+ members, next restructure event specifically targets their most-used tiles.

**Verification:** Agents who socialize with trusted agents should form factions. Factions should be visible in batch output. Large factions trigger factory restructuring.

**Commit:** `feat: factions — trust networks become organized resistance`

---

### Task B4: The Meaning Need — Productivity vs Fulfillment

**Objective:** Add a new need: `meaning`. Factory work does NOT satisfy meaning. Only CREATE (artifacts), deep social bonds (factions), and discovering hidden spaces satisfy meaning. Low meaning causes a unique effect: the agent's compliance gradually decreases (they stop believing in the factory). This is the core tragedy — being productive makes you hollow.

**Files:**
- Modify: `src/components.h` — add `meaning` to NeedsComponent
- Modify: `src/config.h` — add `meaning_decay`, `meaning_from_artifact`, `meaning_from_faction`, `meaning_from_discovery`
- Modify: `src/sim_execute.cpp` — factory actions don't satisfy meaning; resistance actions do
- Modify: `src/sim_utility.cpp` — low meaning reduces compliance (temporary modifier)
- Modify: `src/simulation.cpp` — decay meaning each tick

**Step 1:** Add need:
```cpp
float meaning = 0.0f;  // [0, 1], 1 = fulfilled
```

**Step 2:** Decay per tick:
```cpp
needs.meaning = std::min(1.0f, needs.meaning + config_.meaning_decay); // 0.001 per tick
```

**Step 3:** Fulfillment sources:
- CREATE near artifact: meaning -= 0.03
- Join/interact with faction member: meaning -= 0.02
- Discover hidden space: meaning -= 0.15 (one-time burst)
- GATHER/BUILD/WORK: meaning does NOT decrease (factory doesn't provide meaning)

**Step 4:** Low meaning erodes compliance:
```cpp
// In utility computation:
float effective_compliance = personality.compliance;
if (needs.meaning > 0.7f) {
    effective_compliance *= (1.0f - (needs.meaning - 0.7f) * 1.5f);
    // At meaning=1.0, compliance is reduced by 45%
}
```

**Step 5:** Low meaning + high compliance = crisis state (special stress input):
```cpp
// The tragic state: you do everything right but feel nothing
if (needs.meaning > 0.6f && personality.compliance > 0.7f) {
    stress_input += 0.003f; // "burnout from meaninglessness"
}
```

**Verification:** Productive agents (Workers, Foremen) should develop high meaning need over time. Their compliance should erode. Artisans and Explorers who CREATE/discover should maintain lower meaning need. The most tragic agents are those who are both productive and unfulfilled.

**Commit:** `feat: meaning need — factory work doesn't satisfy, only resistance does`

---

## Phase C: Emergent Narrative

These systems should create stories without scripting. The mechanics above should interact to produce recognizable patterns.

### Task C1: The Slowdown

**Emergent behavior:** When a faction forms and enough agents have high meaning need, they spontaneously reduce WORK output. The factory notices (quota missed) and escalates (restructure). This creates a cycle: oppression → resistance → retaliation.

**No new code needed** — this emerges from:
- Task B4 (low meaning → reduced compliance → less WORK)
- Task B3 (factions shield noncompliance)
- Task A1 (quota escalation punishes low output)
- Task A2 (factory restructures in response)

**Verification:** In batch output, look for patterns where:
1. Faction forms around tick 500-800
2. Faction members' meaning need rises (they socialize more, work less)
3. Quota misses increase
4. Factory restructures
5. Faction may break apart or grow stronger

---

### Task C2: Batch Output Enhancement for Narrative

**Objective:** Add reporting that reveals the story: faction events, artifact counts, hidden space discovery/destruction, meaning crisis statistics.

**Files:**
- Modify: `src/batch_main.cpp` — add section after TURNOVER

**Step 1:** Add to end-of-run report:
```
NARRATIVE:
  Artifacts created:     47 (12 still active)
  Hidden spaces found:   3 (1 sealed by factory)
  Factions formed:       2 (largest: 5 members)
  Agents in meaning crisis: 8/24
  Factory restructures:  4
  Quota: 0.10 -> 0.38
```

**Step 2:** In timeline, add columns for narrative markers:
```
  tick alive | Factions | Artifacts | Hidden | Quota  | Noncomp avg
```

**Verification:** Batch output tells a story. You can see the arc: early compliance → faction formation → resistance → factory retaliation.

**Commit:** `feat: narrative reporting — batch output reveals the story`

---

## Implementation Order

```
A1: Quota escalation           (30 min)
A2: Factory restructuring      (45 min)
A3: The Watcher / noncompliance (30 min)
B1: Cultural artifacts          (45 min)
B2: Hidden spaces               (45 min)
B3: Factions                    (60 min)
B4: Meaning need                (30 min)
C2: Batch narrative output      (30 min)
```

Total estimated: ~5.5 hours of focused implementation.

Each task is independent — they can be implemented and tested in isolation. The emergent narrative (C1) requires all of A+B to be in place.

---

## Success Criteria

The implementation is successful when batch runs produce these patterns:

1. **The compliant die inside.** Foremen and Workers who never CREATE or EXPLORE develop meaning crisis. They work themselves into stress breakdowns despite being "productive".

2. **Resistance is costly but meaningful.** Agents who CREATE and EXPLORE maintain better mood and lower stress, but they accumulate noncompliance. The factory punishes them via Foremen distrust and restructuring.

3. **Factions are powerful but fragile.** A faction of 5 agents can shield each other from noncompliance, but the factory restructures to break them up. The tension between collective power and factory retaliation drives the narrative.

4. **The quota always wins eventually.** Quota escalation means no colony survives forever. The question isn't "do they survive?" but "what happened before they didn't?"

5. **Every death tells a story.** The batch output should make it clear: this agent died of meaning crisis burnout. That agent died protecting a hidden space. This faction was broken up by a restructure.
