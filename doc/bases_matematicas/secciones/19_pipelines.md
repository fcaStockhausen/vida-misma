# Production Pipelines and Physical Conveyor Logistics {#sec:pipelines}

The executable starts with a degraded, inherited three-machine factory. Agents
can operate it without founding the production chain, then maintain, haul, and
extend its logistics. Conveyors are autonomous one-resource buffers, but agents
remain necessary for machine work, construction, maintenance, and fallback
hauling.

## Active Three-Stage Factory

The material transformations are:

$$
\text{raw food}\xrightarrow{\text{FoodMachine}}\text{food},
$$

$$
\text{raw material}\xrightarrow{\text{MaterialsMachine}}
\text{construction material},
$$

$$
\text{construction material}\xrightarrow{\text{OutputMachine}}
\text{output}\xrightarrow{\text{Exit Storage}}\text{shipped quota}.
$$

FoodMachine and MaterialsMachine are inherited on renewable resource deposits
and auto-gather a small amount into their machine buffers. OutputMachine consumes
construction material. Machine transformations still require an agent executing
WORK; conveyors do not operate machines (`src/simulation.cpp`,
`src/sim_execute.cpp`, `src/recipes.h`).

Before the founding population is spawned, WFC places one built machine of each
type, at least three Storages, and four built eastbound conveyors joining the
OutputMachine to Exit-adjacent Storage. Initial belt conditions are 0.55, 0.65,
0.75, and 0.85. Property tests verify this reachable minimum chain over 20 seeds,
and a focused fixture produces all three outputs and ships without any BUILD
action (`src/wfc_generator.h`,
`test_inherited_factory_map_properties()` and
`test_inherited_chain_operates_without_build()` in
`tests/simulation_tests.cpp`).

## Conveyor State and Walkability

Each conveyor uses the general structure fields `built`, `build_progress`, and
`build_cost`, plus (`src/components.h`):

| Field | Executable meaning |
|---|---|
| `conveyor_dir` | one of N, S, E, W; selects exactly one target tile |
| `conveyor_condition` | $[0,1]$ mechanical condition |
| `conveyor_contents_type` | the single resource type currently represented |
| `conveyor_contents` | amount buffered on this segment |
| `maintenance_priority` | anonymous normal/high Director signal |

**Conveyors are walkable.** `Grid::is_walkable()` rejects only Wall, so agents can
stand on and path across built or unbuilt belts. A conveyor cannot be a path
blocker in the current movement model, and `Grid::is_conveyor_blocking_path()`
therefore always returns false (`src/grid.h`, `src/pathfinding.h`).

MAINTAIN and DISMANTLE nevertheless use an adjacent-interaction plan: target
selection sends the agent to a walkable neighbor and execution scans the
eight-neighborhood for the belt. BUILD can complete an unbuilt conveyor frame
from an adjacent Floor path or while standing on the walkable frame
(`src/sim_targets.cpp`, `src/sim_execute.cpp`). There are no separate DEPOSIT or
RETRIEVE actions in `ActionType`; machine output deposition and agent hauling are
effects within WORK and the execution pass.

## One-Resource Transport Semantics

A segment may contain only one resource type at a time. If its amount is nonzero,
a deposit or upstream transfer of a different type is rejected. When empty, its
type tag may be replaced. With configured throughput $\tau=0.5$, the same value
acts both as maximum transfer per tick and as the effective segment capacity:

$$
m=\min(a_c,\tau),\qquad
m_{c\to n}=\min(m,\max(0,\tau-a_n))
$$

for a compatible downstream conveyor. The sender loses exactly the amount the
receiver accepts. This one-type contract and partial-deposit conservation are
covered by `test_conveyors_do_not_mix_resources()` and
`test_partial_conveyor_deposit_preserves_remainder()`
(`tests/simulation_tests.cpp`).

`system_conveyor_transport()` collects built belts and sorts them by increasing
Manhattan distance to the nearest Exit. It then processes each belt once. This
geometric downstream-first order prevents double movement on the inherited chain
and on routes whose every step approaches Exit. It is not a topological sort of
an arbitrary directed network; equal-distance, looping, or temporarily
away-from-Exit layouts do not have a general one-segment-per-tick proof
(`src/sim_conveyor.cpp`).

For a belt whose condition remains at least 0.2 after the current tick's decay,
the target behavior is:

| Target | Transfer rule |
|---|---|
| Built compatible Conveyor | move into free segment capacity; mismatched type, broken target, or full target backs up |
| Built Machine | accept only RAW_FOOD to FoodMachine, RAW_MATERIAL to MaterialsMachine, or CONSTRUCTION_MATERIAL to OutputMachine, up to a small input buffer |
| Storage not near Exit | accept non-output resources up to shared Storage capacity; OUTPUT remains on the belt |
| Storage within Manhattan radius 3 of Exit | accept every represented resource, including OUTPUT, up to capacity |
| Exit | try to deposit into a cardinally adjacent Storage; without one, the belt backs up |
| Floor, Wall, incompatible or unbuilt structure | no transfer |

An Output belt aimed at non-Exit Storage does not pass through that Storage: it
backs up on its current segment. The source comment saying that such output
"continues past" contradicts the implemented branch; there is no pass-through
operation. This remains a code/comment mismatch in `src/sim_conveyor.cpp`.

## Machine-to-Belt and Agent Logistics

Resource injection is specialized rather than represented by a generic action:

- FoodMachine keeps up to 40% of produced food in the worker's inventory, then
  tries Storage and compatible conveyors within the implementation's square
  radius 3.
- MaterialsMachine first deposits construction material onto a visible compatible
  belt whose directed chain reaches a built OutputMachine. Any remainder enters
  the worker's inventory for delivery.
- OutputMachine first deposits output onto a compatible nearby belt, then into
  worker inventory, then nearby Storage, and finally its own machine buffer.
- An agent carrying no output can passively pick up trapped output from an
  OutputMachine in its eight-neighborhood, subject to remaining total inventory
  capacity. It deposits only when standing on Storage within Manhattan radius 3
  of an Exit.

These radius-3 helper scans include diagonal positions and should not be described
as strict adjacency. Agent inventories can mix resource fields up to total
capacity 10; the one-resource restriction belongs to each conveyor segment.

## Exit Storage Is the Sole Shipping Drain

`system_ship_out_food()` is a legacy name for output shipping. Each tick it scans
Storage at Manhattan radii 1, 2, and 3 around every Exit and removes at most the
current output quota. Neither output buffered in a machine nor output in arbitrary
Storage is shipped. A conveyor targeting Exit itself also deposits into qualifying
Storage first; Exit does not consume the belt directly.

Thus the institutional signal is based on physically shipped output:

$$
\text{fill}_t=
\begin{cases}
\text{shipped}_t/\text{quota}_t,&\text{quota}_t>0,\\
1,&\text{otherwise}.
\end{cases}
$$

Under canonical `external.supply_variant = 1`, an EMA of this fill controls later
resource regeneration. Stock trapped outside Exit Storage has no institutional
value. `test_output_haul_requires_storage_arrival()` and
`test_external_supply_causality()` verify the physical arrival, Manhattan radius,
blocked shipping, and next-tick support timing (`tests/simulation_tests.cpp`).

## Degradation, Repair, and Maintenance Priority

Every built belt loses the configured
$\delta=0.0005$ condition during its transport pass, even when empty. Below 0.2
it stops moving resources but remains walkable and repairable. MAINTAIN restores
up to `maintain_rate = 0.08` before the same tick's conveyor decay and transport.
It consumes time but no material.

Maintenance utility is based on observed degradation, continuous compliance,
purpose, hunger suppression, social pressure, and mood. A high-priority belt
multiplies this utility by 1.75. Target selection considers degraded belts within
the 12-tile observation radius and chooses high priority before distance;
execution chooses high priority before worst condition among adjacent belts
(`src/sim_utility.cpp`, `src/sim_targets.cpp`, `src/sim_execute.cpp`).

The Director's priority is therefore a visible anonymous signal, not an order. It
does not repair the belt, assign an agent, or make an otherwise infeasible action
execute (`src/director.h`, `src/sim_director.cpp`).

## Conveyor Construction and the Active Cost Discrepancy

Residents extend routes one frame at a time using a BFS-based site query that
prioritizes the OutputMachine-to-Exit path. Frames consume raw material as BUILD
progress and auto-select a direction along the proposed route
(`Grid::find_conveyor_build_site()`). The construction planner limits pending and
total conveyor counts, but it does not optimize a global flow network.

There is currently **no single effective conveyor build cost**:

| Route | Stored `build_cost` | Reference |
|---|---:|---|
| Resident-created frame on Floor | 0.5, hard-coded at the call site | `src/sim_execute.cpp` |
| Four inherited built belts | 0.15 from WFC placement | `src/wfc_generator.h` |
| Director-placed completed belt | 1.5, hard-coded | `src/sim_director.cpp` |
| Generic `Grid::place_new_conveyor` default | 1.5 | `src/grid.h` |

Dismantle refunds are calculated from each tile's stored cost, so these construction
paths also produce different refunds. The former unused `conveyor.build_cost` TOML
key was removed rather than presented as a universal runtime cost.

## Implemented Mechanism Versus Logistics Hypotheses

The executable establishes physical buffering, type compatibility, capacity,
decay, autonomous movement, manual fallback hauling, and prioritized autonomous
maintenance. It does not yet establish that agents discover globally efficient,
redundant, or robust networks. Single-point failures and collective maintenance
failure are plausible consequences, but require metrics and counterfactual tests.
The only agent-driven removal policy is dead-end dismantling, documented with its
limits in @sec:adaptive-logistics.

## Implementation and Verification References

- State, walkability, route queries, and construction:
  `src/components.h`, `src/grid.h`, `src/pathfinding.h`.
- Production and resource injection: `src/sim_execute.cpp`, `src/recipes.h`,
  `src/production.h`.
- Belt transport and shipping: `src/sim_conveyor.cpp`, `src/simulation.cpp`.
- Active defaults: `[production]`, `[conveyor]`, and `[external]` in
  `config/default.toml`; loading in `src/config.cpp`.
- Map, one-resource, partial-deposit, shipping, and maintenance tests:
  `tests/simulation_tests.cpp`; metrics contract: `tests/verify_metrics.cmake`.
