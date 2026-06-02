#pragma once
#include <string>
#include <toml++/toml.hpp>

struct Config {
    // Grid
    int grid_width  = 60;
    int grid_height = 40;

    // Simulation
    int   initial_population = 24;
    int   max_population     = 200;
    int   seed               = 0;
    bool  use_wfc            = true;  // wave function collapse map generation

    // Need decay rates
    float hunger_decay         = 0.005f;
    float rest_decay       = 0.006f;
    float social_decay     = 0.003f;
    float expression_decay = 0.003f;
    float purpose_decay    = 0.002f;

    // Action effects (per tick while executing)
    float eat_satisfaction      = 0.018f;  // from processed food
    float eat_raw_efficiency    = 0.5f;    // raw_food gives 50% of eat_satisfaction
    float eat_food_per_tick     = 0.02f;   // how much food consumed per eat tick
    float rest_recovery         = 0.015f;
    float social_satisfaction   = 0.012f;
    float create_satisfaction   = 0.012f;
    float explore_satisfaction  = 0.008f;
    float work_purpose_gain     = 0.004f;

    // Production
    float gather_rate       = 0.08f;   // resources gathered per tick
    float build_rate        = 0.12f;   // build progress per tick
    float machine_output    = 0.04f;   // food produced per work tick (raw_food → food)
    float machine_mat_output = 0.025f; // construction material produced per work tick
    float machine_out_output = 0.02f;  // output product produced per work tick
    float machine_input     = 0.015f;  // raw input consumed per work tick

    // Urgency
    float urgency_alpha = 2.0f;

    // Death
    int starvation_ticks = 120;
    int exhaustion_ticks = 160;

    // Stress
    float stress_high_need      = 0.008f;
    float stress_decay          = 0.001f;  // Minimal natural decay — mostly needs expression
    float breakdown_threshold   = 0.92f;

    // Stress trauma system
    float trauma_accumulation_rate = 0.001f;  // per tick while stress > 0.5
    float trauma_resilience_impact = 0.5f;     // effective_resilience *= (1 - trauma * this)
    float trauma_social_impact     = 0.3f;     // effective_gregariousness *= (1 - trauma * this)
    float redemption_chance        = 0.08f;    // per sabotage tick, chance of epiphany
    float suicide_chance           = 0.03f;    // per sabotage tick, chance of self-destruction
    float sabotage_stress_threshold = 0.6f;    // stress must be >= this for SABOTAGE utility

    // Personality ranges [min, max]
    float compliance_range[2]     = {0.1f, 0.95f};
    float laziness_range[2]       = {0.1f, 0.90f};
    float artistry_range[2]       = {0.05f, 0.85f};
    float gregariousness_range[2] = {0.1f, 0.90f};
    float resilience_range[2]     = {0.2f, 0.90f};
    float curiosity_range[2]      = {0.1f, 0.80f};

    // Movement noise
    float movement_noise = 0.05f;  // chance of random move instead of toward target

    // Conveyor
    float conveyor_build_cost  = 1.5f;   // raw_material per segment
    float conveyor_decay_rate   = 0.0005f; // condition loss per tick
    float conveyor_throughput   = 0.5f;    // max resource movement per tick
    float maintain_rate       = 0.08f;  // condition restored per MAINTAIN tick
    float dismantle_return    = 0.5f;   // fraction of build_cost returned on DISMANTLE
    int   dismantle_rebuild_window = 200; // ticks before social penalty for not rebuilding

    // External pressure (quota / supply-chain contraction)
    float quota_per_tick           = 0.10f;  // food units demanded by the outside per tick
    float health_decay_per_miss    = 0.0005f;// factory_health drop when quota not met (slow — gives time to build)
    float health_recovery_per_hit  = 0.002f; // factory_health rise when quota met
    float machine_break_threshold  = 0.25f;  // below this, machines may revert to unbuilt
    float machine_break_prob       = 0.0003f;// per-tick probability per built machine
    float initial_food_per_agent   = 3.0f;   // bootstrap so agents survive while building
    float quota_growth_rate         = 0.00002f; // quota increase per tick (factory demands more)
    int   restructure_interval     = 800;    // ticks between restructure checks
    float restructure_probability  = 0.2f;   // chance per check
    float noncompliance_stress     = 0.002f; // stress per tick per noncompliance level

    // Eating zones / social pressure
    float inv_food_cap              = 2.0f;  // "una vianda" — max food an agent can carry
    int   eatingzone_min_dist_machine = 5;   // Manhattan ≥ this from any Machine for an EatingZone
    float eatingzone_build_cost     = 2.0f;  // raw_material required (~17 BUILD ticks at rate 0.12)
    float eat_at_work_health_decay  = 0.002f;// factory_health drop per eat-at-work tick
    float eat_at_work_stress        = 0.010f;// stress added to transgressor per eat-at-work tick
    int   eat_at_work_witness_radius = 3;    // Manhattan: another agent within this radius "reports"
                                              // the transgression. Without a witness, no penalty.
};

Config load_config(const std::string& path);
