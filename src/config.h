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
    float gather_rate       = 0.05f;   // resources gathered per tick
    float build_rate        = 0.02f;   // build progress per tick
    float machine_output    = 0.025f;  // food produced per work tick
    float machine_input     = 0.02f;   // raw_food consumed per work tick

    // Urgency
    float urgency_alpha = 2.0f;

    // Death
    int starvation_ticks = 120;
    int exhaustion_ticks = 160;

    // Stress
    float stress_high_need      = 0.008f;
    float stress_decay          = 0.005f;
    float breakdown_threshold   = 0.92f;

    // Personality ranges [min, max]
    float compliance_range[2]     = {0.1f, 0.95f};
    float laziness_range[2]       = {0.1f, 0.90f};
    float artistry_range[2]       = {0.05f, 0.85f};
    float gregariousness_range[2] = {0.1f, 0.90f};
    float resilience_range[2]     = {0.2f, 0.90f};
    float curiosity_range[2]      = {0.1f, 0.80f};

    // Movement noise
    float movement_noise = 0.05f;  // chance of random move instead of toward target
};

Config load_config(const std::string& path);
