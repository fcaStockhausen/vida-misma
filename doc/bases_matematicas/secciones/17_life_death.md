# Life, Death, Arrivals, and Generations {#sec:birth-death}

The executable has an active demographic system. It combines material and
psychological mortality, age-dependent natural death, an exogenous arrival
process, and endogenous reproduction. None of these mechanisms restores the
population to `initial_population`: the number alive may rise to
`max_population`, fall below the founding cohort, or remain at zero.

## Identity and Lifecycle State

Every person receives a monotonic historical ID and an ECS entity that is never
reused. `LifecycleComponent` records (`src/components.h`):

| Field | Meaning |
|---|---|
| `origin` | initial founder, exogenous arrival, or birth |
| `parent_a`, `parent_b` | biological parent IDs for births; $-1$ otherwise |
| `entry_tick`, `age_at_entry` | when the person entered and its age then |
| `lifespan` | deterministic individual natural-death age |
| `cohort` | temporal entry cohort |
| `generation` | genealogical generation |
| `last_reproduction_tick` | pair cooldown state |
| `death_tick` | terminal tick, or $-1$ while alive |
| `first_trusted_edge_tick` | first reciprocal familiarity/trust integration event |
| `peak_influence` | largest observed continuous influence score |

Age is derived rather than incremented:

$$
a_i(t)=a_i^{\mathrm{entry}}+
       \max(0,t-t_i^{\mathrm{entry}}).
$$

The temporal cohort is
$\lfloor t_i^{\mathrm{entry}}/w_c\rfloor$, with default width
$w_c=1000$ ticks. Cohort and generation are deliberately different: unrelated
arrivals in the same time window share a cohort, while a child's generation is
one plus the larger parental generation.

`SocialFabric`, Chronicle, and per-agent metric ledgers expand as historical IDs
grow. Dead entities remain in the registry for genealogy and post-hoc analysis,
but all behavior systems skip `alive == false`.

## Founding Population

`spawn_initial_agents()` creates the configured 48 founders on independently
sampled walkable Floor or OpenSpace positions (`config/default.toml`). A founder
first samples one of six archetypes uniformly, then jitters its six continuous
traits around that archetype's values and clamps them to $[0.05,0.95]$. The
archetype is a display provenance for founders, not a profession assignment.

Founding needs are sampled independently from $[0,0.25]$; opinion priors come
from the sampled archetype plus $\pm0.10$ noise; skills and XP start at zero;
stress starts at zero; place memory and relationships are empty. Each founder
receives the configured bootstrap reserve of 5 processed-food units. Founder age
is a deterministic seed/ID hash in the configured interval $[800,2000]$ ticks.

For every origin, lifespan is another seed/ID hash:

$$
L_i=\operatorname{round}\left(
L_0[1+s(2u_i-1)]\right),
$$

where default life expectancy $L_0=8000$, spread $s=0.20$, and
$u_i\in[0,1)$. The implementation also enforces
$L_i\geq a_{\mathrm{maturity}}+1$ (`src/sim_lifecycle.cpp`).

## Mortality and the Exclusive Death Pipeline {#sec:death}

Needs decay at the beginning of a tick, before decisions and actions. Stress and
death checks occur much later, after production, policy, artifact, and community
systems (@sec:tick-loop). `system_check_deaths()` evaluates the following
causes in strict precedence order:

| Cause | Active trigger under the default configuration |
|---|---|
| Starvation | hunger remains exactly at 1 for 180 consecutive death checks |
| Exhaustion | rest remains exactly at 1 for 200 consecutive death checks |
| Breakdown | stress is at least 0.92, followed by a probabilistic death draw |
| Natural | derived age reaches the individual's lifespan |

The hunger and rest counters reset to zero as soon as the associated need falls
below 1. In the canonical continuous stress model, breakdown is **not immediate**:
the per-tick probability is 0.005 at the threshold and smoothly decreases to
0.003 as stress approaches 1. The lower probability at the most extreme stress
is current executable behavior, intended to leave time for sabotage or recovery;
it should not be documented as a deterministic ceiling death
(`src/simulation.cpp`, `[stress]` and `[death]` in `config/default.toml`).

Natural mortality is checked only after the three preceding causes. Thus, if
starvation and lifespan coincide, the recorded cause is starvation. Suicide can
occur during SABOTAGE execution and enters the same pipeline through
`kill_agent()` before the later death check. `DeathCause::COLLAPSE` remains a
typed historical category, but canonical factory collapse does not directly kill
agents.

`kill_agent()` is the sole terminal operation. It sets the alive flag and typed
cause, records `death_tick`, releases all tile claims held by that ID, emits one
factual Chronicle event, increments suicide state when applicable, and queues one
generic grief application. `record_metric_deaths()` subsequently records each ID
once. Factory health and quota never call a direct population-kill operation.

## Exogenous Arrivals {#sec:new-arrivals}

Arrivals are active by default and are not death replacements. At the end of each
tick, `system_lifecycle()` evaluates a deterministic Bernoulli event with

$$
p_A = 1-\exp(-\lambda_A/1000),
$$

where the default rate is $\lambda_A=0.8$ attempts per 1000 ticks. The draw is a
hash of seed and tick; it does not inspect deaths, `initial_population`, factory
state, relationships, or the shared behavioral RNG. A death can only affect
whether capacity is available when an already scheduled attempt occurs.

If the number alive has reached `max_population = 200`, the attempt is counted as
blocked and discarded. It is not queued. Otherwise, the new person appears at the
first Entrance tile and receives:

- six independent traits in $[0.1,0.9]$ and no assigned archetype;
- four neutral opinions in $[0.45,0.55]$;
- hunger, rest, social, expression, and purpose independently in $[0,0.2]$;
- an empty inventory, no skills or XP, no relationships, no place memory, and
  zero stress;
- a deterministic entry age in the default interval $[1200,3000]$.

The arrival is recorded with origin `ARRIVAL`, no parents, a new monotonic ID,
and the temporal cohort of its entry tick. Because lifecycle runs after all
behavior and metrics for the tick, the newcomer first acts on the following tick.

## Endogenous Reproduction

Reproduction is also active by default. It is evaluated every 50 ticks at a base
rate of 1.0 per 1000 ticks. Candidate agents are sorted by historical ID. A pair
is eligible only when both agents:

- are at least 1200 ticks old;
- have completed the 1500-tick reproduction cooldown;
- are within Manhattan distance 3;
- have positive reciprocal familiarity and trust evidence.

The social factor is

$$
S_{ab}=\sqrt{f_{ab}f_{ba}\max(0,t_{ab})\max(0,t_{ba})}.
$$

This is multiplied by continuous material, wellbeing, and age factors. Material
state combines each parent's hunger/rest satisfaction with local food security:
personal food plus 10% of food and raw-food stocks in Storage within Manhattan
radius 12, normalized and clamped. Wellbeing combines both stress levels and
moods. The age factor ramps up after maturity and falls between 75% and 95% of
individual lifespan. For combined score $Q_{ab}$ and check interval $\Delta=50$,
the birth probability is

$$
p_B=1-\exp\left(-\lambda_B\Delta Q_{ab}/1000\right).
$$

The draw is deterministic from seed, reproduction epoch, and parent IDs. Material
abundance alone cannot bypass absent reciprocal social evidence. If a successful
draw occurs at live capacity, the blocked birth is counted and discarded; there
is no deferred pregnancy or replacement queue.

## Inheritance and Genealogy

A child appears at the first parent's current position with age zero, origin
`BIRTH`, both parent IDs, and generation
$1+\max(g_a,g_b)$. For each personality dimension $k$,

$$
x_k^{\mathrm{child}}=
\operatorname{clamp}_{[0.05,0.95]}
\left(\frac{x_k^a+x_k^b}{2}+\epsilon_k\right),
\qquad \epsilon_k\in[-0.08,0.08].
$$

The child starts with each ordinary need at 0.05, opinions at 0.5, empty
inventory, zero skills/XP, no archetype, no relationships, no community label,
no place memory, and no inherited creative progress. Genealogy therefore records
descent but does not copy a role, social group, learned relation, spatial culture,
or practiced skill. Subsequent cultural similarity must be produced by later
interaction, not by a hidden descendant label.

## Integration, Cohorts, and Metrics

At each lifecycle pass, an agent is marked socially integrated for metrics when
there exists another living agent such that both directed edges have familiarity
and trust at least 0.1. `first_trusted_edge_tick - entry_tick` is reported as
integration latency. Peak influence is updated continuously. These quantities are
descriptive mobility proxies, not proof of assimilation or leadership.

Schema-3 metrics (`src/batch_main.cpp`) close historical population accounting:

$$
N_{\mathrm{ever}}=N_{\mathrm{initial}}+N_{\mathrm{arrivals}}+N_{\mathrm{births}}
=N_{\mathrm{alive}}+N_{\mathrm{deaths}}.
$$

They report attempts, admissions, capacity blocks, origins among survivors,
deaths by cause, temporal cohorts with person-ticks and live censoring, and a
per-person genealogy containing age, lifespan, parents, generation, integration,
influence, and traits. The field named `integrated_descendants` counts all
integrated non-founders, including both arrivals and births.

## What Has and Has Not Been Demonstrated

The Phase 7 verification ran deterministic 10,000-tick CALM simulations. Across
the established seeds, no founder remained alive; multiple cohorts, arrivals,
births, natural deaths, and generations were observed, and population peaks could
rise above 48 before falling well below it. Separate fixtures demonstrate
persistent extinction when arrivals and reproduction are disabled, and identical
arrival schedules in worlds differing only by an unrelated death. These are
evidence for demographic turnover and for the absence of target-population
replacement (`tests/simulation_tests.cpp`).

They do **not** demonstrate preservation, loss, or divergence of a culture across
generations. The executable records the genealogy and social/spatial observations
needed for such a study, but no multiseed intergenerational norm metric or
counterfactual has yet established institutional memory, generational conflict,
or inherited subculture. Those outcomes remain hypotheses.

## Implementation and Verification References

- Lifecycle implementation: `src/sim_lifecycle.cpp`; spawning and death pipeline:
  `src/simulation.cpp`; state types: `src/components.h`.
- Active defaults: `[simulation]`, `[lifecycle]`, `[death]`, and `[stress]` in
  `config/default.toml`; declarations and parsing in `src/config.h` and
  `src/config.cpp`.
- Focused tests: `test_natural_mortality_uses_exclusive_death_pipeline()`,
  `test_arrivals_are_exogenous_and_newcomers_start_empty()`,
  `test_reproduction_inherits_traits_not_roles_or_relationships()`, and
  `test_dynamic_identity_and_ten_thousand_tick_turnover()` in
  `tests/simulation_tests.cpp`.
- Schema and accounting checks: `tests/verify_metrics.cmake`; static prohibition
  of inherited behavioral labels: `tests/verify_policy_audit.cmake`.
