#include "simulation.h"
#include "graphical_view.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
    int seed_override = 0;
    bool has_seed_override = false;
    bool debug_view = false;
    std::string record_path;
    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];
        if (argument == "--seed" && i + 1 < argc) {
            char* end = nullptr;
            long parsed = std::strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || static_cast<long>(static_cast<int>(parsed)) != parsed) {
                std::fprintf(stderr, "Invalid --seed value\n");
                return 1;
            }
            seed_override = static_cast<int>(parsed);
            has_seed_override = true;
        } else if (argument == "--record" && i + 1 < argc) {
            record_path = argv[++i];
        } else if (argument == "--debug") {
            debug_view = true;
        } else {
            std::fprintf(stderr, "Usage: vida_gui [--seed N] [--record FILE] [--debug]\n");
            return 1;
        }
    }

    std::string config_path = "config/default.toml";
    FILE* test = std::fopen(config_path.c_str(), "r");
    if (!test) {
        config_path = "../config/default.toml";
        test = std::fopen(config_path.c_str(), "r");
    }
    if (test) std::fclose(test);
    Config cfg = load_config(config_path);
    if (has_seed_override) cfg.seed = seed_override;
    uint64_t config_fingerprint = 0;
    if (!record_path.empty()) {
        std::string error;
        if (!fingerprint_config_source(config_path, config_fingerprint, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
    }

    Simulation sim(cfg);

    std::printf("La Vida Misma — 2.5D GUI\n");
    std::printf("Grid: %dx%d | Agents: %d\n", cfg.grid_width, cfg.grid_height, cfg.initial_population);
    std::printf("Controls: SPACE=pause  N=step  </>=speed  F=follow  click=select\n");

    GraphicalView gui(sim, debug_view);
    gui.run();

    if (!record_path.empty()) {
        std::string error;
        if (!write_director_log(record_path, cfg.seed, cfg.director_mode,
                                config_fingerprint,
                                sim.director_log(), error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::printf("Recorded %zu intervention(s) in %s\n",
            sim.director_log().size(), record_path.c_str());
    }

    std::printf("Goodbye.\n");
    return 0;
}
