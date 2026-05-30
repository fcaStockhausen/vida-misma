# Social Fabric {#sec:social-fabric}

The social fabric of *La Vida Misma* is the substrate on which collective dynamics operate. This section describes the current implementation of social interaction, the designed-but-unimplemented mechanisms that will enrich it, and the emergent phenomena the full model is intended to produce.

## Implemented Social Mechanisms {#sec:social-implemented}

### Social Need Satisfaction via Proximity

The simulation currently provides a single social primitive: the `SOCIALIZE` action. When an agent selects this action and occupies a tile adjacent to at least one other agent, its social need is partially satisfied. The magnitude of satisfaction depends on the agent's **gregariousness** personality facet: agents with higher gregariousness receive a larger reduction in social need per interaction, while low-gregariousness agents derive comparatively less benefit. Formally, the social need decrement is weighted by a factor proportional to the agent's gregariousness score.

This mechanism is intentionally minimal. It provides the basic incentive for agents to seek proximity with one another, establishing the spatial clustering precondition upon which all higher-order social phenomena depend. The `SOCIALIZE` action is selected through the standard utility-based decision process described in Section @sec:agentes, meaning that agents will pursue social interaction only when its marginal utility exceeds that of competing actions (work, rest, artistic expression).

### Limitations of the Current Model

In the current implementation, social interactions are anonymous and stateless: an agent gains the same benefit from socializing with agent $j$ as with agent $k$, and no memory of past interactions is retained. There is no relationship formation, no stress contagion, and no persistent social structure. The social need functions as a periodic drive that draws agents together spatially, but the resulting clusters have no internal differentiation.

## Designed Mechanisms (Not Yet Implemented) {#sec:social-designed}

### Relationship Graph

The central planned data structure is a weighted, undirected relationship graph $G = (V, E, w)$ where $V$ is the set of living agents, $E \subseteq V \times V$ is the set of established relationships, and $w: E \to [-100, 100]$ assigns an affinity weight to each edge. Positive weights indicate friendship or trust; negative weights indicate antagonism or distrust. The absence of an edge indicates that two agents have not yet formed a relationship.

Edge weights evolve through interaction. When two agents $i$ and $j$ socialize while adjacent on the grid, their edge weight $w(i,j)$ is incremented by a quantity modulated by personality compatibility, shared activities, and the current stress state of both agents. High-gregariousness agents form edges faster: their proximity-based weight increment is amplified by their gregariousness score, making them high-degree vertices in $G$.

### Stress Contagion and Grief Cascades

Stress propagation is designed to follow the edges of $G$. When an agent $i$ experiences a stress event, a fraction of that stress is transmitted to each neighbor $j$ in proportion to $|w(i,j)|$---strong relationships, whether positive or negative, transmit more stress. This mechanism produces **grief cascades**: when a central, high-degree agent dies, the stress signal propagates through the graph along multiple paths, potentially triggering secondary stress events in agents who were only distantly connected to the deceased.

The formal model is given by Eq. @eq:stress-contagion. The contagion parameter governs the rate of decay with graph distance: at low values, stress remains localized; at high values, cascades can sweep through the entire population.

### Collaboration Bonuses

Agents who share a strong positive relationship ($w(i,j) > 0$) receive a productivity bonus when working on adjacent tiles. This creates a direct economic incentive for relationship maintenance: agents who sustain friendships work more efficiently, reinforcing the co-location behavior initiated by the social need. The bonus is small enough that isolated agents remain viable but large enough that socially embedded agents have a systematic advantage.

### Informal Leadership Through Network Centrality

No agent is explicitly designated as a leader. Instead, high-gregariousness agents naturally become high-degree vertices in $G$ because they form relationships faster. These agents acquire disproportionate influence through two channels: (1) their stress events propagate to many neighbors, making them socially salient; and (2) their presence on a tile provides a social-need satisfaction bonus to nearby agents (via the collaboration mechanism), making their location attractive. Network centrality---measured by degree, betweenness, or eigenvector metrics---emerges as a structural property of $G$ without any top-down assignment. The ODD protocol (Section @sec:agentes) would classify this as an emergent property of the model, not a designed behavior.

### Proximity-Based Relationship Formation

Edges in $G$ are created exclusively through spatial proximity: two agents must occupy adjacent tiles and interact. This ensures that the topology of the social graph is coupled to the spatial layout of the factory. Machine placement, common-area design, and transit corridors all shape which relationships can form, giving the Director an indirect lever over social structure.

## Emergence Targets {#sec:social-emergence}

The following table summarizes the emergent social phenomena the full model is designed to produce, their mechanisms, and their formal bases:

| Phenomenon | Mechanism | Formal basis |
|---|---|---|
| Natural specialization | Skill-utility feedback loop | Eq. @eq:skill-xp; Section @sec:agentes |
| Spatial segregation | Personality-driven tile preference + Schelling dynamics | Schelling (1971); Section @sec:agentes |
| Informal leadership | High-gregariousness agents become high-degree vertices in $G$ | Network centrality |
| Artistic subcultures | High-artistry agents cluster spatially and reinforce each other's expression need | Schelling + bounded confidence (Section @sec:complementarios) |
| Free-rider crisis | Low-compliance agents exploit high-compliance agents on spatial grid | Nowak and May (1992) spatial game; Section @sec:social |
| Grief cascades | Death of central agent propagates stress through $G$ | Eq. @eq:stress-contagion |
| Collective slowdown | Critical mass of low-compliance agents reduces output below quota | Threshold models; bounded confidence |
| Emergent spaces | Agent density patterns self-organize around need satisfaction | Section @sec:spaces |
| Generational turnover | Old agents die, new agents lack shared history with existing $G$ | Section @sec:birth-death |

### Natural Specialization

Agents who repeatedly perform a task accumulate skill experience (Eq. @eq:skill-xp), increasing their productivity and making the task more attractive in future utility calculations. Over time, agents converge toward a narrow set of high-productivity actions. Because skill development is path-dependent and influenced by personality (e.g., high-artistry agents gravitate toward expressive tasks), the population self-organizes into functional roles without any explicit assignment mechanism.

### Spatial Segregation

Personality-driven tile preference produces Schelling-style dynamics. Agents seek tiles that match their need profile (e.g., quiet tiles for high-artistry agents, high-density tiles for high-gregariousness agents). When agents with similar preferences cluster, they reinforce the desirability of those tiles for similar agents, creating positive feedback. The result is spatial segregation along personality dimensions: the factory develops distinct zones characterized by the personality profiles of their inhabitants.

### Artistic Subcultures

High-artistry agents cluster in tiles that satisfy their expression need (quiet, moderate density, distant from machines). Within these clusters, artistic performance creates positive social reinforcement: agents who perform together develop stronger edges in $G$. This is a bounded confidence dynamic (Section @sec:complementarios): high-artistry agents are more influenced by other high-artistry agents, causing their behaviors to converge within the subgroup. The result is a subculture---a cluster of agents who share artistic preferences, socialize primarily with each other, and develop distinct behavioral patterns from the rest of the population.

### The Free-Rider Problem as Spatial Game

The compliance facet maps directly onto the cooperator/defector distinction in spatial game theory (Nowak and May, 1992; Section @sec:social). High-compliance agents are cooperators: they contribute to factory production even when they could free-ride. Low-compliance agents are defectors: they prioritize personal needs over factory work, benefiting from the production of others.

On the 2D grid, cooperators form clusters where production is sustained. At cluster boundaries, defectors exploit the output. The Nowak-May result predicts that cooperators can persist if they form sufficiently dense clusters---the internal benefit of cooperation offsets the boundary exploitation. In the spatial iterated Prisoner's Dilemma, the critical parameter is the ratio of the temptation-to-defect payoff to the cooperation payoff; when this ratio is moderate, spatial structure alone is sufficient to maintain cooperation.

For *La Vida Misma*, this means that a factory with a spatially clustered compliant workforce can sustain production even with a substantial non-compliant minority. But if compliant agents are dispersed (e.g., because the Director placed machines in a spread-out configuration), each compliant agent is more exposed to free-riding, and collective output may fall below the quota threshold. This is a game-theoretic justification for the Director to consider spatial layout carefully: clustering machines creates conditions for cooperative production to persist.

### Collective Slowdown and Threshold Models

If the non-compliant fraction of the population exceeds a critical threshold, aggregate output falls below the quota. This triggers the $w_{\text{fear}}$ term in the utility function (Eq. @eq:vida-utility), which increases the factory-utility weight for all agents. But this response has a delay: agents must perceive the factory failing before $w_{\text{fear}}$ rises. If the decline is gradual, $w_{\text{fear}}$ may not rise fast enough to prevent a production collapse.

This is analogous to a threshold model of collective behavior (Granovetter, 1978): each agent's decision to increase work effort depends on the perceived state of the factory, and the threshold varies by personality (compliance, resilience). If enough agents cross the threshold simultaneously, the factory recovers. If not, it collapses. The spatial structure of $G$ mediates this process: agents embedded in strong positive relationships can coordinate their response more effectively, while isolated agents respond only to the global signal.
