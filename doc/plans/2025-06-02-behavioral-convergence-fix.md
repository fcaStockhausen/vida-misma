# Behavioral Convergence Fix — Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Prevent 24 utility-based agents from all choosing the same action simultaneously, producing emergent role diversity without top-down assignment.

**Architecture:** Layered approach from game AI literature. Replace greedy argmax with Boltzmann selection, add task claiming on resource tiles, introduce personality-based response thresholds per action type, and stagger initial need levels. Each layer independently reduces convergence; together they produce robust diversity.

**Tech Stack:** C++17, existing entt ECS, existing utility system in `sim_utility.cpp`

**References:**
- Bonabeau et al. (1996) — Response Threshold Model for Division of Labour
- The Sims — Smart Objects + object contention
- RimWorld — Skill-based priority multipliers
- ONI — Personal priority multipliers + task claiming
- Gerkey & Mataric (2004) — Task Allocation Taxonomy

---

## Layer 1: Boltzmann Action Selection

**Why:** Currently all 24 agents do `argmax(utilities)` → identical choice. Boltzmann/softmax replaces this with probabilistic selection: agents USUALLY pick the best action but ~20-30% of the time pick the 2nd or 3rd best. This is the single most impactful change.

**Mechanics:**
```
P(action a) = exp(U(a) / tau) / sum(exp(U(b) / tau) for all b)
```
- `tau` (temperature) controls exploration vs exploitation
- tau=0.3 → strong preference for best action, ~15% variance
- tau=0.5 → moderate, ~25% variance
- tau=1.0 → very random, agents are erratic

**Target:** tau=0.4 (tune later)

### Task 1.1: Add Boltzmann config parameter

**Files:**
- Modify: `src/config.h`

Add after `initial_food_per_agent` (line ~86):

```cpp
float selection_temperature = 0.4f;  // Boltzmann selection: 0=greeedy, 1=random
```

### Task 1.2: Replace argmax with Boltzmann selection

**Files:**
- Modify: `src/sim_utility.cpp:695-731`

Replace the current greedy argmax block (lines 695-731) with:

```cpp
        // === BOLTZMANN ACTION SELECTION ===
        // Replace greedy argmax with probabilistic softmax selection.
        // Agents USUALLY pick the highest-utility action but have a
        // temperature-controlled chance of picking alternatives.
        // This prevents all 24 agents from choosing the same action.
        struct Scored { ActionType type; float score; };
        Scored options[] = {
            {ActionType::GATHER,    u_gather},
            {ActionType::BUILD,     u_build},
            {ActionType::WORK,      u_work},
            {ActionType::EAT,       u_eat},
            {ActionType::REST,      u_rest_action},
            {ActionType::SOCIALIZE, u_socialize},
            {ActionType::CREATE,    u_create},
            {ActionType::EXPLORE,   u_explore},
            {ActionType::GET_FOOD,  u_get_food},
            {ActionType::MAINTAIN,  u_maintain},
            {ActionType::DISMANTLE, u_dismantle},
            {ActionType::SABOTAGE,  u_sabotage},
        };
        constexpr int N = sizeof(options) / sizeof(options[0]);

        float tau = config_.selection_temperature;
        if (tau <= 0.001f) {
            // Degenerate: greedy argmax (legacy behavior)
            float best_score = -1.0f;
            ActionType best_action = ActionType::IDLE;
            for (auto& opt : options) {
                if (opt.score > best_score) {
                    best_score = opt.score;
                    best_action = opt.type;
                }
            }
            action.current = best_action;
        } else {
            // Boltzmann: clamp negatives, compute softmax weights
            float max_u = options[0].score;
            for (int i = 1; i < N; i++)
                if (options[i].score > max_u) max_u = options[i].score;

            float weights[N];
            float sum_w = 0.0f;
            for (int i = 0; i < N; i++) {
                float u = std::max(0.0f, options[i].score);
                weights[i] = std::exp((u - max_u) / tau);
                sum_w += weights[i];
            }

            // Weighted random selection
            std::uniform_real_distribution<float> pick(0.0f, sum_w);
            float r = pick(rng_);
            float cumulative = 0.0f;
            action.current = ActionType::IDLE;
            for (int i = 0; i < N; i++) {
                cumulative += weights[i];
                if (r <= cumulative) {
                    action.current = options[i].type;
                    break;
                }
            }
        }
```

**Verification:** Build and run `./build/vida_batch run 100 42`. Check that GATH/BUIL/WORK counts are spread across multiple actions per tick (not 24-0-0 blocks).

### Task 1.3: Commit Layer 1

```bash
git add src/config.h src/sim_utility.cpp
git commit -m "feat: Boltzmann action selection replaces greedy argmax"
```

---

## Layer 2: Resource Tile Claiming

**Why:** Currently all agents can target the same FoodSource or ScrapPile for BUILD. This produces dogpiling: 10 agents walk to the same tile, only some get to build. The Sims / DF / ONI all use object contention (capacity=1) as the #1 convergence breaker.

**Mechanics:**
- When an agent selects a FoodSource/ScrapPile as BUILD target, claim it (`claimed_by = agent_id`)
- Other agents skip claimed tiles in target selection
- Claims release when: agent switches action, or agent reaches the tile and completes the build
- Claims expire after 30 ticks if agent hasn't arrived (stuck/killed)

### Task 2.1: Add claim fields to resource tile data

**Files:**
- Modify: `src/components.h` — TileData already has `claimed_by = -1` (line 317). Already works for Machine tiles. Just needs to be used for FoodSource/ScrapPile too.

No code change needed — field already exists.

### Task 2.2: Claim FoodSource/ScrapPile in BUILD target selection

**Files:**
- Modify: `src/sim_targets.cpp:BUILD case`

In the BUILD target section (around line 90-180), when selecting `fs_t` (FoodSource) and `sp_t` (ScrapPile), skip tiles that are claimed by other agents:

Find the `find_nearest_free_foodsource` and `find_nearest_free_scrappile` calls and update the grid helpers to respect claims.

**Files:**
- Modify: `src/grid.h` — Update `find_nearest_free_foodsource` and `find_nearest_free_scrappile`

Change the condition that checks if a tile is "free" to also check `claimed_by`:

```cpp
// In find_nearest_free_foodsource:
if (grid_.at(nx, ny) == TileType::FoodSource) {
    const auto& td = grid_.data_at(nx, ny);
    // Only unclaimed tiles, or tiles this agent already claimed
    // (agent_id parameter needs to be added to the function)
    if (td.claimed_by < 0 || td.claimed_by == agent_id) {
        ...
    }
}
```

Add `agent_id` parameter to both helpers.

### Task 2.3: Set claim when BUILD target is chosen

**Files:**
- Modify: `src/sim_targets.cpp:BUILD case`

After selecting the target tile for FoodSource/ScrapPile builds, set the claim:

```cpp
if (best_target matches fs_t or sp_t) {
    auto& td = grid_.data_at(best_x, best_y);
    td.claimed_by = my_id;
}
```

Also release the old claim when target changes (line ~171 already does this for machines).

### Task 2.4: Release claim on BUILD completion and action switch

**Files:**
- Modify: `src/sim_execute.cpp` — In the FoodSource/ScrapPile BUILD cases, after `td.built = true`, release the claim:
  ```cpp
  td.claimed_by = -1;
  ```

- Modify: `src/sim_targets.cpp` — The existing action-switch release (lines 14-19) already clears `claimed_by`. Verify it covers the new tile types (FoodSource/ScrapPile that have become Machine tiles).

### Task 2.5: Commit Layer 2

```bash
git add src/grid.h src/sim_targets.cpp src/sim_execute.cpp
git commit -m "feat: resource tile claiming prevents BUILD dogpiling"
```

---

## Layer 3: Personality-Based Response Thresholds

**Why:** Even with Boltzmann selection, agents have identical utility landscapes. Response thresholds (Bonabeau et al. 1996) give each agent a per-action-type sensitivity. An agent with low GATHER threshold responds to small food deficits; one with high threshold only responds in crisis. This creates natural "lean" toward roles using the EXISTING personality system.

**Mechanics:**
```
threshold[action_type] = f(personality)
effective_stimulus = S^2 / (S^2 + theta^2)
utility *= effective_stimulus
```

### Task 3.1: Add threshold helper function

**Files:**
- Modify: `src/sim_utility.cpp`

Add a helper function before `system_compute_utility`:

```cpp
// Response threshold model (Bonabeau et al. 1996).
// Low theta = agent responds to small stimuli (specialist).
// High theta = agent only responds to large stimuli (reluctant).
static float response_threshold(float stimulus, float theta) {
    float s2 = stimulus * stimulus;
    float t2 = theta * theta;
    return s2 / (s2 + t2 + 0.001f);  // epsilon prevents div-by-zero
}
```

### Task 3.2: Map personality traits to per-action thresholds

**Files:**
- Modify: `src/sim_utility.cpp` — Inside the per-agent loop, after personality is loaded

Add threshold computation based on personality. Each action type gets a threshold derived from the personality traits that logically relate to it:

```cpp
// Per-action response thresholds from personality.
// theta near 0.0 = specialist (responds eagerly)
// theta near 1.0 = reluctant (only responds to strong stimulus)
float theta_gather    = 0.5f - 0.3f * personality.compliance;   // compliant agents gather eagerly
float theta_build     = 0.5f - 0.3f * personality.compliance;   // compliant agents build eagerly
float theta_work      = 0.5f - 0.3f * (1.0f - personality.laziness);  // non-lazy agents work eagerly
float theta_eat       = 0.1f;  // everyone eats eagerly (survival)
float theta_rest      = 0.5f - 0.3f * personality.laziness;    // lazy agents rest eagerly
float theta_socialize = 0.5f - 0.3f * personality.gregariousness;  // social agents socialize eagerly
float theta_create    = 0.5f - 0.3f * personality.artistry;    // artistic agents create eagerly
float theta_explore   = 0.5f - 0.3f * personality.curiosity;   // curious agents explore eagerly
```

### Task 3.3: Apply thresholds to utility scores

**Files:**
- Modify: `src/sim_utility.cpp`

Apply the threshold filter to each action's stimulus. The "stimulus" is the urgency signal for each action type. Apply BEFORE the Boltzmann selection:

```cpp
// Apply response thresholds (Bonabeau model).
// This differentiates agents: each responds to different stimulus levels.
u_gather    *= response_threshold(u_gather,    theta_gather);
u_build     *= response_threshold(u_build,     theta_build);
u_work      *= response_threshold(u_work,      theta_work);
u_eat       *= response_threshold(u_eat,       theta_eat);
u_rest_action *= response_threshold(u_rest_action, theta_rest);
u_socialize *= response_threshold(u_socialize, theta_socialize);
u_create    *= response_threshold(u_create,    theta_create);
u_explore   *= response_threshold(u_explore,   theta_explore);
// GET_FOOD, MAINTAIN, DISMANTLE, SABOTAGE: no threshold (situational)
```

Place this block right before the Boltzmann selection block (before the `Scored options[]` array).

### Task 3.4: Commit Layer 3

```bash
git add src/sim_utility.cpp
git commit -m "feat: personality-based response thresholds (Bonabeau model)"
```

---

## Layer 4: Staggered Initial Needs

**Why:** All 24 agents start with identical need levels (hunger=0.0, rest=0.0, social=0.0). They deplete at the same rate, producing synchronized need spikes. Adding small random offsets desynchronizes their cycles — agents eat/rest at different times.

**Mechanics:** Add random offsets [0.0, 0.2] to each need at initialization.

### Task 4.1: Add jitter to initial needs

**Files:**
- Modify: `src/simulation.cpp` — Agent creation section (around line ~200)

Find where needs are initialized (currently defaults to 0.0) and add jitter:

```cpp
        // Stagger initial needs to desynchronize agent behavior cycles.
        // Without this, all 24 agents spike the same need at the same tick.
        auto& needs = registry_.emplace<NeedsComponent>(entity);
        std::uniform_real_distribution<float> need_jitter(0.0f, 0.2f);
        needs.hunger    += need_jitter(rng_);
        needs.rest      += need_jitter(rng_);
        needs.social    += need_jitter(rng_);
        needs.expression += need_jitter(rng_);
        needs.meaning   += need_jitter(rng_);
```

### Task 4.2: Commit Layer 4

```bash
git add src/simulation.cpp
git commit -m "feat: staggered initial needs desynchronize agent cycles"
```

---

## Verification & Tuning

### Task 5.1: Multi-seed baseline test

After all 4 layers are implemented:

```bash
for s in 42 137 271 999 1337 7 500 1234 777 31415; do
    echo -n "seed=$s "
    ./build/vida_batch run 500 $s 2>/dev/null | tail -1
done
```

**Target:** Average >= 18 alive across 10 seeds. Minimum >= 12.

### Task 5.2: Temperature tuning

If convergence persists, adjust `selection_temperature`:
- More convergence → increase tau (0.4 → 0.6)
- Too erratic → decrease tau (0.4 → 0.3)
- Tune in `src/config.h:selection_temperature`

### Task 5.3: Commit final tuning

```bash
git add src/config.h
git commit -m "tune: selection_temperature for optimal diversity"
```

---

## Dependency Graph

```
Layer 1 (Boltzmann)     ← no dependencies, do first
Layer 2 (Tile Claiming) ← no dependencies, parallel with L1
Layer 3 (Thresholds)    ← depends on L1 (thresholds modify utilities before Boltzmann)
Layer 4 (Stagger Needs) ← no dependencies, can do anytime
Task 5 (Verification)   ← depends on L1-L4 all complete
```

## Risk Assessment

| Layer | Risk | Mitigation |
|-------|------|-----------|
| L1 Boltzmann | tau too high → agents ignore food and die | Start at 0.4, tune down if agents starve |
| L2 Claiming | Dead agents hold claims forever | Add 30-tick expiry on claims |
| L3 Thresholds | Personality mapping wrong → agents ignore critical actions | Keep theta_eat=0.1 (everyone eats), tune others |
| L4 Stagger | Jitter too high → agents start hungry and die immediately | Cap at 0.2, not enough to trigger urgent needs |
