# The Factory: World Substrate and Institution {#sec:factory}

The factory has two compatible descriptions. As a **substrate**, it is the grid,
resources, machines, storage, conveyors, Entrance, and Exit. As an
**institution**, it imposes an output demand and updates material support through
anonymous rules. It is not implemented as a strategic player in a zero-sum game.

## Grid and Tile Model {#sec:tile-types}

**Implemented.** The canonical configuration creates a bounded $60\times40$
grid. Tile type and mutable `TileData` are stored in dense arrays, while
inhabitants and artifacts are EnTT entities. This is the hybrid ECS/Grid
architecture described in @sec:diseno.

| Tile type | Current role |
|---|---|
| `Floor` | ordinary buildable and usable space |
| `Wall` | the only non-walkable tile type |
| `Machine` | built or incomplete Food, Materials, or Output machine |
| `Storage` | shared buffer for all five resources |
| `Exit` | boundary associated with the sole institutional output drain |
| `Entrance` | boundary at which exogenous arrivals enter |
| `OpenSpace` | spatial label without an exclusive cultural-action privilege |
| `FoodSource` | renewable `RAW_FOOD` deposit and FoodMachine site |
| `ScrapPile` | renewable `RAW_MATERIAL` deposit and MaterialsMachine site |
| `EatingZone` | inherited or buildable label without canonical satisfaction or capacity bonus |
| `Conveyor` | directed, degrading, single-resource transport segment |
| `HiddenSpace` | discoverable place; canonical closure uses anonymous occupancy capacity rather than cultural meaning |

All non-wall tiles, including built conveyors and machines, are currently
walkable. Earlier descriptions of active belts as impassable are obsolete. Agent
movement uses cardinal cached A*, and a tile can hold at most six moving
inhabitants.[^factory-grid]

Resource tiles and source-backed machines retain an amount, maximum, and base
regeneration rate. Storage has a shared capacity and separate buffers for the
five resources. Conveyors retain direction, condition, maintenance priority, one
content type, and one content amount. A condition below $0.2$ stops transport;
`MAINTAIN` can restore a degraded belt.

## Inherited Three-Machine, Two-Branch Factory {#sec:production-chain}

**Implemented.** The generated world contains an operational but degraded
factory before the initial population appears. For every one of twenty tested
seeds, the map contains one built machine of each subtype, at least three built
storages, one Entrance, one Exit, and four built output conveyors with initial
conditions $0.55$, $0.65$, $0.75$, and $0.85$. The OutputMachine is connected to
the Exit-side storage, and all three machines are reachable. Inherited structures
are counted separately from later construction by inhabitants.[^factory-inherited]

The three machines form two physical branches:

1. **Food branch:**
   `FoodSource` $\rightarrow$ FoodMachine $\rightarrow$ processed `FOOD`.
   A source-backed FoodMachine regenerates and auto-gathers `RAW_FOOD`; effective
   `WORK` consumes that input. Part of the result can remain with the worker and
   the remainder is deposited into nearby storage or a conveyor.
2. **Institutional output branch:**
   `ScrapPile` $\rightarrow$ MaterialsMachine $\rightarrow$
   `CONSTRUCTION_MATERIAL` $\rightarrow$ OutputMachine $\rightarrow$ `OUTPUT`
   $\rightarrow$ Exit-side storage $\rightarrow$ shipment.
   The MaterialsMachine is source-backed, produces construction material, and
   recycles part of its byproduct into nearby scrap deposits. The OutputMachine
   consumes construction material. Output reaches the drain only by a directed
   conveyor route or by an inhabitant hauling it to storage within Manhattan
   radius three of the Exit.

Manual `GATHER`, hauling, additional construction, repair, rerouting, and storage
remain useful, but `BUILD` is not the founding act of the colony. A focused
fixture operates all three inherited machines and ships output without selecting
or executing `BUILD`.

## Resources and Logistics

**Implemented.** The resource set is exactly:

| Resource | Main production or use |
|---|---|
| `RAW_FOOD` | gathered or auto-gathered from FoodSource; FoodMachine input; edible raw with disease risk |
| `RAW_MATERIAL` | gathered or auto-gathered from ScrapPile; MaterialsMachine and construction input |
| `FOOD` | processed nutrition consumed by inhabitants |
| `CONSTRUCTION_MATERIAL` | MaterialsMachine output; input to OutputMachine operation and construction |
| `OUTPUT` | OutputMachine product; the only resource that satisfies institutional demand after shipment |

A conveyor carries one resource type at a time and rejects a different type until
emptied. Contents move at configured throughput toward another conveyor, a
compatible machine buffer, storage, or the Exit-side storage. Output in a remote
storage, machine buffer, inventory, or disconnected belt is physically present
but institutionally unshipped. Partial deposits preserve the undelivered
remainder.[^factory-logistics]

The canonical configuration uses renewable FoodSource and ScrapPile deposits.
Their effective regeneration is the sole variable controlled by delayed external
support. They are not accurately described as fixed finite budgets.

## Shipped Output and Delayed Material Support

**Implemented.** Let $q_t$ be demand during tick $t$, let $y_t$ be output actually
drained from Exit-side storage, and define

$$
f_t =
\begin{cases}
\operatorname{clamp}(y_t/q_t,0,1), & q_t>0,\\
1, & q_t=0.
\end{cases}
$$

External support is an exponential moving average,

$$
s_t = s_{t-1} + \left(1-e^{-1/T}\right)(f_t-s_{t-1}),
$$ {#eq:external-support}

with canonical response time $T=600$ ticks. Support is mapped to a regeneration
factor by

$$
r_t = r_{\min} + (1-r_{\min})
\operatorname{smoothstep}\!\left(
\operatorname{clamp}\!\left(\frac{s_t-s_{\mathrm{low}}}
{s_{\mathrm{high}}-s_{\mathrm{low}}},0,1\right)\right),
$$ {#eq:external-supply-factor}

where `supply_floor = 0.20`, `supply_low = 0.05`, and
`supply_high = 0.45`. The factor multiplies only FoodSource and ScrapPile
regeneration. Regeneration precedes shipping in the tick pipeline, so the support
update from tick $t$ affects replenishment no earlier than tick $t+1$.

Only **shipped `OUTPUT`** drives this delayed support. Producing output, storing
it, or moving it near the Exit without allowing the drain does not count. Missed
demand does not directly change utility, stress, machine efficiency, or mortality
under canonical `external.supply_variant = 1`. Hunger, exhaustion, breakdown,
suicide, and natural mortality remain the death mechanisms; there is no canonical
"factory health equals zero, therefore everyone dies" rule.[^factory-supply]

`factory_health` remains as an aggregate condition diagnostic: built machines
contribute one and built conveyors contribute their condition. In the canonical
supply variant it is not the moral score or survival cause described by older
drafts.

## Indifferent Canonical Policy

**Implemented.** `external.policy_variant = 1` is canonical. At configured
restructure epochs, a seed-, epoch-, and position-derived stable hash gates the
event. Candidate priorities use only anonymous physical state:

- a built conveyor may be selected from wear and current load, with stable
  physical jitter;
- a nonempty storage may be selected from total occupancy fraction, with stable
  physical jitter.

The highest-priority conveyor loses $0.15$ condition without crossing the
repairable floor $0.20$, or the highest-priority storage loses the same $10\%$
fraction of each stored resource. This restructure rule does not read the agent
registry. A separate overcapacity rule counts living positions anonymously; it
may read only `alive` and position, never agent IDs, actions, behavioral targets,
personality, opinions, trust, noncompliance, graph communities, utility, output
stock as a special category, or behavioral RNG.[^factory-policy]

`external.policy_variant = 0` preserves the historical strategic policy and its
legacy social sanctions only for explicit A/B comparison. It is not the default
and must not be used to explain the canonical factory as an adversarial mind. In
`CALM`, quota demand, supply contraction, restructuring, deterioration, Watcher
logic, and closure pressure are disabled.

## Claims and Open Questions

**Design objective.** The inherited layout should make dependence material and
repairable: sustained shipping supports future inputs, while sustained blockage
contracts them gradually and leaves a nonzero recovery floor. The Director can
alter physical conditions without commanding inhabitants (@sec:director).

**Established result.** In a twenty-seed, 3000-tick blocked-Exit comparison with
restructuring disabled, blocking shipment reduced survivors in 14/20 seeds and
increased starvation deaths in 18/20. In the recorded reopening experiment,
support recovered before stocks, and survivor counts were better in four of five
seeds with one tie. These results support the delayed material pathway at the
tested horizons; they do not imply that every seed must respond monotonically.

**Not established.** The grid and production chain do not by themselves establish
territories, segregation, public-goods equilibria, free-riding, or collective
leadership. Those are separate empirical questions, not consequences that may be
inferred from using a two-dimensional lattice.

[^factory-grid]: Implementation references: `src/grid.h`, `src/components.h`,
    `src/sim_movement.cpp`, and `[grid]` in `config/default.toml`.
[^factory-inherited]: `src/wfc_generator.h` stamps the inherited chain.
    `test_inherited_factory_map_properties` and
    `test_inherited_chain_operates_without_build` in
    `tests/simulation_tests.cpp` verify twenty generated maps and no-BUILD
    operation.
[^factory-logistics]: See `src/recipes.h`, `src/sim_execute.cpp`, and
    `src/sim_conveyor.cpp`. Focused tests cover single-type belts, direct machine
    feeding, required hauling arrival, and partial deposits.
[^factory-supply]: See `Simulation::system_ship_out_food()` and
    `Simulation::system_regen_resources()` in `src/simulation.cpp`, plus
    `[external]` in `config/default.toml`. `test_external_supply_causality` and
    `tests/verify_metrics.cmake` check the active variant and causal metrics.
[^factory-policy]: See `src/sim_policy.cpp` and `src/sim_space_policy.cpp`.
    `test_indifferent_policy_ignores_social_state`,
    `test_indifferent_storage_policy_is_resource_neutral`, and
    `tests/verify_policy_audit.cmake` enforce the boundary.
