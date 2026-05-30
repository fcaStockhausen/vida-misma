# Adaptive Logistics: Dismantle, Rebuild, and Self-Organizing Infrastructure {#sec:adaptive-logistics}

The conveyor infrastructure described in Section @sec:pipelines provides a fixed topology: belts placed during factory setup that agents build and maintain. However, a fixed topology creates rigidity. As the factory evolves---agents discover better machine placements, storage fills at different rates, or population shifts create new demand patterns---the conveyor network may become suboptimal or actively obstructive.

This section introduces the **adaptive logistics** system: the capacity for agents to dismantle and rebuild conveyor segments, reorganizing the factory's physical infrastructure in response to observed inefficiencies. This system creates a feedback loop between spatial layout (Section @sec:factory) and agent decision-making (Section @sec:inhabitants): the factory shapes agent behavior, but agents can reshape the factory.

## The Dismantle Action {#sec:dismantle-action}

The DISMANTLE action type extends the agent's action repertoire (Section @sec:inhabitants) to include infrastructure deconstruction. An agent may dismantle a built conveyor segment when standing on an adjacent tile---the same adjacency rule that governs BUILD and MAINTAIN interactions with conveyors.

### Dismantle Conditions

Not every conveyor is a candidate for dismantling. An agent evaluates a conveyor for removal only when it falls into one of two categories:

1. **Dead end**: The conveyor's flow direction points to a tile that is not useful---not a Storage tile, not an Exit tile, and not another built conveyor. Dead-end conveyors consume maintenance resources without transporting material anywhere productive.

2. **Path blocker**: The conveyor obstructs a critical agent movement corridor. A built conveyor is not traversable (Section @sec:tile-types), so a line of built conveyors can bisect the factory floor, preventing agents from reaching tiles on the other side. Agents detect blocking conveyors by checking whether the conveyor tile prevents north-south or east-west passage between walkable areas.

These conditions are checked in the target selection phase (Section @sec:inhabitants) before an agent commits to the DISMANTLE action.

### Dismantle Mechanics

When executed, the DISMANTLE action:

1. **Converts the conveyor tile back to Floor.** The built conveyor is removed, and the tile becomes walkable terrain. The tile retains metadata tracking the dismantle event (see below).

2. **Refunds a fraction of the construction material.** The dismantling agent receives a material refund:

$$\text{refund} = \text{build\_cost} \times \text{dismantle\_return}$$

where `dismantle_return` is a configuration parameter (default $\alpha = 0.5$, i.e., 50% refund). The remaining material is permanently lost, representing the real cost of deconstruction: torn belts, broken connectors, wasted effort.

3. **Records tracking metadata** on the resulting Floor tile:

| Field | Value |
|:------|:------|
| `dismantled_by` | Agent ID of the dismantler |
| `dismantled_at_tick` | Current tick $t$ |
| `original_type` | `TileType::Conveyor` |

These fields persist until the tile is rebuilt or overwritten.

## The Rebuild Cycle {#sec:rebuild-cycle}

Dismantling is only useful as part of a rebuild cycle. The agent who dismantles a conveyor is expected to rebuild a conveyor elsewhere---ideally in a position that creates a more efficient logistics path. The rebuild uses the standard BUILD action on an unbuilt conveyor frame at the new location.

The rebuild cycle is:

1. **Detect** an inefficient or blocking conveyor segment.
2. **Dismantle** the segment (DISMANTLE action, $\alpha$ material refund).
3. **Navigate** to a better position for the new segment.
4. **Build** a new conveyor frame at the improved location (BUILD action, consumes `build_cost` material).

The net material cost of one dismantle-rebuild cycle is:

$$\Delta m = \text{build\_cost} \times (1 - \alpha)$$

For the default parameters ($\text{build\_cost} = 3.0$, $\alpha = 0.5$), each cycle costs $\Delta m = 1.5$ units of construction material. This cost ensures that adaptive reorganization is not free: agents must weigh the material cost of rearrangement against the efficiency gains of a better conveyor path.

## Social Penalty: The Trust Cost of Disorder {#sec:dismantle-penalty}

Dismantling a conveyor is a visible act of infrastructure disruption. Other agents who observe a torn-up conveyor belt---a Floor tile where a functioning conveyor used to be---interpret it as evidence of wasted collective effort. This interpretation is modeled through the social penalty system.

### Penalty Trigger

When a Floor tile retains dismantle tracking metadata (Section @sec:dismantle-action) and no rebuild has occurred on that tile within a configurable window of $w = 200$ ticks (`dismantle_rebuild_window`), the social penalty activates:

1. **Trust decay**: Every agent within Manhattan distance $d \leq 6$ of the abandoned tile loses trust in the dismantling agent via the `negative_interaction` function (Section @sec:social-fabric). The trust penalty magnitude scales with the observer's proximity to the abandoned site.

2. **Stress contagion**: Witnesses receive a small stress increase ($+0.003$ per tick of exposure), modeling the ambient anxiety produced by visible disorder in the workspace.

### Penalty Clearance

The penalty clears when any agent rebuilds a conveyor (or any structure) on the abandoned tile. The tracking metadata is overwritten, and the social system no longer penalizes the original dismantler. Crucially, **the rebuild need not be performed by the same agent who dismantled**---any agent who fills the gap clears the social debt. This design encourages collaborative reconstruction: an agent who sees an abandoned dismantle site has an incentive to rebuild it, not only to restore logistics efficiency but also to help the dismantler's social standing.

### Calibration

The penalty window ($w = 200$ ticks) is calibrated to be generous enough that agents performing legitimate dismantle-rebuild cycles are not penalized during normal operation (navigation + build time is typically 20--50 ticks), but short enough that genuinely abandoned dismantle sites produce social consequences within a reasonable timeframe. The penalty does not accumulate indefinitely: after $w + 50$ ticks, the tracking metadata is considered stale and the penalty ceases to grow.

## Utility Computation for DISMANTLE {#sec:dismantle-utility}

The DISMANTLE action competes with all other actions in the agent's utility computation (Section @sec:inhabitants). Its utility is:

$$U(\text{DSMNTL}) = u_{\text{base}} \times c_{\text{compliance}} \times g_{\text{calm}} \times g_{\text{hunger}} \times (1 - p_{\text{laziness}}) \times b_{\text{blocker}}$$

where:

| Factor | Formula | Purpose |
|:-------|:--------|:--------|
| $u_{\text{base}}$ | $0.15$ | Base utility, intentionally moderate |
| $c_{\text{compliance}}$ | `personality.compliance` | Only obedient agents consider dismantling |
| $g_{\text{calm}}$ | $\max(0,\; 2.0 \cdot \text{mood} - 0.5)$ | Stressed agents don't dismantle (gate) |
| $g_{\text{hunger}}$ | $3.0$ if hunger < 0.3, else $1.0$ | Hungry agents are not gated out but hungry trumps |
| $p_{\text{laziness}}$ | `personality.laziness` | Lazy agents avoid the effort |
| $b_{\text{blocker}}$ | $2.0$ if blocking, $1.0$ if dead-end | Prioritize removing blockers |

The calm gate ($g_{\text{calm}}$) requires `mood > 0.25` for the utility to be non-zero. This ensures that only agents in a reasonably calm emotional state attempt infrastructure rearrangement---stressed agents are not trusted with deconstruction tools.

The hunger gate is inverted from the MAINTAIN action's hunger gate (Section @sec:production-chain): MAINTAIN is suppressed when agents are hungry (to avoid starvation from over-maintaining), but DISMANTLE is boosted when agents are hungry. The rationale is that a hungry agent in a blocked factory has the strongest incentive to rearrange logistics---the current layout is failing them.

## Integration with Factory Systems

The adaptive logistics system integrates with the existing factory systems at three points:

### Movement and Pathfinding

When an agent dismantles a blocking conveyor, the tile becomes Floor (traversable), immediately opening the blocked corridor. Agents on both sides of the former barrier can now pathfind to tiles that were previously unreachable. The pathfinding system (Section @sec:pathfinding) automatically incorporates the new walkability state in the next A* computation---no manual update is required.

### Conveyor Transport

The conveyor transport system (Section @sec:pipelines) skips tiles that are not built conveyors. When a conveyor is dismantled mid-chain, the transport system treats the gap as a break in the line: material flowing toward the gap stops at the last conveyor before the break. This creates a local logistics disruption that persists until the gap is rebuilt or the chain is rerouted.

### Social Fabric

The trust penalty for abandoned dismantle sites creates a social incentive structure that mirrors real-world collective action dynamics. Agents who dismantle without rebuilding are perceived as unreliable by their peers---a social cost that must be weighed against the potential efficiency gain of the rearrangement. This dynamic connects the adaptive logistics system to the broader social simulation (Section @sec:social-fabric), where trust and reputation influence cooperation, resource sharing, and coalition formation.

## Design Philosophy: Controlled Disorder

The adaptive logistics system embodies a core tension in the factory's design: **the factory is more efficient when its infrastructure is well-organized, but the agents who maintain it have conflicting priorities**. An agent who dismantles a blocking conveyor does the factory a service by opening a corridor---but if they fail to rebuild, the service becomes a disruption.

This tension creates emergent narratives: an agent who repeatedly dismantles without rebuilding becomes socially isolated (low trust from peers). An agent who systematically identifies and fixes logistics bottlenecks becomes a valued community member. The factory's physical layout becomes a record of agent decisions---a material trace of collective intelligence or collective dysfunction.

The system also creates a natural role for the Director (Section @sec:director): by placing machines and storage in configurations that suggest efficient conveyor paths, the Director can reduce the frequency of dismantle-rebuild cycles, guiding agents toward layouts that require less adaptation. A well-designed factory layout produces less disorder; a poorly designed one produces constant rearrangement and the social friction that accompanies it.
