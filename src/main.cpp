#include "simulation.h"
#include "tui.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    std::string config_path = "config/default.toml";
    Config cfg = load_config(config_path);

    Simulation sim(cfg);

    std::printf("La Vida Misma\n");
    std::printf("Grid: %dx%d | Agents: %d\n", cfg.grid_width, cfg.grid_height, cfg.initial_population);
    std::printf("Opening terminal UI...\n");

    TUI tui(sim);
    tui.run();

    std::printf("Goodbye.\n");
    return 0;
}
