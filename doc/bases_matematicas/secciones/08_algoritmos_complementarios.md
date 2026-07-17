# Complementary Algorithms {#sec:complementarios}

The core systems covered in previous sections (CA, procedural generation, agents, pathfinding, physics, social simulation) form the primary architecture of a community simulation. Several additional algorithms are used to fill specific gaps: organic region boundaries, vegetation modeling, group behavior, pattern generation, opinion dynamics, and---connecting several of the above under one algebraic roof---tropical geometry.

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

## Tropical Geometry: The Algebraic Shape of Decision, Search, and Utility {#sec:tropical}

The preceding subsections present heterogeneous algorithms (spatial tessellation, generative grammars, flocking, reaction-diffusion, opinion averaging) that appear mathematically unrelated. A surprising unifying fact is that **three of the load-bearing computations in *La Vida Misma*** — action selection, pathfinding, and the utility functions themselves — **are computations over the tropical semiring**, whether or not they were designed with that framing in mind. This subsection makes the connection explicit, because doing so clarifies what each system is *actually* computing and yields formal tools (tropical convexity, tropical equilibration) that apply to all three at once.

### The Tropical Semiring

The **tropical semiring** (also called the min-plus or max-plus semiring) is the set $\mathbb{R} \cup \{\infty\}$ equipped with two operations:

$$a \oplus b = \min(a, b), \qquad a \otimes b = a + b$$

where the tropical "addition" $\oplus$ is ordinary minimum and the tropical "multiplication" $\otimes$ is ordinary addition. The additive identity is $\infty$ (the annihilator of $\min$) and the multiplicative identity is $0$. The dual **max-plus** convention takes $\oplus = \max$ instead. Either convention yields a semiring; crucially, $\oplus$ is **idempotent** ($a \oplus a = a$), which is the property that makes tropical geometry behave differently from classical linear algebra.

### Application 1: A* Pathfinding Is Min-Plus Linear Algebra

The shortest-path problem is the canonical computation of the min-plus semiring [@mohri2002]. Pathfinding via A* (Section @sec:pathfinding) is a concrete instance: the total path cost is the tropical product (ordinary sum) of edge weights, and the optimal path is the tropical sum (ordinary minimum) over alternative paths.

The implementation in `pathfinding.h` makes this literal. The `f`-score is the tropical product of the accumulated cost and the heuristic:

```
struct Node { int g; int f; /* g + heuristic */ };
```

Path relaxation accumulates cost via ordinary `+` (tropical $\otimes$) and selects via strict `<` (tropical $\oplus = \min$):

```
int ng = cur.g + 1;                  // tropical product
if (ng < g_score[ni]) {              // tropical sum (min)
    g_score[ni] = ng;                // write the new minimum
    int nf = ng + |dx| + |dy|;       // tropical product with heuristic
}
```

The initial `g_score` seed of `999999` is the additive identity $\infty$ of the min-plus semiring. The min-heap keyed on `f` repeatedly extracts the $\min$ of the open set. This is a textbook fixpoint computation over $(\mathbb{R} \cup \{\infty\}, \min, +)$.

### Application 2: Boltzmann Action Selection Is a Deformed Max-Plus Semiring

The agent decision system (Section @sec:agentes) selects the highest-utility action each tick. The naive rule $a^* = \arg\max_{a} U(a)$ is exactly the $\oplus = \max$ operation of the max-plus semiring. The implementation in `sim_utility.cpp` replaces this hard argmax with a **Boltzmann softmax** controlled by a temperature $\tau$:

```
float tau = config_.selection_temperature;     // config.h: tau=0.4, "0=greedy"
weights[i] = std::exp((u - max_u) / tau);      // sim_utility.cpp
```

This is not an arbitrary smoothing. The softmax is the standard **LogSumExp** function, which is the unique optimal smooth approximation to the max function in a precise sense: every overestimating smoothing of $\max$ differs from it by at least as much as LogSumExp does. Formally:

$$\mathrm{LSE}_{1/\tau}(\mathbf{u}) = \tau \log \sum_i e^{u_i / \tau} \xrightarrow{\tau \to 0^+} \max_i u_i$$

In the zero-temperature limit, the softmax degenerates to the one-hot argmax, recovering the undeformed max-plus semiring exactly. The parameter `selection_temperature` is therefore a **deformation parameter** that interpolates between the tropical semiring ($\tau = 0$, greedy/deterministic) and the standard real semiring ($\tau \to \infty$, uniform/random). The same pattern governs the factory adversary's target selection (`simulation.cpp`, `restructure_temperature`), which is structurally identical: `std::exp((scores[i] - max_score) / tau)`. Both selection sites in the codebase are tropical deformations.

### Application 3: Utility Functions Are Tropical Rational Functions

The composite utility $U(a) = \sum_i w_i f_i(\text{need}_i)$ (Eq. @eq:utility-composite) is piecewise-linear in the need variables once the threshold gates are accounted for. Three representative structures in `sim_utility.cpp` make this precise:

1. **Thresholded multiplicative gates** (the "Maslow boost"):
   ```
   if (needs.hunger < 0.3 && needs.rest < 0.3) u_socialize *= 4.0;
   ```
   These produce kinks in the utility surface at each threshold.

2. **Piecewise-quadratic spike** (`critical_spike`):
   ```
   if (need < 0.75) return 0.0;
   return ((need - 0.75)/0.25) * ((need - 0.75)/0.25) * 5.0;
   ```
   Identically zero below $0.75$, polynomial above.

3. **Bonabeau response threshold** (`bonabeau`, Bonabeau et al. 1996):
   ```
   return s2 / (s2 + t2 + 0.001f);
   ```
   Applied multiplicatively to every action utility (lines 868--875).

A function built from finitely many affine pieces joined at thresholds is a **tropical polynomial**; a difference of two such functions is a **tropical rational function**. Zhang, Naitzat, and Lim (2018) [@zhang2018tropical] showed that the decision boundaries of piecewise-linear classifiers (such as ReLU neural networks) are exactly tropical hypersurfaces, and that the number of linear regions controls the classifier's expressiveness. The agent utilities in *La Vida Misma* inhabit the same class: the "number of behavioral regimes" an agent can express is bounded by the number of linear regions of its utility surface, which is a tropical-geometric quantity.

### Why This Unification Matters

Treating action selection, search, and utility under one algebraic roof has three payoffs:

- **Consistency.** The deformation parameter `selection_temperature` and the pathfinding cost are not ad hoc: both live in the same semiring-theoretic framework, so results about one (e.g., convergence of tropical fixpoint iterations) transfer to the other.
- **Equilibration.** Noel et al. (2013) [@noel2013] define *tropical equilibration* as the regime where two opposing monomials have equal magnitude — a rigorous version of Wolfram's "edge of chaos." The adversary-intensity dial that blends the factory's strategic score with uniform randomness (`adversary_intensity` in `config.h`) is operationally a tropical-equilibration knob: at $\alpha = 0$ the system is in the uniform (max-entropy) regime; at $\alpha = 1$ it is in the pure-best-response (zero-temperature tropical) regime; the interesting dynamics live between.
- **Auditability.** Tropical geometry provides tools (Newton polytopes, tropical convexity) to count decision regions and measure robustness. A future diagnostic could report the number of linear regions an agent's utility surface exhibits, giving a formal measure of that agent's "behavioral complexity."

This subsection is descriptive rather than prescriptive: it does not change the implementation. It documents that three systems already in the codebase are instances of one mathematical structure, and it names the structure so that future analysis can use its tools.
