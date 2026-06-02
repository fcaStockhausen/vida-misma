#pragma once
// Path cache data for A* pathfinding.
// Separated from pathfinding.h to avoid circular includes (components.h <-> grid.h).

#include <vector>
#include <utility>

struct PathCache {
    std::vector<std::pair<int,int>> path;  // full A* path from current pos to target
    int target_x = -1;                     // target the path was computed for
    int target_y = -1;
    int step_index = 0;                    // current position in the path
    int computed_tick = -1;                // tick when path was computed (staleness check)
};
