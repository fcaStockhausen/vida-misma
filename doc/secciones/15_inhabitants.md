# The Inhabitants: Agent Architecture {#sec:inhabitants}

This section specifies the agent architecture for *La Vida Misma*, grounding each component in the formal models established in Part I. Agents are ECS entities (Section @sec:diseno) composed of independent data components that are processed by discrete systems each tick. The architecture follows the principle that components store only data and systems contain only logic, enabling clean separation of concerns and facilitating the emergence of complex behavior from simple rules.

## Needs Component {#sec:needs}

The `NeedsComponent` is the primary motivational substrate of each agent. It contains five scalar drives, each normalized to the unit interval $[0, 1]$ where $0$ denotes complete satisfaction and $1$ denotes a critical deficit:

$$\mathbf{N} = (\text{hunger},\; \text{rest},\; \text{social},\; \text{expression},\; \text{purpose}), \quad n_i \in [0, 1]$$ {#eq:needs-vector}

All needs accumulate (decay toward $1$) at constant linear rates per tick, as specified in the simulation configuration. Table @tbl:decay-rates presents the per-tick decay rates, the approximate time to reach criticality from zero, and the satisfaction parameters for the actions that reduce each need.

| Need | Decay rate (per tick) | Ticks to criticality | Satisfaction action | Satisfaction rate (per tick) |
|---|---|---|---|---|
| Hunger | $0.005$ | $\approx 200$ | Eat processed food | $0.018$ |
| Hunger | --- | --- | Eat raw food | $0.018 \times 0.7 = 0.0126$ |
| Rest | $0.006$ | $\approx 167$ | Rest | $0.015$ |
| Social | $0.003$ | $\approx 333$ | Socialize (with neighbor) | $0.012$ |
| Expression | $0.003$ | $\approx 333$ | Create (at OpenSpace) | $0.012$ |
| Purpose | $0.002$ | $\approx 500$ | Explore | $0.008$ |
| Purpose | --- | --- | Work | $0.004$ |

: Per-tick need decay rates and satisfaction parameters {#tbl:decay-rates}

Several observations follow from these parameters. First, hunger and rest are the most urgent survival needs: an agent with no food intake will reach critical hunger ($1.0$) in approximately 200 ticks, while rest reaches criticality in approximately 167 ticks. Second, the raw food efficiency penalty ($0.7$) means that agents subsisting on ungathered food require more frequent eating, creating an economic pressure to build and operate machines that produce processed food. Third, the satisfaction rates are calibrated so that an agent must spend a non-trivial fraction of its time satisfying each need, preventing any single need from being permanently resolved.

The urgency function for each need follows the superlinear power-law model from Section @sec:agentes:

$$f(n) = n^{\alpha}, \quad \alpha > 1$$ {#eq:vida-urgency}

where $\alpha$ is a global tuning parameter with default value $\alpha = 2.0$. This quadratic urgency produces the behavioral pattern where agents occasionally attend to low-priority needs but become disproportionately fixated on critical ones. At $\alpha = 2.0$, a need at $0.5$ produces urgency $0.25$, while a need at $0.9$ produces urgency $0.81$---a $3.24\times$ increase from a $1.8\times$ increase in need magnitude. This nonlinearity is the engine of behavioral triage: agents cannot ignore critical needs without catastrophic consequences.

**Expression** and **Purpose** are the needs that distinguish *La Vida Misma* from standard survival simulations. They have no factory function. A musician playing music does not produce anything the factory requires. But the musician's utility function assigns high value to playing music when the expression need is high, and this can exceed the utility of operating a machine. This is the core design tension: the factory demands production, but the agents have internal drives that conflict with production.

## Personality Component {#sec:personality-vida}

Personality is a vector of six facets assigned at agent creation and immutable thereafter:

$$P = (f_{\text{compliance}},\; f_{\text{laziness}},\; f_{\text{artistry}},\; f_{\text{gregariousness}},\; f_{\text{resilience}},\; f_{\text{curiosity}}), \quad f_i \in [0, 1]$$ {#eq:vida-personality}

Each facet is drawn from a uniform distribution over a configured range at spawn time (Table @tbl:personality-ranges). The ranges are asymmetrically bounded to prevent extreme outliers while preserving meaningful inter-agent variation.

| Facet | Range | Default |
|---|---|---|
| Compliance | $[0.10,\, 0.95]$ | $0.5$ |
| Laziness | $[0.10,\, 0.90]$ | $0.5$ |
| Artistry | $[0.05,\, 0.85]$ | $0.5$ |
| Gregariousness | $[0.10,\, 0.90]$ | $0.5$ |
| Resilience | $[0.20,\, 0.90]$ | $0.5$ |
| Curiosity | $[0.10,\, 0.80]$ | $0.5$ |

: Personality facet spawn ranges {#tbl:personality-ranges}

The facets modulate behavior through the utility function (Section @sec:tension-engine) and the stress system (Section @sec:stress-vida). Their effects are as follows.

**Compliance** ($f_{\text{compliance}}$) is the primary axis of behavioral differentiation. It weights the utility of factory-oriented actions (GATHER materials, BUILD machines, WORK machines) relative to self-oriented actions. A high-compliance agent ($f_{\text{compliance}} \approx 0.95$) will select factory work even when other needs are moderately elevated. A low-compliance agent ($f_{\text{compliance}} \approx 0.10$) will neglect factory work in favor of personal needs. The population must contain a sufficient proportion of compliant agents to sustain collective production, but the Director cannot control the personality distribution of arriving agents (Section @sec:birth-death). This creates an emergent collective action problem.

**Laziness** ($f_{\text{laziness}}$) weights the utility of REST relative to productive action. In the utility computation, the rest weight is $w_{\text{rest}} = 0.4 + 0.6 \cdot f_{\text{laziness}}$, with a further $1.5\times$ multiplier when $\text{rest} > 0.7$. The laziness facet produces the **minimum work principle**: agents prefer to work as little as possible, consistent with their compliance level. A high-compliance, high-laziness agent will perform the minimum necessary factory work and then rest. A low-compliance, low-laziness agent may work voluntarily on personal projects (art, exploration) while neglecting assigned factory tasks.

**Artistry** ($f_{\text{artistry}}$) weights the utility of the CREATE action, which is computed as $U_{\text{create}} = f_{\text{artistry}} \cdot f(\text{expression})$. It also gates the expression need's contribution to stress: only agents with high artistry suffer significant stress from unmet expression. An agent born with high artistry has a strong internal drive to create art, but art produces no factory output. This is the **artisan's dilemma** (discussed in Section @sec:tension-engine).

**Gregariousness** ($f_{\text{gregariousness}}$) weights the utility of SOCIALIZE: $U_{\text{socialize}} = f_{\text{gregariousness}} \cdot f(\text{social})$. Highly gregarious agents seek proximity to others and suffer more from prolonged social deprivation. Low-gregariousness agents are more self-sufficient but contribute less to the social fabric that buffers collective stress.

**Resilience** ($f_{\text{resilience}}$) modulates stress accumulation. The stress input from elevated needs is multiplied by $(1 - 0.7 \cdot f_{\text{resilience}})$, meaning a maximally resilient agent ($f_{\text{resilience}} = 0.9$) accumulates stress at only $37\%$ of the base rate. Resilient agents can endure prolonged deprivation without psychological breakdown; fragile agents are vulnerable to cascading stress spirals.

**Curiosity** ($f_{\text{curiosity}}$) weights the utility of EXPLORE: $U_{\text{explore}} = f_{\text{curiosity}} \cdot f(\text{purpose}) \cdot 0.3$. Curious agents are more likely to discover distant resource deposits and unexplored terrain, potentially at the cost of time spent on more immediately productive activities.

## Inventory Component {#sec:inventory}

Each agent carries a personal inventory with three resource slots and a fixed capacity:

$$\text{Inventory} = (\text{raw\_food},\; \text{raw\_material},\; \text{food}), \quad \text{CAPACITY} = 10.0$$ {#eq:inventory}

The capacity constraint is enforced by the function $\texttt{can\_carry}(a) = (\text{raw\_food} + \text{raw\_material} + \text{food}) + a \leq 10.0$. This introduces a logistical decision: an agent carrying both raw food and raw material has less room for processed food, creating tension between gathering, building, and self-feeding.

The resource types form a two-stage production chain:

1. **raw_food** is gathered from `FoodSource` tiles at rate $0.05$ per tick. It can be eaten directly at $70\%$ efficiency, or fed into machines.
2. **raw_material** is gathered from `ScrapPile` tiles at rate $0.05$ per tick. It is consumed exclusively by the BUILD action to construct machines.
3. **food** (processed) is produced by operational machines at rate $0.025$ per tick (consuming $0.02$ raw_food per tick). It provides full eating efficiency.

The inventory system interacts with communal storage tiles. Agents can deposit surplus into adjacent storage and withdraw from it when eating or operating machines. This creates the possibility of cooperative resource pooling without any centralized allocation mechanism.

## Skills Component {#sec:skills-vida}

The `SkillsComponent` tracks four skill categories:

$$\mathbf{S} = (\text{factory\_work},\; \text{domestic},\; \text{artistic},\; \text{social\_skill}), \quad s_i \in [0, 1]$$ {#eq:skills-vector}

In the current implementation, all skills are initialized to $0.0$ and remain unused---no system reads or modifies them. They are retained in the architecture as a design scaffold for future extensions. The intended progression model, following Section @sec:social, is:

$$\text{XP for level } N = \text{base} + \text{increment} \times N$$

The feedback loop between skills and utility is designed to produce natural specialization: an agent who operates machines gains factory skill, which increases the quality of their output, which increases the utility of assigning them to machines. Artistic skills follow the same loop but produce no factory output. An agent who practices music becomes a better musician, which increases the utility of practicing music (because it satisfies the expression need more efficiently), which makes the agent less likely to perform factory work.

This creates a genuine trade-off: skilled artists are less productive factory workers, but they could in future versions produce morale benefits for nearby agents (Section @sec:social-fabric) that may be more valuable than their direct production loss.

## Stress Component {#sec:stress-vida}

The `StressComponent` contains a single scalar value $\sigma \in [0, 1]$ that represents the agent's accumulated psychological strain. Stress is updated each tick according to the following recurrence:

$$\sigma_t = \text{clamp}\!\Big(\sigma_{t-1} + \sum_{i} \delta_i - d,\;\; 0,\;\; 1\Big)$$ {#eq:stress-recurrence}

where $d = 0.005$ is the constant per-tick stress decay, and each $\delta_i$ is the stress contribution from need $i$ when it exceeds the activation threshold of $0.7$:

$$\delta_i = \begin{cases} \lambda \cdot (n_i - 0.7) \cdot m_i & \text{if } n_i > 0.7 \\ 0 & \text{otherwise} \end{cases}$$ {#eq:stress-delta}

Here $\lambda = 0.008$ is the base stress coefficient and $m_i$ is a need-specific multiplier that reflects the differential psychological impact of each deprivation:

| Need $i$ | Multiplier $m_i$ | Rationale |
|---|---|---|
| Hunger | $1.0$ | Physical deprivation; strongest stressor |
| Rest | $1.0$ | Physical deprivation; equally urgent |
| Social | $0.5$ | Psychological; less immediately urgent |
| Expression | $f_{\text{artistry}}$ | Personality-gated; only artists suffer significantly |
| Purpose | $0.5$ | Existential; slow but cumulative |

: Need-specific stress multipliers {#tbl:stress-multipliers}

The aggregate stress input is further modulated by resilience:

$$\text{stress\_input} = \left(\sum_i \delta_i\right) \cdot (1 - 0.7 \cdot f_{\text{resilience}})$$ {#eq:stress-resilience}

This means that a maximally resilient agent ($f_{\text{resilience}} = 0.9$) accumulates stress at $37\%$ of the base rate, while a minimally resilient agent ($f_{\text{resilience}} = 0.2$) accumulates at $86\%$.

The full per-tick stress update, combining accumulation and decay, is:

$$\sigma_t = \text{clamp}\!\Big(\sigma_{t-1} + \text{stress\_input} - 0.005,\;\; 0,\;\; 1\Big)$$ {#eq:stress-full}

Three death conditions are checked each tick via the `AgentComponent`:

1. **Starvation**: If $\text{hunger} \geq 1.0$ for $\geq 120$ consecutive ticks, the agent dies with cause "starvation".
2. **Exhaustion**: If $\text{rest} \geq 1.0$ for $\geq 160$ consecutive ticks, the agent dies with cause "exhaustion".
3. **Breakdown**: If $\sigma \geq 0.92$ (the breakdown threshold), the agent dies immediately with cause "breakdown".

The starvation and exhaustion counters are reset to zero whenever the corresponding need drops below $1.0$, allowing agents to recover from near-death experiences. Breakdown, by contrast, is instantaneous: once stress crosses the threshold, there is no grace period.

The stress system produces several emergent dynamics. An agent trapped in a resource-poor region will see hunger rise, which drives stress up, which---unlike hunger or rest---has no direct satisfaction action. Stress can only decay passively at $0.005$ per tick, meaning that even after needs are satisfied, the agent remains in a precarious psychological state. Multiple simultaneous deprivations (e.g., hunger and social isolation) compound additively, creating a multiplicative effective urgency through the urgency function that feeds back into action selection.

## Action Component {#sec:action-component}

The `ActionComponent` mediates between the utility computation and the spatial simulation. It stores:

- `current`: The selected action type, drawn from the enumeration $\{\text{GATHER}, \text{BUILD}, \text{WORK}, \text{EAT}, \text{REST}, \text{SOCIALIZE}, \text{CREATE}, \text{EXPLORE}, \text{IDLE}\}$.
- `target_x`, `target_y`: Grid coordinates of the action's spatial target.
- `at_target`: Boolean flag indicating whether the agent is at the target and can execute the action.
- `last_utility_*`: Per-action utility scores from the most recent computation, retained for debugging and visualization.

The action pipeline executes in four phases each tick:

1. **Utility computation** (`system_compute_utility`): Evaluates all candidate actions and selects the one with highest utility (Section @sec:tension-engine). A $2\%$ noise probability injects a uniformly random action, preventing behavioral lock-in.
2. **Target finding** (`system_find_targets`): Maps the selected action to a spatial target on the grid using a nearest-tile heuristic. Each action type has a distinct targeting strategy (Table @tbl:target-strategies).
3. **Movement** (`system_move_to_targets`): If not at target, the agent takes one greedy step along the Manhattan-distance gradient. A $5\%$ movement noise probability produces a random step instead, introducing stochasticity into pathfinding.
4. **Execution** (`system_execute_actions`): If the agent is at the target tile, the action's effects are applied to needs, inventory, and grid state.

| Action | Target selection | Execution condition |
|---|---|---|
| GATHER | Nearest `FoodSource` or `ScrapPile` | At resource tile |
| BUILD | Nearest unbuilt `Machine` | At machine tile, carrying raw\_material |
| WORK | Nearest built `Machine` | At machine tile, with raw\_food available |
| EAT | Current position (no movement) | Has food in inventory or adjacent storage |
| REST | Current position (no movement) | Always |
| SOCIALIZE | Nearest alive agent | Another agent within Manhattan distance $\leq 2$ |
| CREATE | Nearest `OpenSpace` tile | At open space tile |
| EXPLORE | Random grid coordinate | Always (also triggers random movement) |

: Action targeting and execution strategies {#tbl:target-strategies}

The targeting system uses Manhattan distance as its proximity metric, consistent with the four-directional movement model. The greedy pathfinding avoids obstacles by evaluating all four cardinal directions and selecting the one that maximally reduces Manhattan distance to the target. If no progress is possible (all reducing directions blocked), a random move is attempted.

## The Utility Function: The Tension Engine {#sec:tension-engine}

The utility function for each candidate action $a$ decomposes into two components:

$$U(a) = \underbrace{U_{\text{factory}}(a)}_{\text{survival}} + \underbrace{U_{\text{self}}(a)}_{\text{living}}$$ {#eq:vida-utility}

The factory component captures the utility of actions that sustain the collective production apparatus. The self component captures the utility of actions that satisfy the agent's internal needs. The tension between these two components is the central dynamic of *La Vida Misma*.

### The Urgency Function

All need references in the utility function pass through the power-law urgency transform (Eq. @eq:vida-urgency) with $\alpha = 2.0$:

$$f(n) = n^{2.0}$$

This quadratic mapping compresses low needs and amplifies high ones. At need level $0.3$, urgency is $0.09$; at $0.7$, urgency is $0.49$; at $0.95$, urgency is $0.9025$. The result is a behavioral priority system that is smooth (no hard thresholds) but highly responsive to critical deficits.

### Factory Utility

The factory-oriented actions (GATHER, BUILD, WORK) derive their utility from personality-weighted need urgency:

**GATHER** splits into two sub-cases. The food-gathering score is:

$$U_{\text{gather\_food}} = f(\text{hunger}) \cdot (1 - \text{food\_security}) \cdot 1.5$$

where $\text{food\_security} = \min\!\big(1,\; (\text{food} + 0.5 \cdot \text{raw\_food}) / 2.0\big)$ represents the agent's current food buffer. This score is highest when the agent is hungry and has no food reserves. The material-gathering score is:

$$U_{\text{gather\_material}} = f_{\text{compliance}} \cdot f(\text{purpose}) \cdot 0.5 \cdot (1 + [r_{\text{mat}} < 1.0])$$

where $[r_{\text{mat}} < 1.0]$ is an indicator that adds $0.5$ when raw material reserves are low. The final GATHER utility is the maximum of the two sub-scores.

**BUILD** requires both unbuilt machines and raw material in inventory:

$$U_{\text{build}} = f_{\text{compliance}} \cdot f(\text{purpose}) \cdot 1.2 \cdot \min\!\big(1,\; \text{raw\_material} / 2.0\big)$$

The compliance gating ensures that only agents with sufficient obedience invest in long-term infrastructure.

**WORK** operates built machines:

$$U_{\text{work}} = f_{\text{compliance}} \cdot f(\text{hunger}) \cdot 0.8 + (1 - f_{\text{laziness}}) \cdot f(\text{purpose}) \cdot 0.3$$

This formulation separates two motivations for working: compliance-driven hunger response and intrinsic purpose satisfaction (diluted by laziness). A compliant, non-lazy agent works both because they are told to and because they find meaning in it.

### Self Utility

The self-oriented actions derive their utility directly from personality-weighted need urgency:

**EAT** is gated by food availability and scales with hunger urgency:

$$U_{\text{eat}} = f(\text{hunger}) \cdot w_{\text{eat}}$$

where $w_{\text{eat}} = 1.3$ normally, rising to $1.8$ when $\text{food\_security} > 0.3$. The increased weight when food is available prevents agents from hoarding food without eating.

**REST** is modulated by laziness and fatigue level:

$$U_{\text{rest}} = (0.4 + 0.6 \cdot f_{\text{laziness}}) \cdot f(\text{rest}) \cdot [1 + 0.5 \cdot (\text{rest} > 0.7)]$$

The $1.5\times$ multiplier at high fatigue prevents agents from working themselves to death.

**SOCIALIZE** is driven by gregariousness:

$$U_{\text{socialize}} = f_{\text{gregariousness}} \cdot f(\text{social})$$

**CREATE** is driven by artistry:

$$U_{\text{create}} = f_{\text{artistry}} \cdot f(\text{expression})$$

**EXPLORE** combines curiosity with purpose need:

$$U_{\text{explore}} = f_{\text{curiosity}} \cdot f(\text{purpose}) \cdot 0.3$$

### The Structural Tensions

The decomposition $U(a) = U_{\text{factory}}(a) + U_{\text{self}}(a)$ produces four structural tensions that define the game's emergent dynamics.

**The compliance spectrum.** Agents range from obedient workers, for whom $U_{\text{factory}} \gg U_{\text{self}}$, to reluctant inhabitants, for whom $U_{\text{self}} \gg U_{\text{factory}}$. The factory requires a minimum threshold of compliant agents to function, but compliance is randomly assigned at birth and cannot be controlled. If a population drifts toward low compliance---through random mortality of compliant agents or stochastic spawn variation---the factory may fail despite no individual agent behaving irrationally.

**The free-rider problem.** All agents benefit from the factory running regardless of their individual contribution. This is the spatial Prisoner's Dilemma from Nowak and May (Section @sec:social): on a 2D grid, cooperators (high-compliance agents) form clusters that sustain production, but defectors (low-compliance agents) at cluster boundaries free-ride on the collective output. The free-rider gains the same food supply as the worker without expending time on factory tasks, leaving more time for self-oriented needs. If the proportion of free-riders exceeds a critical threshold, the cooperative cluster collapses and all agents---including the free-riders---starve.

**The artisan's dilemma.** An agent born with high artistry ($f_{\text{artistry}} \gg f_{\text{compliance}}$) has a strong internal drive to create art: when expression need rises, $U_{\text{create}}$ dominates the utility landscape. But CREATE produces no factory output. The agent must choose between self-expression and contributing to collective survival. If the artisan creates, they become a free-rider by default, benefiting from the factory without contributing. If the artisan works, their high expression need remains unsatisfied, driving stress accumulation (since expression contributes to stress proportionally to $f_{\text{artistry}}$). The artisan is thus doubly penalized: working causes psychological harm, and not working causes collective harm.

**The minimum work principle.** The laziness facet ensures that agents prefer to work the minimum necessary to satisfy their $U_{\text{factory}}$ component, then redirect effort to $U_{\text{self}}$. Concretely, once hunger is satiated, the $U_{\text{eat}}$ and $U_{\text{gather\_food}}$ scores drop, and the rest weight---amplified by laziness---rises to dominate. This produces natural variation in work output across the population: at any given tick, some agents are working while others are resting, socializing, or creating, even if all have similar need profiles. The population-level effect is a steady-state labor supply that is always less than the theoretical maximum, requiring the factory to be overbuilt relative to the minimum viable workforce.
