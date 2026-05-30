# Phase 8 Fix Plan — Conveyor Issues

## Current State
- Phase 8 compiles clean but conveyors never get built (0/37 at 5000 ticks)
- Root causes identified below

## Fix 1: Conveyor NOT walkable
**File:** `src/grid.h` line 33-36
```
Current:  return t != TileType::Wall;
Target:   return t != TileType::Wall && t != TileType::Conveyor;
```
Conveyors are physical infrastructure — agents walk BESIDE them, not on them.

## Fix 2: BUILD/MAINTAIN on conveyor → agent stands ADJACENT
**Files:** `src/sim_execute.cpp`, `src/sim_targets.cpp`

Currently BUILD checks `grid_.at(pos.x, pos.y)` — the agent's own tile.
For conveyors, the agent must be on an ADJACENT tile and target the conveyor.

In `sim_execute.cpp` BUILD case: when `here == TileType::Floor` and there's an
adjacent unbuilt conveyor, build it from beside. Same pattern for MAINTAIN —
agent stands next to degraded conveyor, repairs it.

In `sim_targets.cpp`: BUILD target for conveyors should be the tile ADJACENT to
the conveyor, not the conveyor itself (since it's not walkable).

## Fix 3: Machines produce food + construction_material
**File:** `src/sim_execute.cpp` WORK case (line ~112)

Currently: `produced = config_.machine_output` → all food.
Change to: produce BOTH food (60%) and raw_material (40%) as separate outputs.
Deposit food to storage/conveyor AND raw_material to storage/conveyor.
This closes the material loop: machines generate material to build more conveyors.

Add to Config: `machine_mat_output = 0.06` (construction material per WORK tick)

## Fix 4: Max 1 EatingZone
**File:** `src/sim_execute.cpp` BUILD case (line ~65)

In the `if (here == TileType::Floor)` block that initiates a new EZ,
add guard: `if (grid_.built_eatingzone_count() >= 1) break;`

In `src/sim_utility.cpp` BUILD sub-3 (new EZ):
skip when `built_ez_exists == true` — already guarded, but verify.

In `src/sim_targets.cpp` BUILD case:
verify `!any_built_ez` check still works with the count limit.

## Fix 5: Target priority — conveyors actually get built
**File:** `src/sim_targets.cpp` BUILD case

Current priority: Machine → Conveyor → EZ → new EZ
Problem: `find_nearest_unbuilt_machine()` always finds something because EZs
keep getting created. The 17/16 count proves this.

Fix: only search for unbuilt machines AFTER conveyors are done, or weight by
what's most needed. Simplest fix: when all Machine tiles have `built=true`,
`find_nearest_unbuilt_machine()` returns {-1,-1} and conveyors get their turn.

Verify that `total_machines_built_++` only fires for `TileType::Machine`,
not for EZ or Conveyor builds.

## Fix 6: Batch validate
Run `./build/vida_batch 3000` and verify:
- Conveyors get built (>0)
- Conv condition decays over time
- MAINTAIN action appears in distribution
- Machines produce both food and raw_material
- Only 1 EatingZone exists
- 0 deaths (no regression)
