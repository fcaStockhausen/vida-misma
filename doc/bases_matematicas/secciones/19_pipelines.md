# Production Pipelines: Conveyor Infrastructure and Physical Logistics {#sec:pipelines}

The production chain described in Section @sec:factory operates through direct agent transportation: an agent gathers raw material, carries it in personal inventory, walks to a machine, deposits it, and then walks the processed output to storage. This model is sufficient for small-scale production but introduces a scaling bottleneck---every unit of material requires an agent's full attention for the duration of transport, and no material moves without an agent explicitly assigned to carry it.

This section introduces the conveyor infrastructure layer: a system of directional transport tiles that move resources autonomously along fixed paths, independent of agent intervention. Conveyors do not replace agents; they augment the production chain by allowing material to flow continuously between production nodes while agents attend to higher-level tasks such as gathering, construction, and maintenance. The design follows the spatial logistics principles discussed in Section @sec:agentes, where path cost influences agent behavior, and extends the tile taxonomy established in Section @sec:factory with a new traversable but directional tile type.

## The Conveyor Tile Type

The conveyor is a new tile type added to the `TileType` enumeration:

```
TileType::Conveyor
```

Each conveyor tile maintains directional and state data within its `TileData` record. The additional fields are:

| Field | Type | Description |
|---|---|---|
| `direction` | `ConveyorDir` {N, S, E, W} | Direction of material flow |
| `condition` | float [0, 1] | Structural integrity; 0 = broken, blocks flow |
| `contents_type` | `ResourceType` or null | Type of material currently on the belt segment |
| `contents_amount` | float >= 0 | Quantity of material sitting on this segment |
| `build_progress` | float | Accumulated build effort toward `build_cost` |
| `build_cost` | float | Total build effort required to construct this segment |

### Traversability

A critical design decision governs agent-conveyor interaction: **conveyor tiles are not traversable by agents**. Agents interact with conveyors from adjacent tiles. This choice reflects the physical intuition that a moving conveyor belt occupies the full surface of its tile---an agent cannot stand on an active belt and simultaneously perform other actions. The interaction model is:

- To deposit material onto a conveyor: agent stands on an adjacent tile and executes a DEPOSIT action toward the conveyor.
- To retrieve material from a conveyor: agent stands on an adjacent tile and executes a RETRIEVE action toward the conveyor.
- To repair a degraded conveyor: agent stands on an adjacent tile and executes the MAINTAIN action.

This adjacency-based interaction model is consistent with the action-targeting system described in Section @sec:inhabitants, where agents select a target tile and move to a valid adjacent position before executing their action.

### Material Transport Mechanics

Each simulation tick, the conveyor transport system processes all conveyor tiles in downstream order (starting from segments nearest the Exit and working backward toward sources). This processing order prevents double-moving: a segment pushes its contents before the segment behind it attempts to push, so no resource is advanced more than one tile per tick.

For each conveyor tile $c$ with direction $d$ and contents amount $a_c$, the transport rule is:

$$a_c(t+1) = \max\!\bigl(0,\; a_c(t) - \min(a_c(t),\; \tau,\; a_{n})\bigr)$$

where $\tau$ is the throughput parameter (`conveyor_throughput`, in resource units per tick) and $a_n$ is the remaining capacity of the neighbor tile in direction $d$. The neighbor receives:

$$a_n(t+1) = a_n(t) + \min\!\bigl(a_c(t),\; \tau,\; \text{cap}_n - a_n(t)\bigr)$$

The behavior depends on the neighbor tile type:

- **Conveyor**: contents transfer if the neighbor has capacity; otherwise the belt backs up.
- **Storage**: contents deposit into the storage tile's inventory, subject to storage capacity.
- **Machine**: contents feed as input material for the machine's production cycle.
- **Exit**: contents ship out, counting toward the factory's production quota (Section @sec:director).
- **Any other tile type or blocked path**: contents remain stationary; the conveyor segment is effectively stalled.

### Degradation and Maintenance

Conveyor condition degrades linearly over time:

$$\text{condition}(t+1) = \text{condition}(t) - \delta_c$$

where $\delta_c$ is the `conveyor_decay_rate` parameter. When $\text{condition} < 0.2$, the conveyor is considered broken and ceases to transport material. This threshold is chosen to provide a grace period: agents observe gradually yellowing conveyor status before total failure, allowing proactive maintenance rather than abrupt collapse.

The MAINTAIN action restores condition at rate $\mu$ per tick:

$$\text{condition}(t+1) = \min\!\bigl(1.0,\; \text{condition}(t) + \mu\bigr)$$

Maintenance is costless in material terms but carries opportunity cost: the agent performing maintenance is not gathering, building, or working. The utility of the MAINTAIN action scales with:

$$U_{\text{maintain}} = \alpha_{\text{compliance}} \cdot I_c \cdot (1 - \text{condition})$$

where $\alpha_{\text{compliance}}$ is the agent's compliance personality trait (Section @sec:inhabitants), $I_c$ is the conveyor's importance weight, and $(1 - \text{condition})$ captures urgency. This formulation ensures that high-compliance agents prioritize infrastructure upkeep, while low-compliance agents are less responsive to degradation---a behavioral divergence that creates the possibility of infrastructure neglect as a collective action failure.

### Conveyor Importance

The importance weight $I_c$ classifies conveyors by their role in the production network:

- **High importance** ($I_c = 1.0$): conveyors connected directly to Exit tiles. These are the factory's output arteries; failure here halts quota fulfillment.
- **Medium importance** ($I_c = 0.6$): conveyors connected to built machines with active production. These sustain the production line.
- **Low importance** ($I_c = 0.2$): isolated conveyors or conveyors connected only to unbuilt infrastructure.

This tiered importance system means that a broken conveyor near the Exit generates higher maintenance urgency than one feeding an unbuilt machine, directing agent attention toward the most consequential failures.

## The MAINTAIN Action

The MAINTAIN action extends the action taxonomy defined in Section @sec:inhabitants:

```
ActionType::MAINTAIN
```

An agent may execute MAINTAIN when standing adjacent to a conveyor tile with $\text{condition} < 1.0$. The action restores condition at rate $\mu$ per tick (the `maintain_rate` configuration parameter). The action has no material cost but requires the agent to remain adjacent to the conveyor for the duration of repair. This time investment creates a natural trade-off: maintaining infrastructure is never the highest-utility action for a starving agent, but becomes compelling when the agent's basic needs are satisfied and nearby conveyors are degraded.

The utility function for MAINTAIN integrates with the existing utility evaluation pipeline (Section @sec:inhabitants) and competes with all other candidate actions. An agent with high compliance and satisfied basic needs will tend toward maintenance behavior; an agent with low compliance or pressing hunger will ignore degraded conveyors entirely.

## Complete Production Flow with Conveyors

The conveyor infrastructure extends the production chain from Section @sec:factory into a multi-stage logistics pipeline:

The key architectural difference from the agent-carries-everything model is that material transport between fixed nodes (machine to storage, storage to exit) is now handled by the conveyor system rather than by individual agents. The complete logistics flow is:

1. ScrapPile $\xrightarrow{\text{GATHER}}$ Agent inventory
2. Agent $\xrightarrow{\text{DEPOSIT}}$ Conveyor entry point
3. Conveyor $\to$ Conveyor $\to$ Machine
4. Machine $\xrightarrow{\text{WORK}}$ processed output
5. Output $\xrightarrow{\text{Conveyor}}$ Storage / Exit
6. Storage $\xrightarrow{\text{EAT}}$ Agent

Agents remain responsible for:

1. **Gathering** raw materials from source tiles (ScrapPile, FoodSource).
2. **Depositing** gathered materials onto conveyor entry points.
3. **Building** new conveyor segments to extend the logistics network.
4. **Maintaining** degraded conveyors to prevent supply chain disruption.
5. **Operating** machines (WORK action) to transform raw inputs into outputs.

This division of labor creates a richer action space for agents and a more interesting optimization problem for the Director (Section @sec:director), who must now design conveyor layouts that minimize transport distance while remaining robust to degradation and agent neglect.

## Conveyor Construction

Conveyors are built by agents through the BUILD action. The construction process follows the same build-progress model used for machines (Section @sec:factory):

1. The agent stands on a Floor tile adjacent to an existing Conveyor, Machine, Storage, or Exit tile.
2. The agent executes BUILD, specifying the direction toward the adjacent infrastructure tile.
3. Construction costs `conveyor_build_cost` units of `raw_material`.
4. The build completes when `build_progress` reaches `build_cost`, at which point the Floor tile is replaced with a Conveyor tile whose direction is automatically set to point toward the adjacent infrastructure.

This adjacency-based construction rule ensures that conveyors form connected paths rather than isolated segments. A conveyor network grows organically from existing infrastructure, requiring agents to extend it segment by segment. The Director can influence network topology by placing machines, storage, and exits in configurations that suggest efficient conveyor routes, but the actual construction depends on agent labor and the utility system's willingness to assign agents to building tasks.

## Configuration Parameters

The conveyor system introduces the following configurable parameters:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `conveyor_build_cost` | float | 1.5 | Raw material cost to construct one conveyor segment |
| `conveyor_decay_rate` | float | 0.0005 | Condition loss per tick ($\delta_c$) |
| `conveyor_throughput` | float | 0.5 | Maximum resource units moved per tick ($\tau$) |
| `maintain_rate` | float | 0.02 | Condition restored per MAINTAIN tick ($\mu$) |
| `conveyor_break_threshold` | float | 0.2 | Condition below which transport halts |

These values are tuned for a grid of 2400 tiles with 10--30 agents. The decay rate means that a fully maintained conveyor degrades to the break threshold in approximately $0.8 / 0.0005 = 1600$ ticks without maintenance, providing a generous window for agents to respond. The throughput of 0.5 units per tick means a conveyor chain of length 10 takes 20 ticks to fully transport a single unit of material from end to end---slow enough that conveyor layout efficiency matters, but fast enough to be observable within a typical simulation run.

## TUI Rendering

Conveyor tiles are rendered with directional glyphs indicating flow direction:

| Glyph | Direction |
|:-----:|:----------|
| `>` | East |
| `<` | West |
| `v` | South |
| `^` | North |

Color encodes condition state: green for condition > 0.7, yellow for 0.3--0.7, and red for < 0.3. Material contents appear as a colored dot overlaid on the directional glyph. This visual encoding allows the Director to assess conveyor network health and material flow at a glance, consistent with the diagnostic rendering principles described in Section @sec:tick-loop.

## System Integration

The conveyor transport system is integrated into the tick loop as an eighth processing stage, inserted between `system_execute_actions` and `system_decay_needs`:

```
1. system_regen_resources
2. system_compute_utility
3. system_find_targets
4. system_execute_actions
5. system_conveyor_transport    ← NEW
6. system_decay_needs
7. system_update_stress
8. system_check_deaths
```

This placement ensures that conveyor transport occurs after agents have deposited materials and executed actions (potentially building new conveyor segments or depositing materials onto existing ones), and before needs decay evaluates the consequences of the tick. Material arriving at Storage via conveyor is available for the EAT action in the same tick's action execution phase (which has already completed), so agents will perceive the updated storage levels when computing utility in the next tick.

The downstream processing order---starting from conveyors nearest Exit tiles and working backward---prevents any resource from being advanced more than one tile per tick, maintaining the simulation's one-tile-per-tick movement invariant that also governs agent movement (Section @sec:tick-loop).

## Design Implications

The conveyor system introduces three emergent phenomena that the simulation must support:

**Infrastructure as collective good.** Conveyors benefit all agents who use the logistics chain, but their construction and maintenance depend on individual agent utility decisions. This creates a free-rider problem: an agent with low compliance may never volunteer for maintenance, relying on others to sustain the network. The compliance-weighted maintenance utility (Eq. above) ensures that this behavioral divergence is personality-driven rather than random, making infrastructure health a visible expression of the population's aggregate personality profile.

**Single points of failure.** A linear conveyor chain has no redundancy: if any segment degrades below the break threshold, the entire chain downstream stalls. This creates pressure for the Director to design branched or looped conveyor networks, and for agents to maintain critical segments. The importance-weighted utility system biases maintenance toward high-importance segments, but does not guarantee it---a sufficiently stressed or non-compliant population may neglect even critical infrastructure.

**Spatial logistics as Director lever.** With conveyors, the Director's infrastructure decisions acquire a logistics dimension that did not exist in the agent-carries-everything model. Placing a machine far from its material source now has consequences beyond agent travel time: the conveyor chain required to bridge the distance is itself infrastructure that must be built and maintained. This adds strategic depth to the Director's environmental design role (Section @sec:director) and creates more observable consequences for infrastructure decisions.
