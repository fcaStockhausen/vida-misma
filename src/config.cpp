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
        cfg.portion_size   = p->get_as<double>("portion_size")->value_or(cfg.portion_size);
    }

    // Conveyor
    if (auto c = config["conveyor"].as_table()) {
        cfg.conveyor_build_cost = c->get_as<double>("build_cost")->value_or(cfg.conveyor_build_cost);
        cfg.conveyor_decay_rate = c->get_as<double>("decay_rate")->value_or(cfg.conveyor_decay_rate);
        cfg.conveyor_throughput = c->get_as<double>("throughput")->value_or(cfg.conveyor_throughput);
        cfg.maintain_rate       = c->get_as<double>("maintain_rate")->value_or(cfg.maintain_rate);
    }

    // Dismantle
    if (auto dm = config["dismantle"].as_table()) {
        cfg.dismantle_return          = dm->get_as<double>("return")->value_or(cfg.dismantle_return);
        cfg.dismantle_rebuild_window  = dm->get_as<int64_t>("rebuild_window")->value_or(cfg.dismantle_rebuild_window);
    }

    // Urgency
    if (auto u = config["urgency"].as_table()) {
        cfg.urgency_alpha = u->get_as<double>("alpha")->value_or(cfg.urgency_alpha);
        cfg.urgency_curve_variant = u->get_as<int64_t>("curve_variant")->value_or(cfg.urgency_curve_variant);
        cfg.stress_model_variant = u->get_as<int64_t>("stress_model_variant")->value_or(cfg.stress_model_variant);
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
        cfg.sabotage_stress_threshold = st->get_as<double>("sabotage_threshold")->value_or(cfg.sabotage_stress_threshold);
        cfg.noncompliance_stress      = st->get_as<double>("noncompliance_stress")->value_or(cfg.noncompliance_stress);
        cfg.trauma_accumulation_rate  = st->get_as<double>("trauma_accumulation_rate")->value_or(cfg.trauma_accumulation_rate);
        cfg.trauma_resilience_impact  = st->get_as<double>("trauma_resilience_impact")->value_or(cfg.trauma_resilience_impact);
        cfg.trauma_social_impact      = st->get_as<double>("trauma_social_impact")->value_or(cfg.trauma_social_impact);
        cfg.redemption_chance         = st->get_as<double>("redemption_chance")->value_or(cfg.redemption_chance);
        cfg.suicide_chance            = st->get_as<double>("suicide_chance")->value_or(cfg.suicide_chance);
        cfg.selection_temperature     = st->get_as<double>("selection_temperature")->value_or(cfg.selection_temperature);
    }

    // Disease
    if (auto di = config["disease"].as_table()) {
        cfg.raw_food_disease_chance = di->get_as<double>("raw_food_chance")->value_or(cfg.raw_food_disease_chance);
        cfg.disease_severity        = di->get_as<double>("severity")->value_or(cfg.disease_severity);
        cfg.disease_recovery        = di->get_as<double>("recovery")->value_or(cfg.disease_recovery);
        cfg.disease_hunger_mult     = di->get_as<double>("hunger_mult")->value_or(cfg.disease_hunger_mult);
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
        cfg.quota_growth_rate       = ex->get_as<double>("quota_growth_rate")->value_or(cfg.quota_growth_rate);
        cfg.health_decay_per_miss   = ex->get_as<double>("health_decay_per_miss")->value_or(cfg.health_decay_per_miss);
        cfg.health_recovery_per_hit = ex->get_as<double>("health_recovery_per_hit")->value_or(cfg.health_recovery_per_hit);
        cfg.machine_break_threshold = ex->get_as<double>("machine_break_threshold")->value_or(cfg.machine_break_threshold);
        cfg.machine_break_prob      = ex->get_as<double>("machine_break_prob")->value_or(cfg.machine_break_prob);
        cfg.initial_food_per_agent  = ex->get_as<double>("initial_food_per_agent")->value_or(cfg.initial_food_per_agent);
        cfg.restructure_interval    = ex->get_as<int64_t>("restructure_interval")->value_or(cfg.restructure_interval);
        cfg.restructure_probability = ex->get_as<double>("restructure_probability")->value_or(cfg.restructure_probability);

        // Adversarial factory policy (Evaluator) + The Watcher
        cfg.adversary_intensity             = ex->get_as<double>("adversary_intensity")->value_or(cfg.adversary_intensity);
        cfg.restructure_temperature         = ex->get_as<double>("restructure_temperature")->value_or(cfg.restructure_temperature);
        cfg.strategic_weight                = ex->get_as<double>("strategic_weight")->value_or(cfg.strategic_weight);
        cfg.faction_target_bonus            = ex->get_as<double>("faction_target_bonus")->value_or(cfg.faction_target_bonus);
        cfg.watcher_influence_threshold     = ex->get_as<double>("watcher_influence_threshold")->value_or(cfg.watcher_influence_threshold);
        cfg.watcher_compliance_threshold    = ex->get_as<double>("watcher_compliance_threshold")->value_or(cfg.watcher_compliance_threshold);
        cfg.watcher_radius                  = ex->get_as<int64_t>("watcher_radius")->value_or(cfg.watcher_radius);
        cfg.noncompliance_report_threshold  = ex->get_as<double>("noncompliance_report_threshold")->value_or(cfg.noncompliance_report_threshold);
        cfg.report_severity                 = ex->get_as<double>("report_severity")->value_or(cfg.report_severity);
    }

    // Director
    if (auto dir = config["director"].as_table()) {
        std::string mode_str = dir->get_as<std::string>("mode")->value_or("");
        if (mode_str == "calm" || mode_str == "CALM")
            cfg.director_mode = DirectorMode::CALM;
        else if (mode_str == "production" || mode_str == "PRODUCTION")
            cfg.director_mode = DirectorMode::PRODUCTION_TEST;
        else
            cfg.director_mode = DirectorMode::NORMAL;
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
