#include "simulation.h"
#include "renderer.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <string>

int main(int argc, char* argv[]) {
    std::string config_path = "config/default.toml";
    Config cfg = load_config(config_path);

    Simulation sim(cfg);

    std::printf("La Vida Misma — Community Simulation Engine\n");
    std::printf("Grid: %dx%d | Agents: %d\n", cfg.grid_width, cfg.grid_height, cfg.initial_population);
    std::printf("\nPress ENTER to open terminal UI...\n");

    // Wait for Enter in cooked mode (before raw terminal)
    char buf[8];
    if (!fgets(buf, sizeof(buf), stdin)) return 0;

    Renderer renderer(sim);

    RawTerminal term;
    term.enter();

    bool running = false;
    bool paused = true;
    int speed_ms = 200;  // ms between ticks when running

    renderer.draw(paused, speed_ms);

    while (true) {
        if (term.kbhit()) {
            int ch = term.getch();
            if (ch < 0) continue;

            switch (ch) {
                case 'q':
                    term.leave();
                    std::printf("\nGoodbye.\n");
                    return 0;
                case 'n':
                    sim.advance();
                    renderer.draw(paused, speed_ms);
                    break;
                case 'r':
                    running = true;
                    paused = false;
                    break;
                case 'p':
                    running = false;
                    paused = true;
                    renderer.draw(paused, speed_ms);
                    break;
                case '+':
                    speed_ms = std::max(20, speed_ms - 50);
                    renderer.draw(paused, speed_ms);
                    break;
                case '-':
                    speed_ms = std::min(2000, speed_ms + 50);
                    renderer.draw(paused, speed_ms);
                    break;
                case '\t':
                    renderer.next_agent();
                    renderer.draw(paused, speed_ms);
                    break;
            }
        }

        if (running) {
            sim.advance();
            renderer.draw(paused, speed_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(speed_ms));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}
