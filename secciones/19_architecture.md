# Systems Architecture and Tick Loop {#sec:tick-loop}

## Entity-Component-System Foundation

The simulation is built on the Entity-Component-System (ECS) pattern using the EnTT library (v3.13.2). Entities are lightweight identifiers; components are plain data structs attached to entities; systems are stateless functions that iterate over entities possessing specific component signatures. This decomposition ensures that each system can be developed, tested, and modified in isolation, as systems communicate exclusively through shared component data---never through direct references to one another.

The project depends on EnTT v3.13.2 and tomlplusplus v3.4.0, both fetched at configure time via CMake FetchContent. The build system requires a C++20-compatible compiler.

## Executable Targets

Two binaries are produced from the same codebase:

- **`vida_misma`**: An interactive terminal user interface (TUI). The simulation grid is rendered to the console each display frame. Keyboard controls drive the session: `n` advances one tick; `r` toggles continuous auto-run; `p` pauses; `+`/`-` adjusts simulation speed; `Tab` cycles agent selection to inspect individual agent state; `q` terminates the process.
- **`vida_batch`**: A headless runner intended for scripted experiments and regression testing. It executes a configurable number of ticks without rendering, writing summary statistics to standard output.

Both binaries share identical simulation logic; only the presentation layer differs.

## Tick Loop

Each simulation tick processes seven systems in strict dependency order. The sequence is designed so that every system reads component state that was finalized by the preceding system in the same tick. The loop is:

```
1. system_regen_resources   Regenerate food and scrap source tiles
2. system_compute_utility   Evaluate utility AI for all agents; select action
3. system_find_targets      Locate nearest tile matching the chosen action
4. system_execute_actions   Move agent toward target; execute action upon arrival
5. system_decay_needs       Reduce all need values for every agent
6. system_update_stress     Compute stress from critical needs; apply stress decay
7. system_check_deaths      Evaluate and process death conditions
```

The ordering enforces the following invariants:

- **Resources exist before decisions.** `system_regen_resources` restores depleted food and scrap tiles at the start of each tick, ensuring that agents perceive an up-to-date resource landscape when computing utility.
- **Decisions precede movement.** `system_compute_utility` assigns each agent a single action (gather, build, work, eat, rest, socialize, create, or idle) by evaluating the utility function across all candidate actions. Once the action is fixed, `system_find_targets` resolves it to a concrete map tile using a nearest-tile heuristic.
- **Movement and execution are coupled.** `system_execute_actions` advances each agent one step along a greedy path toward its target tile. When the agent occupies the target tile, the action fires immediately (e.g., food is consumed, scrap is gathered, a machine is operated). This coupling guarantees that an agent never idles at a valid target.
- **Needs decay after action.** `system_decay_needs` reduces all need pools (hunger, rest, social, creativity) after actions have been executed. This means the satisfaction gained from eating in the current tick offsets the simultaneous decay, preventing misleading spikes in displayed need levels.
- **Stress reflects post-action state.** `system_update_stress` reads need levels after decay and computes cumulative stress. A high-need agent accumulates stress; a low-need agent experiences stress decay toward baseline. Stress therefore captures the agent's true post-tick welfare.
- **Death is the final gate.** `system_check_deaths` runs last so that it evaluates the full consequences of the tick---including any stress spike or need threshold breach that resulted from the decay and stress systems. Agents that satisfy a death condition (e.g., hunger reaches zero) are removed from the registry.

```
system_regen_resources --> system_compute_utility --> system_find_targets
                                                         |
                                                         v
                            system_check_deaths <-- system_update_stress
                                    |                       ^
                                    v                       |
                           system_decay_needs <-- system_execute_actions
```

Each system iterates over the subset of entities that possess the required components. Entities lacking a given component signature are silently skipped, which allows heterogeneous entity populations (e.g., tile entities without `NeedsComponent`, agent entities without `PositionComponent`) to coexist in the same EnTT registry.

## Console Rendering

The primary visualization is a console grid rendered with ANSI escape codes. Each cell displays either a terrain glyph or an agent glyph, color-coded by category:

**Terrain glyphs.**

| Glyph | Tile type   |
|:-----:|:------------|
| `#`   | Wall        |
| `.`   | Floor       |
| `M`   | Machine     |
| `S`   | Storage     |
| `>`   | Entrance    |
| `<`   | Exit        |
| `O`   | Open space  |
| `F`   | Food source |
| `R`   | Scrap pile  |

**Agent glyphs.** Agents are rendered with a character that reflects their currently selected action:

| Glyph | Action     |
|:-----:|:-----------|
| `G`   | Gather     |
| `B`   | Build      |
| `W`   | Work       |
| `E`   | Eat        |
| `r`   | Rest       |
| `S`   | Socialize  |
| `A`   | Create     |
| `?`   | Idle       |

When an agent occupies a tile, the agent glyph replaces the terrain glyph for that cell. Tile and agent colors are assigned via ANSI foreground codes to provide at-a-glance differentiation. This rendering strategy is sufficient to validate all emergent phenomena targeted by the simulation; graphical tilesets and sprite animation are explicitly out of scope.

## Implementation Roadmap {#sec:roadmap-vida}

The phased development plan from Section @sec:stack, adapted for *La Vida Misma* and updated to reflect actual implementation progress:

| Phase | Scope | Status | Notes |
|:------|:------|:-------|:------|
| 1. Factory Floor | Tile grid, terrain types, resource tiles, map loading from TOML | **DONE** | Reproducible world with consistent geography |
| 2. Workers | `NeedsComponent`, `PersonalityComponent`, `UtilitySystem`, action selection | **DONE** | Agents autonomously prioritize actions; personality variation visible |
| 3. Movement | Agent positioning, tile targeting, greedy step-toward-target | **PARTIALLY DONE** | Greedy movement works; A\* pathfinding and path caching not yet implemented |
| 4. Production | GATHER, BUILD, WORK actions, `ProductionSystem` skeleton | **PARTIALLY DONE** | Core actions functional; `SkillsComponent` and skill-based specialization not yet active |
| 5. Social Fabric | `RelationshipsComponent`, stress contagion, social graph | **NOT STARTED** | Planned: relationships, grief cascades, informal leadership |
| 6. Emergent Spaces | Tile preference computation, movement utility, Schelling dynamics | **NOT STARTED** | Planned: spontaneous zone formation within the factory |
| 7. Life and Death | Death conditions, entity removal | **PARTIALLY DONE** | Death system operational; birth cycle, grief events, and generational dynamics not yet implemented |

Phase 2 remains the critical design gate: the utility function (Eq. @eq:vida-utility) must produce differentiated behavior across the personality spectrum for the entire simulation to yield interesting dynamics. The current implementation confirms that personality-weighted utility scores generate heterogeneous action preferences; this foundation is validated.
