# Emergence Redesign — From Scripted Heuristics to Simple Rules

> **Status (2026-07-21):** Phases 1 + 2 complete and shipped. Phases 3 + 4 deferred to the next session. This document is the source of truth for resuming the work.

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
| **2.2 Niche dampening removed** | ✅ Shipped (`14731f7`) | Diversification emerges from Bonabeau + personality |
| **2.3 Compliance sigmoid** | ❌ Negative result (`c17e09b`) | Reverted — kink is load-bearing, not a patch |
| **3. Stress FSM → continuous** | ⏳ Deferred | Next session |
| **4. Documentation sync** | ⏳ Deferred | Next session |

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

With the sigmoid, the colony now:
- Recovers `factory_health` to 0.95+ (was 0.00 in legacy) — because agents eat better → less stress → less sabotage → less destruction. A local change to the urgency curve propagated systemically.
- Reduces sabotages 2-5× (32-154 vs 173-339).
- **Unlocks faction emergence** — the colony survives long enough to accumulate trust. This was blocked in the legacy regime.

### Phase 2.3 — negative result (valuable)

Replacing the compliance kink `if (needs.meaning > 0.7f) effective_compliance *= (1 - (meaning-0.7)*1.5f)` with a smooth sigmoid caused **production collapse in seed=2** (44 → 10 alive). Timeline: as `meaning` rises, the sigmoid reduces compliance gradually from meaning=0, agents progressively abandon WORK, output crashes, starvation follows.

The kink encodes a real behavioral regime: "compliance stays HIGH until meaning genuinely crises, then drops fast." A sigmoid destroys this. **Reverted and documented.** Lesson: A/B testing is mandatory; not every gate is pathological.

---

## Phase 3 — Stress FSM → continuous (deferred)

**Goal:** Replace the 5 scripted qualitative states with continuous modifiers derived from `stress.value`, honoring `07_principios_diseno.md:7` ("a depressed dwarf is not a scripted state, it is a conjunction...").

Current FSM (`components.h:212-218`):
```
NORMAL (0.0-0.4) → DISSOCIATED (0.4-0.7) → HOSTILE_EUPHORIA (0.7-0.9) → BROKEN (0.9+)
                                                                ↘ REDEEMED (post-sabotage)
```

Transitions at `simulation.cpp:544-547`, behavioral multipliers scattered across `sim_utility.cpp:700-701, 728, 741, 784-786`.

### Sub-tasks

**3.1 — Replace the 4 numeric states with smoothstep modifiers.** E.g. `gregariousness_mult = 1.0 - smoothstep(0.4, 0.7, stress.value) * 0.3` instead of `if state == DISSOCIATED then 0.7`. Same effect, no FSM.

**3.2 — REDEEMED is the hard case.** It rewrites personality (`compliance *= 0.5f`, `sim_execute.cpp:1142-1175`) and carries encoded narrative ("collectivist martyr"). Plan: move it to the existing `can_redeem` flag, treat redemption as a chronicle *event* not a stress *state*. This honors `03_agentes_ia.md:85` ("no event has a predetermined narrative interpretation").

**3.3 — HOSTILE_EUPHORIA's artificial mood boost.** Model that chronic high stress *disconnects* mood from need-satisfaction. Can emerge from `update_mood` (social.h) if the lerp target is modified by stress, without an FSM state.

### Risks
- REDEEMED is narratively load-bearing; testers may notice if the redemption arc disappears. Preserve as event.
- The FSM transitions are read in many places; grep `StressState::` across src/ before removing.
- Apply the same A/B-test discipline as Phase 2: one state at a time, `vida_batch analysis 3000` between changes, revert on >30% alive-count regression in any seed.

### Regression target
Post-Phase-2 baseline captured at `/tmp/redesign_baseline_v3/analysis_seed{0,1,2,3,7}.txt` (md5s in commit `c2c76a3` message). Re-capture fresh at start of next session since `/tmp` does not persist across reboots.

---

## Phase 4 — Documentation sync (deferred)

**Goal:** Align docs with the post-redesign reality.

- **`14_inhabitants.md`**: argmax formulation → softmax (already documented in §8.6 tropical); update skill-feedback claim if Phase 3 changes utility inputs.
- **`16_social_fabric.md`**: leadership "centrality metrics" claim vs the actual `influence` formula in `social.h:163-166` — either implement real centrality or correct the doc.
- **Implementation-status block** in each theoretical section: which emergence claims are delivered, which aspirational, which intentionally scripted.
- **Remove `w_fear`** reference (`16_social_fabric.md:81`) — still does not exist in code.

---

## How to Resume

1. Re-capture the regression baseline (the `/tmp` md5s do not survive reboot):
   ```
   cd build
   for seed in 0 1 2 3 7; do
     ./vida_batch analysis 3000 $seed > /tmp/redesign_baseline_v3/analysis_seed${seed}.txt
   done
   ```
2. Confirm `config/default.toml` has `curve_variant = 3` (the sigmoid default).
3. Start Phase 3.1 (the 4 numeric stress states → smoothstep). Grep `StressState::` first to map all read sites.
4. Use the A/B-test harness pattern from Phase 2: add a config knob, keep the legacy path, compare before committing.

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
