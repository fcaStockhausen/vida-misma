# Runtime Architecture, Tick Pipeline, and Interfaces {#sec:tick-loop}

The current program is a hybrid simulation architecture, not a pure ECS and not
a terminal prototype. EnTT manages dynamic entities and their components, while a
dense grid, a directed relationship matrix, event stores, configuration, metrics,
and institutional state are owned directly by `Simulation`.

## Hybrid Runtime Model

The main state holders are:

| Subsystem | Representation | Primary references |
|---|---|---|
| Agents and artifacts | EnTT entities with plain data components | `src/components.h`, `Simulation::registry_` |
| Physical map | Dense `Grid` arrays of `TileType` and `TileData` | `src/grid.h` |
| Directed relationships | Dynamically expandable flat matrix indexed by historical IDs | `src/social.h` |
| Production assessment | Value snapshot recomputed after transport and shipping | `src/production.h` |
| Chronicle | Typed factual event collection and narrative rendering | `src/chronicle.h` |
| Metrics | Structured counters, sums, and per-agent ledgers | `src/metrics.h` |
| Human interventions | Typed command variant and ordered event ledger | `src/director.h` |
| Orchestration | Stateful `Simulation` object and member systems | `src/simulation.h` |

EnTT components are data-oriented, but the systems are not independent stateless
functions: they are `Simulation` member functions and communicate through the
registry, `Grid`, `SocialFabric`, metrics, Chronicle, RNG streams, and aggregate
state. Tile entities are not stored in EnTT. This distinction matters when
reasoning about dependencies and tests.

The project requires C++20. CMake fetches EnTT v3.13.2 and tomlplusplus v3.4.0
from pinned Git tags. SDL is isolated behind the GUI build option
(`CMakeLists.txt`).

## Executable Targets

The build defines these targets:

| Target | Availability | Purpose |
|---|---|---|
| `vida_gui` | when `VIDA_BUILD_GUI=ON` | SDL2/SDL2_ttf interactive 2.5D isometric client |
| `vida_batch` | always | headless commands, experiments, metrics, Chronicle export, and replay |
| `vida_tests` | when `BUILD_TESTING=ON` | deterministic C++ simulation tests |

There is no current `vida_misma` TUI executable. Both user-facing executables
load `config/default.toml` (or `../config/default.toml`) once, construct the same
`Simulation`, and use the same source list. The batch CLI provides `run`, `calm`,
`production`, `culture`, `story`, `agent`, `analysis`, `jsonl`, `map`, `metrics`,
and `replay` commands (`src/batch_main.cpp`).

## Source Decomposition

`VIDA_SIM_SOURCES` in `CMakeLists.txt` is the shared executable core:

| Source | Responsibility |
|---|---|
| `src/simulation.cpp` | construction, tick orchestration, resource regeneration, shipping, stress/death, social observations, aggregate metrics, and historical legacy pressure |
| `src/sim_utility.cpp` | local observations, utility decomposition, feasibility, and Boltzmann action selection |
| `src/sim_targets.cpp` | action-specific target plans and physical claims |
| `src/sim_movement.cpp` | occupancy-limited movement over cached A* paths |
| `src/sim_execute.cpp` | action effects, production transformations, resource transfer, help, construction, maintenance, dismantling, and sabotage |
| `src/sim_conveyor.cpp` | condition decay and one-resource conveyor transfer |
| `src/sim_lifecycle.cpp` | spawning primitive, age, arrivals, reproduction, genealogy, and integration observations |
| `src/sim_policy.cpp` | canonical anonymous physical institutional policy |
| `src/sim_space_policy.cpp` | anonymous occupancy-capacity policy |
| `src/director.cpp` | intervention-log TOML schema and parsing/serialization |
| `src/sim_director.cpp` | validation and application of typed Director commands |

Header-only services include `Grid`, A* and path caching, production assessment,
social rules, recipes, components, metrics, and Chronicle. `src/config.cpp` is
compiled separately into both user-facing executables. GUI-only sources are
`src/main_gui.cpp`, `src/graphical_view.cpp`, and `src/font_cache.cpp`.

## Exact `Simulation::advance()` Pipeline

The executable order is the order below (`src/simulation.cpp`). It is not the
seven- or eight-stage loop described by earlier versions of this document.

```text
 1. system_regen_resources()
 2. system_decay_needs()
 3. apply CALM state, or grow quota when it was not manually fixed
 4. system_compute_utility()
 5. system_find_targets()
 6. system_move_to_targets()
 7. system_execute_actions()
 8. system_social_learning()
 9. system_spatial_learning()
10. system_conveyor_transport()
11. system_ship_out_food()              # output shipping despite legacy name
12. ProductionChain::assess()            # snapshot for the next tick
13. non-CALM institutional policy:
      supply variant 0: factory deterioration
      policy variant 0: legacy restructure + hidden-space exposure
      policy variant 1: indifferent restructure + space overcapacity
14. supply variant 1, non-CALM: system_update_factory_condition()
15. system_artifact_effects()
16. system_community_detection()
17. system_update_stress()
18. system_check_deaths()                # includes pending grief
19. SocialFabric: contagion, influence, mood, relationship decay
20. system_check_dismantle_penalties()
21. system_record_emergence_metrics()
22. system_chronicle_narrative()
23. record_metric_deaths()
24. system_lifecycle()                   # integration, arrivals, reproduction
25. metrics_.ticks_advanced++; tick_++
```

Several timing consequences follow:

- Need decay precedes action, so an action observes already-decayed needs and may
  satisfy them later in the same tick.
- Social and spatial evidence observe action effects after execution.
- Conveyors transport resources deposited by WORK in the same tick; agents can
  use the resulting Storage stock no earlier than the next action phase.
- Shipping drains only output that has physically reached qualifying Storage by
  stage 11. The support signal then affects regeneration first on the next tick.
- `ProductionChain::assess()` is post-action and post-shipping. Its stored snapshot
  is exposed for diagnostics; canonical utility and targeting do not consume it
  and mostly use local observations. Conveyor connectivity planning is a declared
  global scan, and A* reads the full map only after target selection.
- Death occurs before graph contagion but grief is applied inside the death stage;
  dead agents are absent from the later alive-vector social updates.
- Death metrics are recorded before lifecycle creates arrivals and births.
  Newcomers therefore begin acting on the following tick.

Director interventions are intentionally outside this function. Their event
phase is `before_advance`: GUI commands and replay events are applied at the
current `Simulation::tick()` before calling `advance()`.

## SDL Graphical Client

`vida_gui` is an SDL2/SDL2_ttf renderer with an isometric tile view, camera pan and
zoom, agent selection/following, speed control, pause, single-step, factual event
display, tile tooltips, and agent panels (`src/graphical_view.h`,
`src/graphical_view.cpp`). Simulation and rendering run on the same thread.

Normal view exposes environmental consequences such as quota/fill, stocks,
shipping, condition, density, zones, maintenance priority, and events. `F12`
explicitly enables debug information, including exact needs, personality,
relations, and utility decomposition. `E` enters Director edit mode and pauses
advancement. `vida_gui --seed N --record FILE` writes accepted interventions when
the session exits (`src/main_gui.cpp`). SDL2 and SDL2_ttf are resolved through
CMake package targets, including vcpkg, with a Unix/Homebrew library fallback.

## Typed Director Boundary

`DirectorCommand` is a `std::variant` containing only:

- `DirectorSetQuota`;
- `DirectorSetZone` with anonymous occupancy capacity $0\ldots8$;
- `DirectorPlaceStructure` for Wall, Storage, one of three Machines, or Conveyor;
- `DirectorRemoveStructure`;
- `DirectorSetMaintenancePriority` for a built conveyor.

The API accepts environmental coordinates and physical parameters, never agent
identity, action, behavioral target, personality, relationship, community, or
utility state (`src/director.h`, `src/sim_director.cpp`). Placed structures are
completed immediately. Priority is a signal consumed by autonomous maintenance;
it does not repair a belt or assign a worker. CALM rejects quota intervention but
continues to permit physical editing.

Each accepted command receives the current tick and a strict sequence number.
Rejected commands are atomic and do not enter the ledger. Intervention logs use
TOML format `vida-interventions`, schema 2, `tick_phase = "before_advance"`, and a
fingerprint of the loaded configuration source (`src/director.cpp`).

`vida_batch replay <ticks> <seed> <file>` validates format, mode, seed,
configuration-source fingerprint, ordered sequence, event ticks, and each command
transition. It emits a separate replay JSON schema 1 with a state fingerprint. The
guarantee tested by the project is determinism within the **same build and
configuration**; the format is not a promise of bit-identical replay across
compiler, platform, or future schema and model versions.

## Schema-3 Metrics Contract

`vida_batch metrics` emits exactly one JSON object with `schema_version: 3`. Its
top-level evidence includes:

| Section | Content |
|---|---|
| `population` | initial/max/alive/peak/historical counts, arrivals, births, capacity blocks, deaths by cause |
| `demography` | mechanism toggles, integration summaries, temporal cohorts, and per-person genealogy/lifecycle records |
| `factory` | quota demand, output shipped/produced/hauled, support/supply factor, policy variants, intervention window, deterioration events |
| `actions` | selected, lookup, failed, invalidated, reached, executed, and utility decomposition for all 13 actions |
| `resources`, `machines`, `stocks` | physical flow accounting and inherited/current infrastructure |
| `social`, `emergence` | directed-edge aggregates, communities, persistence/modularity/stability, specialization, contribution/benefit, per-agent ledger |
| `needs`, `skills`, `events`, `timeline` | terminal aggregates, factual counters, and optional samples |

The command accepts explicit toggles for social learning, spatial affinity,
artifact effects, natural mortality, arrivals, and reproduction, enabling paired
counterfactuals. Schema 3 is the metrics contract; it should not be confused with
the schema-2 intervention log or schema-1 replay result (`src/batch_main.cpp`).

## Verification and Phase Status

CMake registers four CTest entries when testing is enabled:

| Test | Coverage |
|---|---|
| `vida.metrics` | C++ fixtures for simulation, map, logistics, social directionality, mortality, demographics, Director, and deterministic replay |
| `vida.cli_metrics` | byte-identical same-build schema-3 output, accounting, sections, toggles, and CLI validation |
| `vida.policy_audit` | static institutional, group-label, RNG, lifecycle-label, and Director-boundary prohibitions |
| `vida.cli_replay` | byte-identical same-build replay and rejection of a mismatched seed |

The alignment plan's Phases 0 through 9 are complete. Phase 9 consolidated the
documentation so claims in present tense point to executable mechanisms, while
emergent outcomes require stated evidence
(`doc/plans/2026-07-21-alineacion-diseno-implementacion.md`).

## Implementation and Verification References

- Build graph and targets: `CMakeLists.txt`.
- Orchestration and shared state: `src/simulation.h`, `src/simulation.cpp`.
- Batch and schema-3 output: `src/batch_main.cpp`, `src/metrics.h`.
- GUI: `src/main_gui.cpp`, `src/graphical_view.h`,
  `src/graphical_view.cpp`.
- Director and replay: `src/director.h`, `src/director.cpp`,
  `src/sim_director.cpp`, `tests/replay_fixture.toml`,
  `tests/verify_replay.cmake`.
- Test contracts: `tests/simulation_tests.cpp`,
  `tests/verify_metrics.cmake`, `tests/verify_policy_audit.cmake`.
