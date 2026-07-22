# Emergence Redesign — From Scripted Heuristics to Simple Rules

> **Status (2026-07-22):** Historical/completed. Phases 1-4 were implemented
> and verified; this document is a retrospective record, not a plan to resume.
> Later ontology, social and lifecycle work is tracked in
> `2026-07-21-alineacion-diseno-implementacion.md` and supersedes broad emergence
> claims made during these experiments.

## Motivation: The Gap Between Doc and Code

A critical audit (2026-07-20) compared the design documentation's claims against the implementation. The docs promise **emergent behavior from simple rules** — Conway/Boids/Schelling class:

> `07_principios_diseno.md:21` — *"Complex behavior should emerge from the interaction of simple subsystems, not from the complexity of individual subsystems."*

> `03_agentes_ia.md:73` — *"This loop is not scripted: no code specifies that 'a hungry dwarf should eat.' Instead, the eating behavior emerges from the interaction of need decay, utility maximization, and personality."*

The code did not honor this. Forensic findings:

- **~740 magic-number float literals** across `sim_utility.cpp`, `sim_execute.cpp`, `simulation.cpp`.
- **121 if/else threshold gates** in `sim_utility.cpp` alone.
- A **scripted 5-state stress FSM** (`NORMAL/DISSOCIATED/HOSTILE_EUPHORIA/BROKEN/REDEEMED`) that the doc explicitly says should *not* exist.
- Four separate mechanisms forcing EAT to win at critical hunger (because the base urgency curve was too weak).
- Commit-message archaeology showed the same pattern repeated 70+ times: emergent pathology → hardcoded clamp → permanent residue.

**Verdict:** The codebase is *The Sims class* (scripted with an emergent veneer), not the Schelling/Boids class the docs claim. The overrides were there from day 1 (`8e0b0c8` already had `if needs.hunger > 0.7f`); what accreted was a sediment of patches on an already-scripted core.

## Thesis (refined by experiment)

> If the base utility curves are shaped correctly, the hardcoded overrides become unnecessary because the emergent utility produces sane behavior without clamps.

**Important refinement from Phase 2.3:** not every threshold gate is a patch. The compliance kink at `meaning > 0.7` is *load-bearing* — it encodes a real behavioral regime, not a workaround. A/B testing is mandatory before removing any gate; ~1/3 of candidate removals will cause regressions.

---

## Phase Status

| Phase | Status | Outcome |
|---|---|---|
| **1. De-duplication refactor** | ✅ Shipped (`cb0b186`) | 4 helpers extracted, 10/10 md5-identical verification |
| **2.1 Survival urgency sigmoid** | ✅ Shipped (`c2c76a3`) | 2.5x survival, eliminates 3 patch mechanisms |
| **2.2 Niche dampening removed** | ✅ Shipped (`14731f7`) | Removed dampener; retained Bonabeau/personality mechanism passed the recorded regression, without a validated diversification claim |
| **2.3 Compliance sigmoid** | ❌ Negative result (`c17e09b`) | Reverted — kink is load-bearing, not a patch |
| **3. Stress FSM → continuous** | Completed (2026-07-21) | Smoothstep modifiers replace behavioral FSM branches; enum becomes a derived label |
| **4. Documentation sync** | Completed (2026-07-21) | Contemporary inhabitant/social sections were corrected to match that redesign |

---

## Phase 1 — De-duplication (shipped, behavior-neutral)

Extracted four duplicated patterns into named helpers in an anonymous namespace at the top of `sim_utility.cpp`:

- **`factory_pressure(factory_health, k)`** — was inlined 3× (gather_urgency k=2.0, build_urgency k=2.0, health_urgency k=3.0).
- **`calm_comfortable_dampener(mode, hu, hunger, rest, ratio, mult)`** — the 4-way conjunction was duplicated 3× verbatim. GATHER/BUILD used mult=0.3, WORK used 0.2.
- **`calm_work_focus(mode, higher_unmet)`** — WORK's gradual focus dampener (distinct from the hard comfortable dampener).
- **`maslow_boost(hunger, rest, primary, secondary)`** — two-tier boost duplicated 3×; fixed EXPLORE which was missing its secondary tier.

**Verification:** md5-identical output across 3 seeds in NORMAL mode + 3 seeds in CALM mode + 2 run-timelines (10/10 identical). Behavior-neutral by construction.

**Skipped:** Phase 1.4 (helper `build_into()` in sim_execute.cpp). The 3 BUILD sub-blocks differ in `grid_.set` vs no-set, `machine_type` assignment, and log text — a single helper would be fragile and the gain (~40 LOC) did not justify the regression risk.

---

## Phase 2 — Survival urgency redesign (shipped, the big win)

### The experiment

Added config knob `urgency.curve_variant` (0=legacy, 1=steep, 2=extra-steep, 3=sigmoid). Variants 1-3 disable the three patch mechanisms (critical_spike, eat_weight boost, HARD OVERRIDE) and rely on the single curve alone.

### A/B test results (5 seeds × 3000 ticks, avg alive / 48)

| Variant | seed 0 | seed 1 | seed 2 | seed 3 | seed 7 | **Avg** |
|---|---|---|---|---|---|---|
| 0 (legacy) | 19 | 12 | 8 | 21 | 25 | **17.0** |
| 1 (steep) | 0 | 42 | 1 | 31 | 43 | 23.4 (bimodal) |
| 2 (extra-steep) | 15 | 31 | 0 | 33 | 2 | 16.2 (bimodal) |
| **3 (sigmoid)** | **46** | 41 | **44** | **44** | 41 | **43.2** |

### Why sigmoid won (counter to the original plan's hypothesis)

The plan hypothesized "steep pure exponential." The data refuted it. The sigmoid wins because it is **smooth in the mid-range** (doesn't kill work prematurely at need=0.6) but **dominates clearly at the edge** (need>0.85). The steep variants were too binary: agents either worked or panic-ate with nothing between, producing bimodal seed-dependent outcomes.

Sigmoid formula: `s = 1 / (1 + exp(-12 * (need - 0.7))); return s * 8.0;`

### Cascading effects (unanticipated but welcome)

In the original sigmoid experiment, the colony:
- Recovered the then-behavioral `factory_health` metric to 0.95+ (from 0.00 in
  legacy) because agents ate better, accumulated less stress and sabotaged less.
  Later alignment work replaced this canonical causal role with shipped-output
  support; the historical result is not a description of current policy.
- Reduced sabotages 2-5× (32-154 vs 173-339).
- Extends the observation window for trust and social learning. This did not, by
  itself, establish factions or any other canonical collective ontology.

### Phase 2.3 — negative result (valuable)

Replacing the compliance kink `if (needs.meaning > 0.7f) effective_compliance *= (1 - (meaning-0.7)*1.5f)` with a smooth sigmoid caused **production collapse in seed=2** (44 → 10 alive). Timeline: as `meaning` rises, the sigmoid reduces compliance gradually from meaning=0, agents progressively abandon WORK, output crashes, starvation follows.

The kink encodes a real behavioral regime: "compliance stays HIGH until meaning genuinely crises, then drops fast." A sigmoid destroys this. **Reverted and documented.** Lesson: A/B testing is mandatory; not every gate is pathological.

---

## Phase 3 — Stress FSM → continuous (completed)

**Historical goal:** replace scripted qualitative behavior branches with
continuous modifiers derived from `stress.value`, while preserving display labels
and testing against the Phase 2 baseline.

### Implemented result

- `urgency.stress_model_variant = 1` is the canonical/default path. Variant `0`
  retains the old discrete behavior only for explicit historical A/B comparison.
- Gregariousness, creativity, noncompliance response, work suppression, mood and
  breakdown risk use smoothstep functions of continuous stress. `StressState` is
  derived from that value for GUI and Chronicle presentation rather than being
  the canonical behavioral driver.
- The behavioral `REDEEMED` state and its personality rewrite were removed. Later
  alignment work retained only a factual post-sabotage pause event, without a
  predetermined martyr or redemption ontology.
- The established `0 1 2 3 7` regression guard was applied one mechanism at a
  time. The temporary `/tmp` baseline mentioned during development is no longer
  an instruction or prerequisite for resuming this completed plan.

---

## Phase 4 — Documentation sync (completed)

The contemporary inhabitants and social-fabric sources were updated after the
utility/stress experiments: action selection was described as softmax rather than
argmax, unsupported leadership/`w_fear` claims were removed or qualified, and
implementation status was separated from theory. The broader Phase 9
consolidation in the alignment plan was completed later and did not reopen this
completed redesign.

The durable lesson is experimental: preserve a legacy variant only while it
answers a concrete A/B question, test one causal mechanism at a time, and retain
negative findings such as the failed compliance-smoothing experiment instead of
rewriting them as successes.

## Files touched (Phase 1 + 2)

| File | Change |
|---|---|
| `src/sim_utility.cpp` | 4 helpers (Phase 1), sigmoid curve + 3 patches gated (Phase 2.1), niche dampening gated (Phase 2.2), compliance sigmoid reverted (Phase 2.3) |
| `src/config.h` | `urgency_curve_variant` knob |
| `src/config.cpp` | TOML read for the knob |
| `config/default.toml` | `curve_variant = 3` default + documentation |

## Key commits (in order)

- `cb0b186` — Phase 1: de-duplication (behavior-neutral)
- `c2c76a3` — Phase 2.1: sigmoid survival urgency (2.5x survival)
- `14731f7` — Phase 2.2: niche dampening disabled for sigmoid variant
- `c17e09b` — Phase 2.3: compliance kink is load-bearing (negative result, documented)
