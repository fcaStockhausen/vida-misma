# Design: Conveyor Machine→Machine + Recipe/Blueprint System

> **Status:** Design document — NOT yet implemented. Created after the config-loading
> fix (Part A of the 2026-07-02 plan) so that the production-chain fixes have a
> clear spec to build against.
>
> **Prerequisite done:** config loading now exposes all tuning knobs
> (`config/default.toml` + `src/config.cpp`). This design assumes that is in place.

---

## Goal

Make the production chain self-sustaining across layouts (6/7 seeds ≥ 60% quota)
by giving the colony two capabilities it currently lacks:

1. **Conveyors that feed machines directly** (Materials→Conveyor→Output), not just
   Machine→Storage→Exit. Currently c_mat has no path onto a belt, so Output machines
   can only be fed by an agent physically standing on them.
2. **A colony blueprint** that decides what to build next, so Output machines get
   constructed when the WFC layout produces too few (the seed-1 failure mode).
   Today agents build greedily with no colony-level arbitration.

---

## Ground truth (verified from code, 2026-07-02)

### The 3-tier chain is the real model
```
ScrapPile --GATHER--> raw_material (inv)
       --BUILD--> MaterialsMachine on ScrapPile
       --WORK-->  raw_material consumed → construction_material (c_mat) into agent inv
       --WORK-->  c_mat consumed by OutputMachine → output
       --haul/conveyor--> Exit-adjacent Storage → quota drain
```
- **Output machines eat `construction_material`**, NOT scrap. The README (lines
  137–161) describing a stale 2-machine model where Output eats raw_material is
  WRONG and should be updated separately.
- "Scrap" is not a ResourceType. ScrapPile is a TileType holding raw_material.
- Materials production goes **100% into the working agent's inventory**
  (`sim_execute.cpp:547`); it never touches Storage or conveyor.

### Conveyors today: Machine→Storage→Exit only
- Each conveyor has an explicit `conveyor_dir` (N/S/E/W), routing to ONE neighbor
  (`components.h:343`, `grid.h:405-414`). Not broadcast — deterministic.
- `sim_conveyor.cpp:134-145` transport loop handles Storage, Exit, Conveyor targets
  but **explicitly backs up on Machine targets** (line 145 comment).
- `deposit_to_adjacent_conveyor` (radius-3 scan) is called only for FOOD
  (`sim_execute.cpp:499`) and OUTPUT (`:613`). The CONSTRUCTION_MATERIAL branches
  in `sim_conveyor.cpp:77,97,123` are **dead code** — nothing loads c_mat onto a belt.
- "Near Exit" = Manhattan ≤ 3, hardcoded and duplicated 5×
  (`sim_conveyor.cpp:60`, `grid.h:555,679,714`, `sim_targets.cpp:216`).

### Build system: greedy, no colony arbitration
- Target chosen per-agent via bonus-weighted nearest candidate
  (`sim_targets.cpp:72-191`). No global "what should we build next."
- Machine type is determined by the tile stood on: ScrapPile→Materials,
  FoodSource→Food, Floor+c_mat→Output (`sim_execute.cpp:161-272`).
- WFC places NO machine frames (`wfc_generator.h:491-499`). All machines are
  agent-built. Storage is pre-built; EatingZone pre-placed at center.
- `ColonyProduction::Need::BUILD_OUTPUT` is **set but never read** (dead). The hook
  exists; no consumer. `need_to_preferences` has no case for it.

### Tile buffers (components.h TileData)
- Storage: all 5 resource types.
- Machine tiles buffer their input/output: Food→`stored_raw_food`,
  Materials→`stored_raw_material`, Output→`stored_output` (overflow only).
- Machine tiles do NOT use `stored_construction_material` for buffering today
  (Output pulls it live each WORK tick; the field exists but stays 0).

---

## Part B.1 — Conveyor Machine→Machine (implement first)

### Changes

**1. `sim_conveyor.cpp` (~line 134)** — add a `target == TileType::Machine` branch
in the transport loop. When a belt points at a built machine, transfer the belt's
contents into the machine's input buffer IF the resource matches the machine's diet:
- belt carrying CONSTRUCTION_MATERIAL + target is Output machine → feed it
- belt carrying RAW_FOOD + target is Food machine → feed it
- belt carrying RAW_MATERIAL + target is Materials machine → feed it
- otherwise the belt backs up (preserve current behavior)

Deposit into the existing TileData fields (`stored_construction_material`,
`stored_raw_food`, `stored_raw_material`). Respect a per-tile capacity so a belt
can't infinitely overfill a machine.

**2. `sim_execute.cpp` Materials case (~line 547)** — split c_mat output:
```cpp
// Priority 1: deposit to adjacent conveyor (feeds Output machines downstream)
float cmat_dep = deposit_to_adjacent_conveyor(pos.x, pos.y,
    ResourceType::CONSTRUCTION_MATERIAL, mat_produced);
float cmat_left = mat_produced - cmat_dep;
// Rest: worker carries it (self-delivery to Output)
inv.construction_material += cmat_left;
```
This reactivates the dead conveyor c_mat branches. Agents still haul when no
conveyor is adjacent (robust to layouts without belts).

**3. `sim_execute.cpp` Output case (~line 580-607)** — add Source 4: pull c_mat
from adjacent conveyors (symmetric to `pull_construction_material_from_adjacent_storage`
at `:1355`). A new helper `pull_construction_material_from_adjacent_conveyor`.

**4. `config.h` + `config/default.toml`** — add `exit_proximity_radius = 3` and
replace the 5 hardcoded `<= 3` sites (`sim_conveyor.cpp:60`, `grid.h:555,679,714`,
`sim_targets.cpp:216`) with `config_.exit_proximity_radius`.

### Verify
```sh
for s in 1 3 5 7 10 20 42; do ./build/vida_batch run 1000 $s | grep "Done."; done
```
Success: `constr_mat > 0` in Storage on most seeds (conveyors carrying it), quota
rises on seeds 5/20/42 (the c_mat-starved failures). Seed 1 still fails (no Output
machines — needs B.2).

---

## Part B.2 — Recipe system + Colony Blueprint

### B.2.1 `src/recipes.h` (new, ~150 LOC)

Formalize what each structure costs and requires:
```cpp
struct Recipe {
    MachineType output;            // what machine this builds
    ResourceType input_material;   // raw_material or construction_material
    float input_amount;            // cost
    TileType required_tile;        // ScrapPile / FoodSource / Floor
};
// FoodMachine:      raw_material + FoodSource  → Food
// MaterialsMachine: raw_material + ScrapPile   → Materials
// OutputMachine:    construction_material + Floor → Output
```
This replaces the ad-hoc per-tile-type branching in `sim_execute.cpp:161-272`.

### B.2.2 Extend ColonyProduction (`production.h`)

Add a colony blueprint computed each tick in `assess()`:
```cpp
struct BlueprintItem {
    MachineType type;   // what to build
    int x, y;           // target tile (or -1,-1 for "find a site")
    float priority;     // 0..1 urgency
};
std::vector<BlueprintItem> blueprint;
```
Decision logic in `assess()`:
- `output_machines == 0` && c_mat available → push Output blueprint (priority 1.0)
- output machines exist but 0 connected to Exit → push Conveyor blueprint (priority 0.8)
- `food_machines == 0` → push Food blueprint (priority 1.0)
- `mat_machines == 0` → push Materials blueprint (priority 0.9)

### B.2.3 Activate the dead `Need::BUILD_OUTPUT`

Wire `cp.primary_need == BUILD_OUTPUT` into:
- `sim_utility.cpp:546-562` (`u_build_output`): inflate utility when the colony
  needs Output machines built.
- `sim_targets.cpp:72-191` (BUILD case): when a BlueprintItem exists, give the
  target tile a large bonus so agents converge on it.
- `production.h:136-150` (`need_to_preferences`): add cases for BUILD_OUTPUT and
  the new BUILD_FOOD / BUILD_MATERIALS needs.

### B.2.4 Collaborative conveyor correction (ROADMAP "they can correct between them")

Agents with high compliance + high influence detect dead-end conveyors (belt whose
`conveyor_target` is a Wall/Floor, not Storage/Exit/Machine) and DISMANTLE +
rebuild them pointing toward the Exit. Visual highlight in GUI for broken chains.

### Verify
```sh
for s in 1 3 5 7 10 20 42; do ./build/vida_batch run 1000 $s | grep -E "Done.|Machines:"; done
```
Success: seed 1 now constructs Output machines (was 0). 6/7 seeds ≥ 60% quota.

---

## Part B.3 — Culture consistency (documented for later)

Findings from the consistency audit (not blocking, but documented):

1. **Upper needs don't feed passive stress decay** (`simulation.cpp:521-526`):
   only hunger/rest. Cultural relief (CREATE/SOCIALIZE) is instant-per-action, not
   sustained. The comment overstates the buffering effect. Fix: add a term to the
   decay formula for social/expression satisfaction, OR accept the design as-is.

2. **No sabotage reintegration path**: a saboteur is shunned (trust loss to all
   witnesses, `sim_execute.cpp:1082-1097`), mood drops, no relief except redemption
   (8%/tick) or death. This is the felt "culture spiral." Fix: add a slow trust
   recovery for agents who haven't sabotaged recently.

3. **Three different social-proximity radii** hardcoded: communal eat=5
   (`sim_execute.cpp:714`), socialize=6 (`:800`), sabotage witness=3 (`:1089`).
   Centralize as config constants.

4. **`simulation.cpp:634-635` anti-safety**: at health=0, ALL agents get
   `stress += 0.002`. This accelerates the death spiral rather than offering relief.
   Consider gating or removing.

---

## Success criteria (all parts complete)

- [ ] 6/7 seeds ≥ 60% quota by tick 1000
- [ ] avg alive ≥ 40/48, no seed with alive < 30
- [ ] `constr_mat > 0` in Storage on all seeds (conveyors carrying c_mat)
- [ ] Seed 1 builds Output machines (recipe/blueprint working)
- [ ] All tuning knobs editable from `config/default.toml`

## Implementation order
1. **B.1** Conveyor Machine→Machine (prerequisite for c_mat logistics)
2. **B.2** Recipes + Blueprint (fixes seed 1, adds colony planning)
3. **B.3** Culture consistency (tuning the death spiral)

Each part independently testable. Re-baseline after each — don't pile on.
