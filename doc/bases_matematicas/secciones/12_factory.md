# The Factory: World Substrate {#sec:factory}

## 2D Grid Rationale

The simulation world is a bounded 2D grid of $60 \times 40$ tiles. The choice of dimensionality follows the adequacy principle established in Section @sec:diseno: the simulation should model phenomena at the resolution where they produce observable behavioral differences.

The core mechanics of *La Vida Misma* are social and economic, not spatial. The phenomena that the simulation must support---natural specialization (Section @sec:agentes), spatial segregation (Schelling's model, Section @sec:agentes), cooperation and free-riding (Nowak and May, Section @sec:social), and emergent space use---all operate on 2D lattices in the academic literature. Adding a third dimension increases pathfinding cost (the branching factor $b$ in A* grows from the 8 neighbors of a Moore neighborhood to 26 in 3D), complicates fluid and physics simulation, and requires visualisation infrastructure that does not contribute to the core design question.

Dwarf Fortress uses 3D because mining, fluid pressure, and vertical construction are central to its gameplay. In *La Vida Misma*, the central question is how agents organize themselves within a constrained space. A 2D grid is the adequate substrate for this question. The specific dimensions of $60 \times 40 = 2400$ tiles provide sufficient spatial diversity for resource clustering and territorial emergence while remaining computationally tractable for per-tick tile updates and pathfinding.

**Future expansion**: If verticality becomes mechanically necessary (e.g., a multi-story factory, basement storage, or roof access), the architecture should support extension to 2.5D (a small number of discrete Z-levels) without redesigning the core systems. This is architecturally straightforward in an ECS framework (Section @sec:diseno) because the Position component is a data structure that can be extended from $(x, y)$ to $(x, y, z)$ without modifying any system that does not explicitly depend on dimensionality.

## Tile Types {#sec:tile-types}

Each tile in the grid is classified by a discrete type that determines traversability, available agent actions, and interaction semantics. The complete tile type taxonomy is:

\footnotesize

| Type | Walk | Description |
|:-----|:----:|:------------|
| Wall | No | Perimeter and structural barriers |
| Floor | Yes | Generic walkable interior; substrate for construction |
| OpenSpace | Yes | Exterior/unbuilt area; traversable, no infrastructure |
| FoodSource | Yes | Wild food deposit; yields `raw_food` via GATHER; regenerates |
| ScrapPile | Yes | Raw material deposit; yields `raw_material`; finite, slow regen |
| Machine | Cond. | Processing station; must be built; three subtypes (§ below) |
| Storage | Yes | Warehousing tile; holds processed and raw goods |
| Entrance | Yes | Boundary portal for external resource inflow |
| Exit | Yes | Boundary portal for finished goods outflow |
| EatingZone | Cond. | Eating area; must be built; at most one per factory |
| Conveyor | Cond. | Directional transport; blocks movement when built, walkable when unbuilt |

\normalsize

### Machine Subtypes {#sec:machine-subtypes}

Machines are not homogeneous. The factory requires three functionally distinct machine types, each converting specific inputs into specific outputs. The subtypes form a dependency chain that drives the factory's economy:

\footnotesize

| Subtype | Input | Output | Role |
|:--------|:------|:-------|:-----|
| **FoodMachine** | `raw_food` (FoodSource) | `processed_food` | Processes wild food into safe nutrition. Inefficient rate (output < regen) creates depletion pressure. |
| **MaterialsMachine** | `raw_material` (ScrapPile) | `construction_material` + scrap | Converts raw material into construction blocks. Generates recyclable scrap byproduct. |
| **OutputMachine** | `construction_material` | Factory health restoration | Produces nothing for agents; restores $h_{\text{factory}}$. The Director's existential metric. |

\normalsize

The dependency chain is:

1. **Materials chain**: ScrapPile $\xrightarrow{\text{GATHER}}$ raw_material $\xrightarrow{\text{MatMachine}}$ constr._material $\xrightarrow{\text{OutMachine}}$ $h_{\text{factory}}$

2. **Food chain**: FoodSource $\xrightarrow{\text{GATHER}}$ raw_food $\xrightarrow{\text{FoodMachine}}$ processed_food $\xrightarrow{\text{EAT}}$ subsistence

This three-tier structure creates a natural priority hierarchy: the factory cannot survive without the OutputMachine, but the OutputMachine cannot be built without the MaterialsMachine, and the agents operating these machines need food from the FoodMachine. Each tier depends on the one below it.

### FoodSource Disease Mechanic {#sec:foodsource-disease}

FoodSource tiles can be gathered raw via the GATHER action and consumed directly via EAT, but raw food carries a disease risk. Agents who eat raw `raw_food` have a per-tick probability of contracting a debility that increases their hunger decay rate and stress accumulation. The disease probability scales with the quantity of raw food consumed:

$$P(\text{disease}) = 1 - (1 - p_d)^{q}$$

where $p_d$ is the per-unit disease probability and $q$ is the quantity consumed. This mechanic creates a real economic incentive to build and operate FoodMachines: processed food from machines is disease-free and provides better nutrition per unit (efficiency factor $\eta_{\text{processed}} = 1.0$ vs. $\eta_{\text{raw}} = 0.7$). Agents may still subsist on raw food---the subsistence pathway is never closed---but the combined penalty of disease risk and reduced efficiency makes industrial food processing the rational long-term strategy.

### Conveyor Traversability

A critical design decision governs agent-conveyor interaction. Conveyor tiles have two traversability states:

- **Built conveyor**: **not traversable**. An active conveyor belt occupies the full surface of its tile. Agents interact with conveyors from adjacent tiles: depositing material, retrieving material, or performing maintenance. This prevents agents from standing on active belts.
- **Unbuilt conveyor frame**: **traversable**. Before construction completes, a conveyor frame is a walkable marker on the ground. Agents can walk across these markers and build them in place, standing directly on the frame during construction. This prevents the central conveyor line from bisecting the map before construction begins.

This dual-state traversability ensures that the conveyor layout does not create impassable barriers during the construction phase, while still enforcing the physical constraint that agents cannot occupy an active conveyor belt.

### EatingZone Constraint

The factory supports at most one built EatingZone. This constraint is enforced at the BUILD action level: if `built_eatingzone_count() >= 1`, no additional EatingZone may be constructed. The single-EZ constraint forces agents to share a communal eating space, creating a social bottleneck where agents must coexist in proximity during meals. This is a deliberate design choice to amplify social interaction frequency and the stress contagion effects described in Section @sec:social-fabric.

## Tile Data Model {#sec:tile-data}

Each tile maintains a mutable data record (the `TileData` struct) whose fields depend on the tile type. The complete field set is organized by category:

### Resource Source Fields

| Field | Type | Applies to | Description |
|:------|:-----|:-----------|:------------|
| `resource_amount` | float | FoodSource, ScrapPile | Current quantity available |
| `resource_max` | float | FoodSource, ScrapPile | Maximum capacity |
| `resource_regen` | float | FoodSource, ScrapPile | Regeneration rate per tick |

For source tiles, regeneration follows:

$$r(t+1) = \min\!\bigl(r_{\max},\; r(t) + \rho \bigr)$$

where $r(t)$ is `resource_amount` at tick $t$, $r_{\max}$ is `resource_max`, and $\rho$ is `resource_regen`. FoodSource tiles regenerate at a moderate rate, supporting sustained foraging. ScrapPile tiles regenerate slowly (or negligibly), making them effectively finite and creating scarcity pressure.

### Machine and Construction Fields

| Field | Type | Applies to | Description |
|:------|:-----|:-----------|:------------|
| `built` | bool | Machine, EatingZone | Construction complete |
| `build_progress` | float | Machine, EZ, Conveyor | Accumulated build effort |
| `build_cost` | float | Machine, EZ, Conveyor | Total effort required |
| `machine_type` | MachineType | Machine | Subtype: Food, Materials, or Output |

### Storage Fields

| Field | Type | Applies to | Description |
|:------|:-----|:-----------|:------------|
| `stored_food` | float | Storage | Processed food quantity |
| `stored_raw_food` | float | Storage | Raw food quantity |
| `stored_raw_material` | float | Storage | Raw material quantity |
| `storage_capacity` | float | Storage | Maximum total units |

### Conveyor Fields

| Field | Type | Applies to | Description |
|:------|:-----|:-----------|:------------|
| `conveyor_dir` | ConveyorDir {N,S,E,W} | Conveyor | Material flow direction |
| `conveyor_condition` | float [0, 1] | Conveyor | Structural integrity; 0 = broken |
| `conveyor_contents` | float $\geq$ 0 | Conveyor | Material on this belt segment |
| `conveyor_content_type` | ResourceType/null | Conveyor | Type of material on belt |

### Dismantle Tracking Fields

| Field | Type | Applies to | Description |
|:------|:-----|:-----------|:------------|
| `dismantled_by` | int (agent ID / -1) | Floor (post-dismantle) | Who dismantled this conveyor |
| `dismantled_at_tick` | int (tick / -1) | Floor (post-dismantle) | When the dismantle occurred |
| `original_type` | TileType | Floor (post-dismantle) | Type before dismantle (always Conveyor) |

These tracking fields persist on Floor tiles after a conveyor is dismantled. They enable the social penalty system (Section @sec:social-fabric): if the tile remains Floor (no rebuild) for more than `dismantle_rebuild_window` ticks, the dismantling agent suffers trust decay with nearby witnesses.

Tile properties are updated by the EnvironmentSystem each tick (Section @sec:tick-loop).

## Resource Sources and the Production Chain {#sec:production-chain}

The production model provides three interconnected resource pathways, anchored by the three machine subtypes described in Section @sec:machine-subtypes.

### The Subsistence Pathway

FoodSource tiles provide wild food that agents can gather directly:

1. FoodSource $\xrightarrow{\text{GATHER}}$ raw_food
2. raw_food $\xrightarrow{\text{EAT}}$ subsistence **(disease risk)**

This is the emergency pathway: any agent adjacent to or standing on a FoodSource tile may execute the GATHER action to extract `raw_food`, which can be consumed directly via EAT. This requires no infrastructure and no coordination. However, raw food carries the disease mechanic described in Section @sec:foodsource-disease: agents eating raw food risk a debility that compounds their hunger and stress problems. The subsistence pathway is always available but is never the optimal long-term strategy.

### The Industrial Food Pathway

The FoodMachine transforms raw food into safe, efficient nutrition:

1. FoodSource $\xrightarrow{\text{GATHER}}$ raw_food
2. raw_food $\xrightarrow{\text{FoodMachine (WORK)}}$ processed_food
3. processed_food $\xrightarrow{\text{store}}$ Storage
4. Storage $\xrightarrow{\text{GET\_FOOD + EAT}}$ efficient nutrition

The FoodMachine operates at an intentionally inefficient conversion rate: output per tick is less than the FoodSource regeneration rate. This design choice creates progressive depletion pressure---at high tick counts, FoodSource tiles cannot sustain the extraction rate, forcing the population to either ration food or face scarcity. The ineficiency is not a bug but a tuning parameter: future improvements (machine upgrades, Director interventions) can increase efficiency, giving the Director a lever to modulate food pressure.

### The Materials and Output Pathway

The MaterialsMachine and OutputMachine form the factory's survival chain:

1. ScrapPile $\xrightarrow{\text{GATHER}}$ raw_material
2. raw_material $\xrightarrow{\text{MaterialsMachine (WORK)}}$ construction_material + scrap byproduct
3. construction_material $\xrightarrow{\text{OutputMachine (WORK)}}$ $h_{\text{factory}}$ restoration

ScrapPile tiles provide the initial `raw_material` that feeds this chain. ScrapPiles are effectively finite (negligible regeneration), creating a hard resource budget: the factory must build a MaterialsMachine before scrap runs out, or it loses the ability to produce construction material entirely. The MaterialsMachine partially alleviates this by generating a recyclable scrap byproduct, which feeds back into ScrapPile tiles at a reduced rate---a recycling loop that extends but does not eliminate material scarcity.

The OutputMachine is the factory's existential anchor. It produces no tangible good for agents; instead, it restores factory health ($h_{\text{factory}}$), which is the Director's primary metric (Section @sec:director). When factory health reaches zero, the factory is crumbling---and agents inside a crumbling factory should die. This creates a collective survival imperative: agents may not understand factory health as a concept, but their utility functions are calibrated so that WORK on an OutputMachine becomes highly valued when `factory_health` is low. The factory enforces its own survival through the agents' utility computation.

### Conveyor Logistics

The conveyor infrastructure (Section @sec:pipelines) extends the production chain by automating material transport between fixed nodes. The complete logistics flow is:

1. ScrapPile $\xrightarrow{\text{GATHER}}$ Agent inventory
2. Agent $\xrightarrow{\text{BUILD}}$ Conveyor segments
3. Conveyor $\to$ Conveyor $\to$ Machine
4. Machine $\xrightarrow{\text{WORK}}$ output
5. Output $\xrightarrow{\text{Conveyor}}$ Storage
6. Storage $\xrightarrow{\text{EAT}}$ Agent

Conveyors do not replace agents; they augment the production chain by allowing material to flow continuously between production nodes while agents attend to higher-level tasks. The conveyor network is built organically by agents through the BUILD action, following adjacency-based construction rules that ensure connected paths. The Director can influence network topology by placing machines, storage, and exits in configurations that suggest efficient conveyor routes.

### Conveyor Dismantle and Rebuild

Agents may dismantle built conveyor segments through the DISMANTLE action when a conveyor is identified as:

1. **A dead end**: the conveyor's flow target is not a useful tile (not Storage, Exit, or another built conveyor).
2. **A path blocker**: the conveyor blocks a critical agent movement corridor (e.g., a built conveyor segment that separates the factory into unreachable zones).

The dismantle action converts the conveyor tile back to Floor and refunds a fraction of the construction material to the dismantling agent's inventory:

$$\text{refund} = \text{build\_cost} \times \text{dismantle\_return}$$

where `dismantle_return` is a configuration parameter (default 0.5, i.e., 50% refund). The remaining material is lost, representing the real cost of deconstruction.

**Social penalty**: Dismantling a conveyor without rebuilding (or having another agent rebuild) within `dismantle_rebuild_window` ticks triggers a social penalty. Nearby agents (Manhattan distance $\leq 6$) lose trust in the dismantler via the `negative_interaction` function, and witnesses receive a small stress increase. This penalty models the social cost of creating disorder: a torn-up conveyor belt is visual evidence of wasted collective effort, and agents who observe it resent the responsible party.

The dismantle-rebuild cycle enables adaptive logistics: agents can rearrange conveyor paths when they detect inefficiencies, but they bear a social risk if the rearrangement is not completed. High-compliance, calm agents are more likely to attempt dismantle-rebuild operations, while stressed or hungry agents are gated out by the utility function's hunger and mood requirements.

### Dual-Pathway Tension and the Three-Layer Economy

The dual-pathway design (subsistence vs. industry) of food production, combined with the three-tier machine dependency (Food → Materials → Output), creates a multi-layered economy:

\footnotesize

| Layer | Resource | Pressure | Failure mode |
|:------|:---------|:----------|:-------------|
| Survival | Food | Hunger decay → starvation | Agents die individually |
| Industrial | Construction material | Scrap depletion → no new infrastructure | Factory cannot expand or repair |
| Existential | Factory health | Health decay → factory collapse | Agents die collectively |

\normalsize

Each layer depends on the one below. The factory cannot maintain health without construction material; construction material requires the MaterialsMachine; the MaterialsMachine requires agents who are fed. This dependency chain means that a failure at any layer cascades upward: food scarcity reduces the number of agents available for factory work, which reduces material output, which prevents health restoration, which ultimately collapses the factory.

The design principle is that **the factory demands production, but agents have internal drives that conflict with production**. Every agent wants to work as little as possible (the laziness trait). Every agent has non-productive drives (expression, social, curiosity). The factory's three-layer economy creates the structural pressure that makes these individual decisions consequential: an agent choosing to paint instead of operate the OutputMachine is not merely lazy---they are accelerating the factory's collapse.

### External Sources (Architectural Provision)

Entrance and Exit tiles define a boundary pathway for external resource exchange:

- **Entrance tiles** are designated to receive resource shipments from outside the factory, providing an inflow channel for raw materials, food, medicine, and other goods.
- **Exit tiles** are designated to ship finished goods out of the factory.

This pathway is architecturally supported---the tile types, boundary placement, and system hooks exist in the codebase---but is not yet active in the current simulation. When activated, the external pathway will introduce a Director-driven quota system (Section @sec:director): meeting the production quota sustains inflow rates, while sustained underproduction triggers supply chain contraction. This will add a collective survival pressure layer on top of the internal resource economy described above.

## Spatial Layout and Emergent Organization

The fixed $60 \times 40$ layout is designed to produce spatial asymmetries that drive emergent behavior:

- FoodSource clusters near corners and midpoints create multiple foraging territories, encouraging territoriality and the spatial segregation patterns predicted by Schelling's model.
- ScrapPile scatter ensures that raw material acquisition requires movement across the map, increasing the distance penalty for hauling tasks and favoring agents who establish efficient routes.
- The 16 Machine tiles (distributed across Food, Materials, and Output subtypes), initially unbuilt, present a collective action problem: no single agent can build all machines, but an unbuilt machine benefits no one. This mirrors the public goods dilemmas studied by Nowak and May on spatial lattices.
- Storage tiles distributed near machine clusters reduce hauling distance for the industrial pathway, creating locational value that agents may compete to control.
- The conveyor layout is procedurally generated as a central horizontal line that agents must build and maintain. The line divides the map into north and south zones, creating a spatial tension: conveyors must be built to enable logistics, but poorly placed conveyors block agent movement and may need to be dismantled and rebuilt.

The distance between resource sources, machines, and storage affects task utility via the distance penalty (Eq. @eq:task-distance), which creates natural industrial organization: agents prefer nearby tasks, so clusters of complementary tiles receive more consistent service than isolated ones.
