# The Inhabitants: Agent Architecture {#sec:inhabitants}

This section specifies the agent architecture for *La Vida Misma*, grounding each component in the formal models established in Part I. Agents are ECS entities (Section @sec:diseno) composed of independent data components that are processed by independent systems.

## Needs Component {#sec:needs}

The Needs component drives agent behavior through the utility system. Each need is a scalar value that decays over time and is satisfied by specific actions:

| Need | Decay model | Satisfaction action | Critical threshold effect |
|---|---|---|---|
| Hunger | Linear, constant rate | Eat food (at food storage or meal site) | Death after sustained maximum |
| Rest | Linear, constant rate | Sleep (at bed or designated rest tile) | Forced collapse; reduced skill effectiveness |
| Social | Linear, slow rate | Proximity + interaction with other agents | Stress accumulation; isolation penalty |
| Expression | Variable rate, personality-dependent | Perform artistic/musical/creative activity | Stress accumulation; personality-gated |
| Purpose | Irregular, event-triggered | Varies by personality (work, create, socialize, explore) | Stress accumulation; existential penalty |

The urgency function for each need follows the superlinear model from Section @sec:agentes:

$$f(\text{need}) = \left(\frac{\text{need}}{\text{max\_need}}\right)^\alpha$$ {#eq:vida-urgency}

where $\alpha > 1$ produces the behavioral pattern where agents occasionally attend to low-priority needs but become increasingly fixated on critical ones. The value of $\alpha$ is a global tuning parameter (initial suggestion: $\alpha = 1.5$).

**Expression** and **Purpose** are the needs that distinguish *La Vida Misma* from standard utility AI. They have no factory function. A musician playing music does not produce anything the factory requires. But the musician's utility function assigns high value to playing music when the expression need is high, and this can exceed the utility of operating a machine. This is the core design tension: the factory demands production, but the agents have internal drives that conflict with production.

## Personality Component {#sec:personality-vida}

Personality is a vector of facets assigned at agent creation and (nearly) immutable:

$$P = (f_1, f_2, \ldots, f_k), \quad f_i \in [0, 1]$$ {#eq:vida-personality}

The facets and their effects on the utility weights:

| Facet | Effect on utility weights |
|---|---|
| Compliance | Weight of factory-assigned tasks vs. personal needs |
| Laziness | Weight of rest/leisure vs. productive action |
| Artistry | Weight of expression need; probability of selecting creative actions |
| Gregariousness | Weight of social need; interaction radius; relationship formation rate |
| Resilience | Stress recovery rate; tolerance for adverse events before stress spike |
| Curiosity | Probability of exploring unknown tiles; weight of novelty in utility |

The compliance facet is the primary axis of behavioral differentiation. A high-compliance agent ($f_{\text{compliance}} \approx 1.0$) will select factory work even when other needs are moderately high. A low-compliance agent ($f_{\text{compliance}} \approx 0.1$) will neglect factory work in favor of personal needs. The population must contain enough compliant agents to sustain production, but the Director cannot control the personality distribution of arriving agents (Section @sec:birth-death).

The laziness facet produces the "minimum work principle": agents prefer to work as little as possible, consistent with their compliance level. A high-compliance, high-laziness agent will do the minimum necessary factory work and then rest. A low-compliance, low-laziness agent may work voluntarily on personal projects (art, exploration) while neglecting assigned factory tasks.

## Skills Component {#sec:skills-vida}

Skills improve through practice, following the progression model from Section @sec:social:

$$\text{XP for level } N = \text{base} + \text{increment} \times N$$

Skill categories:

| Category | Example skills | Factory relevance |
|---|---|---|
| Factory work | Machine operation, assembly, hauling | Direct production contribution |
| Domestic | Cooking, cleaning, maintenance | Indirect: sustains agent health and environment |
| Artistic | Music, painting, storytelling | None for production; satisfies expression need |
| Social | Mediation, leadership, teaching | Indirect: reduces collective stress, speeds skill transfer |

The feedback loop between skills and utility (Section @sec:agentes) produces natural specialization: an agent who operates machines gains skill, which increases the quality of their output, which increases the utility of assigning them to machines (because high-quality output contributes more to the quota). Artistic skills follow the same loop but produce no factory output. An agent who practices music becomes a better musician, which increases the utility of practicing music (because it satisfies the expression need more efficiently), which makes the agent less likely to perform factory work.

This creates a genuine trade-off: skilled artists are less productive factory workers, but they produce morale benefits for nearby agents (Section @sec:social-fabric) that may be more valuable than their direct production loss.

## Stress Component {#sec:stress-vida}

The three-layer stress model from Section @sec:agentes is retained:

| Layer | Duration | Function |
|---|---|---|
| Short-term | Minutes to hours | Drives immediate behavioral changes |
| Medium-term | Days to weeks | Modulates baseline emotional state |
| Long-term | Months to years | Permanent personality shifts (rare) |

$$\text{stress}_t = \sigma\left(\sum_{e \in E_t} \text{impact}(e) \cdot \text{modulate}(e, P) - \text{recovery}(P)\right)$$

Event categories and their stress impact:

| Event category | Examples | Impact magnitude |
|---|---|---|
| Factory-related | Production failure, quota shortfall, machine breakdown | Moderate; compliance-dependent |
| Social | Conflict, isolation, loss of relationship, rejection | High; gregariousness-dependent |
| Existential | Witnessing death, prolonged unfulfilled purpose, forced compliance | Very high; resilience-dependent |
| Environmental | Cold, noise, dirt, overcrowding | Low but cumulative |
| Expressive | Creating art, hearing music, attending performance | Negative (stress reduction); artistry-dependent |

The last category is specific to *La Vida Misma*: artistic activity reduces stress not only for the creator but for nearby agents with high artistry facets. This creates an indirect economic role for artists: they are not producing factory output, but they are maintaining the stress levels of the workforce. A factory with no artistic activity may have higher aggregate stress, leading to more breakdowns, lower productivity, and cascading social failure.

## Relationships Component {#sec:relationships-vida}

The social graph from Section @sec:agentes is the substrate for social dynamics:

$$G = (V, E, w), \quad w(i, j) \in [-100, 100]$$

Edge weight modification sources:

| Source | Effect on $w(i, j)$ | Notes |
|---|---|---|
| Proximity | $+0.1$ per tick of co-location | Mere exposure effect; slow but reliable |
| Collaboration | $+0.5$ per completed joint task | Working together builds rapport |
| Conflict | $-1.0$ to $-5.0$ per event | Resource competition, space disputes |
| Expression | $+0.3$ to $+2.0$ for audience of artistic performance | Artistry-dependent; musicians draw listeners |
| Stress contagion | Modulated by $w(i, j)$ | Per Eq. @eq:stress-contagion |

## The Utility Function: The Tension Engine {#sec:tension-engine}

The utility function for each agent action $a$ decomposes into two components:

$$U(a) = \underbrace{U_{\text{factory}}(a)}_{\text{survival}} + \underbrace{U_{\text{self}}(a)}_{\text{living}}$$ {#eq:vida-utility}

$$U_{\text{factory}}(a) = w_{\text{compliance}} \cdot f(\text{hunger}) + w_{\text{fear}} \cdot f(\text{factory\_health})$$ {#eq:vida-factory}

$$U_{\text{self}}(a) = w_{\text{laziness}} \cdot f(\text{rest}) + w_{\text{social}} \cdot f(\text{social}) + w_{\text{artistry}} \cdot f(\text{expression}) + w_{\text{purpose}} \cdot f(\text{purpose})$$ {#eq:vida-self}

where $f(\cdot)$ is the urgency function (Eq. @eq:vida-urgency) and $w_i$ are personality-dependent weights derived from the facets in Eq. @eq:vida-personality.

The $w_{\text{fear}}$ term is a population-level signal: it represents the agent's awareness that factory failure threatens survival. When the factory is healthy (quota met, resources flowing), $w_{\text{fear}}$ is low, and agents prioritize personal needs. When the factory is failing, $w_{\text{fear}}$ rises, and even low-compliance agents increase their factory work. This creates a collective response to crisis without any centralized coordination mechanism.

The two components are in tension:

1. **The compliance spectrum**: agents range from obedient workers ($U_{\text{factory}} \gg U_{\text{self}}$) to reluctant inhabitants ($U_{\text{self}} \gg U_{\text{factory}}$). The factory needs a minimum threshold of compliant agents to function.

2. **The free-rider problem**: all agents benefit from the factory running regardless of their individual contribution. This is the spatial Prisoner's Dilemma from Nowak and May (Section @sec:social): on a 2D grid, cooperators (high-compliance agents) form clusters that sustain production, but defectors (low-compliance agents) at cluster boundaries free-ride on the collective output.

3. **The artisan's dilemma**: an agent born with high artistry ($w_{\text{artistry}} \gg w_{\text{compliance}}$) has a strong internal drive to create art, but art does not produce factory output. The agent must choose between self-expression and contributing to collective survival.

4. **The minimum work principle**: the laziness facet ensures that agents prefer to work the minimum necessary to satisfy their $U_{\text{factory}}$ component, then redirect effort to $U_{\text{self}}$. This produces natural variation in work output across the population.
