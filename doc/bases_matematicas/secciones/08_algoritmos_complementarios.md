# Complementary Algorithms {#sec:complementarios}

The core systems covered in previous sections (CA, procedural generation, agents, pathfinding, physics, social simulation) form the primary architecture of a community simulation. Several additional algorithms are used to fill specific gaps: organic region boundaries, vegetation modeling, group behavior, pattern generation, opinion dynamics, and---connecting several of the above under one algebraic roof---tropical geometry.

## Voronoi Diagrams for Region Boundaries

Voronoi diagrams partition a plane into regions based on proximity to a set of generator points:

$$\text{Region}(p) = \{x \in \mathbb{R}^2 : d(x, p) < d(x, q) \; \forall q \neq p\}$$ {#eq:voronoi}

The diagram was formally described by Georgy Voronoi in 1908, though the concept of partitioning space by proximity predates him (Descartes used a similar construction in 1644 for mapping planetary regions). Voronoi diagrams have the property that all boundaries are equidistant from their nearest generators, producing convex polygons.

For world generation, Voronoi diagrams produce regions with organic, irregular boundaries suitable for biome delimitation, political territories, or cultural zones. The *relaxed* variant (Lloyd's algorithm: compute Voronoi, move each generator to its region's centroid, repeat) produces more uniform cell sizes and is used when approximately equal-sized regions are desired.

Computation of the Voronoi diagram for $n$ points in 2D is $O(n \log n)$ via Fortune's sweep-line algorithm. For simulation purposes, the diagram is typically computed once during world generation and cached.

## L-Systems for Vegetation Modeling

Lindenmayer systems (L-systems) are parallel rewriting grammars originally developed by @lindenmayer1968 to model the growth patterns of filamentous organisms. An L-system consists of:

- An *alphabet* $\Sigma$ of symbols.
- An *axiom* $\omega \in \Sigma^+$ (the initial string).
- A set of *production rules* $P \subset \Sigma \times \Sigma^*$, each mapping a symbol to a replacement string.

At each iteration, all symbols in the current string are replaced simultaneously by their production rules (parallel rewriting, distinguishing L-systems from sequential Chomsky grammars). When interpreted as turtle graphics commands ($F$ = draw forward, $+$ = turn right by angle $\theta$, $-$ = turn left, $[$ = push state, $]$ = pop state), the resulting strings produce branching structures.

Different rule sets and branching angles produce distinct morphologies: binary trees ($F \to F[+F]F[-F]F$ with $\theta \approx 25.7°$), bushes, ferns, coral structures, and algal colonies. The stochastic L-system variant assigns probabilities to multiple production rules for the same symbol, producing natural variation within a species.

For community simulation, L-systems are used during world generation to produce vegetation and, by extension, to model any growth process that follows branching patterns (rivers, cave systems, root networks). The computational cost is $O(k^n)$ where $k$ is the average rule length and $n$ is the iteration count, but $n$ is typically small (3--7 iterations).

## Boids: Reynolds' Flocking Model

@reynolds1987 introduced the Boids model to generate realistic flocking behavior for computer animation. Each agent (boid) computes its velocity based on three local forces derived from nearby neighbors within a perception radius:

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

@turing1952 proposed that biological pattern formation (stripes, spots, spirals) can arise from the interaction of two chemical morphogens with different diffusion rates:

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

The preceding subsections present heterogeneous algorithms (spatial tessellation, generative grammars, flocking, reaction-diffusion, opinion averaging) that appear mathematically unrelated. Two load-bearing computations in *La Vida Misma* have a useful tropical interpretation: shortest-path search is min-plus, and greedy action selection is the zero-temperature limit of the implemented Boltzmann selector. Some min/max utility fragments can also be described tropically, but the complete nonlinear utility model cannot.

### The Tropical Semiring

The **tropical semiring** (also called the min-plus or max-plus semiring) is the set $\mathbb{R} \cup \{\infty\}$ equipped with two operations:

$$a \oplus b = \min(a, b), \qquad a \otimes b = a + b$$

where the tropical "addition" $\oplus$ is ordinary minimum and the tropical "multiplication" $\otimes$ is ordinary addition. The additive identity is $\infty$ (the annihilator of $\min$) and the multiplicative identity is $0$. The dual **max-plus** convention takes $\oplus = \max$ instead. Either convention yields a semiring; crucially, $\oplus$ is **idempotent** ($a \oplus a = a$), which is the property that makes tropical geometry behave differently from classical linear algebra.

### Application 1: A* Pathfinding Is Min-Plus Linear Algebra

The shortest-path problem is the canonical computation of the min-plus semiring [@mohri2002]. Pathfinding via A* (@sec:pathfinding) is a concrete instance: the total path cost is the tropical product (ordinary sum) of edge weights, and the optimal path is the tropical sum (ordinary minimum) over alternative paths.

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

### Application 2: Boltzmann Selection and the Max Limit

The greedy limiting rule $a^* = \arg\max_{a} U(a)$ is exactly the $\oplus = \max$ operation of the max-plus semiring. The implemented agent selector in `sim_utility.cpp` instead uses a **Boltzmann softmax** over feasible positive-score actions, controlled by a temperature $\tau$:

```
float tau = config_.selection_temperature;     // config.h: tau=0.4, "0=greedy"
weights[i] = std::exp((u - max_u) / tau);      // sim_utility.cpp
```

After normalization, these weights define softmax probabilities. The related
scalar LogSumExp function is

$$\mathrm{LSE}_{\tau}(\mathbf{u}) = \tau \log \sum_i e^{u_i / \tau} \xrightarrow{\tau \to 0^+} \max_i u_i.$$

Its gradient with respect to utilities is the softmax distribution. As temperature
approaches zero, probability concentrates on maximizing actions; it becomes
one-hot only when the maximum is unique, while ties retain mass across maximizers.
At high temperature the implementation approaches a uniform distribution over
positive-score feasible actions. These are limiting facts about stochastic choice,
not an interpolation between semirings. A structurally similar target softmax
survives only in historical `external.policy_variant = 0`; the canonical
indifferent institution uses a deterministic physical maximum and must not be
described as a second strategic selector.

### Application 3: Limits of the Utility Analogy

A function assembled solely from finitely many affine pieces through `min`, `max`,
and addition admits a tropical-polynomial or tropical-rational description. Some
small utility fragments have that shape, such as clamped linear factors and maxima
between action sub-scores. The implemented action utilities as a whole do not.

Canonical survival urgency includes a sigmoid; retained variants include powers
and quadratic spikes; Bonabeau response thresholds are rational functions; and
many terms are multiplied together. Threshold branches may add kinks or jumps,
but they do not turn these nonlinear expressions into affine pieces. Results for
piecewise-linear classifiers and tropical hypersurfaces [@zhang2018tropical]
therefore cannot be transferred to the full behavioral surface without first
constructing and validating an explicit piecewise-linear approximation.

### Why This Unification Matters

Keeping exact tropical structure separate from limiting analogies has three payoffs:

- **Precision.** Path cost is genuinely min-plus, while temperature describes a
  smooth approximation to max. The analogy relates their limiting operations but
  does not transfer convergence theorems between search and stochastic choice.
- **Historical comparison.** @noel2013 define *tropical equilibration* as the regime where two opposing monomials have equal magnitude. The retained legacy `adversary_intensity` and `restructure_temperature` controls can be studied through that lens, but they do not describe canonical `external.policy_variant = 1`.
- **Auditability.** Tropical tools may analyze explicitly piecewise-linear
  approximations or isolated min/max fragments. They do not currently provide a
  count of behavioral regions for the nonlinear executable utility.

This subsection is descriptive rather than prescriptive: it does not change the
implementation. It identifies exact tropical structure in pathfinding, a limiting
connection in action selection, and the boundary beyond which that framing no
longer describes the executable utility.
