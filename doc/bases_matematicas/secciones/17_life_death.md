# Life, Death, and Generations {#sec:birth-death}

The population of *La Vida Misma* is not static. Agents die when their physiological or psychological limits are exceeded, and new agents arrive to replace them. This section describes the mortality system, the initial population seeding mechanism, the intended-but-unimplemented replacement spawning pipeline, and the generational dynamics that full implementation is expected to produce.

## Death {#sec:death}

Death is processed by `system_check_deaths`, which runs at the end of every tick after stress has been updated (Section @sec:stress-vida). When a death condition is triggered, the agent's `AgentComponent` is updated: the `alive` flag is set to `false` and the `cause_of_death` string is recorded. The entity is not removed from the ECS registry; dead agents are simply excluded from all subsequent system iterations by the `if (!alive) continue` guard that prefixes every system loop.

There are three mortality pathways, corresponding to the three need systems that can reach a critical ceiling.

### Starvation

An agent dies of starvation when its hunger need remains at maximum ($\text{hunger} \geq 1.0$) for $T_{\text{starve}} = 120$ consecutive ticks. The implementation tracks this via the `ticks_at_max_hunger` counter in `AgentComponent`, which increments each tick that hunger is at ceiling and resets to zero whenever hunger drops below $1.0$. This design means that intermittent feeding---even a single tick of eating that pushes hunger below the threshold---fully resets the counter. An agent on the verge of starvation must therefore find food within a contiguous 120-tick window, but can repeatedly approach the brink and recover so long as each critical episode is resolved before the counter expires.

At the default hunger decay rate of $0.005$ per tick (Table @tbl:decay-rates), an agent whose hunger reaches $1.0$ from zero has already gone approximately 200 ticks without eating. The additional 120-tick starvation window means that total time from full satisfaction to death is approximately 320 ticks. This generous margin exists to prevent premature death from transient pathfinding failures or resource shortages, giving the utility system adequate opportunity to redirect the agent toward food.

### Exhaustion

Exhaustion death follows an analogous mechanism: when $\text{rest} \geq 1.0$ for $T_{\text{exhaust}} = 160$ consecutive ticks, the agent is marked dead with `cause_of_death = "exhaustion"`. The `ticks_at_max_rest` counter tracks consecutive ticks at maximum fatigue and resets when the agent rests sufficiently to bring the need below $1.0$.

Exhaustion is slightly more tolerant than starvation ($160$ vs.\ $120$ ticks at ceiling), reflecting the design judgment that while both are lethal, total physical collapse from sleep deprivation takes longer than death from complete caloric deprivation. At the default rest decay rate of $0.006$ per tick, an agent reaches maximum fatigue in approximately 167 ticks from full rest, yielding a total time to death of roughly 327 ticks.

### Stress Breakdown

Unlike starvation and exhaustion, which require sustained need saturation, stress-induced breakdown is instantaneous: if an agent's accumulated stress exceeds the breakdown threshold, $\text{stress} \geq \theta_{\text{breakdown}} = 0.92$, the agent dies immediately. There is no grace period and no tick counter.

Stress accumulates from high unsatisfied needs (Section @sec:stress-vida) and is modulated by the agent's resilience personality facet. An agent with high resilience ($f_{\text{resilience}} \approx 0.9$) reduces stress input by approximately $63\%$, making breakdown extremely unlikely under normal conditions. An agent with low resilience ($f_{\text{resilience}} \approx 0.2$) receives only a $14\%$ reduction, making it vulnerable to cascading stress from even moderate need deficits. Breakdown thus functions as the personality-gated mortality pathway: it selectively removes agents who are both psychologically fragile and trapped in adverse conditions.

### Summary of Mortality Conditions

| Cause | Trigger | Counter | Default threshold | Immediate? |
|---|---|---|---|---|
| Starvation | $\text{hunger} \geq 1.0$ | `ticks_at_max_hunger` | 120 ticks | No |
| Exhaustion | $\text{rest} \geq 1.0$ | `ticks_at_max_rest` | 160 ticks | No |
| Breakdown | $\text{stress} \geq 0.92$ | None | $\theta_{\text{breakdown}}$ | Yes |

: Mortality pathways in the current implementation. {#tbl:mortality}

### Consequences of Death

In the current implementation, death has limited systemic consequences. The `alive = false` flag causes the dead agent to be excluded from all subsequent system processing---it no longer decays needs, computes utility, moves, executes actions, or accumulates stress. The `cause_of_death` field is retained for post-hoc analysis.

Several designed consequences are not yet implemented:

- **Grief events.** The intended design specifies that when an agent dies, all surviving agents connected to it in the social graph receive a grief-induced stress spike proportional to the relationship weight. This would create cascading stress propagation through the network. Neither grief events nor the social graph itself currently exist in the codebase.

- **Skill gap.** If the deceased agent had developed high proficiency in a particular skill, that specialisation is lost. Other agents must rebuild it through the skill-utility feedback loop (Section @sec:inhabitants). This consequence is implicit---the simulation does not actively compensate for lost skills---but it becomes significant once the skill system is functional.

- **Social graph restructuring.** The removal of a high-degree vertex (an informal leader, as described in the designed model of Section @sec:social-fabric) could fragment the social graph and isolate peripheral agents. This mechanism awaits the implementation of the relationship graph.

## Population Seeding {#sec:population-seeding}

The initial population is created by `spawn_initial_agents()` during simulation construction. Agents are placed on randomly selected walkable tiles (Floor and OpenSpace) across the entire grid. Each agent receives the following initial state:

- **Personality.** Each facet is drawn independently from a uniform distribution over its configured range: $f_i \sim \mathcal{U}(\text{range}_i[0],\; \text{range}_i[1])$. The default ranges, specified in the `Config` struct, are deliberately asymmetric---no facet spans the full $[0, 1]$ interval. For example, compliance is drawn from $[0.1, 0.95]$ and resilience from $[0.2, 0.9]$. This ensures that extreme personality profiles are possible but rare, and that no agent is born entirely incapable of social functioning or factory work.

- **Needs.** All needs start at a low random value drawn from $\mathcal{U}(0, 0.15)$, approximating full satisfaction. This gives newly seeded agents a grace period before urgency-driven behavior becomes necessary.

- **Skills.** All four skills (factory work, domestic, artistic, social) are initialised to $0.0$. No skill-modifying systems are active in the current implementation.

- **Stress.** Initial stress is $0.0$.

- **Relationships.** No relationship graph exists. Agents begin as socially anonymous entities.

- **Inventory.** Empty (all resources at $0.0$).

## New Agent Arrivals {#sec:new-arrivals}

The replacement spawning mechanism---whereby new agents arrive at Entrance tiles to replace the dead---is a design target that is not yet implemented. The intended specification is as follows.

When an agent dies and the population falls below the configured `initial_population`, a new agent is spawned at a randomly selected Entrance tile on the grid boundary. The new agent receives:

- **Random personality.** Each facet is drawn from the same uniform distributions used during initial seeding. The Director cannot control the personality of arriving agents. This is a deliberate design constraint: it prevents the Director from optimising population composition and forces adaptation to the personality distribution that chance provides.

- **No skills and no relationships.** The new agent begins as a blank slate, with all skills at $0.0$ and no edges in the social graph.

- **Full needs.** All needs start at $0.0$ (complete satisfaction).

The replacement mechanism ensures population continuity: the simulation never runs out of agents, but each replacement arrives without the accumulated human capital (skills, relationships, spatial familiarity) of the agent it replaced.

## Integration of New Agents {#sec:integration}

New agents---whether from the initial cohort or future replacement spawns---must integrate into the factory's activity patterns through the existing systems:

1. **Proximity and action.** The new agent is placed on a tile and begins selecting actions via the utility system (Section @sec:inhabitants). Co-location with existing agents during shared activities (working adjacent machines, eating at the same storage) creates the conditions for social interaction.

2. **Skill development.** The new agent gains experience in whatever actions it performs. The skill-utility feedback loop, once active, will gradually produce specialisation, but the process takes time. A factory that loses its only high-skill machine worker faces a production gap until a replacement reaches comparable proficiency.

3. **Social embedding.** Once the relationship graph is implemented, new agents will form edges with existing agents through repeated co-location and collaboration. The rate of edge formation depends on the gregariousness facet and on spatial proximity. Agents placed at Entrance tiles on the grid boundary face a longer integration period because they must first traverse the map to reach high-density areas.

The integration process introduces a natural recovery latency following catastrophic mortality events. If many agents die simultaneously---from a sustained resource collapse that triggers starvation across the population, or from a stress cascade---the replacement cohort lacks the skills and social connections of the agents they replaced. The social graph may restructure around surviving agents, and new informal leaders may emerge with different personality profiles than their predecessors.

## Generational Dynamics {#sec:generational-dynamics}

Generational dynamics are a design target, not yet emergent in the running simulation. The intended mechanism operates as follows.

If the simulation runs for a sufficiently long period, the original agents die and are replaced entirely by new arrivals. At this generational boundary, no living agent retains firsthand experience of the factory's founding conditions. Social memory exists only in the structure of the relationship graph: edges between agents who joined in the same cohort may differ systematically from edges between agents of different cohorts, because same-cohort agents have had more time to interact and develop stronger ties.

This creates the possibility of a generational shift. The spatial patterns, informal hierarchies, and behavioural norms established by the original cohort may persist---if replacement agents, through the same utility-driven spatial self-organisation process (Section @sec:spaces), converge on similar tile-use patterns---or may diverge, if the personality distribution of the new cohort differs sufficiently from the original. The simulation does not enforce continuity between generations; it may emerge or not, depending on stochastic personality draws and the environmental state inherited from the previous generation.

The formal conditions for generational turnover depend on three parameters: the average agent lifespan (determined by mortality rates and resource availability), the replacement spawn rate, and the population size. When lifespan is short relative to simulation runtime and replacement is immediate, turnover is rapid and generational boundaries are diffuse. When lifespan is long and replacements are infrequent, generations are discrete and the transition between them is a punctuated event that may disrupt established social structures.

These dynamics are not yet observable in the current implementation because the replacement spawning mechanism is not active. Once implemented, generational turnover is expected to produce the emergent phenomena catalogued in Section @sec:social-fabric: potential loss of institutional knowledge, reconfiguration of informal leadership, and possible divergence in spatial organisation patterns between successive cohorts.
