# Adaptive Logistics: Implemented Dismantling and Open Hypotheses {#sec:adaptive-logistics}

The current adaptive-logistics mechanism is narrower than a general
dismantle-and-rebuild system. Agents can identify and remove certain dead-end
conveyors. They cannot detect conveyor path blockers, are not committed to rebuild
what they remove, and do not compare the efficiency of the old and new network.
Claims of self-organizing infrastructure therefore remain hypotheses.

## Implemented Candidate: Protected Dead Ends {#sec:dismantle-action}

`Grid::is_dismantle_candidate()` accepts only a built Conveyor whose directed
target remains inside the grid and is not:

- Storage or Exit;
- another built Conveyor;
- a built Machine.

A belt pointing outside the grid is rejected rather than classified as a dead
end.

It then protects the belt if any cardinal neighbor is a built Machine or built
Conveyor. This grace rule prevents the agent from removing a segment adjacent to
a chain that may still be under construction. `find_nearest_dead_end_conveyor()`
returns the nearest remaining candidate (`src/grid.h`).

Earlier designs also proposed a **path-blocker** category. It is inactive:
conveyors are walkable, and `Grid::is_conveyor_blocking_path()` unconditionally
returns false. The blocker scans left in utility, target selection, and execution
therefore produce no candidate and no priority. DISMANTLE currently means
dead-end removal only.

## Selection, Targeting, and Execution

Utility observes candidates within the same 12-tile local window used by agent
decision-making. Because the blocker branch is false, the active drive is

$$
U_D = c_{\mathrm{eff}}\,(0.6)\,
      \max(0,1-3u_h)\,
      \max(0,2m-0.5)\,
      (1-0.5\ell)\,g_m,
$$

where $c_{\mathrm{eff}}$ is effective compliance, $u_h$ is hunger urgency,
$m$ is mood, $\ell$ is laziness, and $g_m$ is the general mood factor. Contrary
to the older design, hunger suppresses dismantling rather than boosting it. If
the agent already carries more than 2 units of raw material, utility is multiplied
by 0.3 (`src/sim_utility.cpp`).

Target selection chooses the nearest dead end within observation radius and sends
the agent to a walkable neighboring tile. Execution then scans the agent's
eight-neighborhood, rechecks built state and chain protection, and removes the
highest-scoring candidate. With no active blocker score, a valid dead end receives
score 1 (`src/sim_targets.cpp`, `src/sim_execute.cpp`).

## Physical Effect and Refund

Successful execution:

1. records the belt's resource type and amount as lost;
2. changes the tile type to Floor;
3. resets built state, progress, condition, and contents;
4. returns
   $\text{build\_cost}\times\text{dismantle\_return}$ as raw material, capped by
   a hard-coded inventory value of 10;
5. stores `dismantled_by`, `dismantled_at_tick`, and `original_type` on the Floor
   tile;
6. provides a small purpose-need reduction and emits a factual DISMANTLED event.

The default return fraction is 0.5 (`[dismantle]` in
`config/default.toml`). The refund uses the tile's stored `build_cost`, not a
universal recipe. @sec:pipelines documents the unresolved cost split:
resident-created, inherited, and Director-created belts currently store different
costs, so they yield different nominal refunds.

## There Is No Enforced Rebuild Cycle {#sec:rebuild-cycle}

After dismantling, the acting agent receives no destination, obligation, sticky
state, or reserved material for reconstruction. Ordinary BUILD may later place a
frame selected by the independent machine-to-destination BFS planner, but that
planner does not know who dismantled the old belt, does not promise to fill the
same tile, and does not compare route throughput before and after removal.

Consequently, the executable does not implement the proposed sequence
"detect, dismantle, navigate, rebuild at a better position" as one policy. It
implements two independent possibilities: removal of a protected dead end and
ordinary future construction. Any observed rebuild association must be measured,
not inferred from those two mechanisms.

## Repeated Unrebuilt-Site Penalty {#sec:dismantle-penalty}

`system_check_dismantle_penalties()` scans Floor tiles retaining dismantle
metadata after social relationship decay. For default rebuild window $w=200$, it
acts on every tick satisfying

$$
w\leq t-t_D\leq w+50.
$$

This is an inclusive 51-tick exposure period, not a one-time penalty. It applies
only while the original dismantler is alive. On each exposure tick, every other
living agent within Manhattan distance $d\leq6$:

- records a negative observation on its own directed edge
  `observer -> dismantler`, with severity
  $0.05(1-d/7)$;
- receives a stress increment of 0.003, clamped to $[0,1]$.

Thus repeated observers repeatedly lose trust and repeatedly gain stress. The
relationship update is disabled when `social_learning_enabled = false`, but the
stress increment still occurs because it is applied separately. After
$t-t_D>w+50$, metadata remains but the system performs no further penalty
(`src/simulation.cpp`, `src/social.h`).

The scan requires the site still to be Floor. Building any non-Floor structure on
that exact tile stops future checks because the tile no longer matches, even
though the resident conveyor-build path does not explicitly clear the metadata
fields. Director replacement assigns fresh `TileData`, but the penalty scan still
depends first on tile type. Rebuilding somewhere else does not clear the original
site's exposure. Stopping future checks also does not restore trust or stress
already changed.

## What the Mechanism Can and Cannot Support

| Claim | Current status |
|---|---|
| Agents can remove isolated built belts that point nowhere useful | Implemented |
| Dismantling loses carried belt contents and refunds part of stored cost | Implemented |
| Nearby observers can repeatedly penalize a surviving dismantler when the exact Floor site remains empty | Implemented |
| Conveyors can block paths and be removed to reopen corridors | False under current walkability semantics |
| The dismantler is required or incentivized by explicit state to rebuild | Not implemented |
| A new segment is guaranteed at the old site or at a better site | Not implemented |
| The system evaluates route efficiency, redundancy, throughput, or bottlenecks before removal | Not implemented |
| Repeated dismantlers become socially isolated | Plausible but unverified outcome |
| Skilled logistics maintainers become valued leaders | Plausible but unverified outcome |
| The network self-organizes toward greater efficiency | Plausible but unverified outcome |

No schema-3 field currently links dismantle events, later builds, route length,
throughput improvement, or the dismantler's social trajectory. The deterministic
suite covers conveyor planning purity, one-resource transport, physical shipping,
and maintenance, but it has no focused behavioral test for dead-end dismantle
selection or the 51 repeated penalty ticks. These are evidence gaps, not evidence
that the proposed outcomes occur.

## Required Evidence for Stronger Claims

A self-organizing-logistics claim would require, at minimum, event linkage between
removal and construction, a physical route-quality measure fixed before the
experiment, and a multiseed comparison against dismantling disabled. A social-cost
claim would additionally require directed trust trajectories with the penalty on
and off. Until such tests exist, the academically defensible conclusion is only
that dead-end removal and a repeated local social/stress penalty are active.

## Implementation and Verification References

- Candidate, walkability, refund base, and construction planner: `src/grid.h`.
- Utility, targeting, and action effect: `src/sim_utility.cpp`,
  `src/sim_targets.cpp`, `src/sim_execute.cpp`.
- Repeated penalty and tick placement: `src/simulation.cpp`.
- Directed negative evidence: `src/social.h`.
- Configuration: `[dismantle]` and `[conveyor]` in `config/default.toml`, loaded by
  `src/config.cpp` into `src/config.h`.
- Existing adjacent logistics coverage and the current focused-test gap:
  `tests/simulation_tests.cpp`; structured output contract:
  `tests/verify_metrics.cmake`.
