# La Vida Misma — Project Roadmap

A factory simulation where agents inhabit a factory they did not build, do not
control, and do not understand. The factory demands output. The agents want to
live. Every mechanic exists to sharpen that tension.

---

## Current State

**~9,600 LOC** across 24 source files. Compiles clean (Clang, C++20, CMake).
Three targets: `vida_batch` (headless benchmark), `vida_gui` (SDL2 2.5D isometric), `vida_gui` (SDL2).

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

Two transport paths for output: conveyor chains and agent hauling.
Both deposit into Exit-adjacent Storage (radius 3), the only quota drain point.

### Latest Batch Results (1000 ticks, 24 agents, 10 seeds)

| Seed | Survivors | Conveyors | Last Quota | Avg Quota | Notes |
|------|-----------|-----------|------------|-----------|-------|
| 999  | 23        | 69        | 100%       | 75%       | Best performer |
| 7    | 22        | 91        | 0%         | 33%       | Good output, dies late |
| 555  | 24        | 37        | 0%         | 33%       | Low conveyor investment |
| 123  | 24        | 100       | 0%         | 10%       | Over-invests in conveyors |
| 42   | 24        | 74        | 0%         | 4%        | Food-surplus, output-starved |
| 31337| 24        | 66        | 0%         | 3%        | Survival-stable, no output |
| 7777 | 24        | 88        | 0%         | 2%        | |
| 1    | 24        | 82        | 0%         | 1%        | |
| 100  | 24        | 61        | 0%         | 1%        | |
| 2024 | 22        | 36        | 0%         | 1%        | |

**Key finding**: Most seeds keep all 24 agents alive (survival solved) but only
30-40% of seeds produce meaningful quota. The bottleneck is logistics throughput,
not agent survival.

---

## Architecture

```
src/
  components.h          360   ECS components, enums, archetype tables, InventoryComponent
  config.h              108   Config struct + TOML loader
  config.cpp            115   TOML parsing implementation
  grid.h                894   60x40 grid, tile types, conveyor BFS, machine_connected_to_exit
  wfc_generator.h       508   Wave Function Collapse procedural map layout
  path_cache.h           14   Per-agent A* path cache struct
  pathfinding.h         166   A* + cached_next_step (fully integrated)
  social.h              397   SocialFabric: trust, familiarity, contagion, influence, mood
  simulation.h          226   Simulation orchestrator
  simulation.cpp        986   Tick loop, systems, death, quota, factory health, auto-gather
  sim_utility.cpp       905   Utility AI: 11 actions, production chain routing
  sim_execute.cpp      1338   Action execution: BUILD, WORK, GATHER, EAT, hauling deposit
  sim_targets.cpp       427   Target finding: nearest machine, output hauler routing
  sim_movement.cpp       68   Cached A* pathfinding movement
  sim_conveyor.cpp      147   Conveyor belt transport, Exit-only output deposit
  chronicle.h           639   Narrative event system
  batch_main.cpp        495   Headless runner: run, story, analysis, map, jsonl
  font_cache.cpp        178   SDL2 bitmap font cache
  sprite_atlas.h        528   Sprite atlas for isometric renderer
  graphical_view.h      145   SDL2 isometric renderer declaration
  graphical_view.cpp    899   SDL2 2.5D isometric renderer
  main_gui.cpp           36   GUI entry point
  config/
  default.toml               All tunable parameters
doc/
  bases_matematicas/         Academic document (Pandoc, 20 sections)
  design_spec.md             Design specification
```

---

## Completed Features

### Core Simulation
- [x] ECS architecture via EnTT (header-only)
- [x] 60x40 grid with WFC procedural layout
- [x] Tick-based simulation loop
- [x] Config-driven parameters via TOML (no recompile to tune)
- [x] Seeded RNG for reproducible batch runs

### Agent Systems
- [x] Utility AI with 11 action types (GATHER, BUILD, WORK, EAT, REST, SOCIALIZE, CREATE, EXPLORE, GET_FOOD, MAINTAIN, DISMANTLE)
- [x] Need decay: hunger, rest, social, expression, purpose
- [x] Skill system: factory_work, domestic, artistic, social_skill with XP and level bonuses
- [x] Disease system: immune response, hunger multiplier, recovery rate

### Stress & Trauma
- [x] Stress accumulation with personality modulation
- [x] 5 stress states: NORMAL → DISSOCIATED → EUPHORIC → BROKEN → REDEEMED
- [x] Permanent trauma from chronic stress (never decays)
- [x] Noncompliance tracking: factory "notices" slacking agents
- [x] Sabotage action: high-stress agents damage conveyors
- [x] Redemption arc: Broken agents can recover via prolonged low-stress
- [x] Suicide risk for severely traumatized agents
- [x] Stress-affected personality: trauma reduces gregariousness, curiosity
- [x] 4 death causes: starvation, exhaustion, breakdown, factory collapse
- [x] Cultural artifacts: CREATE spawns mood-boosting objects at location
- [x] Hidden spaces: factory seals overused rest areas
- [x] 6 personality archetypes: Foreman, Networker, Artisan, Survivor, Explorer, Steady Worker

### Production Chain (3-tier)
- [x] GATHER raw_material from ScrapPiles
- [x] BUILD machines (3 subtypes: Food, Materials, Output), conveyors, eating zones
- [x] MaterialsMachine: raw_material → construction_material (consumes auto-gathered stockpile)
- [x] OutputMachine: construction_material → output product
- [x] Agent hauling: output in InventoryComponent → routed to Exit-adjacent Storage → passive deposit
- [x] Conveyor belt transport system (directional, condition decay, Output-only deposit near Exit)
- [x] External quota system: output shipped via Exit tiles (radius 3 drain)
- [x] Factory health: decays when quota missed, surplus bonus on over-delivery
- [x] `machine_connected_to_exit()`: traces conveyor flow to verify Output machines are served
- [x] Dynamic routing: prioritizes Materials when construction_material scarce, Output when abundant
- [x] Conveyor budget reserved for Output chains (Food/Materials deposit directly to Storage)

### Pathfinding
- [x] A* with Manhattan heuristic, max 2400 node expansions (`pathfinding.h`)
- [x] Per-agent `PathCache` — invalidates on target change or every 20 ticks
- [x] `sim_movement.cpp` calls `cached_next_step()` → `astar_find_path()` for all movement
- [x] All non-Wall tiles walkable (machines, conveyors, storage included)
- [x] Divergence recovery: reconnects to cached path or recomputes from current position

### Social Fabric
- [x] Relationship graph: familiarity [0,1], trust [-1,1] per agent pair
- [x] Social learning: agents copy actions of trusted high-influence neighbors (radius 3)
- [x] Food sharing: agents with excess food give to hungry neighbors during SOCIALIZE
- [x] Collaborative BUILD: co-builders near same target boost build rate up to 2.5x (trust-modulated)
- [x] Work coordination: collaboration bonus up to 2.0x with trusted adjacent workers
- [x] Emotional contagion: stress propagates along relationship edges
- [x] Influence/leadership: emergent from compliance, calmness, network centrality, trust
- [x] Mood: function of needs and stress, decays toward equilibrium
- [x] Grief cascade: agent death causes stress in familiar agents
- [x] Relationship decay over time (familiarity fades, trust drifts to neutral)

### Opinion Dynamics (doc §8.5 — Hegselmann-Krause + DeGroot)
- [x] 4D opinion vector: work_ethic, risk_tolerance, tradition, solidarity
- [x] Bounded confidence exchange (ε=0.3) during SOCIALIZE
- [x] DeGroot-weighted averaging with trust-modulated learning rate
- [x] Faction formation: similar opinions + mutual trust → BFS clustering
- [x] Faction trust modulation: same faction boost, different faction friction
- [x] Leader opinion pull: high-influence agents shift faction consensus

### Adaptive Infrastructure
- [x] DISMANTLE action: tear down dead-end or blocking conveyors for raw_material refund
- [x] Social penalty for dismantlers who don't rebuild
- [x] Conveyor rebuild cycle: dismantle → gather → rebuild elsewhere

### Interface
- [x] Headless batch runner with timeline report (run, story, agent, analysis, map, jsonl)
- [x] Multi-seed support for consistency testing
- [x] SDL2 2.5D isometric GUI with WASD camera, zoom, chord keyboard system
- [x] Side panel with needs, personality, opinions, stress, inventory, utility

### Procedural Generation
- [x] WFC (Wave Function Collapse) map layout
- [x] 3-type structural WFC (Wall/Floor/OpenSpace) with weighted priors
- [x] Layered generation: structural → functional stamping → bootstrap
- [x] Seed-reproducible, configurable via `use_wfc` in TOML

### Documentation
- [x] 20-section academic document (Pandoc + XeLaTeX, builds to PDF/HTML)
- [x] 25+ verified academic references

---

## In Progress / Needs Work

### Balance Tuning (primary bottleneck)
- [ ] **Logistics throughput**: most seeds produce output but can't deliver it fast enough — agent hauling round-trips take ~60 ticks, conveyor chains often incomplete
- [ ] **Productivity trap**: agents over-commit to factory work, starve (main death cause)
- [ ] **Seed variance**: best seed hits 75% avg quota, worst hits 1% — layout-dependent (food/machine placement)
- [ ] **Conveyor over-investment**: some seeds build 100 conveyors but 0 output (budget consumed by Food/Materials chains before Output machines exist)
- [ ] Conveyor condition decay may need tuning (no maintain culture emerges)

---

## Planned Features (by priority)

### Priority 1: Storyteller / Director
The simulation needs a meta-agent that modulates external pressure.
Currently quota grows at a fixed rate. A Director would:

- [ ] Track colony capacity (machines built, food stored, agent count, logistics state)
- [ ] Scale quota pressure based on colony capacity — ease off when struggling, push when stable
- [ ] Introduce events: resource booms, scrap depletion, machine breakdowns, migrant arrivals
- [ ] Create narrative rhythm: calm periods followed by pressure spikes
- [ ] Reference: RimWorld's Storyteller system (Phoebe / Cassandra / Randy)

Estimated: ~500-800 LOC, modifications to `simulation.cpp` tick loop

### Priority 2: Balance Pass — Logistics & Throughput
The core simulation is functional but logistics throughput caps quota at ~33% avg.

- [ ] Tune agent hauling: multiple output carriers per OutputMachine, inventory capacity for output
- [ ] Conveyor chain reliability: ensure flow directions form complete Machine→Exit paths
- [ ] Dynamic conveyor dismantling: tear down Food/Materials chains once Output chains are built
- [ ] Reduce seed variance: minimum Output machine placement guarantee near Exit
- [ ] Address productivity trap: stronger EAT urgency threshold tuning

Estimated: ~200-400 LOC across `sim_execute.cpp`, `sim_targets.cpp`, `sim_utility.cpp`

### Priority 3: Generational Turnover
Currently agents die but no new agents are born:

- [ ] Agent reproduction: 2 agents with high trust + low stress + sufficient food produce offspring
- [ ] Offspring inherits blended personality + random archetype from distribution
- [ ] Parent-child trust starts high (familiarity = 0.5, trust = 0.7)
- [ ] Population cap to prevent explosion
- [ ] Death of elders triggers grief but also frees resources

Estimated: ~600-800 LOC in `simulation.cpp`

### Priority 4: Disease & Health Model Extension
Basic disease exists but could be deeper:

- [ ] Contagion via proximity (social graph already tracks relationships)
- [ ] Workplace injuries (factory hazards based on machine type)
- [ ] Epidemic events (Director-triggered)
- [ ] Immune system individuality (per-agent resistance traits)

Estimated: ~400-500 LOC in `simulation.cpp` + `components.h`

---

## Not Planned (deliberately excluded)

- **3D / fluid dynamics / heat simulation** — the 2D grid is sufficient
- **Networking / multiplayer** — single-threaded local simulation
- **Save/load** — can be added later as serialization over ECS state
- **Machine learning for agent behavior** — hand-crafted utility functions are sufficient

---

## Key Design Principles

1. **Conveyors ARE walkable** — agents walk over conveyor belts (factory is already hostile enough)
2. **Two output transport paths** — conveyor chains (infrastructure) + agent hauling (labor), both deposit to Exit-adjacent Storage
3. **Quota drains only from Exit-adjacent Storage** (radius 3) — no magical teleportation, output must be physically delivered
4. **Deaths are turnover, not failure** — the simulation tracks causes but doesn't punish death
5. **Config-driven tuning** — all parameters in `config/default.toml`, no recompile needed
6. **Archetypes with jitter** — structured diversity, not uniform random
7. **Social mechanics produce emergent coordination** — not scripted behaviors

---

## Batch Test Baseline (1000 ticks, 24 agents, 10 seeds)

Best-to-worst by avg quota fill:

| Seed | Alive | Conveyors | Last Quota | Avg Quota |
|------|-------|-----------|------------|-----------|
| 999  | 23    | 69        | 100%       | 75%       |
| 7    | 22    | 91        | 0%         | 33%       |
| 555  | 24    | 37        | 0%         | 33%       |
| 123  | 24    | 100       | 0%         | 10%       |
| 42   | 24    | 74        | 0%         | 4%        |
| 31337| 24    | 66        | 0%         | 3%        |
| 7777 | 24    | 88        | 0%         | 2%        |
| 1    | 24    | 82        | 0%         | 1%        |
| 100  | 24    | 61        | 0%         | 1%        |
| 2024 | 22    | 36        | 0%         | 1%        |

**Main bottleneck**: logistics throughput. Most seeds keep all agents alive but
only deliver meaningful quota in ~30% of seeds. Agent hauling works but is
round-trip-limited (~60 ticks per delivery). Conveyor chains are incomplete
on many seeds due to budget allocation and boxed-in machine placement.
