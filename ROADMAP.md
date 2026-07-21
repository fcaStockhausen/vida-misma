# La Vida Misma — Project Roadmap

A factory simulation where agents inhabit a factory they did not build, do not
control, and do not understand. The factory demands output. The agents want to
live. Every mechanic exists to sharpen that tension.

---

## Current State

**~11,000 LOC** across 24 source files. Compiles clean (Clang, C++20, CMake).
Two targets: `vida_batch` (headless), `vida_gui` (SDL2 2.5D isometric).
48 agents. Director system (CALM / NORMAL). Narrative journals.

### Emergence Redesign (2026-07 — in progress)

A critical audit found the codebase was *The Sims class* (scripted with an
emergent veneer), not the Schelling/Boids class the design docs claim: ~740
magic-number constants, 121 threshold gates in `sim_utility.cpp` alone, and a
scripted 5-state stress FSM. The redesign replaces hardcoded overrides with
correctly-shaped utility curves so behavior emerges from simple rules.

**Full plan & status:** see [`doc/plans/2026-07-21-emergence-redesign.md`](doc/plans/2026-07-21-emergence-redesign.md)

```
Phase 1 (shipped):  De-duplication refactor — 4 helpers extracted,
                    behavior-neutral (10/10 md5-identical verification).
Phase 2.1 (shipped): Survival urgency SIGMOID — A/B tested 4 curve variants,
                    sigmoid won 2.5x survival (43 vs 17 alive/48 avg).
                    Eliminates 3 patch mechanisms: critical_spike,
                    eat_weight boost, HARD SURVIVAL OVERRIDE.
Phase 2.2 (shipped): Niche dampening removed — role diversity now emerges
                    from Bonabeau thresholds + personality, no clamp needed.
Phase 2.3 (reverted): Compliance sigmoid caused production collapse in 1/5
                    seeds. Negative result: the meaning>0.7 kink is
                    load-bearing, NOT a patch. Documented.
Phase 3 (deferred):  Stress FSM → continuous modifiers.
Phase 4 (deferred):  Documentation sync (docs still describe legacy behavior).
```

Cascading effect of Phase 2: colony now recovers `factory_health` to 0.95+
(was 0.00), sabotages drop 2-5×, and **factions can emerge** (the colony
survives long enough). Config knob `urgency.curve_variant` (0=legacy,
3=sigmoid default) preserves the old path for reproducibility.

### Production Chain Fixes (Phase 1 — implemented)

```
c_mat visibility: assess() now sums agent-carried construction_material
  → planner routes Output workers correctly (was blind to inventory c_mat)
Passive output pickup: agents passing Output machines pick up stuck output
  → unblocks output trapped in machines (Priority 4 fallback)
  → machines now drain to 0.00 instead of hoarding 0.33-0.38
raw_gap gate: GATHER utility scales with colony raw material supply
  → when banked > 5x per-agent need, GATHER collapses to 20% floor
  → frees agent time from GATHER into WORK (was 24-53%, now 5-15%)

Result (NORMAL mode, 1500 ticks):
  Phase 1 (c_mat + pickup):    quota 25% → 29%
  Phase 2 (+ raw_gap gate):    quota 29% → 42% avg (seed 42)
  WORK @ 1500 ticks            13 → 22
  GATHER stabilized            10-33 → 5-15
  7-seed sweep (1000 ticks):   3/7 ≥ 94%, 5/7 ≥ 52% (was 0% all)
```

### Architecture of Desire (implemented)

```
Maslow hierarchy gates:
  hunger/rest high ──► survival overrides everything
  hunger/rest low  ──► higher needs activate

Focus system (Dwarf Fortress):
  higher needs unmet ──► WORK/GATHER/BUILD lose utility (distracted)
  only fires when food supply is healthy

Threshold gates (The Sims):
  CREATE needs expression > 0.15
  SOCIALIZE needs social > 0.15
  EXPLORE needs purpose > 0.25
  Below threshold = near-zero utility. Above = ramps hard.

Purpose decoupled from work:
  CREATE satisfies purpose (+0.006/tick)
  SOCIALIZE satisfies purpose (+0.003/tick)
  WORK satisfies purpose (+0.004/tick) — no longer the sole source

Personality-modulated action duration:
  Artisan stays 60 ticks in CREATE, Foreman 30+15*comp in WORK
  Actions don't change every tick — behavior is visible and readable
```

### Congregation Spaces (implemented)

```
WFC pre-builds a large EatingZone (5×3 to 7×4 tiles) at map center.
Adjacent Storage provides food access.

Social mechanics:
  SOCIALIZE targets EatingZone as congregation point
  Communal eating: 3+ agents at EatingZone → social satisfaction bonus
  Congregation bonus: 2+ nearby agents → satisfaction +50%, 3+ → +100%
  Passive social: agents near 3+ others get small social satisfaction
  Artifact attraction: CREATE targets OpenSpace near existing artifacts
    → emergent galleries/studios where artists cluster
```

Culture diagnostic correlations (48 agents, calm mode, 2000 ticks):

| Trait → Action | r (Pearson) | Status |
|---|---|---|
| artistry → CREATE | **0.86-0.92** | ✅ Strong |
| curiosity → EXPLORE | **0.65-0.89** | ✅ Strong |
| gregariousness → SOCIALIZE | **0.30-0.42** | ✅ Correct |
| compliance → WORK | 0.23-0.41 | ⚠️ Variable |

### Narrative System (implemented)

```
Chronicle dedup: same agent+type within 50 ticks is suppressed
Event grouping: consecutive same-type events collapsed into summaries
  "created 55 artworks" instead of 55 identical entries

agent_arc generates archetype-based narrative:
  "Artisan. A creator — made 60 artifacts in 997 ticks."
  "Worker. A rebel — struck back 5 times against the machine."
  "Networker. A giver — shared food 3 times. The community held."

agent_journal shows collapsed life arc:
  [    0] Artisan — appeared with something to make
  [  166] created 55 artworks
  [  907] struck at the machine
  [  997] struck at the machine

Per-archetype spawn phrases:
  "Survivor — stumbled in — just wants to see tomorrow"
  "Foreman — arrived — sleeves rolled, ready to run the floor"
```

---

## Completed Features

### Core Simulation
- [x] ECS architecture via EnTT (header-only)
- [x] 60x40 grid with WFC procedural layout
- [x] Tick-based simulation loop, seeded RNG
- [x] Config-driven parameters via TOML
- [x] 48 agents with balanced archetype distribution

### Agent Systems
- [x] Utility AI with 12 action types (GATHER, BUILD, WORK, EAT, REST, SOCIALIZE, CREATE, EXPLORE, GET_FOOD, MAINTAIN, DISMANTLE, SABOTAGE)
- [x] Need decay: hunger, rest, social, expression, purpose, meaning
- [x] Skill system: factory_work, domestic, artistic, social_skill with XP
- [x] Disease system: immune response, hunger multiplier, recovery
- [x] Personality-modulated action stickiness (Artisan stays in CREATE, Foreman in WORK)
- [x] Focus system: unmet higher needs reduce WORK/GATHER/BUILD utility
- [x] Threshold gates: cultural actions activate above need threshold
- [x] Purpose decoupled from work (CREATE and SOCIALIZE also satisfy purpose)
- [x] `infra_gap` reform: need-based formula, not total-tiles-based. BUILD collapses to 5-13% when needs met.

### Stress & Trauma
- [x] 5 stress states: NORMAL → DISSOCIATED → EUPHORIC → BROKEN → REDEEMED
- [x] Permanent trauma, noncompliance tracking
- [x] Sabotage action, redemption arc, suicide risk
- [x] 4 death causes: starvation, exhaustion, breakdown, factory collapse
- [x] Cultural artifacts: CREATE spawns mood-boosting objects
- [x] Hidden spaces: factory seals overused rest areas
- [x] 6 personality archetypes with per-agent jitter

### Production Chain (3-tier)
- [x] MaterialsMachine: raw_material → construction_material
- [x] OutputMachine: construction_material → output
- [x] Agent hauling: output → Exit-adjacent Storage
- [x] Conveyor belt transport (Exit-only output deposit)
- [x] External quota system (radius 3 drain from Exit)
- [x] Factory health, machine breaks, conveyor condition decay
- [x] `machine_connected_to_exit()` flow verification
- [x] Dynamic routing based on construction_material supply

### Pathfinding
- [x] A* with per-agent `PathCache` (invalidates on target change or 20 ticks)
- [x] All non-Wall tiles walkable, divergence recovery

### Social Fabric
- [x] Relationship graph: familiarity, trust per agent pair
- [x] Social learning, food sharing, collaborative BUILD/WORK
- [x] Emotional contagion, influence/leadership, mood
- [x] Grief cascade, relationship decay
- [x] Opinion dynamics (bounded confidence + DeGroot)
- [x] Faction formation and trust modulation

### Congregation Spaces
- [x] WFC pre-builds large EatingZone (5×3 to 7×4) at map center with Storage
- [x] SOCIALIZE targets EatingZone as congregation point
- [x] Communal eating bonus: 3+ agents eating together → social satisfaction
- [x] Passive social satisfaction: agents near 3+ others get small social tick
- [x] Congregation count in SOCIALIZE: more agents = higher satisfaction
- [x] Artifact attraction: CREATE targets tiles near existing artifacts

### Director System
- [x] CALM mode: no quota, no deterioration, no restructure — pure observation
- [x] NORMAL mode: standard factory pressure with quota escalation

### Narrative & Chronicle
- [x] Chronicle dedup: same agent+type within 50 ticks suppressed
- [x] Event grouping: consecutive same-type collapsed into summaries
- [x] agent_arc: archetype-based narrative ("A creator — made 60 artifacts")
- [x] agent_journal: collapsed life arc with narrative phrasing
- [x] Per-archetype spawn phrases
- [x] Per-event-type first-person narrative texts
- [x] CREATE/EXPLORE/SOCIALIZE generate chronicle events
- [x] "ate at work" suppressed from chronicle (mechanical penalty still fires)

### Interface
- [x] Headless batch runner: run, calm, culture, story, agent, analysis, map, jsonl
- [x] SDL2 2.5D isometric GUI (120ms/tick for readability)
- [x] Config path fallback (works from build/ directory)

---

## In Progress / Needs Work

### Emergence Redesign (continuation — see doc/plans/2026-07-21-emergence-redesign.md)
- [ ] **Phase 3: Stress FSM → continuous** — replace the 5 scripted qualitative states
      (NORMAL/DISSOCIATED/HOSTILE_EUPHORIA/BROKEN/REDEEMED) with smoothstep modifiers
      on `stress.value`. REDEEMED moves to an event flag. Honors `07_principios_diseno.md:7`.
- [ ] **Phase 4: Documentation sync** — update `14_inhabitants.md` (argmax→softmax),
      `16_social_fabric.md` (centrality claim vs actual influence formula), remove
      the non-existent `w_fear` reference, add implementation-status blocks.
- [ ] **Faction emergence is weak** — even with 2.5x better survival (Phase 2), factions
      still only form in 1-2/5 seeds. The colony survives but trust accumulation is too
      slow. Diagnose before assuming the faction-formation thresholds are correct.

### Balance Tuning
- [x] ~~GATHER dominates~~ — further alleviated by the sigmoid survival curve (Phase 2.1):
      agents eat better → less stress → less sabotage → factory recovers.
- [ ] **Output logistics**: output produced but trapped in machines until agents pass by.
      Passive pickup helps (Phase 1) but throughput still caps quota.
- [ ] **c_mat transit visibility**: agents with c_mat in inventory trigger `prefer_output`
      routing. Depositing c_mat to Storage breaks this — needs a hauling action instead.
- [ ] **SOCIALIZE correlation variable** (r=0.08 to 0.42 across seeds) — layout-dependent
- [ ] **Seed variance**: layout-dependent outcomes (food/machine placement)
- [ ] **Narrative arc spam**: REDEMPTION/SABOTAGE still repeat in narrative summary

---

## Planned Features (by priority)

### Priority 1: Construction Recipes & Collaborative Building

Currently agents build blindly — the WFC generator places machine frames and
agents build whatever is nearest. There is no concept of "what the colony
needs next" or "where should this go."

Design (inspired by RimWorld blueprints and DF work orders):

- [ ] **Recipe system**: each structure has a recipe (input materials + skill
      requirement + output). Agents evaluate recipes against colony needs:
      ```
      FoodMachine:   2 raw_material + ScrapPile/FoodSource tile → FoodMachine
      MaterialsMachine: 2 raw_material + ScrapPile tile → MaterialsMachine
      OutputMachine: 1 construction_material + Floor tile → OutputMachine
      Conveyor:      1.5 raw_material + Floor tile → Conveyor
      Storage:       1 raw_material + Floor tile → Storage
      ```
- [ ] **Colony blueprint**: shared state tracking what the colony needs next.
      Agents read the blueprint and prioritize building accordingly:
      - 0 food machines? → FoodMachine is priority #1
      - Output exists but no conveyor to Exit? → Conveyor is priority
      - 3 output machines but 0 connected? → Connect them first
- [ ] **Collaborative construction correction**: agents working on the same
      structure contribute progress faster (already implemented for BUILD).
      Extend to: agents can REDIRECT construction — if a conveyor leads
      nowhere, a nearby agent can dismantle and rebuild it pointing the
      right way. This is the "they can correct between them" behavior.
- [ ] **Construction planning**: agents with high compliance + high influence
      become "foremen" who evaluate the colony's needs and mark tiles for
      construction. Other agents see the marks and build them.
- [ ] **Error correction**: if a conveyor chain is incomplete (dead-end),
      agents with high compliance recognize it and prioritize completing
      or dismantling it. Visual feedback in the GUI (highlight broken chains).

Estimated: ~500-800 LOC across `grid.h`, `sim_execute.cpp`, `sim_utility.cpp`, new `recipes.h`

### Priority 2: Storyteller / Director (Full Implementation)

CALM and NORMAL modes exist. The full Director adds:

- [ ] Track colony capacity (machines, food, agents, logistics state)
- [ ] Scale quota pressure based on capacity — ease off when struggling
- [ ] Introduce events: resource booms, scrap depletion, breakdowns, migrants
- [ ] Create narrative rhythm: calm periods followed by pressure spikes
- [ ] 36 Dramatic Situations (Polti) as event templates:
      - Revolt (faction with low trust sabotages infrastructure)
      - Vengeance (agent witnesses sabotage → vendetta)
      - Sacrifice (REDEEMED agent dies protecting infrastructure)
      - Disaster (factory collapse event)
      - Ambition (agent with high influence accumulates power)
      - Discovery (agent finds hidden space → colony-wide mood event)

Estimated: ~600-800 LOC in `simulation.cpp` + new `director.h`

### Priority 3: Generational Turnover
- [ ] Agent reproduction: high trust + low stress + sufficient food
- [ ] Offspring inherits blended personality
- [ ] Population cap, grief on elder death
- [ ] Social memory through relationship graph only

Estimated: ~600-800 LOC in `simulation.cpp`

### Priority 4: Deeper Congregation & Emergent Spaces
Congregation spaces are functional but basic. Deeper social emergence:

- [ ] **Schelling zones**: agents with similar traits cluster spatially over time.
      High-artistry agents near other creatives form "studio" zones.
      High-gregariousness agents form "plaza" zones.
- [ ] **Location affinity**: agents that REST/EAT repeatedly in the same area
      develop affinity for that location. Creates "neighborhoods".
- [ ] **Ritual emergence**: repeated communal eating at same time → tradition
      bonus that reinforces the behavior (positive feedback loop).
- [ ] **Gallery spaces**: artifact clusters become named locations that
      attract visitors, creating cultural pilgrimage points.

Estimated: ~400-600 LOC across `sim_utility.cpp`, `sim_execute.cpp`, `grid.h`

---

## Not Planned (deliberately excluded)

- **3D / fluid dynamics / heat simulation** — the 2D grid is sufficient
- **Networking / multiplayer** — single-threaded local simulation
- **Save/load** — can be added later as serialization over ECS state
- **Machine learning for agent behavior** — hand-crafted utility functions are sufficient

---

## Key Design Principles

1. **Conveyors ARE walkable** — agents walk over conveyor belts
2. **Two output transport paths** — conveyor chains + agent hauling
3. **Quota drains only from Exit-adjacent Storage** — no magical teleportation
4. **Deaths are turnover, not failure** — the simulation tracks causes
5. **Config-driven tuning** — all parameters in `config/default.toml`
6. **Archetypes with jitter** — structured diversity, not uniform random
7. **Social mechanics produce emergent coordination** — not scripted
8. **The factory is antagonist** — its demands are structurally opposed to
   the agents' self-actualization. Culture emerges from the cracks.
9. **Personality drives behavior** — artistry→CREATE r=0.86, curiosity→EXPLORE
   r=0.78. The architecture of desire is validated.
10. **Congregation enables culture** — EatingZone as the communal hearth.
    Socialization, eating, and art converge in shared physical space.
11. **Emergence over scripting** (redesign principle, 2026-07) — when behavior
    is wrong, fix the *curve*, don't add a clamp. Every hardcoded override
    accreted to patch a weak utility function; the sigmoid survival curve
    (Phase 2.1) eliminated 3 patches at once. A/B-test any gate removal: ~1/3
    of "patches" turn out to be load-bearing designs (see Phase 2.3 negative
    result). Legacy paths are preserved behind config knobs for reproducibility.

---

## Culture Diagnostic Baseline (48 agents, calm mode, 2000 ticks)

| Trait → Action | r (Pearson) | Status |
|---|---|---|
| artistry → CREATE | 0.86-0.92 | ✅ Strong |
| curiosity → EXPLORE | 0.65-0.89 | ✅ Strong |
| gregariousness → SOCIALIZE | 0.08-0.42 | ⚠️ Variable |
| compliance → WORK | 0.09-0.41 | ⚠️ Variable |

Agent time distribution (calm mode):
- BUILD: 5-14% (was 40-55% before infra_gap reform)
- GATHER: 5-15% (was 24-53% before raw_gap reform)
- CREATE: 4-12%
- SOCIALIZE: 2-7%
- EXPLORE: 1-8%
- REST: 15-19%

**Main bottlenecks**:
1. ~~GATHER dominance~~ — fixed by raw_gap gate (Phase 2)
2. Output logistics — output trapped in machines, not reaching Exit-adjacent Storage
3. Seed variance in NORMAL mode — some layouts produce few Output machines
4. SOCIALIZE correlation variable — needs more consistent congregation
