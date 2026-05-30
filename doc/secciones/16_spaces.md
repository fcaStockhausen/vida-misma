# Emergent Spaces {#sec:spaces}

The simulation unfolds on a discrete grid of $60 \times 40$ tiles. Each tile belongs to one of several functional types---FoodSource, Machine, Storage, OpenSpace, ScrapPile, Floor, Wall, Entrance, and Exit---and agents move across this substrate in pursuit of their highest-utility action. Spatial self-organization arises not from top-down zoning but from the repeated interaction between utility-maximising agents and a heterogeneous tile landscape. The formal basis for this emergence is Schelling's segregation model (Section @sec:agentes): agents with similar need profiles, selecting the same action, are drawn to the same tile types, and thus spontaneously cluster into functionally distinct regions.

## The Tile Landscape

The default factory layout partitions the grid into several functional zones. **FoodSource** tiles, carrying a regenerating resource pool ($r_{\max} = 8$, $\dot{r} = 0.02$/tick), are scattered in clusters at the four corners and along the central horizontal corridor. **ScrapPile** tiles provide finite raw material along the north and south edges and at the cardinal midpoints. **Machine** frames are placed in four $2 \times 2$ clusters at the quadrant centres, each initially unbuilt and requiring construction. **Storage** bays sit adjacent to each machine cluster, plus two central storages at mid-grid. A central **OpenSpace** zone ($7 \times 5$ tiles) occupies the factory floor. The perimeter is bounded by Wall tiles with Entrance and Exit gates on the left and right walls.

## Tile-Action Affinity

The critical link between space and behaviour is the action--tile affinity table. When the utility system (Section @sec:agentes) selects an agent's best action $a^{*}$, the target-finding system maps that action to a specific tile type:

| Action | Target tile type | Selection criterion |
|---|---|---|
| GATHER | FoodSource or ScrapPile | Nearest non-exhausted source; prefers food when hungry |
| BUILD | Machine (unbuilt) | Nearest machine with $\texttt{built} = \texttt{false}$ |
| WORK | Machine (built) | Nearest machine with $\texttt{built} = \texttt{true}$ |
| EAT | Current position | Consumes from inventory or adjacent Storage |
| REST | Current position | No tile requirement |
| SOCIALIZE | Nearest agent | Moves toward another alive agent |
| CREATE | OpenSpace | Nearest OpenSpace tile |
| EXPLORE | Random walkable tile | Uniform random coordinates |

Movement is greedy one-step-per-tick Manhattan-distance minimisation toward the target tile, with a noise parameter ($p = 0.05$) that occasionally substitutes a random step, preventing perfect convergence and maintaining spatial heterogeneity.

## Action Utility and Spatial Destination

An agent's spatial trajectory is determined entirely by its utility computation. Let $n_i \in [0, 1]$ denote the current value of need $i$ (hunger, rest, social, expression, purpose), and let $\alpha = 2.0$ be the urgency exponent. The urgency of need $i$ is:

$$\phi(n_i) = n_i^{\alpha}$$ {#eq:urgency}

Each action utility $U_a$ is a function of need urgencies, personality traits $\mathbf{p}$, and inventory state $\mathbf{v}$. The dominant terms for spatially-bound actions are:

$$U_{\text{GATHER}} = \max\!\bigl(\phi(n_{\text{hunger}})(1 - s_{\text{food}}) \cdot 1.5,\; p_{\text{compliance}} \cdot \phi(n_{\text{purpose}}) \cdot 0.5 \cdot \mathbf{1}[\text{unbuilt machines}]\bigr)$$ {#eq:u-gather}

$$U_{\text{BUILD}} = p_{\text{compliance}} \cdot \phi(n_{\text{purpose}}) \cdot 1.2 \cdot \min\!\bigl(1,\; v_{\text{raw}} / 2\bigr) \cdot \mathbf{1}[v_{\text{raw}} > 0.5]$$ {#eq:u-build}

$$U_{\text{WORK}} = p_{\text{compliance}} \cdot \phi(n_{\text{hunger}}) \cdot 0.8 + (1 - p_{\text{laziness}}) \cdot \phi(n_{\text{purpose}}) \cdot 0.3$$ {#eq:u-work}

$$U_{\text{CREATE}} = p_{\text{artistry}} \cdot \phi(n_{\text{expression}})$$ {#eq:u-create}

where $s_{\text{food}} = \min(1, (v_{\text{food}} + 0.5\,v_{\text{raw\_food}})/2)$ is the agent's food-security index and $\mathbf{1}[\cdot]$ is the indicator function for environmental preconditions (e.g., whether a relevant tile exists).

The agent selects $a^{*} = \arg\max_a U_a$ (with 2% random exploration noise), and the target-finding system immediately maps $a^{*}$ to a tile coordinate. The spatial consequence is transparent: an agent with high $n_{\text{hunger}}$ and low $s_{\text{food}}$ receives a high $U_{\text{GATHER}}$ score and moves toward the nearest FoodSource; an agent with high $p_{\text{compliance}}$ and raw material in inventory receives a high $U_{\text{BUILD}}$ and gravitates toward an unbuilt Machine; an agent with high $p_{\text{artistry}}$ and elevated $n_{\text{expression}}$ receives a high $U_{\text{CREATE}}$ and seeks out OpenSpace.

## Predicted Emergent Zones

Because actions are mapped to tile types, agents with similar need--personality profiles converge on the same spatial regions. The following table summarises the expected emergent zones:

| Zone type | Tile attractor | Who congregates | Dominant action |
|---|---|---|---|
| Foraging grounds | FoodSource clusters | Hungry agents (high $n_{\text{hunger}}$, low $s_{\text{food}}$) | GATHER |
| Construction sites | Unbuilt Machine clusters | Compliant, purpose-driven agents carrying raw material | BUILD |
| Factory floor | Built Machine clusters | Compliant, non-lazy agents | WORK |
| Creative commons | Central OpenSpace | Artistic agents (high $p_{\text{artistry}}$, high $n_{\text{expression}}$) | CREATE |
| Supply corridors | Storage bays | Agents carrying resources (deposit/withdraw cycles) | GATHER, EAT |
| Social hubs | Near other agents | Gregarious agents (high $p_{\text{gregariousness}}$) | SOCIALIZE |
| Frontier perimeter | Map edges, unexplored tiles | Curious agents (high $p_{\text{curiosity}}$) | EXPLORE |
| Rest areas | Any low-traffic tile | Exhausted, lazy agents (high $n_{\text{rest}}$, high $p_{\text{laziness}}$) | REST |

The Director does not designate these zones. They emerge from agent movement decisions filtered through the tile--action affinity table. However, the Director can *facilitate* emergence by placing or removing tile types: building additional OpenSpace tiles creates new attractors for artistic agents, while relocating Storage bays shifts supply corridors. The Director shapes the possibility space; the agents determine its actual use.

## Connection to Schelling's Model

The dynamics are formally analogous to Schelling's spatial segregation model (Section @sec:agentes). In Schelling's formulation, each agent has a tolerance threshold $\tau$ and moves when the fraction of unlike neighbours falls below $\tau$, producing macroscopic segregation from mild individual preferences. In the present system, each agent has a utility function over actions, each action maps to a tile type, and the agent moves toward tiles that enable its highest-utility action. The Schelling tolerance parameter $\tau$ is here replaced by the utility differential $\Delta U = U_{a^{*}}(\text{best tile}) - U_{a^{*}}(\text{current tile})$: when $\Delta U$ is small (needs are satisfied, personality is moderate), agents have little incentive to move and remain dispersed; when $\Delta U$ is large (acute need, strong personality), agents converge aggressively on the relevant tile type, producing sharp spatial clustering.

Two features distinguish the present model from Schelling's original. First, preferences are *multidimensional*: an agent's location depends simultaneously on hunger, fatigue, sociability, artistry, and purpose, weighted by personality traits. This produces overlapping, partially contradictory spatial attractors rather than the binary same/different classification of the original model. Second, the spatial substrate is *heterogeneous*: tile types are not interchangeable, and their fixed distribution introduces geographic constraints absent from Schelling's uniform grid. The four machine clusters, for instance, create fixed attractor basins that compete for the same compliant-agent population, fragmenting the would-be "work zone" into geographically separate subzones---a pattern that has no analogue in the original model.
