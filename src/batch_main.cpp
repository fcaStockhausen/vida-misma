#include "simulation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <bit>
#include <cstdint>

// ============================================================
// Helpers
// ============================================================

static std::string runtime_config_path() {
    std::string config_path = "config/default.toml";
    FILE* test = std::fopen(config_path.c_str(), "r");
    if (!test) {
        config_path = "../config/default.toml";
        test = std::fopen(config_path.c_str(), "r");
    }
    if (test) std::fclose(test);
    return config_path;
}

static Config make_config(int argc, char* argv[], int arg_base, bool force_calm = false) {
    std::string config_path = runtime_config_path();
    Config cfg = load_config(config_path);
    if (force_calm) cfg.director_mode = DirectorMode::CALM;
    if (argc > arg_base + 0) cfg.seed = std::atoi(argv[arg_base + 0]);
    return cfg;
}

static bool parse_int_argument(const char* text, int& value) {
    char* end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') return false;
    value = static_cast<int>(parsed);
    return static_cast<long>(value) == parsed;
}

static bool parse_float_argument(const char* text, float& value) {
    char* end = nullptr;
    float parsed = std::strtof(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

static uint64_t replay_fingerprint(Simulation& sim) {
    uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](uint64_t value) {
        for (int byte = 0; byte < 8; byte++) {
            hash ^= (value >> (byte * 8)) & 0xffULL;
            hash *= 1099511628211ULL;
        }
    };
    auto mix_float = [&](float value) { mix(std::bit_cast<uint32_t>(value)); };

    mix(sim.tick());
    mix(sim.alive_count());
    mix(sim.ever_created());
    mix_float(sim.current_quota());
    mix_float(sim.factory_health());
    mix_float(sim.total_food_shipped());
    const Grid& grid = sim.grid();
    for (int y = 0; y < grid.height(); y++)
        for (int x = 0; x < grid.width(); x++) {
            const auto& data = grid.data_at(x, y);
            mix(static_cast<uint8_t>(grid.at(x, y)));
            mix(data.built);
            mix_float(data.resource_amount);
            mix_float(data.resource_max);
            mix_float(data.resource_regen);
            mix_float(data.build_progress);
            mix_float(data.build_cost);
            mix(static_cast<uint8_t>(data.machine_type));
            mix_float(data.stored_food);
            mix_float(data.stored_raw_food);
            mix_float(data.stored_raw_material);
            mix_float(data.stored_construction_material);
            mix_float(data.stored_output);
            mix_float(data.storage_capacity);
            mix(static_cast<uint8_t>(data.conveyor_dir));
            mix_float(data.conveyor_condition);
            mix(static_cast<uint8_t>(data.conveyor_contents_type));
            mix_float(data.conveyor_contents);
            mix(data.maintenance_priority);
            mix(data.occupancy_capacity);
            mix(data.overcapacity_ticks);
        }

    auto agents = sim.alive_agents();
    std::sort(agents.begin(), agents.end(), [&](entt::entity left, entt::entity right) {
        return sim.registry().get<AgentComponent>(left).id
             < sim.registry().get<AgentComponent>(right).id;
    });
    for (auto entity : agents) {
        const auto& agent = sim.registry().get<AgentComponent>(entity);
        const auto& position = sim.registry().get<PositionComponent>(entity);
        const auto& needs = sim.registry().get<NeedsComponent>(entity);
        const auto& inventory = sim.registry().get<InventoryComponent>(entity);
        const auto& action = sim.registry().get<ActionComponent>(entity);
        mix(agent.id);
        mix(position.x);
        mix(position.y);
        mix(static_cast<uint8_t>(action.current));
        mix(action.target_x);
        mix(action.target_y);
        mix_float(needs.hunger);
        mix_float(needs.rest);
        mix_float(needs.social);
        mix_float(needs.expression);
        mix_float(needs.purpose);
        mix_float(needs.meaning);
        mix_float(inventory.raw_food);
        mix_float(inventory.raw_material);
        mix_float(inventory.food);
        mix_float(inventory.construction_material);
        mix_float(inventory.output);
    }
    return hash;
}

// Collect archetype name for an agent_id (may be dead, search all entities)
static const char* find_archetype_name(Simulation& sim, int agent_id) {
    auto view = sim.registry().view<const AgentComponent, const PersonalityComponent>();
    for (auto e : view) {
        auto& ag = sim.registry().get<AgentComponent>(e);
        if (ag.id == agent_id) {
            auto& ps = sim.registry().get<PersonalityComponent>(e);
            return archetype_name(ps.archetype);
        }
    }
    return "?";
}

// ============================================================
// Commands
// ============================================================

static int cmd_run(int argc, char* argv[]) {
    int ticks = 500;
    if (argc > 2) ticks = std::atoi(argv[2]);
    if (ticks <= 0) ticks = 500;

    Config cfg = make_config(argc, argv, 3);
    Simulation sim(cfg);

    std::printf("=== La Vida Misma - Batch Run ===\n");
    std::printf("Grid: %dx%d  Agents: %d  Ticks: %d  Seed: %d\n",
        cfg.grid_width, cfg.grid_height, cfg.initial_population, ticks, cfg.seed);

    int sample_interval = std::max(1, ticks / 20);
    std::printf("\n%6s %5s %5s %5s %5s %5s %5s %5s %5s | %5s %5s %5s | %4s %4s\n",
        "tick", "alive", "GATH", "BUIL", "WORK", "EAT", "REST", "SOC", "OTHR",
        "rawF", "rawM", "food", "mach", "raw");
    for (int t = 0; t < ticks; t++) {
        sim.advance();
        if ((t + 1) % sample_interval == 0 || t == 0) {
            int alive = sim.alive_count();
            int act_counts[14] = {};
            float inv_rf = 0, inv_rm = 0, inv_f = 0;
            auto av = sim.alive_agents();
            for (auto e : av) {
                auto& a = sim.registry().get<ActionComponent>(e);
                auto& iv = sim.registry().get<InventoryComponent>(e);
                act_counts[(int)a.current]++;
                inv_rf += iv.raw_food;
                inv_rm += iv.raw_material;
                inv_f  += iv.food;
            }
            int other = act_counts[(int)ActionType::CREATE]
                      + act_counts[(int)ActionType::EXPLORE]
                      + act_counts[(int)ActionType::GET_FOOD]
                      + act_counts[(int)ActionType::MAINTAIN]
                      + act_counts[(int)ActionType::DISMANTLE]
                      + act_counts[(int)ActionType::SABOTAGE]
                      + act_counts[(int)ActionType::IDLE];
            std::printf("%6d %5d %5d %5d %5d %5d %5d %5d %5d | %5.1f %5.1f %5.1f | %4d %4.0f\n",
                t + 1, alive,
                act_counts[(int)ActionType::GATHER],
                act_counts[(int)ActionType::BUILD],
                act_counts[(int)ActionType::WORK],
                act_counts[(int)ActionType::EAT],
                act_counts[(int)ActionType::REST],
                act_counts[(int)ActionType::SOCIALIZE],
                other,
                inv_rf, inv_rm, inv_f,
                sim.built_machine_count(),
                sim.total_raw_gathered());
        }
    }

    float avg_quota = 0.0f;
    if (sim.metrics().quota_demand > 0.001)
        avg_quota = static_cast<float>(sim.metrics().output_shipped /
                                       sim.metrics().quota_demand);
    std::printf("\nDone. alive=%d  machines=%d  resident_completions=%d  conv=%d  food=%.1f  quota=%.0f%% (avg=%.0f%%)\n",
        sim.alive_count(), sim.built_machine_count(), sim.total_machines_built(),
        sim.built_conveyor_count(),
        sim.total_storage_food(), sim.last_quota_fill() * 100, std::min(avg_quota, 1.0f) * 100);

    // Built machines summary
    {
        auto& g2 = sim.grid();
        int nf=0, no=0, nm=0;
        for (int y = 0; y < g2.height(); y++)
            for (int x = 0; x < g2.width(); x++)
                if (g2.at(x,y) == TileType::Machine && g2.data_at(x,y).built) {
                    auto& d2 = g2.data_at(x,y);
                    const char* mt2 = d2.machine_type == MachineType::Food ? "FOOD" :
                                     d2.machine_type == MachineType::Materials ? "MAT" : "OUT";
                    if (d2.machine_type == MachineType::Food) nf++;
                    else if (d2.machine_type == MachineType::Output) no++;
                    else nm++;
                    std::printf("  M(%s) (%d,%d) out=%.2f food=%.2f raw_mat=%.2f res_amt=%.2f bor=%d\n",
                        mt2, x, y, d2.stored_output, d2.stored_food, d2.stored_raw_material,
                        d2.resource_amount, d2.built_on_resource);
                }
        std::printf("  Summary: %d Food, %d Output, %d Mat\n", nf, no, nm);
        std::printf("  Total storage: food=%.1f output=%.1f constr_mat=%.1f\n",
            sim.total_storage_food(), sim.total_storage_output(), sim.total_storage_constr_mat());
        // Debug: show all ScrapPiles and their claim status
        for (int y = 0; y < g2.height(); y++)
            for (int x = 0; x < g2.width(); x++)
                if (g2.at(x,y) == TileType::ScrapPile) {
                    auto& dd = g2.data_at(x,y);
                    std::printf("  SP(%d,%d) claimed=%d has_machine=%d\n",
                        x, y, dd.claimed_by, (int)(g2.at(x,y) == TileType::ScrapPile));
                }
        // Exit-adjacent Storage
        for (auto& [ex,ey] : g2.find_all(TileType::Exit))
            for (int dy = -3; dy <= 3; dy++)
                for (int dx = -3; dx <= 3; dx++) {
                    int nx=ex+dx, ny=ey+dy;
                    if (nx<0||nx>=g2.width()||ny<0||ny>=g2.height()) continue;
                    if (g2.at(nx,ny) == TileType::Storage)
                        std::printf("  ExitStorage(%d,%d) out=%.2f food=%.2f raw=%.2f\n",
                            nx,ny, g2.data_at(nx,ny).stored_output, g2.data_at(nx,ny).stored_food,
                            g2.data_at(nx,ny).stored_raw_material);
                }
    }
    return 0;
}

static int cmd_replay(int argc, char* argv[]) {
    if (argc != 5) {
        std::fprintf(stderr, "Usage: vida_batch replay <ticks> <seed> <interventions.toml>\n");
        return 1;
    }
    int ticks = 0;
    int seed = 0;
    if (!parse_int_argument(argv[2], ticks) || ticks <= 0
        || !parse_int_argument(argv[3], seed)) {
        std::fprintf(stderr, "replay requires a positive tick count and an integer seed\n");
        return 1;
    }

    int recorded_seed = 0;
    DirectorMode recorded_mode = DirectorMode::NORMAL;
    uint64_t recorded_config_fingerprint = 0;
    std::vector<DirectorEvent> events;
    std::string error;
    if (!read_director_log(argv[4], recorded_seed, recorded_mode,
                           recorded_config_fingerprint, events, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (recorded_seed != seed) {
        std::fprintf(stderr, "replay seed does not match intervention log\n");
        return 1;
    }
    if (!events.empty() && events.back().tick >= ticks) {
        std::fprintf(stderr, "intervention event is outside requested replay ticks\n");
        return 1;
    }

    std::string config_path = runtime_config_path();
    uint64_t current_config_fingerprint = 0;
    if (!fingerprint_config_source(config_path, current_config_fingerprint, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (current_config_fingerprint != recorded_config_fingerprint) {
        std::fprintf(stderr, "replay configuration does not match intervention log\n");
        return 1;
    }

    Config cfg = load_config(config_path);
    cfg.seed = seed;
    cfg.director_mode = recorded_mode;
    Simulation sim(cfg);
    size_t event_index = 0;
    for (int step = 0; step < ticks; step++) {
        while (event_index < events.size() && events[event_index].tick == sim.tick()) {
            DirectorResult result = sim.replay_director_event(events[event_index]);
            if (!result.applied()) {
                std::fprintf(stderr, "replay event %zu rejected: %s\n", event_index,
                    director_error_name(result.error));
                return 1;
            }
            event_index++;
        }
        sim.advance();
    }
    if (event_index != events.size()) {
        std::fprintf(stderr, "replay did not consume every intervention event\n");
        return 1;
    }

    uint64_t fingerprint = replay_fingerprint(sim);
    std::printf("{\"schema_version\":1,\"command\":\"replay\",\"ticks\":%d,"
                "\"seed\":%d,\"interventions\":%zu,\"alive\":%d,\"ever\":%d,"
                "\"quota\":%.9g,\"quota_demand\":%.17g,\"output_shipped\":%.17g,"
                "\"chronicle_events\":%zu,\"state_fingerprint\":\"%016llx\"}\n",
        ticks, seed, events.size(), sim.alive_count(), sim.ever_created(),
        sim.current_quota(), sim.metrics().quota_demand, sim.metrics().output_shipped,
        sim.chronicle().size(), static_cast<unsigned long long>(fingerprint));
    return 0;
}

static int cmd_story(int argc, char* argv[]) {
    int ticks = 500;
    if (argc > 2) ticks = std::atoi(argv[2]);
    if (ticks <= 0) ticks = 500;

    Config cfg = make_config(argc, argv, 3);
    std::printf("# La Vida Misma — Story Mode\n");
    std::printf("# Grid: %dx%d  Agents: %d  Ticks: %d  Seed: %d\n\n",
        cfg.grid_width, cfg.grid_height, cfg.initial_population, ticks, cfg.seed);

    Simulation sim(cfg);
    for (int t = 0; t < ticks; t++) {
        sim.advance();
    }

    // Narrative milestones
    std::printf("== NARRATIVE ARC ==\n");
    std::printf("%s", sim.chronicle().narrative_summary().c_str());

    // Per-agent arcs: alive first, then dead
    auto alive = sim.alive_agents();
    std::vector<int> alive_ids, dead_ids;

    auto all_view = sim.registry().view<const AgentComponent>();
    for (auto e : all_view) {
        auto& ag = sim.registry().get<AgentComponent>(e);
        if (ag.alive) alive_ids.push_back(ag.id);
        else dead_ids.push_back(ag.id);
    }

    std::printf("\n== AGENT ARCS (%d alive, %d dead) ==\n",
        (int)alive_ids.size(), (int)dead_ids.size());

    for (int id : alive_ids) {
        const char* arch = find_archetype_name(sim, id);
        std::printf("\n--- Agent %d (%s) ALIVE ---\n", id, arch);
        std::printf("  %s\n", sim.chronicle().agent_arc(id, arch).c_str());
        std::printf("%s", sim.chronicle().agent_journal(id, arch, 3, 12).c_str());
    }
    for (int id : dead_ids) {
        const char* arch = find_archetype_name(sim, id);
        std::printf("\n--- Agent %d (%s) DEAD ---\n", id, arch);
        std::printf("  %s\n", sim.chronicle().agent_arc(id, arch).c_str());
        std::printf("%s", sim.chronicle().agent_journal(id, arch, 2, 8).c_str());
    }

    // Death report
    std::printf("\n%s", sim.chronicle().death_report().c_str());

    // Production summary
    std::printf("\n== EPILOGUE ==\n");
    std::printf("  Factory health: %.0f%%\n", sim.factory_health() * 100);
    std::printf("  Resident machine completions: %d\n", sim.total_machines_built());

    // Show built machines with type and position
    auto& g = sim.grid();
    int n_food = 0, n_out = 0, n_mat = 0;
    for (int y = 0; y < g.height(); y++)
        for (int x = 0; x < g.width(); x++) {
            if (g.at(x, y) == TileType::Machine && g.data_at(x, y).built) {
                auto& d = g.data_at(x, y);
                const char* mt = d.machine_type == MachineType::Food ? "FOOD" :
                                 d.machine_type == MachineType::Materials ? "MAT" : "OUT";
                if (d.machine_type == MachineType::Food) n_food++;
                else if (d.machine_type == MachineType::Output) n_out++;
                else n_mat++;
                std::printf("    Machine(%s) at (%d,%d) stored_out=%.2f stored_food=%.2f\n",
                    mt, x, y, d.stored_output, d.stored_food);
            }
        }
    std::printf("  Summary: %d Food, %d Output, %d Materials\n", n_food, n_out, n_mat);

    // Show Exit-adjacent Storage contents
    auto exits = g.find_all(TileType::Exit);
    for (auto& [ex, ey] : exits) {
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                int nx = ex+dx, ny = ey+dy;
                if (nx < 0 || nx >= g.width() || ny < 0 || ny >= g.height()) continue;
                if (g.at(nx, ny) == TileType::Storage) {
                    auto& d = g.data_at(nx, ny);
                    std::printf("    Exit-r3 Storage(%d,%d): out=%.2f food=%.2f raw=%.2f\n",
                        nx, ny, d.stored_output, d.stored_food, d.stored_raw_material);
                }
            }
    }
    std::printf("  Food shipped: %.1f\n", sim.total_food_shipped());
    std::printf("  Sabotages: %d  Post-sabotage pauses: %d  Suicides: %d\n",
        sim.sabotages_total(), sim.post_sabotage_pauses(), sim.suicides_total());
    std::printf("  Communities: %d  Artifacts: %d\n",
        sim.communities_detected(), sim.artifacts_created());

    return 0;
}

static int cmd_agent(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: vida_batch agent <agent_id> <ticks> [seed]\n");
        return 1;
    }
    int agent_id = std::atoi(argv[2]);
    int ticks = 500;
    if (argc > 3) ticks = std::atoi(argv[3]);
    if (ticks <= 0) ticks = 500;

    Config cfg = make_config(argc, argv, 4);
    Simulation sim(cfg);
    for (int t = 0; t < ticks; t++) {
        sim.advance();
    }

    const char* arch = find_archetype_name(sim, agent_id);
    auto evs = sim.chronicle().by_agent(agent_id);

    std::printf("# Journal of Agent %d (%s)\n", agent_id, arch);
    std::printf("# %d ticks, seed %d, %zu events\n\n",
        ticks, cfg.seed, evs.size());

    if (evs.empty()) {
        std::printf("(no recorded events)\n");
        return 0;
    }

    // Print every event — full journal
    char buf[64];
    for (auto* ev : evs) {
        std::snprintf(buf, sizeof(buf), "[%5d] ", ev->tick);
        std::printf("  %s%s\n", buf, ev->text.c_str());
    }

    std::printf("\n---\n%s\n", sim.chronicle().agent_arc(agent_id, arch).c_str());
    return 0;
}

static int cmd_analysis(int argc, char* argv[]) {
    int ticks = 500;
    if (argc > 2) ticks = std::atoi(argv[2]);
    if (ticks <= 0) ticks = 500;

    Config cfg = make_config(argc, argv, 3);
    if (argc > 4 && !parse_int_argument(argv[4], cfg.external_policy_variant)) {
        std::fprintf(stderr, "Policy variant must be 0 or 1\n");
        return 1;
    }
    if (cfg.external_policy_variant < 0 || cfg.external_policy_variant > 1) {
        std::fprintf(stderr, "Policy variant must be 0 or 1\n");
        return 1;
    }
    Simulation sim(cfg);
    for (int t = 0; t < ticks; t++) {
        sim.advance();
    }

    std::printf("# La Vida Misma — Ex-Post Analysis\n");
    std::printf("# Ticks: %d  Seed: %d  Events: %zu\n\n",
        ticks, cfg.seed, sim.chronicle().size());

    // Event distribution
    std::printf("%s", sim.chronicle().event_distribution().c_str());

    // Observed relationship-graph communities
    std::printf("\n%s", sim.chronicle().community_arcs().c_str());

    // Crisis timeline
    std::printf("\n%s", sim.chronicle().crisis_timeline().c_str());

    // Death report
    std::printf("\n%s", sim.chronicle().death_report().c_str());

    // Per-agent arc summaries (compact)
    std::printf("\n--- Agent Arcs ---\n");
    auto all_view = sim.registry().view<const AgentComponent>();
    for (auto e : all_view) {
        auto& ag = sim.registry().get<AgentComponent>(e);
        const char* arch = find_archetype_name(sim, ag.id);
        std::printf("  A%-2d %-9s %s  %s\n", ag.id, arch,
            ag.alive ? "ALIVE" : "DEAD ",
            sim.chronicle().agent_arc(ag.id, arch).c_str());
    }

    // Aggregate stats
    std::printf("\n--- Aggregate ---\n");
    std::printf("  Factory health:    %.2f\n", sim.factory_health());
    std::printf("  Total events:      %zu\n", sim.chronicle().size());
    std::printf("  Resident completions: %d\n", sim.total_machines_built());
    std::printf("  Food shipped:      %.1f\n", sim.total_food_shipped());
    std::printf("  Sabotages:         %d\n", sim.sabotages_total());
    std::printf("  Post-sabotage pauses: %d\n", sim.post_sabotage_pauses());
    std::printf("  Suicides:          %d\n", sim.suicides_total());
    std::printf("  Communities:       %d\n", sim.communities_detected());
    std::printf("  Artifacts:         %d\n", sim.artifacts_created());

    int total_restr = sim.total_restructures();
    std::printf("\n--- Institutional Policy ---\n");
    std::printf("  Policy variant:           %s\n",
        cfg.external_policy_variant == 1 ? "indifferent" : "strategic legacy");
    std::printf("  Restructures total:       %d\n", total_restr);
    if (cfg.external_policy_variant == 0) {
        std::printf("  Foreman reports:          %d\n", sim.foreman_reports());
    }

    return 0;
}

static int cmd_jsonl(int argc, char* argv[]) {
    int ticks = 500;
    if (argc > 2) ticks = std::atoi(argv[2]);
    if (ticks <= 0) ticks = 500;

    Config cfg = make_config(argc, argv, 3);
    Simulation sim(cfg);
    for (int t = 0; t < ticks; t++) {
        sim.advance();
    }

    // Header comment with metadata
    std::printf("# La Vida Misma JSONL | ticks=%d seed=%d grid=%dx%d people=%d alive=%d events=%zu\n",
        ticks, cfg.seed, cfg.grid_width, cfg.grid_height,
        sim.ever_created(), sim.alive_count(), sim.chronicle().size());

    // Dump all events as JSONL
    std::printf("%s", sim.chronicle().to_jsonl().c_str());

    return 0;
}

static int cmd_map(int argc, char* argv[]) {
    Config cfg = make_config(argc, argv, 2);
    Simulation sim(cfg);
    auto& grid = sim.grid();

    std::printf("=== Map Diagnostic (seed=%d) ===\n\n", cfg.seed);

    // Count boundary walls (no inner walls — open floor plan)
    int boundary_walls = 0;
    for (int y = 0; y < grid.height(); y++)
        for (int x = 0; x < grid.width(); x++) {
            if (grid.at(x, y) == TileType::Wall)
                boundary_walls++;
        }
    std::printf("  Boundary walls: %d\n\n", boundary_walls);

    // All special tiles
    for (int y = 0; y < grid.height(); y++)
        for (int x = 0; x < grid.width(); x++) {
            auto t = grid.at(x, y);
            if (t == TileType::Machine) {
                auto& d = grid.data_at(x, y);
                const char* mt = d.machine_type == MachineType::Food ? "FOOD" :
                                 d.machine_type == MachineType::Materials ? "MAT" : "OUT";
                float input = d.machine_type == MachineType::Food ? d.stored_raw_food
                    : d.machine_type == MachineType::Materials ? d.stored_raw_material
                    : d.stored_construction_material;
                std::printf("  MACHINE %-4s at (%2d,%2d) built=%d source=%d progress=%.2f/%.2f input=%.2f\n",
                    mt, x, y, d.built, d.built_on_resource,
                    d.build_progress, d.build_cost, input);
            } else if (t == TileType::Storage) {
                auto& d = grid.data_at(x, y);
                std::printf("  STORAGE      at (%2d,%2d) cap=%.0f food=%.1f rawF=%.1f rawM=%.1f cMat=%.1f out=%.1f\n",
                    x, y, d.storage_capacity, d.stored_food, d.stored_raw_food,
                    d.stored_raw_material, d.stored_construction_material,
                    d.stored_output);
            } else if (t == TileType::Exit) {
                std::printf("  EXIT         at (%2d,%2d)\n", x, y);
            } else if (t == TileType::Entrance) {
                std::printf("  ENTRANCE     at (%2d,%2d)\n", x, y);
            } else if (t == TileType::Conveyor) {
                auto& d = grid.data_at(x, y);
                const char* dir = d.conveyor_dir == ConveyorDir::N ? "N" :
                                  d.conveyor_dir == ConveyorDir::S ? "S" :
                                  d.conveyor_dir == ConveyorDir::E ? "E" : "W";
                std::printf("  CONVEYOR %-2s  at (%2d,%2d) built=%d progress=%.2f/%.2f condition=%.2f contents=%.2f\n",
                    dir, x, y, d.built, d.build_progress, d.build_cost,
                    d.conveyor_condition, d.conveyor_contents);
            } else if (t == TileType::ScrapPile) {
                auto& d = grid.data_at(x, y);
                std::printf("  SCRAPPILE    at (%2d,%2d) amt=%.1f\n", x, y, d.resource_amount);
            } else if (t == TileType::FoodSource) {
                std::printf("  FOODSOURCE   at (%2d,%2d) amt=%.1f\n", x, y, grid.data_at(x, y).resource_amount);
            } else if (t == TileType::EatingZone) {
                std::printf("  EATINGZONE   at (%2d,%2d) built=%d\n", x, y, grid.data_at(x, y).built);
            }
        }

    // Machine-Storage adjacency
    std::printf("\n=== Machine Adjacency ===\n");
    for (int y = 0; y < grid.height(); y++)
        for (int x = 0; x < grid.width(); x++) {
            if (grid.at(x, y) != TileType::Machine) continue;
            auto& d = grid.data_at(x, y);
            const char* mt = d.machine_type == MachineType::Food ? "FOOD" :
                             d.machine_type == MachineType::Materials ? "MAT" : "OUT";
            int adj_storage = 0, adj_conveyor = 0;
            for (int dy2 = -2; dy2 <= 2; dy2++)
                for (int dx2 = -2; dx2 <= 2; dx2++) {
                    int nx = x + dx2, ny = y + dy2;
                    if (nx < 0 || nx >= grid.width() || ny < 0 || ny >= grid.height()) continue;
                    if (grid.at(nx, ny) == TileType::Storage) adj_storage++;
                    if (grid.at(nx, ny) == TileType::Conveyor) adj_conveyor++;
                }
            std::printf("  Machine(%s) at (%d,%d): %d storages, %d conveyors within r=2\n",
                mt, x, y, adj_storage, adj_conveyor);
        }

    // Exit-Storage adjacency
    std::printf("\n=== Exit Adjacency ===\n");
    auto exits = grid.find_all(TileType::Exit);
    for (auto [ex, ey] : exits) {
        int adj_storage = 0;
        for (int dy2 = -3; dy2 <= 3; dy2++)
            for (int dx2 = -3; dx2 <= 3; dx2++) {
                int nx = ex + dx2, ny = ey + dy2;
                if (nx < 0 || nx >= grid.width() || ny < 0 || ny >= grid.height()) continue;
                if (std::abs(dx2) + std::abs(dy2) <= 3
                    && grid.at(nx, ny) == TileType::Storage) adj_storage++;
            }
        std::printf("  Exit at (%d,%d): %d storages within r=3\n", ex, ey, adj_storage);
    }

    std::printf("\n=== Inherited Chain ===\n");
    std::printf("  minimum_chain_present=%s\n",
        grid.minimum_chain_present() ? "yes" : "no");
    std::printf("  output_connected_to_exit=%s (%d machine%s)\n",
        grid.exit_connected_output_machine_count() > 0 ? "yes" : "no",
        grid.exit_connected_output_machine_count(),
        grid.exit_connected_output_machine_count() == 1 ? "" : "s");

    return 0;
}

// ============================================================
// Culture diagnostic: correlate personality traits with cultural actions
// ============================================================
static int cmd_culture(int argc, char* argv[]) {
    int ticks = 2000;
    if (argc > 2) ticks = std::atoi(argv[2]);
    if (ticks <= 0) ticks = 2000;

    Config cfg = make_config(argc, argv, 3, true);  // force calm
    Simulation sim(cfg);

    struct AgentTrack {
        int id = -1;
        const char* arch = "?";
        float artistry = 0, gregariousness = 0, curiosity = 0, compliance = 0;
        int create_ticks = 0, social_ticks = 0, explore_ticks = 0;
        int work_ticks = 0, gather_ticks = 0, build_ticks = 0;
        int rest_ticks = 0, eat_ticks = 0;
        int alive_ticks = 0;
    };
    std::vector<AgentTrack> tracks;

    // Initialize from spawn
    {
        auto view = sim.registry().view<const AgentComponent, const PersonalityComponent>();
        for (auto e : view) {
            auto& ag = sim.registry().get<AgentComponent>(e);
            auto& ps = sim.registry().get<PersonalityComponent>(e);
            AgentTrack t;
            t.id = ag.id;
            t.arch = archetype_name(ps.archetype);
            t.artistry = ps.artistry;
            t.gregariousness = ps.gregariousness;
            t.curiosity = ps.curiosity;
            t.compliance = ps.compliance;
            tracks.push_back(t);
        }
        std::sort(tracks.begin(), tracks.end(), [](auto& a, auto& b) { return a.id < b.id; });
    }

    for (int t = 0; t < ticks; t++) {
        sim.advance();

        auto view = sim.registry().view<const ActionComponent, const AgentComponent,
                                        const PersonalityComponent>();
        for (auto e : view) {
            auto& ag = sim.registry().get<AgentComponent>(e);
            if (!ag.alive) continue;
            if (ag.id < 0) continue;
            if (ag.id >= static_cast<int>(tracks.size()))
                tracks.resize(static_cast<size_t>(ag.id) + 1);
            if (tracks[ag.id].id < 0) {
                const auto& ps = sim.registry().get<PersonalityComponent>(e);
                tracks[ag.id].id = ag.id;
                tracks[ag.id].arch = archetype_name(ps.archetype);
                tracks[ag.id].artistry = ps.artistry;
                tracks[ag.id].gregariousness = ps.gregariousness;
                tracks[ag.id].curiosity = ps.curiosity;
                tracks[ag.id].compliance = ps.compliance;
            }
            auto& a = sim.registry().get<ActionComponent>(e);
            auto& tr = tracks[ag.id];
            tr.alive_ticks++;
            switch (a.current) {
                case ActionType::CREATE:    tr.create_ticks++; break;
                case ActionType::SOCIALIZE: tr.social_ticks++; break;
                case ActionType::EXPLORE:   tr.explore_ticks++; break;
                case ActionType::WORK:      tr.work_ticks++; break;
                case ActionType::GATHER:    tr.gather_ticks++; break;
                case ActionType::BUILD:     tr.build_ticks++; break;
                case ActionType::REST:      tr.rest_ticks++; break;
                case ActionType::EAT:       tr.eat_ticks++; break;
                default: break;
            }
        }
    }

    // Print per-agent table
    std::printf("# Cultural Behavior vs Personality (calm mode, %d ticks, seed %d)\n\n", ticks, cfg.seed);
    std::printf("%3s %-9s %4s %4s %4s %4s | %5s %5s %5s %5s %5s %5s %5s\n",
        "ID", "Arch", "Art", "Greg", "Cur", "Comp",
        "CRE%", "SOC%", "EXP%", "WRK%", "BLD%", "GATH%", "RST%");
    for (auto& tr : tracks) {
        float at = std::max(1, tr.alive_ticks);
        std::printf("%3d %-9s %.2f %.2f %.2f %.2f | %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f\n",
            tr.id, tr.arch, tr.artistry, tr.gregariousness, tr.curiosity, tr.compliance,
            100.0f * tr.create_ticks / at,
            100.0f * tr.social_ticks / at,
            100.0f * tr.explore_ticks / at,
            100.0f * tr.work_ticks / at,
            100.0f * tr.build_ticks / at,
            100.0f * tr.gather_ticks / at,
            100.0f * tr.rest_ticks / at);
    }

    // Pearson correlation helper
    auto pearson = [](const std::vector<float>& xs, const std::vector<float>& ys) -> float {
        int n = xs.size();
        if (n < 3) return 0.0f;
        float mx = 0, my = 0;
        for (int i = 0; i < n; i++) { mx += xs[i]; my += ys[i]; }
        mx /= n; my /= n;
        float num = 0, dx2 = 0, dy2 = 0;
        for (int i = 0; i < n; i++) {
            float dx = xs[i] - mx, dy = ys[i] - my;
            num += dx * dy; dx2 += dx * dx; dy2 += dy * dy;
        }
        float den = std::sqrt(dx2 * dy2);
        return (den > 0.001f) ? num / den : 0.0f;
    };

    // Compute correlations
    std::vector<float> art_vals, greg_vals, cur_vals, comp_vals;
    std::vector<float> crea_pct, soc_pct, exp_pct, work_pct;
    for (auto& tr : tracks) {
        float at = std::max(1, tr.alive_ticks);
        art_vals.push_back(tr.artistry);
        greg_vals.push_back(tr.gregariousness);
        cur_vals.push_back(tr.curiosity);
        comp_vals.push_back(tr.compliance);
        crea_pct.push_back(100.0f * tr.create_ticks / at);
        soc_pct.push_back(100.0f * tr.social_ticks / at);
        exp_pct.push_back(100.0f * tr.explore_ticks / at);
        work_pct.push_back(100.0f * tr.work_ticks / at);
    }

    std::printf("\n# Correlations (Pearson r)\n");
    std::printf("%-20s %6s %6s %6s %6s\n", "", "Art", "Greg", "Cur", "Comp");
    std::printf("%-20s %6.2f %6.2f %6.2f %6.2f\n", "CREATE",
        pearson(art_vals, crea_pct), pearson(greg_vals, crea_pct),
        pearson(cur_vals, crea_pct), pearson(comp_vals, crea_pct));
    std::printf("%-20s %6.2f %6.2f %6.2f %6.2f\n", "SOCIALIZE",
        pearson(art_vals, soc_pct), pearson(greg_vals, soc_pct),
        pearson(cur_vals, soc_pct), pearson(comp_vals, soc_pct));
    std::printf("%-20s %6.2f %6.2f %6.2f %6.2f\n", "EXPLORE",
        pearson(art_vals, exp_pct), pearson(greg_vals, exp_pct),
        pearson(cur_vals, exp_pct), pearson(comp_vals, exp_pct));
    std::printf("%-20s %6.2f %6.2f %6.2f %6.2f\n", "WORK",
        pearson(art_vals, work_pct), pearson(greg_vals, work_pct),
        pearson(cur_vals, work_pct), pearson(comp_vals, work_pct));

    // Interpretation
    std::printf("\n# Interpretation\n");
    float r_art_crea = pearson(art_vals, crea_pct);
    float r_greg_soc = pearson(greg_vals, soc_pct);
    float r_cur_exp = pearson(cur_vals, exp_pct);
    float r_comp_work = pearson(comp_vals, work_pct);
    std::printf("  artistry  → CREATE:     r=%.2f  %s\n", r_art_crea,
        r_art_crea > 0.3f ? "STRONG (correct)" : r_art_crea > 0.1f ? "weak" : "NONE (problem)");
    std::printf("  gregariou → SOCIALIZE:  r=%.2f  %s\n", r_greg_soc,
        r_greg_soc > 0.3f ? "STRONG (correct)" : r_greg_soc > 0.1f ? "weak" : "NONE (problem)");
    std::printf("  curiosity → EXPLORE:    r=%.2f  %s\n", r_cur_exp,
        r_cur_exp > 0.3f ? "STRONG (correct)" : r_cur_exp > 0.1f ? "weak" : "NONE (problem)");
    std::printf("  compliance→ WORK:       r=%.2f  %s\n", r_comp_work,
        r_comp_work > 0.3f ? "STRONG (correct)" : r_comp_work > 0.1f ? "weak" : "NONE (problem)");

    std::printf("\nDone. alive=%d  artifacts=%d\n", sim.alive_count(), sim.artifacts_created());
    return 0;
}

static const char* director_mode_name(DirectorMode mode) {
    switch (mode) {
        case DirectorMode::CALM:            return "calm";
        case DirectorMode::PRODUCTION_TEST: return "production";
        default:                            return "normal";
    }
}

struct MetricsSample {
    int tick;
    int alive;
    float support;
    float supply_factor;
    double quota_demand;
    double output_shipped;
    double output_produced;
    double raw_food_regenerated;
    double raw_material_regenerated;
    double raw_food_regeneration_requested;
    double raw_material_regeneration_requested;
    float source_raw_food;
    float source_raw_material;
    float storage_food;
};

static double normalized_action_entropy(
    const std::array<uint64_t, METRIC_ACTION_COUNT>& counts)
{
    double total = 0.0;
    for (uint64_t count : counts) total += count;
    if (total <= 0.0) return 0.0;
    double entropy = 0.0;
    for (uint64_t count : counts) {
        if (count == 0) continue;
        double probability = count / total;
        entropy -= probability * std::log(probability);
    }
    return entropy / std::log(static_cast<double>(METRIC_ACTION_COUNT));
}

static double pearson_correlation(const std::vector<double>& x,
                                  const std::vector<double>& y) {
    if (x.size() < 2 || x.size() != y.size()) return 0.0;
    double mean_x = 0.0, mean_y = 0.0;
    for (size_t i = 0; i < x.size(); i++) { mean_x += x[i]; mean_y += y[i]; }
    mean_x /= x.size();
    mean_y /= y.size();
    double covariance = 0.0, variance_x = 0.0, variance_y = 0.0;
    for (size_t i = 0; i < x.size(); i++) {
        double dx = x[i] - mean_x;
        double dy = y[i] - mean_y;
        covariance += dx * dy;
        variance_x += dx * dx;
        variance_y += dy * dy;
    }
    if (variance_x <= 0.0 || variance_y <= 0.0) return 0.0;
    return covariance / std::sqrt(variance_x * variance_y);
}

struct ActionEmergenceSummary {
    double mean_entropy = 0.0;
    double agent_specialization = 0.0;
    double population_specialization = 0.0;
    double contribution_benefit_correlation = 0.0;
};

static ActionEmergenceSummary summarize_agent_evidence(const SimulationMetrics& metrics) {
    ActionEmergenceSummary result;
    std::array<double, METRIC_ACTION_COUNT> action_totals{};
    std::vector<double> contributions;
    std::vector<double> benefits;
    double total_ticks = 0.0;
    double entropy_sum = 0.0;
    size_t agents = 0;

    for (size_t id = 0; id < metrics.agent_action_ticks.size(); id++) {
        const auto& row = metrics.agent_action_ticks[id];
        double agent_ticks = 0.0;
        for (size_t action = 0; action < METRIC_ACTION_COUNT; action++) {
            agent_ticks += row[action];
            action_totals[action] += row[action];
        }
        if (agent_ticks <= 0.0) continue;
        entropy_sum += normalized_action_entropy(row);
        total_ticks += agent_ticks;
        agents++;
        contributions.push_back(static_cast<double>(
            metrics.agent_productive_effect_ticks[id]));
        benefits.push_back(metrics.agent_food_consumed[id]);
    }
    if (agents == 0 || total_ticks <= 0.0) return result;

    result.mean_entropy = entropy_sum / agents;
    result.agent_specialization = 1.0 - result.mean_entropy;
    double action_entropy = 0.0;
    for (double count : action_totals) {
        if (count <= 0.0) continue;
        double probability = count / total_ticks;
        action_entropy -= probability * std::log(probability);
    }
    double mutual_information = 0.0;
    for (const auto& row : metrics.agent_action_ticks) {
        double agent_ticks = 0.0;
        for (uint64_t count : row) agent_ticks += count;
        if (agent_ticks <= 0.0) continue;
        for (size_t action = 0; action < METRIC_ACTION_COUNT; action++) {
            if (row[action] == 0 || action_totals[action] <= 0.0) continue;
            double joint = row[action] / total_ticks;
            double agent_probability = agent_ticks / total_ticks;
            double action_probability = action_totals[action] / total_ticks;
            mutual_information += joint * std::log(
                joint / (agent_probability * action_probability));
        }
    }
    result.population_specialization = action_entropy > 0.0
        ? mutual_information / action_entropy : 0.0;
    result.contribution_benefit_correlation = pearson_correlation(
        contributions, benefits);
    return result;
}

static int cmd_metrics(int argc, char* argv[]) {
    int ticks = 500;
    if (argc > 2) ticks = std::atoi(argv[2]);
    if (ticks <= 0) ticks = 500;

    Config cfg = make_config(argc, argv, 3);
    if (argc > 4) {
        std::string mode = argv[4];
        if (mode == "normal") cfg.director_mode = DirectorMode::NORMAL;
        else if (mode == "calm") cfg.director_mode = DirectorMode::CALM;
        else if (mode == "production") cfg.director_mode = DirectorMode::PRODUCTION_TEST;
        else {
            std::fprintf(stderr, "Unknown metrics mode: %s\n", argv[4]);
            return 1;
        }
    }

    if (argc > 5 && !parse_int_argument(argv[5], cfg.external_supply_variant)) {
        std::fprintf(stderr, "Supply variant must be 0 or 1\n");
        return 1;
    }
    if (cfg.external_supply_variant < 0 || cfg.external_supply_variant > 1) {
        std::fprintf(stderr, "Supply variant must be 0 or 1\n");
        return 1;
    }
    int block_start = argc > 6 ? std::atoi(argv[6]) : -1;
    int block_end = argc > 7 ? std::atoi(argv[7]) : -1;
    int sample_every = argc > 8 ? std::atoi(argv[8]) : 0;
    if (argc > 9 && !parse_float_argument(argv[9], cfg.restructure_probability)) {
        std::fprintf(stderr, "Restructure probability must be between 0 and 1\n");
        return 1;
    }
    if (argc > 10 && !parse_int_argument(argv[10], cfg.external_policy_variant)) {
        std::fprintf(stderr, "Policy variant must be 0 or 1\n");
        return 1;
    }
    int social_learning = cfg.social_learning_enabled ? 1 : 0;
    int spatial_affinity = cfg.spatial_affinity_enabled ? 1 : 0;
    int artifact_effects = cfg.artifact_effects_enabled ? 1 : 0;
    int natural_mortality = cfg.natural_mortality_enabled ? 1 : 0;
    int arrivals = cfg.arrivals_enabled ? 1 : 0;
    int reproduction = cfg.reproduction_enabled ? 1 : 0;
    if (argc > 11 && !parse_int_argument(argv[11], social_learning)) {
        std::fprintf(stderr, "Social-learning toggle must be 0 or 1\n");
        return 1;
    }
    if (argc > 12 && !parse_int_argument(argv[12], spatial_affinity)) {
        std::fprintf(stderr, "Spatial-affinity toggle must be 0 or 1\n");
        return 1;
    }
    if (argc > 13 && !parse_int_argument(argv[13], artifact_effects)) {
        std::fprintf(stderr, "Artifact-effects toggle must be 0 or 1\n");
        return 1;
    }
    if (argc > 14 && !parse_int_argument(argv[14], natural_mortality)) {
        std::fprintf(stderr, "Natural-mortality toggle must be 0 or 1\n");
        return 1;
    }
    if (argc > 15 && !parse_int_argument(argv[15], arrivals)) {
        std::fprintf(stderr, "Arrivals toggle must be 0 or 1\n");
        return 1;
    }
    if (argc > 16 && !parse_int_argument(argv[16], reproduction)) {
        std::fprintf(stderr, "Reproduction toggle must be 0 or 1\n");
        return 1;
    }
    if (social_learning < 0 || social_learning > 1
        || spatial_affinity < 0 || spatial_affinity > 1
        || artifact_effects < 0 || artifact_effects > 1
        || natural_mortality < 0 || natural_mortality > 1
        || arrivals < 0 || arrivals > 1
        || reproduction < 0 || reproduction > 1) {
        std::fprintf(stderr, "Mechanism toggles must be 0 or 1\n");
        return 1;
    }
    cfg.social_learning_enabled = social_learning != 0;
    cfg.spatial_affinity_enabled = spatial_affinity != 0;
    cfg.artifact_effects_enabled = artifact_effects != 0;
    cfg.natural_mortality_enabled = natural_mortality != 0;
    cfg.arrivals_enabled = arrivals != 0;
    cfg.reproduction_enabled = reproduction != 0;
    if (block_start >= 0 && block_end >= 0 && block_end <= block_start) {
        std::fprintf(stderr, "Shipping block end must be greater than start\n");
        return 1;
    }
    if (sample_every < 0) sample_every = 0;
    if (cfg.restructure_probability < 0.0f || cfg.restructure_probability > 1.0f) {
        std::fprintf(stderr, "Restructure probability must be between 0 and 1\n");
        return 1;
    }
    if (cfg.external_policy_variant < 0 || cfg.external_policy_variant > 1) {
        std::fprintf(stderr, "Policy variant must be 0 or 1\n");
        return 1;
    }

    Simulation sim(cfg);
    std::vector<MetricsSample> timeline;
    for (int t = 0; t < ticks; t++) {
        bool blocked = block_start >= 0 && t >= block_start
            && (block_end < 0 || t < block_end);
        sim.set_output_shipping_enabled(!blocked);
        sim.advance();
        if (sample_every > 0 && sim.tick() % sample_every == 0) {
            const auto& sample_metrics = sim.metrics();
            timeline.push_back({
                sim.tick(), sim.alive_count(), sim.external_support(),
                sim.external_supply_factor(), sample_metrics.quota_demand,
                sample_metrics.output_shipped, sim.total_output_produced(),
                sample_metrics.resources_regenerated[metric_index(ResourceType::RAW_FOOD)],
                sample_metrics.resources_regenerated[metric_index(ResourceType::RAW_MATERIAL)],
                sample_metrics.regeneration_requested[metric_index(ResourceType::RAW_FOOD)],
                sample_metrics.regeneration_requested[metric_index(ResourceType::RAW_MATERIAL)],
                sim.total_source_resource(ResourceType::RAW_FOOD),
                sim.total_source_resource(ResourceType::RAW_MATERIAL),
                sim.total_storage_food(),
            });
        }
    }

    const auto& metrics = sim.metrics();
    double avg_hunger = 0.0, avg_rest = 0.0, avg_social = 0.0;
    double avg_expression = 0.0, avg_purpose = 0.0, avg_meaning = 0.0;
    double avg_stress = 0.0, avg_mood = 0.0, avg_noncompliance = 0.0;
    double avg_factory_skill = 0.0, avg_domestic_skill = 0.0;
    double avg_artistic_skill = 0.0, avg_social_skill = 0.0;
    std::vector<int> alive_ids;
    std::vector<int> community_ids;
    auto agents = sim.registry().view<const AgentComponent, const NeedsComponent,
                                      const StressComponent, const SocialComponent,
                                      const SkillsComponent>();
    for (auto e : agents) {
        const auto& agent = sim.registry().get<AgentComponent>(e);
        if (!agent.alive) continue;
        const auto& needs = sim.registry().get<NeedsComponent>(e);
        const auto& stress = sim.registry().get<StressComponent>(e);
        const auto& social = sim.registry().get<SocialComponent>(e);
        const auto& skills = sim.registry().get<SkillsComponent>(e);
        alive_ids.push_back(agent.id);
        if (agent.community_id >= 0) community_ids.push_back(agent.community_id);
        avg_hunger += needs.hunger;
        avg_rest += needs.rest;
        avg_social += needs.social;
        avg_expression += needs.expression;
        avg_purpose += needs.purpose;
        avg_meaning += needs.meaning;
        avg_stress += stress.value;
        avg_mood += social.mood;
        avg_noncompliance += agent.noncompliance;
        avg_factory_skill += skills.factory_work;
        avg_domestic_skill += skills.domestic;
        avg_artistic_skill += skills.artistic;
        avg_social_skill += skills.social_skill;
    }
    std::sort(alive_ids.begin(), alive_ids.end());
    std::sort(community_ids.begin(), community_ids.end());
    community_ids.erase(std::unique(community_ids.begin(), community_ids.end()), community_ids.end());

    double alive_den = alive_ids.empty() ? 1.0 : static_cast<double>(alive_ids.size());
    avg_hunger /= alive_den;
    avg_rest /= alive_den;
    avg_social /= alive_den;
    avg_expression /= alive_den;
    avg_purpose /= alive_den;
    avg_meaning /= alive_den;
    avg_stress /= alive_den;
    avg_mood /= alive_den;
    avg_noncompliance /= alive_den;
    avg_factory_skill /= alive_den;
    avg_domestic_skill /= alive_den;
    avg_artistic_skill /= alive_den;
    avg_social_skill /= alive_den;

    uint64_t relationship_edges = 0;
    double relationship_trust = 0.0;
    double relationship_familiarity = 0.0;
    for (int from : alive_ids) {
        for (int to : alive_ids) {
            if (from == to) continue;
            const auto& rel = sim.social().get_rel(from, to);
            if (rel.familiarity <= 0.05f) continue;
            relationship_edges++;
            relationship_trust += rel.trust;
            relationship_familiarity += rel.familiarity;
        }
    }
    double relationship_den = relationship_edges > 0
        ? static_cast<double>(relationship_edges) : 1.0;
    auto emergence = summarize_agent_evidence(metrics);

    struct CohortSummary {
        int entered = 0;
        int alive = 0;
        double person_ticks = 0.0;
        std::array<uint64_t, METRIC_DEATH_COUNT> deaths{};
    };
    std::map<int, CohortSummary> cohorts;
    int initial_alive = 0;
    int arrivals_alive = 0;
    int births_alive = 0;
    int integrated_agents = 0;
    int integrated_descendants = 0;
    double integration_latency_sum = 0.0;
    double peak_influence_sum = 0.0;
    std::vector<entt::entity> historical_agents;
    auto historical_view = sim.registry().view<const AgentComponent,
                                                 const LifecycleComponent>();
    for (auto entity : historical_view) historical_agents.push_back(entity);
    std::sort(historical_agents.begin(), historical_agents.end(),
        [&](entt::entity left, entt::entity right) {
            return sim.registry().get<AgentComponent>(left).id
                 < sim.registry().get<AgentComponent>(right).id;
        });
    auto metric_death_from_agent = [](DeathCause cause) {
        switch (cause) {
            case DeathCause::STARVATION: return MetricDeathCause::Starvation;
            case DeathCause::EXHAUSTION: return MetricDeathCause::Exhaustion;
            case DeathCause::BREAKDOWN: return MetricDeathCause::Breakdown;
            case DeathCause::SUICIDE: return MetricDeathCause::Suicide;
            case DeathCause::NATURAL: return MetricDeathCause::Natural;
            default: return MetricDeathCause::Other;
        }
    };
    for (auto entity : historical_agents) {
        const auto& agent = sim.registry().get<AgentComponent>(entity);
        const auto& lifecycle = sim.registry().get<LifecycleComponent>(entity);
        auto& cohort = cohorts[lifecycle.cohort];
        cohort.entered++;
        if (agent.alive) {
            cohort.alive++;
            if (lifecycle.origin == AgentOrigin::INITIAL) initial_alive++;
            else if (lifecycle.origin == AgentOrigin::ARRIVAL) arrivals_alive++;
            else births_alive++;
        }
        peak_influence_sum += lifecycle.peak_influence;
        if (lifecycle.first_trusted_edge_tick >= 0) {
            integrated_agents++;
            integration_latency_sum += lifecycle.first_trusted_edge_tick
                                     - lifecycle.entry_tick;
            if (lifecycle.origin != AgentOrigin::INITIAL) integrated_descendants++;
        }
        int terminal_tick = lifecycle.death_tick >= 0
            ? lifecycle.death_tick : sim.tick();
        cohort.person_ticks += std::max(0, terminal_tick - lifecycle.entry_tick);
        if (!agent.alive)
            cohort.deaths[metric_index(metric_death_from_agent(agent.death_cause))]++;
    }

    std::printf("{");
    std::printf("\"schema_version\":3");
    std::printf(",\"seed\":%d,\"mode\":\"%s\"", cfg.seed, director_mode_name(cfg.director_mode));
    std::printf(",\"requested_ticks\":%d,\"ticks\":%d", ticks, sim.tick());
    std::printf(",\"grid\":{\"width\":%d,\"height\":%d}", cfg.grid_width, cfg.grid_height);
    std::printf(",\"population\":{\"initial\":%d,\"max\":%d,\"alive\":%d,"
                "\"peak_alive\":%d,\"ever_created\":%d,\"arrival_attempts\":%d,"
                "\"arrivals\":%d,\"arrivals_blocked_capacity\":%d,"
                "\"births\":%d,\"births_blocked_capacity\":%d,"
                "\"initial_alive\":%d,\"arrivals_alive\":%d,"
                "\"births_alive\":%d,\"deaths\":{",
        cfg.initial_population, cfg.max_population, sim.alive_count(),
        sim.peak_population(), sim.ever_created(), sim.arrival_attempts(),
        sim.arrivals_admitted(), sim.arrivals_blocked_capacity(),
        sim.births_total(), sim.births_blocked_capacity(),
        initial_alive, arrivals_alive, births_alive);
    for (size_t i = 0; i < METRIC_DEATH_COUNT; i++) {
        if (i > 0) std::printf(",");
        auto cause = static_cast<MetricDeathCause>(i);
        std::printf("\"%s\":%llu", metric_death_name(cause),
            static_cast<unsigned long long>(metrics.deaths[i]));
    }
    std::printf("}}");

    std::printf(",\"factory\":{");
    std::printf("\"health\":%.6f,\"current_quota\":%.6f", sim.factory_health(), sim.current_quota());
    std::printf(",\"quota_demand\":%.6f,\"output_shipped\":%.6f",
        metrics.quota_demand, metrics.output_shipped);
    std::printf(",\"last_quota_fill\":%.6f,\"output_produced\":%.6f",
        sim.last_quota_fill(), sim.total_output_produced());
    double support_den = metrics.external_support_updates > 0
        ? static_cast<double>(metrics.external_support_updates) : 1.0;
    std::printf(",\"supply_variant\":%d,\"external_support\":%.6f,\"external_supply_factor\":%.6f",
        cfg.external_supply_variant, sim.external_support(), sim.external_supply_factor());
    std::printf(",\"policy_variant\":%d", cfg.external_policy_variant);
    std::printf(",\"supply_response_ticks\":%.6f,\"supply_floor\":%.6f,"
                "\"supply_low\":%.6f,\"supply_high\":%.6f",
        cfg.external_supply_response_ticks, cfg.external_supply_floor,
        cfg.external_supply_low, cfg.external_supply_high);
    std::printf(",\"mean_external_support\":%.6f,\"mean_external_supply_factor\":%.6f",
        metrics.external_support_sum / support_den,
        metrics.external_supply_factor_sum / support_den);
    std::printf(",\"shipping_block_start\":%d,\"shipping_block_end\":%d,\"shipping_blocked_ticks\":%llu",
        block_start, block_end, static_cast<unsigned long long>(metrics.shipping_blocked_ticks));
    std::printf(",\"restructure_probability\":%.6f,\"output_hauled\":%.6f,"
                "\"machines_broken\":%d,\"restructures\":%d}",
        cfg.restructure_probability, metrics.output_hauled,
        sim.total_machines_broken(), sim.total_restructures());

    std::printf(",\"actions\":{");
    for (size_t i = 0; i < METRIC_ACTION_COUNT; i++) {
        if (i > 0) std::printf(",");
        auto action = static_cast<ActionType>(i);
        double utility_den = metrics.utility_samples[i] > 0
            ? static_cast<double>(metrics.utility_samples[i]) : 1.0;
        std::printf("\"%s\":{\"selected\":%llu,\"lookups\":%llu,\"target_failures\":%llu,"
                    "\"plan_invalidations\":%llu,\"reached\":%llu,\"executed\":%llu,\"utility\":{"
                    "\"samples\":%llu,\"feasible\":%llu,\"self\":%.6f,"
                    "\"factory\":%.6f,\"cost\":%.6f,\"risk\":%.6f,\"final\":%.6f}}",
            metric_action_name(action),
            static_cast<unsigned long long>(metrics.action_selected[i]),
            static_cast<unsigned long long>(metrics.target_lookups[i]),
            static_cast<unsigned long long>(metrics.target_failures[i]),
            static_cast<unsigned long long>(metrics.plan_invalidations[i]),
            static_cast<unsigned long long>(metrics.target_reached[i]),
            static_cast<unsigned long long>(metrics.action_executed[i]),
            static_cast<unsigned long long>(metrics.utility_samples[i]),
            static_cast<unsigned long long>(metrics.feasible_samples[i]),
            metrics.utility_self_sum[i] / utility_den,
            metrics.utility_factory_sum[i] / utility_den,
            metrics.utility_cost_sum[i] / utility_den,
            metrics.utility_risk_sum[i] / utility_den,
            metrics.utility_final_sum[i] / utility_den);
    }
    std::printf("}");

    std::printf(",\"resources\":{");
    for (size_t i = 0; i < METRIC_RESOURCE_COUNT; i++) {
        if (i > 0) std::printf(",");
        auto resource = static_cast<ResourceType>(i);
        std::printf("\"%s\":{\"regeneration_base\":%.6f,\"regeneration_requested\":%.6f,"
                    "\"regenerated\":%.6f,\"produced\":%.6f,\"consumed\":%.6f,"
                    "\"lost\":%.6f}",
            metric_resource_name(resource), metrics.regeneration_base[i],
            metrics.regeneration_requested[i], metrics.resources_regenerated[i],
            metrics.resources_produced[i], metrics.resources_consumed[i],
            metrics.resources_lost[i]);
    }
    std::printf("}");

    std::printf(",\"machines\":{");
    for (size_t i = 0; i < METRIC_MACHINE_COUNT; i++) {
        if (i > 0) std::printf(",");
        auto machine = static_cast<MachineType>(i);
        std::printf("\"%s\":{\"initial_active\":%llu,\"built_events\":%llu,\"active\":%d}",
            metric_machine_name(machine),
            static_cast<unsigned long long>(metrics.initial_machines_active[i]),
            static_cast<unsigned long long>(metrics.machines_built[i]),
            sim.count_built_machines(machine));
    }
    std::printf(",\"conveyors_initial_active\":%llu,\"conveyors_active\":%d,"
                "\"storages_initial_active\":%llu,\"exit_connected_outputs_initial\":%llu,"
                "\"minimum_chain_present_initial\":%s}",
        static_cast<unsigned long long>(metrics.initial_conveyors_active),
        sim.built_conveyor_count(),
        static_cast<unsigned long long>(metrics.initial_storages_active),
        static_cast<unsigned long long>(metrics.initial_exit_connected_outputs),
        metrics.initial_minimum_chain_present ? "true" : "false");

    std::printf(",\"stocks\":{\"source_raw_food\":%.6f,\"source_raw_material\":%.6f,"
                "\"storage_food\":%.6f,\"storage_output\":%.6f,"
                "\"storage_construction_material\":%.6f,\"inventory_raw_material\":%.6f,"
                "\"inventory_construction_material\":%.6f}",
        sim.total_source_resource(ResourceType::RAW_FOOD),
        sim.total_source_resource(ResourceType::RAW_MATERIAL),
        sim.total_storage_food(), sim.total_storage_output(), sim.total_storage_constr_mat(),
        sim.total_inventory_raw_material(), sim.total_inventory_constr_mat());

    std::printf(",\"social\":{\"communities\":%zu,\"relationship_edges\":%llu,"
                "\"avg_trust\":%.6f,\"avg_familiarity\":%.6f,\"avg_mood\":%.6f,"
                "\"avg_stress\":%.6f,\"avg_noncompliance\":%.6f}",
        community_ids.size(), static_cast<unsigned long long>(relationship_edges),
        relationship_trust / relationship_den, relationship_familiarity / relationship_den,
        avg_mood, avg_stress, avg_noncompliance);

    double spatial_den = metrics.spatial_persistence_samples > 0
        ? static_cast<double>(metrics.spatial_persistence_samples) : 1.0;
    double personality_den = metrics.personality_distance_samples > 0
        ? static_cast<double>(metrics.personality_distance_samples) : 1.0;
    double modularity_den = metrics.social_modularity_samples > 0
        ? static_cast<double>(metrics.social_modularity_samples) : 1.0;
    double stability_den = metrics.community_stability_samples > 0
        ? static_cast<double>(metrics.community_stability_samples) : 1.0;
    std::printf(",\"emergence\":{\"toggles\":{\"social_learning\":%s,"
                "\"spatial_affinity\":%s,\"artifact_effects\":%s},"
                "\"spatial_cluster_persistence\":%.6f,"
                "\"spatial_samples\":%llu,"
                "\"similar_personality_distance_delta_vs_shuffled\":%.6f,"
                "\"personality_samples\":%llu,\"social_modularity\":%.6f,"
                "\"community_stability\":%.6f,\"community_samples\":%llu,"
                "\"mean_action_entropy\":%.6f,\"agent_specialization\":%.6f,"
                "\"population_specialization\":%.6f,"
                "\"contribution_benefit_correlation\":%.6f,\"agent_ledger\":[",
        cfg.social_learning_enabled ? "true" : "false",
        cfg.spatial_affinity_enabled ? "true" : "false",
        cfg.artifact_effects_enabled ? "true" : "false",
        metrics.spatial_persistence_sum / spatial_den,
        static_cast<unsigned long long>(metrics.spatial_persistence_samples),
        metrics.personality_distance_delta_sum / personality_den,
        static_cast<unsigned long long>(metrics.personality_distance_samples),
        metrics.social_modularity_sum / modularity_den,
        metrics.community_stability_sum / stability_den,
        static_cast<unsigned long long>(metrics.community_stability_samples),
        emergence.mean_entropy, emergence.agent_specialization,
        emergence.population_specialization,
        emergence.contribution_benefit_correlation);
    bool first_ledger = true;
    for (size_t id = 0; id < metrics.agent_action_ticks.size(); id++) {
        uint64_t action_ticks = 0;
        for (uint64_t count : metrics.agent_action_ticks[id]) action_ticks += count;
        if (action_ticks == 0) continue;
        if (!first_ledger) std::printf(",");
        first_ledger = false;
        std::printf("{\"id\":%zu,\"action_entropy\":%.6f,"
                    "\"productive_effect_ticks\":%llu,\"food_shared_given\":%.6f,"
                    "\"food_received\":%.6f,\"food_consumed\":%.6f}",
            id, normalized_action_entropy(metrics.agent_action_ticks[id]),
            static_cast<unsigned long long>(metrics.agent_productive_effect_ticks[id]),
            metrics.agent_food_shared_given[id], metrics.agent_food_received[id],
            metrics.agent_food_consumed[id]);
    }
    std::printf("]}");

    std::printf(",\"demography\":{\"toggles\":{\"natural_mortality\":%s,"
                "\"arrivals\":%s,\"reproduction\":%s},"
                "\"cohort_width_ticks\":%d,\"mobility\":{"
                "\"integrated_agents\":%d,\"integrated_descendants\":%d,"
                "\"mean_integration_latency\":%.6f,"
                "\"mean_peak_influence\":%.6f},\"cohorts\":[",
        cfg.natural_mortality_enabled ? "true" : "false",
        cfg.arrivals_enabled ? "true" : "false",
        cfg.reproduction_enabled ? "true" : "false",
        cfg.cohort_width_ticks, integrated_agents, integrated_descendants,
        integrated_agents > 0 ? integration_latency_sum / integrated_agents : 0.0,
        historical_agents.empty() ? 0.0
            : peak_influence_sum / historical_agents.size());
    bool first_cohort = true;
    for (const auto& [cohort_id, cohort] : cohorts) {
        if (!first_cohort) std::printf(",");
        first_cohort = false;
        std::printf("{\"id\":%d,\"entered\":%d,\"alive_censored\":%d,"
                    "\"person_ticks\":%.0f,\"deaths\":{",
            cohort_id, cohort.entered, cohort.alive, cohort.person_ticks);
        for (size_t cause_index = 0; cause_index < METRIC_DEATH_COUNT; cause_index++) {
            if (cause_index > 0) std::printf(",");
            auto cause = static_cast<MetricDeathCause>(cause_index);
            std::printf("\"%s\":%llu", metric_death_name(cause),
                static_cast<unsigned long long>(cohort.deaths[cause_index]));
        }
        std::printf("}}");
    }
    std::printf("],\"agents\":[");
    bool first_agent = true;
    for (auto entity : historical_agents) {
        if (!first_agent) std::printf(",");
        first_agent = false;
        const auto& agent = sim.registry().get<AgentComponent>(entity);
        const auto& lifecycle = sim.registry().get<LifecycleComponent>(entity);
        const auto& personality = sim.registry().get<PersonalityComponent>(entity);
        const auto& social = sim.registry().get<SocialComponent>(entity);
        int terminal_tick = lifecycle.death_tick >= 0
            ? lifecycle.death_tick : sim.tick();
        int age = lifecycle.age_at_entry
                + std::max(0, terminal_tick - lifecycle.entry_tick);
        std::printf("{\"id\":%d,\"origin\":\"%s\",\"cohort\":%d,"
                    "\"generation\":%d,\"parents\":[",
            agent.id, agent_origin_name(lifecycle.origin), lifecycle.cohort,
            lifecycle.generation);
        if (lifecycle.parent_a >= 0) std::printf("%d", lifecycle.parent_a);
        if (lifecycle.parent_b >= 0) std::printf(",%d", lifecycle.parent_b);
        std::printf("],\"entry_tick\":%d,\"age_at_entry\":%d,\"age\":%d,"
                    "\"lifespan\":%d,\"alive\":%s,\"death_tick\":%d,"
                    "\"death_cause\":\"%s\",\"first_trusted_edge_tick\":%d,"
                    "\"peak_influence\":%.6f,\"terminal_influence\":%.6f,"
                    "\"traits\":[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f]}",
            lifecycle.entry_tick, lifecycle.age_at_entry, age,
            lifecycle.lifespan, agent.alive ? "true" : "false",
            lifecycle.death_tick, death_cause_name(agent.death_cause),
            lifecycle.first_trusted_edge_tick, lifecycle.peak_influence,
            social.influence, personality.compliance, personality.laziness,
            personality.artistry, personality.gregariousness,
            personality.resilience, personality.curiosity);
    }
    std::printf("]}");

    std::printf(",\"needs\":{\"hunger\":%.6f,\"rest\":%.6f,\"social\":%.6f,"
                "\"expression\":%.6f,\"purpose\":%.6f,\"meaning\":%.6f}",
        avg_hunger, avg_rest, avg_social, avg_expression, avg_purpose, avg_meaning);
    std::printf(",\"skills\":{\"factory\":%.6f,\"domestic\":%.6f,"
                "\"artistic\":%.6f,\"social\":%.6f,\"forgetting\":false}",
        avg_factory_skill, avg_domestic_skill, avg_artistic_skill, avg_social_skill);
    std::printf(",\"events\":{\"chronicle\":%zu,\"artifacts_created\":%d,"
                "\"artifacts_active\":%d,\"sabotages\":%d,\"foreman_reports\":%d,"
                "\"space_closures\":%d}",
        sim.chronicle().size(), sim.artifacts_created(), sim.artifacts_active(),
        sim.sabotages_total(), sim.foreman_reports(), sim.space_closures());
    std::printf(",\"timeline\":[");
    for (size_t i = 0; i < timeline.size(); i++) {
        if (i > 0) std::printf(",");
        const auto& s = timeline[i];
        std::printf("{\"tick\":%d,\"alive\":%d,\"external_support\":%.6f,"
                    "\"external_supply_factor\":%.6f,\"quota_demand\":%.6f,"
                    "\"output_shipped\":%.6f,\"output_produced\":%.6f,"
                    "\"raw_food_regenerated\":%.6f,\"raw_material_regenerated\":%.6f,"
                    "\"raw_food_regeneration_requested\":%.6f,"
                    "\"raw_material_regeneration_requested\":%.6f,"
                    "\"source_raw_food\":%.6f,\"source_raw_material\":%.6f,"
                    "\"storage_food\":%.6f}",
            s.tick, s.alive, s.support, s.supply_factor, s.quota_demand,
            s.output_shipped, s.output_produced, s.raw_food_regenerated,
            s.raw_material_regenerated, s.raw_food_regeneration_requested,
            s.raw_material_regeneration_requested, s.source_raw_food,
            s.source_raw_material, s.storage_food);
    }
    std::printf("]");
    std::printf("}\n");
    return 0;
}

static void usage() {
    std::fprintf(stderr,
        "La Vida Misma — CLI\n"
        "\n"
        "Usage: vida_batch <command> [args]\n"
        "\n"
        "Commands:\n"
        "  culture <ticks> [seed]         Cultural behavior vs personality diagnostic\n"
        "  production <ticks> [seed]     Production test (no cultural drives)\n"
        "  calm    <ticks> [seed]          Calm mode (no factory pressure)\n"
        "  run     <ticks> [seed]          Numeric timeline (default)\n"
        "  story   <ticks> [seed]          First-person narrative for all agents\n"
        "  agent   <id> <ticks> [seed]     Full journal of one agent\n"
        "  analysis <ticks> [seed] [policy] Ex-post structured analysis\n"
        "  jsonl   <ticks> [seed]          JSONL dump for external tools\n"
        "  map     [seed]                  Generated layout and inherited-chain diagnostic\n"
        "  replay  <ticks> <seed> <file>  Replay a recorded Director session\n"
        "  metrics <ticks> [seed] [mode] [supply] [block_start] [block_end] [sample_every] [restructure_prob] [policy] [social_learning] [spatial_affinity] [artifact_effects] [natural_mortality] [arrivals] [reproduction]\n"
        "\n"
        "Defaults: ticks=500, seed from config/default.toml\n"
    );
}

int main(int argc, char* argv[]) {
    if (argc < 2) { usage(); return 1; }

    std::string cmd = argv[1];
    if (cmd == "culture")     return cmd_culture(argc, argv);
    if (cmd == "replay")      return cmd_replay(argc, argv);
    if (cmd == "metrics")     return cmd_metrics(argc, argv);
    if (cmd == "production") {
        Config cfg = make_config(argc, argv, 3);
        cfg.director_mode = DirectorMode::PRODUCTION_TEST;
        int ticks = 1000;
        if (argc > 2) ticks = std::atoi(argv[2]);
        if (ticks <= 0) ticks = 1000;
        Simulation sim(cfg);
        std::printf("=== La Vida Misma - PRODUCTION TEST (no culture) ===\n");
        std::printf("Grid: %dx%d  Agents: %d  Ticks: %d  Seed: %d\n",
            cfg.grid_width, cfg.grid_height, cfg.initial_population, ticks, cfg.seed);
        int sample_interval = std::max(1, ticks / 20);
        std::printf("\n%6s %5s %5s %5s %5s %5s %5s | %6s %6s %6s\n",
            "tick", "alive", "GATH", "BUIL", "WORK", "EAT", "REST",
            "food", "c_mat", "output");
        for (int t = 0; t < ticks; t++) {
            sim.advance();
            if (t % sample_interval == 0 || t == ticks - 1) {
                int act_counts[(int)ActionType::COUNT] = {};
                auto view = sim.registry().view<const ActionComponent, const AgentComponent>();
                for (auto e : view) {
                    if (!sim.registry().get<AgentComponent>(e).alive) continue;
                    auto& a = sim.registry().get<ActionComponent>(e);
                    act_counts[(int)a.current]++;
                }
                std::printf("%6d %5d %5d %5d %5d %5d %5d | %6.1f %6.1f %6.1f\n",
                    t, sim.alive_count(),
                    act_counts[(int)ActionType::GATHER],
                    act_counts[(int)ActionType::BUILD],
                    act_counts[(int)ActionType::WORK],
                    act_counts[(int)ActionType::EAT],
                    act_counts[(int)ActionType::REST],
                    sim.total_storage_food(),
                    sim.total_storage_constr_mat(),
                    sim.total_storage_output());
            }
        }
        std::printf("\nDone. alive=%d  quota=%.0f%%  food=%.1f  output=%.1f  c_mat=%.1f (inv=%.1f)\n",
            sim.alive_count(), sim.last_quota_fill() * 100,
            sim.total_storage_food(), sim.total_storage_output(),
            sim.total_storage_constr_mat(),
            sim.total_inventory_constr_mat());
        auto& g = sim.grid();
        int nf=0, no=0, nm=0;
        for (int y = 0; y < g.height(); y++)
            for (int x = 0; x < g.width(); x++)
                if (g.at(x, y) == TileType::Machine && g.data_at(x, y).built) {
                    if (g.data_at(x, y).machine_type == MachineType::Food) nf++;
                    else if (g.data_at(x, y).machine_type == MachineType::Output) no++;
                    else nm++;
                }
        std::printf("  Machines: %d Food, %d Output, %d Mat  Conveyors: %d\n",
            nf, no, nm, sim.built_conveyor_count());
        const auto& cp = sim.colony_production();
        std::printf("  Chain: health=%.2f  need=%d  bottleneck='%s'\n",
            cp.chain_health, (int)cp.primary_need, cp.bottleneck);
        return 0;
    }
    bool force_calm = false;
    if (cmd == "calm") {
        force_calm = true;
        cmd = "run";
        // Shift argv so cmd_run sees "calm" as argv[1] (it uses make_config with force_calm)
    }
    if (force_calm) {
        // Re-run cmd_run with calm mode
        Config cfg = make_config(argc, argv, 3, true);
        int ticks = 1000;
        if (argc > 2) ticks = std::atoi(argv[2]);
        if (ticks <= 0) ticks = 1000;
        Simulation sim(cfg);
        std::printf("=== La Vida Misma - CALM MODE (no pressure) ===\n");
        std::printf("Grid: %dx%d  Agents: %d  Ticks: %d  Seed: %d\n",
            cfg.grid_width, cfg.grid_height, cfg.initial_population, ticks, cfg.seed);
        int sample_interval = std::max(1, ticks / 20);
        std::printf("\n%6s %5s %5s %5s %5s %5s %5s %5s %5s\n",
            "tick", "alive", "GATH", "BUIL", "WORK", "EAT", "REST", "SOC", "CREA");
        for (int t = 0; t < ticks; t++) {
            sim.advance();
            if (t % sample_interval == 0 || t == ticks - 1) {
                int act_counts[(int)ActionType::COUNT] = {};
                auto view = sim.registry().view<const ActionComponent, const AgentComponent>();
                for (auto e : view) {
                    if (!sim.registry().get<AgentComponent>(e).alive) continue;
                    auto& a = sim.registry().get<ActionComponent>(e);
                    act_counts[(int)a.current]++;
                }
                std::printf("%6d %5d %5d %5d %5d %5d %5d %5d %5d\n",
                    t, sim.alive_count(),
                    act_counts[(int)ActionType::GATHER],
                    act_counts[(int)ActionType::BUILD],
                    act_counts[(int)ActionType::WORK],
                    act_counts[(int)ActionType::EAT],
                    act_counts[(int)ActionType::REST],
                    act_counts[(int)ActionType::SOCIALIZE],
                    act_counts[(int)ActionType::CREATE]);
            }
        }
        std::printf("\nDone. alive=%d  artifacts=%d  communities=%d  food=%.1f\n",
            sim.alive_count(), sim.artifacts_created(), sim.communities_detected(),
            sim.total_storage_food());
        return 0;
    }
    if (cmd == "run")          return cmd_run(argc, argv);
    if (cmd == "story")        return cmd_story(argc, argv);
    if (cmd == "agent")        return cmd_agent(argc, argv);
    if (cmd == "analysis")     return cmd_analysis(argc, argv);
    if (cmd == "jsonl")        return cmd_jsonl(argc, argv);
    if (cmd == "map")          return cmd_map(argc, argv);

    // Legacy: just a number = old run mode
    if (std::isdigit(argv[1][0])) {
        // Rewrite argv to match cmd_run expectations
        std::vector<char*> args;
        args.push_back(argv[0]);
        args.push_back(const_cast<char*>("run"));
        for (int i = 1; i < argc; i++) args.push_back(argv[i]);
        args.push_back(nullptr);
        return cmd_run(args.size() - 1, args.data());
    }

    std::fprintf(stderr, "Unknown command: %s\n", argv[1]);
    usage();
    return 1;
}
