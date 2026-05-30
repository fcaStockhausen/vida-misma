# Physical Simulation Systems {#sec:fisicos}

Physical systems in a community simulation must be computationally inexpensive enough to run alongside agent AI, pathfinding, and social simulation, while producing behavior that is qualitatively plausible. This section covers three systems that model physical phenomena using discrete approximations rather than continuous equations: fluid dynamics, heat transfer, and structural mechanics.

## Fluid Dynamics as a Cellular Automaton

The Navier-Stokes equations provide the physically accurate model for fluid behavior, but their computational cost (requiring iterative solvers on fine meshes at small time steps) makes them impractical for real-time simulation with many concurrent systems. The cellular automaton approach, as used in Dwarf Fortress, approximates fluid behavior with a quantized, rule-based system.

Each tile contains a fluid level $s \in \{0, 1, \ldots, 7\}$. The update rules are applied in order:

1. **Gravity**:

$$s_{\text{below}} < 7 \implies \text{transfer} = \min(s_{\text{current}}, 7 - s_{\text{below}})$$ {#eq:fluid-gravity}

2. **Lateral spread** (when the tile below is full):

$$s_{\text{neighbor}} < s_{\text{current}} \implies \text{transfer} = \left\lfloor \frac{s_{\text{current}} - s_{\text{neighbor}}}{2} \right\rfloor$$ {#eq:fluid-spread}

3. **Pressure propagation**: flood-fill from pressure sources to identify connected fluid bodies, then equalize levels across connected tiles, allowing fluid to rise above its entry point in enclosed channels.

This system does not conserve momentum, does not model viscosity or surface tension, and does not produce turbulence. However, it reproduces several qualitatively important behaviors: downward flow under gravity, lateral spreading on flat surfaces, accumulation in basins, and pressure-driven rising in enclosed columns. These are sufficient for gameplay scenarios (flooding, irrigation, magma intrusion) at a computational cost of $O(\text{fluid tiles})$ per tick.

## Heat Diffusion

Heat transfer is modeled as discrete diffusion on the grid. Each tile has a temperature $T$ and a material-specific conductivity $k$. The update rule is:

$$T_t^{i,j} = T_{t-1}^{i,j} + \alpha \cdot k_{i,j} \cdot \sum_{(n,m) \in N(i,j)} (T_{t-1}^{n,m} - T_{t-1}^{i,j})$$ {#eq:heat-diffusion}

where $\alpha$ is a global scaling constant and $N(i,j)$ is the neighborhood of tile $(i,j)$. This is a discrete approximation of the continuous heat equation:

$$\frac{\partial T}{\partial t} = k \nabla^2 T$$

The discrete update converges to the continuous solution as the grid resolution increases and the time step decreases. For simulation purposes, the discrete version is sufficient: it produces the expected qualitative behavior---heat flows from hot to cold regions, materials with higher conductivity equilibrate faster, and insulated regions retain heat longer.

The stability condition for the explicit Euler scheme is:

$$\alpha \cdot k_{\max} \cdot |N| < 1$$

Violating this condition produces numerical instability (temperature oscillations that grow without bound). In practice, this constrains the choice of $\alpha$ given the maximum conductivity in the simulation.

## Structural Collapse

Structural mechanics in community simulations typically use a simplified connectivity model rather than a stress analysis. The rule is:

1. Each constructed tile (wall, floor, etc.) must be *connected to the ground* via a continuous path through adjacent constructed tiles.
2. Connectivity is checked via flood-fill from the ground layer.
3. Tiles that lose connectivity (due to mining, destruction, or collapse) enter a "falling" state.
4. Falling tiles propagate downward until they reach a supported surface or the ground.

This model captures the essential gameplay-relevant behavior---undermining a support causes the structure above to collapse---without computing stress vectors. It is computationally $O(\text{constructed tiles})$ when triggered by a modification event.

The connection between structural collapse and fluid dynamics is an example of emergent interaction between subsystems: a flood can destroy a support, triggering a collapse, which redirects the flood. Neither system references the other; the interaction occurs through the shared tile grid.
