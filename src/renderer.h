#pragma once

#include "simulation.h"
#include <cstdio>
#include <cstdlib>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <string>
#include <cmath>

// ============================================================
// Raw terminal I/O (POSIX)
// ============================================================

struct RawTerminal {
    termios orig_;
    bool active_ = false;

    void enter() {
        tcgetattr(STDIN_FILENO, &orig_);
        termios raw = orig_;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        active_ = true;
        // Hide cursor
        std::printf("\033[?25l");
    }

    void leave() {
        if (active_) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
            std::printf("\033[?25h");
            active_ = false;
        }
    }

    ~RawTerminal() { leave(); }

    bool kbhit() {
        timeval tv = {0, 0};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
    }

    int getch() {
        char c = 0;
        if (read(STDIN_FILENO, &c, 1) <= 0) return -1;
        return c;
    }
};

// ============================================================
// ANSI helpers
// ============================================================

static inline void ansi_fg(int r, int g, int b) {
    std::printf("\033[38;2;%d;%d;%dm", r, g, b);
}
static inline void ansi_bg(int r, int g, int b) {
    std::printf("\033[48;2;%d;%d;%dm", r, g, b);
}
static inline void ansi_reset() { std::printf("\033[0m"); }
static inline void ansi_bold() { std::printf("\033[1m"); }
static inline void ansi_dim()  { std::printf("\033[2m"); }

// ============================================================
// Renderer
// ============================================================

class Renderer {
public:
    Renderer(Simulation& sim) : sim_(sim), selected_idx_(0), first_draw_(true) {}

    void draw(bool paused, int speed) {
        if (first_draw_) {
            // Full clear on first frame to wipe the "Press ENTER" prompt
            std::printf("\033[2J\033[H");
            first_draw_ = false;
        } else {
            std::printf("\033[H");  // move cursor to top-left
        }

        draw_header(paused, speed);
        draw_grid();
        draw_agent_panel();

        std::printf("\033[J");  // clear from cursor to end of screen
        std::fflush(stdout);
    }

    void next_agent() {
        auto agents = sim_.alive_agents();
        if (agents.empty()) return;
        selected_idx_ = (selected_idx_ + 1) % agents.size();
    }

private:
    Simulation& sim_;
    size_t selected_idx_;
    bool first_draw_;

    void draw_header(bool paused, int speed) {
        ansi_bold();
        ansi_fg(200, 220, 255);
        std::printf("  LA VIDA MISMA");
        ansi_reset();
        std::printf("  tick:%d  alive:%d/%d  built:%d  storage:%.1f  ",
            sim_.tick(),
            sim_.alive_count(),
            sim_.config().initial_population,
            sim_.total_machines_built(),
            sim_.total_storage_food());
        ansi_fg(180, 180, 180);
        std::printf("produced:%.1f", sim_.total_food_produced());
        ansi_reset();
        if (paused) {
            ansi_fg(255, 200, 100);
            std::printf("  [PAUSED]");
            ansi_reset();
        }
        std::printf("\n");

        // Controls hint
        ansi_dim();
        std::printf("  n:tick r:run p:pause +/-:speed Tab:agent q:quit\n");
        ansi_reset();
        std::printf("\n");
    }

    void draw_grid() {
        const Grid& grid = sim_.grid();

        for (int y = 0; y < grid.height(); y++) {
            std::printf("  ");
            for (int x = 0; x < grid.width(); x++) {
                TileType t = grid.at(x, y);

                // Check if an agent is here
                bool agent_here = false;
                bool selected_here = false;
                auto agents = sim_.alive_agents();
                for (size_t i = 0; i < agents.size(); i++) {
                    auto& pos = sim_.registry().get<PositionComponent>(agents[i]);
                    if (pos.x == x && pos.y == y) {
                        agent_here = true;
                        if (i == selected_idx_) selected_here = true;
                        break;
                    }
                }

                if (selected_here) {
                    ansi_bg(80, 80, 120);
                    ansi_fg(255, 255, 100);
                    std::printf("@");
                    ansi_reset();
                    continue;
                }

                if (agent_here) {
                    ansi_fg(100, 200, 255);
                    std::printf("o");
                    ansi_reset();
                    continue;
                }

                // Tile rendering with resource info
                switch (t) {
                    case TileType::Wall:
                        ansi_fg(60, 60, 80);
                        std::printf("#");
                        break;
                    case TileType::Floor:
                        std::printf(".");
                        break;
                    case TileType::Machine: {
                        const auto& d = grid.data_at(x, y);
                        if (d.built) {
                            ansi_fg(100, 255, 100);
                            std::printf("M");
                        } else {
                            float pct = d.build_cost > 0 ? d.build_progress / d.build_cost : 0;
                            if (pct > 0.5f) {
                                ansi_fg(255, 200, 80);
                            } else if (pct > 0.0f) {
                                ansi_fg(255, 130, 80);
                            } else {
                                ansi_fg(120, 120, 120);
                            }
                            std::printf("m");
                        }
                        ansi_reset();
                        break;
                    }
                    case TileType::Storage: {
                        const auto& d = grid.data_at(x, y);
                        float total = d.stored_food + d.stored_raw_food + d.stored_raw_material;
                        if (total > 5.0f) ansi_fg(255, 255, 100);
                        else if (total > 0.5f) ansi_fg(200, 200, 100);
                        else ansi_fg(120, 120, 100);
                        std::printf("S");
                        ansi_reset();
                        break;
                    }
                    case TileType::Entrance:
                        ansi_fg(100, 200, 100);
                        std::printf("<");
                        ansi_reset();
                        break;
                    case TileType::Exit:
                        ansi_fg(100, 200, 100);
                        std::printf(">");
                        ansi_reset();
                        break;
                    case TileType::OpenSpace:
                        ansi_fg(40, 70, 50);
                        std::printf("_");
                        break;
                    case TileType::FoodSource: {
                        const auto& d = grid.data_at(x, y);
                        if (d.resource_amount > 1.5f) ansi_fg(80, 200, 80);
                        else if (d.resource_amount > 0.3f) ansi_fg(180, 200, 80);
                        else ansi_fg(100, 100, 60);
                        std::printf("f");
                        ansi_reset();
                        break;
                    }
                    case TileType::ScrapPile: {
                        const auto& d = grid.data_at(x, y);
                        if (d.resource_amount > 2.0f) ansi_fg(200, 150, 80);
                        else if (d.resource_amount > 0.3f) ansi_fg(150, 110, 60);
                        else ansi_fg(80, 70, 50);
                        std::printf("s");
                        ansi_reset();
                        break;
                    }
                }
            }
            std::printf("\n");
        }
        std::printf("\n");

        // Legend
        ansi_dim();
        std::printf("  #wall .floor M/machine S:storage f:food s:scrap _:open\n");
        ansi_reset();
    }

    void draw_agent_panel() {
        auto agents = sim_.alive_agents();
        if (agents.empty()) {
            std::printf("  No alive agents.\n");
            return;
        }

        if (selected_idx_ >= agents.size()) selected_idx_ = 0;
        auto e = agents[selected_idx_];

        auto& agent  = sim_.registry().get<AgentComponent>(e);
        auto& needs  = sim_.registry().get<NeedsComponent>(e);
        auto& pers   = sim_.registry().get<PersonalityComponent>(e);
        auto& action = sim_.registry().get<ActionComponent>(e);
        auto& stress = sim_.registry().get<StressComponent>(e);
        auto& inv    = sim_.registry().get<InventoryComponent>(e);
        auto& pos    = sim_.registry().get<PositionComponent>(e);

        ansi_bold();
        std::printf("  Agent[%d] ", agent.id);
        ansi_reset();

        // Action
        const char* action_names[] = {
            "GATHER", "BUILD", "WORK", "EAT", "REST", "SOCIALIZE", "CREATE", "EXPLORE", "IDLE"
        };
        ansi_fg(200, 200, 255);
        std::printf("action=%s ", action_names[(int)action.current]);
        ansi_reset();

        // Position
        std::printf("pos=(%d,%d) ", pos.x, pos.y);
        if (action.target_x >= 0)
            std::printf("target=(%d,%d)%s ", action.target_x, action.target_y,
                action.at_target ? " [AT]" : " [MOVING]");

        std::printf("\n");

        // Needs bar
        std::printf("  Needs: ");
        draw_bar("H", needs.hunger, 255, 100, 100);
        draw_bar("R", needs.rest, 100, 100, 255);
        draw_bar("S", needs.social, 100, 200, 255);
        draw_bar("E", needs.expression, 200, 100, 255);
        draw_bar("P", needs.purpose, 255, 200, 100);
        std::printf("\n");

        // Stress bar
        std::printf("  Stress: ");
        draw_bar("Stress", stress.value, 255, 80, 80);
        std::printf("\n");

        // Inventory
        ansi_fg(200, 200, 150);
        std::printf("  Inventory: raw_food=%.2f raw_mat=%.2f food=%.2f (cap=%.0f)",
            inv.raw_food, inv.raw_material, inv.food, InventoryComponent::CAPACITY);
        ansi_reset();
        std::printf("\n");

        // Personality
        std::printf("  Personality: comp=%.2f lazy=%.2f art=%.2f greg=%.2f res=%.2f cur=%.2f\n",
            pers.compliance, pers.laziness, pers.artistry,
            pers.gregariousness, pers.resilience, pers.curiosity);

        // Utilities
        std::printf("  Utility: G=%.3f B=%.3f W=%.3f E=%.3f R=%.3f Soc=%.3f C=%.3f X=%.3f\n",
            action.last_utility_gather,
            action.last_utility_build,
            action.last_utility_work,
            action.last_utility_eat,
            action.last_utility_rest,
            action.last_utility_socialize,
            action.last_utility_create,
            action.last_utility_explore);
    }

    void draw_bar(const char* label, float value, int r, int g, int b) {
        ansi_fg(r, g, b);
        std::printf("%s", label);
        ansi_reset();
        std::printf("[");
        int filled = (int)(value * 10);
        for (int i = 0; i < 10; i++) {
            if (i < filled) {
                ansi_fg(r, g, b);
                std::printf("#");
                ansi_reset();
            } else {
                std::printf("-");
            }
        }
        std::printf("%.2f ", value);
    }
};
