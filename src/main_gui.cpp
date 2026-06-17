#include "simulation.h"
#include "graphical_view.h"
#include <cstdio>
#include <thread>
#include <atomic>

int main(int /*argc*/, char* /*argv*/[]) {
    std::string config_path = "config/default.toml";
    FILE* test = std::fopen(config_path.c_str(), "r");
    if (!test) {
        config_path = "../config/default.toml";
        test = std::fopen(config_path.c_str(), "r");
    }
    if (test) std::fclose(test);
    Config cfg = load_config(config_path);

    Simulation sim(cfg);

    std::printf("La Vida Misma — 2.5D GUI\n");
    std::printf("Grid: %dx%d | Agents: %d\n", cfg.grid_width, cfg.grid_height, cfg.initial_population);
    std::printf("Controls: SPACE=pause  N=step  </>=speed  F=follow  click=select\n");

    std::atomic<bool> running{true};
    std::atomic<bool> paused{false};
    std::atomic<int>  sim_speed_ms{300};  // default slow for observation

    GraphicalView gui(sim, paused);

    std::thread sim_thread([&]() {
        while (running) {
            if (!paused) {
                sim.advance();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(sim_speed_ms.load()));
        }
    });

    // GUI sets the pace
    gui.set_speed_callback([&](int ms) { sim_speed_ms.store(ms); });
    gui.run();

    running = false;
    sim_thread.join();

    std::printf("Goodbye.\n");
    return 0;
}
