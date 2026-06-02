#pragma once
// A* pathfinding for La Vida Misma.
// Operates on Grid, returns next step toward target.
// Uses a per-agent path cache to avoid re-computation every tick.

#include "path_cache.h"
#include "grid.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <queue>
#include <unordered_map>

// A* on a grid. Returns the full path from (sx,sy) to (tx,ty).
// If no path exists, returns empty vector.
inline std::vector<std::pair<int,int>> astar_find_path(
    const Grid& grid, int sx, int sy, int tx, int ty, int max_nodes = 2400)
{
    if (sx == tx && sy == ty) return {};
    if (!grid.is_walkable(tx, ty)) return {};

    struct Node {
        int x, y;
        int g;      // cost from start
        int f;      // g + heuristic
        bool operator>(const Node& o) const { return f > o.f; }
    };

    // Priority queue (min-heap by f-score)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    // Closed set + g-scores
    int w = grid.width();
    int h = grid.height();
    std::vector<bool> closed(w * h, false);
    std::vector<int> g_score(w * h, 999999);
    std::vector<int> from(w * h, -1);  // parent index

    auto idx = [w](int x, int y) { return y * w + x; };

    g_score[idx(sx, sy)] = 0;
    int h0 = std::abs(tx - sx) + std::abs(ty - sy);
    open.push({sx, sy, 0, h0});

    int expanded = 0;

    while (!open.empty() && expanded < max_nodes) {
        Node cur = open.top();
        open.pop();

        if (cur.x == tx && cur.y == ty) {
            // Reconstruct path
            std::vector<std::pair<int,int>> path;
            int ci = idx(cur.x, cur.y);
            while (ci != idx(sx, sy)) {
                path.push_back({ci % w, ci / w});
                ci = from[ci];
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        int ci = idx(cur.x, cur.y);
        if (closed[ci]) continue;
        closed[ci] = true;
        expanded++;

        // 4-directional neighbors
        constexpr int dx[] = {1, -1, 0, 0};
        constexpr int dy[] = {0, 0, 1, -1};

        for (int d = 0; d < 4; d++) {
            int nx = cur.x + dx[d];
            int ny = cur.y + dy[d];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (!grid.is_walkable(nx, ny)) continue;

            int ni = idx(nx, ny);
            if (closed[ni]) continue;

            int ng = cur.g + 1;
            if (ng < g_score[ni]) {
                g_score[ni] = ng;
                from[ni] = ci;
                int nf = ng + std::abs(tx - nx) + std::abs(ty - ny);
                open.push({nx, ny, ng, nf});
            }
        }
    }

    return {};  // no path found
}

// Cached path-following: uses PathCache to avoid recomputing A* every tick.
// Returns the next (x,y) step from (sx,sy) toward (tx,ty).
// Recomputes when: target changes, path is stale (>20 ticks old), or path is blocked.
inline std::pair<int,int> cached_next_step(
    const Grid& grid, PathCache& cache, int sx, int sy, int tx, int ty, int tick)
{
    if (sx == tx && sy == ty) return {sx, sy};

    bool need_recompute = false;

    // Recompute if target changed
    if (cache.target_x != tx || cache.target_y != ty) {
        need_recompute = true;
    }
    // Recompute if path is stale (map changes: conveyors built/dismantled)
    if (!need_recompute && (tick - cache.computed_tick > 20)) {
        need_recompute = true;
    }
    // Recompute if step_index is out of bounds (shouldn't happen normally)
    if (!need_recompute && cache.step_index >= (int)cache.path.size()) {
        need_recompute = true;
    }
    // Recompute if next step in path is no longer walkable
    if (!need_recompute && cache.step_index < (int)cache.path.size()) {
        auto [nx, ny] = cache.path[cache.step_index];
        if (!grid.is_walkable(nx, ny)) {
            need_recompute = true;
        }
    }

    if (need_recompute) {
        cache.path = astar_find_path(grid, sx, sy, tx, ty);
        cache.target_x = tx;
        cache.target_y = ty;
        cache.step_index = 0;
        cache.computed_tick = tick;

        if (cache.path.empty()) return {sx, sy};  // no path exists
    }

    // Advance along the cached path: find the step closest to current position
    // (handles the case where the agent moved away from the path due to external forces)
    if (cache.step_index < (int)cache.path.size()) {
        // Check if we are at the expected position; if not, try to reconnect
        auto [px, py] = cache.path[cache.step_index];
        if (sx != px || sy != py) {
            // Agent diverged from path — look for current position in remaining path
            bool found = false;
            for (int i = cache.step_index; i < (int)cache.path.size(); i++) {
                if (cache.path[i].first == sx && cache.path[i].second == sy) {
                    cache.step_index = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Not on path at all — recompute from current position
                cache.path = astar_find_path(grid, sx, sy, tx, ty);
                cache.step_index = 0;
                cache.computed_tick = tick;
                if (cache.path.empty()) return {sx, sy};
            }
        }

        if (cache.step_index < (int)cache.path.size()) {
            auto [nx, ny] = cache.path[cache.step_index];
            cache.step_index++;
            return {nx, ny};
        }
    }

    return {sx, sy};
}
