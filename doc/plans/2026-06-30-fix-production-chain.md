# Plan: Fix the Production Chain

> **Goal:** Make the factory economy self-sustaining. As of the last batch run
> (`vida_batch run 1500 42`), `quota=0%`, `constr_mat=0.0` globally, food works
> fine (123.5 stored). The Materials→Output tier is dead. This plan fixes it.

---

## Diagnosis: Why the Chain Dies

Evidence from `vida_batch run 1500 42` (columns: `tick alive GATHER BUILD WORK EAT REST SOC other | inv_rf inv_rm inv_f | built raw`):

```
1500  43  10  0  13  7  12  0  1 |  5.6  316.0  67.7 |  21  461
```

Three compounding root causes, in order of impact:

### Root Cause 1: c_mat is invisible to the colony planner (THE critical bug)

`production.h:75-80` — `ProductionChain::assess()` counts `construction_material`
**only from Storage tiles**:

```cpp
if (t == TileType::Storage && d.built) {
    cp.construction_material += d.stored_construction_material;  // line 78
}
```

But `sim_execute.cpp:526` — Materials machines put c_mat **directly into agent inventory**:

```cpp
inv.construction_material += mat_produced;  // never touches Storage
```

Result: the planner **always sees `c_mat == 0`** → perpetually routes agents to
`OPERATE_MATERIALS` (production.h:98-102) → c_mat piles up in wandering agents'
inventories → Output machines only receive c_mat when an agent happens to stand
on them → output throughput collapses to a trickle (0.33–0.38 from 2 of 4 machines).

There is also **no deposit path for c_mat**: `sim_execute.cpp:43-61` passively
deposits `inv.output` to Exit-adjacent Storage, but nothing deposits
`inv.construction_material` anywhere. c_mat is stranded in inventory forever
(or until the agent reaches an Output machine).

### Root Cause 2: GATHER over-pulls raw material

`gather_rate = 0.08` (config.h:45). With ~10 agents gathering, raw inflow ≈
0.8/tick. But Materials conversion consumes only `machine_input = 0.04`/work-tick,
so even 4 workers consume 0.16/tick. **Inflow outpaces conversion 5:1.**

Agents hoard raw (`inv_rm = 316` across the colony; each Mat machine buffer
holds `stored_raw_material = 219`). That is 10+ agents' worth of time spent
gathering material that can never be converted. GATHER dominates 24–53% of all
agent time (ROADMAP). The `input_readiness` gate in WORK utility
(sim_utility.cpp:581-583) rewards carrying raw, which compounds the hoarding.

### Root Cause 3: No c_mat logistics leg

Two output-transport paths exist (conveyor chains + agent hauling, see
simulation.cpp:280-340). There is **no equivalent for c_mat**. Materials→Output
has no Storage waypoint, no hauling action, and the planner can't see c_mat in
inventory, so it can't decide "we have enough c_mat, route workers to Output
instead." The chain has a blind spot where the middle tier should be.

---

## Phase 1 — Make c_mat visible to the planner

**Impact:** Highest. Unblocks routing so Output machines get fed.
**Risk:** Low. Read-only assessment change + one deposit hook.
**Files:** `src/production.h`, `src/simulation.cpp`

### 1.1 Count c_mat in agent inventories during assessment

`production.h` `assess()` currently takes `(grid, alive, quota_fill)`. It can't
see inventories. Two options:

- **Option A (preferred):** Pass a pre-computed `float agent_c_mat` total into
  `assess()`. Compute it in `Simulation::advance()` by summing
  `inv.construction_material` across alive agents (one loop), then add it to
  `cp.construction_material`. One new parameter, one loop, no architecture change.
- Option B: Have `assess()` take the `registry_` reference. More invasive, breaks
  the clean Grid-only signature. Reject.

Implement Option A:
- `production.h`: add `float agent_c_mat = 0.0f` parameter to `assess()`;
  `cp.construction_material += agent_c_mat;`
- `simulation.cpp`: in `advance()` before `system_choose_actions()`, sum
  agent c_mat; pass to `assess()`. Cache the result (already cached as
  `colony_production_` — verify the cache is populated before utility runs).

### 1.2 Add a c_mat deposit path (Materials → nearby Storage)

`sim_execute.cpp` Materials WORK case (~line 526) currently does
`inv.construction_material += mat_produced` (100% to inventory). Change to the
same split pattern Food uses (worker keeps some, rest to adjacent Storage):

```cpp
// Worker keeps 30% for self-delivery to Output; 70% banks to adjacent Storage
// so the planner sees it and Output machines can pull it (Source 1/3).
float worker_keep = mat_produced * 0.3f;
inv.construction_material += worker_keep;
float to_store = mat_produced - worker_keep;
float mat_dep = deposit_to_adjacent_storage(pos.x, pos.y,
    ResourceType::CONSTRUCTION_MATERIAL, to_store);
```

This makes c_mat land in Storage where:
- `assess()` already counts it (fixes Root Cause 1 even without 1.1),
- Output machines already pull from it (sim_execute.cpp:560, Source 1).

`deposit_to_adjacent_storage` already handles `CONSTRUCTION_MATERIAL`
(verify the ResourceType branch exists; if not, add it).

### 1.3 Verify

```sh
cmake --build build -j$(sysctl -n hw.ncpu) && ./build/vida_batch run 1500 42
```

Success: `constr_mat > 0` in `Total storage`, Output machines show `out > 0`
on more than 2 of 4, `quota > 0%`. Expect quota 15–30% (improved but not final).

---

## Phase 2 — Stop the raw-material flood

**Impact:** Frees 15–30% of agent time from GATHER into WORK.
**Risk:** Medium. Tuning, not logic — iterate against batch sweeps.
**Files:** `src/config.h`, `src/sim_utility.cpp`

### 2.1 Cap raw gathering by colony need

The cleanest fix (mirrors the `infra_gap` reform that killed BUILD spam): GATHER
utility should fall when the colony already has enough raw material banked.

`sim_utility.cpp` GATHER utility (~the section feeding `u_gather`): multiply by
a `raw_gap` factor computed from `colony_production()`:

```cpp
// colony_production_ already has raw_material total (Storage) at this point
float raw_banked = colony_production_.raw_material;   // Storage total
float raw_need   = std::max(2.0f, alive * 0.3f);      // same scale as c_mat threshold
float raw_gap    = std::min(1.0f, raw_need / std::max(0.5f, raw_banked));
u_gather *= (0.2f + 0.8f * raw_gap);   // never fully zero — survival retains a floor
```

When Storage holds plenty of raw, GATHER utility collapses and agents re-route
to WORK. When raw is scarce, GATHER recovers. This is the `infra_gap` pattern
that already works for BUILD.

**Also count agent-carried raw** in `raw_banked` (add the same agent-summed total
from Phase 1.1, or a separate `agent_raw_material` sum), otherwise hoarded
inventory doesn't suppress gathering.

### 2.2 Tune gather_rate down

`config.h:45` `gather_rate = 0.08` → `0.05`. Reduces inflow so the buffer doesn't
swamp conversion. Revisit after Phase 2.1 lands (the gap gate may make this
unnecessary — test both).

### 2.3 Verify

```sh
./build/vida_batch run 1500 42
```

Success: GATHER column drops from 10→~4 at tick 1500; WORK rises to ~18–20;
`inv_rm` falls below 50. Quota should climb to 30–50%.

---

## Phase 3 — c_mat hauling leg (optional but recommended)

**Impact:** Makes the chain robust across layouts (seed variance).
**Risk:** Low. Reuses the existing hauling pattern.
**Files:** `src/sim_execute.cpp`, `src/sim_targets.cpp`

Only do this if Phase 1+2 don't reach quota ≥ 60%. The passive deposit from 1.2
may suffice for compact layouts.

### 3.1 Passive c_mat deposit near Output machines

Mirror the `inv.output` hauling block at `sim_execute.cpp:43-61`. Add a block
for `inv.construction_material`: if carrying c_mat and adjacent to an Output
machine (or Storage near an Output machine), deposit it. This lets c_mat-carrying
agents drop their load at the right place instead of wandering.

```cpp
if (inv.construction_material > 0.05f) {
    // Deposit to Storage adjacent to a built Output machine (the consumer)
    // ... scan radius 3 for Output machines, then Storage within radius 1 of them
}
```

### 3.2 Verify

```sh
./build/vida_batch run 1500 42
```

Success: quota ≥ 60%, all 4 Output machines active (`out > 0`).

---

## Phase 4 — Balance & throughput tuning

**Impact:** Hits the ROADMAP success criteria.
**Risk:** Low (numbers only).
**Files:** `src/config.h`, `src/config/default.toml`

After Phases 1–3, run a 7-seed sweep and tune:

| Parameter | Current | Target range | Why |
|---|---|---|---|
| `quota_per_tick` | 0.10 | 0.08–0.10 | Lower early pressure |
| `quota_growth_rate` | 0.00002 | 0.00001–0.00002 | Slower escalation |
| `machine_mat_output` | 0.08 | 0.08–0.12 | If c_mat still starves Output |
| `machine_out_output` | 0.06 | 0.06–0.08 | Output throughput |
| `health_decay_per_miss` | 0.0005 | 0.0003–0.0005 | More recovery time |
| `gather_rate` | 0.08 | 0.05 (from 2.2) | Cuts flood |

### Verify (success criteria, from ROADMAP + this plan)

```sh
# 7-seed sweep
for s in 1 3 5 7 10 20 42; do ./build/vida_batch run 1000 $s; done
```

- **6/7 seeds** quota ≥ 60% by tick 1000
- avg alive ≥ 40/48
- No seed with alive < 30
- `constr_mat > 0` in every seed
- GATHER ≤ 20% of agent time (was 24–53%)

---

## Implementation Order

```
1.1  c_mat visible to planner        (~30 min)  — THE fix
1.2  Materials → Storage deposit      (~20 min)
1.3  verify: constr_mat > 0, quota > 0%
2.1  GATHER raw_gap gate              (~30 min)
2.2  gather_rate tune                 (~5 min)
2.3  verify: GATHER drops, WORK rises
3.1  c_mat hauling (if needed)        (~30 min)
3.2  verify: quota ≥ 60%
4    7-seed sweep + balance           (~45 min)
```

Each phase is independently testable. **Phase 1 alone should move quota from
0% → 15–30%.** Stop and re-baseline after each phase — if a phase doesn't help,
don't pile on the next; investigate why first.

---

## Out of scope

- Conveyor reliability (separate subsystem; conveyors are bonus, not load-bearing,
  since agents haul output directly)
- Recipe/blueprint system (ROADMAP Priority 1 — depends on a working chain first)
- Narrative, social, director systems (already implemented; untouched here)
