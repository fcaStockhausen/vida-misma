# Implementation Roadmap {#sec:stack}

This section provides a concrete, phased approach to building a community simulation engine. The roadmap is ordered by dependency: each phase produces a testable system, and each subsequent phase depends on the previous one while introducing emergent behavior that was not possible before.

## Component Summary

| Component | Algorithm | Key equations | Complexity per tick |
|---|---|---|---|
| Terrain | Perlin/Simplex + fBm | Eq. @eq:fbm | $O(M)$ at generation; $O(1)$ per query |
| Biomes | Multi-field thresholds | Classification rules | $O(M)$ at generation |
| Regions | Voronoi + Lloyd relaxation | Eq. @eq:voronoi | $O(n \log n)$ at generation |
| Rivers | Gradient descent on elevation | Optimization | $O(\text{river cells})$ at generation |
| Fluids | 3D cellular automaton | Eqs. @eq:fluid-gravity, @eq:fluid-spread | $O(F)$ where $F$ = fluid tiles |
| Pathfinding | A* + connected components + cache | Eq. @eq:astar | $O(b^d)$ per query; amortized $O(1)$ with cache |
| Agent AI | Utility theory + needs | Eqs. @eq:utility-argmax, @eq:need-utility | $O(|A| \cdot |a|)$ per agent |
| Personality | Facet vector + stress layers | Eq. @eq:stress | $O(|\text{events}|)$ |
| Storyteller | Temporal distribution | Eq. @eq:storyteller | $O(1)$ per evaluation |
| Social graph | Weighted edges + contagion | Eqs. @eq:relationship, @eq:stress-contagion | $O(V + E)$ |
| Spatial games | Grid-based game theory | [@nowak1992; @schelling1971] | $O(|\text{agents}|)$ |
| Structures | WFC or graph grammar | Eq. @eq:shannon-entropy | $O(|\text{tiles}| \cdot |\Sigma|)$ |
| Vegetation | L-systems | Rewrite grammar | $O(k^n)$ at generation |
| Heat | Diffusion on grid | Eq. @eq:heat-diffusion | $O(M)$ |
| Opinion dynamics | DeGroot / bounded confidence | Weighted averaging | $O(V + E)$ |

Note: $M$ = total map tiles, $F$ = fluid tiles, $|A|$ = number of agents, $|a|$ = number of actions per agent, $V$ = vertices, $E$ = edges in social graph, $\Sigma$ = tile state set.

## Phase 1: Terrain and Biome Generation

Implement Perlin/Simplex noise with multi-octave fBm (Eq. @eq:fbm). Generate an elevation map. Add temperature (function of latitude + elevation), rainfall (noise + orographic shadow), and drainage (separate fractal). Classify biomes via threshold intersection of all layers. Generate rivers via gradient descent on the elevation map. Delimit regions via relaxed Voronoi.

**Deliverable**: A reproducible world map with internally consistent geography. The biomes form at the intersection of independent layers, and the geography is deterministic given the random seed.

**Estimated scope**: ~2,000 LOC. Key data structures: 2D/3D arrays for terrain layers, noise function implementations, biome classification table.

**Primary risk**: Tuning the biome threshold values requires iterative experimentation. There is no analytical method for determining "correct" thresholds; the criterion is whether the resulting geography appears plausible when inspected visually.

## Phase 2: Agents, Needs, and Utility AI

Implement ECS architecture. Create agents with: Position component, Needs component (hunger, rest, social, safety), Personality component (facet vector), and a UtilityAI system that evaluates available actions. Actions include: move to location, eat, sleep, socialize, work. Implement need decay (linear or exponential) and urgency curves (Eq. @eq:need-utility with $\alpha = 1.5$ as a starting point).

**Deliverable**: Agents that autonomously prioritize actions based on their internal state, with personality variation producing different behavioral patterns across agents.

**Estimated scope**: ~3,000 LOC. Key data structures: ECS framework with component pools, utility evaluation loop.

**Primary risk**: Balancing utility weights across needs. If one need consistently dominates, all agents will exhibit identical behavioral patterns. The weight structure must allow for personality-driven differentiation.

## Phase 3: Pathfinding and Navigation

Implement A* with an appropriate heuristic for the terrain grid. Add connected components tagging via flood-fill (so impossible paths abort without search). Add LRU path cache. Add configurable tile costs (agents avoid difficult terrain unless necessary).

**Deliverable**: Agents navigate efficiently around obstacles. Path computation is fast enough for 50+ simultaneous agents requesting paths every few ticks.

**Estimated scope**: ~1,500 LOC. Key data structures: priority queue, component index for spatial queries, path cache.

**Primary risk**: Cache invalidation when the map changes (construction, destruction, flooding). An overly aggressive invalidation strategy negates the cache benefit; an overly conservative one produces stale paths.

## Phase 4: Physical Systems (Fluids, Heat, Collapse)

Implement 3D CA fluids (7-level system per Eqs. @eq:fluid-gravity, @eq:fluid-spread, plus pressure via flood-fill). Implement heat diffusion (Eq. @eq:heat-diffusion with per-material conductivity). Implement structural collapse (connected-to-surface check via flood-fill).

**Deliverable**: Water flows, heat spreads, unsupported structures collapse. These systems interact through the shared tile grid.

**Estimated scope**: ~2,000 LOC. Key data structures: 3D CA grid for fluids, material property tables, flood-fill queue.

**Primary risk**: Fluid pressure propagation is the most computationally expensive component. Naive flood-fill on every tick for every fluid source is $O(M)$; optimization requires identifying connected fluid bodies and updating them incrementally.

## Phase 5: Economy (Production Chains and Skills)

Implement a production graph: raw resources enter as inputs to workshops, which produce intermediate goods and final products. Agents gain XP for completed tasks (Eq. @eq:skill-xp). Skill level affects output quality via quality tier probabilities. Distance reduces task utility (Eq. @eq:task-distance), creating natural industrial organization.

**Deliverable**: A self-organizing economy where agents specialize through practice, workshops cluster around resource sources, and resource scarcity drives behavioral adaptation.

**Estimated scope**: ~1,500 LOC. Key data structures: production graph, skill tables, quality probability tables.

**Primary risk**: Balancing XP curves and quality thresholds so that skill progression feels meaningful but not grind-intensive. The mathematical model provides the framework, but the specific numerical values require playtesting.

## Phase 6: Society (Social Graph, Stress, Spatial Games)

Implement a relationship graph with affinity edges in $[-100, 100]$. Events modify edges (Eq. @eq:relationship). Stress accumulates (Eq. @eq:stress) with personality modifiers and three-layer memory. Stress contagion propagates through the relationship graph (Eq. @eq:stress-contagion). Implement spatial game-theoretic interactions: agents compete for resources based on spatial proximity, and cooperation clusters form naturally per the Nowak-May model [@nowak1992}.

**Deliverable**: Communities with internal politics, friendships, rivalries, and cascading stress events. Two agents who experience the same event react differently based on personality and relationship history.

**Estimated scope**: ~2,500 LOC. Key data structures: adjacency graph, event queue, stress accumulator.

**Primary risk**: Stress cascade tuning. If contagion is too strong, a single negative event collapses the entire community; if too weak, social dynamics are invisible.

## Phase 7: Meta-Agent and Opinion Dynamics

Implement a Storyteller meta-agent that calibrates external pressure (threats, events, resource scarcity) based on colony wealth and elapsed time (Eq. @eq:storyteller). Implement opinion dynamics (DeGroot or bounded confidence models) so cultural traits and beliefs evolve through agent interaction.

**Deliverable**: A simulation that generates narrative rhythm (calm periods followed by pressure) and evolving cultural landscapes.

**Estimated scope**: ~1,000 LOC. Key data structures: distribution sampler for event timing, opinion vectors per agent.

**Primary risk**: The Storyteller difficulty curve is entirely empirical. There is no theoretical basis for the function parameters; they must be calibrated to produce engaging pacing.

## Estimated Total Scope

| Phase | Approximate LOC | Cumulative |
|---|---|---|
| 1. Terrain + Biomes | 2,000 | 2,000 |
| 2. Agents + Needs + AI | 3,000 | 5,000 |
| 3. Pathfinding | 1,500 | 6,500 |
| 4. Physics | 2,000 | 8,500 |
| 5. Economy | 1,500 | 10,000 |
| 6. Society | 2,500 | 12,500 |
| 7. Storyteller + Opinions | 1,000 | 13,500 |

The total estimate of ~13,500 LOC is conservative compared to Dwarf Fortress (~700,000 LOC over 20 years). The difference reflects scope: this estimate covers a functional engine that demonstrates the core emergent phenomena described in this document, not the full content depth of a commercial game.

## Deliberately Excluded (Initial Scope)

- **Rendering / graphics**: Console/terminal output is sufficient for validating simulation behavior.
- **Networking / multiplayer**: Single-threaded, local simulation.
- **Save/load serialization**: Can be added as a serialization layer over the ECS state.
- **Machine learning**: The neural CA results (Section 1.7) are relevant to long-term research but premature for an initial implementation. Hand-crafted utility functions are sufficient.
- **Navier-Stokes fluid dynamics**: The 7-level CA approximation produces adequate behavioral fidelity for community simulation purposes at a fraction of the computational cost.
