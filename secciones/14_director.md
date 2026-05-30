# The Director: Player as Environment Architect {#sec:director}

The Director is the human player's sole interface to the simulation. It operates exclusively at the environmental level: the Director modifies the factory's physical infrastructure and production parameters, but never selects actions for individual agents. Agents determine their own behavior through the utility-based decision system described in Section @sec:inhabitants. This separation of concerns follows the influential design model established by *Dwarf Fortress* (Adams 2014), wherein the player constructs and configures the world, and its inhabitants respond autonomously according to their internal motivations.

The term "Director" is deliberately chosen to distinguish this role from both the *Dwarf Fortress* overseer and RimWorld's Storyteller algorithm. Unlike the Storyteller---which is a software system that procedurally calibrates external events such as raids and weather to maintain a target narrative tension (Sylvester 2013)---the Director is a human player whose interventions are not governed by any utility function. The Director may set impossible quotas, dismantle all food-producing machines, or wall agents into enclosed spaces. The simulation is expected to handle these cases as legitimate outcomes: agents starve, stress cascades through the social network (Section @sec:social-fabric), and the factory may collapse. This is correct emergent behavior, not a failure state.

## Director Capabilities

The Director's action space is restricted to environmental modifications. Table @tbl:director-actions summarizes the available operations, their effects on the simulation state, and the formal systems they engage.

| Action | Effect | Formal basis |
|---|---|---|
| Place/remove machines | Creates or removes production nodes in the factory graph | Production graph (Section @sec:factory) |
| Place/remove walls | Modifies traversability and the pathfinding graph | Grid-based A\* (Section @sec:pathfinding) |
| Set production quota | Determines target output rates for resources | Resource flow model (Section @sec:factory) |
| Place storage zones | Designates tiles as valid destinations for specific material types | No enforcement; agents evaluate storage tasks via utility |
| Designate spaces | Marks regions for purposes (workshop, common area, residential) | Spatial affordances (Section @sec:spaces) |
| Observe agents | Inspect agent state: needs, stress, relationships, skills | Diagnostic overlay |
| Read narrative log | Review chronologically ordered significant events | Narrative system (Section @sec:tick-loop) |

: Director actions and their simulation-level effects. {#tbl:director-actions}

## The Indirection Principle

The critical design constraint is that the Director cannot issue direct commands to any agent. This principle---call it the *indirection principle*---ensures that all agent behavior passes through the utility evaluation pipeline. If the Director requires more metalworkers, the correct intervention is not to select an agent and assign it to metalworking, but rather to construct a metalworking workshop, supply it with raw materials via strategically placed storage zones, and allow the utility system to recruit agents whose personality profiles and existing skills make metalworking their highest-utility option.

This indirection has two consequences. First, the Director's effectiveness depends on understanding how agents evaluate utility. A workshop placed far from residential areas or common spaces may go unused because travel costs reduce the utility of working there below competing actions. Second, the Director cannot guarantee outcomes. The factory's productivity emerges from the interaction between infrastructure configuration and the aggregate utility-maximizing behavior of a population whose members have heterogeneous needs, skills, and preferences.

## Interaction with the Production System

The Director shapes the production system (Section @sec:factory) through three primary mechanisms:

1. **Machine placement and removal.** Each machine added to the factory graph introduces a new production node capable of transforming input resources into output resources. Removing a machine eliminates that transformation capability. Agents discover available machines through the task-generation system, which evaluates the production graph each tick and creates work tasks for machines that have unmet input requirements or pending output quotas.

2. **Storage and logistics configuration.** By placing storage zones near input stockpiles or output destinations, the Director reduces the travel cost component of relevant utility scores, indirectly increasing the likelihood that agents will perform transportation tasks in that area. Agents do not respect storage designations as hard constraints; they evaluate whether moving materials to a designated zone yields higher utility than alternative actions.

3. **Quota setting.** Production quotas establish target output levels. The task-generation system translates quota deficits into work tasks with urgency-weighted utility scores. A quota for 50 units of processed metal, when current stock is 10, produces higher-utility tasks than a quota that is already satisfied. The Director thus influences agent priorities without selecting which agent performs which task.

## Observational Tools

Beyond environmental modification, the Director has access to two read-only information channels. The **agent inspection overlay** exposes individual agent state variables: current need levels, accumulated stress, social relationships, and skill proficiencies. The **narrative log** records significant events---births, deaths, relationship formations, production milestones, stress breakdowns---in chronological order, providing the Director with a coherent account of emergent factory life (Section @sec:tick-loop).

These observational tools are the Director's only window into the agents' internal states. The simulation does not expose utility calculations, personality parameter values, or the internal state of the need-satisfaction model directly. The Director must infer these from observable behavior and the narrative record.

## Current Implementation Status

The Director role described in this section is a design specification. No implementation exists in the current codebase. The production graph, task-generation system, and agent utility evaluation pipeline described in Sections @sec:factory and @sec:inhabitants are partially implemented, but the player-facing interface for performing Director actions---the construction overlay, quota management panel, agent inspector, and narrative log viewer---remains a future development objective.
