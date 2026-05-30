# Complementary Algorithms {#sec:complementarios}

The core systems covered in previous sections (CA, procedural generation, agents, pathfinding, physics, social simulation) form the primary architecture of a community simulation. Several additional algorithms are used to fill specific gaps: organic region boundaries, vegetation modeling, group behavior, pattern generation, and opinion dynamics.

## Voronoi Diagrams for Region Boundaries

Voronoi diagrams partition a plane into regions based on proximity to a set of generator points:

$$\text{Region}(p) = \{x \in \mathbb{R}^2 : d(x, p) < d(x, q) \; \forall q \neq p\}$$ {#eq:voronoi}

The diagram was formally described by Georgy Voronoi in 1908, though the concept of partitioning space by proximity predates him (Descartes used a similar construction in 1644 for mapping planetary regions). Voronoi diagrams have the property that all boundaries are equidistant from their nearest generators, producing convex polygons.

For world generation, Voronoi diagrams produce regions with organic, irregular boundaries suitable for biome delimitation, political territories, or cultural zones. The *relaxed* variant (Lloyd's algorithm: compute Voronoi, move each generator to its region's centroid, repeat) produces more uniform cell sizes and is used when approximately equal-sized regions are desired.

Computation of the Voronoi diagram for $n$ points in 2D is $O(n \log n)$ via Fortune's sweep-line algorithm. For simulation purposes, the diagram is typically computed once during world generation and cached.

## L-Systems for Vegetation Modeling

Lindenmayer systems (L-systems) are parallel rewriting grammars originally developed by botanist Aristid Lindenmayer in 1968 to model the growth patterns of filamentous organisms [@lindenmayer1968}. An L-system consists of:

- An *alphabet* $\Sigma$ of symbols.
- An *axiom* $\omega \in \Sigma^+$ (the initial string).
- A set of *production rules* $P \subset \Sigma \times \Sigma^*$, each mapping a symbol to a replacement string.

At each iteration, all symbols in the current string are replaced simultaneously by their production rules (parallel rewriting, distinguishing L-systems from sequential Chomsky grammars). When interpreted as turtle graphics commands ($F$ = draw forward, $+$ = turn right by angle $\theta$, $-$ = turn left, $[$ = push state, $]$ = pop state), the resulting strings produce branching structures.

Different rule sets and branching angles produce distinct morphologies: binary trees ($F \to F[+F]F[-F]F$ with $\theta \approx 25.7°$), bushes, ferns, coral structures, and algal colonies. The stochastic L-system variant assigns probabilities to multiple production rules for the same symbol, producing natural variation within a species.

For community simulation, L-systems are used during world generation to produce vegetation and, by extension, to model any growth process that follows branching patterns (rivers, cave systems, root networks). The computational cost is $O(k^n)$ where $k$ is the average rule length and $n$ is the iteration count, but $n$ is typically small (3--7 iterations).

## Boids: Reynolds' Flocking Model

Reynolds (1987) [@reynolds1987} introduced the Boids model to generate realistic flocking behavior for computer animation. Each agent (boid) computes its velocity based on three local forces derived from nearby neighbors within a perception radius:

- **Separation**: steer to avoid crowding neighbors.

$$\vec{F}_s = k_s \sum_{j \in N_i} \frac{\vec{p}_i - \vec{p}_j}{\|\vec{p}_i - \vec{p}_j\|^2}$$

- **Alignment**: steer towards the average heading of neighbors.

$$\vec{F}_a = k_a (\bar{\vec{v}}_{N_i} - \vec{v}_i)$$

- **Cohesion**: steer towards the average position of neighbors.

$$\vec{F}_c = k_c (\bar{\vec{p}}_{N_i} - \vec{p}_i)$$

The updated velocity is:

$$\vec{v}_{\text{new}} = \vec{v} + \vec{F}_s + \vec{F}_a + \vec{F}_c$$ {#eq:boids}

Each force is weighted by a coefficient ($k_s$, $k_a$, $k_c$) that controls the relative strength of each behavior. The model produces collective motion that is qualitatively similar to biological flocking, schooling, and herding, despite having no centralized control and no explicit representation of the flock as a whole.

For community simulation, boids can control herd animal behavior, migration patterns, or crowd dynamics in settlements. The model is computationally $O(n^2)$ in the naive implementation (each agent checks all others), but spatial hashing reduces this to $O(n)$ average case by limiting neighbor queries to nearby cells.

## Turing Patterns

Alan Turing proposed in 1952 that biological pattern formation (stripes, spots, spirals) can arise from the interaction of two chemical morphogens with different diffusion rates [@turing1952}:

$$\frac{\partial u}{\partial t} = D_u \nabla^2 u + f(u, v)$$ {#eq:turing-u}

$$\frac{\partial v}{\partial t} = D_v \nabla^2 v + g(u, v)$$ {#eq:turing-v}

where $u$ is the activator, $v$ is the inhibitor, $D_v \gg D_u$, and $f, g$ are nonlinear reaction terms (typically activator-inhibitor kinetics). The mechanism works because the activator creates local positive feedback (short-range activation) while the inhibitor suppresses activation at distance (long-range inhibition). The resulting instability (the Turing instability) spontaneously breaks spatial symmetry, producing stable, periodic patterns.

The specific pattern (spots, stripes, labyrinths, hexagons) depends on the ratio $D_v / D_u$, the reaction kinetics, and the domain geometry. Turing patterns have been verified experimentally in chemical systems (the Belousov-Zhabotinsky reaction, the CIMA reaction) and are hypothesized to play a role in biological pattern formation, though direct in vivo confirmation remains limited.

For world generation, Turing patterns can generate spatial variation in biome properties, resource distribution, or terrain texture. The discrete implementation on a grid is straightforward: approximate the Laplacian $\nabla^2$ by the finite difference:

$$\nabla^2 u_{i,j} \approx u_{i+1,j} + u_{i-1,j} + u_{i,j+1} + u_{i,j-1} - 4u_{i,j}$$

and iterate the coupled equations with forward Euler time-stepping.

## Opinion Dynamics: Modeling Belief Propagation

A social phenomenon that is underexplored in simulation games but well-studied in the mathematical social sciences is the dynamics of opinion formation. Two canonical models provide computationally efficient formalisms.

The **DeGroot model** (1974) describes opinion convergence in a network. Each agent $i$ holds an opinion $x_i \in \mathbb{R}$ and updates it as a weighted average of its neighbors' opinions:

$$x_i^{(t+1)} = \sum_{j} w_{ij} x_j^{(t)}$$

where $w_{ij} \geq 0$ and $\sum_j w_{ij} = 1$. The weights $w_{ij}$ represent the trust or influence that agent $i$ accords to agent $j$. Under mild conditions (strongly connected, aperiodic influence graph), all opinions converge to a consensus value that is a weighted average of initial opinions. The rate of convergence depends on the spectral gap of the weight matrix.

The **bounded confidence model** (Hegselmann and Krause, 2002) adds a homophily threshold $\epsilon$: agent $i$ only averages the opinions of neighbors whose opinions are within $\epsilon$ of its own:

$$x_i^{(t+1)} = \frac{1}{|N_i(\epsilon)|} \sum_{j : |x_j - x_i| < \epsilon} x_j^{(t)}$$

This small modification fundamentally changes the dynamics. Instead of convergence to a single consensus, the population fragments into clusters separated by at least $\epsilon$. The number and size of clusters depend on $\epsilon$ and the initial opinion distribution. For small $\epsilon$, many small clusters form (extreme polarization); for large $\epsilon$, the population converges (broad consensus).

For community simulation, opinion dynamics can model the spread of cultural traits, political beliefs, social norms, or technological preferences through a population. The computational cost is $O(V + E)$ per tick (each edge contributes to one update), which is negligible compared to pathfinding or fluid simulation. The emergent phenomena---consensus, polarization, echo chambers, cultural fragmentation---are produced by simple averaging operations without any scripted narrative.
