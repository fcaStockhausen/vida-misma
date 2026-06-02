#include "simulation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ============================================================
// Helpers
// ============================================================

static Config make_config(int argc, char* argv[], int arg_base) {
    std::string config_path = "config/default.toml";
    Config cfg = load_config(config_path);
    if (argc > arg_base + 0) cfg.seed = std::atoi(argv[arg_base + 0]);
    return cfg;
}

static Simulation run_sim(Config& cfg, int ticks) {
    Simulation sim(cfg);
    for (int t = 0; t < ticks; t++) sim.advance();
    return sim;
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
        if (sim.alive_count() == 0) {
            std::printf("\nEXTINCTION at tick %d.\n", t + 1);
            break;
        }
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

    std::printf("\nDone. alive=%d  built=%d  food=%.1f  quota=%.0f%%\n",
        sim.alive_count(), sim.total_machines_built(),
        sim.total_storage_food(), sim.last_quota_fill() * 100);
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
        if (sim.alive_count() == 0) break;
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
        std::printf("%s", sim.chronicle().agent_journal(id, 3, 12).c_str());
    }
    for (int id : dead_ids) {
        const char* arch = find_archetype_name(sim, id);
        std::printf("\n--- Agent %d (%s) DEAD ---\n", id, arch);
        std::printf("  %s\n", sim.chronicle().agent_arc(id, arch).c_str());
        std::printf("%s", sim.chronicle().agent_journal(id, 2, 8).c_str());
    }

    // Death report
    std::printf("\n%s", sim.chronicle().death_report().c_str());

    // Production summary
    std::printf("\n== EPILOGUE ==\n");
    std::printf("  Factory health: %.0f%%\n", sim.factory_health() * 100);
    std::printf("  Machines built: %d\n", sim.total_machines_built());
    std::printf("  Food shipped: %.1f\n", sim.total_food_shipped());
    std::printf("  Sabotages: %d  Redemptions: %d  Suicides: %d\n",
        sim.sabotages_total(), sim.redemptions_total(), sim.suicides_total());
    std::printf("  Factions: %d  Artifacts: %d\n",
        sim.factions_formed(), sim.artifacts_created());

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
        if (sim.alive_count() == 0) break;
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
    Simulation sim(cfg);
    for (int t = 0; t < ticks; t++) {
        sim.advance();
        if (sim.alive_count() == 0) break;
    }

    std::printf("# La Vida Misma — Ex-Post Analysis\n");
    std::printf("# Ticks: %d  Seed: %d  Events: %zu\n\n",
        ticks, cfg.seed, sim.chronicle().size());

    // Event distribution
    std::printf("%s", sim.chronicle().event_distribution().c_str());

    // Faction arcs
    std::printf("\n%s", sim.chronicle().faction_arcs().c_str());

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
    std::printf("  Machines built:    %d\n", sim.total_machines_built());
    std::printf("  Food shipped:      %.1f\n", sim.total_food_shipped());
    std::printf("  Sabotages:         %d\n", sim.sabotages_total());
    std::printf("  Redemptions:       %d\n", sim.redemptions_total());
    std::printf("  Suicides:          %d\n", sim.suicides_total());
    std::printf("  Factions:          %d\n", sim.factions_formed());
    std::printf("  Artifacts:         %d\n", sim.artifacts_created());

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
        if (sim.alive_count() == 0) break;
    }

    // Header comment with metadata
    std::printf("# La Vida Misma JSONL | ticks=%d seed=%d grid=%dx%d agents=%d events=%zu\n",
        ticks, cfg.seed, cfg.grid_width, cfg.grid_height,
        cfg.initial_population, sim.chronicle().size());

    // Dump all events as JSONL
    std::printf("%s", sim.chronicle().to_jsonl().c_str());

    return 0;
}

static int cmd_map(int argc, char* argv[]) {
    Config cfg = make_config(argc, argv, 2);
    Simulation sim(cfg);
    auto& grid = sim.grid();

    std::printf("=== Map Diagnostic (seed=%d) ===\n\n", cfg.seed);

    // Count walls
    int inner_walls = 0, boundary_walls = 0;
    for (int y = 0; y < grid.height(); y++)
        for (int x = 0; x < grid.width(); x++) {
            if (grid.at(x, y) == TileType::Wall) {
                if (x == 0 || x == grid.width()-1 || y == 0 || y == grid.height()-1)
                    boundary_walls++;
                else
                    inner_walls++;
            }
        }
    std::printf("  Walls: %d boundary, %d inner\n\n", boundary_walls, inner_walls);

    // All special tiles
    for (int y = 0; y < grid.height(); y++)
        for (int x = 0; x < grid.width(); x++) {
            auto t = grid.at(x, y);
            if (t == TileType::Machine) {
                auto& d = grid.data_at(x, y);
                const char* mt = d.machine_type == MachineType::Food ? "FOOD" :
                                 d.machine_type == MachineType::Materials ? "MAT" : "OUT";
                std::printf("  MACHINE %-4s at (%2d,%2d) built=%d\n", mt, x, y, d.built);
            } else if (t == TileType::Storage) {
                auto& d = grid.data_at(x, y);
                std::printf("  STORAGE      at (%2d,%2d) cap=%.0f\n", x, y, d.storage_capacity);
            } else if (t == TileType::Exit) {
                std::printf("  EXIT         at (%2d,%2d)\n", x, y);
            } else if (t == TileType::Entrance) {
                std::printf("  ENTRANCE     at (%2d,%2d)\n", x, y);
            } else if (t == TileType::Conveyor) {
                auto& d = grid.data_at(x, y);
                const char* dir = d.conveyor_dir == ConveyorDir::N ? "N" :
                                  d.conveyor_dir == ConveyorDir::S ? "S" :
                                  d.conveyor_dir == ConveyorDir::E ? "E" : "W";
                std::printf("  CONVEYOR %-2s  at (%2d,%2d) built=%d\n", dir, x, y, d.built);
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
                if (grid.at(nx, ny) == TileType::Storage) adj_storage++;
            }
        std::printf("  Exit at (%d,%d): %d storages within r=3\n", ex, ey, adj_storage);
    }

    return 0;
}

static void usage() {
    std::fprintf(stderr,
        "La Vida Misma — CLI\n"
        "\n"
        "Usage: vida_batch <command> [args]\n"
        "\n"
        "Commands:\n"
        "  run     <ticks> [seed]          Numeric timeline (default)\n"
        "  story   <ticks> [seed]          First-person narrative for all agents\n"
        "  agent   <id> <ticks> [seed]     Full journal of one agent\n"
        "  analysis <ticks> [seed]         Ex-post structured analysis\n"
        "  jsonl   <ticks> [seed]          JSONL dump for external tools\n"
        "\n"
        "Defaults: ticks=500, seed from config/default.toml\n"
    );
}

int main(int argc, char* argv[]) {
    if (argc < 2) { usage(); return 1; }

    std::string cmd = argv[1];
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
