# The Inhabitants: Current Agent Architecture {#sec:inhabitants}

This section is an implementation account rather than an idealized utility model.
Exact action scores remain in code because they are action-specific and combine
urgency variants, feasibility, personality, mood, skill, local observations,
social evidence, place affinity, and stress.

## State and Motivation {#sec:needs}

**Implemented.** Each living inhabitant is an EnTT entity with position, needs,
personality, action, stress, social state, opinions, inventory, skills, place
memory, creative-work progress, lifecycle, and a private RNG component.[^inhabitants-components]

`NeedsComponent` contains six decaying motivational deficits plus one condition
variable:

| Variable | Range and canonical update |
|---|---|
| `hunger` | $[0,1]$; increases by $0.0035$ per tick, amplified by disease |
| `rest` | $[0,1]$; increases by $0.0040$ per tick |
| `social` | $[0,1]$; increases by $0.003$ per tick |
| `expression` | $[0,1]$; increases by $0.003$ per tick |
| `purpose` | $[0,1]$; increases by $0.002$ per tick |
| `meaning` | $[0,1]$; increases by a hard-coded $0.001$ per tick and is reduced by completed creative work or discovery |
| `disease` | $[0,1]$; physical illness that recovers by $0.002$ per tick and can multiply hunger decay up to $2.0$ |

Canonical action effects include `eat_satisfaction = 0.022`, raw-food efficiency
$0.7$, rest recovery $0.020$, social and creative satisfaction $0.012$, explore
purpose gain $0.008$, and work purpose gain $0.004$. Raw meals have an $8\%$
per-meal disease chance and add $0.15$ disease severity when eaten from personal
inventory. A full processed portion eaten from personal inventory is free of that
risk and accelerates recovery. The nearby-storage helper currently applies raw
food's lower satisfaction but does not perform the disease roll or the processed
food recovery step; this is an implementation asymmetry, not a claimed nutritional
model.[^inhabitants-config]

The six motivational deficits do not all enter stress in the same
way. Social, expression, purpose, and meaning primarily affect utility and mood;
canonical direct stress input comes from critical hunger, critical rest, and
disease.

## Personality and Opinions {#sec:personality-vida}

**Implemented.** Personality has six continuous facets in $[0,1]$:
`compliance`, `laziness`, `artistry`, `gregariousness`, `resilience`, and
`curiosity`. They modulate productive willingness, rest, creation, social contact,
stress susceptibility, exploration, and action response thresholds. They are not
job assignments.

Founders sample one of six archetype profiles and apply bounded jitter to produce
their initial continuous traits and opinion priors. The archetype remains useful
for display, but there is no fixed sequence by ID and no routing by archetype.
Arrivals sample continuous traits independently. A child receives the mean of its
parents' six traits plus bounded mutation of at most `0.08`; it does not inherit a
founder archetype.

The separate `OpinionComponent` stores four continuous stances: work ethic, risk
tolerance, tradition, and solidarity. During effective social contact, sufficiently
close opinions can move through bounded-confidence, influence-weighted exchange.
An opinion may cause local directed disapproval, but canonical institutional
policy cannot read it.

## Five Resources and Inventory {#sec:inventory}

**Implemented.** The resource taxonomy has exactly five values:
`RAW_FOOD`, `RAW_MATERIAL`, `FOOD`, `CONSTRUCTION_MATERIAL`, and `OUTPUT`.
`InventoryComponent` has a slot for every one of them and total carrying capacity
10. The canonical processed-food pocket target is separately limited by
`inv_food_cap = 2.0`; storage and machine buffers hold communal or in-process
stock. Output carried by an agent has no institutional effect until deposited at
Exit-side storage and shipped.

## Four Skills {#sec:skills-vida}

**Implemented.** Skills are `factory_work`, `domestic`, `artistic`, and
`social_skill`, each derived from persistent XP and capped at level 5:

$$
\operatorname{level}(x)=\min(5,x/10).
$$ {#eq:skill-level}

An effective relevant action adds `0.1` XP: GATHER trains domestic, WORK trains
factory, CREATE trains artistic, and SOCIALIZE trains social skill. There is no
forgetting by disuse. Execution uses

$$
\operatorname{level\_bonus}(\ell)=1+0.15\ell,
$$ {#eq:skill-execution-bonus}

for the applicable output or satisfaction, while action utility receives a
smaller $1+0.02\ell$ preference multiplier. Skill therefore creates a smooth
practice-effectiveness-preference feedback, not a profession gate. Focused tests
confirm that artistic and social skills now progress.[^inhabitants-skills]

## Thirteen Actions {#sec:action-component}

**Implemented.** `ActionType` has thirteen selectable values, including `IDLE`:

| Action | Current effect or role |
|---|---|
| `GATHER` | collect visible raw food or raw material |
| `BUILD` | complete or place machines, storage, conveyors, or an EatingZone when material and a valid site exist |
| `WORK` | operate a feasible machine or complete output hauling to Exit-side storage |
| `EAT` | consume processed or raw food from inventory or nearby storage |
| `REST` | recover rest at a scored place |
| `SOCIALIZE` | interact with a nearby inhabitant, update social evidence and opinions, and possibly share food |
| `CREATE` | reduce expression and accumulate a discrete creative work unit at a scored walkable place |
| `EXPLORE` | choose a visible destination, reduce purpose, and possibly discover a hidden place |
| `GET_FOOD` | fetch processed food and available raw material from visible storage |
| `MAINTAIN` | repair a degraded adjacent conveyor |
| `DISMANTLE` | remove an eligible dead-end or blocking adjacent conveyor for a partial refund |
| `SABOTAGE` | under sufficient stress, damage adjacent built infrastructure |
| `IDLE` | explicit always-feasible no-effect alternative |

CREATE is not restricted to `OpenSpace`, and REST or SOCIALIZE is not restricted
to `EatingZone`. Cultural and restorative actions use scored places as described
in @sec:spaces.

## Decision and Movement Pipeline {#sec:tension-engine}

**Implemented.** A decision proceeds through four distinct stages:

1. `system_compute_utility()` scores all thirteen actions and records
   `UtilityBreakdown {self, factory, cost, risk, final, feasible}`. Cost and risk
   are currently zero where no expected term is modeled; the structure does not
   imply that such terms already affect choice.
2. Feasible actions with positive score enter Boltzmann selection at canonical
   temperature $0.4$. `IDLE` contributes score $0.02$; utility-zero or infeasible
   actions have zero probability.
3. `system_find_targets()` chooses a target consistent with the same feasibility
   contracts. Invalidated plans fall back to `IDLE` and are measured separately
   from target failures.
4. `system_move_to_targets()` follows a cached cardinal A* route; execution changes
   state only after the target and spatial preconditions are reached.

Actions remain sticky for action- and personality-dependent durations, but may be
released when no longer feasible or interrupted by survival need. Metrics
separate selection, target lookup, target arrival, and effective execution.

**Implemented, with a locality limitation.** Ordinary physical and social plans
use Manhattan observation radius twelve, and a distant-stock counterfactual
leaves utility, action, and target unchanged. A* may use the complete map after a
target is selected. Conveyor route planning is the explicit exception: its helper
uses factory-wide connectivity before the caller enforces a local returned site.
The architecture is therefore mostly local, not fully local.[^inhabitants-decision]

## Stress, Trauma, and Death {#sec:stress-vida}

**Implemented.** Direct canonical stress input is added when hunger or rest
exceeds $0.7$ and when disease exceeds $0.3$, then reduced by effective resilience.
Passive recovery is

$$
d_t = d_0\left[1+8(1-h_t)(1-r_t)\right],
\qquad d_0=0.005,
$$ {#eq:stress-recovery}

so recovery is faster when hunger and rest are satisfied. SOCIALIZE, CREATE, and
EXPLORE can also reduce stress. Stress above $0.5$ accumulates persistent trauma
at `0.001` per tick; trauma reduces effective resilience and gregariousness.

Canonical `stress_model_variant = 1` derives continuous modifiers from the stress
value: social approach falls, creativity/exploration can rise in a middle band,
work is suppressed near maximum stress, and sabotage becomes possible above
$0.6$. `StressState` labels (`NORMAL`, `DISSOCIATED`, `HOSTILE_EUPHORIA`,
`BROKEN`) are display and Chronicle categories in this variant, not a discrete
behavioral state machine.

Death checks are exclusive within a tick. Starvation requires hunger at 1 for 180
consecutive ticks; exhaustion requires rest at 1 for 200. Above the canonical
breakdown threshold $0.92$, death is probabilistic rather than immediate, with a
per-tick chance shaped from approximately $0.005$ toward $0.003$. Sabotage has a
separate configured suicide chance of $0.03$ per effective sabotage tick. Natural
mortality uses lifecycle age. Canonical supply failure never directly assigns a
factory-collapse death; it acts indirectly through material replenishment and
physical needs.[^inhabitants-stress]

## Lifecycle and Generations

**Implemented.** IDs are unique, monotonic, and never reused; dead entities remain
available for history while releasing physical claims. Founders enter with ages
800--2000 ticks. Lifespan is a deterministic seed-and-ID value around 8000 ticks
with $20\%$ spread, and maturity begins at age 1200.

Arrivals are exogenous attempts at expected rate $0.8$ per 1000 ticks. They enter
through `Entrance`, do not inspect deaths or target population, start without
relationships, and are discarded rather than queued when the living population
has reached `max_population = 200`.

Reproduction is evaluated every 50 ticks for nearby mature pairs. Its continuous
probability combines local food security, hunger, rest, stress, mood, reciprocal
familiarity and trust, age, and a 1500-tick cooldown. Descendants inherit only
mutated personality traits and genealogy. They do not inherit skills, XP,
community labels, archetypes, relationships, opinions, place memory, inventory,
or creative progress. Population can grow, decline, or become extinct without
automatic replacement toward the initial 48 inhabitants.[^inhabitants-lifecycle]

**Established result.** Deterministic 10,000-tick tests exercise historical IDs
beyond simultaneous capacity, multiple cohorts, natural mortality, closed
population accounting, and same-build replay. The Phase 7 five-seed CALM run also
reached generations 2--3 with no founders surviving at tick 10,000.

**Not established.** Generational turnover does not by itself establish cultural
inheritance, occupational dynasties, leadership succession, or subculture. The
implemented inheritance rule deliberately does not copy those labels or states.

[^inhabitants-components]: See `src/components.h`, `src/simulation.cpp`, and
    `src/sim_lifecycle.cpp`.
[^inhabitants-config]: Effective values are in `[needs]`, `[actions]`,
    `[disease]`, and `[stress]` of `config/default.toml`; header values in
    `src/config.h` are fallbacks and may differ.
[^inhabitants-skills]: See `SkillsComponent` in `src/components.h`, utility skill
    multipliers in `src/sim_utility.cpp`, execution XP in `src/sim_execute.cpp`,
    and `test_artistic_and_social_skills_progress`.
[^inhabitants-decision]: See `src/sim_utility.cpp`, `src/sim_targets.cpp`,
    `src/sim_movement.cpp`, and `src/grid.h`. The metrics contract requires
    thirteen action entries and zero target failures in its deterministic fixture.
[^inhabitants-stress]: See stress and death systems in `src/simulation.cpp` and
    sabotage execution in `src/sim_execute.cpp`. Death exclusivity, suicide, and
    natural mortality have focused tests in `tests/simulation_tests.cpp`.
[^inhabitants-lifecycle]: See `src/sim_lifecycle.cpp`, `[lifecycle]` in
    `config/default.toml`, and the lifecycle, inheritance, exogenous-arrival, and
    10,000-tick tests in `tests/simulation_tests.cpp`.
