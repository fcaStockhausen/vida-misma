# Emergent Spaces {#sec:spaces}

Agents modify their environment to satisfy needs. This is not scripted; it emerges from the utility system interacting with the spatial substrate. The formal basis is Schelling's segregation model (Section @sec:agentes): agents with mild preferences for certain spatial properties spontaneously segregate into distinct zones on a 2D grid.

## Mechanism

Each agent evaluates tiles based on how well they satisfy current needs. A tile's utility for an agent is:

$$U_{\text{tile}}(x, y) = \sum_{i} w_i \cdot g_i(x, y)$$ {#eq:tile-utility}

where $g_i$ are tile properties relevant to need $i$:

| Need | Relevant tile property $g_i$ | Effect |
|---|---|---|
| Social | Agent density at $(x, y)$ | High density $\to$ high social utility |
| Rest | Distance from nearest machine; noise level | Low noise $\to$ high rest utility |
| Expression | Agent density $\times$ ambient noise inverse | Moderate density + quiet $\to$ high expression utility |
| Purpose | Varies by personality; may be workplace or exploration frontier | Personality-dependent |

When an agent selects a "move" action (because no higher-utility need-specific action is available), it moves toward the tile with the highest $U_{\text{tile}}$. Over time, agents with similar need profiles cluster in the same regions.

## Predicted Emergent Zones

| Zone type | Who creates it | Tile properties |
|---|---|---|
| Gathering space | High-gregariousness agents | High agent density, open floor, central location |
| Quiet corner | High-artistry agents | Low noise, low traffic, moderate distance from machines |
| Break area | High-laziness agents | Far from machines, near food, low traffic |
| Work cluster | High-compliance agents | Near machines and input storage |
| Frontier | High-curiosity agents | Unexplored tiles, factory edges |

The Director does not designate these zones. They emerge from agent movement decisions. However, the Director can *facilitate* emergence by building rooms, leaving open spaces, and placing furniture. A Director who builds a large open room with seating near the food storage creates conditions favorable to a gathering space, but whether agents actually use it as such depends on their collective personality distribution.

## Connection to Schelling

The dynamics are formally similar to Schelling's model (Section @sec:agentes): each agent has a mild preference for tiles that match its need profile, and moves when its current tile is sufficiently unsatisfying. The threshold for movement is the utility difference between the current tile and the best available tile. This threshold functions like Schelling's tolerance parameter: a high threshold (agents are tolerant of any tile) produces minimal segregation, while a low threshold (agents are picky) produces strong spatial clustering.

The key difference from Schelling is that preferences are *multidimensional*: an agent cares about noise, density, proximity to resources, and distance from machines simultaneously. This produces more complex spatial patterns than the binary same/different classification in Schelling's original model.
