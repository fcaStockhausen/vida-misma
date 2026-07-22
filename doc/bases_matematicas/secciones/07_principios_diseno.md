# Simulation Design Principles {#sec:diseno}

The principles below are methodological constraints, not claims that every
desired macro-phenomenon has emerged. They distinguish the architecture that
exists, the objectives that guide it, and hypotheses that still require evidence.

## Decomposition and Causal Legibility

**Background.** Decomposition replaces a scripted composite state with smaller
processes that interact through shared state. This idea is common to simulation
game design [@adams2014] and to formal ABM descriptions such as ODD
[@grimm2020]. It increases the number of possible combinations, but complexity
alone does not prove emergence.

**Implemented.** `Simulation::advance()` defines one ordered pipeline: resource
regeneration and need decay; utility, targeting, movement, and execution; social
and spatial learning; conveyor transport and shipping; institutional policy;
artifacts, graph communities, stress, death, lifecycle, and metrics. Action
scoring, target selection, movement, effects, conveyor transport, indifferent
policy, lifecycle, and Director interventions are separate translation units.[^design-pipeline]

**Design objective.** Each aggregate explanation should be traceable through that
pipeline. For example, reduced shipping updates external support, support changes
future resource regeneration, and scarcity can later affect hunger and mortality.
The explanation must not jump directly from "quota missed" to "agent punished."

## Adequacy Rather Than Maximal Fidelity

**Background.** A model is adequate when it preserves distinctions relevant to
its question at the observable scale; more physical detail is not automatically
better.

**Implemented.** The executable uses a bounded $60\times40$ grid, five resource
types, three machine recipes, directed single-content conveyors, and one-tick
updates. It does not model continuous mechanics, fluid dynamics, or a third
spatial dimension. Cached A* provides movement paths over the grid, while agent
choice is mostly bounded to radius-twelve observations.

**Design objective.** Add fidelity only when it changes a measured decision,
logistical constraint, or player-visible consequence. The current known exception
to strict locality is conveyor planning, whose global route query should be
treated as technical debt or explicitly reinterpreted as institutional planning,
not silently described as local perception.

## Hybrid ECS/Grid Architecture

**Implemented.** The runtime is hybrid rather than a pure Entity-Component-System:

| Representation | Current responsibility |
|---|---|
| EnTT registry | inhabitants, artifacts, and their components |
| `Grid` dense arrays | tile type and `TileData` for terrain, resources, machines, storage, conveyors, zoning, and construction |
| Systems | transformations that read and write both representations |
| `SocialFabric` | dynamically sized directed trust/familiarity matrix keyed by stable agent ID |
| `Chronicle` and `SimulationMetrics` | factual event history and aggregate observation |

An inhabitant is an ECS entity with plain-data components, but a wall, machine,
storage tile, or resource source is not an ECS entity. It is a grid cell. Systems
such as execution bridge the two: an entity's `ActionComponent` changes a
`TileData` resource buffer and the inhabitant's inventory in the same causal
operation. Calling the implementation simply "ECS" obscures this deliberate
division.[^design-hybrid]

## Indifferent Institution and Human Indirection

**Implemented.** Canonical `external.policy_variant = 1` applies stable physical
rules to conveyor wear/load, storage occupancy, and anonymous occupancy capacity.
It cannot inspect identity, personality, action, trust, opinion, graph community,
or output semantics. Canonical `external.supply_variant = 1` links only shipped
output to delayed material replenishment. The old strategic policy remains under
variant 0 solely for controlled A/B comparison; it is not the canonical ontology
of the factory.[^design-policy]

**Design objective.** The institution may be demanding and materially harmful,
but it is not a player that hates, classifies, or strategically targets people.
The human Director likewise changes environmental conditions rather than issuing
orders to inhabitants. These are separate boundaries: one governs autonomous
institutional pressure, and the other governs player intervention.

## Iteration, Counterfactuals, and Maturity

**Background.** Iterative development is valuable when each model extension is
observable and can be compared with a baseline. Without a counterfactual, however,
a visually plausible pattern may be only a direct consequence of a coefficient or
map stamp.

**Implemented.** Batch metrics distinguish selection, target lookup, target
arrival, and effective execution. The same seed and build replay deterministically.
Mechanism toggles exist for social learning, place affinity, artifact effects,
lifecycle processes, BUILD, supply variant, and institutional policy. The regular
regression set uses seeds `0 1 2 3 7`, while macro-claims use twenty or more seeds.
Director interventions have their own typed replay ledger.[^design-evidence]

**Design objective.** Documentation uses three maturity levels:

1. **Implemented mechanism:** directly traceable to code, configuration, and a
   focused test.
2. **Design objective:** a normative property such as locality, indirection, or
   causal legibility.
3. **Hypothesis or result:** an aggregate statement requiring a defined metric,
   comparison, horizon, and uncertainty.

**Not established.** Segregation, artistic subculture, informal leadership, and
free-riding have not passed that third standard. Their absence is a valid result
and must not be repaired by introducing a categorical label or privilege merely
to make the pattern visible.

[^design-pipeline]: `src/simulation.cpp` is the scheduling source; subsystem
    boundaries are declared in `src/simulation.h` and compiled through
    `VIDA_SIM_SOURCES` in `CMakeLists.txt`.
[^design-hybrid]: See `src/components.h`, `src/grid.h`, `src/social.h`, and
    `src/chronicle.h`. EnTT architecture background is discussed by
    [@nystrom2014].
[^design-policy]: See `src/sim_policy.cpp`, `src/sim_space_policy.cpp`, and
    `config/default.toml`. `tests/verify_policy_audit.cmake` statically forbids
    behavioral and social dependencies in the canonical policy.
[^design-evidence]: See `src/metrics.h`, `src/batch_main.cpp`,
    `tests/simulation_tests.cpp`, `tests/verify_metrics.cmake`, and
    `tests/verify_replay.cmake`.
