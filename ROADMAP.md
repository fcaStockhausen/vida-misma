# La Vida Misma — Project Roadmap

Community simulation engine in C++20 using ECS (EnTT) with FTXUI terminal interface.
Grid-based factory simulation where autonomous agents build, produce, socialize, and die.

---

## Current State

**4,623 LOC** across 17 source files. Compiles clean (Clang, C++20, CMake).
Both `vida_misma` (TUI) and `vida_batch` (headless) targets build successfully.

Latest batch results (3000 ticks, 7 seeds, 24 agents):
- Average survivors: 2.1 (up from 1.3)
- Average food produced: 333 (up from 264)
- 4/7 seeds produce survivors with stable factory health
- 0 factory collapse deaths in 5/7 seeds

---

## Architecture

```
src/
  components.h          250   ECS components, enums, archetype tables
  config.h               88   Config struct + TOML loader
  config.cpp            112   TOML parsing implementation
  grid.h                626   60x40 grid, tile types, machine clusters, conveyor placement
  pathfinding.h         110   A* implementation (exists, not integrated)
  social.h              282   SocialFabric: trust, familiarity, contagion, influence, mood, grief
  simulation.h          145   Simulation orchestrator
  simulation.cpp        460   Tick loop, systems, death, quota, factory health
  sim_utility.cpp       398   Utility AI: 11 actions, social learning, infra_gap drive
  sim_execute.cpp       690   Action execution: BUILD, WORK, GATHER, EAT, SHARE, DISMANTLE...
  sim_targets.cpp       180   Target finding: nearest unbuilt machine, scrap, storage, etc.
  sim_movement.cpp       72   Movement toward targets with noise
  sim_conveyor.cpp       76   Conveyor belt transport chain
  batch_main.cpp        214   Headless runner with multi-seed, timeline report
  main.cpp               20   Entry point (TUI)
  renderer.h            346   FTXUI grid renderer
  tui.h                 553   FTXUI interactive interface
  renderer.cpp            1   (empty)
config/
  default.toml               All tunable parameters
doc/
  bases_matematicas/         Academic document (Pandoc, 20 sections)
  design_spec.md             Design specification
  adversarial_utility_agents.md  Research notes on adversarial agent patterns
```

---

## Completed Features

### Core Simulation
- [x] ECS architecture via EnTT (header-only)
- [x] 60x40 grid with predefined factory layout
- [x] Tick-based simulation loop
- [x] Config-driven parameters via TOML (no recompile to tune)
- [x] Seeded RNG for reproducible batch runs

### Agent Systems
- [x] Utility AI with 11 action types (GATHER, BUILD, WORK, EAT, REST, SOCIALIZE, CREATE, EXPLORE, GET_FOOD, MAINTAIN, DISMANTLE)
- [x] Need decay: hunger, rest, social, expression, purpose
- [x] Stress accumulation with personality modulation
- [x] 3 death causes: starvation (burnout), exhaustion (collapse), stress (breakdown), factory collapse
- [x] Inventory system with food, raw_material, construction_material

### Personality Archetypes (based on Big Five, Belbin, RIASEC)
- [x] 6 archetypes: Foreman, Networker, Artisan, Survivor, Explorer, Steady Worker
- [x] Per-archetype base traits with per-agent jitter (+-0.08 to +-0.10)
- [x] Balanced distribution across 24 agents
- [x] Archetype name displayed in batch output

### Production Chain
- [x] GATHER raw_material from ScrapPiles
- [x] BUILD machines (3 subtypes: Food, Materials, Output), conveyors, eating zones
- [x] WORK machines: FoodMachine produces food, MaterialsMachine produces construction_material, OutputMachine consumes construction_material to restore factory health
- [x] Conveyor belt transport system (directional, condition decay)
- [x] External quota system: food shipped via Exit tiles
- [x] Factory health: decays when quota missed, triggers machine breaks, triggers collapse deaths
- [x] 2 Food machines pre-built as bootstrap

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
- [x] Negative interactions: witnesses report transgressions, trust drops

### Adaptive Infrastructure
- [x] DISMANTLE action: tear down dead-end or blocking conveyors for raw_material refund
- [x] Social penalty for dismantlers who don't rebuild
- [x] Conveyor rebuild cycle: dismantle -> gather -> rebuild elsewhere

### Interface
- [x] FTXUI interactive TUI with grid view, agent details, log
- [x] Batch runner with timeline report (actions, production, deaths)
- [x] Multi-seed support for consistency testing

### Documentation
- [x] 20-section academic document (Pandoc + XeLaTeX, builds to PDF/HTML)
- [x] Part I: Mathematical Foundations (sections 01-10)
- [x] Part II: Design Specification (sections 11-20)
- [x] 25+ verified academic references

---

## In Progress / Needs Work

### Balance Tuning
- [ ] Agents die too much by starvation ("productivity trap"): build/gather instead of eating
- [ ] Factory health decay may be too aggressive for certain seeds
- [ ] Machine break threshold interactions with health need tuning
- [ ] Conveyors break too quickly (condition decays fast, no maintain culture)

### Pathfinding
- [ ] A* exists in `pathfinding.h` but is NOT used by the movement system
- [ ] Agents currently use random_walk + target-seeking (greedy, gets stuck)
- [ ] Integration needed: `sim_movement.cpp` should call A* for route planning

---

## Planned Features (by priority)

### Priority 1: Storyteller / Director (Phase 7 of original roadmap)
The simulation needs a meta-agent that modulates external pressure.
Currently factory health decays at a fixed rate. A Director would:

- [ ] Track colony "wealth" (total machines built, food stored, agent count)
- [ ] Scale quota pressure based on colony capacity
- [ ] Introduce events: resource booms, scrap depletion, machine breakdowns, migrant arrivals
- [ ] Create narrative rhythm: calm periods followed by pressure spikes
- [ ] Reference: RimWorld's Storyteller system (Phoebe / Cassandra / Randy)

Estimated: ~500-800 LOC new, modifications to `simulation.cpp` tick loop

### Priority 2: Opinion Dynamics (Phase 7 of original roadmap)
With archetypes in place, agents can develop cultural divergence:

- [ ] Each agent holds an opinion vector (e.g., work ethic, risk tolerance, tradition vs. innovation)
- [ ] SOCIALIZE exchanges opinions via DeGroot averaging or bounded confidence
- [ ] Agents with similar opinions cluster into factions
- [ ] Faction alignment modulates trust: same faction = trust boost, different = friction
- [ ] Leaders (high influence) pull faction opinions toward their own

Estimated: ~400-600 LOC new in `social.h` + `components.h`

### Priority 3: Pathfinding Integration (Phase 3 of original roadmap)
The A* code exists. It needs to be wired into the movement system:

- [ ] `sim_targets.cpp` computes target -> feeds to pathfinding
- [ ] `sim_movement.cpp` follows A* path instead of greedy walk
- [ ] Path cache with invalidation on grid changes (conveyor build/dismantle)
- [ ] Tile cost variation (agents avoid recently-dismantled areas?)

Estimated: ~200-300 LOC modifications to `sim_movement.cpp` + `sim_targets.cpp`

### Priority 4: Generational Turnover
Currently agents die but no new agents are born:

- [ ] Agent reproduction: 2 agents with high trust + low stress + sufficient food produce offspring
- [ ] Offspring inherits blended personality + random archetype from distribution
- [ ] Parent-child trust starts high (familiarity = 0.5, trust = 0.7)
- [ ] Population cap to prevent explosion
- [ ] Death of elders triggers grief but also frees resources

Estimated: ~600-800 LOC new in `simulation.cpp`

### Priority 5: Skill System (Phase 5 extension)
Agents get better at tasks through practice:

- [ ] SkillComponent: gather_skill, build_skill, work_skill, social_skill
- [ ] XP gain per action completion (tick-based)
- [ ] Skill level affects: gather rate, build rate, work output, collaboration bonus
- [ ] Skills decay slowly without practice (use it or lose it)
- [ ] Specialists emerge naturally from archetype + practice

Estimated: ~400-500 LOC new in `components.h` + `sim_execute.cpp`

### Priority 6: World Generation (Phase 1 of original roadmap)
Currently using a hardcoded factory layout. For a more open simulation:

- [ ] Procedural map generation (Perlin/Simplex noise for elevation)
- [ ] Biome classification from layered noise (temperature, rainfall, drainage)
- [ ] Resource placement based on biome
- [ ] Region delineation via relaxed Voronoi
- [ ] This is a major scope expansion — only pursue if moving toward Dwarf Fortress style

Estimated: ~2,000 LOC new

---

## Not Planned (deliberately excluded)

- **3D / fluid dynamics / heat simulation** — the 2D grid is sufficient for community simulation
- **Graphics / rendering** — terminal output is the product
- **Networking / multiplayer** — single-threaded local simulation
- **Save/load** — can be added later as serialization over ECS state
- **Machine learning for agent behavior** — hand-crafted utility functions are sufficient

---

## Key Design Principles

1. **Conveyors are NOT walkable when built** — agents interact from adjacent tiles
2. **Maximum ONE EatingZone** — centralized food distribution
3. **Machines produce dual output** — Food machines produce food, Materials machines produce construction material
4. **Deaths are turnover, not failure** — the simulation tracks causes but doesn't punish death
5. **Config-driven tuning** — all parameters in `config/default.toml`, no recompile needed
6. **Archetypes with jitter** — structured diversity, not uniform random
7. **Social mechanics produce emergent coordination** — not scripted behaviors

---

## Batch Test Baseline (as of latest session)

Seed sweep (3000 ticks, 24 agents, 7 seeds):

| Seed | Survivors | Factory HP | Machines | Food Prod | Deaths (hunger/fatigue/collapse) |
|------|-----------|------------|----------|-----------|----------------------------------|
| 1    | 2         | 0.79       | 11/16    | 340       | 18/4/0                           |
| 3    | 0         | 0.00       | 7/16     | 272       | 16/3/2+3stress                   |
| 5    | 4         | 0.63       | 10/16    | 355       | 17/3/0                           |
| 7    | 0         | 0.00       | 14/16    | 285       | 21/1/2                           |
| 10   | 5         | 0.72       | 10/16    | 375       | 15/4/0                           |
| 20   | 2         | 0.23       | 13/16    | 353       | 19/2/1stress                     |
| 42   | 2         | 0.58       | 12/16    | 349       | 15/6/0                           |
| **Avg** | **2.1** | -          | **11.0** | **333**   | -                                |

Main bottleneck: starvation deaths (agents over-commit to build/gather, don't eat enough).
