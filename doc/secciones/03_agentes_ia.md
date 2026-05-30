# Agent-Based Modeling and Artificial Intelligence {#sec:agentes}

With a world generated and dynamics established, the simulation requires inhabitants whose behavior is driven by internal state rather than scripted sequences. This section covers the formal frameworks for autonomous agent decision-making, from utility theory to agent-based modeling (ABM), which provides the mathematical and methodological foundations for simulating social systems.

## Agent-Based Modeling: Foundations

Agent-based modeling is a computational paradigm in which a population of autonomous agents interacts within an environment according to well-defined local rules. The defining properties of ABM, as formalized in the ODD (Overview, Design, Details) protocol [@grimm2020}, are:

1. **Autonomy**: each agent maintains internal state and makes decisions independently.
2. **Heterogeneity**: agents differ in their attributes, preferences, and history.
3. **Locality**: agents interact with their local environment and neighbors, not with the global system state.
4. **Path dependence**: the system's state depends on the sequence of events, not merely on initial conditions.

ABM emerged as a distinct methodology in the 1990s, driven by the recognition that many social, ecological, and economic phenomena cannot be adequately modeled by aggregate equations because they arise from the interaction of heterogeneous agents with local information.

**Sugarscape** (Epstein and Axtell, 1996) [@epstein1996] is a canonical demonstration. Agents inhabit a 2D grid with a sugar resource distribution. Each agent has a metabolic rate, vision range, and movement rule (move to the visible cell with the most sugar, consume, and deplete). From these simple rules, complex macroscopic phenomena emerge: wealth inequality (Gini coefficient rises to $\sim 0.5$ without any inequality mechanism), cultural differentiation, combat, trade networks, and migration patterns. Sugarscape demonstrated that social phenomena need not be programmed at the macro level; they arise from micro-level agent interactions.

## Utility Theory for Agent Decision-Making

The dominant approach to agent AI in simulation games is *utility-based decision-making*. Each agent evaluates a set of candidate actions $A = \{a_1, a_2, \ldots, a_n\}$ by assigning a scalar utility to each:

$$a^* = \arg\max_{a \in A} U(a)$$ {#eq:utility-argmax}

where $U(a)$ is a composite utility function. In Dwarf Fortress, each action's utility is computed from the agent's current needs:

$$U(a) = \sum_{i=1}^{k} w_i \cdot f_i(\text{need}_i)$$ {#eq:utility-composite}

where $w_i$ are personality-modulated weights and $f_i$ are urgency curves. The urgency function for a need is typically a monotonically increasing function of deficit:

$$f(\text{need}) = \left(\frac{\text{need}}{\text{max\_need}}\right)^\alpha$$ {#eq:need-utility}

where $\alpha > 1$ produces a superlinear urgency (needs become increasingly pressing as they are neglected). This produces the behavioral pattern where agents occasionally attend to low-priority needs but become increasingly fixated on critical ones.

Utility theory has the advantage of being *composable*: adding a new need or action requires only defining its utility contribution, not modifying the decision loop. The disadvantage is that the behavior is locally greedy---the agent selects the highest-utility action at each tick without planning ahead. In practice, this produces adequate behavior for community simulation because the rapid tick rate (many decisions per simulated unit time) compensates for the lack of lookahead.

## Personality and Stress Systems

To produce behavioral heterogeneity, agents require personality models that modulate their responses to events. Dwarf Fortress uses a vector of personality facets:

$$P = (f_1, f_2, \ldots, f_k), \quad f_i \in [0, 100]$$ {#eq:personality}

where each $f_i$ represents a trait such as anxiety propensity, anger threshold, empathy, or diligence. These facets modify:

- The weights $w_i$ in the utility function (anxious agents weight safety higher).
- The rate of stress accumulation.
- The probability of emotional reactions to events.

Stress is modeled as a cumulative process with three memory layers [@adams2014]:

| Layer | Duration | Function |
|---|---|---|
| Short-term | Minutes to hours | Drives immediate behavioral responses |
| Medium-term | Days to weeks | Modulates baseline emotional state |
| Long-term | Months to years | Permanent personality shifts |

$$\text{stress}_t = \sigma\left(\sum_{e \in E_t} \text{impact}(e) \cdot \text{modulate}(e, P) - \text{recovery}(P)\right)$$ {#eq:stress}

where $E_t$ is the set of events at time $t$, $\text{impact}(e)$ is the inherent severity of event $e$, $\text{modulate}(e, P)$ adjusts impact based on personality, and $\sigma$ is a sigmoid that bounds stress to $[0, 1]$. Recovery is a personality-dependent decay term.

This formulation produces stress trajectories that are path-dependent: two agents with identical personalities who experience events in different order will arrive at different stress levels. The three-layer memory structure captures the clinically observed phenomenon that recent and severe events dominate short-term behavior while cumulative history shapes long-term disposition.

## The Needs--Utility Feedback Loop

The combination of needs, utility evaluation, and personality creates a feedback structure:

1. Needs decay over time (hunger increases, energy decreases).
2. Rising needs increase the utility of need-satisfying actions.
3. Personality weights determine *which* need an agent prioritizes.
4. Completing an action reduces the corresponding need.
5. Actions generate events that modify stress and relationships.
6. Stress modulates personality weights, changing future priorities.

This loop is not scripted: no code specifies that "a hungry dwarf should eat." Instead, the eating behavior emerges from the interaction of need decay, utility maximization, and personality. The significance of this architectural choice is that adding new behaviors (e.g., a "creativity" need satisfied by crafting) requires no modification to the decision loop---only a new need curve, a set of satisfying actions, and appropriate utility weights.

## The Social Graph

Inter-agent relationships are modeled as a weighted, directed graph $G = (V, E, w)$ where vertices $V$ are agents and each edge $e = (i, j)$ carries an affinity weight $w(i, j) \in [-100, 100]$. Events modify edge weights:

$$w_{t+1}(i, j) = w_t(i, j) + \Delta w(e, i, j)$$ {#eq:relationship}

where $\Delta w$ depends on the event type, the agents' personalities, and their existing relationship. Stress contagion propagates through this graph: when agent $i$ experiences a stress event, neighboring agents $j$ receive a stress increment proportional to $w(i, j)$ and their own susceptibility.

This structure means that the *history* of the community is encoded in the graph. Two agents who have shared many positive experiences develop a high-affinity edge, which causes them to be more affected by each other's emotional state. This produces emergent social phenomena: cliques form around agents with mutually positive edges, rivalries develop from accumulated negative interactions, and the loss of a central agent (high-degree vertex) can cascade through the community.

The distinction from scripted social systems is that no event has a predetermined narrative interpretation. The system records that agent $A$ witnessed agent $B$'s death at time $t$, and this modifies the relationship weights and stress levels accordingly. The player's interpretation of this data as a narrative ("Urist has been depressed since his friend died") is a projection onto the data, not something the system encodes.

## Schelling's Segregation Model

Schelling (1971) [@schelling1971] demonstrated that agents with a mild preference for similar neighbors---a threshold as low as 30%---spontaneously produce highly segregated neighborhoods on a 2D grid. Each agent evaluates the proportion of similar neighbors and moves if the proportion falls below the threshold. No central coordination is required, and no agent desires complete segregation. The result is robust across parameter variations and is one of the most widely cited examples of how individual micromotives produce collective outcomes that no individual intended.

For community simulation, Schelling's model suggests that even simple preference structures embedded in the social graph can produce emergent spatial segregation, cultural differentiation, and neighborhood formation without explicit programming.

## The ODD Protocol for Simulation Design

Grimm et al. (2020) [@grimm2020] introduced the ODD (Overview, Design, Details) protocol as a standardized framework for describing agent-based models. While developed for scientific modeling, its structure is applicable to simulation engine design:

1. **Overview**: purpose, entities, state variables, scale, process scheduling.
2. **Design**: conceptual model, general principles, emergence, adaptation, sensing, interaction, stochasticity, observation.
3. **Details**: initialization, input data, submodels (mathematical specification of each process).

Adopting ODD as a design discipline forces explicit specification of what the simulation is intended to model, what its state variables are, and what mathematical rules govern each subsystem. This reduces the risk of ambiguous specifications that produce emergent behavior the designer cannot explain or control.
