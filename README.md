# La Vida Misma

A factory simulation engine inspired by Dwarf Fortress, RimWorld, and Oxygen Not
Included. Agents exist inside a factory they didn't choose, caught between the
factory's survival requirements and their own personal drives -- expression,
purpose, artistry.

The simulation runs a 2D grid (60x40) with ECS architecture and utility-based AI.
Each tick, every agent evaluates its needs (hunger, rest, social, expression,
purpose) and picks the action with highest utility. The core tension emerges from
competing demands on limited time: survival consumes ticks that could go toward
self-actualization.

## Status

Full factory cycle operational with conveyor transport. 7/7 seeds reach quota=100%
over 500 ticks with avg 23.0/24 agents alive. Conveyors now functional (17-65 per
seed) via BFS chain planning from machines to Storage/Exit.

### Benchmark (7 seeds, 500 ticks)

```
Seed  Alive  Built  Conv  Quota  Avg_Quota  Notes
42      24     16    53    100%     94%     Conveyor-heavy, food secure
7       24     27    50    100%     80%     Heavy infrastructure
123     21     33    65    100%     91%     Most conveyors
256     22     16    40    100%     92%     Was built=0 before fix
999     22     27    47    100%     89%     Balanced build
1337    24     25    65    100%     85%     Was quota=0% before conv fix
2024    24     14    17    100%     93%     Lightest infrastructure
AVG    23.0    --     --   100%     89%     7/7 seeds pass
```

## Production Chain

```
ScrapPile tiles   --GATHER-> raw_material in inventory
FoodSource tiles  --GATHER-> raw_food in inventory  
raw_material      --BUILD--> Machine on ScrapPile (OutputMachine) or FoodSource (FoodMachine)
raw_food          --WORK---> processed food (via FoodMachine, 60/40 worker/storage split)
raw_material      --WORK---> output product (via OutputMachine on ScrapPile, auto-gather)
output product    --CONVEYOR-> conveyor chains carry output to Storage/Exit
output product    --STORAGE-> Storage tiles (deposit in radius 3, or conveyor-fed)
output product    --EXIT----> shipped via Exit tiles → meets quota
processed food    --EAT----> hunger reduced
```

Two active machine types form the supply chain:

- **FoodMachine**: placed on FoodSource tiles. Converts raw_food into processed food (60% to worker, 40% to storage).
- **OutputMachine**: placed on ScrapPile tiles. Auto-gathers raw_material from the ScrapPile, converts it to output product. Self-sustaining -- no separate materials step needed.

(Note: MaterialsMachine exists in code but is superseded by OutputMachine's auto-gather on ScrapPile.)

Agents start with nothing. Wild food sources keep them alive at subsistence level.
To thrive (satisfy higher needs like expression and purpose), they must build
machines and produce processed food -- freeing ticks for non-survival actions.

## Conveyor System

Conveyors transport output from machines to Storage/Exit tiles automatically each
tick. The system uses BFS chain planning:

1. **Chain Planning**: When an agent triggers conveyor construction, a BFS from
   the nearest machine (lacking conveyor output) to the nearest Storage/Exit
   traces the optimal path. All conveyor frames along the route are placed at once
   as unbuilt tiles.
2. **Collaborative Build**: Agents then complete unbuilt frames. Build cost is 0.15
   (1-2 ticks per tile). Multiple agents can contribute.
3. **Transport**: Built conveyors move contents downstream (nearest-to-Exit first)
   each tick. Contents deposit into Storage or adjacent-to-Exit Storage.
4. **Maintenance**: Conveyors degrade over time. Agents with high compliance
   prioritize repair when condition drops. Grace period prevents premature
   dismantling of chains adjacent to machines or other conveyors.
5. **Caps**: Maximum 20 unbuilt / 50 total conveyors to prevent over-infrastructuring.

## Architecture

| Layer       | Technology                             |
|-------------|----------------------------------------|
| ECS         | EnTT v3.13.2 (header-only)             |
| Config      | tomlplusplus v3.4.0 (TOML, hot-reload) |
| Language    | C++20                                  |
| Build       | CMake + FetchContent (no system deps)  |
| Output      | SDL2 GUI + headless batch runner       |
| Pathfinding | A* with path caching                   |
| Map Gen     | WFC (Wave Function Collapse)           |

All tunable parameters live in `config/default.toml` -- no recompile needed for
balancing. Hardcoded fallbacks in `src/config.h` for quick iteration.

## Build & Run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)   # macOS
# cmake --build build -j$(nproc)             # Linux
```

Two binaries:

- `vida_gui` -- SDL2 graphical view (mouse + keyboard controls)
- `vida_batch run <ticks> <seed>` -- headless runner, prints tick-by-tick statistics
- `vida_batch map <seed>` -- show generated map layout

## Utility AI

Each agent has a personality vector (compliance, laziness, artistry, gregariousness,
resilience, curiosity) that weights action utilities differently. A lazy agent rests
more; a gregarious one prioritizes socializing; an artistic one seeks creation
opportunities.

Utility for action `a`:

```
U(a) = weight(personality, need_state) x urgency(need)
```

### Urgency Curves

Needs use different urgency curves depending on type (inspired by Oxygen Not
Included's need escalation):

- **Survival needs** (hunger, rest): S-curve (`x^4`) + exponential critical spike
  above 0.75. Agents tolerate moderate levels but PANIC at critical.
  - urgency(0.3) = 0.008, urgency(0.7) = 0.240, urgency(0.9) = 2.66
- **Non-survival needs** (social, expression, purpose): flat power curve (`x^2`).
  Important but never override survival.

### Action Selection Patterns

Design patterns imported from game AI research:

- **Action Stickiness** (The Sims): Once an agent commits to WORK, it stays
  committed for `dist_to_target + 15` ticks. Only critical starvation breaks
  commitment. Prevents mid-transit flip-flopping.
- **Task Claiming** (RimWorld / Dwarf Fortress): Machines get soft-claimed by
  agents. Other agents see reduced utility for claimed machines (-30 score),
  distributing workers across the factory.
- **Supply-Chain Foraging** (Oxygen Not Included): Agents gather raw_food to
  FEED THE MACHINES even when personally well-fed. Activates when storage
  raw_food drops below 3.0 units.
- **Preference-Dominant Routing** (RimWorld): Machine type preference dominates
  distance. An agent carrying raw_food walks to the FoodMachine, not the nearest
  OutputMachine.

## Needs Model

| Need       | Decay (/tick) | Death if maxed for | Satisfaction                    |
|------------|:---:|:---:|---:|
| Hunger     | 0.005 | ~200 ticks (starvation) | Eat (raw or processed food) |
| Rest       | 0.006 | ~167 ticks (exhaustion) | Rest action                 |
| Social     | 0.003 | no death | Socialize (requires adjacent agent) |
| Expression | 0.003 | no death | Create (requires OpenSpace)  |
| Purpose    | 0.002 | no death | Work, Build, Explore         |

Unmet needs above 0.5 generate stress. Stress above 0.92 causes breakdown (death).

## Project Structure

```
src/
  components.h     # ECS components: ActionType, Needs, Personality, Position, Inventory
  config.h/cpp     # TOML config loader + hardcoded fallbacks
  grid.h           # 2D factory grid (60x40) with tile data, pathfinding targets
  simulation.h     # Simulation class declaration
  simulation.cpp   # advance(), spawn, regen, decay, stress, death, quota
  sim_utility.cpp  # Utility AI: per-tick action scoring + urgency curves
  sim_targets.cpp  # Per-action target selection + task claiming
  sim_movement.cpp # A* pathfinding + cached movement
  sim_execute.cpp  # Action execution + adjacent-storage helpers
  sim_conveyor.cpp # Conveyor belt system (BFS chain planning, transport, degradation)
  pathfinding.h    # A* implementation with path cache
  wfc_generator.h  # Wave Function Collapse map generator
  social.h         # Opinion dynamics, trust, affinity
  chronicle.h      # Narrative event logging
  graphical_view.h/cpp  # SDL2 renderer with sprite atlas
  sprite_atlas.h   # Sprite sheet management
  font_cache.h/cpp # TTF font rendering
  main_gui.cpp     # SDL2 GUI entry point
  batch_main.cpp   # Headless runner (vida_batch)
config/
  default.toml     # All tunable parameters
doc/
  design_spec.md              # Practical design crystallization
  vida_misma.md               # Original narrative seed
  gui_interface_spec.md       # SDL2 GUI specification
  adversarial_utility_agents.md  # Notes on adversarial utility agents
  dependency_graph.html       # Dependency graph visualization
  bases_matematicas/          # Academic document build system
    secciones/                # Pandoc markdown sources (00-20)
    build.sh                  # Pandoc + crossref + xelatex
```

## Key Design Decisions

- **2D grid** (not 3D) -- justified by Nowak-May spatial games, Schelling segregation,
  and adequacy for the factory setting
- **Turn-based ticks** (not real-time) -- one action per tick per agent,
  deterministic, easy to debug
- **A\* pathfinding** with path caching -- replaced greedy movement; internal walls
  and obstacles are now navigable
- **WFC map generation** -- procedural factory layouts with guaranteed machine
  placement, storage adjacency, and conveyor routes
- **No pre-built machines** -- all machines built dynamically by agents on resource
  tiles (FoodSource/ScrapPile). NEVER pre-built in WFC.
- **Build cost 0.15** -- machines complete in 1-2 ticks. Previously 0.5 (5+ ticks)
  caused agents to abandon half-built machines.
- **BFS conveyor chains** -- full route planned at once from machine to Storage/Exit,
  preventing the dead-end dismantle cycle that kept conveyors at 0.
- **3-phase drain** -- OutputMachine stored_output → Exit-adjacent Storage (radius 3)
  → any Storage → any Machine. Ensures quota pipeline works even without conveyors.
- **All parameters external** -- TOML config for all decay rates, satisfaction
  amounts, personality ranges, grid size, population

## Roadmap

### Phase 1 -- Core loop (DONE)
1. Production system (GATHER, BUILD, WORK)
2. Spatial constraints (actions require correct tile type)
3. Communal production chain with storage
4. A* pathfinding with path caching
5. WFC procedural map layout

### Phase 2 -- External pressure (DONE)
6. Quota / ship-out via Exit tiles
7. Factory deterioration (health decay on missed quota, machine breakage)
8. EatingZones (agent-built social spaces)
9. GET_FOOD action + food cap ("vianda" to-go)
10. Conveyor belts connecting machines to storage/exit

### Phase 3 -- AI tuning (DONE)
11. Urgency curves (ONI-style S-curves for survival needs)
12. Action stickiness (The Sims pattern)
13. Task claiming (RimWorld/DF pattern)
14. Supply-chain foraging (ONI "haul to workshop" pattern)
15. 500-tick stability -- 7/7 quota=100%, conveyors functional (17-65/seed)

Key fixes enabling stable 7/7 pass rate:
- Agents released from sticky loops when target becomes invalid
- Build cost reduced to 0.15 (was 0.5) -- machines complete in 1-2 ticks
- BFS conveyor chain planning -- full route placed at once, ending dead-end cycle
- Conveyor grace period -- no dismantle of chains adjacent to machines/other conveyors
- 3-phase drain system: OutputMachine → Storage → Exit
- ScrapPile placement near Exit in WFC generator

### Phase 4 -- Robustness & sustainability (IN PROGRESS)

Known issues at 500 ticks:
- Stress-induced suicides (seed 42: 3 suicides by tick 500) -- stress curve may be too aggressive
- Seed 123 alive=21/24 -- survival variance across seeds needs investigation
- Conveyor transport not verified -- belts are built but actual item throughput is unmeasured
- MaterialsMachine unused in practice -- OutputMachine bypasses the raw→construction→output chain
- 3-phase drain carries the pipeline; conveyors are decorative infrastructure, not load-bearing

16. **1000-tick stability test** -- run 10+ seeds at 1000 ticks, identify collapse patterns
17. **Stress/suicide tuning** -- adjust stress accumulation curves to reduce irrational deaths
    while keeping tension. Current stress threshold (0.92) may be too low for long runs.
18. **Conveyor transport verification** -- add metrics for actual conveyor throughput
    (items moved per tick). Verify belts carry goods, not just exist.
19. **MaterialsMachine role** -- decide: activate as intermediate step in the chain
    (raw_material → construction_material → output) or remove entirely.
    Currently OutputMachine auto-gathers and converts directly, making MaterialsMachine dead code.
20. **Multi-seed extended benchmark** -- 10+ seeds at 1000 ticks, track alive/quota/conveyor
    throughput per seed. Establish baseline for regression testing.

### Phase 5 -- Emergent social behavior
21. Affinity matrix between agent pairs (stress contagion, cooperation, grief cascades)
    -- social.h has opinion dynamics infrastructure; integrate with utility scoring
22. Skills that improve with use (factory_work, artistic, gathering) -- agents become
    faster at repeated tasks, creating personality-driven specialization
23. Generational replacement -- new agents arrive at Entrance when population drops.
    Younger agents inherit partial personality from predecessors + random mutation.
24. Social spaces and rituals -- EatingZones become gathering points that boost social
    need satisfaction. Agents develop location preferences and routines.

### Phase 6 -- Long-term engagement
25. Random external events (storms, supplier dropouts, machine damage) -- factory
    faces external shocks that require collective response
26. Aging and mortality curves -- agents slow down over time, retire, or die of old age.
    Creates urgency for generational knowledge transfer.
27. Narrative event log surface -- chronicle.h already logs events; surface as a
    readable timeline the player/director can browse
28. Director interface -- quota setting, machine placement overlay, agent inspector.
    The player becomes the factory management the agents fight against.

### Open design questions
- **Conveyor dependency**: Should conveyors be required (remove drain fallback) or
  remain optional? Current design: drain handles the real work, conveyors are bonus.
- **MaterialsMachine**: Does the 3-step chain add interesting decisions, or just
  complexity without depth?
- **Stress balance**: How much agent death is "correct"? Zero deaths = no tension.
  Full colony wipe = broken. Target: 0-3 deaths per 1000-tick run across seeds.
- **Agent count**: 24 agents on 60x40 grid -- is this dense enough for social
  mechanics to matter? Or should population scale with factory size?

## Academic Document

The `doc/bases_matematicas/secciones/` directory contains a two-part document built
with Pandoc:

- **Part I**: Mathematical foundations -- cellular automata, procedural generation,
  utility theory, pathfinding, social simulation, spatial games
- **Part II**: Design specification -- factory metaphor, director AI, inhabitants,
  spaces, social fabric, life/death mechanics

Build with: `bash doc/bases_matematicas/build.sh` (requires `pandoc` and
`pandoc-crossref`). Outputs land in `doc/bases_matematicas/`.

## License

MIT
