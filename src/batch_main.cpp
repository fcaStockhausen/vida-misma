#include "simulation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char* argv[]) {
    int ticks = 500;
    if (argc > 1) ticks = std::atoi(argv[1]);
    if (ticks <= 0) ticks = 500;

    std::string config_path = "config/default.toml";
    Config cfg = load_config(config_path);

    Simulation sim(cfg);

    std::printf("=== La Vida Misma - Batch Run ===\n");
    std::printf("Grid: %dx%d  Agents: %d  Ticks: %d  Seed: %d\n",
        cfg.grid_width, cfg.grid_height, cfg.initial_population, ticks, cfg.seed);

    // Print map stats
    int food_sources = 0, scrap_piles = 0, machines = 0, storages = 0;
    float total_raw_food = 0.0f, total_raw_mat = 0.0f;
    for (int y = 0; y < sim.grid().height(); y++)
        for (int x = 0; x < sim.grid().width(); x++) {
            auto t = sim.grid().at(x, y);
            const auto& d = sim.grid().data_at(x, y);
            if (t == TileType::FoodSource) {
                food_sources++;
                total_raw_food += d.resource_amount;
            }
            if (t == TileType::ScrapPile) {
                scrap_piles++;
                total_raw_mat += d.resource_amount;
            }
            if (t == TileType::Machine) machines++;
            if (t == TileType::Storage) storages++;
        }
    std::printf("Map: %d food_src (%.1f raw_food) %d scrap (%.1f mat) %d machines %d storages\n\n",
        food_sources, total_raw_food, scrap_piles, total_raw_mat, machines, storages);

    // Print initial state
    std::printf("--- INITIAL STATE ---\n");
    auto agents = sim.alive_agents();
    for (auto e : agents) {
        auto& agent = sim.registry().get<AgentComponent>(e);
        auto& pers = sim.registry().get<PersonalityComponent>(e);
        std::printf("Agent[%2d] comp=%.2f lazy=%.2f art=%.2f greg=%.2f res=%.2f cur=%.2f\n",
            agent.id, pers.compliance, pers.laziness, pers.artistry,
            pers.gregariousness, pers.resilience, pers.curiosity);
    }

    // Run simulation
    for (int t = 0; t < ticks; t++) {
        sim.advance();
    }

    // Print final state
    agents = sim.alive_agents();
    std::printf("\n--- AFTER %d TICKS (alive: %d) ---\n", ticks, (int)agents.size());
    std::printf("\n");

    // Count action distribution and deaths
    int action_counts[8] = {};
    int deaths_by[3] = {};  // starvation, exhaustion, breakdown

    // Count dead
    auto all_view = sim.registry().view<const AgentComponent>();
    for (auto e : all_view) {
        auto& agent = sim.registry().get<AgentComponent>(e);
        if (!agent.alive) {
            if (agent.cause_of_death == "starvation") deaths_by[0]++;
            else if (agent.cause_of_death == "exhaustion") deaths_by[1]++;
            else if (agent.cause_of_death == "breakdown") deaths_by[2]++;
        }
    }

    for (auto e : agents) {
        auto& agent  = sim.registry().get<AgentComponent>(e);
        auto& needs  = sim.registry().get<NeedsComponent>(e);
        auto& pers   = sim.registry().get<PersonalityComponent>(e);
        auto& action = sim.registry().get<ActionComponent>(e);
        auto& stress = sim.registry().get<StressComponent>(e);
        auto& inv    = sim.registry().get<InventoryComponent>(e);

        action_counts[(int)action.current]++;

        const char* action_names[] = {
            "GATHER", "BUILD", "WORK", "EAT", "REST", "SOCIAL", "CREATE", "EXPLORE"
        };

        std::printf("Agent[%2d] action=%-8s stress=%.2f | H=%.2f R=%.2f S=%.2f E=%.2f P=%.2f | inv[rf=%.1f rm=%.1f f=%.1f] | comp=%.2f art=%.2f lazy=%.2f\n",
            agent.id,
            action.current == ActionType::IDLE ? "IDLE" : action_names[(int)action.current],
            stress.value,
            needs.hunger, needs.rest, needs.social, needs.expression, needs.purpose,
            inv.raw_food, inv.raw_material, inv.food,
            pers.compliance, pers.artistry, pers.laziness);
    }

    // Action distribution
    const char* action_names[] = {
        "GATHER", "BUILD", "WORK", "EAT", "REST", "SOCIAL", "CREATE", "EXPLORE"
    };
    std::printf("\n--- ACTION DISTRIBUTION ---\n");
    for (int i = 0; i < 8; i++) {
        if (action_counts[i] > 0) {
            std::printf("  %-8s: %2d ", action_names[i], action_counts[i]);
            for (int j = 0; j < action_counts[i]; j++) std::printf("#");
            std::printf("\n");
        }
    }

    // Deaths
    std::printf("\n--- DEATHS (%d total) ---\n",
        deaths_by[0] + deaths_by[1] + deaths_by[2]);
    if (deaths_by[0]) std::printf("  starvation: %d\n", deaths_by[0]);
    if (deaths_by[1]) std::printf("  exhaustion: %d\n", deaths_by[1]);
    if (deaths_by[2]) std::printf("  breakdown:  %d\n", deaths_by[2]);

    // Production stats
    std::printf("\n--- PRODUCTION ---\n");
    std::printf("  Machines built:   %d / %d\n", sim.total_machines_built(), machines);
    std::printf("  Food produced:    %.1f\n", sim.total_food_produced());
    std::printf("  Raw gathered:     %.1f\n", sim.total_raw_gathered());
    std::printf("  Storage food:     %.1f\n", sim.total_storage_food());

    // Map resource state
    float remaining_food = 0.0f, remaining_mat = 0.0f;
    float storage_food = 0.0f, storage_raw = 0.0f, storage_mat = 0.0f;
    for (int y = 0; y < sim.grid().height(); y++)
        for (int x = 0; x < sim.grid().width(); x++) {
            auto t = sim.grid().at(x, y);
            const auto& d = sim.grid().data_at(x, y);
            if (t == TileType::FoodSource) remaining_food += d.resource_amount;
            if (t == TileType::ScrapPile) remaining_mat += d.resource_amount;
            if (t == TileType::Storage) {
                storage_food += d.stored_food;
                storage_raw += d.stored_raw_food;
                storage_mat += d.stored_raw_material;
            }
        }
    std::printf("  Wild food left:   %.1f / %.1f\n", remaining_food, total_raw_food);
    std::printf("  Scrap left:       %.1f / %.1f\n", remaining_mat, total_raw_mat);
    std::printf("  In storage:       food=%.1f raw_food=%.1f mat=%.1f\n",
        storage_food, storage_raw, storage_mat);

    std::printf("\nDone.\n");
    return 0;
}
