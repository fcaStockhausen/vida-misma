# Phase 8: Production Pipelines — Design Spec

## Core Idea

The factory doesn't exist without production lines. Currently agents carry resources
in their pockets and walk them between tiles. With conveyor/pipeline tiles, the factory
gets physical infrastructure that agents must build, maintain, and connect.

## New Tile Type: `Conveyor`

```
TileType::Conveyor   // Directional transport tile
```

### Properties (in TileData)
- `direction`: enum {N, S, E, W} — which way resources flow
- `condition`: float [0, 1] — degrades over time, 0 = broken, blocks flow
- `build_progress` / `build_cost`: same as Machine (agents must BUILD it)
- `contents`: ResourceType + float amount — what's currently on the belt
- `contents_amount`: float — how much is sitting on this segment

### How it works

1. Each tick, every Conveyor tile tries to push its contents to the neighbor in `direction`
2. If the neighbor is:
   - Another Conveyor: contents transfer (if neighbor has room)
   - Storage: contents deposit (if storage has room)
   - Exit: contents ship out (counts toward quota)
   - Machine: contents feed as input (raw_material for building, food for workers)
   - Anything else / blocked: contents stay, belt backs up
3. `condition` degrades by `conveyor_decay_rate` per tick
4. When `condition < 0.2`, the conveyor is "broken" — stops moving contents
5. Agents with BUILD action can repair by standing on it (build_progress → build_cost)

### Building a Conveyor

1. Agent stands on Floor tile, chooses BUILD
2. If adjacent tile is Machine, Storage, Exit, or another Conveyor → auto-set direction
   toward that neighbor
3. Costs `conveyor_build_cost` raw_material
4. Takes `conveyor_build_cost / build_rate` ticks to build

### Maintenance

- `condition` decays by `conveyor_decay_rate` per tick (e.g., 0.0005)
- Agents with high `compliance` and nearby broken conveyors get a MAINTAIN utility boost
- MAINTAIN is a new action (or BUILD sub-target on already-built conveyors)

## New Action: `MAINTAIN`

```
ActionType::MAINTAIN  // Repair a degraded conveyor
```

- Agent stands on Conveyor with condition < 1.0
- Restores condition by `maintain_rate` per tick
- Costs nothing but time (opportunity cost: agent isn't working/gathering)
- Utility scales with: compliance × conveyor_importance × (1 - condition)

### Conveyor Importance

How "important" is a conveyor? Based on what it connects:
- Connected to Exit → high importance (factory output!)
- Connected to Machine with built=true → medium (production line)
- Isolated or connected to unbuilt → low

## Production Flow (complete chain)

```
ScrapPile ──GATHER──> Agent Inventory
                          │
                     Agent walks to Conveyor start
                          │
                     Deposit on Conveyor
                          │
              Conveyor ──> Conveyor ──> Machine (raw_material input)
                                           │
                                      WORK produces food
                                           │
                                  Conveyor ──> Conveyor ──> Storage
                                                              │
                                                     Agents EAT from Storage
                                                              │
                                              Conveyor ──> Exit (quota shipped)
```

## Exit Tiles: Factory Output

Currently Exit tiles exist but `system_ship_out_food()` pulls from Storage magically.
With conveyors:

1. Food must reach Storage adjacent to Exit
2. OR a Conveyor must be connected directly to Exit, carrying food
3. Exit ships whatever food arrives via Conveyor OR is in adjacent Storage
4. This creates a physical supply chain that agents must build and maintain

## Implementation Plan

### Step 1: New types in components.h
- `ConveyorDir` enum
- Add conveyor fields to `TileData` (direction, condition, contents_type, contents_amount)
- Add `TileType::Conveyor` to enum
- Add `ActionType::MAINTAIN`

### Step 2: Conveyor tile logic in grid.h
- `place_conveyor(x, y, dir)` helper
- `find_nearest_conveyor_needing_repair()`
- `find_nearest_unbuilt_conveyor()`
- Update `is_walkable()` — conveyors ARE walkable (agents walk on them)
- Update `is_valid_action_tile()` for MAINTAIN

### Step 3: Conveyor transport system
- `system_conveyor_transport()` — each tick, move contents along conveyor chains
- Called in `advance()` after `system_execute_actions()`
- Process order: start from downstream end (near Exit) to avoid double-moving

### Step 4: Build/Maintain actions
- BUILD on Floor with adjacent Conveyor/Machine/Storage/Exit → creates Conveyor segment
- MAINTAIN on degraded Conveyor → restores condition
- Utility formulas for both

### Step 5: Config params
```toml
conveyor_build_cost    = 1.5   # raw_material to build one segment
conveyor_decay_rate    = 0.0005 # condition loss per tick
conveyor_throughput    = 0.5    # max contents that move per tick
maintain_rate          = 0.02   # condition restored per MAINTAIN tick
```

### Step 6: TUI rendering
- Conveyor rendered with directional arrow: > < v ^
- Color coded by condition: green (>0.7), yellow (0.3-0.7), red (<0.3)
- Contents shown as colored dot on the tile
