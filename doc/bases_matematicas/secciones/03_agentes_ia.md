# Agent-Based Modeling and Artificial Intelligence {#sec:agentes}

This section separates general agent-based modeling theory from the mechanisms
implemented in *La Vida Misma*. A statement marked **Background** motivates the
model but does not describe executable behavior. **Implemented** identifies a
current mechanism, **Design objective** identifies a criterion used to evaluate
it, and **Not established** identifies a hypothesis for which the present
experiments do not provide sufficient evidence.

## Agent-Based Modeling

**Background.** Agent-based modeling (ABM) studies systems in which autonomous,
heterogeneous entities interact with one another and with an environment. The ODD
protocol organizes a model description into Overview, Design concepts, and
Details, and asks the author to state explicitly what agents sense, how they
adapt, where stochasticity enters, and which aggregate observations are measured
[@grimm2020]. This is especially useful for distinguishing a programmed
micro-mechanism from an observed macro-pattern.

Classic models such as Sugarscape show how resource distributions, bounded
vision, metabolism, and local movement can generate aggregate distributions that
are not stored as agent goals [@epstein1996]. Such results are theoretical
precedents, not evidence that the same phenomena occur in this simulation.

**Implemented.** An inhabitant stores individual needs, personality, stress,
skills, inventory, spatial memory, lifecycle state, and a private random stream.
Systems update those components in a fixed tick pipeline. No action is assigned
by profession, community label, or `agent.id`; graph communities and founder
archetypes are observational or initialization data rather than commands.[^agents-components]

## Feasible Utility and Stochastic Choice

**Background.** Utility AI assigns comparable scores to heterogeneous actions.
A deterministic implementation would choose an argmax. A Boltzmann policy instead
turns scores into probabilities and approaches greedy choice as its temperature
approaches zero.

The executable retains that deterministic limit when the configured temperature
is effectively zero:

$$
a_t^* = \arg\max_{a\in C_t} U_t(a).
$$ {#eq:utility-argmax}

This is a fallback, not the canonical policy at temperature $0.4$.

**Implemented.** At a decision tick, the simulation computes a score and a
feasibility flag for each of thirteen actions. An action with no feasible target
or effect receives no selection weight, and a score of zero also receives no
weight. `IDLE` is always feasible and has the explicit positive score $0.02$.
The recorded diagnostic decomposition is

$$
U_t(a)=U_{\mathrm{self},t}(a)+U_{\mathrm{factory},t}(a)
-U_{\mathrm{cost},t}(a)-U_{\mathrm{risk},t}(a).
$$ {#eq:utility-composite}

At present, each action's fully shaped score is classified as either `self` or
`factory`, and explicit cost and risk remain zero; the equation describes the
implemented metrics contract, not four independently calibrated score models.
For the resulting candidate set

$$
C_t = \{a : \operatorname{feasible}_t(a) \land U_t(a) > 0\},
$$

selection uses

$$
\Pr(A_t=a) =
\frac{\exp((U_t(a)-U_{\max})/\tau)}
     {\sum_{b\in C_t}\exp((U_t(b)-U_{\max})/\tau)},
\qquad a\in C_t,
$$ {#eq:implemented-boltzmann}

where $U_{\max}=\max_{b\in C_t}U_t(b)$ is a numerical-stability offset and the
canonical temperature is `selection_temperature = 0.4`. If the temperature is
effectively zero, the code uses a greedy argmax. Action commitment persists for
several ticks, subject to feasibility and survival overrides, so choice is not an
independent draw on every tick.[^agents-utility]

The score is not the simple quadratic expression used in earlier drafts. The
canonical configuration selects urgency curve variant 3 for hunger and rest,
continuous stress variant 1, personality-dependent response thresholds, mood,
skill, local supply observations, social learning, and action-specific gates.
Consequently, a compact universal equation for every action would be misleading;
the executable definitions are in `src/sim_utility.cpp` and the effective tuning
is in `config/default.toml`.

For social, expression, and purpose, the implemented higher-need transform is

$$
u_{\mathrm{higher}}(n)=n^\alpha, \qquad \alpha=2
$$ {#eq:need-utility}

under the canonical configuration. Hunger and rest do **not** use this equation:
canonical urgency variant 3 uses the scaled logistic
$8/(1+e^{-12(n-0.7)})$.

## Local Information and Technical Pathfinding

**Design objective.** Decisions should be explainable from individual state,
observations, and bounded memory rather than colony-wide omniscience.

**Implemented, with a limitation.** Most utility and target queries inspect a
Manhattan radius of twelve tiles (`Simulation::OBSERVATION_RADIUS`). Food,
materials, machines, storage, people, artifacts, and candidate places outside
that radius do not enter ordinary choice. A remembered place may be revisited,
but its current remote conditions are not read. Once a visible or remembered
target has been chosen, cached A* may inspect the full grid as a technical routing
service; that service does not choose the behavioral goal.

Conveyor construction is not fully local. `Grid::find_conveyor_build_site()`
scans factory-wide machine, belt, storage, and Exit connectivity and runs a
grid-wide breadth-first route planner before callers reject a returned build site
farther than radius twelve. Parts of conveyor urgency also count factory-wide
machine connections. Thus the defensible claim is **mostly local decision-making**,
not complete epistemic locality. The counterfactual test changes distant food
stock and verifies identical utility, action, and target, but there is not yet an
equivalent proof for conveyor planning.[^agents-locality]

## The Relationship Graph {#sec:relationship-graph}

**Implemented.** Relationships form a directed graph over stable historical
agent IDs. Each ordered edge stores two distinct variables:

$$
R_{ij}=(f_{ij},t_{ij})\in[0,1]\times[-1,1],
$$ {#eq:relationship}

- `familiarity` in $[0,1]$, meaning accumulated evidence of contact;
- `trust` in $[-1,1]$, meaning the observer's evaluation of the other person.

The distinction is causal. Copresence within two tiles increases familiarity in
both directions without creating trust. Effective shared `BUILD`, `WORK`, or
`CREATE` activity adds reciprocal familiarity and trust. `SOCIALIZE` is a stronger
reciprocal positive interaction. Help is directional: the recipient gains trust
in the helper, while the helper gains familiarity but not trust by fiat. A
negative observation modifies only `observer -> actor`. Familiarity decays after
inactivity and trust drifts toward neutral.[^agents-social]

Trust and familiarity affect social target scores, food sharing, collaboration,
grief, opinion exchange, stress contagion, and an influence score. Every fifty
ticks, reciprocal familiarity and trust can be used to derive connected graph
components of at least three inhabitants. The resulting `community_id` is used
for observation and metrics only; static policy tests prohibit utility,
targeting, execution, and institutional policy from consuming it.

The stress system is likewise stateful but does not implement the three memory
layers described in older drafts. At the dedicated stress-update stage its
scalar update can be written exactly as

$$
\sigma'=
\max\!\left(0,
\min(1,\sigma+I_t)-
d_0[1+8(1-h)(1-r)]
\right),
\qquad d_0=0.005,
$$ {#eq:stress}

where $I_t$ is the resilience-modulated input from critical hunger, critical
rest, disease, and, only in legacy policy variant 0,
noncompliance. Action effects, trauma, and later graph contagion are separate
stages; @sec:stress-vida describes them.

## Schelling as Background, Not a Model Claim

**Background.** Schelling's segregation model assigns agents a same/different
neighborhood preference and a tolerance threshold; relocation under that rule can
produce segregation without a central segregating authority [@schelling1971].

**Not implemented.** *La Vida Misma* has no same/different class, no Schelling
tolerance threshold, and no rule that moves an inhabitant because neighbors are
dissimilar. Place choice instead scores remembered outcomes and currently
observable properties such as traffic, machinery noise, hazard, food access,
known people, artifacts, and travel distance. It is therefore not formally a
Schelling process.

**Not established.** The current paired experiments do not establish spatial
segregation, subcultures, leadership, or free-riding. Those terms require a
predeclared metric, a relevant disabled-mechanism or shuffled counterfactual,
multiple seeds, and uncertainty over a sufficiently long horizon. The measured
place-affinity result is narrower and is reported in @sec:spaces.

[^agents-components]: Implementation references: `src/components.h`,
    `src/simulation.cpp`, and `src/sim_lifecycle.cpp`. The identity-routing and
    group-label boundaries are audited by `tests/verify_policy_audit.cmake`.
[^agents-utility]: Implementation references: `src/sim_utility.cpp` and
    `src/sim_targets.cpp`. `test_metrics_contract` and `test_build_can_be_disabled`
    in `tests/simulation_tests.cpp` check explicit feasible `IDLE`, zero target
    failures, and zero softmax weight for utility-zero actions.
[^agents-locality]: Implementation references: `src/simulation.h`,
    `src/sim_utility.cpp`, `src/sim_targets.cpp`, and `src/grid.h`. The distant
    stock counterfactual is `test_unseen_stock_does_not_change_decision`; planner
    purity is `test_conveyor_planning_is_pure` in `tests/simulation_tests.cpp`.
[^agents-social]: Implementation reference: `src/social.h` and the social and
    community systems in `src/simulation.cpp`. Directionality and label neutrality
    are covered by `test_social_evidence_is_directional` and
    `test_graph_labels_are_behavior_neutral`.
