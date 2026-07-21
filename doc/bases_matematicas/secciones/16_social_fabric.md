# Social Fabric {#sec:social-fabric}

The social fabric of *La Vida Misma* is the substrate on which collective dynamics operate. This section describes the implemented social mechanisms and the emergent phenomena they produce.

## Implemented Social Mechanisms {#sec:social-implemented}

### Social Need Satisfaction via Proximity

The `SOCIALIZE` action satisfies social need when an agent occupies a tile near other agents. The satisfaction scales with the number of nearby agents (congregation bonus: 2+ agents → +50%, 3+ → +100%) and is amplified by mutual trust with those neighbors. The `SOCIALIZE` action is selected through the standard utility-based decision process (Section @sec:agentes).

### Relationship Graph {#sec:social-graph}

The social system is built on a weighted relationship graph $G = (V, E)$ where $V$ is the set of agents and each edge carries a `RelationshipEntry` with two fields: `familiarity` $\in [0,1]$ (increases with every interaction) and `trust` $\in [-1,+1]$ (where $-1$ = antagonism, $0$ = neutral, $+1$ = deep trust). The graph is stored as a flat matrix indexed by agent ID (`SocialFabric::rels_`).

Edge weights evolve through `process_interaction` (`social.h`): familiarity gains at rate $0.05 \cdot (1 - \text{familiarity})$ per interaction, trust at rate $0.03 \cdot (0.5 + 0.5 \cdot \text{familiarity})$, modulated by the valence of the interaction (positive collaborations raise trust; negative events like witnessed sabotage lower it via `negative_interaction`). Edges are created exclusively through spatial proximity — two agents must occupy nearby tiles and interact — coupling the social graph topology to the factory's spatial layout.

### Stress Contagion and Grief Cascades

Stress propagation follows the edges of $G$ via `apply_contagion` (`social.h`). The transfer from agent $i$ to agent $j$ is:

$$\Delta\sigma_j = \gamma \cdot |w(i,j)| \cdot (0.3 + 0.7 \cdot \text{familiarity}_{ij}) \cdot (\sigma_i - \sigma_j) \cdot (1 - \text{resilience}_j)$$

where $\gamma = 0.02$ is the global contagion coefficient and the weight $|w|$ is $|\text{trust}|$ — strong relationships, whether positive or negative, transmit stress. This produces **grief cascades**: when a high-degree agent dies, `apply_grief` injects stress into every neighbor proportional to their familiarity and positive trust, potentially triggering secondary stress events across the graph.

### Collaboration Bonuses

Agents who share a positive relationship receive a productivity bonus when working on nearby tiles (Manhattan distance $\leq 2$) via `collaboration_bonus` (`social.h`). The bonus is $1.0 + \min(1.0, \sum \text{trust}_{ij} \cdot g_j)$ where $g_j$ is the neighbor's gregariousness, capped at $2.0\times$ solo rate. Isolated agents remain viable at $1.0\times$; socially embedded agents gain up to double throughput.

### Informal Leadership (Influence)

No agent is explicitly designated as a leader. Instead, `SocialComponent::influence` emerges as a smoothed score updated each tick via `update_influence` (`social.h`):

$$\text{influence} \mathrel{+}= 0.05 \cdot \big(\text{target} - \text{influence}\big), \quad \text{target} = \text{compliance} \cdot (1 - \sigma) \cdot (0.3 + 0.7 \cdot \overline{\text{fam}}) \cdot (0.5 + 0.5 \cdot \overline{\text{trust}})$$

Influence thus requires four conditions simultaneously: high compliance, low stress, high average familiarity with neighbors, and high average trustworthiness. It is a degree/familiarity-weighted metric, not a betweenness or eigenvector centrality — the formal graph-centrality framing sometimes invoked for leadership is an aspirational refinement, not the current implementation. Influence feeds back into opinion dynamics (`leader_opinion_pull`) and the utility of SOCIALIZE.

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

If the non-compliant fraction of the population exceeds a critical threshold, aggregate output falls below the quota. The factory responds via the `factory_pressure` multiplier (`sim_utility.cpp`): production utilities (GATHER, BUILD, WORK) are amplified by $1 + (1 - h_{\text{factory}}) \cdot k$ where $k$ is action-specific (2.0 for gather/build, 3.0 for work). Additionally, when quota fill drops below 50%, WORK utility is boosted 3× and input-readiness is floor-raised. This creates a feedback loop: declining health raises the utility of productive work, recruiting more agents into the production chain.

This is analogous to a threshold model of collective behavior (Granovetter, 1978): each agent's decision to increase work effort depends on the perceived state of the factory, and the threshold varies by personality (compliance, resilience). If enough agents cross the threshold simultaneously, the factory recovers. If not, it collapses. The spatial structure of $G$ mediates this process: agents embedded in strong positive relationships can coordinate their response more effectively (via `collaboration_bonus` and `exchange_opinions`), while isolated agents respond only to the global signal.
