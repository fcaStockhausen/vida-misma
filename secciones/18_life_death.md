# Life, Death, and Generations {#sec:birth-death}

## Death

Agents are removed from the simulation when any of the following conditions is met:

| Cause | Trigger | Consequences |
|---|---|---|
| Starvation | Hunger need at maximum for $T_{\text{starve}}$ consecutive ticks | Agent removed; grief event for all connected agents |
| Catastrophe | Structural collapse, machine explosion, fire | Agent removed; grief event; possible additional deaths |
| Stress breakdown | Stress at maximum for $T_{\text{breakdown}}$ consecutive ticks | Agent removed (death or incapacitation beyond recovery) |
| Old age | Tick count exceeds $T_{\text{lifespan}}$ (if implemented) | Agent removed; grief event proportional to relationship weights |

Death is permanent. The agent entity is removed from the ECS and its components are deallocated. However, the *effects* of the agent persist in:

- **Relationship edges**: edges incident to the dead agent are removed, but the grief event modifies the stress and relationship weights of surviving agents who were connected to the deceased.
- **Skill gap**: if the dead agent was a specialist (high skill level), the factory loses that capacity. Other agents may begin training in the skill, but the feedback loop (Section @sec:skills-vida) takes time to rebuild expertise.
- **Social graph structure**: the removal of a high-degree vertex (informal leader, Section @sec:social-fabric) can fragment the social graph, isolating peripheral agents and reducing their social need satisfaction.

Grief events propagate through the social graph via stress contagion (Eq. @eq:stress-contagion). The impact is proportional to the relationship weight between the deceased and the survivor:

$$\Delta\text{stress}_j = \gamma \cdot |w(j, \text{deceased})| \cdot \text{grief\_magnitude}$$ {#eq:grief}

A cascade occurs when multiple agents experience grief simultaneously, and their stress propagates to neighbors, who propagate further. If the deceased was a central node (high-degree vertex, many strong edges), the cascade can affect a large fraction of the population.

## Birth: New Arrivals

New agents arrive at the factory entrance tiles at intervals determined by the Director's configuration or by a population maintenance rule. Each new agent is generated with:

- **Random personality**: each facet $f_i$ is drawn from $\mathcal{U}(0, 1)$ or from a distribution configured by the Director.
- **No skills**: all skills start at level 0.
- **No relationships**: the social graph has no edges incident to the new agent.
- **Full needs**: all needs start at zero (satisfied).

The Director cannot control the personality of arriving agents. This is a deliberate design choice: it prevents the Director from optimizing the population composition and forces adaptation to whatever personalities arrive.

## Integration of New Agents

New agents must be integrated into the social graph through the mechanisms described in Section @sec:relationships-vida:

1. **Proximity**: the new agent is placed at the entrance tile and begins moving based on utility. Co-location with existing agents initiates slow relationship formation.
2. **Assignment**: the utility system assigns the new agent to actions based on its personality. A high-compliance new agent will be drawn to factory work, where it will encounter other factory workers and form relationships through collaboration.
3. **Skill development**: the new agent begins gaining XP in whatever actions it performs. The skill-utility feedback loop gradually produces specialization.

The integration process takes simulation time. A factory that loses many agents in a short period (catastrophe, epidemic) faces a recovery gap: new agents lack the skills and relationships of the agents they replaced. The social graph may restructure around the survivors, and new subcultures or leadership structures may emerge that differ from the pre-catastrophe configuration.

## Generational Dynamics

If the simulation runs long enough, the original agents die and are replaced entirely by new arrivals. At this point, no agent has firsthand knowledge of the factory's founding. Social memory exists only in the relationship graph, and the original edges have been replaced by edges between newer agents.

This creates a generational shift: the social structures, informal leaders, and spatial patterns established by the original agents may persist (if new agents adopt similar behaviors in the same spaces) or may diverge (if new agents with different personalities repurpose spaces differently). The simulation does not enforce continuity; it may emerge or not, depending on the personality distribution of successive cohorts.
