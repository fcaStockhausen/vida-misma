#include "config.h"
#include <toml++/toml.hpp>

Config load_config(const std::string& path) {
    Config cfg;
    auto config = toml::parse_file(path);

    // Grid
    if (auto g = config["grid"].as_table()) {
        cfg.grid_width  = g->get_as<int64_t>("width")->value_or(cfg.grid_width);
        cfg.grid_height = g->get_as<int64_t>("height")->value_or(cfg.grid_height);
    }

    // Simulation
    if (auto s = config["simulation"].as_table()) {
        cfg.initial_population = s->get_as<int64_t>("initial_population")->value_or(cfg.initial_population);
        cfg.max_population     = s->get_as<int64_t>("max_population")->value_or(cfg.max_population);
        cfg.seed               = s->get_as<int64_t>("seed")->value_or(cfg.seed);
        cfg.use_wfc            = s->get_as<bool>("use_wfc")->value_or(cfg.use_wfc);
    }

    // Needs
    if (auto n = config["needs"].as_table()) {
        cfg.hunger_decay     = n->get_as<double>("hunger_decay")->value_or(cfg.hunger_decay);
        cfg.rest_decay       = n->get_as<double>("rest_decay")->value_or(cfg.rest_decay);
        cfg.social_decay     = n->get_as<double>("social_decay")->value_or(cfg.social_decay);
        cfg.expression_decay = n->get_as<double>("expression_decay")->value_or(cfg.expression_decay);
        cfg.purpose_decay    = n->get_as<double>("purpose_decay")->value_or(cfg.purpose_decay);
    }

    // Actions
    if (auto a = config["actions"].as_table()) {
        cfg.eat_satisfaction    = a->get_as<double>("eat_satisfaction")->value_or(cfg.eat_satisfaction);
        cfg.eat_raw_efficiency  = a->get_as<double>("eat_raw_efficiency")->value_or(cfg.eat_raw_efficiency);
        cfg.eat_food_per_tick   = a->get_as<double>("eat_food_per_tick")->value_or(cfg.eat_food_per_tick);
        cfg.rest_recovery       = a->get_as<double>("rest_recovery")->value_or(cfg.rest_recovery);
        cfg.social_satisfaction = a->get_as<double>("social_satisfaction")->value_or(cfg.social_satisfaction);
        cfg.create_satisfaction = a->get_as<double>("create_satisfaction")->value_or(cfg.create_satisfaction);
        cfg.explore_satisfaction= a->get_as<double>("explore_satisfaction")->value_or(cfg.explore_satisfaction);
        cfg.work_purpose_gain   = a->get_as<double>("work_purpose_gain")->value_or(cfg.work_purpose_gain);
    }

    // Production
    if (auto p = config["production"].as_table()) {
        cfg.gather_rate    = p->get_as<double>("gather_rate")->value_or(cfg.gather_rate);
        cfg.build_rate     = p->get_as<double>("build_rate")->value_or(cfg.build_rate);
        cfg.machine_output = p->get_as<double>("machine_output")->value_or(cfg.machine_output);
        cfg.machine_mat_output = p->get_as<double>("machine_mat_output")->value_or(cfg.machine_mat_output);
        cfg.machine_out_output = p->get_as<double>("machine_out_output")->value_or(cfg.machine_out_output);
        cfg.machine_input  = p->get_as<double>("machine_input")->value_or(cfg.machine_input);
    }

    // Urgency
    if (auto u = config["urgency"].as_table()) {
        cfg.urgency_alpha = u->get_as<double>("alpha")->value_or(cfg.urgency_alpha);
    }

    // Death
    if (auto d = config["death"].as_table()) {
        cfg.starvation_ticks = d->get_as<int64_t>("starvation_ticks")->value_or(cfg.starvation_ticks);
        cfg.exhaustion_ticks = d->get_as<int64_t>("exhaustion_ticks")->value_or(cfg.exhaustion_ticks);
    }

    // Stress
    if (auto st = config["stress"].as_table()) {
        cfg.stress_high_need    = st->get_as<double>("high_need")->value_or(cfg.stress_high_need);
        cfg.stress_decay        = st->get_as<double>("decay")->value_or(cfg.stress_decay);
        cfg.breakdown_threshold = st->get_as<double>("breakdown_threshold")->value_or(cfg.breakdown_threshold);
    }

    // Personality
    if (auto pr = config["personality"].as_table()) {
        auto load_range = [&](const char* name, float (&arr)[2]) {
            if (auto arr_node = pr->get_as<toml::array>(name)) {
                if (arr_node->size() >= 2) {
                    arr[0] = (float)arr_node->get(0)->value_or((double)arr[0]);
                    arr[1] = (float)arr_node->get(1)->value_or((double)arr[1]);
                }
            }
        };
        load_range("compliance_range", cfg.compliance_range);
        load_range("laziness_range", cfg.laziness_range);
        load_range("artistry_range", cfg.artistry_range);
        load_range("gregariousness_range", cfg.gregariousness_range);
        load_range("resilience_range", cfg.resilience_range);
        load_range("curiosity_range", cfg.curiosity_range);
    }

    // Movement
    if (auto mv = config["movement"].as_table()) {
        cfg.movement_noise = mv->get_as<double>("noise")->value_or(cfg.movement_noise);
    }

    // External pressure
    if (auto ex = config["external"].as_table()) {
        cfg.quota_per_tick          = ex->get_as<double>("quota_per_tick")->value_or(cfg.quota_per_tick);
        cfg.health_decay_per_miss   = ex->get_as<double>("health_decay_per_miss")->value_or(cfg.health_decay_per_miss);
        cfg.health_recovery_per_hit = ex->get_as<double>("health_recovery_per_hit")->value_or(cfg.health_recovery_per_hit);
        cfg.machine_break_threshold = ex->get_as<double>("machine_break_threshold")->value_or(cfg.machine_break_threshold);
        cfg.machine_break_prob      = ex->get_as<double>("machine_break_prob")->value_or(cfg.machine_break_prob);
        cfg.initial_food_per_agent  = ex->get_as<double>("initial_food_per_agent")->value_or(cfg.initial_food_per_agent);
    }

    // Eating zones / social pressure
    if (auto ez = config["eating"].as_table()) {
        cfg.inv_food_cap              = ez->get_as<double>("inv_food_cap")->value_or(cfg.inv_food_cap);
        cfg.eatingzone_min_dist_machine = ez->get_as<int64_t>("min_dist_to_machine")->value_or(cfg.eatingzone_min_dist_machine);
        cfg.eatingzone_build_cost     = ez->get_as<double>("build_cost")->value_or(cfg.eatingzone_build_cost);
        cfg.eat_at_work_health_decay  = ez->get_as<double>("eat_at_work_health_decay")->value_or(cfg.eat_at_work_health_decay);
        cfg.eat_at_work_stress        = ez->get_as<double>("eat_at_work_stress")->value_or(cfg.eat_at_work_stress);
        cfg.eat_at_work_witness_radius = ez->get_as<int64_t>("witness_radius")->value_or(cfg.eat_at_work_witness_radius);
    }

    return cfg;
}
