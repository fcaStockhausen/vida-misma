# La Vida Misma

A community simulation engine inspired by Dwarf Fortress and RimWorld. Agents exist inside a factory they didn't choose, caught between the factory's survival requirements and their own personal drives — expression, purpose, artistry.

The simulation runs a 2D grid with ECS architecture and utility-based AI. Each tick, every agent evaluates its needs (hunger, rest, social, expression, purpose) and picks the action with highest utility. The core tension emerges from competing demands on limited time: survival consumes ticks that could go toward self-actualization.

## Status

Full factory cycle operational. 24/24 agents survive 5000 ticks. All 16 machines are built by tick ~1000, and the production chain (gather → build → work → storage → eat) sustains itself: ~55 units of processed food accumulate in communal storage while agents diversify into social and creative actions during Phase 3.

## Production Chain

```
FoodSource tiles ──GATHER──▶ raw_food in inventory ──EAT──▶ hunger reduced
ScrapPile tiles  ──GATHER──▶ raw_material in inventory
raw_material     ──BUILD──▶ Machine (UnbuiltMachine → MachineBuilt)
MachineBuilt     ──WORK──▶  processed food → Storage
Storage (food)   ──EAT──▶   hunger reduced (more efficient than raw)
```

Agents start with nothing. Wild food sources keep them alive at subsistence level. To thrive (satisfy higher needs like expression and purpose), they must build machines and produce processed food — freeing ticks for non-survival actions.

## Architecture

| Layer | Technology |
|-------|-----------|
| ECS | EnTT v3.13.2 (header-only) |
| Config | tomlplusplus v3.4.0 (TOML, hot-reloadable) |
| Language | C++20 |
| Build | CMake + FetchContent (no system deps) |
| Output | Terminal ANSI (interactive TUI) |

All tunable parameters live in `config/default.toml` — no recompile needed for balancing.

## Build & Run

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

Two binaries:

- `vida_misma` — interactive TUI (keyboard controls: `n` next tick, `r` run, `p` pause, `+/-` speed, `Tab` cycle agent, `q` quit)
- `vida_batch [N]` — headless runner, prints initial/final state and statistics after N ticks

## Utility AI

Each agent has a personality vector (compliance, laziness, artistry, gregariousness, resilience, curiosity) that weights action utilities differently. A lazy agent rests more; a gregarious one prioritizes socializing; an artistic one seeks creation opportunities.

Utility for action $a$:

```
U(a) = weight(personality, need_state) × urgency(need)
```

where urgency follows a power law: `urgency = need_level^α` (α=2.0 by default). This creates a sharp preference for critical needs over moderate ones.

## Needs Model

| Need | Decay (/tick) | Death if maxed for | Satisfaction |
|------|:---:|:---:|:---|
| Hunger | 0.005 | ~200 ticks (starvation) | Eat (raw or processed food) |
| Rest | 0.006 | ~167 ticks (exhaustion) | Rest action |
| Social | 0.003 | no death | Socialize (requires adjacent agent) |
| Expression | 0.003 | no death | Create (requires OpenSpace) |
| Purpose | 0.002 | no death | Work, Build, Explore |

Unmet needs above 0.5 generate stress. Stress above 0.92 causes breakdown (death).

## Project Structure

```
src/
  components.h    # ECS components: TileType, ActionType, Needs, Personality, Position, Inventory...
  config.h/cpp    # TOML config loader
  grid.h          # 2D factory grid (60×40) with tile data
  simulation.h    # Simulation class declaration
  simulation.cpp  # advance(), spawn, regen, decay, stress, death
  sim_utility.cpp # Utility AI: per-tick action scoring
  sim_targets.cpp # Per-action target selection
  sim_movement.cpp # Greedy movement + helpers
  sim_execute.cpp # Action execution + adjacent-storage helpers
  renderer.h/cpp  # Terminal TUI with ANSI colors
  main.cpp        # Interactive entry point
  batch_main.cpp  # Headless runner
config/
  default.toml    # All tunable parameters
doc/
  design_spec.md            # Practical design crystallization
  vida_misma.md             # Original narrative seed
  bases_matematicas.{md,html,pdf,zip}  # Compiled academic document (output of build.sh)
  adversarial_utility_agents.md         # Notes on adversarial utility agents
  dependency_graph.html                  # Dependency graph visualization
  secciones/                # Academic document source (Pandoc markdown)
    00_metadata.yaml through 19_architecture.md
    references.yaml         # CSL YAML bibliography
```

## Key Design Decisions

- **2D grid** (not 3D) — justified by Nowak-May spatial games, Schelling segregation, and adequacy for the factory setting
- **Turn-based ticks** (not real-time) — one action per tick per agent, deterministic, easy to debug
- **No internal walls** (temporary) — removed until A* pathfinding is implemented; greedy movement can't navigate around obstacles
- **All parameters external** — TOML config for all decay rates, satisfaction amounts, personality ranges, grid size, population

## Roadmap

### Phase 1 — Core loop completion
1. ~~Production system (GATHER, BUILD, WORK)~~ ✓
2. ~~Spatial constraints (actions require correct tile type)~~ ✓
3. A* pathfinding (replace greedy movement, restore internal walls)
4. Proximity requirements (SOCIALIZE needs adjacent agent)

### Phase 2 — Emergent behavior
5. Affinity matrix between agent pairs (affects stress, cooperation)
6. Skills that improve with use (factory_work, artistic, gathering)
7. Dynamic factory health with consequences (breakdowns, bonuses)

### Phase 3 — Long-term engagement
8. Random events (machine breaks, storms, new agents arrive)
9. Aging, birth, generations with inherited traits
10. Narrative event log ("Tick 347: Agent 12 broke the north Machine")

## Academic Document

The `doc/secciones/` directory contains a two-part document built with Pandoc:

- **Part I**: Mathematical foundations — cellular automata, procedural generation, utility theory, pathfinding, social simulation, spatial games
- **Part II**: Design specification — factory metaphor, director AI, inhabitants, spaces, social fabric, life/death mechanics

Build with: `./build.sh` (requires `pandoc` and `pandoc-crossref`). Outputs land in `doc/`.

## License

MIT
