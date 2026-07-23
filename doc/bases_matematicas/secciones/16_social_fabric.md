# Social Fabric: Evidence, Directed Relations, and Claims {#sec:social-fabric}

The social layer is implemented, but not every collective pattern that it can
support has been demonstrated. This section therefore separates three levels:
the state and update rules executed by the program, observational summaries
derived from that state, and emergence hypotheses that still require a
multiseed counterfactual.

## Implemented State: a Directed Relationship Graph

`SocialFabric` stores a dynamically expandable flat matrix of
`RelationshipEntry` values (`src/social.h`). For two historical agent IDs $i$
and $j$, the entry

$$
R_{ij} = (f_{ij}, t_{ij}, \ell_{ij})
$$

is **agent $i$'s relation toward agent $j$**. Familiarity
$f_{ij}\in[0,1]$, trust $t_{ij}\in[-1,1]$, and the last-evidence tick
$\ell_{ij}$ are directional: in general, $R_{ij}\ne R_{ji}$. The matrix grows
when monotonic historical IDs exceed its current capacity, so dead agents and
new cohorts do not require ID reuse (`SocialFabric::ensure_agent_id`).

All evidence updates use the common rule

$$
f' = \operatorname{clamp}_{[0,1]}\left(f + g_f(1-f)\right),\qquad
t' = \operatorname{clamp}_{[-1,1]}(t + g_t).
$$

Trust is therefore an additive evidence balance; it is not multiplied by
familiarity during reinforcement. With social learning enabled, the active
evidence sources are:

| Evidence | Direction updated | $g_f$ | $g_t$ | Runtime source |
|---|---|---:|---:|---|
| Copresence within Manhattan distance 2 | both $i\to j$ and $j\to i$ | 0.001 | 0 | `system_social_learning()` |
| Effective shared BUILD, WORK, or CREATE within distance 2 | both directions | 0.003 | 0.002 | `system_social_learning()` |
| Explicit SOCIALIZE interaction with the selected neighbor | both directions | 0.05 | 0.03 | `system_execute_actions()` |
| Help of amount $a$ from $i$ to $j$ | recipient $j\to i$ | $0.01a$ | $0.03a$ | `record_help()` |
| The helper's observation of the recipient | helper $i\to j$ | $0.005a$ | 0 | `record_help()` |
| Negative observation of actor $j$ by observer $i$ | observer $i\to j$ only | 0.01 | $-s$ | `record_negative_observation()` |

The collaboration row requires both agents to report an effective action in the
current tick; sharing an action label without an effect does not create trust.
The directional help rule is equally important: receiving food is evidence for
trusting the helper, but helping does not grant the helper reciprocal trust by
fiat. Negative evidence similarly changes only the observer's edge toward the
observed actor. Resident-to-resident execution rules generate such local evidence
for witnessed sabotage and, when an observer's work-ethic opinion exceeds 0.65,
eating near a machine; the canonical institution receives no report. An
unrepaired dismantle site also creates directed negative evidence
(@sec:adaptive-logistics). Watcher reports to the institution are retained only in
the noncanonical `external.policy_variant = 0` branch (`src/sim_execute.cpp`).

After 100 ticks without evidence, familiarity loses 0.0001 per tick. Trust moves
0.1% of its current distance toward zero every tick. These two decay rules run
after contagion, influence, and mood updates in `Simulation::advance()`.

## Social Action, Help, and Collaboration

SOCIALIZE is selected by the ordinary utility process. At execution it searches
agents within Manhattan distance 6 and chooses by the acting agent's directed
familiarity and positive trust, with distance and ID tie-breaking. Its social-need
satisfaction depends on gregariousness, social skill, and crowd size: two nearby
agents multiply satisfaction by 1.5 and three or more by 2.0. Trust does **not**
directly multiply this satisfaction. Instead, positive $t_{ij}$ reduces the
actor's stress, affects opinion learning, and contributes to willingness to share
surplus food (`src/sim_execute.cpp`). Crowds of at least three also provide a
small passive social-need reduction even when the agent is doing another action.

When an agent works near a trusted acquaintance, `collaboration_bonus()` reads
the actor's edge $R_{ij}$. For agents within distance 2 satisfying
$f_{ij}\geq0.1$ and $t_{ij}\geq0.1$, the increment is

$$
b_{ij}=0.2\max(0,t_{ij})(0.5+0.5f_{ij}),
$$

and the total multiplier is $1+\min(1,\sum_j b_{ij})$. The result is in
$[1,2]$ and is applied to machine WORK. BUILD has an additional co-builder
multiplier and directed-trust adjustment in `src/sim_execute.cpp`; these are
mechanistic productivity effects, not proof that stable teams or occupations
have emerged.

## Contagion, Grief, and Influence

### Stress contagion

For each index pair $i<j$ in the current alive-vector iteration,
`apply_contagion()` reads only the directed edge from the first indexed agent to
the second. If that edge has $f_{ij}\geq0.05$, it computes

$$
q_{ij}=0.02\,|t_{ij}|(0.3+0.7f_{ij})(\sigma_i-\sigma_j)(1-r_j),
$$

then applies $\Delta\sigma_i=-q_{ij}$ and $\Delta\sigma_j=+q_{ij}$, clamped to
$[0,1]$. Antagonistic and positive ties both transmit stress because the rule
uses $|t_{ij}|$. This is the exact implementation, including its asymmetry: it
does not average $R_{ij}$ with $R_{ji}$, and the susceptibility factor is always
that of the second member in the iterated pair. It should not be described as a
general symmetric diffusion operator.

### Grief

Every terminal cause uses `Simulation::kill_agent()`, which queues the deceased
ID once. During the death system, each survivor reads its own edge toward the
deceased, $R_{sd}$. For $f_{sd}\geq0.1$ the stress increment is

$$
\Delta\sigma_s=0.05f_{sd}\max(0,t_{sd})(1-r_s).
$$

The survivor's familiarity with the deceased is then multiplied by 0.8. This
implements a grief impulse and can provide input to later contagion. It does not,
by itself, establish that a multistep grief cascade or secondary mortality occurs
at population scale (`src/social.h`, `src/simulation.cpp`).

### Influence is a score, not a discovered leader

`SocialComponent::influence` is a smoothed continuous score. For agent $i$, the
averages are calculated from **incoming** edges $R_{ji}$ for living $j$ with
$f_{ji}>0.05$: influence represents being known and trusted by others. The target
is

$$
I_i^*=c_i(1-\sigma_i)(0.3+0.7\bar f_{\to i})
       (0.5+0.5\max(0,\bar t_{\to i})),
$$

and $I_i\leftarrow I_i+0.05(I_i^*-I_i)$. Influence affects SOCIALIZE utility,
food-sharing willingness, and DeGroot weights during bounded-confidence opinion
exchange. It is not betweenness, eigenvector centrality, an assigned office, or a
behavioral group label. Calling a high-$I$ agent a leader remains an
interpretation until temporal precedence and effects are tested.

## Observational Communities and Metrics

Every 50 ticks, `system_community_detection()` derives connected components from
reciprocal evidence: both directed edges must have trust above 0.3 and familiarity
above 0.2. Components smaller than three remain unlabeled. The resulting
`AgentComponent::community_id` is rebuilt for observation and Chronicle events;
static tests prohibit its use in utility, targeting, execution, and institutional
policy (`tests/verify_policy_audit.cmake`).

The schema-3 `vida_batch metrics` record reports directed relationship-edge
counts, average directed trust/familiarity, graph modularity, community-pair
stability, spatial-pair persistence, personality-distance comparison, action
entropy, specialization summaries, and a contribution/food-benefit ledger
(`src/metrics.h`, `src/batch_main.cpp`). These are measurements, not automatic
classifiers of culture.

## Demonstrated Results and Open Hypotheses

The Phase 6 paired CALM experiment used 20 seeds and independent mechanism
toggles. At 1000 ticks, disabling social learning reduced measured modularity and
community stability to zero; disabling spatial affinity reduced spatial-pair
persistence relative to the full model. Those results support the narrower claims
that relationship evidence creates the measured graph components and learned
place affinity contributes to short-horizon spatial persistence
(`doc/plans/2026-07-21-alineacion-diseno-implementacion.md`).

The same experiment did **not** establish the following stronger claims:

- **Spatial segregation:** the personality-distance delta lacked uncertainty
  estimates and long-horizon persistence.
- **Artistic subcultures:** the model has artistry but no shared aesthetic
  preference variable, so this hypothesis is not operationalized.
- **Informal leadership:** influence exists and has downstream coefficients, but
  causal leader formation was not tested.
- **Free-riding:** the observed contribution-benefit correlation was positive;
  exploitation by a noncontributing subgroup was not demonstrated.
- **Collective slowdown or strike:** no threshold transition or coordinated work
  withdrawal has been isolated from needs, logistics, and factory pressure.
- **Grief cascades:** grief and contagion are active mechanisms, but a cascade of
  secondary outcomes has not been shown by a multiseed counterfactual.
- **Generational culture:** turnover is implemented, but persistence or divergence
  of norms across cohorts has not yet been identified (@sec:birth-death).

Accordingly, specialization, leadership, subculture, segregation, free-riding,
slowdown, and generational continuity remain hypotheses unless a metric,
counterfactual, horizon, and multiseed result are stated with them.

## Implementation and Verification References

- State and update rules: `src/social.h`, `src/simulation.cpp`,
  `src/sim_execute.cpp`.
- Utility and place effects: `src/sim_utility.cpp`, `src/sim_targets.cpp`.
- Configuration: `[culture]` and canonical `[external].policy_variant = 1` in
  `config/default.toml`; fields in `src/config.h` and parsing in `src/config.cpp`.
- Directionality and label neutrality tests:
  `test_social_evidence_is_directional()` and
  `test_graph_labels_are_behavior_neutral()` in
  `tests/simulation_tests.cpp`, plus `tests/verify_policy_audit.cmake`.
- Metrics contract and counterfactual toggles: `tests/verify_metrics.cmake`.
