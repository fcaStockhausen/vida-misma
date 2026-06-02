#include "simulation.h"
#include "pathfinding.h"
#include <algorithm>
#include <cstdlib>

// ============================================================
// SYSTEM: Move to Targets (with cached A* pathfinding)
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

        // Cached A* pathfinding: get next step along cached path
        auto [nx, ny] = cached_next_step(grid_, action.path_cache,
                                          pos.x, pos.y,
                                          action.target_x, action.target_y,
                                          tick_);

        if (nx != pos.x || ny != pos.y) {
            pos.x = nx;
            pos.y = ny;
        }

        if (pos.x == action.target_x && pos.y == action.target_y) {
            action.at_target = true;
        }
    }
}

// ============================================================
// Movement helpers
// ============================================================

void Simulation::move_toward(PositionComponent& pos, int tx, int ty) {
    // Direct A* call (no cache) — used only as fallback
    auto path = astar_find_path(grid_, pos.x, pos.y, tx, ty);
    if (!path.empty()) {
        pos.x = path[0].first;
        pos.y = path[0].second;
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
