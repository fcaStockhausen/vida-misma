# The Director: External Agency {#sec:director}

The Director is the player's interface to the simulation. It operates at the environmental level, not the agent level. The Director defines the factory's production requirements and modifies its physical layout. The Director does not select actions for agents; agents decide their own behavior through the utility system (Section @sec:agentes).

This design choice follows the Dwarf Fortress model (Section @sec:diseno): the player builds the environment, and the agents respond autonomously. The emergent behavior arises from the interaction between the Director's infrastructure decisions and the agents' personality-driven utility maximization.

## Director Capabilities

| Action | Effect | Formal basis |
|---|---|---|
| Place/remove machines | Creates or removes production capacity | Production graph (Section @sec:social) |
| Place/remove walls | Modifies traversability and pathfinding graph | A* on grid (Section @sec:pathfinding) |
| Set production quota | Determines resource inflow rate | Resource flow model (Section @sec:factory) |
| Designate spaces | Marks tiles for specific purposes (storage, workshop, common area) | No enforcement; agents use spaces based on utility |
| Observe agents | Read agent state: needs, stress, relationships, skills | Diagnostic interface |
| Read narrative log | Sequence of significant events | NarrativeSystem (Section @sec:tick-loop) |

The critical constraint is that the Director cannot directly command an agent. If the Director needs more metalworkers, the solution is not to click an agent and assign it to metalworking. The solution is to build a metalworking workshop, ensure it is supplied with raw materials, and rely on the utility system to assign agents whose personality and skill profile make metalworking their highest-utility action.

## The Director as Meta-Agent

The Director's role is structurally similar to RimWorld's Storyteller (Section @sec:social), but operates at a different level of abstraction. The Storyteller calibrates *external events* (raids, weather) based on colony state. The Director calibrates *infrastructure and production requirements*. Both are meta-agents that observe the simulation state and inject changes that modify the conditions under which in-world agents operate.

The Director differs from the Storyteller in one important respect: the Director is a human player, not an algorithm. This means the Director's decisions are not governed by a utility function and are not constrained to produce "balanced" gameplay. A Director can set impossible quotas, remove all food sources, or wall agents into enclosed spaces. The simulation should handle these cases gracefully: agents will starve, stress will cascade, and the factory will fail. This is correct behavior, not a bug.
