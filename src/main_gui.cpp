#include "simulation.h"
#include "graphical_view.h"
#include <cstdio>
#include <thread>
#include <atomic>

int main(int /*argc*/, char* /*argv*/[]) {
    std::string config_path = "config/default.toml";
    Config cfg = load_config(config_path);

    Simulation sim(cfg);

    std::printf("La Vida Misma — 2.5D GUI\n");
    std::printf("Grid: %dx%d | Agents: %d\n", cfg.grid_width, cfg.grid_height, cfg.initial_population);

    std::atomic<bool> running{true};
    std::atomic<bool> paused{false};

    std::thread sim_thread([&]() {
        while (running) {
            if (!paused) {
                sim.advance();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    GraphicalView gui(sim, paused);
    gui.run();

    running = false;
    sim_thread.join();

    std::printf("Goodbye.\n");
    return 0;
}
