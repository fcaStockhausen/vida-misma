#include "config.h"
#include <algorithm>
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
        cfg.allow_build         = a->get_as<bool>("allow_build")->value_or(cfg.allow_build);
    }

    if (auto culture = config["culture"].as_table()) {
        cfg.social_learning_enabled = culture->get_as<bool>("social_learning_enabled")
            ->value_or(cfg.social_learning_enabled);
        cfg.spatial_affinity_enabled = culture->get_as<bool>("spatial_affinity_enabled")
            ->value_or(cfg.spatial_affinity_enabled);
        cfg.artifact_effects_enabled = culture->get_as<bool>("artifact_effects_enabled")
            ->value_or(cfg.artifact_effects_enabled);
        cfg.creative_work_ticks = culture->get_as<int64_t>("creative_work_ticks")
            ->value_or(cfg.creative_work_ticks);
    }

    if (auto lifecycle = config["lifecycle"].as_table()) {
        cfg.natural_mortality_enabled = lifecycle->get_as<bool>("natural_mortality_enabled")
            ->value_or(cfg.natural_mortality_enabled);
        cfg.life_expectancy_ticks = lifecycle->get_as<int64_t>("life_expectancy_ticks")
            ->value_or(cfg.life_expectancy_ticks);
        cfg.lifespan_spread = lifecycle->get_as<double>("lifespan_spread")
            ->value_or(cfg.lifespan_spread);
        cfg.maturity_age_ticks = lifecycle->get_as<int64_t>("maturity_age_ticks")
            ->value_or(cfg.maturity_age_ticks);
        cfg.founder_age_min_ticks = lifecycle->get_as<int64_t>("founder_age_min_ticks")
            ->value_or(cfg.founder_age_min_ticks);
        cfg.founder_age_max_ticks = lifecycle->get_as<int64_t>("founder_age_max_ticks")
            ->value_or(cfg.founder_age_max_ticks);
        cfg.arrivals_enabled = lifecycle->get_as<bool>("arrivals_enabled")
            ->value_or(cfg.arrivals_enabled);
        cfg.arrival_rate_per_1000_ticks = lifecycle->get_as<double>("arrival_rate_per_1000_ticks")
            ->value_or(cfg.arrival_rate_per_1000_ticks);
        cfg.arrival_age_min_ticks = lifecycle->get_as<int64_t>("arrival_age_min_ticks")
            ->value_or(cfg.arrival_age_min_ticks);
        cfg.arrival_age_max_ticks = lifecycle->get_as<int64_t>("arrival_age_max_ticks")
            ->value_or(cfg.arrival_age_max_ticks);
        cfg.reproduction_enabled = lifecycle->get_as<bool>("reproduction_enabled")
            ->value_or(cfg.reproduction_enabled);
        cfg.reproduction_check_interval_ticks = lifecycle->get_as<int64_t>("reproduction_check_interval_ticks")
            ->value_or(cfg.reproduction_check_interval_ticks);
        cfg.reproduction_rate_per_1000_ticks = lifecycle->get_as<double>("reproduction_rate_per_1000_ticks")
            ->value_or(cfg.reproduction_rate_per_1000_ticks);
        cfg.reproduction_cooldown_ticks = lifecycle->get_as<int64_t>("reproduction_cooldown_ticks")
            ->value_or(cfg.reproduction_cooldown_ticks);
        cfg.personality_mutation_amplitude = lifecycle->get_as<double>("personality_mutation_amplitude")
            ->value_or(cfg.personality_mutation_amplitude);
        cfg.cohort_width_ticks = lifecycle->get_as<int64_t>("cohort_width_ticks")
            ->value_or(cfg.cohort_width_ticks);
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

    // Conveyor
    if (auto c = config["conveyor"].as_table()) {
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
        cfg.post_sabotage_pause_chance = st->get_as<double>("post_sabotage_pause_chance")
            ->value_or(cfg.post_sabotage_pause_chance);
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

    // External pressure
    if (auto ex = config["external"].as_table()) {
        cfg.quota_per_tick          = ex->get_as<double>("quota_per_tick")->value_or(cfg.quota_per_tick);
        cfg.quota_growth_rate       = ex->get_as<double>("quota_growth_rate")->value_or(cfg.quota_growth_rate);
        cfg.external_supply_variant = ex->get_as<int64_t>("supply_variant")->value_or(cfg.external_supply_variant);
        cfg.external_policy_variant = ex->get_as<int64_t>("policy_variant")->value_or(cfg.external_policy_variant);
        cfg.external_supply_response_ticks = ex->get_as<double>("supply_response_ticks")->value_or(cfg.external_supply_response_ticks);
        cfg.external_supply_floor   = ex->get_as<double>("supply_floor")->value_or(cfg.external_supply_floor);
        cfg.external_supply_low     = ex->get_as<double>("supply_low")->value_or(cfg.external_supply_low);
        cfg.external_supply_high    = ex->get_as<double>("supply_high")->value_or(cfg.external_supply_high);
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
        cfg.watcher_influence_threshold     = ex->get_as<double>("watcher_influence_threshold")->value_or(cfg.watcher_influence_threshold);
        cfg.watcher_compliance_threshold    = ex->get_as<double>("watcher_compliance_threshold")->value_or(cfg.watcher_compliance_threshold);
        cfg.watcher_radius                  = ex->get_as<int64_t>("watcher_radius")->value_or(cfg.watcher_radius);
        cfg.noncompliance_report_threshold  = ex->get_as<double>("noncompliance_report_threshold")->value_or(cfg.noncompliance_report_threshold);
        cfg.report_severity                 = ex->get_as<double>("report_severity")->value_or(cfg.report_severity);
    }
    if (cfg.external_supply_variant != 0 && cfg.external_supply_variant != 1) {
        cfg.external_supply_variant = 1;
    }
    if (cfg.external_policy_variant != 0 && cfg.external_policy_variant != 1) {
        cfg.external_policy_variant = 1;
    }
    cfg.creative_work_ticks = std::max(1, cfg.creative_work_ticks);
    cfg.life_expectancy_ticks = std::max(1, cfg.life_expectancy_ticks);
    cfg.lifespan_spread = std::clamp(cfg.lifespan_spread, 0.0f, 0.95f);
    cfg.maturity_age_ticks = std::max(0, cfg.maturity_age_ticks);
    cfg.founder_age_min_ticks = std::max(0, cfg.founder_age_min_ticks);
    cfg.founder_age_max_ticks = std::max(cfg.founder_age_min_ticks, cfg.founder_age_max_ticks);
    cfg.arrival_rate_per_1000_ticks = std::max(0.0f, cfg.arrival_rate_per_1000_ticks);
    cfg.arrival_age_min_ticks = std::max(0, cfg.arrival_age_min_ticks);
    cfg.arrival_age_max_ticks = std::max(cfg.arrival_age_min_ticks, cfg.arrival_age_max_ticks);
    cfg.reproduction_check_interval_ticks = std::max(1, cfg.reproduction_check_interval_ticks);
    cfg.reproduction_rate_per_1000_ticks = std::max(0.0f, cfg.reproduction_rate_per_1000_ticks);
    cfg.reproduction_cooldown_ticks = std::max(0, cfg.reproduction_cooldown_ticks);
    cfg.personality_mutation_amplitude = std::clamp(
        cfg.personality_mutation_amplitude, 0.0f, 0.5f);
    cfg.cohort_width_ticks = std::max(1, cfg.cohort_width_ticks);
    cfg.external_supply_response_ticks = std::max(1.0f, cfg.external_supply_response_ticks);
    cfg.external_supply_floor = std::clamp(cfg.external_supply_floor, 0.0f, 1.0f);
    cfg.external_supply_low = std::clamp(cfg.external_supply_low, 0.0f, 0.9999f);
    cfg.external_supply_high = std::clamp(
        cfg.external_supply_high, cfg.external_supply_low + 0.0001f, 1.0f);
    cfg.restructure_interval = std::max(1, cfg.restructure_interval);
    cfg.restructure_probability = std::clamp(cfg.restructure_probability, 0.0f, 1.0f);

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
