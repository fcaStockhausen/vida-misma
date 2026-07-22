# La Vida Misma

A factory simulation where agents inhabit a factory they did not build, do not
control, and do not understand. The factory demands output. The agents want to
live. Every mechanic exists to sharpen that tension.

A company town, an institution, a closed system where the inhabitants did not
choose to be there but must make life within it. Unlike Dwarf Fortress (agents
building a home) or RimWorld (agents surviving a crash), there is no founding
act -- the factory preexists them and they wake inside it.

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

Each agent balances two broad layers of pressure:

- **Survival needs** (hunger, rest): the factory can satisfy these if the agent
  cooperates. Satisfying them consumes time that could go toward
  self-actualization.
- **Human needs** (social, expression, purpose, meaning): the institution does
  not classify or reward their meaning, although social cooperation and artifacts
  can still have indirect material or mood effects.

Each action has its own score from individual state and mostly local observation.
The diagnostic ledger classifies the resulting scalar as self- or factory-oriented,
but this is not a literal two-term causal equation. Only feasible positive-score
actions enter the Boltzmann selector, with `IDLE` as an explicit alternative.

```
P(action) proportional to exp(U(action) / temperature), action in feasible set
```

Compliance, artistry, laziness, curiosity, skills, needs, stress, mood, local
stocks, relationships and place memory all shape action-specific scores. They
create tendencies rather than professions or guaranteed choices.

This defines four design tensions or hypotheses (formalized in
[`doc/design_spec.md`](doc/design_spec.md) §2.4):

1. **The compliance spectrum** — from obedient workers to reluctant inhabitants.
2. **Contribution and benefit** — whether unequal contribution produces a
   repeatable free-riding pattern remains an empirical question.
3. **The artisan's dilemma** — creative work competes with industrial time while
   artifacts may benefit nearby mood.
4. **Labor allocation** — whether inhabitants converge on minimum necessary work
   is an empirical question, not a scripted rule.

### Where the code lives

| Concept | Code | Design doc |
|---|---|---|
| Utility computation | `src/sim_utility.cpp` | `doc/design_spec.md` §2.4 |
| Needs model | `src/components.h` (NeedsComponent) | `doc/design_spec.md` §2.3 |
| Personality weights | `src/components.h` (PersonalityComponent) | `doc/design_spec.md` §2.3 |
| Stress system | `src/simulation.cpp` (system_update_stress) | `doc/design_spec.md` §2.3 |
| Social graph | `src/social.h` | `doc/design_spec.md` §2.3, `doc/bases_matematicas/secciones/16_social_fabric.md` |
| Lifecycle and generations | `src/sim_lifecycle.cpp` | `doc/plans/2026-07-21-alineacion-diseno-implementacion.md` |
| Institutional policy | `src/sim_policy.cpp`, `src/simulation.cpp` | `doc/plans/2026-07-21-alineacion-diseno-implementacion.md` |

---

## The Indifferent Factory

The factory is not a passive backdrop, but it is not a strategic opponent. It is
a relentless institution that applies the same physical rules without reading
identities, factions, opinions, trust, or the meaning of an agent's actions.

**What the factory does to agents:**

- **Quota escalation**: demands increase over time. Agents must produce more
  with the same resources.
- **External support**: only output shipped through Exit sustains delayed
  replenishment of food and material sources.
- **Physical restructuring**: periodic load and wear rules degrade repairable
  conveyors or purge the same fraction from every resource in the selected
  nonempty storage, prioritized by occupancy fraction.
- **Space safety**: sustained anonymous overcapacity can close a capacity-marked
  space without knowing whether inhabitants consider it culturally important.

**What the factory does NOT do:**

- The factory does not kill agents directly. Residents die from hunger,
  exhaustion, breakdown, suicide or natural mortality through one exclusive
  lifecycle pipeline.
- The factory does not control agent behavior. It modifies the environment;
  agents respond through their utility functions.
- The factory does not surveil non-productive actions or target factions.

`external.policy_variant = 0` retains the former strategic/Watcher policy only
for historical A/B comparison. It is not the canonical model.

> Design rationale in
> [`doc/plans/2026-07-21-alineacion-diseno-implementacion.md`](doc/plans/2026-07-21-alineacion-diseno-implementacion.md).
> Factory policy lives in `src/sim_policy.cpp` and `src/sim_space_policy.cpp`.
> Director interface in
> [`doc/bases_matematicas/secciones/13_director.md`](doc/bases_matematicas/secciones/13_director.md).

---

## Current Status

The executable and documentation now include the complete alignment plan through
Phase 9:

- Every generated map starts with an inherited three-machine chain, storage, a
  degraded output belt, Entrance and Exit. Agents maintain, reroute and expand it
  rather than founding the factory from nothing.
- The canonical institution reacts only to physical state. Shipped output drives
  delayed external support; quota misses do not directly injure agents, and the
  factory does not inspect identities, communities or cultural acts.
- Utility selection uses local observations, feasibility filtering and Boltzmann
  choice. Conveyor planning and A* retain documented global-topology exceptions.
  Social communities are derived for analysis and grant no behavioral privileges.
- Aging, exogenous arrivals, reproduction, stable historical IDs, genealogy and
  cohort metrics support multi-generation runs without replacement toward a
  target population.
- The GUI exposes indirect institutional controls; accepted interventions can be
  recorded and replayed deterministically by the batch executable.

The Phase 8 regression at 3000 ticks ended with `51,48,51,49,50` residents alive
for seeds `0,1,2,3,7`, with zero target failures. CTest covers metrics, lifecycle,
Director boundaries and record/replay; the GUI also builds and starts under MSVC
with vcpkg.

**Explicit negative findings:** the current experiments do not establish
segregation, artistic subcultures, leadership or free-riding. The measured
contribution-benefit correlation was positive, and the trait-distance result
lacks a long-horizon interval. These remain hypotheses, not present-tense features.

Phase 9 documentation consolidation is complete. Open emergence questions remain
research tasks and are not claimed as delivered behavior.

---

## Production Chain

```
FoodSource --regeneration/auto-feed--> FoodMachine
raw_food (machine, inventory or Storage) --WORK--> processed food
processed food --inventory/Storage/conveyor--> EAT

ScrapPile --regeneration/auto-feed--> MaterialsMachine
raw_material (machine, inventory or Storage) --WORK--> construction_material
construction_material --inventory/Storage/conveyor--> OutputMachine
construction_material --WORK--> output
output --inventory/conveyor--> Exit-adjacent Storage --SHIP--> quota + external support
```

Three machine types form the executable chain:

- **FoodMachine**: placed on FoodSource tiles. Converts raw food into processed
  food. The worker keeps up to 40% subject to inventory capacity; the remainder
  goes to adjacent Storage or a compatible conveyor. Raw food remains a less
  efficient fallback with disease risk.
- **MaterialsMachine**: placed on ScrapPile tiles. Converts raw material into
  construction material, which is both infrastructure input and the feedstock
  for OutputMachine.
- **OutputMachine**: placed on Floor. Converts construction material into output.
  Output has no resident need value; only the amount shipped from Storage within
  Manhattan radius 3 of Exit satisfies quota and sustains canonical external
  support.

WFC places one built machine of each type before residents spawn, plus three
Storages and a short degraded output belt. Agents may gather and haul
inputs directly, use Storage, or extend conveyors; `actions.allow_build = false`
provides an inherited-factory counterfactual without removing the initial chain.

> Full tile taxonomy, machine subtypes, and dependency chain in
> [`doc/bases_matematicas/secciones/12_factory.md`](doc/bases_matematicas/secciones/12_factory.md).

### Conveyor System

Conveyors carry one resource type at a time toward compatible downstream machines,
Storage or Exit. Construction material can feed OutputMachine directly; output
must reach Exit-adjacent Storage before shipping. Belts have bounded throughput,
degrade below operability and can be maintained, dismantled or expanded. Agent
hauling remains a physical fallback; there is no global three-phase resource drain.

---

## Design Patterns

This project imports established patterns from game AI research. Each is
referenced in the code with a comment linking to its origin.

| Pattern | Source | What it does | Code |
|---|---|---|---|
| Urgency curves | Oxygen Not Included | Canonical survival needs use a sigmoid; legacy variants retain historical spikes for A/B | `sim_utility.cpp` |
| Action stickiness | The Sims | WORK commitment includes distance, a 15-tick base and up to 15 compliance ticks | `sim_utility.cpp` |
| Task claiming | RimWorld / Dwarf Fortress | Soft-claim machines; another agent's claim subtracts 20 target-score units | `sim_targets.cpp` |
| Supply-chain foraging | Oxygen Not Included | Gather raw_food to feed machines, not just self | `sim_utility.cpp` |
| Boltzmann action selection | Decision theory | Softmax over utilities — agents aren't deterministic | `sim_utility.cpp` |
| Maslow hierarchy dampener | This project | WORK loses appeal when higher needs are critical AND basic needs are met | `sim_utility.cpp` |
| Survival urgency variants | This project | Canonical variant 3 uses a smooth curve; variant 0 retains the historical hard override for A/B | `sim_utility.cpp` |
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
| Hunger | 0.0035 | Yes (starvation) | Eat (raw or processed) | Survival pressure |
| Rest | 0.004 | Yes (exhaustion) | Rest action | Survival pressure |
| Social | 0.003 | No | Socialize (radius 6) | Internal drive with indirect cooperation effects |
| Expression | 0.003 | No | Create at a personally scored place | Internal drive with artifact effects |
| Purpose | 0.002 | No | Work, Build, Explore | Mixed — satisfies self AND factory |
| Meaning | 0.001 | No | Create and Explore | Internal drive; no institutional classification |

Stress accumulates from critical survival needs and observed harmful events.
Upper needs (social, expression, purpose and meaning) shape utility and mood but
do not add a nominal stress term. This keeps the institution indifferent to the
meaning residents assign to their actions.

> Formal stress model in
> [`doc/bases_matematicas/secciones/17_life_death.md`](doc/bases_matematicas/secciones/17_life_death.md).

### Lifecycle

Residents have a deterministic lifespan sampled from the configured expectation.
External arrival attempts follow a time-and-seed process at Entrance and are never
scheduled in response to deaths. Reproduction requires local material security,
mutual familiarity/trust, wellbeing and mature parents; descendants inherit
bounded personality variation but no archetype, skill, relationship, community or
place memory. Stable historical IDs are never reused.

---

## Architecture

| Layer | Technology | Docs |
|---|---|---|
| ECS | EnTT v3.13.2 (header-only) | `doc/bases_matematicas/secciones/18_architecture.md` |
| Config | tomlplusplus v3.4.0 (TOML, loaded at startup) | `config/default.toml` |
| Language | C++20 | — |
| Build | CMake + FetchContent; SDL2/SDL2_ttf for GUI | — |
| Output | SDL2 GUI + headless batch runner | `doc/gui_interface_spec.md` |
| Pathfinding | A* with path caching | `doc/bases_matematicas/secciones/04_pathfinding.md` |
| Map Gen | WFC (Wave Function Collapse) | `doc/bases_matematicas/secciones/02_generacion_procedimental.md` |

All tunable parameters live in `config/default.toml`. The file is loaded once when
the process starts, so balancing changes require a restart but not a recompile.
Hardcoded fallbacks live in `src/config.h`.

### Build & Run

```bash
cmake -S . -B build -DVIDA_BUILD_GUI=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --target vida_batch vida_tests
ctest --test-dir build --output-on-failure
```

With a Visual Studio or other multi-config generator, use `--config Release` when
building and `ctest --test-dir build -C Release --output-on-failure`; binaries are
written under `build/Release/`.

The headless configuration does not require SDL. The GUI resolves SDL2 and SDL2_ttf
through portable CMake package targets first, with a conventional Unix/Homebrew
library fallback:

```bash
cmake -S . -B build-gui -DVIDA_BUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-gui --parallel --target vida_gui
```

On Windows with MSVC and vcpkg:

```powershell
vcpkg install sdl2:x64-windows sdl2-ttf:x64-windows
cmake -S . -B build-gui -A x64 -DVIDA_BUILD_GUI=ON -DBUILD_TESTING=ON `
  -DCMAKE_TOOLCHAIN_FILE="$env:USERPROFILE/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build-gui --config Release --parallel --target vida_gui vida_batch vida_tests
ctest --test-dir build-gui -C Release --output-on-failure
```

- `vida_gui [--seed N] [--record FILE] [--debug]` — SDL2 view; `E` opens the
  indirect Director controls, `1-5` selects quota/zoning/construction/removal/
  maintenance, `F12` explicitly reveals debug-only agent internals; `--record`
  writes accepted interventions on exit
- `vida_batch run <ticks> [seed]` — headless runner, prints tick-by-tick stats
- `vida_batch production <ticks> [seed]` — three-machine chain diagnostic without cultural drives
- `vida_batch culture <ticks> [seed]` — CALM cultural/personality diagnostic
- `vida_batch analysis <ticks> [seed] [policy_variant]` — structured ex-post analysis
- `vida_batch jsonl <ticks> [seed]` — outputs the Chronicle as JSONL
- `vida_batch metrics <ticks> [seed] [normal|calm|production] [supply_variant] [block_start] [block_end] [sample_every] [restructure_probability] [policy_variant] [social_learning] [spatial_affinity] [artifact_effects] [natural_mortality] [arrivals] [reproduction]` — one schema-3 JSON record with causal, emergence, cohort and genealogy metrics
- `vida_batch replay <ticks> <seed> <interventions.toml>` — deterministically
  replays a recorded Director session before each matching simulation tick
- `vida_batch map [seed]` — shows the generated layout and inherited-chain diagnostic

For batch commands, omitted optional seeds come from `config/default.toml`.
`replay` requires an explicit seed and rejects logs recorded with another seed or
configuration-source fingerprint.
Neither executable accepts a config path; launch from the repository root or its
`build/` directory so startup config lookup can find the default file.

### Source Map

```
src/
  components.h      # ECS components: ActionType, Needs, Personality, Position, Inventory
  config.h/cpp      # TOML config loader + hardcoded fallbacks
  grid.h            # 2D factory grid (60x40) with tile data, pathfinding targets
  simulation.h/cpp  # Tick loop, spawn, stress, death, quota, factory deterioration
  director.h/cpp    # Typed institutional commands and TOML record/replay codec
  sim_director.cpp  # Validated environmental intervention effects
  sim_lifecycle.cpp # Stable identities, age, arrivals, reproduction, genealogy
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
| [`doc/adversarial_utility_agents.md`](doc/adversarial_utility_agents.md) | General/historical theory of adversarial optimization; not the canonical factory ontology |
| [`doc/plans/2026-05-30-factory-as-antagonist.md`](doc/plans/2026-05-30-factory-as-antagonist.md) | Superseded historical plan; retained as a decision record |
| [`doc/gui_interface_spec.md`](doc/gui_interface_spec.md) | Current SDL2 player/debug views and Director controls |

Build the academic document:

```bash
bash doc/bases_matematicas/build.sh   # requires crossref/xelatex on PATH; pandoc may use PANDOC=/path
```

---

## Roadmap

The active roadmap is
[`doc/plans/2026-07-21-alineacion-diseno-implementacion.md`](doc/plans/2026-07-21-alineacion-diseno-implementacion.md):

| Phase | Delivered state | Status |
|---:|---|---|
| 0 | Headless build, deterministic metrics and regression contract | Complete |
| 1 | Correct event, death, quota and production observability | Complete |
| 2 | Delayed material dependence on output shipped through Exit | Complete |
| 3 | Canonical indifferent physical policy; strategic policy retained only for A/B | Complete |
| 4 | Inherited three-machine chain and Entrance/Exit map guarantees | Complete |
| 5 | Local feasible utility decisions and gradual skill feedback | Complete |
| 6 | Continuous social/place dynamics with counterfactual emergence metrics | Complete |
| 7 | Aging, arrivals, reproduction, genealogy and long-run cohorts | Complete |
| 8 | Typed GUI Director controls, recording, replay and player/debug separation | Complete |
| 9 | Documentation consolidation and retirement of obsolete present-tense claims | Complete |

Phase 9 closed after canonical and academic sources were reconciled, generated
HTML/PDF were rebuilt, and present-tense claims were audited against code and tests.

---

## License

MIT
