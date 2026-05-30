# Procedural Terrain Generation {#sec:procedural}

Cellular automata provide the formal framework for *dynamics*---how the world evolves over time. Procedural generation addresses a distinct problem: constructing the initial *topology* of the simulation world. This section covers the mathematical techniques used to produce terrain, biomes, and spatial structures that exhibit statistical properties similar to natural landscapes.

## Gradient Noise: Perlin Noise (1983)

Ken Perlin developed gradient noise while working on computer-generated textures for the film *Tron* (1982). The problem was that existing methods produced either constant-valued regions or uncorrelated white noise. Neither captured the statistical structure of natural phenomena like clouds, terrain, or organic textures, which exhibit correlated variation across multiple spatial scales.

Perlin's method assigns a random unit-length gradient vector $\vec{g}_i$ to each node of a regular grid. For any query point $\vec{P}$ within a grid cell, the algorithm computes dot products between the gradient at each corner and the displacement vector:

$$\text{dot}_i = \vec{g}_i \cdot (\vec{P} - \vec{c}_i)$$ {#eq:perlin-dot}

where $\vec{c}_i$ is the position of corner $i$. The $2^d$ dot products (for dimension $d$) are then interpolated using a smoothing function. The original formulation used a cubic Hermite polynomial:

$$f(x) = 3x^2 - 2x^3$$ {#eq:hermite}

This function satisfies $f(0) = 0$, $f(1) = 1$, and $f'(0) = f'(1) = 0$, but its second derivative is discontinuous at cell boundaries, producing visible artifacts. The improved version uses a quintic polynomial that is $C^2$ continuous:

$$f(x) = 6x^5 - 15x^4 + 10x^3$$ {#eq:quintic}

satisfying $f'(0) = f'(1) = 0$ and $f''(0) = f''(1) = 0$.

The output is a continuous scalar field in $[-1, 1]$ with controlled spectral properties. To produce the multi-scale variation characteristic of natural terrain, multiple *octaves* are superimposed:

$$\text{noise}_{\text{total}}(\vec{x}) = \sum_{i=0}^{O-1} \text{perlin}(\vec{x} \cdot f_0 \cdot l^i) \cdot a_0 \cdot p^i$$ {#eq:fbm}

where $O$ is the number of octaves, $l$ is the *lacunarity* (frequency multiplier between successive octaves, typically $l \approx 2$), and $p$ is the *persistence* (amplitude decay, typically $p \approx 0.5$). This technique, known as *fractal Brownian motion* (fBm), produces a power spectrum that approximates $1/f^\beta$ noise---a statistical signature common in natural terrain, coastlines, and cloud formations.

| Parameter | Effect on output | Typical range |
|---|---|---|
| Base frequency $f_0$ | Scale of the largest features | 0.001--0.1 (relative to map size) |
| Octaves $O$ | Amount of high-frequency detail | 4--12 |
| Persistence $p$ | Contribution of each successive octave | 0.4--0.7 |
| Lacunarity $l$ | Frequency ratio between octaves | 1.8--2.2 |

Perlin received a Technical Academy Award in 1997 for this work. **Simplex noise** (Perlin, 2001) improved on the original by replacing the hypercubic grid with a simplex lattice, reducing computational complexity from $O(2^d)$ to $O(d)$ in dimension $d$ and eliminating directional artifacts inherent to the axis-aligned grid structure.

## Diamond-Square: Midpoint Displacement on a Grid

An alternative to gradient noise for terrain generation is the *diamond-square* algorithm, a specific instance of midpoint displacement fractals. The method was motivated by Mandelbrot's observation (1967) that natural forms such as coastlines and mountain ridges exhibit statistical self-similarity: their structure recurs at multiple spatial scales.

The algorithm operates on a $2^n + 1 \times 2^n + 1$ grid:

1. Initialize the four corners with random values.
2. **Diamond step**: set the center of each square to the average of its four corners plus a random displacement $\delta \sim \mathcal{U}(-d_n, d_n)$.
3. **Square step**: set the midpoint of each edge to the average of its available neighbors plus displacement $\delta$.
4. Reduce the displacement magnitude:

$$d_{n+1} = d_n \times 2^{-H}$$ {#eq:midpoint-displacement}

5. Repeat on each sub-square until the grid is fully populated.

The parameter $H$ (roughness) controls the fractal dimension of the output. Higher $H$ produces smoother terrain; lower $H$ produces more rugged terrain. The relationship to fractal dimension is $D = 3 - H$ for a 2D height field.

Dwarf Fortress uses diamond-square for elevation maps rather than Perlin noise, reportedly for reasons of implementation simplicity and predictability of output on a regular grid.

## Wave Function Collapse: Constraint Satisfaction for Tile Maps (2016)

Gradient noise and midpoint displacement generate continuous scalar fields. A different problem arises when the goal is to generate *discrete, coherent structures*: tile maps where adjacent tiles must satisfy compatibility constraints (e.g., "a door tile requires wall tiles on both sides"), or patterns that must match a given exemplar image.

Wave Function Collapse (WFC) [@gumin2017] formulates this as a constraint satisfaction problem. Each cell $c_i$ maintains a set of allowed states $S_i \subseteq \Sigma$ (the "superposition"). The algorithm iterates:

1. **Observe**: select the cell $c_i$ with minimum Shannon entropy among uncollapsed cells:

$$H(c_i) = -\sum_{s \in S_i} p_s \log_2 p_s$$ {#eq:shannon-entropy}

where $p_s$ is the prior probability of state $s$.

2. **Collapse**: set $S_i = \{s\}$ for a randomly chosen $s$, weighted by prior probabilities.

3. **Propagate**: enforce arc consistency---for each neighbor $c_j$ of a recently collapsed cell, remove states from $S_j$ that are incompatible with the collapsed state. Continue propagation until no further eliminations occur.

4. If a cell's allowed set becomes empty (contradiction), backtrack or restart.

The minimum-entropy heuristic (step 1) is a standard variable-ordering strategy from the CSP literature, analogous to the "most constrained variable" heuristic. Karth and Smith (2017) [@karth2017] formalized this connection, demonstrating that WFC is arc consistency checking with a specific variable-ordering heuristic.

WFC operates in two modes: *tile-based* (adjacency constraints specified explicitly) and *overlapping* (constraints extracted automatically from an exemplar image by analyzing all $N \times N$ sub-patterns). The overlapping mode has been widely adopted in procedural generation for games including *Caves of Qud*, *Townscaper*, and *Bad North*.

## Layered Generation: The Dwarf Fortress Pipeline

Dwarf Fortress generates worlds through a sequential pipeline where each layer depends on previously computed layers. Adams describes this as an instance of his second design principle: decompose the system into independent subsystems and let complex phenomena emerge from their interaction [@adams2014].

| Layer | Output | Primary method |
|---|---|---|
| Elevation | Height map ($0$--$400$) | Diamond-square fractal |
| Temperature | Per-tile temperature | Function of elevation + latitude + noise |
| Rainfall | Per-tile precipitation | Noise + orographic shadow model |
| Drainage | Per-tile drainage value ($0$--$100$) | Separate fractal |
| Vegetation | Per-tile vegetation density | Derived from rainfall + temperature + drainage |
| Biome classification | Per-tile biome type | Threshold intersection of all previous layers |
| Rivers | River paths | Gradient descent on elevation with drainage constraints |
| Civilizations | Settlements and territories | Voronoi-based territory assignment with historical simulation |

The key property of this pipeline is that each layer introduces a small number of independent parameters, but their *intersection* produces rich combinatorial variation. A "desert" is not coded as a single entity; it emerges where elevation, temperature, rainfall, and drainage simultaneously satisfy the desert criterion. This produces geographic features that are internally consistent in ways that a monolithic biome generator would not guarantee.

## Diamond-Square vs. Perlin Noise: A Comparison

| Property | Perlin/Simplex noise | Diamond-square |
|---|---|---|
| Grid requirement | Arbitrary resolution | Must be $2^n + 1$ |
| Continuity | $C^2$ (quintic) or $C^1$ (cubic) | Discontinuous at boundaries |
| Directional artifacts | Axis-aligned (Perlin); isotropic (Simplex) | None inherent |
| Self-similarity | Via fBm octave stacking | Built into the algorithm |
| Implementation complexity | Moderate | Low |
| Extensibility to $d > 2$ | Straightforward | Requires generalization |
| Control over spectrum | Explicit (octaves, persistence) | Indirect (roughness parameter) |
