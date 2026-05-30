# The Factory: World Substrate {#sec:factory}

## 2D Grid Rationale

The simulation world is a bounded 2D grid of $60 \times 40$ tiles. The choice of dimensionality follows the adequacy principle established in Section @sec:diseno: the simulation should model phenomena at the resolution where they produce observable behavioral differences.

The core mechanics of *La Vida Misma* are social and economic, not spatial. The phenomena that the simulation must support---natural specialization (Section @sec:agentes), spatial segregation (Schelling's model, Section @sec:agentes), cooperation and free-riding (Nowak and May, Section @sec:social), and emergent space use---all operate on 2D lattices in the academic literature. Adding a third dimension increases pathfinding cost (the branching factor $b$ in A* grows from the 8 neighbors of a Moore neighborhood to 26 in 3D), complicates fluid and physics simulation, and requires visualisation infrastructure that does not contribute to the core design question.

Dwarf Fortress uses 3D because mining, fluid pressure, and vertical construction are central to its gameplay. In *La Vida Misma*, the central question is how agents organize themselves within a constrained space. A 2D grid is the adequate substrate for this question. The specific dimensions of $60 \times 40 = 2400$ tiles provide sufficient spatial diversity for resource clustering and territorial emergence while remaining computationally tractable for per-tick tile updates and pathfinding.

**Future expansion**: If verticality becomes mechanically necessary (e.g., a multi-story factory, basement storage, or roof access), the architecture should support extension to 2.5D (a small number of discrete Z-levels) without redesigning the core systems. This is architecturally straightforward in an ECS framework (Section @sec:diseno) because the Position component is a data structure that can be extended from $(x, y)$ to $(x, y, z)$ without modifying any system that does not explicitly depend on dimensionality.

## Tile Types

Each tile in the grid is classified by a discrete type that determines traversability, available agent actions, and interaction semantics. The tile type taxonomy is:

| Type | Traversable | Description |
|---|---|---|
| Wall | No | Perimeter and structural barriers; delimit the factory boundary and interior rooms |
| Floor | Yes | Generic walkable interior surface |
| OpenSpace | Yes | Exterior or unbuilt area; traversable but lacks infrastructure |
| FoodSource | Yes | Wild food deposit; yields `raw_food` via the GATHER action; regenerates over time |
| ScrapPile | Yes | Raw material deposit; yields `raw_material` via the GATHER action; finite with slow regeneration |
| Machine | Conditional | Processing station; must be built before use; converts `raw_material` into `processed_food` via the WORK action |
| Storage | Yes | Warehousing tile; holds processed and raw goods for collective access |
| Entrance | Yes | Boundary portal for external resource inflow (architecturally supported, not yet active) |
| Exit | Yes | Boundary portal for finished goods outflow (architecturally supported, not yet active) |

The default layout places perimeter walls along all grid edges, clusters of FoodSource tiles near corners and midpoints to encourage exploration, scattered ScrapPile tiles throughout the interior, 16 unbuilt Machine tiles, 10 Storage tiles, and designated Entrance and Exit tiles on the boundary.

## Tile Data Model

Each tile maintains a mutable data record (the `TileData` struct) whose fields depend on the tile type. The complete field set is:

| Field | Type | Applicability | Description |
|---|---|---|---|
| `resource_amount` | float | FoodSource, ScrapPile | Current quantity of resource available at this tile |
| `resource_max` | float | FoodSource, ScrapPile | Maximum resource capacity of this tile |
| `resource_regen` | float | FoodSource, ScrapPile | Regeneration rate per tick |
| `built` | bool | Machine | Whether construction is complete |
| `build_progress` | float | Machine | Accumulated build effort toward `build_cost` |
| `build_cost` | float | Machine | Total effort required to complete construction |
| `stored_food` | float | Storage | Quantity of processed food currently stored |
| `stored_raw_food` | float | Storage | Quantity of raw food currently stored |
| `stored_raw_material` | float | Storage | Quantity of raw material currently stored |
| `storage_capacity` | float | Storage | Maximum total units this tile can hold |

For source tiles (FoodSource, ScrapPile), regeneration follows:

$$r(t+1) = \min\!\bigl(r_{\max},\; r(t) + \rho \bigr)$$

where $r(t)$ is `resource_amount` at tick $t$, $r_{\max}$ is `resource_max`, and $\rho$ is `resource_regen`. FoodSource tiles regenerate at a moderate rate, supporting sustained foraging. ScrapPile tiles regenerate slowly (or negligibly), making them effectively finite and creating scarcity pressure.

Tile properties are updated by the EnvironmentSystem each tick (Section @sec:tick-loop).

## Resource Sources and the Production Chain

The production model provides two categories of resource input:

### Internal Sources

Resources are embedded directly in the map as harvestable tiles. This is the primary production model in the current implementation.

**FoodSource tiles** provide wild food that agents can gather directly:

$$\text{FoodSource} \xrightarrow{\text{GATHER}} \text{raw\_food} \xrightarrow{\text{EAT}} \text{subsistence}$$

This is the subsistence pathway: any agent adjacent to or standing on a FoodSource tile may execute the GATHER action to extract `raw_food`, which can be consumed directly via EAT. This requires no infrastructure and no coordination, but wild food is less efficient than processed food.

**ScrapPile tiles** provide raw materials that feed the industrial pathway:

$$\text{ScrapPile} \xrightarrow{\text{GATHER}} \text{raw\_material} \xrightarrow{\text{BUILD}} \text{Machine} \xrightarrow{\text{WORK}} \text{processed\_food} \xrightarrow{\text{store}} \text{Storage} \xrightarrow{\text{EAT}} \text{efficient nutrition}$$

This is the industrial pathway: agents gather `raw_material` from ScrapPile tiles, invest it in constructing Machine tiles (via the BUILD action), and then operate completed machines (via the WORK action) to produce `processed_food`. Processed food is stored in Storage tiles and consumed via EAT. The industrial pathway yields more efficient nutrition per unit of agent effort but requires coordination: machines must be built before they can be operated, and storage must be maintained for the output.

The dual-pathway design creates a natural tension between short-term individual foraging (FoodSource) and long-term collective investment (ScrapPile → Machine → Storage). Agents must decide, based on their drives and social context (Section @sec:agentes), whether to subsist on wild food or contribute to industrial production.

### External Sources (Architectural Provision)

Entrance and Exit tiles define a boundary pathway for external resource exchange:

- **Entrance tiles** are designated to receive resource shipments from outside the factory, providing an inflow channel for raw materials, food, medicine, and other goods.
- **Exit tiles** are designated to ship finished goods out of the factory.

This pathway is architecturally supported---the tile types, boundary placement, and system hooks exist in the codebase---but is not yet active in the current simulation. When activated, the external pathway will introduce a Director-driven quota system (Section @sec:director): meeting the production quota sustains inflow rates, while sustained underproduction triggers supply chain contraction. This will add a collective survival pressure layer on top of the internal resource economy described above.

## Spatial Layout and Emergent Organization

The fixed $60 \times 40$ layout is designed to produce spatial asymmetries that drive emergent behavior:

- FoodSource clusters near corners and midpoints create multiple foraging territories, encouraging territoriality and the spatial segregation patterns predicted by Schelling's model.
- ScrapPile scatter ensures that raw material acquisition requires movement across the map, increasing the distance penalty for hauling tasks and favoring agents who establish efficient routes.
- The 16 Machine tiles, initially unbuilt, present a collective action problem: no single agent can build all machines, but an unbuilt machine benefits no one. This mirrors the public goods dilemmas studied by Nowak and May on spatial lattices.
- Storage tiles distributed near machine clusters reduce hauling distance for the industrial pathway, creating locational value that agents may compete to control.

The distance between resource sources, machines, and storage affects task utility via the distance penalty (Eq. @eq:task-distance), which creates natural industrial organization: agents prefer nearby tasks, so clusters of complementary tiles receive more consistent service than isolated ones.
