#include "simulation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

// ============================================================
// Helpers
// ============================================================

static Config make_config(int argc, char* argv[], int arg_base, bool force_calm = false) {
    std::string config_path = "config/default.toml";
    FILE* test = std::fopen(config_path.c_str(), "r");
    if (!test) {
        config_path = "../config/default.toml";
        test = std::fopen(config_path.c_str(), "r");
    }
    if (test) std::fclose(test);
    Config cfg = load_config(config_path);
    if (force_calm) cfg.director_mode = DirectorMode::CALM;
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
                auto& p = sim.registry().get<PositionComponent>(e);
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
    if (ticks > 0) {
        float total_quota = 0.0f;
        float qpt = cfg.quota_per_tick;
        for (int t = 1; t <= ticks; t++)
            total_quota += qpt * (1.0f + (float)t * cfg.quota_growth_rate);
        if (total_quota > 0.001f)
            avg_quota = sim.total_food_shipped() / total_quota;
    }
    std::printf("\nDone. alive=%d  built=%d  conv=%d  food=%.1f  quota=%.0f%% (avg=%.0f%%)\n",
        sim.alive_count(), sim.total_machines_built(),
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
    std::printf("  Machines built: %d\n", sim.total_machines_built());

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

    // Adversarial factory audit — distinguishes best-response targeting from
    // uniform-random noise (doc/adversarial_utility_agents.md heuristic #5).
    int total_restr = sim.total_restructures();
    int fac_restr = sim.restructures_targeting_factions();
    std::printf("\n--- Adversarial Audit ---\n");
    std::printf("  Restructures total:       %d\n", total_restr);
    if (total_restr > 0) {
        std::printf("  Restructures vs factions: %d  (%.0f%% of total)\n",
            fac_restr, 100.0f * (float)fac_restr / (float)total_restr);
    } else {
        std::printf("  Restructures vs factions: %d  (n/a)\n", fac_restr);
    }
    std::printf("  Foreman reports:          %d\n", sim.foreman_reports());

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
                if (grid.at(nx, ny) == TileType::Storage) adj_storage++;
            }
        std::printf("  Exit at (%d,%d): %d storages within r=3\n", ex, ey, adj_storage);
    }

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
        if (sim.alive_count() == 0) break;

        auto view = sim.registry().view<const ActionComponent, const AgentComponent>();
        for (auto e : view) {
            auto& ag = sim.registry().get<AgentComponent>(e);
            if (!ag.alive) continue;
            if (ag.id < 0 || ag.id >= (int)tracks.size()) continue;
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
        "  analysis <ticks> [seed]         Ex-post structured analysis\n"
        "  jsonl   <ticks> [seed]          JSONL dump for external tools\n"
        "\n"
        "Defaults: ticks=500, seed from config/default.toml\n"
    );
}

int main(int argc, char* argv[]) {
    if (argc < 2) { usage(); return 1; }

    std::string cmd = argv[1];
    if (cmd == "culture")     return cmd_culture(argc, argv);
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
            if (sim.alive_count() == 0) break;
            if (t % sample_interval == 0 || t == ticks - 1) {
                int act_counts[12] = {};
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
            if (sim.alive_count() == 0) {
                std::printf("  ...everyone died at tick %d\n", t);
                break;
            }
            if (t % sample_interval == 0 || t == ticks - 1) {
                int act_counts[12] = {};
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
        std::printf("\nDone. alive=%d  artifacts=%d  factions=%d  food=%.1f\n",
            sim.alive_count(), sim.artifacts_created(), sim.factions_formed(),
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
