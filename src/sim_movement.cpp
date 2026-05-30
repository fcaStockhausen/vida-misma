#include "simulation.h"
#include <algorithm>
#include <cstdlib>

// ============================================================
// SYSTEM: Move to Targets
// ============================================================

void Simulation::system_move_to_targets() {
    auto view = registry_.view<ActionComponent, PositionComponent,
                               const AgentComponent>();
    for (auto e : view) {
        if (!registry_.get<AgentComponent>(e).alive) continue;

        auto& action = registry_.get<ActionComponent>(e);
        auto& pos    = registry_.get<PositionComponent>(e);

        if (action.at_target) continue;
        if (action.target_x < 0 || action.target_y < 0) continue;

        if (pos.x == action.target_x && pos.y == action.target_y) {
            action.at_target = true;
            continue;
        }

        // Noise: sometimes random step
        std::uniform_real_distribution<float> noise_roll(0.0f, 1.0f);
        if (noise_roll(rng_) < config_.movement_noise) {
            random_move(pos);
            continue;
        }

        // Greedy step toward target
        move_toward(pos, action.target_x, action.target_y);

        if (pos.x == action.target_x && pos.y == action.target_y) {
            action.at_target = true;
        }
    }
}

// ============================================================
// Movement helpers
// ============================================================

void Simulation::move_toward(PositionComponent& pos, int tx, int ty) {
    int dx = tx - pos.x;
    int dy = ty - pos.y;
    if (dx == 0 && dy == 0) return;

    int current_dist = std::abs(dx) + std::abs(dy);

    // Try all 4 cardinal directions, pick the one that reduces Manhattan distance most
    struct Dir { int x; int y; };
    Dir dirs[] = {{1,0}, {-1,0}, {0,1}, {0,-1}};

    int best_dist = current_dist;
    int best_nx = pos.x, best_ny = pos.y;
    bool found = false;

    for (auto& d : dirs) {
        int nx = pos.x + d.x;
        int ny = pos.y + d.y;
        if (!grid_.is_walkable(nx, ny)) continue;
        int nd = std::abs(tx - nx) + std::abs(ty - ny);
        if (nd < best_dist) {
            best_dist = nd;
            best_nx = nx;
            best_ny = ny;
            found = true;
        }
    }

    if (found) {
        pos.x = best_nx;
        pos.y = best_ny;
    } else {
        // No progress possible -- try random move
        random_move(pos);
    }
}

void Simulation::random_move(PositionComponent& pos) {
    std::uniform_int_distribution<int> dir(-1, 1);
    int nx = pos.x + dir(rng_);
    int ny = pos.y + dir(rng_);
    nx = std::clamp(nx, 0, grid_.width() - 1);
    ny = std::clamp(ny, 0, grid_.height() - 1);
    if (grid_.is_walkable(nx, ny)) {
        pos.x = nx;
        pos.y = ny;
    }
}
