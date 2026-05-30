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
    if (argc > 2) cfg.seed = std::atoi(argv[2]);

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
        std::printf("Agent[%2d] %-9s comp=%.2f lazy=%.2f art=%.2f greg=%.2f res=%.2f cur=%.2f\n",
            agent.id, archetype_name(pers.archetype),
            pers.compliance, pers.laziness, pers.artistry,
            pers.gregariousness, pers.resilience, pers.curiosity);
    }

    // Run simulation with periodic sampling
    int sample_interval = std::max(1, ticks / 20);
    std::printf("\n--- TIMELINE (every %d ticks) ---\n", sample_interval);
    std::printf("%6s %5s %5s %5s %5s %5s %5s %5s %5s | %5s %5s %5s | %4s %4s\n",
        "tick", "alive", "GATH", "BUIL", "WORK", "EAT", "REST", "SOC", "OTHR",
        "rawF", "rawM", "food", "mach", "raw");
    for (int t = 0; t < ticks; t++) {
        sim.advance();
        if ((t + 1) % sample_interval == 0 || t == 0) {
            int alive = sim.alive_count();
            int act_counts[12] = {};
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

    // Print final state
    agents = sim.alive_agents();
    std::printf("\n--- AFTER %d TICKS (alive: %d) ---\n", ticks, (int)agents.size());
    std::printf("\n");

    // Count action distribution and deaths
    int action_counts[13] = {};
    int deaths_by[5] = {};  // starvation, exhaustion, breakdown, collapse, suicide

    // Count dead
    auto all_view = sim.registry().view<const AgentComponent>();
    for (auto e : all_view) {
        auto& agent = sim.registry().get<AgentComponent>(e);
        if (!agent.alive) {
            if (agent.cause_of_death == "starvation") deaths_by[0]++;
            else if (agent.cause_of_death == "exhaustion") deaths_by[1]++;
            else if (agent.cause_of_death == "breakdown") deaths_by[2]++;
            else if (agent.cause_of_death == "collapse") deaths_by[3]++;
            else if (agent.cause_of_death == "suicide") deaths_by[4]++;
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
            "GATHER", "BUILD", "WORK", "EAT", "REST", "SOCIAL", "CREATE", "EXPLORE", "GETFOOD", "MAINT", "DSMNTL", "SABOT", "IDLE"
        };

        std::printf("Agent[%2d] action=%-8s stress=%.2f tr=%.2f %s | H=%.2f R=%.2f S=%.2f E=%.2f P=%.2f | inv[rf=%.1f rm=%.1f f=%.1f] | comp=%.2f art=%.2f lazy=%.2f\n",
            agent.id,
            action.current == ActionType::IDLE ? "IDLE" : action_names[(int)action.current],
            stress.value, stress.trauma, stress_state_name(stress.state),
            needs.hunger, needs.rest, needs.social, needs.expression, needs.purpose,
            inv.raw_food, inv.raw_material, inv.food,
            pers.compliance, pers.artistry, pers.laziness);
    }

    // Action distribution (counts already accumulated in action_counts above)
    const char* action_names[] = {
        "GATHER", "BUILD", "WORK", "EAT", "REST", "SOCIAL", "CREATE", "EXPLORE",
        "GETFOOD", "MAINT", "DSMNTL", "IDLE"
    };
    std::printf("\n--- ACTION DISTRIBUTION ---\n");
    for (int i = 0; i < 12; i++) {
        if (action_counts[i] > 0) {
            std::printf("  %-8s: %2d ", action_names[i], action_counts[i]);
            for (int j = 0; j < action_counts[i]; j++) std::printf("#");
            std::printf("\n");
        }
    }

    // Deaths (framed as natural turnover — this is a stressful factory environment)
    int total_dead = deaths_by[0] + deaths_by[1] + deaths_by[2] + deaths_by[3];
    if (total_dead > 0) {
        std::printf("\nTURNOVER:\n");
        if (deaths_by[0]) std::printf("  burnout (hunger):   %d\n", deaths_by[0]);
        if (deaths_by[1]) std::printf("  collapse (fatigue): %d\n", deaths_by[1]);
        if (deaths_by[2]) std::printf("  breakdown (stress): %d\n", deaths_by[2]);
        if (deaths_by[3]) std::printf("  factory collapse:   %d\n", deaths_by[3]);
        if (deaths_by[4]) std::printf("  suicide:            %d\n", deaths_by[4]);
    } else {
        std::printf("\nTURNOVER: (none — stable shift)\n");
    }

    // Production stats
    std::printf("\n--- PRODUCTION ---\n");
    std::printf("  Factory health:   %.2f\n", sim.factory_health());
    std::printf("  Machines built:   %d / %d\n", sim.total_machines_built(), machines);
    std::printf("  Food produced:    %.1f\n", sim.total_food_produced());
    std::printf("  Raw gathered:     %.1f\n", sim.total_raw_gathered());
    std::printf("  Storage food:     %.1f\n", sim.total_storage_food());
    std::printf("  Conveyors:        %d built / %d total\n",
        sim.grid().built_conveyor_count(), sim.grid().conveyor_count());

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

    // Conveyor stats
    int conv_built = 0, conv_broken = 0;
    float conv_contents = 0.0f, avg_condition = 0.0f;
    for (int y = 0; y < sim.grid().height(); y++)
        for (int x = 0; x < sim.grid().width(); x++) {
            if (sim.grid().at(x, y) != TileType::Conveyor) continue;
            const auto& d = sim.grid().data_at(x, y);
            if (d.built) { conv_built++; avg_condition += d.conveyor_condition; }
            if (d.built && d.conveyor_condition < 0.2f) conv_broken++;
            conv_contents += d.conveyor_contents;
        }
    if (conv_built > 0) avg_condition /= conv_built;
    std::printf("  Conv condition:   %.2f avg, %d broken\n", avg_condition, conv_broken);
    std::printf("  Conv contents:    %.1f on belts\n", conv_contents);
    std::printf("  In storage:       food=%.1f raw_food=%.1f mat=%.1f\n",
        storage_food, storage_raw, storage_mat);

    // Narrative stats
    std::printf("\n--- NARRATIVE ---\n");
    std::printf("  Quota:            %.3f -> %.3f\n", cfg.quota_per_tick, sim.current_quota());
    std::printf("  Restructures:     %d\n", sim.total_restructures());
    std::printf("  Artifacts:        %d created, %d active\n",
        sim.artifacts_created(), sim.artifacts_active());
    std::printf("  Hidden spaces:    %d found, %d sealed\n",
        sim.hidden_spaces_found(), sim.hidden_spaces_sealed());
    std::printf("  Factions:         %d\n", sim.factions_formed());
    std::printf("  Sabotages:        %d\n", sim.sabotages_total());
    std::printf("  Redemptions:      %d\n", sim.redemptions_total());
    std::printf("  Suicides:         %d\n", sim.suicides_total());

    // Noncompliance, meaning, stress, trauma stats
    float avg_noncomp = 0.0f, avg_meaning = 0.0f, avg_stress = 0.0f, avg_trauma = 0.0f;
    int meaning_crisis = 0;
    int stress_states[5] = {0, 0, 0, 0, 0}; // NORMAL, DISSOCIATED, HOSTILE_EUPHORIA, BROKEN, REDEEMED
    auto all_agents = sim.registry().view<const AgentComponent, const NeedsComponent, const StressComponent>();
    int n_total = 0;
    for (auto e : all_agents) {
        auto& ag = sim.registry().get<AgentComponent>(e);
        auto& nd = sim.registry().get<NeedsComponent>(e);
        auto& st = sim.registry().get<StressComponent>(e);
        avg_noncomp += ag.noncompliance;
        avg_meaning += nd.meaning;
        avg_stress += st.value;
        avg_trauma += st.trauma;
        if (nd.meaning > 0.7f) meaning_crisis++;
        stress_states[static_cast<int>(st.state)]++;
        n_total++;
    }
    if (n_total > 0) {
        avg_noncomp /= n_total;
        avg_meaning /= n_total;
        avg_stress /= n_total;
        avg_trauma /= n_total;
    }
    std::printf("  Avg noncompliance: %.2f\n", avg_noncomp);
    std::printf("  Avg meaning need:  %.2f\n", avg_meaning);
    std::printf("  Meaning crisis:    %d / %d agents\n", meaning_crisis, n_total);
    std::printf("  Avg stress:        %.2f\n", avg_stress);
    std::printf("  Avg trauma:        %.2f\n", avg_trauma);
    std::printf("  Stress states:     N=%d D=%d E=%d B=%d R=%d\n",
        stress_states[0], stress_states[1], stress_states[2],
        stress_states[3], stress_states[4]);

    // Hidden space count on map
    int hidden_count = 0;
    for (int y = 0; y < sim.grid().height(); y++)
        for (int x = 0; x < sim.grid().width(); x++)
            if (sim.grid().at(x, y) == TileType::HiddenSpace) hidden_count++;
    std::printf("  Hidden on map:    %d\n", hidden_count);

    std::printf("\nDone.\n");
    return 0;
}
