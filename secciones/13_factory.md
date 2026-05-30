# The Factory: World Substrate {#sec:factory}

## 2D Grid Rationale

The simulation world is a bounded 2D grid of $W \times H$ tiles. The choice of dimensionality follows the adequacy principle established in Section @sec:diseno: the simulation should model phenomena at the resolution where they produce observable behavioral differences.

The core mechanics of *La Vida Misma* are social and economic, not spatial. The phenomena that the simulation must support---natural specialization (Section @sec:agentes), spatial segregation (Schelling's model, Section @sec:agentes), cooperation and free-riding (Nowak and May, Section @sec:social), and emergent space use---all operate on 2D lattices in the academic literature. Adding a third dimension increases pathfinding cost (the branching factor $b$ in A* grows from the 8 neighbors of a Moore neighborhood to 26 in 3D), complicates fluid and physics simulation, and requires visualisation infrastructure that does not contribute to the core design question.

Dwarf Fortress uses 3D because mining, fluid pressure, and vertical construction are central to its gameplay. In *La Vida Misma*, the central question is how agents organize themselves within a constrained space. A 2D grid is the adequate substrate for this question.

**Future expansion**: If verticality becomes mechanically necessary (e.g., a multi-story factory, basement storage, or roof access), the architecture should support extension to 2.5D (a small number of discrete Z-levels) without redesigning the core systems. This is architecturally straightforward in an ECS framework (Section @sec:diseno) because the Position component is a data structure that can be extended from $(x, y)$ to $(x, y, z)$ without modifying any system that does not explicitly depend on dimensionality.

## Tile Properties

Each tile in the grid has a type and a set of mutable properties:

| Property | Type | Effect |
|---|---|---|
| Type | enum: floor, wall, machine, storage, open, entrance, exit | Determines traversability and available actions |
| Temperature | float | Affects agent comfort; computed via diffusion (Eq. @eq:heat-diffusion) |
| Cleanliness | float $[0, 1]$ | Decays with use; affects stress for agents in the tile |
| Ownership | agent ID or null | Which agent (if any) has claimed this tile for personal use |
| Ambient noise | float | Sum of machine activity nearby; affects stress and artistic expression |
| Wear | float | Accumulates with use; machines with high wear break down |

Tile properties are updated by the EnvironmentSystem each tick (Section @sec:tick-loop). The temperature system reuses the discrete heat diffusion model from Section @sec:fisicos. Cleanliness and wear are simple accumulation-decay processes that create maintenance tasks for agents.

## Factory Boundary and Resource Flow

The factory receives resources from the outside world through designated entrance tiles on the grid boundary. Finished products exit through designated exit tiles. The resource flow model is:

$$\text{Raw materials (entrance)} \xrightarrow{\text{haul}} \text{Processing (machines)} \xrightarrow{\text{haul}} \text{Assembly (workshops)} \xrightarrow{\text{haul}} \text{Finished goods (exit)}$$

Each hauling step requires an agent to carry materials between locations. The distance between stages affects the utility of hauling tasks via the distance penalty (Eq. @eq:task-distance), which creates natural industrial organization: agents prefer nearby tasks, so workshops that are adjacent to their input sources receive more consistent service.

The factory's output quota is set by the Director (Section @sec:director). If the quota is met, resources continue to arrive at the entrance tiles. If the quota is not met for a sustained period, the external system reduces input rates, simulating a supply chain response to underproduction. This creates a collective survival pressure: no single agent is responsible for meeting the quota, but all agents suffer if it is not met.

Additional resource categories:

| Resource | Source | Necessity | Notes |
|---|---|---|---|
| Raw materials | Entrance tiles | Required for production | Flow rate depends on quota compliance |
| Food | Entrance tiles or internal production | Required for survival | Agents who do not eat die |
| Medicine | Entrance tiles | Required for injury/illness | Scarce; creates triage decisions |
| Art supplies | Entrance tiles | Not required for survival | Absence increases stress in high-artistry agents |
