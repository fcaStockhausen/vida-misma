# Systems Architecture and Tick Loop {#sec:tick-loop}

## Update Order

Each simulation tick processes systems in dependency order. Systems are independent (ECS architecture, Section @sec:diseno): each system operates on a specific set of components and does not reference other systems.

```
1. DecaySystem:        reduce all needs for all agents
2. EventSystem:        generate external events (resource arrivals, quota checks)
3. UtilitySystem:      compute utilities for all agents; select highest-utility action
4. ActionSystem:       execute selected actions (move, work, eat, socialize, create)
5. ProductionSystem:   process factory production based on completed tasks
6. StressSystem:       update stress based on events; propagate through social graph
7. RelationshipSystem: update relationship weights based on interactions
8. EnvironmentSystem:  update tile properties (cleanliness, wear, temperature)
9. NarrativeSystem:    log significant events for the Director
```

The ordering ensures that:

- Needs are updated before utilities are computed (agents respond to current need levels).
- Actions are executed before production is computed (agents must complete tasks before output is registered).
- Stress is updated after actions (the stress consequences of actions are computed in the same tick).
- Relationships are updated after stress (stress events modify relationship weights).
- The environment decays after use (wear and cleanliness reflect the tick's activity).

## System Dependencies

```
DecaySystem  -->  UtilitySystem  -->  ActionSystem  -->  ProductionSystem
                       |                    |                  |
                       v                    v                  v
                 (selects action)    (modifies world)    (computes output)
                                            |
                                            v
                  NarrativeSystem  <--  StressSystem  <--  EventSystem
                       |                   |
                       v                   v
                 (logs events)      RelationshipSystem --> EnvironmentSystem
```

Each system processes all entities that possess the required components. An entity that lacks a component (e.g., a tile entity without a NeedsComponent) is simply skipped by systems that require that component.

## Implementation Roadmap for La Vida Misma {#sec:roadmap-vida}

The phased approach from Section @sec:stack, adapted for *La Vida Misma*:

| Phase | Systems implemented | Estimated LOC | Emergent behavior unlocked |
|---|---|---|---|
| 1. Factory Floor | DecaySystem, EnvironmentSystem, tile grid, resource flow | ~2,500 | Reproducible world with consistent geography |
| 2. The Workers | NeedsComponent, PersonalityComponent, UtilitySystem, ActionSystem | ~3,500 | Agents autonomously prioritize actions; personality variation visible |
| 3. Movement | PathfindingSystem (A* on 2D grid + cache + connected components) | ~1,500 | Agents navigate spatially; layout affects behavior |
| 4. Production | ProductionSystem, SkillsComponent, quota tracking | ~1,500 | Self-organizing economy; skill-based specialization |
| 5. Social Fabric | RelationshipsComponent, StressComponent, stress contagion | ~2,500 | Relationships, grief cascades, informal leadership |
| 6. Emergent Spaces | Tile preference computation, movement utility, Schelling dynamics | ~1,000 | Spontaneous zone formation within the factory |
| 7. Life and Death | Birth-death cycle, grief events, generational dynamics | ~1,000 | Generational turnover, social graph restructuring |

Total: ~13,500 LOC.

Phase 2 is the critical design gate: if the utility function (Eq. @eq:vida-utility) does not produce differentiated behavior across the personality spectrum, the entire simulation fails to produce interesting dynamics. Phase 2 should be tested extensively in isolation before proceeding.

## Console Output

The primary interface is console/terminal output. Each tick (or at a configurable display interval), the simulation renders the 2D grid as ASCII characters:

- Tile types represented by characters: `#` (wall), `.` (floor), `M` (machine), `S` (storage), `>` (entrance), `<` (exit).
- Agents represented by characters that reflect their current action: `W` (working), `E` (eating), `R` (resting), `A` (creating art), `S` (socializing), `?` (idle).
- Agent details accessible via a selection interface (click or type coordinates to inspect an agent's needs, stress, relationships, personality, skills).

This is sufficient for validating all emergent phenomena. Rendering infrastructure (sprites, tilesets, animation) is deliberately excluded from the initial scope.
