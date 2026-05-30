# Pathfinding {#sec:pathfinding}

Agents must navigate the spatial environment to satisfy their needs. Pathfinding algorithms compute sequences of positions that connect an agent's current location to a goal while respecting the traversability constraints of the terrain. This section covers the standard approaches and their computational trade-offs.

## A*: Informed Search on a Graph

A* [@hart1968} is the standard pathfinding algorithm for grid-based worlds. It extends Dijkstra's algorithm with an admissible heuristic $h(n)$ that estimates the cost from node $n$ to the goal:

$$f(n) = g(n) + h(n)$$ {#eq:astar}

where $g(n)$ is the accumulated cost from the start node and $h(n)$ is the heuristic estimate. A* is guaranteed to find the optimal path when $h$ is admissible (never overestimates the true cost) and consistent ($h(n) \leq c(n, n') + h(n')$ for all edges $(n, n')$).

Common heuristics for grid worlds:

| Heuristic | Formula | Properties |
|---|---|---|
| Manhattan | $\|x_1 - x_2\| + \|y_1 - y_2\|$ | Admissible for 4-connected grids |
| Euclidean | $\sqrt{(x_1 - x_2)^2 + (y_1 - y_2)^2}$ | Admissible for any connectivity |
| Chebyshev | $\max(\|x_1 - x_2\|, \|y_1 - y_2\|)$ | Admissible for 8-connected grids |
| Octile | $\max(dx, dy) + (\sqrt{2} - 1) \min(dx, dy)$ | Admissible for 8-connected grids with uniform cost |

A* has worst-case complexity $O(b^d)$ where $b$ is the branching factor and $d$ is the solution depth. In practice, the heuristic prunes the search space significantly, but large maps or many simultaneous agents can create computational bottlenecks.

## Jump Point Search

Harabor and Grastien (2012) [@harabor2012} observed that on uniform-cost grids, many nodes expanded by A* are structurally irrelevant: they lie on straight corridors or open areas where the optimal path cannot deviate. Jump Point Search (JPS) exploits this by "jumping" over such nodes, expanding only *jump points*---nodes where the path is forced to turn.

JPS expands up to 100x fewer nodes than A* on uniform-cost grids while returning identical paths. The limitation is that it applies only when all traversable tiles have equal movement cost. For variable-cost terrain (swamps that slow movement, roads that accelerate it), A* with appropriate edge weights remains necessary.

## Connected Components and Path Caching

Two practical optimizations reduce the cost of pathfinding in community simulations:

**Connected components**: Precompute which tiles belong to the same connected region (via flood-fill on the traversability graph). When an agent requests a path, first check whether the start and goal share a component. If not, the path is impossible and the search can be aborted immediately. This avoids the cost of a full A* expansion for unreachable goals.

**Path caching**: Maintain an LRU cache of recently computed paths. When the map changes (a wall is constructed, a flood occurs), invalidate only the paths that traverse the affected region. Path caching exploits the temporal locality of agent behavior: agents in the same area tend to request similar routes.

These optimizations are not asymptotically interesting but are critical for simulation performance. A community of 50+ agents requesting paths every few ticks on a large map will spend a disproportionate fraction of compute time in pathfinding unless these optimizations are applied.

## Hierarchical Pathfinding

For very large maps, hierarchical approaches decompose the world into regions (e.g., rooms, districts) and compute paths in two phases: first at the region level (abstract graph), then within each region (concrete graph). This reduces the effective search space from the full map to a small number of intra-region searches. The trade-off is that hierarchical paths may be slightly suboptimal at region boundaries, but this is typically negligible for community simulation purposes.
