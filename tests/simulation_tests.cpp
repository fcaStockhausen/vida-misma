#include "simulation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <queue>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    failures++;
}

Config test_config() {
    Config cfg;
    cfg.grid_width = 40;
    cfg.grid_height = 30;
    cfg.initial_population = 12;
    cfg.max_population = 32;
    cfg.seed = 4242;
    cfg.director_mode = DirectorMode::NORMAL;
    cfg.urgency_curve_variant = 3;
    cfg.stress_model_variant = 1;
    cfg.raw_food_disease_chance = 0.0f;
    cfg.post_sabotage_pause_chance = 0.0f;
    cfg.suicide_chance = 0.0f;
    cfg.restructure_probability = 0.0f;
    cfg.initial_food_per_agent = 5.0f;
    cfg.inv_food_cap = 2.0f;
    cfg.natural_mortality_enabled = false;
    cfg.arrivals_enabled = false;
    cfg.reproduction_enabled = false;
    return cfg;
}

entt::entity find_agent(Simulation& sim, int id) {
    auto view = sim.registry().view<const AgentComponent>();
    for (auto e : view) {
        if (sim.registry().get<AgentComponent>(e).id == id) return e;
    }
    return entt::null;
}

uint64_t total_deaths(const SimulationMetrics& metrics) {
    uint64_t total = 0;
    for (uint64_t count : metrics.deaths) total += count;
    return total;
}

int factual_death_events(const Simulation& sim) {
    return static_cast<int>(
        sim.chronicle().count_of_type(EventType::DIED_STARVATION) +
        sim.chronicle().count_of_type(EventType::DIED_EXHAUSTION) +
        sim.chronicle().count_of_type(EventType::DIED_BREAKDOWN) +
        sim.chronicle().count_of_type(EventType::DIED_COLLAPSE) +
        sim.chronicle().count_of_type(EventType::DIED_SUICIDE) +
        sim.chronicle().count_of_type(EventType::DIED_NATURAL));
}

struct Snapshot {
    SimulationMetrics metrics;
    int tick = 0;
    int alive = 0;
    int machines = 0;
    int conveyors = 0;
    float health = 0.0f;
    float storage_food = 0.0f;
    float storage_output = 0.0f;
    size_t events = 0;

    bool operator==(const Snapshot&) const = default;
};

Snapshot run_snapshot(const Config& cfg, int ticks, bool render_narrative = false) {
    Simulation sim(cfg);
    for (int i = 0; i < ticks; i++) {
        sim.advance();
        if (render_narrative) {
            for (const ChronicleEvent* event : sim.chronicle().by_agent(0)) {
                (void)event->narrative(Archetype::FOREMAN, StressState::NORMAL);
            }
        }
    }
    return {
        sim.metrics(),
        sim.tick(),
        sim.alive_count(),
        sim.built_machine_count(),
        sim.built_conveyor_count(),
        sim.factory_health(),
        sim.total_storage_food(),
        sim.total_storage_output(),
        sim.chronicle().size(),
    };
}

void test_metrics_contract() {
    Config cfg = test_config();
    cfg.external_supply_variant = 0;
    cfg.quota_growth_rate = 0.05f;
    Simulation sim(cfg);
    constexpr int ticks = 500;
    for (int i = 0; i < ticks; i++) sim.advance();

    const auto& metrics = sim.metrics();
    check(sim.tick() == ticks, "simulation advances the requested number of ticks");
    check(metrics.ticks_advanced == static_cast<uint64_t>(ticks),
          "metrics tick count matches Simulation::tick");
    check(sim.alive_count() >= 0 && sim.alive_count() <= cfg.max_population,
          "population remains within valid bounds");
    check(metrics.quota_demand > 0.0, "normal mode accumulates actual quota demand");
    double expected_demand = 0.0;
    float expected_quota = cfg.quota_per_tick;
    float quota_cap = cfg.quota_per_tick * 3.0f;
    for (int i = 0; i < ticks; i++) {
        expected_quota = std::min(quota_cap, expected_quota + cfg.quota_growth_rate);
        expected_demand += expected_quota;
    }
    check(std::abs(metrics.quota_demand - expected_demand) < 0.0001,
          "quota demand matches the runtime escalation and cap formula");
    check(std::abs(metrics.output_shipped - sim.total_food_shipped()) < 0.0001,
          "metrics output shipped matches the runtime counter");

    uint64_t selections = 0;
    uint64_t machine_builds = 0;
    uint64_t target_failures = 0;
    for (size_t i = 0; i < METRIC_ACTION_COUNT; i++) {
        selections += metrics.action_selected[i];
        target_failures += metrics.target_failures[i];
        check(metrics.target_lookups[i] <= metrics.action_selected[i],
              "target lookups never exceed per-tick action selections");
        check(metrics.target_failures[i] <= metrics.target_lookups[i],
              "target failures never exceed lookups");
        check(metrics.action_executed[i] <= metrics.target_reached[i],
              "executed effects never exceed reached-target ticks");
        check(std::abs(metrics.utility_final_sum[i]
                       - metrics.utility_self_sum[i] - metrics.utility_factory_sum[i]
                       + metrics.utility_cost_sum[i] + metrics.utility_risk_sum[i]) < 0.001,
              "utility decomposition sums to final utility");
    }
    for (uint64_t count : metrics.machines_built) machine_builds += count;
    check(selections > 0, "action selections are recorded");
    check(target_failures == 0,
          "every selected action has a feasible target plan");
    check(metrics.utility_samples[metric_index(ActionType::IDLE)] > 0
          && metrics.feasible_samples[metric_index(ActionType::IDLE)]
             == metrics.utility_samples[metric_index(ActionType::IDLE)],
          "IDLE is an explicit always-feasible utility candidate");
    check(total_deaths(metrics) == static_cast<uint64_t>(
              sim.ever_created() - sim.alive_count()),
          "historical population accounting counts each death once");
    check(metrics.initial_machines_active[metric_index(MachineType::Food)] > 0
          && metrics.initial_machines_active[metric_index(MachineType::Materials)] > 0
          && metrics.initial_machines_active[metric_index(MachineType::Output)] > 0,
          "test run inherits all three machine tiers");
    check(metrics.initial_minimum_chain_present,
          "initial metrics record an Exit-connected minimum chain");
    check(sim.total_machines_built() == static_cast<int>(machine_builds),
          "legacy and structured machine-build counters agree");
    check(sim.chronicle().count_of_type(EventType::BUILT_MACHINE) == machine_builds,
          "machine completion events exclude inherited infrastructure");
    check(metrics.agent_action_ticks.size() >= static_cast<size_t>(sim.ever_created())
          && metrics.agent_productive_effect_ticks.size()
             >= static_cast<size_t>(sim.ever_created()),
          "per-agent ledgers cover every historical identity");
    check(metrics.spatial_persistence_samples > 0
          && metrics.personality_distance_samples > 0
          && metrics.social_modularity_samples > 0
          && metrics.community_stability_samples > 0,
          "emergence metrics accumulate deterministic temporal samples");

    for (size_t i = 0; i < METRIC_RESOURCE_COUNT; i++) {
        check(std::isfinite(metrics.resources_regenerated[i]), "regeneration metric is finite");
        check(std::isfinite(metrics.resources_produced[i]), "production metric is finite");
        check(std::isfinite(metrics.resources_consumed[i]), "consumption metric is finite");
        check(metrics.resources_regenerated[i] >= 0.0, "regeneration metric is nonnegative");
        check(metrics.regeneration_requested[i] <= metrics.regeneration_base[i] + 0.0001,
              "requested regeneration never exceeds its base rate");
        check(metrics.resources_produced[i] >= 0.0, "production metric is nonnegative");
        check(metrics.resources_consumed[i] >= 0.0, "consumption metric is nonnegative");
        check(metrics.resources_lost[i] >= 0.0, "physical-loss metric is nonnegative");
    }
}

bool grid_reachable(const Grid& grid, std::pair<int, int> start,
                    std::pair<int, int> target) {
    std::vector<uint8_t> visited(grid.width() * grid.height(), 0);
    std::queue<std::pair<int, int>> pending;
    pending.push(start);
    visited[start.second * grid.width() + start.first] = 1;
    constexpr int dx[] = {1, -1, 0, 0};
    constexpr int dy[] = {0, 0, 1, -1};
    while (!pending.empty()) {
        auto [x, y] = pending.front();
        pending.pop();
        if (x == target.first && y == target.second) return true;
        for (int direction = 0; direction < 4; direction++) {
            int nx = x + dx[direction], ny = y + dy[direction];
            if (!grid.is_walkable(nx, ny)) continue;
            int index = ny * grid.width() + nx;
            if (visited[index]) continue;
            visited[index] = 1;
            pending.push({nx, ny});
        }
    }
    return false;
}

bool same_generated_grid(const Grid& left, const Grid& right) {
    if (left.width() != right.width() || left.height() != right.height()) return false;
    for (int y = 0; y < left.height(); y++)
        for (int x = 0; x < left.width(); x++) {
            if (left.at(x, y) != right.at(x, y)) return false;
            const auto& a = left.data_at(x, y);
            const auto& b = right.data_at(x, y);
            if (a.resource_amount != b.resource_amount
                || a.resource_max != b.resource_max
                || a.resource_regen != b.resource_regen
                || a.built != b.built
                || a.build_progress != b.build_progress
                || a.build_cost != b.build_cost
                || a.machine_type != b.machine_type
                || a.built_on_resource != b.built_on_resource
                || a.claimed_by != b.claimed_by
                || a.storage_capacity != b.storage_capacity
                || a.stored_food != b.stored_food
                || a.stored_raw_food != b.stored_raw_food
                || a.stored_raw_material != b.stored_raw_material
                || a.stored_construction_material != b.stored_construction_material
                || a.stored_output != b.stored_output
                || a.conveyor_dir != b.conveyor_dir
                || a.conveyor_condition != b.conveyor_condition
                || a.conveyor_contents_type != b.conveyor_contents_type
                || a.conveyor_contents != b.conveyor_contents
                || a.maintenance_priority != b.maintenance_priority
                || a.dismantled_by != b.dismantled_by
                || a.dismantled_at_tick != b.dismantled_at_tick
                || a.original_type != b.original_type
                || a.hidden_space_occupancy != b.hidden_space_occupancy
                || a.occupancy_capacity != b.occupancy_capacity
                || a.overcapacity_ticks != b.overcapacity_ticks) {
                return false;
            }
        }
    return true;
}

void test_inherited_factory_map_properties() {
    bool all_valid = true;
    bool all_deterministic = true;
    for (int seed = 0; seed < 20; seed++) {
        Config cfg = test_config();
        cfg.seed = seed;
        cfg.initial_population = 0;
        cfg.max_population = 4;
        Simulation sim(cfg);
        Simulation replay(cfg);
        const auto& grid = sim.grid();
        all_deterministic &= same_generated_grid(grid, replay.grid());

        auto exits = grid.find_all(TileType::Exit);
        auto entrances = grid.find_all(TileType::Entrance);
        auto machines = grid.find_all(TileType::Machine);
        auto conveyors = grid.find_all(TileType::Conveyor);
        auto storages = grid.find_all(TileType::Storage);
        all_valid &= exits.size() == 1;
        all_valid &= entrances.size() == 1;
        if (entrances.size() == 1) {
            all_valid &= entrances.front().first == 0;
            all_valid &= grid.is_walkable(1, entrances.front().second);
        }
        all_valid &= machines.size() == 3;
        all_valid &= conveyors.size() == 4;
        all_valid &= storages.size() >= 3;
        all_valid &= grid.find_all(TileType::FoodSource).size() >= 10;
        all_valid &= grid.find_all(TileType::ScrapPile).size() >= 12;
        all_valid &= grid.minimum_chain_present();
        all_valid &= grid.exit_connected_output_machine_count() == 1;

        auto floors = grid.find_all(TileType::Floor);
        all_valid &= !floors.empty();
        if (entrances.size() == 1 && !machines.empty())
            all_valid &= grid_reachable(grid, entrances.front(), machines.front());
        for (auto [x, y] : machines) {
            const auto& data = grid.data_at(x, y);
            all_valid &= data.built && data.build_progress == data.build_cost;
            all_valid &= !floors.empty() && grid_reachable(grid, floors.front(), {x, y});
            if (data.machine_type == MachineType::Food
                || data.machine_type == MachineType::Materials) {
                all_valid &= data.built_on_resource
                    && data.resource_amount > 0.0f
                    && data.resource_max > 0.0f
                    && data.resource_regen > 0.0f;
            } else {
                all_valid &= !data.built_on_resource;
                all_valid &= grid.machine_connected_to_exit(x, y);
            }
        }
        for (auto [x, y] : conveyors) {
            const auto& data = grid.data_at(x, y);
            all_valid &= data.built && data.build_progress == data.build_cost;
            all_valid &= data.conveyor_condition >= 0.2f
                && data.conveyor_condition < 1.0f;
            all_valid &= data.conveyor_contents == 0.0f;
        }

        const auto& metrics = sim.metrics();
        all_valid &= metrics.initial_machines_active[metric_index(MachineType::Food)] == 1;
        all_valid &= metrics.initial_machines_active[metric_index(MachineType::Materials)] == 1;
        all_valid &= metrics.initial_machines_active[metric_index(MachineType::Output)] == 1;
        all_valid &= metrics.initial_conveyors_active == 4;
        all_valid &= metrics.initial_storages_active >= 3;
        all_valid &= metrics.initial_exit_connected_outputs == 1;
        all_valid &= metrics.initial_minimum_chain_present;
        all_valid &= sim.total_machines_built() == 0;
        all_valid &= sim.chronicle().count_of_type(EventType::BUILT_MACHINE) == 0;
    }
    check(all_valid,
          "20 generated seeds contain a reachable degraded inherited chain");
    check(all_deterministic,
          "same-seed inherited factory generation is deterministic");
}

std::pair<int, int> machine_position(const Simulation& sim, MachineType type) {
    for (auto [x, y] : sim.grid().find_all(TileType::Machine)) {
        const auto& data = sim.grid().data_at(x, y);
        if (data.built && data.machine_type == type) return {x, y};
    }
    return {-1, -1};
}

void test_inherited_chain_operates_without_build() {
    Config cfg = test_config();
    cfg.initial_population = 3;
    cfg.max_population = 4;
    cfg.restructure_probability = 0.0f;
    Simulation sim(cfg);
    const MachineType types[] = {
        MachineType::Food, MachineType::Materials, MachineType::Output,
    };
    for (int id = 0; id < 3; id++) {
        auto entity = find_agent(sim, id);
        auto position = machine_position(sim, types[id]);
        sim.registry().get<PositionComponent>(entity) = {position.first, position.second};
        auto& action = sim.registry().get<ActionComponent>(entity);
        action.current = ActionType::WORK;
        action.sticky_action = ActionType::WORK;
        action.sticky_ticks = 20;
        sim.registry().get<NeedsComponent>(entity) = {};
        auto& inventory = sim.registry().get<InventoryComponent>(entity);
        inventory = {};
        if (types[id] == MachineType::Food) inventory.raw_food = 1.0f;
        if (types[id] == MachineType::Materials) inventory.raw_material = 1.0f;
        if (types[id] == MachineType::Output) inventory.construction_material = 1.0f;
    }
    for (int tick = 0; tick < 8; tick++) sim.advance();

    const auto& metrics = sim.metrics();
    check(metrics.resources_produced[metric_index(ResourceType::FOOD)] > 0.0
          && metrics.resources_produced[metric_index(ResourceType::CONSTRUCTION_MATERIAL)] > 0.0
          && metrics.resources_produced[metric_index(ResourceType::OUTPUT)] > 0.0,
          "inherited machines produce all three stages without construction");
    check(metrics.output_shipped > 0.0,
          "inherited output route ships through Exit without BUILD");
    check(metrics.action_selected[metric_index(ActionType::BUILD)] == 0
          && metrics.action_executed[metric_index(ActionType::BUILD)] == 0,
          "functional inherited-chain fixture selects and executes no BUILD");
    check(sim.total_machines_built() == 0
          && sim.chronicle().count_of_type(EventType::BUILT_MACHINE) == 0,
          "inherited operation creates no resident construction event");
}

void test_inherited_conveyor_is_maintainable() {
    Config cfg = test_config();
    cfg.initial_population = 1;
    cfg.max_population = 2;
    cfg.director_mode = DirectorMode::CALM;
    cfg.conveyor_decay_rate = 0.01f;
    cfg.maintain_rate = 0.08f;
    Simulation sim(cfg);

    auto conveyors = sim.grid().find_all(TileType::Conveyor);
    auto target = *std::min_element(conveyors.begin(), conveyors.end(),
        [&](const auto& left, const auto& right) {
            return sim.grid().data_at(left.first, left.second).conveyor_condition
                < sim.grid().data_at(right.first, right.second).conveyor_condition;
        });
    float before = sim.grid().data_at(target.first, target.second).conveyor_condition;
    auto agent = find_agent(sim, 0);
    std::pair<int, int> adjacent = {target.first, target.second - 1};
    sim.grid_mut().set(adjacent.first, adjacent.second, TileType::Floor);
    sim.registry().get<PositionComponent>(agent) = {adjacent.first, adjacent.second};
    auto& action = sim.registry().get<ActionComponent>(agent);
    action.current = ActionType::MAINTAIN;
    action.sticky_action = ActionType::MAINTAIN;
    action.sticky_ticks = 10;
    sim.registry().get<NeedsComponent>(agent) = {};
    sim.advance();
    check(sim.grid().data_at(target.first, target.second).conveyor_condition > before,
          "inherited degraded conveyor can be improved through MAINTAIN");
}

void test_artistic_and_social_skills_progress() {
    Config cfg = test_config();
    cfg.initial_population = 2;
    cfg.max_population = 4;
    cfg.director_mode = DirectorMode::CALM;
    Simulation sim(cfg);
    auto artist = find_agent(sim, 0);
    auto socializer = find_agent(sim, 1);
    sim.grid_mut().set(10, 10, TileType::OpenSpace);
    sim.grid_mut().set(11, 10, TileType::OpenSpace);
    sim.registry().get<PositionComponent>(artist) = {10, 10};
    sim.registry().get<PositionComponent>(socializer) = {11, 10};
    sim.registry().get<NeedsComponent>(artist).expression = 1.0f;
    sim.registry().get<NeedsComponent>(socializer).social = 1.0f;

    auto& create = sim.registry().get<ActionComponent>(artist);
    create.current = ActionType::CREATE;
    create.sticky_action = ActionType::CREATE;
    create.sticky_ticks = 10;
    auto& socialize = sim.registry().get<ActionComponent>(socializer);
    socialize.current = ActionType::SOCIALIZE;
    socialize.sticky_action = ActionType::SOCIALIZE;
    socialize.sticky_ticks = 10;

    sim.advance();
    const auto& artistic = sim.registry().get<SkillsComponent>(artist);
    const auto& social = sim.registry().get<SkillsComponent>(socializer);
    check(artistic.xp_art > 0.0f && artistic.artistic > 0.0f,
          "effective CREATE advances artistic skill");
    check(social.xp_social > 0.0f && social.social_skill > 0.0f,
          "effective SOCIALIZE advances social skill");
}

void test_social_evidence_is_directional() {
    SocialFabric social(4);
    social.record_copresence(2, 3, 1);
    check(social.get_rel(2, 3).familiarity > 0.0f
          && social.get_rel(2, 3).trust == 0.0f,
          "copresence builds familiarity without inventing trust");
    social.record_collaboration(2, 3, 2);
    check(social.get_rel(2, 3).trust > 0.0f
          && social.get_rel(3, 2).trust > 0.0f,
          "effective collaboration creates reciprocal trust evidence");

    social.get_rel(1, 0).trust = 0.4f;
    social.record_negative_observation(0, 1, 10, 0.2f);
    check(std::abs(social.get_rel(0, 1).trust + 0.2f) < 0.0001f,
          "observer lowers only its trust toward the observed actor");
    check(std::abs(social.get_rel(1, 0).trust - 0.4f) < 0.0001f,
          "negative observation does not invent reverse-edge hostility");
    check(social.get_rel(0, 1).familiarity > 0.0f,
          "conflict is recorded as familiarity evidence");

    float recipient_before = social.get_rel(1, 0).trust;
    float helper_before = social.get_rel(0, 1).trust;
    social.record_help(0, 1, 11, 0.5f);
    check(social.get_rel(1, 0).trust > recipient_before,
          "recipient gains directed trust in a helper");
    check(std::abs(social.get_rel(0, 1).trust - helper_before) < 0.0001f,
          "help does not grant the helper reciprocal trust by fiat");
}

void prepare_label_neutrality_fixture(Simulation& sim, int first_label, int second_label) {
    auto first = find_agent(sim, 0);
    auto second = find_agent(sim, 1);
    sim.grid_mut().set(10, 10, TileType::Floor);
    sim.grid_mut().set(11, 10, TileType::Floor);
    sim.registry().get<PositionComponent>(first) = {10, 10};
    sim.registry().get<PositionComponent>(second) = {11, 10};
    sim.registry().get<AgentComponent>(first).community_id = first_label;
    sim.registry().get<AgentComponent>(second).community_id = second_label;
    for (auto entity : {first, second}) {
        sim.registry().get<NeedsComponent>(entity) = {};
        sim.registry().get<NeedsComponent>(entity).social = 1.0f;
        auto& action = sim.registry().get<ActionComponent>(entity);
        action.current = ActionType::SOCIALIZE;
        action.sticky_action = ActionType::SOCIALIZE;
        action.sticky_ticks = 10;
    }
}

void test_graph_labels_are_behavior_neutral() {
    Config cfg = test_config();
    cfg.initial_population = 2;
    cfg.max_population = 4;
    cfg.director_mode = DirectorMode::CALM;
    cfg.hunger_decay = cfg.rest_decay = cfg.social_decay = 0.0f;
    cfg.expression_decay = cfg.purpose_decay = 0.0f;
    Simulation same_label(cfg);
    Simulation different_labels(cfg);
    prepare_label_neutrality_fixture(same_label, 7, 7);
    prepare_label_neutrality_fixture(different_labels, 7, 9);

    same_label.advance();
    different_labels.advance();
    for (int id = 0; id < 2; id++) {
        auto same = find_agent(same_label, id);
        auto different = find_agent(different_labels, id);
        const auto& same_needs = same_label.registry().get<NeedsComponent>(same);
        const auto& different_needs = different_labels.registry().get<NeedsComponent>(different);
        check(std::abs(same_needs.social - different_needs.social) < 0.0001f
              && std::abs(same_needs.meaning - different_needs.meaning) < 0.0001f,
              "graph-community labels do not grant social or meaning effects");
    }
    check(same_label.social().get_rel(0, 1).trust
          == different_labels.social().get_rel(0, 1).trust,
          "graph-community labels do not alter trust learning");
}

void test_create_completes_discrete_work_units_on_ordinary_floor() {
    Config cfg = test_config();
    cfg.initial_population = 1;
    cfg.max_population = 2;
    cfg.director_mode = DirectorMode::CALM;
    cfg.creative_work_ticks = 3;
    cfg.hunger_decay = cfg.rest_decay = cfg.social_decay = 0.0f;
    cfg.expression_decay = cfg.purpose_decay = 0.0f;
    Simulation sim(cfg);
    auto artist = find_agent(sim, 0);
    sim.grid_mut().set(10, 10, TileType::Floor);
    sim.registry().get<PositionComponent>(artist) = {10, 10};
    sim.registry().get<NeedsComponent>(artist) = {};
    sim.registry().get<NeedsComponent>(artist).expression = 1.0f;
    auto& action = sim.registry().get<ActionComponent>(artist);
    action.current = ActionType::CREATE;
    action.sticky_action = ActionType::CREATE;
    action.sticky_ticks = 10;

    sim.advance();
    sim.advance();
    check(sim.artifacts_created() == 0,
          "partial creative work does not emit per-tick artifacts");
    sim.advance();
    check(sim.artifacts_created() == 1
          && sim.registry().get<CreativeWorkComponent>(artist).completed_units == 1,
          "one completed creative work unit emits one artifact");
    check(!sim.registry().get<PlaceMemoryComponent>(artist).places.empty()
          && sim.registry().get<PlaceMemoryComponent>(artist).places.front().affinity > 0.0f,
          "effective creative work builds positive personal place memory");
    check(sim.grid().at(10, 10) == TileType::Floor,
          "CREATE is effective outside a designated OpenSpace");
}

void test_unseen_stock_does_not_change_decision() {
    Config cfg = test_config();
    cfg.initial_population = 1;
    cfg.max_population = 2;
    Simulation empty(cfg);
    Simulation stocked(cfg);
    auto empty_agent = find_agent(empty, 0);
    auto stocked_agent = find_agent(stocked, 0);
    empty.registry().get<PositionComponent>(empty_agent) = {3, 3};
    stocked.registry().get<PositionComponent>(stocked_agent) = {3, 3};

    constexpr int sx = 35, sy = 25;
    empty.grid_mut().set(sx, sy, TileType::Storage);
    stocked.grid_mut().set(sx, sy, TileType::Storage);
    empty.grid_mut().data_at(sx, sy) = {};
    stocked.grid_mut().data_at(sx, sy) = {};
    empty.grid_mut().data_at(sx, sy).built = true;
    stocked.grid_mut().data_at(sx, sy).built = true;
    empty.grid_mut().data_at(sx, sy).storage_capacity = 200.0f;
    stocked.grid_mut().data_at(sx, sy).storage_capacity = 200.0f;
    stocked.grid_mut().data_at(sx, sy).stored_food = 100.0f;

    empty.advance();
    stocked.advance();
    const auto& empty_action = empty.registry().get<ActionComponent>(empty_agent);
    const auto& stocked_action = stocked.registry().get<ActionComponent>(stocked_agent);
    bool same_utilities = true;
    for (size_t i = 0; i < static_cast<size_t>(ActionType::COUNT); i++) {
        same_utilities &= empty_action.last_utility[i].final
            == stocked_action.last_utility[i].final;
        same_utilities &= empty_action.last_utility[i].feasible
            == stocked_action.last_utility[i].feasible;
    }
    check(same_utilities && empty_action.current == stocked_action.current
          && empty_action.target_x == stocked_action.target_x
          && empty_action.target_y == stocked_action.target_y,
          "unseen distant stock does not change utility or target");
}

void test_build_can_be_disabled() {
    Config cfg = test_config();
    cfg.allow_build = false;
    cfg.sabotage_stress_threshold = 2.0f;
    Simulation sim(cfg);
    for (int tick = 0; tick < 100; tick++) sim.advance();

    const auto& metrics = sim.metrics();
    check(metrics.action_selected[metric_index(ActionType::BUILD)] == 0
          && metrics.action_executed[metric_index(ActionType::BUILD)] == 0,
          "allow_build=false excludes BUILD from selection and execution");
    check(metrics.action_selected[metric_index(ActionType::SABOTAGE)] == 0,
          "utility-zero actions receive no softmax weight");
}

void test_conveyor_planning_is_pure() {
    Grid grid(16, 10);
    grid.set(2, 4, TileType::Machine);
    auto& machine = grid.data_at(2, 4);
    machine.built = true;
    machine.machine_type = MachineType::Output;
    grid.set(14, 4, TileType::Exit);
    grid.set(13, 4, TileType::Storage);
    grid.data_at(13, 4).built = true;
    grid.data_at(13, 4).storage_capacity = 20.0f;
    Grid before = grid;

    auto site = grid.find_conveyor_build_site(2, 4);
    check(site.x >= 0, "conveyor planner finds a segment for an unserved OutputMachine");
    check(same_generated_grid(before, grid),
          "conveyor site queries do not mutate the grid");
}

void test_output_machine_can_be_rebuilt() {
    Config cfg = test_config();
    cfg.initial_population = 1;
    cfg.max_population = 2;
    cfg.director_mode = DirectorMode::CALM;
    Simulation sim(cfg);

    auto output = machine_position(sim, MachineType::Output);
    sim.grid_mut().set(output.first, output.second, TileType::Floor);
    sim.grid_mut().data_at(output.first, output.second) = {};
    auto site = sim.grid().find_output_machine_site(output.first, output.second);
    check(site.first >= 0, "OutputMachine recovery fixture has a valid site");

    auto agent = find_agent(sim, 0);
    sim.registry().get<PositionComponent>(agent) = {site.first, site.second};
    sim.registry().get<NeedsComponent>(agent) = {};
    auto& inventory = sim.registry().get<InventoryComponent>(agent);
    inventory = {};
    inventory.construction_material = 1.0f;
    auto& action = sim.registry().get<ActionComponent>(agent);
    action.current = ActionType::BUILD;
    action.sticky_action = ActionType::BUILD;
    action.sticky_ticks = 20;

    sim.advance();
    check(sim.grid().at(site.first, site.second) == TileType::Machine,
          "BUILD places the routed OutputMachine frame");
    sim.advance();
    auto rebuilt = machine_position(sim, MachineType::Output);
    check(rebuilt.first >= 0,
          "BUILD routes to and completes a missing OutputMachine with c_mat");
    check(sim.total_machines_built() == 1,
          "OutputMachine recovery records one resident construction event");
}

void test_isolated_agent_cannot_explore() {
    Config cfg = test_config();
    cfg.initial_population = 1;
    cfg.max_population = 1;
    cfg.director_mode = DirectorMode::CALM;
    Simulation sim(cfg);
    auto agent = find_agent(sim, 0);
    auto position = sim.registry().get<PositionComponent>(agent);
    for (int y = 0; y < sim.grid().height(); y++)
        for (int x = 0; x < sim.grid().width(); x++)
            if (x != position.x || y != position.y)
                sim.grid_mut().set(x, y, TileType::Wall);

    auto& action = sim.registry().get<ActionComponent>(agent);
    action.current = ActionType::EXPLORE;
    action.sticky_action = ActionType::EXPLORE;
    action.sticky_ticks = 10;
    sim.advance();
    check(!action.last_utility[metric_index(ActionType::EXPLORE)].feasible
          && action.current != ActionType::EXPLORE,
          "an isolated agent excludes EXPLORE without indexing an empty destination set");
}

void test_conveyors_do_not_mix_resources() {
    Config cfg = test_config();
    cfg.initial_population = 0;
    cfg.director_mode = DirectorMode::CALM;
    cfg.conveyor_decay_rate = 0.0f;
    Simulation sim(cfg);
    auto conveyors = sim.grid().find_all(TileType::Conveyor);
    std::sort(conveyors.begin(), conveyors.end());
    auto upstream = conveyors[0];
    auto downstream = conveyors[1];
    auto& up = sim.grid_mut().data_at(upstream.first, upstream.second);
    auto& down = sim.grid_mut().data_at(downstream.first, downstream.second);
    up.conveyor_dir = ConveyorDir::E;
    up.conveyor_contents = 0.2f;
    up.conveyor_contents_type = ResourceType::OUTPUT;
    down.conveyor_dir = ConveyorDir::N;
    down.conveyor_contents = 0.2f;
    down.conveyor_contents_type = ResourceType::FOOD;

    sim.advance();
    check(std::abs(up.conveyor_contents - 0.2f) < 0.0001f
          && std::abs(down.conveyor_contents - 0.2f) < 0.0001f
          && down.conveyor_contents_type == ResourceType::FOOD,
          "a conveyor rejects input of a different resource type");
}

void test_production_assessment_counts_buffers() {
    Grid grid(8, 8);
    grid.set(2, 2, TileType::Machine);
    auto& machine = grid.data_at(2, 2);
    machine.built = true;
    machine.machine_type = MachineType::Output;
    machine.stored_construction_material = 1.0f;
    machine.stored_output = 2.0f;
    grid.set(3, 2, TileType::Conveyor);
    auto& cmat_belt = grid.data_at(3, 2);
    cmat_belt.built = true;
    cmat_belt.conveyor_contents = 0.5f;
    cmat_belt.conveyor_contents_type = ResourceType::CONSTRUCTION_MATERIAL;
    grid.set(4, 2, TileType::Conveyor);
    auto& output_belt = grid.data_at(4, 2);
    output_belt.built = true;
    output_belt.conveyor_contents = 0.75f;
    output_belt.conveyor_contents_type = ResourceType::OUTPUT;

    auto production = ProductionChain::assess(grid, 1, 0.0f);
    check(std::abs(production.construction_material - 1.5f) < 0.0001f
          && std::abs(production.output - 2.75f) < 0.0001f,
          "production assessment includes machine and conveyor buffers");
}

void test_broken_source_machine_still_regenerates() {
    Config cfg = test_config();
    cfg.initial_population = 0;
    cfg.director_mode = DirectorMode::CALM;
    Simulation sim(cfg);
    auto food = machine_position(sim, MachineType::Food);
    auto& machine = sim.grid_mut().data_at(food.first, food.second);
    machine.built = false;
    machine.resource_amount = 0.0f;
    machine.resource_max = 1.0f;
    machine.resource_regen = 0.2f;
    machine.stored_raw_food = 0.0f;

    sim.advance();
    check(machine.resource_amount > 0.0f && machine.stored_raw_food == 0.0f,
          "underlying resources regenerate while a source-backed machine is broken");
}

void test_output_haul_requires_storage_arrival() {
    Config cfg = test_config();
    cfg.initial_population = 1;
    cfg.max_population = 2;
    cfg.director_mode = DirectorMode::CALM;
    Simulation sim(cfg);
    auto exits = sim.grid().find_all(TileType::Exit);
    auto [ex, ey] = exits.front();
    int storage_x = ex - 1;
    auto& storage = sim.grid_mut().data_at(storage_x, ey);
    float before = storage.stored_output;

    auto agent = find_agent(sim, 0);
    sim.registry().get<PositionComponent>(agent) = {storage_x - 3, ey};
    auto& inventory = sim.registry().get<InventoryComponent>(agent);
    inventory = {};
    inventory.output = 1.0f;
    auto& action = sim.registry().get<ActionComponent>(agent);
    action.current = ActionType::IDLE;
    action.sticky_action = ActionType::IDLE;
    action.sticky_ticks = 10;

    sim.advance();
    check(std::abs(inventory.output - 1.0f) < 0.0001f
          && std::abs(storage.stored_output - before) < 0.0001f,
          "output is not deposited remotely near Exit storage");
    sim.registry().get<PositionComponent>(agent) = {storage_x, ey};
    sim.advance();
    check(inventory.output < 0.001f && storage.stored_output > before,
          "output is deposited after the hauler reaches Exit storage");
}

void place_exit_storage_output(Simulation& sim, float amount) {
    auto exits = sim.grid().find_all(TileType::Exit);
    check(!exits.empty(), "supply fixture has an Exit");
    if (exits.empty()) return;
    auto [ex, ey] = exits.front();
    int sx = ex + 1 < sim.grid().width() ? ex + 1 : ex - 1;
    sim.grid_mut().set(sx, ey, TileType::Storage);
    auto& storage = sim.grid_mut().data_at(sx, ey);
    storage.storage_capacity = 100.0f;
    storage.stored_output = amount;
}

void test_external_supply_causality() {
    Config cfg = test_config();
    cfg.initial_population = 0;
    cfg.max_population = 4;
    cfg.quota_growth_rate = 0.0f;
    cfg.external_supply_variant = 1;
    cfg.external_supply_response_ticks = 1.0f;
    cfg.external_supply_floor = 0.2f;
    cfg.external_supply_low = 0.0f;
    cfg.external_supply_high = 1.0f;
    cfg.conveyor_decay_rate = 0.0f;

    Simulation open(cfg);
    place_exit_storage_output(open, 10.0f);
    open.advance();
    check(open.metrics().output_shipped > 0.0,
          "output in Exit storage is institutionally shipped");
    check(std::abs(open.external_support() - 1.0f) < 0.0001f,
          "meeting demand preserves full external support");

    Simulation diagonal(cfg);
    auto exits = diagonal.grid().find_all(TileType::Exit);
    int diagonal_x = -1, diagonal_y = -1;
    for (auto [ex, ey] : exits) {
        for (int dy : {-3, 3})
            for (int dx : {-3, 3}) {
                int x = ex + dx, y = ey + dy;
                if (x < 0 || x >= diagonal.grid().width()
                    || y < 0 || y >= diagonal.grid().height()) continue;
                bool outside_shipping_radius = true;
                for (auto [other_x, other_y] : exits) {
                    if (std::abs(x - other_x) + std::abs(y - other_y) <= 3) {
                        outside_shipping_radius = false;
                        break;
                    }
                }
                if (outside_shipping_radius) {
                    diagonal_x = x;
                    diagonal_y = y;
                }
            }
        if (diagonal_x >= 0) break;
    }
    check(diagonal_x >= 0, "supply fixture finds a diagonal non-adjacent tile");
    if (diagonal_x >= 0) {
        diagonal.grid_mut().set(diagonal_x, diagonal_y, TileType::Storage);
        auto& storage = diagonal.grid_mut().data_at(diagonal_x, diagonal_y);
        storage.storage_capacity = 100.0f;
        storage.stored_output = 10.0f;
        diagonal.advance();
        check(diagonal.metrics().output_shipped == 0.0,
              "shipping uses the same Manhattan radius as output logistics");
    }

    Simulation blocked(cfg);
    place_exit_storage_output(blocked, 10.0f);
    blocked.set_output_shipping_enabled(false);
    blocked.advance();

    float expected_support = std::exp(-1.0f);
    float x = expected_support;
    float expected_factor = 0.2f + 0.8f * x * x * (3.0f - 2.0f * x);
    check(blocked.metrics().output_shipped == 0.0,
          "blocked Exit ships no output despite adjacent stock");
    check(std::abs(blocked.total_storage_output() - 10.0f) < 0.0001f,
          "blocked output remains physically stored");
    check(std::abs(blocked.external_support() - expected_support) < 0.0001f,
          "zero shipped fill updates support with the configured EMA");
    check(std::abs(blocked.external_supply_factor() - expected_factor) < 0.0001f,
          "support maps to the configured smooth supply curve");

    const auto& first_metrics = blocked.metrics();
    double first_base = first_metrics.regeneration_base[metric_index(ResourceType::RAW_FOOD)]
        + first_metrics.regeneration_base[metric_index(ResourceType::RAW_MATERIAL)];
    double first_requested = first_metrics.regeneration_requested[metric_index(ResourceType::RAW_FOOD)]
        + first_metrics.regeneration_requested[metric_index(ResourceType::RAW_MATERIAL)];
    check(std::abs(first_requested - first_base) < 0.0001,
          "current-tick shipping cannot alter current-tick regeneration");

    float factor_used_next_tick = blocked.external_supply_factor();
    blocked.advance();
    const auto& second_metrics = blocked.metrics();
    double second_base = second_metrics.regeneration_base[metric_index(ResourceType::RAW_FOOD)]
        + second_metrics.regeneration_base[metric_index(ResourceType::RAW_MATERIAL)];
    double second_requested = second_metrics.regeneration_requested[metric_index(ResourceType::RAW_FOOD)]
        + second_metrics.regeneration_requested[metric_index(ResourceType::RAW_MATERIAL)];
    double base_delta = second_base - first_base;
    double requested_delta = second_requested - first_requested;
    check(base_delta > 0.0
          && std::abs(requested_delta / base_delta - factor_used_next_tick) < 0.0001,
          "tick t support scales regeneration first at tick t+1");
    check(blocked.external_supply_factor() >= cfg.external_supply_floor,
          "external supply never falls below its recovery floor");
    check(std::abs(blocked.factory_health() - open.factory_health()) < 0.0001f,
          "quota failure does not alter mechanical condition in supply variant 1");

    cfg.external_supply_variant = 0;
    Simulation legacy(cfg);
    legacy.set_output_shipping_enabled(false);
    for (int i = 0; i < 3; i++) legacy.advance();
    check(legacy.external_supply_factor() == 1.0f,
          "legacy variant preserves full resource regeneration");

    cfg.director_mode = DirectorMode::CALM;
    cfg.external_supply_variant = 1;
    Simulation calm(cfg);
    calm.set_output_shipping_enabled(false);
    for (int i = 0; i < 5; i++) calm.advance();
    check(calm.metrics().quota_demand == 0.0,
          "CALM accumulates no institutional demand");
    check(calm.external_support() == 1.0f && calm.external_supply_factor() == 1.0f,
          "CALM keeps external support neutral even when shipping is blocked");

    cfg.director_mode = DirectorMode::NORMAL;
    Simulation baseline_condition(cfg);
    baseline_condition.advance();
    Simulation condition(cfg);
    condition.grid_mut().set(2, 2, TileType::Conveyor);
    auto& conveyor = condition.grid_mut().data_at(2, 2);
    conveyor.built = false;
    conveyor.conveyor_condition = 0.4f;
    condition.advance();
    float health_without_frame = condition.factory_health();
    check(std::abs(health_without_frame - baseline_condition.factory_health()) < 0.0001f,
          "unfinished frames are excluded from mechanical condition");
    conveyor.built = true;
    condition.advance();
    check(condition.factory_health() < health_without_frame,
          "factory health reports completed infrastructure condition");
}

void freeze_agents_for_policy_test(Simulation& sim) {
    auto agents = sim.registry().view<ActionComponent, NeedsComponent,
                                      PositionComponent, const AgentComponent>();
    int offset = 0;
    for (auto entity : agents) {
        auto& action = sim.registry().get<ActionComponent>(entity);
        action.current = ActionType::REST;
        action.sticky_action = ActionType::REST;
        action.sticky_ticks = 20;
        sim.registry().get<NeedsComponent>(entity) = {};
        auto& position = sim.registry().get<PositionComponent>(entity);
        position = {8 + offset, 8};
        sim.grid_mut().set(position.x, position.y, TileType::Floor);
        offset++;
    }
}

void prepare_policy_fixture(Simulation& sim, bool cohesive_social_state) {
    freeze_agents_for_policy_test(sim);
    for (int y = 0; y < sim.grid().height(); y++)
        for (int x = 0; x < sim.grid().width(); x++) {
            auto& data = sim.grid_mut().data_at(x, y);
            if (sim.grid().at(x, y) == TileType::Conveyor) data.built = false;
            if (sim.grid().at(x, y) == TileType::Storage) {
                data.stored_food = 0.0f;
                data.stored_raw_food = 0.0f;
                data.stored_raw_material = 0.0f;
                data.stored_construction_material = 0.0f;
                data.stored_output = 0.0f;
            }
        }
    sim.grid_mut().set(2, 2, TileType::Conveyor);
    auto& conveyor = sim.grid_mut().data_at(2, 2);
    conveyor.built = true;
    conveyor.conveyor_condition = 0.8f;
    conveyor.conveyor_contents = 0.25f;
    conveyor.conveyor_dir = ConveyorDir::E;
    sim.grid_mut().set(3, 2, TileType::Floor);

    auto agents = sim.registry().view<AgentComponent, PersonalityComponent,
                                      OpinionComponent, SocialComponent>();
    for (auto entity : agents) {
        auto& agent = sim.registry().get<AgentComponent>(entity);
        agent.community_id = cohesive_social_state ? 7 : -1;
        agent.noncompliance = cohesive_social_state ? 1.0f : 0.0f;
        sim.registry().get<PersonalityComponent>(entity).compliance =
            cohesive_social_state ? 1.0f : 0.0f;
        sim.registry().get<SocialComponent>(entity).influence =
            cohesive_social_state ? 1.0f : 0.0f;
        auto& opinion = sim.registry().get<OpinionComponent>(entity);
        for (float& value : opinion.values) {
            value = cohesive_social_state ? 1.0f : 0.0f;
        }
    }

    for (int from = 0; from < sim.config().initial_population; from++)
        for (int to = 0; to < sim.config().initial_population; to++) {
            if (from == to) continue;
            auto& relationship = sim.social().get_rel(from, to);
            relationship.trust = cohesive_social_state ? 1.0f : -1.0f;
            relationship.familiarity = 1.0f;
        }
}

void test_indifferent_policy_ignores_social_state() {
    Config cfg = test_config();
    cfg.initial_population = 3;
    cfg.max_population = 4;
    cfg.external_policy_variant = 1;
    cfg.restructure_interval = 1;
    cfg.restructure_probability = 1.0f;
    cfg.conveyor_decay_rate = 0.0f;
    bool same_outcome = true;
    bool bounded_and_repairable = true;
    bool factual_only = true;
    for (int seed = 0; seed < 20; seed++) {
        cfg.seed = seed;
        Simulation cohesive(cfg);
        Simulation antagonistic(cfg);
        prepare_policy_fixture(cohesive, true);
        prepare_policy_fixture(antagonistic, false);
        cohesive.advance();
        antagonistic.advance();

        const auto& cohesive_conveyor = cohesive.grid().data_at(2, 2);
        const auto& antagonistic_conveyor = antagonistic.grid().data_at(2, 2);
        same_outcome &= std::abs(cohesive_conveyor.conveyor_condition
                                - antagonistic_conveyor.conveyor_condition) < 0.0001f;
        same_outcome &= cohesive_conveyor.conveyor_dir
                        == antagonistic_conveyor.conveyor_dir;
        bounded_and_repairable &= std::abs(
            cohesive_conveyor.conveyor_condition - 0.65f) < 0.0001f;
        factual_only &= cohesive.total_restructures() == 1
                     && antagonistic.total_restructures() == 1;
        factual_only &= cohesive.chronicle().count_of_type(
                            EventType::FACTORY_RESTRUCTURE) == 1
                     && antagonistic.chronicle().count_of_type(
                            EventType::FACTORY_RESTRUCTURE) == 1;
        factual_only &= cohesive.foreman_reports() == 0
                     && antagonistic.foreman_reports() == 0;
    }
    check(same_outcome,
          "20 seeds keep canonical physical policy invariant to social state");
    check(bounded_and_repairable,
          "canonical conveyor adjustment is bounded and repairable");
    check(factual_only,
          "canonical policy emits only factual events and no social targeting");
}

void test_indifferent_storage_policy_is_resource_neutral() {
    Config cfg = test_config();
    cfg.initial_population = 0;
    cfg.max_population = 4;
    cfg.external_policy_variant = 1;
    cfg.restructure_interval = 1;
    cfg.restructure_probability = 1.0f;

    Simulation sim(cfg);
    for (int y = 0; y < sim.grid().height(); y++)
        for (int x = 0; x < sim.grid().width(); x++) {
            auto& data = sim.grid_mut().data_at(x, y);
            if (sim.grid().at(x, y) == TileType::Conveyor) data.built = false;
            if (sim.grid().at(x, y) == TileType::Storage) {
                data.stored_food = 0.0f;
                data.stored_raw_food = 0.0f;
                data.stored_raw_material = 0.0f;
                data.stored_construction_material = 0.0f;
                data.stored_output = 0.0f;
            }
        }
    sim.grid_mut().set(20, 15, TileType::Storage);
    auto& storage = sim.grid_mut().data_at(20, 15);
    storage.built = true;
    storage.storage_capacity = 100.0f;
    storage.stored_raw_food = 10.0f;
    storage.stored_raw_material = 10.0f;
    storage.stored_food = 10.0f;
    storage.stored_construction_material = 10.0f;
    storage.stored_output = 10.0f;
    sim.advance();

    check(std::abs(storage.stored_raw_food - 9.0f) < 0.0001f
          && std::abs(storage.stored_raw_material - 9.0f) < 0.0001f
          && std::abs(storage.stored_food - 9.0f) < 0.0001f
          && std::abs(storage.stored_construction_material - 9.0f) < 0.0001f
          && std::abs(storage.stored_output - 9.0f) < 0.0001f,
          "canonical storage purge applies one resource-neutral fraction");
    for (double lost : sim.metrics().resources_lost) {
        check(std::abs(lost - 1.0) < 0.0001,
              "canonical storage loss is accounted by resource");
    }
}

void test_partial_conveyor_deposit_preserves_remainder() {
    Config cfg = test_config();
    cfg.initial_population = 0;
    cfg.max_population = 4;
    cfg.director_mode = DirectorMode::CALM;
    cfg.conveyor_decay_rate = 0.0f;
    cfg.conveyor_throughput = 0.5f;

    Simulation sim(cfg);
    auto exits = sim.grid().find_all(TileType::Exit);
    check(!exits.empty(), "conveyor fixture has an Exit");
    if (exits.empty()) return;
    auto [ex, ey] = exits.front();
    struct Neighbor { int x; int y; ConveyorDir toward_exit; };
    std::vector<Neighbor> neighbors;
    if (ex > 0) neighbors.push_back({ex - 1, ey, ConveyorDir::E});
    if (ex + 1 < sim.grid().width()) neighbors.push_back({ex + 1, ey, ConveyorDir::W});
    if (ey > 0) neighbors.push_back({ex, ey - 1, ConveyorDir::S});
    if (ey + 1 < sim.grid().height()) neighbors.push_back({ex, ey + 1, ConveyorDir::N});
    check(neighbors.size() >= 2, "Exit has room for conveyor and Storage fixtures");
    if (neighbors.size() < 2) return;

    for (const auto& neighbor : neighbors) {
        sim.grid_mut().set(neighbor.x, neighbor.y, TileType::Floor);
        sim.grid_mut().data_at(neighbor.x, neighbor.y) = {};
    }
    const auto& conveyor_position = neighbors[0];
    const auto& storage_position = neighbors[1];
    sim.grid_mut().set(conveyor_position.x, conveyor_position.y, TileType::Conveyor);
    auto& conveyor = sim.grid_mut().data_at(conveyor_position.x, conveyor_position.y);
    conveyor.built = true;
    conveyor.conveyor_condition = 1.0f;
    conveyor.conveyor_dir = conveyor_position.toward_exit;
    conveyor.conveyor_contents_type = ResourceType::OUTPUT;
    conveyor.conveyor_contents = 0.5f;
    sim.grid_mut().set(storage_position.x, storage_position.y, TileType::Storage);
    auto& storage = sim.grid_mut().data_at(storage_position.x, storage_position.y);
    storage.built = true;
    storage.storage_capacity = 0.1f;

    sim.advance();
    check(std::abs(storage.stored_output - 0.1f) < 0.0001f,
          "Exit-adjacent Storage accepts only available capacity");
    check(std::abs(conveyor.conveyor_contents - 0.4f) < 0.0001f,
          "partial Exit deposit leaves undelivered output on its conveyor");
}

void prepare_eating_fixture(Simulation& sim, float witness_work_ethic) {
    auto eater = find_agent(sim, 0);
    auto witness = find_agent(sim, 1);
    check(eater != entt::null && witness != entt::null,
          "eating fixture contains eater and witness");
    if (eater == entt::null || witness == entt::null) return;

    sim.grid_mut().set(10, 10, TileType::Floor);
    sim.grid_mut().set(11, 10, TileType::Machine);
    auto& machine = sim.grid_mut().data_at(11, 10);
    machine.built = true;
    sim.registry().get<PositionComponent>(eater) = {10, 10};
    sim.registry().get<PositionComponent>(witness) = {10, 10};

    auto& eater_action = sim.registry().get<ActionComponent>(eater);
    eater_action.current = ActionType::EAT;
    eater_action.sticky_action = ActionType::EAT;
    eater_action.sticky_ticks = 10;
    sim.registry().get<NeedsComponent>(eater) = {};
    sim.registry().get<NeedsComponent>(eater).hunger = 1.0f;
    sim.registry().get<InventoryComponent>(eater).food = 1.0f;
    sim.registry().get<AgentComponent>(eater).noncompliance = 1.0f;
    sim.registry().get<StressComponent>(eater).value = 0.0f;

    auto& witness_action = sim.registry().get<ActionComponent>(witness);
    witness_action.current = ActionType::REST;
    witness_action.sticky_action = ActionType::REST;
    witness_action.sticky_ticks = 10;
    sim.registry().get<NeedsComponent>(witness) = {};
    sim.registry().get<OpinionComponent>(witness).values[0] = witness_work_ethic;
    sim.registry().get<StressComponent>(witness).value = 0.0f;
    auto& relationship = sim.social().get_rel(1, 0);
    relationship.trust = 1.0f;
    relationship.familiarity = 1.0f;
}

void test_eating_has_no_institutional_sanction() {
    Config cfg = test_config();
    cfg.initial_population = 2;
    cfg.max_population = 4;
    cfg.director_mode = DirectorMode::CALM;
    cfg.external_policy_variant = 1;

    Simulation disapproving(cfg);
    Simulation permissive(cfg);
    prepare_eating_fixture(disapproving, 1.0f);
    prepare_eating_fixture(permissive, 0.0f);
    disapproving.advance();
    permissive.advance();

    auto disapproving_eater = find_agent(disapproving, 0);
    auto permissive_eater = find_agent(permissive, 0);
    check(disapproving.factory_health() == 1.0f
          && permissive.factory_health() == 1.0f,
          "eating near machinery does not change canonical factory health");
    check(disapproving.external_support() == 1.0f
          && permissive.external_support() == 1.0f,
          "local eating opinions cannot change external support");
    check(disapproving.registry().get<StressComponent>(disapproving_eater).value == 0.0f
          && permissive.registry().get<StressComponent>(permissive_eater).value == 0.0f,
          "canonical eating and noncompliance inject no institutional stress");
    check(disapproving.social().get_rel(1, 0).trust
          < permissive.social().get_rel(1, 0).trust,
          "work-ethic disapproval remains a local opinion-driven interaction");
    check(disapproving.foreman_reports() == 0 && permissive.foreman_reports() == 0,
          "local disapproval creates no factory report");
}

void test_legacy_policy_remains_available() {
    Config cfg = test_config();
    cfg.initial_population = 2;
    cfg.max_population = 4;
    cfg.director_mode = DirectorMode::CALM;
    cfg.external_policy_variant = 0;
    cfg.stress_decay = 0.0f;
    cfg.health_recovery_per_hit = 0.0f;

    Simulation legacy(cfg);
    prepare_eating_fixture(legacy, 1.0f);
    legacy.advance();
    auto eater = find_agent(legacy, 0);
    check(legacy.factory_health() < 1.0f,
          "policy variant 0 retains witnessed eating health sanction");
    check(legacy.registry().get<StressComponent>(eater).value > 0.0f,
          "policy variant 0 retains semantic eating/noncompliance stress");
}

void prepare_hidden_space_case(Simulation& sim) {
    auto anchor = find_agent(sim, 0);
    check(anchor != entt::null, "hidden-space fixture has an anchor agent");
    if (anchor == entt::null) return;
    auto anchor_pos = sim.registry().get<PositionComponent>(anchor);
    sim.grid_mut().set(anchor_pos.x, anchor_pos.y, TileType::HiddenSpace);
    sim.grid_mut().data_at(anchor_pos.x, anchor_pos.y).hidden_space_occupancy = 0;
    sim.grid_mut().data_at(anchor_pos.x, anchor_pos.y).occupancy_capacity = 2;
    sim.grid_mut().data_at(anchor_pos.x, anchor_pos.y).overcapacity_ticks = 0;

    auto agents = sim.registry().view<ActionComponent, PositionComponent, const AgentComponent>();
    for (auto e : agents) {
        auto& pos = sim.registry().get<PositionComponent>(e);
        pos = anchor_pos;
        auto& action = sim.registry().get<ActionComponent>(e);
        action.current = ActionType::REST;
        action.sticky_action = ActionType::REST;
        action.sticky_ticks = 10;
        size_t rest_index = metric_index(ActionType::REST);
        action.preferred_x[rest_index] = anchor_pos.x;
        action.preferred_y[rest_index] = anchor_pos.y;
    }
}

void test_calm_pressure_boundary() {
    Config cfg = test_config();
    cfg.initial_population = 3;
    cfg.max_population = 4;
    cfg.rest_recovery = 0.0f;
    cfg.director_mode = DirectorMode::CALM;

    Simulation calm(cfg);
    prepare_hidden_space_case(calm);
    auto calm_anchor = find_agent(calm, 0);
    auto calm_target = find_agent(calm, 1);
    auto calm_pos = calm.registry().get<PositionComponent>(calm_anchor);
    calm.registry().get<SocialComponent>(calm_anchor).influence = 1.0f;
    calm.registry().get<PersonalityComponent>(calm_anchor).compliance = 1.0f;
    calm.registry().get<AgentComponent>(calm_target).noncompliance = 1.0f;
    calm.advance();
    check(calm.grid().at(calm_pos.x, calm_pos.y) == TileType::HiddenSpace,
          "CALM leaves hidden spaces untouched");
    check(calm.grid().data_at(calm_pos.x, calm_pos.y).hidden_space_occupancy == 0,
           "CALM does not update hidden-space exposure");
    check(calm.grid().data_at(calm_pos.x, calm_pos.y).overcapacity_ticks == 0,
          "CALM does not update anonymous overcapacity");
    check(calm.hidden_spaces_sealed() == 0, "CALM seals no hidden spaces");
    check(calm.foreman_reports() == 0, "CALM disables Watcher reports");
    check(calm.total_restructures() == 0,
          "CALM executes no institutional restructuring");
    check(calm.chronicle().count_of_type(EventType::QUOTA_MILESTONE) == 0,
          "CALM emits no quota-pressure milestone");

    cfg.director_mode = DirectorMode::NORMAL;
    Simulation normal(cfg);
    prepare_hidden_space_case(normal);
    auto normal_anchor = find_agent(normal, 0);
    auto normal_pos = normal.registry().get<PositionComponent>(normal_anchor);
    normal.advance();
    check(normal.grid().at(normal_pos.x, normal_pos.y) == TileType::HiddenSpace,
          "one NORMAL overcapacity tick does not close a fresh space");
    check(normal.grid().data_at(normal_pos.x, normal_pos.y).overcapacity_ticks == 1,
          "NORMAL updates anonymous overcapacity exactly once per tick");
    for (int i = 1; i < 10; i++) normal.advance();
    check(normal.grid().at(normal_pos.x, normal_pos.y) == TileType::Floor,
          "sustained physical overcapacity closes the space");
    check(normal.space_closures() == 1 && normal.hidden_spaces_sealed() == 0,
          "canonical anonymous closure uses its dedicated counter");
    check(normal.chronicle().count_of_type(EventType::FACTORY_SEALED_SPACE) == 0,
          "canonical closure emits no semantic hidden-space event");
}

void test_single_death_and_grief() {
    Config cfg = test_config();
    cfg.initial_population = 2;
    cfg.max_population = 4;
    cfg.director_mode = DirectorMode::CALM;
    cfg.starvation_ticks = 1;
    cfg.exhaustion_ticks = 1;
    cfg.breakdown_threshold = 2.0f;
    cfg.hunger_decay = 0.0f;
    cfg.rest_decay = 0.0f;
    cfg.social_decay = 0.0f;
    cfg.expression_decay = 0.0f;
    cfg.purpose_decay = 0.0f;
    cfg.eat_satisfaction = 0.0f;
    cfg.rest_recovery = 0.0f;
    cfg.stress_decay = 0.0f;

    Simulation sim(cfg);
    auto doomed = find_agent(sim, 0);
    auto survivor = find_agent(sim, 1);
    check(doomed != entt::null && survivor != entt::null,
          "death fixture contains both agents");
    if (doomed == entt::null || survivor == entt::null) return;

    auto& needs = sim.registry().get<NeedsComponent>(doomed);
    needs.hunger = 1.0f;
    needs.rest = 1.0f;
    auto& inventory = sim.registry().get<InventoryComponent>(doomed);
    inventory = {};
    auto& action = sim.registry().get<ActionComponent>(doomed);
    action.current = ActionType::REST;
    action.sticky_action = ActionType::REST;
    action.sticky_ticks = 10;

    auto& survivor_personality = sim.registry().get<PersonalityComponent>(survivor);
    survivor_personality.resilience = 0.0f;
    sim.registry().get<StressComponent>(survivor).value = 0.0f;
    auto& relation = sim.social().get_rel(1, 0);
    relation.familiarity = 1.0f;
    relation.trust = 1.0f;

    sim.advance();

    const auto& dead_agent = sim.registry().get<AgentComponent>(doomed);
    check(!dead_agent.alive, "simultaneously eligible agent dies");
    check(dead_agent.cause_of_death == "starvation",
          "death tie uses documented starvation precedence");
    check(factual_death_events(sim) == 1, "one agent produces one factual death event");
    check(sim.chronicle().count_of_type(EventType::DIED_STARVATION) == 1,
          "starvation death is explicitly typed");
    check(sim.chronicle().count_of_type(EventType::FIRST_DEATH) == 1,
          "first factual death creates one narrative marker");
    check(total_deaths(sim.metrics()) == 1, "death metrics count the agent once");
    check(std::abs(sim.registry().get<StressComponent>(survivor).value - 0.05f) < 0.0001f,
          "relationship grief is applied exactly once");
}

void test_suicide_uses_death_pipeline() {
    Config cfg = test_config();
    cfg.initial_population = 2;
    cfg.max_population = 4;
    cfg.director_mode = DirectorMode::CALM;
    cfg.selection_temperature = 0.0f;
    cfg.breakdown_threshold = 2.0f;
    cfg.sabotage_stress_threshold = 0.1f;
    cfg.stress_decay = 0.0f;
    cfg.post_sabotage_pause_chance = 0.0f;
    cfg.suicide_chance = 1.0f;
    cfg.hunger_decay = 0.0f;
    cfg.rest_decay = 0.0f;
    cfg.social_decay = 0.0f;
    cfg.expression_decay = 0.0f;
    cfg.purpose_decay = 0.0f;

    Simulation sim(cfg);
    auto doomed = find_agent(sim, 0);
    auto survivor = find_agent(sim, 1);
    check(doomed != entt::null && survivor != entt::null,
          "suicide fixture contains both agents");
    if (doomed == entt::null || survivor == entt::null) return;

    auto& doomed_pos = sim.registry().get<PositionComponent>(doomed);
    sim.grid_mut().set(doomed_pos.x, doomed_pos.y, TileType::Floor);
    sim.grid_mut().set(doomed_pos.x + 1, doomed_pos.y, TileType::Machine);
    auto& machine = sim.grid_mut().data_at(doomed_pos.x + 1, doomed_pos.y);
    machine.built = true;
    machine.build_cost = 1.0f;
    machine.build_progress = 1.0f;

    auto& doomed_stress = sim.registry().get<StressComponent>(doomed);
    doomed_stress.value = 1.0f;
    doomed_stress.trauma = 1.0f;
    doomed_stress.state = StressState::BROKEN;
    sim.registry().get<PersonalityComponent>(doomed).compliance = 0.0f;
    sim.registry().get<NeedsComponent>(doomed) = {};

    auto& survivor_pos = sim.registry().get<PositionComponent>(survivor);
    int survivor_x = doomed_pos.x < cfg.grid_width / 2 ? cfg.grid_width - 3 : 2;
    int survivor_y = doomed_pos.y < cfg.grid_height / 2 ? cfg.grid_height - 3 : 2;
    survivor_pos = {survivor_x, survivor_y};
    sim.grid_mut().set(survivor_x, survivor_y, TileType::Floor);
    sim.registry().get<PersonalityComponent>(survivor).resilience = 0.0f;
    sim.registry().get<StressComponent>(survivor).value = 0.0f;
    auto& relation = sim.social().get_rel(1, 0);
    relation.familiarity = 1.0f;
    relation.trust = 1.0f;

    sim.advance();

    const auto& dead_agent = sim.registry().get<AgentComponent>(doomed);
    check(!dead_agent.alive && dead_agent.cause_of_death == "suicide",
          "deterministic sabotage suicide uses the death pipeline");
    check(sim.suicides_total() == 1, "suicide aggregate increments once");
    check(sim.chronicle().count_of_type(EventType::DIED_SUICIDE) == 1,
          "suicide is explicitly typed as a lifecycle death");
    check(factual_death_events(sim) == 1, "suicide produces one factual death event");
    check(sim.chronicle().count_of_type(EventType::FIRST_DEATH) == 1,
          "suicide can trigger the first-death marker");
    check(total_deaths(sim.metrics()) == 1, "suicide metrics count the agent once");
    check(std::abs(sim.registry().get<StressComponent>(survivor).value - 0.05f) < 0.0001f,
          "suicide applies generic relationship grief exactly once");
}

void test_natural_mortality_uses_exclusive_death_pipeline() {
    Config cfg = test_config();
    cfg.initial_population = 1;
    cfg.max_population = 2;
    cfg.natural_mortality_enabled = true;
    cfg.life_expectancy_ticks = 3;
    cfg.lifespan_spread = 0.0f;
    cfg.maturity_age_ticks = 0;
    cfg.founder_age_min_ticks = 0;
    cfg.founder_age_max_ticks = 0;
    cfg.hunger_decay = cfg.rest_decay = 0.0f;
    Simulation natural(cfg);
    for (int tick = 0; tick < 4; tick++) natural.advance();
    auto agent = find_agent(natural, 0);
    const auto& dead = natural.registry().get<AgentComponent>(agent);
    check(!dead.alive && dead.death_cause == DeathCause::NATURAL,
          "age reaches a distinct natural death cause");
    check(natural.chronicle().count_of_type(EventType::DIED_NATURAL) == 1
          && total_deaths(natural.metrics()) == 1,
          "natural mortality uses the one-event one-metric death pipeline");
    for (int tick = 0; tick < 5; tick++) natural.advance();
    check(natural.alive_count() == 0 && natural.ever_created() == 1,
          "extinction persists without target-population replacement");

    cfg.life_expectancy_ticks = 1;
    cfg.founder_age_min_ticks = 1;
    cfg.founder_age_max_ticks = 1;
    cfg.starvation_ticks = 1;
    Simulation precedence(cfg);
    auto doomed = find_agent(precedence, 0);
    precedence.registry().get<NeedsComponent>(doomed).hunger = 1.0f;
    precedence.registry().get<InventoryComponent>(doomed) = {};
    auto& action = precedence.registry().get<ActionComponent>(doomed);
    action.current = ActionType::REST;
    action.sticky_action = ActionType::REST;
    action.sticky_ticks = 10;
    precedence.advance();
    check(precedence.registry().get<AgentComponent>(doomed).death_cause
          == DeathCause::STARVATION,
          "material death keeps precedence over simultaneous natural mortality");
}

void test_arrivals_are_exogenous_and_newcomers_start_empty() {
    Config cfg = test_config();
    cfg.initial_population = 1;
    cfg.max_population = 16;
    cfg.arrivals_enabled = true;
    cfg.arrival_rate_per_1000_ticks = 100000.0f;
    cfg.arrival_age_min_ticks = 0;
    cfg.arrival_age_max_ticks = 0;
    cfg.hunger_decay = cfg.rest_decay = 0.0f;
    cfg.starvation_ticks = 1;
    Simulation survivor(cfg);
    Simulation death(cfg);
    auto doomed = find_agent(death, 0);
    death.registry().get<NeedsComponent>(doomed).hunger = 1.0f;
    death.registry().get<InventoryComponent>(doomed) = {};
    auto& doomed_action = death.registry().get<ActionComponent>(doomed);
    doomed_action.current = ActionType::REST;
    doomed_action.sticky_action = ActionType::REST;
    doomed_action.sticky_ticks = 10;

    survivor.advance();
    death.advance();
    auto newcomer = find_agent(survivor, 1);
    check(newcomer != entt::null, "first accepted arrival has a stable new ID");
    if (newcomer != entt::null) {
        const auto& lifecycle = survivor.registry().get<LifecycleComponent>(newcomer);
        const auto& skills = survivor.registry().get<SkillsComponent>(newcomer);
        const auto& memory = survivor.registry().get<PlaceMemoryComponent>(newcomer);
        check(lifecycle.origin == AgentOrigin::ARRIVAL
              && lifecycle.parent_a < 0 && lifecycle.parent_b < 0,
              "arrival records factual origin without invented parents");
        check(skills.xp_factory == 0.0f && skills.xp_domestic == 0.0f
              && skills.xp_art == 0.0f && skills.xp_social == 0.0f
              && memory.places.empty(),
              "new arrival inherits neither skills nor place memory");
    }
    for (int tick = 1; tick < 5; tick++) {
        survivor.advance();
        death.advance();
    }
    check(survivor.arrival_attempts() == death.arrival_attempts()
          && survivor.arrivals_admitted() == death.arrivals_admitted(),
          "arrival schedule is identical despite an unrelated death");
    check(survivor.alive_count() == death.alive_count() + 1,
          "arrivals do not replace the dead resident toward a target");

    Config capacity_cfg = test_config();
    capacity_cfg.initial_population = 1;
    capacity_cfg.max_population = 1;
    capacity_cfg.arrivals_enabled = true;
    capacity_cfg.arrival_rate_per_1000_ticks = 100000.0f;
    capacity_cfg.natural_mortality_enabled = true;
    capacity_cfg.life_expectancy_ticks = 2;
    capacity_cfg.lifespan_spread = 0.0f;
    capacity_cfg.maturity_age_ticks = 0;
    capacity_cfg.founder_age_min_ticks = 0;
    capacity_cfg.founder_age_max_ticks = 0;
    capacity_cfg.arrival_age_min_ticks = 0;
    capacity_cfg.arrival_age_max_ticks = 0;
    capacity_cfg.hunger_decay = capacity_cfg.rest_decay = 0.0f;
    Simulation capacity(capacity_cfg);
    for (int tick = 0; tick < 3; tick++) capacity.advance();
    check(capacity.arrival_attempts() == 3
          && capacity.arrivals_blocked_capacity() == 2
          && capacity.arrivals_admitted() == 1
          && capacity.ever_created() == 2,
          "capacity-blocked arrivals are discarded rather than queued as replacements");
}

void test_reproduction_inherits_traits_not_roles_or_relationships() {
    Config cfg = test_config();
    cfg.initial_population = 2;
    cfg.max_population = 4;
    cfg.reproduction_enabled = true;
    cfg.reproduction_check_interval_ticks = 1;
    cfg.reproduction_rate_per_1000_ticks = 100000.0f;
    cfg.reproduction_cooldown_ticks = 1000;
    cfg.maturity_age_ticks = 0;
    cfg.founder_age_min_ticks = 1000;
    cfg.founder_age_max_ticks = 1000;
    cfg.hunger_decay = cfg.rest_decay = 0.0f;
    Simulation sim(cfg);
    Simulation no_relationship(cfg);
    no_relationship.advance();
    check(no_relationship.births_total() == 0,
          "material abundance alone cannot bypass absent social evidence");
    auto first = find_agent(sim, 0);
    auto second = find_agent(sim, 1);
    sim.registry().get<PositionComponent>(first) = {10, 10};
    sim.registry().get<PositionComponent>(second) = {10, 10};
    sim.grid_mut().set(10, 10, TileType::Floor);
    sim.registry().get<InventoryComponent>(first).food = 2.0f;
    sim.registry().get<InventoryComponent>(second).food = 2.0f;
    sim.registry().get<SocialComponent>(first).mood = 1.0f;
    sim.registry().get<SocialComponent>(second).mood = 1.0f;
    for (auto [from, to] : {std::pair{0, 1}, std::pair{1, 0}}) {
        auto& relationship = sim.social().get_rel(from, to);
        relationship.familiarity = 1.0f;
        relationship.trust = 1.0f;
    }

    sim.advance();
    check(sim.births_total() == 1 && sim.ever_created() == 3,
          "continuous eligible pair produces one capacity-bounded birth");
    auto child = find_agent(sim, 2);
    check(child != entt::null, "birth allocates a monotonic historical ID");
    if (child != entt::null) {
        const auto& lifecycle = sim.registry().get<LifecycleComponent>(child);
        const auto& personality = sim.registry().get<PersonalityComponent>(child);
        const auto& skills = sim.registry().get<SkillsComponent>(child);
        check(lifecycle.origin == AgentOrigin::BIRTH
              && lifecycle.parent_a == 0 && lifecycle.parent_b == 1
              && lifecycle.generation == 1,
              "genealogy records both parents and generation");
        check(personality.archetype == Archetype::COUNT,
              "descendant receives no encoded archetype or profession");
        check(skills.xp_factory == 0.0f && skills.xp_domestic == 0.0f
              && skills.xp_art == 0.0f && skills.xp_social == 0.0f,
              "descendant inherits no practiced skill");
        check(sim.social().get_rel(2, 0).familiarity == 0.0f
              && sim.social().get_rel(0, 2).familiarity == 0.0f
              && sim.registry().get<PlaceMemoryComponent>(child).places.empty(),
              "genealogy does not copy relationships or spatial culture");
        for (float trait : {personality.compliance, personality.laziness,
                            personality.artistry, personality.gregariousness,
                            personality.resilience, personality.curiosity})
            check(trait >= 0.05f && trait <= 0.95f,
                  "inherited personality with mutation remains bounded");
        const auto& parent_a = sim.registry().get<PersonalityComponent>(first);
        const auto& parent_b = sim.registry().get<PersonalityComponent>(second);
        const float child_traits[] = {
            personality.compliance, personality.laziness, personality.artistry,
            personality.gregariousness, personality.resilience, personality.curiosity,
        };
        const float parent_a_traits[] = {
            parent_a.compliance, parent_a.laziness, parent_a.artistry,
            parent_a.gregariousness, parent_a.resilience, parent_a.curiosity,
        };
        const float parent_b_traits[] = {
            parent_b.compliance, parent_b.laziness, parent_b.artistry,
            parent_b.gregariousness, parent_b.resilience, parent_b.curiosity,
        };
        for (int dimension = 0; dimension < 6; dimension++) {
            float midpoint = (parent_a_traits[dimension]
                            + parent_b_traits[dimension]) * 0.5f;
            check(std::abs(child_traits[dimension] - midpoint)
                  <= cfg.personality_mutation_amplitude + 0.0001f,
                  "descendant trait stays within mutation amplitude of parental midpoint");
        }
    }
}

void test_unobserved_agent_does_not_shift_incumbent_rng() {
    Config one_cfg = test_config();
    one_cfg.initial_population = 1;
    one_cfg.max_population = 4;
    one_cfg.director_mode = DirectorMode::CALM;
    Config two_cfg = one_cfg;
    two_cfg.initial_population = 2;
    Simulation alone(one_cfg);
    Simulation remote(two_cfg);
    auto alone_agent = find_agent(alone, 0);
    auto remote_agent = find_agent(remote, 0);
    auto remote_other = find_agent(remote, 1);
    alone.registry().get<PositionComponent>(alone_agent) = {3, 3};
    remote.registry().get<PositionComponent>(remote_agent) = {3, 3};
    remote.registry().get<PositionComponent>(remote_other) = {35, 25};
    alone.grid_mut().set(3, 3, TileType::Floor);
    remote.grid_mut().set(3, 3, TileType::Floor);
    remote.grid_mut().set(35, 25, TileType::Floor);

    alone.advance();
    remote.advance();
    const auto& alone_action = alone.registry().get<ActionComponent>(alone_agent);
    const auto& remote_action = remote.registry().get<ActionComponent>(remote_agent);
    check(alone_action.current == remote_action.current
          && alone_action.target_x == remote_action.target_x
          && alone_action.target_y == remote_action.target_y,
          "unobserved newcomer does not shift an incumbent's private RNG stream");
}

void test_dynamic_identity_and_ten_thousand_tick_turnover() {
    Chronicle chronicle;
    chronicle.log(0, EventType::ARRIVED, 130, "arrived");
    check(chronicle.by_agent(130).size() == 1,
          "Chronicle indexes historical IDs above the old 64-person limit");

    SocialFabric social(2);
    social.ensure_agent_id(130);
    social.record_copresence(130, 1, 0);
    check(social.get_rel(130, 1).familiarity > 0.0f,
          "SocialFabric expands safely for monotonic historical IDs");

    Config cfg = test_config();
    cfg.initial_population = 2;
    cfg.max_population = 8;
    cfg.natural_mortality_enabled = true;
    cfg.life_expectancy_ticks = 1200;
    cfg.lifespan_spread = 0.1f;
    cfg.maturity_age_ticks = 100;
    cfg.founder_age_min_ticks = 0;
    cfg.founder_age_max_ticks = 0;
    cfg.arrivals_enabled = true;
    cfg.arrival_rate_per_1000_ticks = 2.0f;
    cfg.arrival_age_min_ticks = 0;
    cfg.arrival_age_max_ticks = 0;
    cfg.reproduction_enabled = false;
    cfg.hunger_decay = cfg.rest_decay = 0.0f;
    cfg.social_decay = cfg.expression_decay = cfg.purpose_decay = 0.0f;
    Simulation sim(cfg);
    for (int tick = 0; tick < 10000; tick++) sim.advance();
    Simulation replay(cfg);
    for (int tick = 0; tick < 10000; tick++) replay.advance();

    std::set<int> ids;
    std::set<int> cohorts;
    auto agents = sim.registry().view<const AgentComponent, const LifecycleComponent>();
    for (auto entity : agents) {
        const auto& agent = sim.registry().get<AgentComponent>(entity);
        const auto& lifecycle = sim.registry().get<LifecycleComponent>(entity);
        ids.insert(agent.id);
        cohorts.insert(lifecycle.cohort);
    }
    check(static_cast<int>(ids.size()) == sim.ever_created()
          && *ids.rbegin() == sim.ever_created() - 1,
          "ten-thousand-tick turnover preserves unique monotonic IDs");
    check(sim.ever_created() > cfg.max_population,
          "historical identities can exceed simultaneous population capacity safely");
    check(sim.arrivals_admitted() > 0 && cohorts.size() >= 2,
          "long run contains multiple independently arriving cohorts");
    check(sim.metrics().deaths[metric_index(MetricDeathCause::Natural)] > 0,
          "long run records natural mortality separately");
    check(sim.ever_created() == sim.alive_count()
          + static_cast<int>(total_deaths(sim.metrics())),
          "long-run historical accounting remains closed");
    check(sim.alive_count() <= cfg.max_population,
          "long-run population never exceeds simultaneous capacity");
    check(sim.metrics() == replay.metrics()
          && sim.alive_count() == replay.alive_count()
          && sim.ever_created() == replay.ever_created()
          && sim.arrival_attempts() == replay.arrival_attempts()
          && sim.arrivals_admitted() == replay.arrivals_admitted()
          && sim.births_total() == replay.births_total()
          && sim.chronicle().size() == replay.chronicle().size(),
          "ten-thousand-tick lifecycle replay is deterministic within one build");
}

void test_same_build_replay() {
    Config cfg = test_config();
    Snapshot first = run_snapshot(cfg, 60);
    Snapshot second = run_snapshot(cfg, 60);
    check(first == second, "same seed and build produce an identical snapshot");
}

std::pair<int, int> first_tile(const Grid& grid, TileType type, int skip = 0) {
    for (int y = 0; y < grid.height(); y++)
        for (int x = 0; x < grid.width(); x++)
            if (grid.at(x, y) == type && skip-- <= 0) return {x, y};
    return {-1, -1};
}

void test_director_environmental_commands() {
    Config cfg = test_config();
    cfg.initial_population = 0;
    cfg.max_population = 4;
    cfg.quota_growth_rate = 0.25f;
    Simulation sim(cfg);

    auto floor = first_tile(sim.grid(), TileType::Floor);
    auto source = first_tile(sim.grid(), TileType::FoodSource);
    auto conveyor = first_tile(sim.grid(), TileType::Conveyor);
    check(floor.first >= 0 && source.first >= 0 && conveyor.first >= 0,
          "Director fixture has editable physical sites");

    auto rejected = sim.apply_director_command(DirectorSetZone{-1, -1, 2});
    check(!rejected.applied() && sim.director_log().empty(),
          "invalid Director commands are atomic and are not replay events");

    check(sim.apply_director_command(DirectorSetQuota{0.4f}).applied(),
          "Director can set a nonnegative quota in normal mode");
    check(sim.apply_director_command(DirectorSetZone{floor.first, floor.second, 2}).applied()
          && sim.grid().data_at(floor.first, floor.second).occupancy_capacity == 2,
          "Director zoning sets an anonymous occupancy capacity");

    float source_amount = sim.grid().data_at(source.first, source.second).resource_amount;
    float source_max = sim.grid().data_at(source.first, source.second).resource_max;
    float source_regen = sim.grid().data_at(source.first, source.second).resource_regen;
    DirectorPlaceStructure food_machine;
    food_machine.x = source.first;
    food_machine.y = source.second;
    food_machine.structure = DirectorStructure::Machine;
    food_machine.machine_type = MachineType::Food;
    check(sim.apply_director_command(food_machine).applied()
          && sim.grid().at(source.first, source.second) == TileType::Machine
          && sim.grid().data_at(source.first, source.second).built,
          "Director construction creates completed source-backed infrastructure");
    check(sim.apply_director_command(
              DirectorRemoveStructure{source.first, source.second}).applied()
          && sim.grid().at(source.first, source.second) == TileType::FoodSource
          && sim.grid().data_at(source.first, source.second).resource_amount == source_amount
          && sim.grid().data_at(source.first, source.second).resource_max == source_max
          && sim.grid().data_at(source.first, source.second).resource_regen == source_regen,
          "Director removal restores the physical resource beneath a machine");

    auto& belt = sim.grid_mut().data_at(conveyor.first, conveyor.second);
    belt.conveyor_condition = 0.5f;
    float condition_before = belt.conveyor_condition;
    check(sim.apply_director_command(DirectorSetMaintenancePriority{
              conveyor.first, conveyor.second, MaintenancePriority::High}).applied()
          && belt.maintenance_priority == 1
          && belt.conveyor_condition == condition_before,
          "maintenance priority changes a signal without directly repairing infrastructure");

    size_t event_count = sim.director_log().size();
    for (size_t index = 0; index < event_count; index++) {
        check(sim.director_log()[index].tick == 0
              && sim.director_log()[index].sequence == index,
              "Director events record their tick and strict sequence");
    }
    sim.advance();
    check(std::abs(sim.current_quota() - 0.4f) < 0.0001f
          && std::abs(sim.metrics().quota_demand - 0.4) < 0.0001,
          "a manual quota persists and applies exactly before the next advance");

    Config calm_cfg = cfg;
    calm_cfg.director_mode = DirectorMode::CALM;
    Simulation calm(calm_cfg);
    check(calm.apply_director_command(DirectorSetQuota{0.4f}).error
              == DirectorError::DisabledInCalm,
          "CALM rejects human quota pressure explicitly");
}

void test_director_structural_edits_invalidate_paths() {
    Config cfg = test_config();
    cfg.initial_population = 1;
    Simulation sim(cfg);
    auto agent = find_agent(sim, 0);
    auto& cache = sim.registry().get<ActionComponent>(agent).path_cache;
    cache.path = {{1, 1}, {2, 1}};
    cache.target_x = 2;
    cache.target_y = 1;
    cache.computed_tick = 0;

    auto floor = first_tile(sim.grid(), TileType::Floor);
    DirectorPlaceStructure wall;
    wall.x = floor.first;
    wall.y = floor.second;
    wall.structure = DirectorStructure::Wall;
    check(sim.apply_director_command(wall).applied() && cache.path.empty()
          && cache.target_x == -1 && cache.computed_tick == -1,
          "Director structural edits invalidate cached agent routes");
}

void test_director_log_round_trip_and_replay() {
    Config cfg = test_config();
    cfg.initial_population = 4;
    cfg.max_population = 8;
    cfg.restructure_probability = 0.0f;
    Simulation original(cfg);
    auto floor = first_tile(original.grid(), TileType::Floor);
    auto second_floor = first_tile(original.grid(), TileType::Floor, 1);
    auto conveyor = first_tile(original.grid(), TileType::Conveyor);

    DirectorPlaceStructure storage;
    storage.x = second_floor.first;
    storage.y = second_floor.second;
    storage.structure = DirectorStructure::Storage;
    std::vector<std::pair<int, DirectorCommand>> schedule = {
        {0, DirectorSetQuota{0.15f}},
        {0, DirectorSetZone{floor.first, floor.second, 4}},
        {0, storage},
        {5, DirectorRemoveStructure{second_floor.first, second_floor.second}},
        {5, DirectorSetMaintenancePriority{
            conveyor.first, conveyor.second, MaintenancePriority::High}},
    };

    size_t next = 0;
    constexpr int ticks = 30;
    for (int step = 0; step < ticks; step++) {
        while (next < schedule.size() && schedule[next].first == original.tick()) {
            check(original.apply_director_command(schedule[next].second).applied(),
                  "original Director session accepts scheduled intervention");
            next++;
        }
        original.advance();
    }

    const char* path = "director_replay_test.toml";
    std::string error;
    constexpr uint64_t config_fingerprint = 0x123456789abcdef0ULL;
    check(write_director_log(path, cfg.seed, cfg.director_mode,
                             config_fingerprint,
                             original.director_log(), error),
          "Director session serializes to TOML");
    int parsed_seed = 0;
    DirectorMode parsed_mode = DirectorMode::CALM;
    uint64_t parsed_config_fingerprint = 0;
    std::vector<DirectorEvent> parsed_events;
    check(read_director_log(path, parsed_seed, parsed_mode,
                            parsed_config_fingerprint, parsed_events, error)
          && parsed_seed == cfg.seed && parsed_mode == cfg.director_mode
          && parsed_config_fingerprint == config_fingerprint
          && parsed_events == original.director_log(),
          "Director TOML round-trip preserves every typed parameter");
    std::remove(path);

    const char* lf_config = "director_config_lf.toml";
    const char* crlf_config = "director_config_crlf.toml";
    {
        std::ofstream output(lf_config, std::ios::binary);
        output << "[simulation]\nseed = 42\n";
    }
    {
        std::ofstream output(crlf_config, std::ios::binary);
        output << "[simulation]\r\nseed = 42\r\n";
    }
    uint64_t lf_fingerprint = 0;
    uint64_t crlf_fingerprint = 0;
    check(fingerprint_config_source(lf_config, lf_fingerprint, error)
          && fingerprint_config_source(crlf_config, crlf_fingerprint, error)
          && lf_fingerprint == crlf_fingerprint,
          "configuration fingerprints normalize LF and CRLF checkouts");
    std::remove(lf_config);
    std::remove(crlf_config);

    const char* malformed_path = "director_malformed_event.toml";
    {
        std::ofstream output(malformed_path, std::ios::binary);
        output << "format = \"vida-interventions\"\n"
               << "schema_version = 2\n"
               << "seed = 42\n"
               << "director_mode = \"normal\"\n"
               << "tick_phase = \"before_advance\"\n"
               << "config_fingerprint = \"123456789abcdef0\"\n"
               << "event = \"corrupt\"\n";
    }
    check(!read_director_log(malformed_path, parsed_seed, parsed_mode,
                             parsed_config_fingerprint, parsed_events, error),
          "Director parser rejects a present event ledger with the wrong TOML type");
    std::remove(malformed_path);

    Simulation replay(cfg);
    size_t replay_index = 0;
    for (int step = 0; step < ticks; step++) {
        while (replay_index < parsed_events.size()
               && parsed_events[replay_index].tick == replay.tick()) {
            check(replay.replay_director_event(parsed_events[replay_index]).applied(),
                  "fresh simulation accepts recorded intervention at its exact tick");
            replay_index++;
        }
        replay.advance();
    }
    check(replay_index == parsed_events.size()
          && replay.director_log() == original.director_log(),
          "replay consumes the full intervention ledger in sequence");
    check(original.metrics() == replay.metrics()
          && same_generated_grid(original.grid(), replay.grid())
          && original.alive_count() == replay.alive_count()
          && original.ever_created() == replay.ever_created()
          && original.current_quota() == replay.current_quota()
          && original.chronicle().to_jsonl() == replay.chronicle().to_jsonl(),
          "recorded Director replay reproduces the original session state");
}

void test_narrative_is_behavior_neutral() {
    Config cfg = test_config();
    Snapshot without_rendering = run_snapshot(cfg, 60, false);
    Snapshot with_rendering = run_snapshot(cfg, 60, true);
    check(without_rendering == with_rendering,
          "rendering Chronicle does not consume behavioral RNG");
}

}  // namespace

int main() {
    test_metrics_contract();
    test_inherited_factory_map_properties();
    test_inherited_chain_operates_without_build();
    test_inherited_conveyor_is_maintainable();
    test_artistic_and_social_skills_progress();
    test_social_evidence_is_directional();
    test_graph_labels_are_behavior_neutral();
    test_create_completes_discrete_work_units_on_ordinary_floor();
    test_unseen_stock_does_not_change_decision();
    test_build_can_be_disabled();
    test_conveyor_planning_is_pure();
    test_output_machine_can_be_rebuilt();
    test_isolated_agent_cannot_explore();
    test_conveyors_do_not_mix_resources();
    test_production_assessment_counts_buffers();
    test_broken_source_machine_still_regenerates();
    test_output_haul_requires_storage_arrival();
    test_external_supply_causality();
    test_indifferent_policy_ignores_social_state();
    test_indifferent_storage_policy_is_resource_neutral();
    test_partial_conveyor_deposit_preserves_remainder();
    test_eating_has_no_institutional_sanction();
    test_legacy_policy_remains_available();
    test_calm_pressure_boundary();
    test_single_death_and_grief();
    test_suicide_uses_death_pipeline();
    test_natural_mortality_uses_exclusive_death_pipeline();
    test_arrivals_are_exogenous_and_newcomers_start_empty();
    test_reproduction_inherits_traits_not_roles_or_relationships();
    test_unobserved_agent_does_not_shift_incumbent_rng();
    test_dynamic_identity_and_ten_thousand_tick_turnover();
    test_same_build_replay();
    test_director_environmental_commands();
    test_director_structural_edits_invalidate_paths();
    test_director_log_round_trip_and_replay();
    test_narrative_is_behavior_neutral();
    if (failures > 0) {
        std::fprintf(stderr, "%d test assertion(s) failed\n", failures);
        return 1;
    }
    std::printf("All simulation tests passed\n");
    return 0;
}
