# Adversarial Utility Agents for Optimization Decision Pipelines

> **Scope:** This is a general theoretical note with historical links to utility
> simulation. It does not define the canonical ontology of *La Vida Misma*.
> In the current simulation the factory is an indifferent institution governed by
> physical rules, not an Evaluator, strategic opponent or minimax player. The old
> strategic policy survives only as `external.policy_variant = 0` for explicit
> historical A/B comparison. For the current model, use
> `doc/plans/2026-07-21-alineacion-diseno-implementacion.md`.

## The Problem Pattern

A recurring architecture in quantitative systems:

1. A **fast optimization step** (seconds) that solves a well-defined parametric problem.
2. A **slow decision step** (minutes, sometimes human-in-the-loop) that determines *what* to optimize: which data to include, which constraints to relax, which regime applies, which model family to use.

The bottleneck is not the solver. The bottleneck is the **decision chain around the solver**. Current practice often delegates these decisions to an LLM-based agent that reasons about the problem conversationally. This works but is expensive, non-deterministic, and hard to audit.

The hypothesis: many of these decision chains can be decomposed into a small number of agents with **opposed utility functions** operating in a game-theoretic framework. The "intelligence" emerges from the structure of their incentives, not from a large language model.

## Theoretical Foundations

This framework draws from several established bodies of theory. This section maps each foundation to the specific mechanism it supports.

### Utility Theory and Local Decision-Making

Utility-based agent architectures used in community simulation provide a useful
formal basis for individual choice. Each agent evaluates candidate actions by
computing scalar utility; a simple deterministic formulation is:

$$a^* = \arg\max_{a \in A} U(a)$$

Implementations may choose this argmax or sample a softmax/Boltzmann distribution.
The current *La Vida Misma* agents use the latter. The general property relevant
here is **composability**: a need or action can add a utility contribution without
replacing the selection architecture.

In the adversarial optimization context, the same mechanism can be reused, but
with a critical difference: each optimization agent's utility function is
**opposed** to the other's. This is an architectural analogy to utility-based
simulation, not a claim that a simulation environment or institution is itself
an adversarial agent.

### Emergence and Wolfram's Classification

Wolfram's four-class taxonomy of cellular automata behavior (Section 1.4 of *Mathematical Foundations*) establishes a principle relevant to adversarial systems: the most interesting behavior occurs at the boundary between order (Class I/II) and chaos (Class III). Langton's $\lambda$ parameter quantifies this boundary.

In an adversarial utility system, the same principle applies. If both agents have identical utility functions (order), the system trivially converges to a single point. If the utility functions are completely opposed with no common ground (chaos), the system never converges. The useful operating regime is the **edge of chaos**: opposed but not irreconcilable utilities, where the equilibrium exists but is non-trivial to find.

This suggests a design principle: the utility functions of adversarial agents should share *some* common terms (a shared loss landscape) while differing on *specific* terms (regularization direction, constraint strictness, risk tolerance). Complete opposition produces cycling; complete alignment produces trivial solutions. The design problem is calibrating the opposition.

### Agent-Based Modeling: Autonomy, Locality, Heterogeneity

The ABM framework formalized in the ODD protocol (Grimm et al., 2020) identifies four defining properties of agent-based systems:

1. **Autonomy**: each agent maintains internal state and makes decisions independently.
2. **Heterogeneity**: agents differ in their attributes and preferences.
3. **Locality**: agents interact with their local environment, not the global system state.
4. **Path dependence**: the system's state depends on the sequence of events.

These properties transfer directly to adversarial optimization agents. The Proposer agent is autonomous (it proposes configurations based on its own utility). The Evaluator agent is autonomous (it critiques based on different criteria). They are heterogeneous (different utility functions). Their interaction is local (each sees only the other's last move, not the full internal state). And the system is path-dependent (the sequence of proposals matters, not just the initial conditions).

The connection to Sugarscape (Epstein and Axtell, 1996) is instructive: in Sugarscape, wealth inequality emerges from agents with identical rules operating in a heterogeneous environment. In adversarial optimization, good configuration selection emerges from agents with different rules operating on the same data. The mechanism of emergence is the same: no single agent "knows" the answer; the answer emerges from the interaction.

### Game Theory: Spatial and Non-Spatial

Nowak and May (1992) demonstrated that placing game-theoretic interactions on a spatial lattice fundamentally changes outcomes: cooperators form clusters that resist defector invasion. The key insight is that **spatial structure creates the possibility of cooperation** that would be impossible in a well-mixed population.

In adversarial optimization, the "spatial structure" is the **configuration space**: the set of possible data subsets, model families, and constraint settings. When the Proposer and Evaluator interact over this space, their equilibrium depends on the topology of the space. A discrete configuration space (finite model families, binary data inclusion) produces different dynamics than a continuous one (real-valued parameters, fractional data weights).

The Schelling model (1971) provides another connection: agents with mild preferences for similarity spontaneously produce extreme segregation. In optimization, agents with mild preferences for certain configurations can spontaneously "segregate" into stable regions of configuration space, producing stable equilibria without any centralized coordination.

### Stress and Feedback Loops

The stress-utility feedback loop in Dwarf Fortress provides a structural template:

1. Needs decay over time.
2. Rising needs increase the utility of need-satisfying actions.
3. Personality modulates which needs dominate.
4. Completing an action generates events that modify stress and relationships.
5. Stress modulates personality, changing future priorities.

In adversarial optimization, the analogous loop is:

1. The Proposer generates a configuration.
2. The Evaluator identifies weaknesses (critique).
3. The critique modifies the Proposer's proposal strategy (not the utility function, but the search strategy within it).
4. A new configuration is proposed.
5. If the critique converges to zero, the system has reached equilibrium.

The structural similarity is that both are **closed-loop systems** where the output of one process becomes the input to another, and the system evolves toward a stable state defined by the interaction of the components.

### Opinion Dynamics: Convergence and Polarization

The DeGroot and bounded confidence models from opinion dynamics provide formal tools for analyzing multi-agent systems:

**DeGroot convergence**: when agents update their state as a weighted average of neighbors, opinions converge to consensus under mild conditions (strongly connected, aperiodic graph). In adversarial optimization, this corresponds to the case where both agents' utility landscapes share enough structure that their best-response dynamics converge.

**Bounded confidence polarization**: when agents only average opinions within a threshold $\epsilon$, the population fragments into clusters. In adversarial optimization, this corresponds to the case where the utility functions are sufficiently opposed that no single equilibrium exists, and the system oscillates between competing configurations.

The $\epsilon$ parameter is directly analogous to the "opposition strength" in the utility functions: large $\epsilon$ (broad agreement) produces convergence; small $\epsilon$ (narrow agreement) produces fragmentation.

### Entity-Component-System Architecture

The ECS pattern used in simulation engine design provides the implementation framework. Each adversarial agent is an entity with components:

- **UtilityComponent**: stores the agent's utility function parameters (weights, penalties, constraints).
- **ProposalComponent**: stores the agent's current proposal and proposal history.
- **CritiqueComponent**: stores the agent's evaluation of the last proposal received.
- **StateComponent**: stores the agent's internal state (iteration count, convergence status).

Systems operate on these components independently:

- **ProposalSystem**: generates new proposals based on utility + last critique.
- **EvaluationSystem**: evaluates proposals against utility + held-out criteria.
- **ConvergenceSystem**: checks whether the critique falls below tolerance.

The advantage of ECS here is the same as in simulation: systems are independent, testable in isolation, and new agents can be added by composing existing component types.

## The Core Mechanism: Competitive Utility Maximization

With the theoretical foundations established, the mechanism can be stated precisely.

Two agents interact over a configuration space $\Theta$:

**Proposer** $A_p$: selects $\theta \in \Theta$ to maximize:

$$U_p(\theta) = -\text{loss}(\theta; D_{\text{train}}) - \lambda_p \cdot \text{complexity}(\theta)$$

**Evaluator** $A_e$: evaluates $\theta$ against held-out criteria:

$$U_e(\theta) = \text{loss}(\theta; D_{\text{test}}) + \lambda_e \cdot \text{violation}(\theta, C)$$

The system seeks a configuration $\theta^*$ such that:

$$\theta^* \in \arg\min_\theta \max\{U_p(\theta), U_e(\theta)\}$$

As written, this is a robust scalar optimization over one configuration, not yet a
two-player zero-sum game with separate strategy variables. Compactness and
continuity guarantee that its extrema are attained, but do not guarantee a saddle
point. A minimax equality needs additional structure, such as compact convex
strategy sets and a convex-concave payoff, or finite action sets with mixed
strategies. Alternating best responses are an algorithmic choice and can cycle
even when an equilibrium exists.

**Important caveat for discrete implementations.** A threshold branch may be
continuous at its boundary or may introduce a jump; the code must be inspected
rather than classifying every gate as discontinuous. Historical audits of grid
simulations, including this project's earlier utility code, do not turn those
simulations into two-player zero-sum games. For a discounted zero-sum stochastic
game with finite states and actions, Shapley's 1953 framework supplies a value and
stationary optimal strategies without a continuity assumption on a continuous
action space.

The structural equivalences across domains:

| Field | Name | Proposer role | Evaluator role |
|---|---|---|---|
| Game theory | Stackelberg game | Leader (commits first) | Follower (best-responds) |
| Microeconomics | Principal-agent | Agent (takes action) | Principal (evaluates outcome) |
| Machine learning | GAN | Generator | Discriminator |
| Robust optimization | Adversarial formulation | Minimizer | Maximizer |
| Utility simulation analogy | Needs-utility loop | Agent selects an action | Environment provides consequences |

The last row is an implementation analogy only: a simulation agent acts and the
physics/social systems return consequences. Those systems need not optimize an
opposed utility and should not be called an Evaluator in the game-theoretic sense.
An adversarial optimization system deliberately adds that opposed objective by
reducing the world to a validation function and the agent to a proposer.

## Architectural Patterns

### Pattern 1: Fitter vs. Validator

**When to use**: The decision chain involves selecting a data subset, applying filters, or choosing between parametric families.

**Theoretical grounding**: This is a Stackelberg game where the Fitter (leader) commits to a configuration and the Validator (follower) best-responds by selecting the hardest test conditions. The equilibrium is the configuration that minimizes the maximum regret over test conditions, analogous to the way Dwarf Fortress agents select actions that minimize maximum need deficit.

**Structure**:

```
Fitter agent:
  - Proposes: (data_mask, model_family, initial_params)
  - Utility: -in_sample_error - lambda_1 * |params|
  - Action space: discrete (model selection) + continuous (params)

Validator agent:
  - Proposes: (test_split, penalty_weights)
  - Utility: +out_of_sample_error + lambda_2 * constraint_violations
  - Cannot modify the fit, only the evaluation criteria
```

**Iteration**: Fitter proposes a configuration. Validator selects the hardest test conditions. Fitter re-proposes. Equilibrium is reached when Fitter cannot reduce in-sample error without Validator catching it out-of-sample.

**What this replaces**: The human (or LLM) decision of "should I exclude this outlier?", "should I use 3-factor or 4-factor?", "is this regime change real or noise?"

### Pattern 2: Satisficer vs. Opportunist

**When to use**: The decision chain involves trade-offs between competing objectives (speed vs. accuracy, parsimony vs. fit, robustness vs. responsiveness).

**Theoretical grounding**: This pattern maps to Schelling's segregation model in the following sense: each agent has a "threshold" of acceptability. The Satisficer's threshold is a hard constraint (all requirements must be met). The Opportunist's threshold is a performance gradient (always push for more). The equilibrium is the boundary between acceptable and optimal, analogous to the way Schelling's agents settle at the boundary between their tolerance threshold and the neighborhood composition.

It also connects to the Wolfram classification: the Satisficer represents the ordered regime (Class I/II, converge to stability), the Opportunist represents the chaotic regime (Class III, always pushing). The equilibrium is the edge of chaos (Class IV) where the configuration is both stable enough to be acceptable and dynamic enough to perform well.

```
Satisficer agent:
  - Utility: +1 if ALL constraints are met, 0 otherwise
  - Proposed actions: tighten constraints, add safety margins
  - Conservative bias

Opportunist agent:
  - Utility: -error * weight_objective
  - Proposed actions: relax constraints, exploit patterns, use more data
  - Aggressive bias
```

**Iteration**: Opportunist pushes for maximum performance. Satisficer blocks proposals that violate hard constraints. The equilibrium is the most aggressive configuration that still satisfies all requirements.

**What this replaces**: The conversation "how much risk is acceptable here?", "can we relax this threshold?", "what's the minimum viable fit quality?"

### Pattern 3: Regime Detector vs. Model Selector

**When to use**: The optimization runs repeatedly on time-varying data, and the decision is whether the underlying regime has changed enough to warrant a different model or parameterization.

**Theoretical grounding**: This maps to the opinion dynamics models. The Regime Detector plays the role of the bounded confidence threshold $\epsilon$: it determines whether the distance between the current data distribution and the training distribution exceeds a tolerance. The Model Selector plays the role of the DeGroot averaging: it incorporates the regime signal into its decision but weights it against the switch cost (inertia). The switch cost is analogous to the weight $w_{ii}$ in the DeGroot model (how much an agent trusts its own current state vs. new information).

```
Regime Detector:
  - Monitors: statistical distance between recent data and training distribution
  - Utility: +1 if regime_change_detected correctly, -penalty if false alarm
  - Action: flags regime change, proposes new data window

Model Selector:
  - Receives: regime flag + data window
  - Utility: -error on proposed window - lambda * model_switch_cost
  - The switch cost penalizes unnecessary model changes
```

**Iteration**: Detector signals when the data distribution has shifted. Selector decides whether the shift is large enough to justify switching models (paying the switch cost) or whether the current model is still adequate.

**What this replaces**: The human judgment of "has the market regime changed?" or "should I recalibrate?" or "is this structural break real?"

## Formal Properties

### Convergence

For finite two-player zero-sum games, von Neumann's minimax theorem guarantees a
value in mixed strategies. Continuous-action extensions require conditions such
as compact convex strategy sets, continuity, and convex-concave payoff structure.
These are equilibrium-existence conditions, not a proof that naive alternating
best responses converge. Best-response trajectories can cycle and need a separate
algorithmic convergence argument. DeGroot consensus has different linear-system
assumptions and is not the same criterion.

**Discrete / discontinuous adversarial regime.** A genuine discounted two-player
zero-sum stochastic game with finite state and action sets falls under Shapley's
framework and has a value $V(s)$ with stationary optimal strategies. This result
cannot be transferred merely because an agent-based simulation is discrete; the
simulation must first satisfy the finite, discounted, two-player and opposed-payoff
assumptions.

### When the System Does NOT Converge

- **Non-convex utility landscapes** (multiple local equilibria). The system may cycle between proposals. This is analogous to Wolfram's Class III (chaotic) regime: the rules produce perpetual change without stabilization.
- **Unbounded action spaces** (the Fitter can always propose a more complex model). This violates the compactness/ finiteness requirement of both von Neumann and Shapley.
- **Non-stationary data** (the equilibrium shifts before convergence). This is the online learning setting: the target moves faster than the system can adapt.
- **Discontinuous utilities in a continuous-action framing.** A genuine jump can
  invalidate a selected continuous minimax theorem, but it does not by itself
  justify switching to Shapley's finite discounted stochastic-game framework.

Mitigations: limit iteration count (truncated best-response), add a "referee" agent that detects cycling and forces a decision, or use simulated annealing on the proposal step (occasionally accept worse proposals to escape local equilibria, analogous to introducing noise at the edge of chaos).

### Computational Cost

Each iteration requires one optimization step (the expensive part, ~20s) plus one evaluation step (typically cheap, ~1s). If the system converges in $k$ iterations, total cost is $O(k \times 20\text{s})$. For well-structured problems, $k$ is typically 5--15. This compares favorably to an LLM agent that may make 3--5 tool calls per decision, each requiring a full inference pass.

The cost profile is analogous to the simulation engine's tick-based architecture: each "tick" (iteration) involves all agents acting once, and the computational cost is dominated by the most expensive subsystem (the optimizer, just as pathfinding dominates simulation cost).

## Where This Replaces an LLM Agent

| Decision type | LLM agent | Utility agents | Theoretical basis |
|---|---|---|---|
| "Exclude this outlier?" | Reasons about context | Fitter includes, Validator tests with and without | Stackelberg best-response |
| "Which model family?" | Enumerates options | Fitter proposes, Validator penalizes complexity | Minimax over model space |
| "Has the regime changed?" | Reads statistics, reasons | Regime Detector fires on statistical distance | Bounded confidence threshold $\epsilon$ |
| "Is this fit acceptable?" | Judges qualitatively | Satisficer checks constraints | Binary utility (Schelling threshold) |
| "What should I tell the user?" | Generates natural language | **Use an LLM here** | Language is not a utility optimization problem |

The last row is the critical insight: utility agents handle **decision-making under well-defined objectives**. LLMs handle **communication, ambiguous reasoning, and novel situations**. The boundary is where the decision criteria become explicit enough to write as a utility function.

## Reference Models from the Literature

| Model | Source | Connection to simulation foundations |
|---|---|---|
| Minimax / Nash equilibrium | von Neumann (1928), Nash (1950) | Formal basis for adversarial convergence |
| Principal-agent theory | Holmström (1979) | Incentive design with hidden information |
| GANs | Goodfellow et al. (2014) | Practical adversarial training |
| Stackelberg games | von Stackelberg (1934) | Sequential leader-follower dynamics |
| Multi-objective optimization | Miettinen (1999) | Pareto frontiers for conflicting utilities |
| Coevolutionary algorithms | Potter and De Jong (2000) | Population-based adversarial optimization |
| Online learning / regret minimization | Cesa-Bianchi and Lugosi (2006) | Non-stationary adaptation |
| Mechanism design | Myerson (1981) | Designing rules for selfish agents to produce optimal outcomes |
| Wolfram classification | Wolfram (1984) | Edge-of-chaos as the operating regime for adversarial systems |
| Schelling segregation | Schelling (1971) | Threshold-based agent dynamics producing emergent structure |
| DeGroot opinion dynamics | DeGroot (1974) | Convergence conditions for multi-agent state updates |
| Bounded confidence | Hegselmann and Krause (2002) | Polarization and fragmentation in agent systems |
| Sugarscape | Epstein and Axtell (1996) | Emergent macro-phenomena from micro-level agent rules |
| Utility AI (Dwarf Fortress) | Adams (2002--present) | Composable utility functions for autonomous agents |

## A Minimal Implementation Sketch

```
state = initial_configuration()

for iteration in 1..max_iterations:
    # Proposer proposes
    proposal = proposer.best_response(state, evaluator.last_critique)
    
    # Evaluator evaluates
    result = optimizer.solve(proposal)          # The 20-second step
    critique = evaluator.evaluate(result, held_out_data, constraints)
    
    # Check equilibrium
    if critique.score < tolerance:
        return proposal, result
    
    state = update(state, proposal, critique)

# Fallback: return best proposal found
return best_proposal, best_result
```

The `proposer` and `evaluator` are not neural networks. They are **rule-based agents** that compute utilities over structured action spaces, exactly as Dwarf Fortress agents compute utilities over available actions. The "intelligence" is in the utility function design, not in a learned model.

## Design Heuristics

1. **Start with the utility functions, not the agents.** Write down explicitly what each agent wants. If you cannot write it as a scalar function, the decision is too ambiguous for this framework and probably needs an LLM (or a human). This is the same principle as the ODD protocol's requirement for explicit submodel specification.

2. **Calibrate opposition strength using the edge-of-chaos principle.** If the system converges too fast (first iteration), the utilities are too aligned. If it never converges, they are too opposed. The useful operating range is in between, analogous to Wolfram's Class IV.

3. **Make the action space discrete where possible.** Model selection, data inclusion/exclusion, constraint activation -- these are binary or categorical decisions. Discrete spaces converge faster and are easier to audit. This follows from the Nowak-May result: spatial structure (here, discrete configuration topology) changes the dynamics of interaction.

4. **Add a switch cost for online systems.** This is the DeGroot self-weight $w_{ii}$: how much an agent trusts its current state vs. new signals. High switch cost = high inertia = stable behavior but slow adaptation. Low switch cost = responsive but potentially unstable.

5. **Log every proposal-critique pair.** The audit trail is the primary advantage over an LLM. Every decision is a (proposal, critique, utility_values) triple that can be inspected and reproduced.

6. **Use an LLM as the interface, not the engine.** The LLM translates user requests into utility function parameters, and translates equilibrium outcomes into natural language. The decision-making itself is deterministic and cheap.
