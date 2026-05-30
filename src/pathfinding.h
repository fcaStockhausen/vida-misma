#pragma once
// A* pathfinding for La Vida Misma.
// Operates on Grid, returns next step toward target.
// Uses a simple per-agent path cache to avoid re-computation.

#include "grid.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <queue>
#include <unordered_map>

struct PathCache {
    int target_x = -1;
    int target_y = -1;
    int next_x = -1;
    int next_y = -1;
    int steps_left = 0;  // recompute after N steps (path may stale)
};

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

// Get the next step from (sx,sy) toward (tx,ty).
// Returns (sx,sy) if already there or no path.
inline std::pair<int,int> astar_next_step(
    const Grid& grid, int sx, int sy, int tx, int ty)
{
    if (sx == tx && sy == ty) return {sx, sy};
    auto path = astar_find_path(grid, sx, sy, tx, ty);
    if (path.empty()) return {sx, sy};
    return path[0];
}
