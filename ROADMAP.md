# La Vida Misma — Project Roadmap

A factory simulation where agents inhabit a factory they did not build, do not
control, and do not understand. The factory demands output. The agents want to
live. Every mechanic exists to sharpen that tension.

---

## Current State

**~9,600 LOC** across 24 source files. Compiles clean (Clang, C++20, CMake).
Two targets: `vida_batch` (headless benchmark), `vida_gui` (SDL2 2.5D isometric).
48 agents per simulation. Director system with CALM and NORMAL modes.

### Architecture of Desire (implemented)

```
Maslow hierarchy gates:
  hunger/rest high ──► survival overrides everything
  hunger/rest low  ──► higher needs activate

Focus system (Dwarf Fortress):
  higher needs unmet ──► WORK/GATHER/BUILD lose utility (distracted)
  only fires when food supply is healthy

Threshold gates (The Sims):
  CREATE needs expression > 0.15
  SOCIALIZE needs social > 0.15
  EXPLORE needs purpose > 0.25
  Below threshold = near-zero utility. Above = ramps hard.

Purpose decoupled from work:
  CREATE satisfies purpose (+0.006/tick)
  SOCIALIZE satisfies purpose (+0.003/tick)
  WORK satisfies purpose (+0.004/tick) — no longer the sole source

Personality-modulated action duration:
  Artisan stays 60 ticks in CREATE, Foreman 30+15*comp in WORK
  Actions don't change every tick — behavior is visible and readable
```

Culture diagnostic correlations (48 agents, calm mode, 2000 ticks):

| Trait → Action | r (Pearson) |
|---|---|
| artistry → CREATE | **0.86** |
| curiosity → EXPLORE | **0.78** |
| gregariousness → SOCIALIZE | **0.42** |
| compliance → WORK | 0.23 |

### 3-Tier Production Chain

```
ScrapPile ──GATHER──► raw_material
    │
    ▼
MaterialsMachine: raw_material → construction_material
    │                   │
    │                   ├──► agent inventory (build conveyors, machines)
    │                   └──► adjacent Storage
    │                                      │
    ▼                                      ▼
OutputMachine: construction_material → output
    │                                       │
    ├──► conveyor chain ──────────────────► │
    ├──► agent hauling (output inventory) ─►│
    └──► adjacent Storage (fallback)        │
                                            ▼
                              Exit-adjacent Storage
                                            │
                                            ▼
                              system_ship_out_food (quota drain)
```

---

## Completed Features

### Core Simulation
- [x] ECS architecture via EnTT (header-only)
- [x] 60x40 grid with WFC procedural layout
- [x] Tick-based simulation loop, seeded RNG
- [x] Config-driven parameters via TOML
- [x] 48 agents with balanced archetype distribution

### Agent Systems
- [x] Utility AI with 12 action types (GATHER, BUILD, WORK, EAT, REST, SOCIALIZE, CREATE, EXPLORE, GET_FOOD, MAINTAIN, DISMANTLE, SABOTAGE)
- [x] Need decay: hunger, rest, social, expression, purpose, meaning
- [x] Skill system: factory_work, domestic, artistic, social_skill with XP
- [x] Disease system: immune response, hunger multiplier, recovery
- [x] Personality-modulated action stickiness (Artisan stays in CREATE, Foreman in WORK)
- [x] Focus system: unmet higher needs reduce WORK/GATHER/BUILD utility
- [x] Threshold gates: cultural actions activate above need threshold
- [x] Purpose decoupled from work (CREATE and SOCIALIZE also satisfy purpose)

### Stress & Trauma
- [x] 5 stress states: NORMAL → DISSOCIATED → EUPHORIC → BROKEN → REDEEMED
- [x] Permanent trauma, noncompliance tracking
- [x] Sabotage action, redemption arc, suicide risk
- [x] 4 death causes: starvation, exhaustion, breakdown, factory collapse
- [x] Cultural artifacts: CREATE spawns mood-boosting objects
- [x] Hidden spaces: factory seals overused rest areas
- [x] 6 personality archetypes with per-agent jitter

### Production Chain (3-tier)
- [x] MaterialsMachine: raw_material → construction_material
- [x] OutputMachine: construction_material → output
- [x] Agent hauling: output → Exit-adjacent Storage
- [x] Conveyor belt transport (Exit-only output deposit)
- [x] External quota system (radius 3 drain from Exit)
- [x] Factory health, machine breaks, conveyor condition decay
- [x] `machine_connected_to_exit()` flow verification
- [x] Dynamic routing based on construction_material supply
- [x] `infra_gap` reform: need-based, not total-tiles-based

### Pathfinding
- [x] A* with per-agent `PathCache` (invalidates on target change or 20 ticks)
- [x] All non-Wall tiles walkable, divergence recovery

### Social Fabric
- [x] Relationship graph: familiarity, trust per agent pair
- [x] Social learning, food sharing, collaborative BUILD/WORK
- [x] Emotional contagion, influence/leadership, mood
- [x] Grief cascade, relationship decay
- [x] Opinion dynamics (bounded confidence + DeGroot)
- [x] Faction formation and trust modulation

### Director System
- [x] CALM mode: no quota, no deterioration, no restructure — pure observation
- [x] NORMAL mode: standard factory pressure with quota escalation

### Interface
- [x] Headless batch runner: run, calm, culture, story, agent, analysis, map, jsonl
- [x] SDL2 2.5D isometric GUI (120ms/tick for readability)
- [x] Config path fallback (works from build/ directory)

---

## In Progress / Needs Work

### Balance Tuning
- [ ] **GATHER dominates** (36-53% of agent time) — next `infra_gap`-like to reform
- [ ] **SOCIALIZE frequency low** (0-3%) — correlation correct but agents too dispersed
- [ ] **Logistics throughput** caps quota at ~33% avg in NORMAL mode
- [ ] **Seed variance**: layout-dependent outcomes (food/machine placement)

---

## Planned Features (by priority)

### Priority 1: Congregation Spaces & Social Emergence

The core problem: agents don't form community because they never gather in
the same place. Socialization requires proximity (radius 6) but agents are
dispersed across the map gathering and building.

Design (inspired by DF meeting zones and The Sims community lots):

- [ ] **EatingZone as congregation point**: agents with high social need path
      to EatingZone even when not hungry. EatingZones "advertise" social value.
      Multiple agents eating simultaneously get social satisfaction bonus.
- [ ] **Gathering zones**: new tile type or EatingZone extension where agents
      with high social/expression needs congregate. Not designated by the
      factory — emerges from agent density (Schelling dynamics).
      - Agents near 3+ others get social satisfaction passive tick
      - High-artistry agents near other creatives form "studio" zones
      - High-gregariousness agents form "plaza" zones
- [ ] **Artifact attraction**: cultural artifacts boost mood of nearby agents
      (already implemented) AND attract high-artistry agents to the location.
      Creates emergent "galleries" — places agents visit for inspiration.
- [ ] **Communal eating bonus**: when 3+ agents eat simultaneously in an
      EatingZone, all get +social satisfaction and mood boost. Makes
      synchronized eating socially valuable, not just nutritionally.
- [ ] **Sleeping/nesting**: agents that REST repeatedly in the same area
      develop affinity for that location. Creates "neighborhoods".

Estimated: ~400-600 LOC across `sim_utility.cpp`, `sim_execute.cpp`, `grid.h`

### Priority 2: Construction Recipes & Collaborative Building

Currently agents build blindly — the WFC generator places machine frames and
agents build whatever is nearest. There is no concept of "what the colony
needs next" or "where should this go."

Design (inspired by RimWorld blueprints and DF work orders):

- [ ] **Recipe system**: each structure has a recipe (input materials + skill
      requirement + output). Agents evaluate recipes against colony needs:
      ```
      FoodMachine:   2 raw_material + ScrapPile/FoodSource tile → FoodMachine
      MaterialsMachine: 2 raw_material + ScrapPile tile → MaterialsMachine
      OutputMachine: 1 construction_material + Floor tile → OutputMachine
      Conveyor:      1.5 raw_material + Floor tile → Conveyor
      Storage:       1 raw_material + Floor tile → Storage
      ```
- [ ] **Colony blueprint**: shared state tracking what the colony needs next.
      Agents read the blueprint and prioritize building accordingly:
      - 0 food machines? → FoodMachine is priority #1
      - Output exists but no conveyor to Exit? → Conveyor is priority
      - 3 output machines but 0 connected? → Connect them first
- [ ] **Collaborative construction correction**: agents working on the same
      structure contribute progress faster (already implemented for BUILD).
      Extend to: agents can REDIRECT construction — if a conveyor leads
      nowhere, a nearby agent can dismantle and rebuild it pointing the
      right way. This is the "they can correct between them" behavior.
- [ ] **Construction planning**: agents with high compliance + high influence
      become "foremen" who evaluate the colony's needs and mark tiles for
      construction. Other agents see the marks and build them. This replaces
      the current WFC-frame system with emergent, need-driven planning.
- [ ] **Error correction**: if a conveyor chain is incomplete (dead-end),
      agents with high compliance recognize it and prioritize completing
      or dismantling it. Visual feedback in the GUI (highlight broken chains).

Estimated: ~500-800 LOC across `grid.h`, `sim_execute.cpp`, `sim_utility.cpp`, new `recipes.h`

### Priority 3: Storyteller / Director (Full Implementation)

CALM and NORMAL modes exist. The full Director adds:

- [ ] Track colony capacity (machines, food, agents, logistics state)
- [ ] Scale quota pressure based on capacity — ease off when struggling
- [ ] Introduce events: resource booms, scrap depletion, breakdowns, migrants
- [ ] Create narrative rhythm: calm periods followed by pressure spikes
- [ ] 36 Dramatic Situations (Polti) as event templates:
      - Revolt (faction with low trust sabotages infrastructure)
      - Vengeance (agent witnesses sabotage → vendetta)
      - Sacrifice (REDEEMED agent dies protecting infrastructure)
      - Disaster (factory collapse event)
      - Ambition (agent with high influence accumulates power)
      - Discovery (agent finds hidden space → colony-wide mood event)

Estimated: ~600-800 LOC in `simulation.cpp` + new `director.h`

### Priority 4: Generational Turnover
- [ ] Agent reproduction: high trust + low stress + sufficient food
- [ ] Offspring inherits blended personality
- [ ] Population cap, grief on elder death
- [ ] Social memory through relationship graph only

Estimated: ~600-800 LOC in `simulation.cpp`

---

## Not Planned (deliberately excluded)

- **3D / fluid dynamics / heat simulation** — the 2D grid is sufficient
- **Networking / multiplayer** — single-threaded local simulation
- **Save/load** — can be added later as serialization over ECS state
- **Machine learning for agent behavior** — hand-crafted utility functions are sufficient

---

## Key Design Principles

1. **Conveyors ARE walkable** — agents walk over conveyor belts
2. **Two output transport paths** — conveyor chains + agent hauling
3. **Quota drains only from Exit-adjacent Storage** — no magical teleportation
4. **Deaths are turnover, not failure** — the simulation tracks causes
5. **Config-driven tuning** — all parameters in `config/default.toml`
6. **Archetypes with jitter** — structured diversity, not uniform random
7. **Social mechanics produce emergent coordination** — not scripted
8. **The factory is antagonist** — its demands are structurally opposed to
   the agents' self-actualization. Culture emerges from the cracks.
9. **Personality drives behavior** — artistry→CREATE r=0.86, curiosity→EXPLORE
   r=0.78. The architecture of desire is validated.

---

## Culture Diagnostic Baseline (48 agents, calm mode, 2000 ticks)

| Trait → Action | r (Pearson) | Status |
|---|---|---|
| artistry → CREATE | 0.86 | ✅ Strong |
| curiosity → EXPLORE | 0.78 | ✅ Strong |
| gregariousness → SOCIALIZE | 0.42 | ✅ Correct (frequency low) |
| compliance → WORK | 0.23 | ⚠️ Weak (positive) |

**Main bottleneck for social emergence**: agents are dispersed across the map.
Congregation spaces (Priority 1) will fix this by creating natural gathering
points where socialization, eating, and cultural exchange happen simultaneously.
