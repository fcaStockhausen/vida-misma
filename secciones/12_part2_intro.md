# Part II: Design Specification — La Vida Misma {#sec:part2}

Part I surveyed the computational and theoretical substrate of agent-based simulation. Part II commits to a concrete architecture. *La Vida Misma* is a factory-survival simulation in which autonomous agents must sustain a productive facility while pursuing individual self-actualization. Every design decision below traces to a foundation laid in the preceding sections.

**Spatial model.** A discrete 2D grid serves as the world substrate, combining cellular-automaton rules (§1) with procedurally generated terrain and room layouts (§2). Agent movement across the grid relies on the pathfinding algorithms reviewed in §4.

**Entity-component architecture.** The simulation core uses the EnTT entity-component-system (ECS) library under C++20, following the technology-stack rationale of §9. Entities represent agents, factory machines, resource piles, and structural tiles; components encode state (inventory, needs, fatigue); systems implement per-frame logic in cache-friendly iteration patterns.

**Utility AI and motivational tension.** Each agent evaluates candidate actions through a utility-AI scoring function (§3) informed by a Maslow-style need hierarchy. The central design tension is structural: factory-survival objectives (maintain output, repair machinery, harvest resources) compete against self-actualization drives (social bonding per §6, rest, creative expression). Nowak-May cooperation dynamics (§6.1) and Schelling segregation pressures (§6.2) emerge from agent proximity on the grid, making spatial layout inseparable from social outcome.

**Production chain.** The economy is a three-stage pipeline: GATHER raw materials from the environment → BUILD or repair factory infrastructure → WORK assigned stations to generate output. This loop recurs each simulation tick and forms the objective backbone against which subjective agent preferences rebel.

**Guiding principles.** The design philosophy draws from the Dwarf Fortress-informed principles of §7 (emergence over scripting, systemic interaction, narrative density) and the complementary algorithms of §8 (behavior trees for fallback logic, blackboards for shared knowledge). Theoretical grounding comes from the ODD protocol for model documentation (§3) and the empirical validation strategies surveyed in §10.

The sections that follow specify each subsystem in detail, from the grid representation through agent cognition to the rendering pipeline.
