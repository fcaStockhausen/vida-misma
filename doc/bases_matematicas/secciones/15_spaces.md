# Emergent Spaces and Place Affinity {#sec:spaces}

This section uses "emergent space" in a restricted, measurable sense: repeated
spatial association that is not imposed by an exclusive action zone. It does not
use the term as evidence of segregation, territory, subculture, free-riding, or
leadership.

## Physical Substrate and Affordances

**Implemented.** The $60\times40$ generated layout contains walls, ordinary floor,
OpenSpace clusters, a central inherited EatingZone, renewable resource deposits,
an Entrance, an Exit, and the inherited production chain. These stamps are
designed physical conditions, not emergent zones. The presence of a central plaza
or eating area therefore cannot itself be counted as self-organization.

Tile types constrain production actions: GATHER requires a source, WORK a machine
or output destination, and maintenance/dismantling/sabotage nearby infrastructure.
In contrast, REST, SOCIALIZE, and CREATE do not receive a categorical bonus from
EatingZone or OpenSpace. CREATE is effective on ordinary Floor, and EatingZone
provides no canonical utility, satisfaction, movement-capacity, or community
privilege.[^spaces-affordances]

The human Director can set anonymous occupancy capacity on Floor or OpenSpace,
place or remove infrastructure, and change maintenance priority. These are
designed interventions and must be recorded separately from learned place use.

## Personal Place Memory

**Implemented.** Every inhabitant stores at most 24 `PlaceMemoryEntry` records.
An entry contains coordinate, affinity in $[-1,1]$, exposure count, and last tick.
After action execution, the current place receives an outcome based on the
inhabitant's stress, excessive crowding, and whether an action had an effect.
Effective eating, resting, socializing, and creating contribute positive evidence;
effective work and building contribute a smaller positive value; sabotage and
high stress contribute negative evidence. Repeated exposures update affinity
toward the observed outcome, while the least recently used entry is replaced when
memory is full.[^spaces-learning]

This is individual episodic affinity, not an objective tile quality and not a
shared cultural label. Two inhabitants may evaluate the same coordinate
differently because their histories differ.

## Scored Places

**Implemented.** `find_preferred_place()` plans REST, SOCIALIZE, and CREATE from a
candidate set containing the current tile, a stride-two sample of walkable tiles
within Manhattan radius twelve, visible inhabitants, visible artifacts, and
remembered places. Scores combine action-specific terms:

| Action | Positive observations | Negative observations |
|---|---|---|
| REST | learned affinity, limited food access, personally valued artifacts | traffic, machinery noise, conveyor hazard, travel |
| SOCIALIZE | affinity, familiar/trusted people, moderate local presence | excessive traffic, noise, hazard, travel |
| CREATE | affinity, known people, personally valued artifacts | noise, hazard, excessive traffic, travel |

The table describes implemented score terms, not guaranteed destinations. Place
score only slightly modulates action utility; need, personality, stress, skill,
and stochastic Boltzmann choice still matter.

**Locality boundary.** Current conditions are read only within observation radius
twelve. A remembered place outside that radius may remain a candidate, but its
score uses memory and travel cost rather than querying remote current traffic,
people, food, hazard, or artifacts. Once selected, cached A* can inspect the full
grid to route around walls. This distinguishes remembered destination choice from
omniscient observation. Conveyor construction remains the separate global
planning limitation described in @sec:agentes and @sec:inhabitants.

## Social Proximity Without a Privileged Zone

**Implemented.** Copresence within two tiles increases reciprocal familiarity but
does not create trust. Effective shared BUILD, WORK, or CREATE creates reciprocal
collaboration evidence. SOCIALIZE chooses among people within execution radius
using familiarity, nonnegative trust, and distance, then may update opinions,
stress, social skill, and food sharing. Eating near at least two others can satisfy
a small amount of social need at any place. These mechanisms can make repeatedly
used locations socially consequential without declaring them a home, faction, or
subculture.

Graph-community labels are derived from reciprocal trust and familiarity every
fifty ticks, but behavior is prohibited from reading those labels. Spatial and
social persistence are therefore observations over continuous relations, not
bonuses attached to a named group.

## Measured Persistence Result

**Established, limited result.** Phase 6 recorded a paired CALM comparison over
20 seeds at 1000 ticks. All variants retained 48/48 inhabitants. Mean temporal
Jaccard persistence of pairs within Manhattan radius three was:

| Variant | Mean spatial persistence |
|---|---:|
| Full mechanisms | 0.238 |
| Spatial affinity disabled | 0.198 |

Pairs were sampled every fifty ticks. At this horizon and under this metric,
enabling personal place affinity increased mean short-to-medium-term spatial
persistence by $0.040$. This is evidence for a limited mechanism effect, not for
stable districts or identity-based clustering.[^spaces-metrics]

The same experiment reported a personality-distance difference against a
deterministic trait shuffle, but it did not estimate confidence intervals or
establish long-horizon persistence. It is therefore insufficient for a
segregation claim.

## Background and Unestablished Hypotheses

**Background only.** Schelling's model relocates agents according to the fraction
of similar neighbors and a tolerance threshold [@schelling1971]. The present model
has neither mechanism: it has no binary similarity class, no tolerance test, and
no relocation rule triggered by unlike neighbors. Place affinity must not be
described as a formal Schelling analogue.

**Not established.** Current code and experiments do not establish:

- segregation by personality, opinion, origin, or any other identity;
- an artistic or occupational subculture;
- territorial ownership or exclusion;
- leadership caused by spatial precedence or centrality;
- free-riding or a spatial cooperation equilibrium.

Testing any of these would require a phenomenon-specific metric, longer runs,
multiple seeds, uncertainty estimates, and a causal counterfactual. For example,
free-riding would require a contribution-benefit definition and evidence that low
contribution systematically yields disproportionate benefit; the recorded
Phase 6 contribution-benefit correlation was positive, not evidence for that
claim.

[^spaces-affordances]: Layout generation is in `src/wfc_generator.h`; tile
    validity and walkability are in `src/components.h` and `src/grid.h`.
    `test_create_completes_discrete_work_units_on_ordinary_floor` and the static
    OpenSpace audit in `tests/verify_policy_audit.cmake` protect the nonexclusive
    cultural-action boundary.
[^spaces-learning]: See `PlaceMemoryComponent` in `src/components.h`,
    `Simulation::system_spatial_learning()` in `src/simulation.cpp`, and
    `Simulation::find_preferred_place()` in `src/sim_targets.cpp`.
[^spaces-metrics]: The metric implementation is
    `Simulation::system_record_emergence_metrics()` and its JSON fields are
    defined in `src/metrics.h` and `src/batch_main.cpp`. The 20-seed paired result
    is recorded in the Phase 6 result of
    `doc/plans/2026-07-21-alineacion-diseno-implementacion.md`; deterministic
    metric accumulation and toggles are checked by `test_metrics_contract` and
    `tests/verify_metrics.cmake`.
