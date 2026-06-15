# La Vida Misma

A factory simulation where agents inhabit a factory they did not build, do not
control, and do not understand. The factory demands output. The agents want to
live. Every mechanic exists to sharpen that tension.

This is not Dwarf Fortress (agents building a home) and not RimWorld (agents
surviving a crash). This is a company town, an institution, a closed system
where the inhabitants did not choose to be there but must make life within it.

The factory is a metaphor for arbitrary design — any system that imposes
structure on the people inside it. The simulation explores what happens when
those people have their own needs, talents, and desires that have nothing to do
with the factory's output.

> The full design philosophy, formal models, and literature references are in
> [`doc/design_spec.md`](doc/design_spec.md). The academic treatment (Pandoc +
> LaTeX, cross-referenced equations) is in
> [`doc/bases_matematicas/`](doc/bases_matematicas/). The original narrative
> seed is in [`doc/vida_misma.md`](doc/vida_misma.md).

---

## The Tension Engine

The core design principle is that the factory's requirements and the agents'
internal drives are **structurally opposed**. This is not a balance problem
to be tuned away — it is the engine of the simulation.

Each agent has two layers of needs:

- **Survival needs** (hunger, rest): the factory can satisfy these if the agent
  cooperates. Satisfying them consumes time that could go toward
  self-actualization.
- **Self-actualization needs** (social, expression, purpose): the factory does
  not care about these. They have no factory function. But the agent's utility
  function does.

The utility function formalizes the tension:

```
U(action) = U_factory(action) + U_self(action)
```

Where `U_factory` is weighted by compliance (personality), and `U_self` is
weighted by the agent's internal drives. A high-compliance agent will almost
always select factory work. A low-compliance, high-artistry agent will neglect
factory work to create. The population as a whole must produce enough to keep
the factory running, but individual agents may not cooperate.

This produces four emergent dynamics (formalized in
[`doc/design_spec.md`](doc/design_spec.md) §2.4):

1. **The compliance spectrum** — from obedient workers to reluctant inhabitants.
2. **The free-rider problem** — agents who benefit without contributing
   (Nowak-May spatial game theory).
3. **The artisan's dilemma** — expression has no factory value but the agent
   needs it.
4. **The minimum work principle** — agents work the minimum necessary to meet
   quotas.

### Where the code lives

| Concept | Code | Design doc |
|---|---|---|
| Utility computation | `src/sim_utility.cpp` | `doc/design_spec.md` §2.4 |
| Needs model | `src/components.h` (NeedsComponent) | `doc/design_spec.md` §2.3 |
| Personality weights | `src/components.h` (PersonalityComponent) | `doc/design_spec.md` §2.3 |
| Stress system | `src/simulation.cpp` (system_update_stress) | `doc/design_spec.md` §2.3 |
| Social graph | `src/social.h` | `doc/design_spec.md` §2.3, `doc/bases_matematicas/secciones/16_social_fabric.md` |
| Factory as antagonist | `src/simulation.cpp` (system_factory_*) | `doc/plans/2026-05-30-factory-as-antagonist.md` |

---

## The Factory as Antagonist

The factory is not a passive backdrop. It is an active antagonist — not
intelligent, but relentless and indifferent. It escalates demands, restructures
itself, and punishes non-compliance. The agents cannot predict or control it.

**What the factory does to agents:**

- **Quota escalation**: demands increase over time. Agents must produce more
  with the same resources.
- **Factory deterioration**: when quota is missed, machines break. Broken
  machines reduce production capacity. A death spiral is possible.
- **Factory restructuring**: periodic reconfiguration — conveyor directions
  change, machines degrade, storages are confiscated. Agents must adapt.
- **Surveillance**: agents who spend too many ticks on non-productive actions
  accumulate "noncompliance" — the factory notices, stress rises, trust falls
  with compliant neighbors.

**What the factory does NOT do:**

- The factory does not kill agents directly. It is the environment, not the
  executioner. Agents die from hunger, stress breakdown, or exhaustion —
  consequences of their own choices within the factory's constraints.
- The factory does not control agent behavior. It modifies the environment;
  agents respond through their utility functions.

> Design rationale in
> [`doc/plans/2026-05-30-factory-as-antagonist.md`](doc/plans/2026-05-30-factory-as-antagonist.md).
> Factory subsystems in `src/simulation.cpp` (system_factory_deterioration,
> system_factory_restructure). Director interface in
> [`doc/bases_matematicas/secciones/13_director.md`](doc/bases_matematicas/secciones/13_director.md).

---

## Status

1000-tick benchmark (10 seeds):

```
seed   alive  built  conv   quota  avg
  42     23     23    25   100%   100%
   7     24     25    49   100%    95%
 123     22     32    46    83%    44%
 256     23     33    50     0%    36%
 999     22     18    40   100%    91%
1337     23     36    50     0%     7%
2024     21     19    31   100%    99%
   1     22     32    48   100%    89%
 555     17     36    41   100%    74%
8888     22     19    46   100%   100%

8/10 seeds: 17+ agents alive (no mass extinctions)
7/10 seeds: quota met (>=83%)
3 seeds fail quota at 1000 ticks: output pipeline issue
  (machines have raw_material but stop producing — under investigation)
```

Current focus: making the output pipeline reliable at long timescales.

---

## Production Chain

```
ScrapPile tiles   --GATHER-> raw_material in inventory
FoodSource tiles  --GATHER-> raw_food in inventory
raw_material      --BUILD--> Machine on ScrapPile (Output) or FoodSource (Food)
raw_food          --WORK---> processed food (via FoodMachine, 60/40 worker/storage)
raw_material      --WORK---> output product (via OutputMachine on ScrapPile, auto-gather)
output product    --CONVEYOR-> conveyor chains carry output to Storage/Exit
output product    --STORAGE-> Storage tiles (deposit in radius 3, or conveyor-fed)
output product    --EXIT----> shipped via Exit tiles → meets quota
processed food    --EAT----> hunger reduced
```

Two machine types form the supply chain:

- **FoodMachine**: placed on FoodSource tiles. Converts raw food into processed
  food (60% to worker, 40% to storage). Without this, agents eat raw food
  (disease risk in the design spec).
- **OutputMachine**: placed on ScrapPile tiles. Auto-gathers raw material,
  converts to output product. The output is meaningless to agents — it only
  restores factory health and meets quota.

> Full tile taxonomy, machine subtypes, and dependency chain in
> [`doc/bases_matematicas/secciones/12_factory.md`](doc/bases_matematicas/secciones/12_factory.md).

### Conveyor System

Conveyors transport output from machines to Storage/Exit. BFS chain planning
lays the full route at once. Caps (20 unbuilt / 50 total) prevent
over-infrastructuring. Conveyors degrade over time; agents repair them based on
compliance-weighted utility. Grace period prevents premature dismantling.

The 3-phase drain system ensures quota pipeline works even without conveyors:
OutputMachine → Exit-adjacent Storage (radius 3) → any Storage → any Machine.

---

## Design Patterns

This project imports established patterns from game AI research. Each is
referenced in the code with a comment linking to its origin.

| Pattern | Source | What it does | Code |
|---|---|---|---|
| Urgency curves (S-curve + spike) | Oxygen Not Included | Survival needs escalate exponentially above 0.75 | `sim_utility.cpp` |
| Action stickiness | The Sims | Once committed to WORK, agent stays `dist+15` ticks | `sim_utility.cpp` |
| Task claiming | RimWorld / Dwarf Fortress | Soft-claim machines, -30 score for claimed | `sim_targets.cpp` |
| Supply-chain foraging | Oxygen Not Included | Gather raw_food to feed machines, not just self | `sim_utility.cpp` |
| Boltzmann action selection | Decision theory | Softmax over utilities — agents aren't deterministic | `sim_utility.cpp` |
| Maslow hierarchy dampener | This project | WORK loses appeal when higher needs are critical AND basic needs are met | `sim_utility.cpp` |
| Hard survival override | This project | hunger>0.85 zeroes all non-survival actions — prevents self-starvation | `sim_utility.cpp` |
| BFS conveyor planning | This project | Full machine→Exit route planned at once, prevents dead-end cycles | `grid.h` |

> Theoretical foundations (utility theory, Wolfram classification, Schelling
> dynamics, Nowak-May spatial games) in
> [`doc/bases_matematicas/secciones/03_agentes_ia.md`](doc/bases_matematicas/secciones/03_agentes_ia.md)
> and
> [`doc/adversarial_utility_agents.md`](doc/adversarial_utility_agents.md).

---

## Needs Model

| Need | Decay (/tick) | Kills? | Satisfaction | Design role |
|---|:---:|:---:|---|---|
| Hunger | 0.005 | Yes (starvation) | Eat (raw or processed) | Survival pressure |
| Rest | 0.006 | Yes (exhaustion) | Rest action | Survival pressure |
| Social | 0.003 | No | Socialize (radius 6) | Internal drive — no factory value |
| Expression | 0.003 | No | Create (needs OpenSpace) | Internal drive — no factory value |
| Purpose | 0.002 | No | Work, Build, Explore | Mixed — satisfies self AND factory |

Stress accumulates when needs are critically unmet. Only survival needs
(hunger, rest) cause lethal stress. Upper needs (social, expression, purpose)
cause mild stress — they affect behavior and mood but do not kill. This is
deliberate: the factory is the antagonist, not the agent's own desires.

> Formal stress model in
> [`doc/bases_matematicas/secciones/17_life_death.md`](doc/bases_matematicas/secciones/17_life_death.md).

---

## Architecture

| Layer | Technology | Docs |
|---|---|---|
| ECS | EnTT v3.13.2 (header-only) | `doc/bases_matematicas/secciones/18_architecture.md` |
| Config | tomlplusplus v3.4.0 (TOML, hot-reload) | `config/default.toml` |
| Language | C++20 | — |
| Build | CMake + FetchContent (no system deps) | — |
| Output | SDL2 GUI + headless batch runner | `doc/gui_interface_spec.md` |
| Pathfinding | A* with path caching | `doc/bases_matematicas/secciones/04_pathfinding.md` |
| Map Gen | WFC (Wave Function Collapse) | `doc/bases_matematicas/secciones/02_generacion_procedimental.md` |

All tunable parameters live in `config/default.toml` — no recompile needed for
balancing. Hardcoded fallbacks in `src/config.h` for quick iteration.

### Build & Run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)   # macOS
# cmake --build build -j$(nproc)             # Linux
```

- `vida_gui` — SDL2 graphical view (mouse + keyboard controls)
- `vida_batch run <ticks> <seed>` — headless runner, prints tick-by-tick stats
- `vida_batch jsonl <ticks> <seed>` — headless runner, outputs JSONL chronicle
- `vida_batch map <seed>` — show generated map layout

### Source Map

```
src/
  components.h      # ECS components: ActionType, Needs, Personality, Position, Inventory
  config.h/cpp      # TOML config loader + hardcoded fallbacks
  grid.h            # 2D factory grid (60x40) with tile data, pathfinding targets
  simulation.h/cpp  # Tick loop, spawn, stress, death, quota, factory deterioration
  sim_utility.cpp   # Utility AI: per-tick action scoring, urgency curves, Maslow dampener
  sim_targets.cpp   # Per-action target selection + task claiming
  sim_movement.cpp  # A* pathfinding + cached movement
  sim_execute.cpp   # Action execution + adjacent-storage helpers
  sim_conveyor.cpp  # Conveyor belt system (BFS chain planning, transport, degradation)
  wfc_generator.h   # Wave Function Collapse map generator
  social.h          # Opinion dynamics, trust, affinity, grief cascades
  chronicle.h       # Narrative event logging
  graphical_view.*  # SDL2 renderer
  batch_main.cpp    # Headless runner (vida_batch)
```

---

## Documentation Map

| Document | What it contains |
|---|---|
| [`doc/design_spec.md`](doc/design_spec.md) | Practical design crystallization — the tension engine, factory metaphor, emergence targets |
| [`doc/bases_matematicas/`](doc/bases_matematicas/) | Academic document (Pandoc + LaTeX): mathematical foundations + formal design specification |
| [`doc/bases_matematicas/secciones/`](doc/bases_matematicas/secciones/) | Source markdown: 00-20 numbered sections (CA, WFC, AI agents, pathfinding, social sim, design) |
| [`doc/vida_misma.md`](doc/vida_misma.md) | Original narrative seed — raw design notes |
| [`doc/adversarial_utility_agents.md`](doc/adversarial_utility_agents.md) | Theoretical framework: utility agents as adversarial optimization |
| [`doc/plans/2026-05-30-factory-as-antagonist.md`](doc/plans/2026-05-30-factory-as-antagonist.md) | Implementation plan: factory escalation, agent subversion, emergent narrative |
| [`doc/gui_interface_spec.md`](doc/gui_interface_spec.md) | SDL2 GUI specification |

Build the academic document:

```bash
bash doc/bases_matematicas/build.sh   # requires pandoc + pandoc-crossref + xelatex
```

---

## Roadmap

### Phase 1 — Core loop (DONE)
2D grid, tile types, agents with needs/personality, utility AI, A* pathfinding,
WFC map generation.

### Phase 2 — External pressure (DONE)
Quota system, factory deterioration, conveyor belts, 3-phase drain pipeline.

### Phase 3 — AI tuning (DONE)
ONI-style urgency curves, Sims action stickiness, RimWorld task claiming,
supply-chain foraging, 500-tick stability (7/7 quota=100%).

### Phase 4 — Robustness (IN PROGRESS)
1000-tick stability achieved (8/10 seeds stable). Factory collapse no longer
kills agents directly. Hard survival override prevents self-starvation. Stress
tuned: upper needs don't kill.

**Remaining:**
- Output pipeline reliability at long timescales (3 seeds fail quota)
- Conveyor throughput verification (items moved per tick)
- MaterialsMachine role decision (activate or remove dead code)

### Phase 5 — Emergent social behavior
Affinity matrix, skills with specialization, generational replacement, social
spaces and rituals. The social graph (`social.h`) has opinion dynamics
infrastructure but it's not yet integrated with utility scoring.

> See [`doc/bases_matematicas/secciones/16_social_fabric.md`](doc/bases_matematicas/secciones/16_social_fabric.md)

### Phase 6 — Long-term engagement
Random external events, aging and mortality curves, narrative event log surface,
Director interface (quota setting, machine overlay, agent inspector). The player
becomes the factory management the agents fight against.

> See [`doc/bases_matematicas/secciones/13_director.md`](doc/bases_matematicas/secciones/13_director.md)

### Open design questions
- **Conveyor dependency**: Should conveyors be required (remove drain fallback)
  or remain optional? Current: drain carries the pipeline, conveyors are bonus.
- **MaterialsMachine**: Does the 3-step chain add depth or just complexity?
- **Stress balance**: 0 deaths = no tension. Colony wipe = broken. Target: 0-3
  deaths per 1000-tick run.
- **Agent count**: 24 agents on 60x40 — dense enough for social mechanics?

---

## License

MIT
