# La Vida Misma: Design Specification

> This document is the practical crystallization of the simulation design. For the full academic treatment with formal proofs, cross-referenced mathematical foundations, and literature citations, see the compiled document at `bases_matematicas.pdf` (Part II: Sections 12--19).

## 1. Core Concept

A community simulation where agents inhabit a factory they did not build, do not control, and do not understand. The factory produces something meaningless -- the output is determined by an external agent (the player). The agents' primary constraint is survival: if the factory stops, they die. But survival is not their only drive. They have internal impulses, preferences, and talents that have nothing to do with factory output. The tension between *what the factory requires* and *what the agents want* is the engine of the simulation.

Closer to a company town, a prison, an institution: a closed system where the inhabitants did not choose to be there but must make life within it. Unlike Dwarf Fortress (agents building a home) or RimWorld (agents surviving a crash), there is no founding act -- the factory preexists them and they wake inside it.

## 2. The Factory Metaphor as a Formal System

The metaphor translates directly into the simulation frameworks established in *Mathematical Foundations of Community Simulation Engines*. This section maps each narrative element to its formal component.

### 2.1 The Factory = The World Substrate

The factory is a bounded 2D grid. The choice of 2D is deliberate:

- A 2D grid is sufficient to model the core phenomena (spatial proximity, resource flow, territory formation, segregation). The Nowak-May and Schelling results both operate on 2D lattices.
- 3D adds pathfinding cost ($O(b^d)$ increases), fluid complexity (gravity in 3 directions), and visualisation complexity that does not contribute to the core mechanic.
- Dwarf Fortress uses 3D because mining and fluid physics are central to its design. In La Vida Misma, the central tension is social, not spatial. 2D is the adequate approximation per the adequacy principle (Section 7.2 of *Mathematical Foundations*).

The factory is a grid of $W \times H$ tiles. Each tile has a type (floor, wall, machine, storage, open space, garden, etc.) and a set of properties (temperature, cleanliness, ownership, ambient noise).

**Decision**: 2D grid. Expand to 2.5D (multiple Z-levels) only if verticality becomes mechanically necessary (e.g., the factory has a basement or roof). Start flat.

### 2.2 The External Agent = The Player / Director

An entity outside the simulation that sets production targets, defines what the factory produces, and can modify the factory layout. This is analogous to RimWorld's Storyteller but operates at the infrastructure level rather than the event level.

Formally, the Director defines:

- **Production requirements**: what outputs the factory must produce per unit time.
- **Resource inputs**: what raw materials enter the factory (from outside the grid, from the edges).
- **Layout constraints**: where machines can be placed, where walls can be built.

The Director does not control agents directly. The Director modifies the *environment* and the agents respond through their utility functions. This is the Dwarf Fortress design principle: the player does not select a dwarf and click "eat." The player builds a dining hall and the dwarf decides to eat because its hunger utility exceeds all other options.

### 2.3 The Agents = Factory Inhabitants

Each agent is an ECS entity with the following components:

**NeedsComponent** (drives behavior):
| Need | Decay rate | Satisfaction method | Effect at critical level |
|---|---|---|---|
| Hunger | Linear, constant | Eat food | Death |
| Rest | Linear, constant | Sleep in a bed | Collapse (forced sleep), reduced skill |
| Social | Slow, constant | Proximity + interaction with others | Stress accumulation, isolation penalty |
| Expression | Very slow, variable | Perform artistic/musical activity | Stress accumulation, personality-dependent |
| Purpose | Slow, irregular | Varies by personality | Stress accumulation, the "why am I here" drive |

The key addition relative to standard utility AI is the **Expression** and **Purpose** needs. These are the needs that have no factory function. A musician playing music does not produce anything the factory requires. But the musician *needs* to play music. This is the core tension: the factory does not care about the musician's need, but the musician's utility function does.

**PersonalityComponent** (drives differentiation):
$$P = (f_1, f_2, \ldots, f_k), \quad f_i \in [0, 1]$$

Facets include:
| Facet | Effect on utility weights |
|---|---|
| Compliance | Weight of factory-assigned tasks vs. personal needs |
| Laziness | Weight of rest/leisure vs. productive action |
| Artistry | Weight of expression need, probability of artistic action |
| Gregariousness | Weight of social need, interaction radius |
| Resilience | Stress recovery rate, tolerance for adverse events |
| Curiosity | Probability of exploring unknown tiles, weight of novelty |

Personality is assigned at agent creation and is immutable (or nearly so -- very slow drift under extreme stress). This ensures that agents are *born different* rather than becoming different through experience alone.

**SkillsComponent** (drives specialization):
Same progression model as *Mathematical Foundations* Eq. @eq:skill-xp:

$$\text{XP for level } N = \text{base} + \text{increment} \times N$$

Skill categories: factory work (machine operation, assembly, hauling), domestic (cooking, cleaning), artistic (music, painting, storytelling), social (mediation, leadership). Skills improve with practice and decay with disuse. Specialization emerges from the feedback loop: agents who are good at something are assigned it more, so they get better at it.

**StressComponent** (drives long-term dynamics):
Same three-layer model as *Mathematical Foundations* Eq. @eq:stress:

$$\text{stress}_t = \sigma\left(\sum_{e \in E_t} \text{impact}(e) \cdot \text{modulate}(e, P) - \text{recovery}(P)\right)$$

Events that cause stress:
- Factory-related: production failure, quota not met, machine breakdown
- Social: conflict with another agent, isolation, loss of a relationship
- Existential: witnessing death, prolonged unfulfilled purpose need, forced compliance
- Environmental: cold, noise, dirt, overcrowding

Events that reduce stress:
- Social: positive interaction, friendship, shared meal
- Expressive: artistic activity (for high-artistry agents), music, storytelling
- Environmental: clean space, garden, quiet area

**RelationshipsComponent** (the social graph):
Weighted directed graph $G = (V, E, w)$ with $w(i, j) \in [-100, 100]$. Modified by:
- Proximity: agents who share space accumulate positive weight slowly (mere exposure effect).
- Collaboration: working together on a task increases weight.
- Conflict: competing for resources or space decreases weight.
- Expression: artistic performance increases weight for audience members with high artistry facet.
- Stress contagion: propagates through the graph per *Mathematical Foundations* Eq. @eq:stress-contagion.

### 2.4 The Utility Function = The Tension Engine

This is where the design departs from standard simulation. The utility function has two opposing components:

$$U(a) = \underbrace{U_{\text{factory}}(a)}_{\text{survival}} + \underbrace{U_{\text{self}}(a)}_{\text{living}}$$

$$U_{\text{factory}}(a) = w_{\text{compliance}} \cdot f(\text{hunger}) + w_{\text{fear}} \cdot f(\text{factory\_health})$$

$$U_{\text{self}}(a) = w_{\text{laziness}} \cdot f(\text{rest}) + w_{\text{social}} \cdot f(\text{social}) + w_{\text{artistry}} \cdot f(\text{expression}) + w_{\text{purpose}} \cdot f(\text{purpose})$$

The weights $w_i$ are personality-dependent. A high-compliance, low-artistry agent will almost always select factory work. A low-compliance, high-artistry agent will neglect factory work to play music. The population as a whole must produce enough to keep the factory running, but individual agents may not cooperate.

This creates the following dynamics:

1. **The compliance spectrum**: Agents range from obedient workers to reluctant inhabitants. The factory needs a minimum number of compliant agents to function. If too many agents are non-compliant, production fails, resources stop arriving, and agents starve.

2. **The free-rider problem**: Since all agents benefit from the factory running regardless of their contribution, there is an incentive to free-ride. This is directly modeled by the Nowak-May spatial game theory result (*Mathematical Foundations* Section 6.4): on a 2D lattice, cooperators can form clusters that sustain production, but defectors at the edges exploit them.

3. **The artisan's dilemma**: An agent born with high artistry has a strong internal drive to create art, but art does not produce factory output. The agent must choose between self-expression and survival. The player/Director can resolve this by creating spaces where art contributes indirectly (morale boost for nearby agents), but this is not automatic.

4. **The minimum work principle**: "todos quieren trabajar lo menos posible." Each agent's laziness facet increases the utility of rest/leisure relative to productive action. In a compliant population, this produces the effect that agents work the minimum necessary to meet quotas. In a non-compliant population, it produces underproduction and crisis.

### 2.5 The Spaces = Emergent Architecture

"Generan espacios dentro de la fabrica, espacios organizados para poder sobrellevar el tiempo."

Agents modify their environment to satisfy needs. This is not scripted; it emerges from the utility system:

- An agent with high social need will seek out the location where other agents gather (the cafeteria, the courtyard). If no such space exists, the agent will spend time in whichever space has the highest agent density, and other social agents will follow. A *de facto* gathering space emerges without being designated.
- An agent with high artistry will seek a quiet space with low traffic and attempt to perform. Other high-artistry agents will be drawn to the area (social reinforcement through expression need). A *de facto* studio emerges.
- An agent with high laziness will seek the space farthest from assigned work stations. Other lazy agents will cluster there. A *de facto* break room emerges.

The Schelling model (*Mathematical Foundations* Section 3.6) predicts this: agents with mild preferences for certain spatial properties will spontaneously segregate into distinct zones. The factory does not designate a "garden district" or an "artists' corner." These emerge from agent preferences operating on the spatial substrate.

The Director can facilitate this by building rooms, placing furniture, or leaving open spaces. But the *use* of those spaces is determined by agent behavior, not by designation.

### 2.6 Resources and Flow

"Recursos que tienen que ir y salir hacia otra parte."

Resources enter the factory from the edges of the grid (representing the outside world) and exit as finished products. The production chain is:

$$\text{Raw materials (edge)} \to \text{Processing (machines)} \to \text{Assembly (workshops)} \to \text{Finished goods (edge)}$$

Agents must haul materials between stages. The distance between stages affects the utility of hauling tasks (*Mathematical Foundations* Eq. @eq:task-distance), which creates natural industrial organization.

The factory's output quota is set by the Director. If the quota is met, resources continue to arrive. If not, the external system reduces input (simulating a contract being cancelled or a supplier withdrawing). This creates a survival pressure that operates at the collective level, not the individual level: no single agent is responsible for meeting the quota, but all agents suffer if it is not met.

Additional resource flows:
- **Food**: must be produced or received from outside. Agents who do not eat die.
- **Medicine**: required for treating injuries and illness. Scarce.
- **Art supplies**: required for satisfying the expression need. Not necessary for survival, but their absence increases stress in artistic agents.

### 2.7 Birth and Death

"En la fábrica vive gente, en la fábrica muere gente."

**Death**: Agents die from starvation (hunger need at maximum for sustained period), catastrophic events (machine explosion, structural collapse), or accumulated stress (stress at maximum triggering a breakdown event that removes the agent). Death is permanent. The agent's relationships become grief events for connected agents.

**Birth**: New agents arrive at the factory entrance (grid edge). The Director does not control who arrives. Each new agent is generated with random personality facets and no skills. The population must integrate new arrivals through the social graph: existing agents must interact with newcomers, and the newcomers must find their role through the utility/skill feedback loop.

The birth-death cycle creates generational dynamics: the original agents who built the factory's social structure eventually die, and new agents without that history must sustain or modify what was built. Social memory exists only in the relationship graph, not in the agents themselves.

## 3. Systems Architecture

### 3.1 Tick-Based Update Loop

Each simulation tick processes systems in order:

```
1. DecaySystem:        reduce all needs for all agents
2. EventSystem:        generate external events (resource arrivals, quota checks)
3. UtilitySystem:      compute utilities for all agents, select actions
4. ActionSystem:       execute selected actions (move, work, eat, socialize, create)
5. ProductionSystem:   process factory production based on completed tasks
6. StressSystem:       update stress based on events, propagate through social graph
7. RelationshipSystem: update relationship weights based on interactions
8. EnvironmentSystem:  update tile properties (cleanliness, wear, decay)
9. NarrativeSystem:    log significant events for the player
```

### 3.2 Emergence Targets

These are the phenomena the simulation should produce *without being explicitly programmed*:

| Phenomenon | Mechanism that produces it | Formal basis |
|---|---|---|
| Natural specialization | Skill-utility feedback loop | *Mathematical Foundations* Eq. @eq:skill-xp |
| Spatial segregation | Personality-driven tile preference + Schelling dynamics | Schelling (1971) |
| Informal leadership | High-gregariousness agents become central in social graph | Network centrality |
| Artistic subcultures | High-artistry agents cluster, reinforce each other's expression | Schelling + bounded confidence |
| Free-rider crisis | Low-compliance agents exploit high-compliance agents | Nowak-May spatial game theory |
| Grief cascades | Death of central agent propagates through social graph | Stress contagion Eq. @eq:stress-contagion |
| Strikes or slowdowns | Critical mass of low-compliance agents reduces output below quota | Threshold models |
| Emergent spaces | Agent density patterns self-organize around need satisfaction | Schelling segregation |
| Generational turnover | Old agents die, new agents lack shared history | Birth-death cycle + social graph persistence |

### 3.3 What the Player Does

The player interacts with the simulation at the *environmental* level:

- **Build and modify the factory**: place machines, walls, storage areas, open spaces.
- **Set production quotas**: determine how much output the factory must produce.
- **Observe**: watch agents behave, read the narrative log, inspect relationships and stress levels.
- **Do NOT directly control agents**: agents decide their own actions based on utility.

This is the Dwarf Fortress model: the player is a city planner, not a micromanager. The interesting behavior emerges from agents responding to the environment the player creates.

## 4. Design Decisions and Rationale

| Decision | Choice | Rationale |
|---|---|---|
| Grid dimensionality | 2D | Adequate for social mechanics; avoids unnecessary pathfinding cost |
| Agent control | Indirect (environment only) | Emergence requires agent autonomy |
| Personality | Immutable at creation | Ensures stable behavioral diversity |
| Artistic need | Present, not factory-productive | Core tension between survival and living |
| Resource flow | Edge-to-edge through factory | Creates hauling logistics and spatial layout decisions |
| Death | Permanent | Gives consequences to player decisions and agent behavior |
| New agents | Random, from edge | Prevents player from optimizing population composition |
| LLM integration | None for core simulation | All decisions are utility-based; LLM optional for narrative rendering only |

## 5. Implementation Priority

Following the phased roadmap from *Mathematical Foundations* Section 9, adapted for La Vida Misma:

**Phase 1 -- The Factory Floor**: 2D grid with tile types, simple Perlin terrain (industrial floor with varied zones), resource input/output at edges. ~2,000 LOC.

**Phase 2 -- The Workers**: Agents with Needs, Personality, and UtilityAI. Factory work actions (operate machine, haul material) and personal actions (rest, socialize, create). ~3,000 LOC.

**Phase 3 -- Moving Around**: A* pathfinding on 2D grid with tile costs. ~1,500 LOC.

**Phase 4 -- Production Chain**: Machine processing, resource flow, quota system. Factory health as a global variable that affects all agents' fear utility. ~1,500 LOC.

**Phase 5 -- Social Fabric**: Relationship graph, stress system, stress contagion, personality-driven interaction. ~2,500 LOC.

**Phase 6 -- The Spaces**: Agent-driven space use, environmental modification, emergent zone formation. Schelling dynamics via tile preference. ~1,000 LOC.

**Phase 7 -- Life and Death**: Birth-death cycle, grief events, generational turnover, narrative logging. ~1,000 LOC.

Total: ~12,500 LOC.
