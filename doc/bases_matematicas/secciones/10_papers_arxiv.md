# Verified Academic References {#sec:papers}

The following references were verified through their official sources (arXiv, publisher DOI, or institutional repositories) and form the academic backbone of this document. Each entry includes the formal citation, a summary of the work, and its relevance to the simulation framework discussed here.

## Cellular Automata and Continuous Generalizations

**Lenia: Biology of Artificial Life** [@chan2019]
arXiv:1812.05433. Bert Wang-Chak Chan, published in *Complex Systems*, Vol. 28, No. 3, 2019.
A continuous cellular automaton framework with continuous space, time, and state. The evolution equation $dA/dt = G(K * A^t)$ generalizes the discrete CA update to a continuous domain. By varying the growth function $G$ and kernel $K$, Chan identified over 400 self-organizing pattern types ("species") classified into 18 families. The work demonstrates that CA-like emergence is not an artifact of discretization but a property of the underlying mathematical structure.

**Discretization and Self-Organization in Continuous CA** [@davis2022]
arXiv:2208.09444. Q. Tyrell Davis, University of Vermont, 2022.
Demonstrates that discretization artifacts are essential for self-organization in Lenia. The glider *Scutium gravidus* destabilizes when numerical precision is increased (float32 to float64), when spatial resolution is increased (larger kernels), or when temporal resolution is increased (smaller time steps). The result suggests that emergent patterns in numerical simulations are adapted to the computational substrate on which they are discovered, with implications for the choice of numerical representation in simulation engines.

**SmoothLife: Generalization of Conway's Game of Life** [@rafler2011]
arXiv:1111.1567. Stephan Rafler, 2011.
Replaces binary cell states with continuous values and hard thresholds with sigmoid functions. Introduces inner/outer filling integrals over annular regions and continuous time-stepping via differential equations. Produces translating structures ("smooth gliders") that propagate in arbitrary directions, demonstrating that GoL's emergent properties do not depend on spatial discretization.

**Image Segmentation via Cellular Automata** [@sandler2020]
arXiv:2008.04965. Sandler, Zhmoginov, Luo, Mordvintsev, Randazzo, and Agüera y Arcas (Google AI), 2020.
Formulates the CA update rule as a learnable 3-layer neural network with residual connections (< 10,000 parameters). Demonstrates that purely local information exchange can solve image segmentation, a task that appears to require global context. Reports "regime change" transitions during training (abrupt stability shifts analogous to phase transitions). Establishes a connection between CA theory and deep learning.

## Emergence and Complexity

**Computation at the Edge of Chaos** [@langton1990]
Chris Langton, *Physica D* 42, 1990.
Introduces the $\lambda$ parameter to quantify the location of the "edge of chaos" in CA rule space. Demonstrates that computational capability peaks between ordered (Class I/II) and chaotic (Class III) regimes. Provides the theoretical basis for the observation that interesting emergent behavior requires rules calibrated at a critical boundary.

**Universality and Complexity in Cellular Automata** [@wolfram1984]
Stephen Wolfram, *Physica D* 10, 1984.
Establishes the four-class taxonomy of cellular automata behavior: fixed point (I), periodic (II), chaotic (III), and complex localized structures (IV). Class IV is identified as the regime where computation-like behavior and glider-like structures emerge. Foundational classification still in use in CA research.

## Agent-Based Modeling and Social Simulation

**Growing Artificial Societies** [@epstein1996]
Joshua Epstein and Robert Axtell, MIT Press / Brookings Institution Press, 1996.
The Sugarscape model: autonomous agents on a 2D grid harvest resources, reproduce, trade, and fight under simple local rules. Demonstrated emergent wealth inequality (Gini ~ 0.5), cultural differentiation, combat, and trade networks. Foundational text for agent-based modeling in social science.

**Dynamic Models of Segregation** [@schelling1971]
Thomas Schelling, *Journal of Mathematical Sociology*, 1971.
Agents with mild preference for similar neighbors (threshold as low as 30%) spontaneously produce highly segregated neighborhoods on a 2D grid. Demonstrates that individual micromotives can produce collective macro-outcomes that no individual intended. One of the most influential results in agent-based social modeling.

**The ODD Protocol for Agent-Based Models** [@grimm2020]
Volker Grimm et al., *Journal of Artificial Societies and Social Simulation*, 2020.
A standardized protocol (Overview, Design, Details) for describing agent-based models. Ensures models are replicable and comparable. Provides a disciplined framework for simulation design with explicit specification of entities, state variables, process scheduling, and submodel mathematics.

## Game Theory and Spatial Dynamics

**Evolutionary Games and Spatial Chaos** [@nowak1992]
Martin Nowak and Robert May, *Nature* 359, 1992.
Demonstrates that placing Prisoner's Dilemma on a spatial lattice fundamentally changes evolutionary outcomes: cooperators form clusters that resist defector invasion, producing spatial chaos and fractal patterns. The spatial structure itself enables cooperation that is impossible in well-mixed populations. Directly relevant to how spatial layout affects social dynamics in community simulation.

## Procedural Generation

**WaveFunctionCollapse** [@gumin2017]
Maxim Gumin, GitHub repository, 2016--2017.
A constraint satisfaction algorithm for generating tile maps that satisfy adjacency constraints. Uses Shannon entropy as a variable-ordering heuristic to select the next cell to collapse. Operates in tile-based mode (explicit constraints) and overlapping mode (constraints learned from exemplar images). Widely adopted in procedural generation for games.

**Analysis of WFC as CSP** [@karth2017]
Isaac Karth and Adam Smith, *Proceedings of the 12th International Conference on the Foundations of Digital Games*, 2017.
Formalizes WFC as constraint satisfaction with arc consistency checking and minimum-remaining-values variable ordering. Connects the algorithm to the academic CSP literature and provides theoretical grounding for its effectiveness.

## Pathfinding

**The JPS Pathfinding System** [@harabor2012]
Daniel Harabor and Alban Grastien, *Proceedings of the 5th Symposium on Combinatorial Search (SOCS)*, 2012.
Accelerates A* on uniform-cost grids by identifying "jump points"---nodes where the optimal path must turn---and skipping intermediate nodes. Reduces expanded nodes by up to 100x compared to standard A* on uniform grids. Limited to uniform-cost grids; does not apply to variable-cost terrain.
