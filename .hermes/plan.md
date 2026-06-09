# Refactor: Machine → Output → Quota Pipeline

## Diagnostic Summary

The current system has 6 critical failures:

1. **Fallback global en quota**: `system_ship_out_food()` drains `stored_output` from ALL Storage on the map, not just Exit-adjacent. Conveyors are completely unnecessary for quota fulfillment.
2. **Conveyor sort broken**: `system_conveyor_transport` sorts by distance to a FIXED position `(width-1, height/2)`, not the actual Exit tile position.
3. **OutputMachine requires MaterialsMachine chain**: Output needs `construction_material` input, which requires a MaterialsMachine consuming raw material first. 3-step chain is too fragile.
4. **Conveyors block movement**: Built conveyors are non-walkable, creating physical walls that trap agents.
5. **Conveyor→Exit bypasses quota system**: Conveyor dump at Exit adds to `total_food_shipped_` but `system_ship_out_food` doesn't count it toward quota fill.
6. **Output types conflated**: Conveyor carries any ResourceType but only OUTPUT should feed quota. No filtering.

## Refactor Plan (5 phases)

### Phase 1: Eliminate Global Quota Fallback
**File**: `src/simulation.cpp` — `system_ship_out_food()`
**Goal**: Output MUST physically reach Exit-adjacent Storage to count as quota.

- Remove the fallback that drains from ALL Storage on the map
- Only drain `stored_output` from Storage within radius 3 of an Exit tile
- If Exit-adjacent Storage is empty, quota fill = 0% → factory health drops
- This makes conveyors MEANINGFUL: they're the only way to move output from distant OutputMachines to Exit-adjacent Storage

**Behavior change**: quota will start at 0% until agents build conveyors or OutputMachines near Exit. This is intentional — it's the core tension.

### Phase 2: Fix Conveyor Transport
**File**: `src/sim_conveyor.cpp` — `system_conveyor_transport()`
**Goal**: Conveyors reliably transport output toward Exit-adjacent Storage.

Changes:
- Find actual Exit tile positions from the grid (not hardcoded estimate)
- Sort conveyors by distance to nearest Exit (process downstream first)
- When conveyor flows into Exit: deposit to adjacent Storage (don't bypass quota system)
- Add overflow protection: if target Storage is full, don't lose contents — keep in conveyor
- Filter: only OUTPUT-type conveyor contents contribute to quota

### Phase 3: Simplify OutputMachine Input Chain
**File**: `src/sim_execute.cpp` — OutputMachine WORK case
**Goal**: OutputMachine should be self-sufficient when placed on a ScrapPile.

Current chain: `ScrapPile → GATHER → MaterialsMachine → construction_material → OutputMachine → output`
This requires 3 agent roles (gatherer, materials worker, output worker) to produce a single unit of output. Too fragile.

Simplified chain: `ScrapPile + OutputMachine → output` (auto-gather from tile + convert)
- OutputMachine on ScrapPile: auto-gathers raw_material from tile, converts to output directly
- Rate: `0.03 * efficiency * health_eff` per tick (self-sustaining, no construction_material needed)
- OutputMachine NOT on ScrapPile: requires raw_material from adjacent Storage (from GATHER agents)
- Remove construction_material dependency entirely from OutputMachine

This makes OutputMachine viable as a standalone building on resource tiles, which is the WFC layout intent.

### Phase 4: Conveyors Non-Blocking
**File**: `src/grid.h` — `is_walkable()`, `components.h` — TileData
**Goal**: Conveyors don't create impassable walls.

Option A (preferred): Conveyors are walkable. Agents walk over them. They're conveyor belts, not walls.
- Change `is_walkable()`: remove the `Conveyor && built` non-walkable check
- Conveyor visual: agents shown walking on conveyor tiles (immersion: the belt keeps moving)
- Conveyor transport still works: items flow, agents walk over

Option B (if Option A feels wrong): Conveyors are semi-blocking — only block orthogonal to flow direction.
- N/S conveyor blocks E/W movement but allows N/S (and vice versa)
- More complex but creates "corridors" that feel realistic

Going with Option A for simplicity. The factory is already hostile — conveyors shouldn't be physical walls.

### Phase 5: Balance Tuning
**Files**: `src/config.h`, `src/simulation.cpp`

After the pipeline refactor, tune for fun gameplay:

- **Early game viability**: First OutputMachine on ScrapPile near Storage near Exit should produce enough to meet initial quota
- **Quota ramp**: `quota_per_tick` starts low (0.05), grows slowly (0.00001/tick), caps at 3x
- **Conveyor cost**: Reduce from 0.02 mat/tile to make early conveyor chains affordable
- **Storage near Exit**: Ensure WFC always places 2+ Storage tiles within radius 3 of Exit
- **Factory health decay**: Slower when quota missed (0.0003 vs 0.0005) — gives agents time to build infrastructure

## Testing Protocol

After each phase:
1. `cmake --build build -j$(sysctl -n hw.ncpu)` — clean build
2. Single seed sanity: `./build/vida_batch run 200 42` — check quota > 0%
3. 7-seed sweep: seeds 1,3,5,7,10,20,42 @ 500 ticks
4. Metrics: alive, built, conv, quota%, time

Success criteria:
- 6/7 seeds quota >= 80% by tick 500
- avg alive >= 18/24
- No seed with alive < 10

## Files to Modify (by phase)

| Phase | File | Changes |
|-------|------|---------|
| 1 | `src/simulation.cpp` | Remove global fallback in `system_ship_out_food()` |
| 2 | `src/sim_conveyor.cpp` | Fix sort order, find real Exit, fix Exit→Storage deposit |
| 2 | `src/simulation.cpp` | `system_conveyor_transport()` call site if needed |
| 3 | `src/sim_execute.cpp` | Simplify OutputMachine WORK case |
| 4 | `src/grid.h` | `is_walkable()` — remove Conveyor blocking |
| 5 | `src/config.h` | Quota/conveyor tuning |
| 5 | `src/wfc_generator.h` | Ensure Storage near Exit |

## Out of Scope (for this refactor)
- Pathfinding optimizations (A* already works fine with TTL cache)
- Type index cache (premature optimization for this grid size)
- Social systems, factions, artifacts
- GUI changes
