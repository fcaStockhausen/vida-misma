# Social Simulation {#sec:social}

Social simulation is the subsystem that distinguishes a community simulation from a terrain renderer with moving agents. It encompasses the economic system (production, trade, specialization), the social graph (relationships, stress contagion), and the spatial embedding of social dynamics. This section describes how these components interact to produce emergent social phenomena.

## The Production Economy

The economic system is modeled as a directed acyclic graph (DAG) of transformations:

$$\text{Inputs} \xrightarrow{\text{workshop + labor + time}} \text{Outputs}$$

Raw resources (wood, ore, stone) are transformed into intermediate goods (planks, bars, blocks) and then into final products (furniture, weapons, food). Each transformation requires an agent with sufficient skill, an appropriate workshop, and a time cost proportional to the agent's skill level.

The economy is *emergent* rather than scripted: prices are not set by a global controller but arise from the interaction of supply (what agents produce) and demand (what agents need). When the utility system evaluates tasks, it weights them by both urgency and distance:

$$U(\text{task}) = \text{urgency}(\text{task}) \times \frac{1}{1 + \beta \cdot d(\text{agent}, \text{task})}$$ {#eq:task-distance}

where $d$ is the path distance and $\beta$ is a distance sensitivity parameter. This distance penalty produces natural industrial organization: agents prefer nearby tasks, which causes workshops to become associated with their input sources, forming de facto industrial districts.

This coupling between economic behavior and spatial layout is a design insight from Dwarf Fortress: in early versions, agents accepted any task regardless of distance, producing inefficient cross-fortress movement. Adams resolved this not by scripting "take the nearest task" but by making distance reduce utility in the decision system. The self-organization was not programmed; it emerged from a modification to the utility function.

## Skill Progression and Specialization

Skills improve through practice. A typical progression model uses increasing XP thresholds:

$$\text{XP for level } N = \text{base} + \text{increment} \times N$$ {#eq:skill-xp}

Cumulative XP to reach level $L$:

$$\sum_{n=0}^{L-1} (\text{base} + \text{increment} \times n) = \text{base} \times L + \text{increment} \times \frac{L(L-1)}{2}$$ {#eq:skill-total}

Skill level affects output quality. In Dwarf Fortress, quality tiers (base, well-crafted, finely-crafted, superior, exceptional, masterwork) have probability thresholds that depend on skill. A "Legendary" (level 15) craftsdwarf produces exceptional-quality items approximately 65% of the time, with a hard cap of 33.3% for masterwork.

This system creates a positive feedback loop: an agent who performs a task gains skill, which increases the quality of their output, which increases the utility of assigning them similar tasks (because higher-quality outputs are more valuable), which increases the probability they will be assigned those tasks again. The result is *natural specialization*: agents gravitate toward roles they have practiced, without any explicit assignment mechanism.

Skills can also decay through disuse, providing a countervailing force that prevents permanent lock-in and allows role flexibility in response to changing community needs.

## Relationships and Social Dynamics

The social graph (defined in Section 3.6) is the substrate on which social dynamics operate. The economic and skill systems feed into it through events: working alongside another agent modifies the relationship edge, successful collaboration strengthens it, competition weakens it, and traumatic events (injury, death, loss) create large negative modifications modulated by personality.

The stress system interacts with the social graph through contagion:

$$\Delta \text{stress}_j = \gamma \cdot |w(i, j)| \cdot \Delta \text{stress}_i \cdot \text{susceptibility}_j$$ {#eq:stress-contagion}

where $\gamma$ is a global contagion coefficient and $w(i, j)$ is the relationship weight. Positive relationships propagate stress (empathy response), while strongly negative relationships can also propagate stress (antagonism response). Agents with many strong relationships are both more supported and more vulnerable to cascading stress events.

This mechanism produces emergent social dynamics: a traumatic event (e.g., a creature attack) affects not only the direct victims but their entire social network, with intensity attenuated by graph distance. A community with dense, positive social connections recovers faster (shared coping) but is also more susceptible to cascading breakdown if a central agent is affected.

## Spatial Game Theory

The social dynamics described above gain additional structure when agents are embedded in physical space. Nowak and May (1992) [@nowak1992] demonstrated that placing simple game-theoretic interactions on a spatial lattice fundamentally changes the evolutionary outcomes.

In the standard Prisoner's Dilemma, defection is the dominant strategy in a well-mixed population: agents who defect always outperform cooperators, leading to universal defection. However, when agents are placed on a 2D lattice and interact only with their spatial neighbors, cooperators can form clusters that resist invasion by defectors. The boundary of a cooperator cluster generates enough mutual benefit to offset the exploitation by surrounding defectors. The spatial structure itself enables cooperation that would be impossible without it.

This result has direct implications for community simulation design:

- The physical layout of a settlement (who lives next to whom, which workshops are adjacent, where resources are concentrated) is not merely aesthetic. It constrains which social dynamics can emerge.
- Cooperation, trade, and trust are more likely to develop between spatially proximate agents.
- Spatial isolation or segregation can produce divergent cultural or behavioral clusters within the same community.

A simulation engine that treats spatial position as a first-class variable in social interactions will produce richer emergent dynamics than one where social and spatial systems are independent.

## RimWorld's Storyteller: A Meta-Agent for Pacing

RimWorld introduces an additional layer: a *Storyteller* meta-agent that calibrates the difficulty of external events (raids, weather, disease) to maintain narrative tension. The Storyteller does not simulate physical reality; it monitors the colony's state and adjusts the rate of adverse events:

$$P(\text{event at } t) = f(\text{wealth}_t, \text{population}_t, \text{time}_t)$$ {#eq:storyteller}

where the function parameters differ between Storyteller personalities. The "Phoebe" storyteller uses a low baseline with long peaceful intervals; "Cassandra" uses increasing difficulty over time; "Randy" samples from a uniform distribution, producing unpredictable sequences.

The Storyteller is a meta-agent that operates at a different level of abstraction than the in-world agents. It does not follow the same rules; it observes the simulation state and injects events to modulate the emergent narrative. This architectural pattern---a separate monitoring system that adjusts parameters based on observed state---is applicable to any community simulation where unmodulated emergence might produce extended periods of stagnation or catastrophic collapse.
