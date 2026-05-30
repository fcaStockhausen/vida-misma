# Simulation Design Principles {#sec:diseno}

The preceding sections have presented the mathematical components individually. This section synthesizes the design philosophy that guides their combination into a coherent simulation system, drawing primarily on the design methodology of Dwarf Fortress and the formal properties of emergent systems.

## Decomposition and Emergent Composition

The central design principle of Dwarf Fortress, articulated by Tarn Adams [@adams2014}, is decomposition: rather than programming composite phenomena directly, program independent subsystems and allow complex phenomena to emerge from their interaction. A "desert" is not a single entity; it is the intersection of high temperature, low rainfall, well-drained soil, and sparse vegetation---each computed independently. A "depressed dwarf" is not a scripted state; it is the conjunction of high stress, negative recent events, a personality susceptible to anxiety, and a social graph that provides insufficient positive reinforcement.

This principle has a formal basis in the theory of complex systems: when multiple independent processes operate on a shared substrate (the tile grid, the agent state, the social graph), their intersection produces a combinatorial space of possible states that is exponentially larger than what any single process could generate. The designer does not need to anticipate every possible combination; the system produces them automatically.

## The Adequacy Principle

Adams' second principle is that simulation fidelity should be calibrated to the *observable resolution* of the player (or, more generally, the consumer of the simulation output). Dwarf Fortress fluids do not simulate turbulence, viscosity, or surface tension because these phenomena are not observable at the tile-level resolution of the game. The 8-level quantization was chosen because it is the minimum that allows players to visually distinguish fluid states.

This is not a compromise; it is an application of the principle of *behavioral adequacy*: a model should reproduce the qualitative phenomena relevant to its purpose, not the underlying physics. The Navier-Stokes equations are not "better" than the 7-level CA for simulation purposes if the additional fidelity does not produce observable behavioral differences. The computational savings from reduced fidelity can be invested in other subsystems (more agents, larger maps, additional social dynamics).

## Iterative Development

Dwarf Fortress has been developed over approximately 20 years, accumulating roughly 700,000 lines of code [@adams2021}. Adams describes his development process as iterative: build a minimal system, observe what emerges, identify gaps or failures, extend the system, repeat. This is consistent with the methodology of agent-based modeling in the scientific literature, where the ODD protocol [@grimm2020} similarly emphasizes incremental model development with explicit documentation of each extension.

The practical implication for engine design is that the initial implementation should be minimal and correct, not comprehensive. Each subsystem should be independently testable and should produce observable output. Complex behavior should emerge from the interaction of simple subsystems, not from the complexity of individual subsystems.

## Real-World Analogues

Adams' fourth principle is to ground simulation rules in real-world phenomena, not because the simulation should be realistic, but because the real world has already solved many design problems through evolution and physical law. The orographic rain shadow model (mountains block moisture-carrying winds, producing dry leeward regions) was adopted from meteorology and immediately improved world generation because it captures a genuine statistical regularity in climate geography.

This principle applies beyond terrain generation: the stress and personality model draws on clinical psychology, the social graph draws on sociological network models, and the utility AI draws on microeconomic decision theory. Grounding simulation rules in established models from other disciplines increases the probability that the emergent behavior will be qualitatively plausible.

## Entity-Component-System Architecture

The implementation architecture that supports these principles is Entity-Component-System (ECS):

- **Entity**: an opaque identifier with no behavior. Each agent, item, or tile is an entity.
- **Component**: a pure data structure. Position, Health, Needs, Skills, Personality are each independent components attached to entities.
- **System**: a function that operates on all entities possessing a specific set of components. NeedDecaySystem updates all entities with a Needs component; PathfindingSystem processes all entities with both Position and MovementGoal components.

The critical property of ECS is that systems are independent: the pathfinding system has no knowledge of the needs system. Coordination occurs through shared data in components. An agent with high hunger (Needs component) receives high utility for "eat" actions (UtilityAI system) and requests a path to the dining hall (Pathfinding system). The three systems never communicate directly; they interact through the entity's component state.

This decoupling enables incremental development: new systems can be added without modifying existing ones, as long as they operate on well-defined component types. It also facilitates testing: each system can be validated in isolation by constructing entities with known component states and verifying the system's output.

Nystrom (2014) [@nystrom2014} provides a practical treatment of ECS in the context of game development, emphasizing its advantages for cache-friendly data access patterns and its suitability for simulations with large numbers of entities.
