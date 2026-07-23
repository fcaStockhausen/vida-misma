# Cellular Automata {#sec:ca}

This section establishes the mathematical foundations of emergence: the phenomenon whereby simple, local transition rules applied uniformly across a spatial lattice produce complex, often unpredictable global behavior. Cellular automata (CAs) are the minimal formal system in which this phenomenon can be studied rigorously, and they underpin most of the simulation components discussed in subsequent sections.

## Formal Definition

A cellular automaton is defined as a tuple $A = (L, S, N, f)$ where:

- $L$ is the *lattice*: a regular grid, typically $\mathbb{Z}^d$ for $d \in \{1, 2, 3\}$.
- $S$ is a finite set of states. The minimal binary case is $S = \{0, 1\}$.
- $N \subset L$ is the *neighborhood*: a finite set of relative cell positions that influence each cell's update.
- $f: S^{|N|} \to S$ is the *local transition function*, applied synchronously to every cell at each discrete time step $t$.

The global configuration $C_t: L \to S$ maps each cell to its state at time $t$. The global dynamics are fully determined by iterating $f$ across all cells in parallel. No cell has access to global information; each cell's next state depends only on its current neighborhood.

The lattice geometry and neighborhood structure determine the class of patterns that can emerge. The standard neighborhoods are:

| Name | Cardinality | Definition | Typical application |
|---|---|---|---|
| Von Neumann | $2d + 1$ | $\{c : \|c\|_1 \leq 1\}$ | Diffusion processes |
| Moore | $3^d$ | $\{c : \|c\|_\infty \leq 1\}$ | Game of Life, fluid simulation |
| Extended ($r=2$) | $5^d$ | $\{c : \|c\|_\infty \leq 2\}$ | Long-range pattern formation |

The choice of neighborhood is not merely parametric: it defines the maximum speed of information propagation and the class of local interactions available to the system.

## Historical Context: Self-Reproduction and Universality

Von Neumann introduced the cellular automaton framework in the late 1940s as a formal setting for the question of kinematic self-reproduction. His construction used 29 states per cell and demonstrated that a configuration could contain a description of itself and use that description to build a copy [@burks1970]. The result was primarily existential: it established that self-reproduction is possible within a deterministic, discrete framework, but the construction was not intended for practical simulation.

Subsequent simplifications by Ulam, Burks, and others reduced the state space while preserving the core property: that local synchronous update rules can produce global configurations with non-trivial structure. The question of *how simple* such a system can be while still exhibiting complex behavior motivates the next development.

## Conway's Game of Life (1970)

John Horton Conway's Game of Life (GoL) is a binary-state CA on $\mathbb{Z}^2$ with Moore neighborhood, governed by the rule B3/S23. For a cell with state $s$ and neighbor sum $n$ (the number of live cells among its 8 neighbors):

$$f(s, n) = \begin{cases} 1 & \text{if } s = 0 \text{ and } n = 3 \\ 1 & \text{if } s = 1 \text{ and } n \in \{2, 3\} \\ 0 & \text{otherwise} \end{cases}$$

The rule was selected by exhaustive experimentation to produce dynamics in between trivial extinction and unbounded growth. The specific numerical thresholds (birth at exactly 3, survival at 2 or 3) define a narrow operating regime where both stability and change are possible.

GoL exhibits several mathematically significant properties:

- **Turing completeness**: Rendell (2011) constructed a universal Turing machine within GoL, and subsequent work demonstrated arbitrary computation including a Tetris implementation. This means GoL can simulate any algorithm computable by a Turing machine.
- **Undecidability of the halting problem**: There is no general algorithm that, given an arbitrary initial configuration, can determine whether it will eventually reach a stable state. This follows directly from Turing completeness.
- **Maximum propagation speed**: Information cannot travel faster than 1 cell per tick, denoted $c$ by analogy with the speed of light in physics. This constrains the maximum velocity of any self-propagating structure.

The persistent patterns that arise in GoL are classified by their temporal behavior:

| Pattern type | Definition | Examples |
|---|---|---|
| Still life | $C_{t+1} = C_t$ | Block, Beehive, Loaf |
| Oscillator | $C_{t+k} = C_t$ for minimal $k > 1$ | Blinker ($k=2$), Pulsar ($k=3$) |
| Spaceship | $\exists \vec{v} \neq \vec{0}: C_{t+k} = C_t + \vec{v}$ | Glider ($c/4$ diagonal), LWSS ($c/2$ orthogonal) |
| Gun | Produces spaceships periodically | Gosper Glider Gun (period 30) |

The **Hashlife** algorithm (Gosper, 1980s) exploits the hierarchical structure of quadtrees to memoize sub-pattern evolution. By caching the outcome of spatial regions at multiple time scales, it achieves exponential acceleration for sparse, regular patterns. In benchmark settings it has computed configurations to generation $6.3 \times 10^{24}$ in seconds, though its performance degrades for chaotic, non-repeating configurations.

GoL serves as the foundational example for this document: it demonstrates that a transition function with only two states and a 3x3 neighborhood can produce dynamics that are formally undecidable. The implication for simulation design is that computational intractability at the macro level does not require complexity at the micro level.

## Wolfram's Classification (1984)

Stephen Wolfram performed a systematic survey of one-dimensional CAs in the 1980s and observed that their long-term behavior falls into four qualitative classes [@wolfram1984]:

| Class | Dynamics | Dynamical systems analogue |
|---|---|---|
| I | Convergence to a homogeneous fixed point | Point attractor |
| II | Convergence to periodic structures | Limit cycle |
| III | Aperiodic, apparently random dynamics | Strange attractor / chaos |
| IV | Localized persistent structures that propagate and interact | Computation / complex transients |

Class IV is of particular interest because it is the regime where structures analogous to GoL's gliders arise: localized patterns that maintain coherence over time, propagate through space, and interact non-trivially. Wolfram conjectured, and Cook later proved for Rule 110, that Class IV CAs can be Turing complete.

Langton formalized the boundary between these classes by introducing the $\lambda$ parameter: the fraction of the rule table that maps to a non-quiescent state [@langton1990]. Low $\lambda$ corresponds to Class I/II (ordered), high $\lambda$ to Class III (chaotic), and intermediate $\lambda$ to Class IV. This "edge of chaos" picture suggests that complex computation in CAs is a critical phenomenon, arising at a phase transition between order and disorder.

This classification has practical implications for simulation design: the behavior of an emergent system is highly sensitive to the parameter regime. Rules that are too simple produce stasis; rules that are too complex produce noise. Calibrating the rule set to operate near the critical boundary is essential for generating interesting dynamics.

## Application: Fluids in Dwarf Fortress

Dwarf Fortress (Adams, 2002--present) implements water and magma dynamics as a three-dimensional CA. Each tile contains a fluid level $s \in \{0, 1, \ldots, 7\}$. The update rules are:

1. **Gravity**: if the tile below has $s_{\text{below}} < 7$, transfer $\min(s_{\text{current}}, 7 - s_{\text{below}})$ units downward.
2. **Lateral spread**: if the tile below is full ($s = 7$) and a lateral neighbor has $s_{\text{neighbor}} < s_{\text{current}}$, transfer fluid laterally.
3. **Pressure**: resolved via flood-fill from pressure sources, allowing fluid to propagate upward through enclosed channels.

This is an approximation of fluid dynamics that deliberately sacrifices physical accuracy for computational efficiency and player legibility. The 8-level quantization was chosen so that players can visually distinguish fluid levels at a glance. The system does not model viscosity, turbulence, or surface tension, yet it produces behavior that players perceive as fluid-like: flow, accumulation, flooding, and pressure-driven eruption.

The fluid system illustrates a design principle that recurs throughout this document: the gap between a physically accurate model and a *behaviorally adequate* one can be exploited to reduce computational cost without sacrificing the qualitative phenomena that drive emergent narrative.

## From Discrete to Continuous: SmoothLife and Lenia

The binary state space and discrete time of classical CAs are design choices, not mathematical necessities. Two generalizations relax these constraints.

**SmoothLife**, introduced by @rafler2011, replaces the binary cell state with a continuous value $f(\vec{x}, t) \in [0, 1]$ and the neighbor count with continuous integrals over annular regions:

$$m(\vec{x}, t) = \frac{1}{M} \int_{|\vec{u}| < r_i} f(\vec{x} + \vec{u}, t) \, d\vec{u}$$ {#eq:inner-filling}

$$n(\vec{x}, t) = \frac{1}{N} \int_{r_i < |\vec{u}| < r_a} f(\vec{x} + \vec{u}, t) \, d\vec{u}$$ {#eq:outer-filling}

where $m$ is the inner filling (cell interior) and $n$ is the outer filling (annular neighborhood). The hard thresholds of GoL are replaced by smooth sigmoid functions:

$$\sigma_1(x, a) = \frac{1}{1 + \exp\left(-\frac{4(x - a)}{\alpha}\right)}$$ {#eq:sigmoid1}

$$s(n, m) = \sigma_2\left(n, \sigma_m(b_1, d_1, m), \sigma_m(b_2, d_2, m)\right)$$ {#eq:transition}

and the discrete update becomes a continuous time evolution:

$$\partial_t f(\vec{x}, t) = S[s(n, m)] \cdot f(\vec{x}, t)$$ {#eq:smoothlife-continuous}

SmoothLife produces translating structures ("smooth gliders") that can propagate in any direction, not just the 8 directions of a Moore neighborhood. The significance of this result is that it demonstrates emergence does not depend on spatial discretization: continuous dynamics can produce qualitatively similar phenomena.

**Lenia**, introduced by @chan2019, generalizes further by introducing learnable growth functions and parameterized kernels:

$$\frac{dA}{dt} = G(K * A^t)$$ {#eq:lenia}

where $A^t$ is the grid state, $G$ is a growth function (typically a Gaussian-shaped function centered at zero), $K$ is a kernel (typically a ring-shaped radial function), and $*$ denotes 2D convolution. By varying $G$ and $K$ across a parameter space, Chan identified over 400 distinct self-organizing patterns ("species") with qualitatively different behaviors: stable orbits, reproduction, predation, and collective motion. Chan's classification of these patterns into families and genera parallels biological taxonomy.

However, @davis2022 demonstrated that the self-organization in Lenia is critically dependent on numerical precision. The glider *Scutium gravidus*, for example, destabilizes when simulation precision is increased from float32 to float64, when spatial resolution is increased via larger kernels, or when temporal resolution is increased via smaller time steps. This suggests that the "species" discovered in Lenia are not properties of the continuous mathematical system alone, but co-products of the specific numerical approximation used. Davis frames this as a form of numerical embodied cognition: the patterns that emerge are adapted to the computational substrate on which they were discovered.

The practical implication for simulation engine design is that numerical representation choices (float precision, temporal step size, spatial resolution) are not merely performance parameters. They can qualitatively affect which behaviors emerge. This is particularly relevant when choosing data representations for large-scale community simulations.

## Neural Cellular Automata

@sandler2020 replaced the hand-designed transition function with a learned one:

$$s_{t+1} \leftarrow S(s_t, i; \theta)$$ {#eq:neural-ca}

where $S$ is a 3-layer convolutional neural network with residual connections ($S(s) = U(s) + s$) parameterized by $\theta$, and $i$ is a persistent external input (the image to segment). The model was trained via mini-unroll cross-entropy loss to perform image segmentation using fewer than 10,000 parameters.

Several findings from this work are relevant to simulation design. First, the recurrent application of a purely local rule can solve tasks that appear to require global information (e.g., distinguishing object from background across the entire image). Second, the specific spatial filters learned were of modest importance: replacing them with random filters degraded but did not destroy performance, suggesting that the recurrent dynamics of the CA loop itself is the primary source of computational power. Third, the training process exhibited sharp transitions ("regime changes") from unstable to stable dynamics, qualitatively similar to phase transitions in physical systems.

This work establishes a connection between CA theory and deep learning that is relevant to simulation engine design: it raises the possibility that agent interaction rules could be learned from data rather than hand-coded, while preserving the local, synchronous, emergent character of CA dynamics.
