# Social Fabric {#sec:social-fabric}

The social graph (Section @sec:relationships-vida) is the substrate on which collective dynamics operate. This section describes the emergent social phenomena that the simulation should produce without explicit programming, and their formal basis in the models established in Part I.

## Emergence Targets

| Phenomenon | Mechanism | Formal basis |
|---|---|---|
| Natural specialization | Skill-utility feedback loop | Eq. @eq:skill-xp; Section @sec:agentes |
| Spatial segregation | Personality-driven tile preference + Schelling dynamics | Schelling (1971); Section @sec:agentes |
| Informal leadership | High-gregariousness agents become high-degree vertices in social graph | Network centrality |
| Artistic subcultures | High-artistry agents cluster spatially and reinforce each other's expression need | Schelling + bounded confidence (Section @sec:complementarios) |
| Free-rider crisis | Low-compliance agents exploit high-compliance agents on spatial grid | Nowak and May spatial game theory; Section @sec:social |
| Grief cascades | Death of central agent propagates stress through social graph | Eq. @eq:stress-contagion |
| Collective slowdown | Critical mass of low-compliance agents reduces output below quota | Threshold models; bounded confidence |
| Emergent spaces | Agent density patterns self-organize around need satisfaction | Section @sec:spaces |
| Generational turnover | Old agents die, new agents lack shared history with existing social graph | Section @sec:birth-death |

## Informal Leadership

Agents with high gregariousness form relationships faster (the proximity-based edge weight increment is amplified by gregariousness), which makes them high-degree vertices in the social graph. These agents have disproportionate influence on stress contagion: when they experience a stress event, the signal propagates to many neighbors. Conversely, they serve as social hubs: agents near a high-gregariousness agent satisfy their social need faster because the high-degree agent facilitates interaction.

No code specifies "this agent is a leader." The phenomenon emerges from the interaction of gregariousness (faster relationship formation), spatial clustering (high-gregariousness agents seek high-density tiles), and stress contagion (more edges $=$ more propagation paths). The ODD protocol (Section @sec:agentes) would classify this as an emergent property of the model, not a designed behavior.

## Artistic Subcultures

High-artistry agents have a strong preference for tiles that satisfy their expression need (quiet, moderate density, distance from machines). They cluster in these tiles, forming spatial groups. Within these groups, artistic performance (which satisfies the expression need for both performer and nearby high-artistry audience members) creates positive social reinforcement: agents who perform together develop stronger relationship edges.

This is a bounded confidence dynamic (Section @sec:complementarios): high-artistry agents are more influenced by other high-artistry agents (because artistic performance has a stronger stress-reduction effect on them), which causes their opinions and behaviors to converge within the subgroup. The result is a subculture: a cluster of agents who share artistic preferences, socialize primarily with each other, and develop distinct behavioral patterns from the rest of the factory population.

## The Free-Rider Problem as Spatial Game

The compliance facet maps directly onto the cooperator/defector distinction in spatial game theory (Nowak and May, Section @sec:social). High-compliance agents are cooperators: they contribute to factory production even when they could free-ride. Low-compliance agents are defectors: they prioritize personal needs over factory work, benefiting from the production of others.

On the 2D grid, cooperators form clusters where production is sustained. At cluster boundaries, defectors exploit the output. The Nowak-May result predicts that cooperators can persist if they form sufficiently dense clusters---the internal benefit of cooperation offsets the boundary exploitation.

For *La Vida Misma*, this means that a factory with a spatially clustered compliant workforce can sustain production even with a substantial non-compliant minority. But if the compliant agents are dispersed (e.g., because the Director placed machines in a spread-out configuration), each compliant agent is more exposed to free-riding, and collective output may fall below the quota threshold.

This is a game-theoretic justification for the Director to consider spatial layout carefully: clustering machines creates conditions for cooperative production to persist.

## Collective Slowdown and Threshold Models

If the non-compliant fraction of the population exceeds a critical threshold, aggregate output falls below the quota. This triggers the $w_{\text{fear}}$ term in the utility function (Eq. @eq:vida-factory), which increases the factory-utility weight for all agents. But this response has a delay: agents must perceive the factory failing before $w_{\text{fear}}$ rises. If the decline is gradual, $w_{\text{fear}}$ may not rise fast enough to prevent a production collapse.

This is analogous to a threshold model of collective behavior: each agent's decision to increase work effort depends on the perceived state of the factory, and the threshold varies by personality (compliance, resilience). If enough agents cross the threshold simultaneously, the factory recovers. If not, it collapses.
