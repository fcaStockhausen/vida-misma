#pragma once

#include "components.h"
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>

struct WFCSlot {
    std::vector<bool> possible;
    bool collapsed = false;
    TileType value = TileType::Floor;

    void collapse_to(TileType t) {
        value = t;
        collapsed = true;
    }
};

class WFCGenerator {
public:
    WFCGenerator(int w, int h, uint32_t seed)
        : width_(w)
        , height_(h)
        , rng_(seed)
        , slots_(w * h)
    {
        for (auto& s : slots_) {
            s.possible.resize(NUM_STRUCTURAL, true);
        }
    }

    struct Placement {
        int x, y;
        TileType type;
        MachineType machine_type = MachineType::Food;
        float resource_amount = 0.0f;
        float resource_max = 0.0f;
        float resource_regen = 0.0f;
        float storage_capacity = 0.0f;
        bool built = false;
        float build_cost = 0.0f;
        bool built_on_resource = false;  // Machine sits on FoodSource: auto-gathers
        ConveyorDir conveyor_dir = ConveyorDir::E;
    };

    // ================================================================
    // WFC generates ONLY the bare map skeleton:
    //   - Wall perimeter
    //   - Floor tiles (majority)
    //   - OpenSpace clusters (4+ tiles, for social/expression)
    //   - 1 Exit tile (right wall, random Y)
    //   - 2 initial Storage tiles near the Exit
    //   - Machine frames (UNBUILT, agents build them)
    //   - FoodSource tiles (renewable raw food)
    //   - ScrapPile tiles (renewable raw material)
    //
    // Everything else (conveyors, eating zones, extra storage)
    // is built BY AGENTS through the BUILD action.
    // ================================================================

    std::vector<Placement> generate() {
        pre_collapse_boundaries();
        pre_collapse_exit();
        // Open floor plan: no internal walls
        for (int y = 1; y < height_ - 1; y++)
            for (int x = 1; x < width_ - 1; x++) {
                auto& s = slots_[idx(x, y)];
                if (!s.collapsed)
                    s.possible[IDX_WALL] = false;
            }
        run_collapse();
        auto placements = build_placements();
        return placements;
    }

private:
    static constexpr int NUM_STRUCTURAL = 3;
    static constexpr int IDX_WALL  = 0;
    static constexpr int IDX_FLOOR = 1;
    static constexpr int IDX_OPEN  = 2;

    static constexpr float WEIGHT_WALL  = 0.25f;
    static constexpr float WEIGHT_FLOOR = 1.0f;
    static constexpr float WEIGHT_OPEN  = 0.15f;

    TileType idx_to_type(int i) const {
        switch (i) {
            case IDX_WALL:  return TileType::Wall;
            case IDX_FLOOR: return TileType::Floor;
            case IDX_OPEN:  return TileType::OpenSpace;
            default:        return TileType::Floor;
        }
    }

    int type_to_idx(TileType t) const {
        switch (t) {
            case TileType::Wall:       return IDX_WALL;
            case TileType::OpenSpace:  return IDX_OPEN;
            default:                   return IDX_FLOOR;
        }
    }

    bool compatible(int from_idx, int to_idx) const {
        if (from_idx == IDX_WALL) return to_idx == IDX_WALL || to_idx == IDX_FLOOR;
        if (from_idx == IDX_FLOOR) return true;
        if (from_idx == IDX_OPEN) return to_idx == IDX_FLOOR || to_idx == IDX_OPEN;
        return true;
    }

    int width_, height_;
    std::mt19937 rng_;
    std::vector<WFCSlot> slots_;

    int idx(int x, int y) const { return y * width_ + x; }

    void pre_collapse_boundaries() {
        for (int x = 0; x < width_; x++) {
            force_collapse(x, 0, TileType::Wall);
            force_collapse(x, height_ - 1, TileType::Wall);
        }
        for (int y = 0; y < height_; y++) {
            force_collapse(0, y, TileType::Wall);
            force_collapse(width_ - 1, y, TileType::Wall);
        }
    }

    // Single Exit on right wall at random Y position
    void pre_collapse_exit() {
        std::uniform_int_distribution<int> y_dist(3, height_ - 4);
        int ey = y_dist(rng_);
        force_collapse(width_ - 1, ey, TileType::Exit);
    }

    void force_collapse(int x, int y, TileType t) {
        auto& s = slots_[idx(x, y)];
        s.collapse_to(t);
        std::fill(s.possible.begin(), s.possible.end(), false);
    }

    float weight_for(int k) const {
        switch (k) {
            case IDX_WALL:  return WEIGHT_WALL;
            case IDX_FLOOR: return WEIGHT_FLOOR;
            case IDX_OPEN:  return WEIGHT_OPEN;
            default:        return 1.0f;
        }
    }

    void run_collapse() {
        constexpr int dx[] = {1, -1, 0, 0};
        constexpr int dy[] = {0, 0, 1, -1};

        for (;;) {
            int best = -1;
            float best_entropy = 1e30f;

            for (int i = 0; i < (int)slots_.size(); i++) {
                auto& s = slots_[i];
                if (s.collapsed) continue;

                float total_weight = 0.0f;
                int count = 0;
                for (int k = 0; k < NUM_STRUCTURAL; k++) {
                    if (s.possible[k]) {
                        total_weight += weight_for(k);
                        count++;
                    }
                }
                if (count == 0) continue;

                float entropy = 0.0f;
                for (int k = 0; k < NUM_STRUCTURAL; k++) {
                    if (s.possible[k]) {
                        float p = weight_for(k) / total_weight;
                        entropy -= p * std::log2(p);
                    }
                }

                float noise = std::uniform_real_distribution<float>(0.0f, 0.001f)(rng_);
                entropy += noise;
                if (entropy < best_entropy) {
                    best_entropy = entropy;
                    best = i;
                }
            }

            if (best < 0) break;

            auto& slot = slots_[best];
            std::vector<int> candidates;
            std::vector<float> weights;
            float total_w = 0.0f;
            for (int k = 0; k < NUM_STRUCTURAL; k++) {
                if (slot.possible[k]) {
                    candidates.push_back(k);
                    float w = weight_for(k);
                    weights.push_back(w);
                    total_w += w;
                }
            }

            if (candidates.empty()) {
                for (auto& s : slots_) {
                    if (!s.collapsed) {
                        s.collapse_to(TileType::Floor);
                        s.possible.assign(NUM_STRUCTURAL, false);
                    }
                }
                break;
            }

            float r = std::uniform_real_distribution<float>(0.0f, total_w)(rng_);
            int chosen = candidates.back();
            float cumulative = 0.0f;
            for (int i = 0; i < (int)candidates.size(); i++) {
                cumulative += weights[i];
                if (r <= cumulative) {
                    chosen = candidates[i];
                    break;
                }
            }

            slot.collapse_to(idx_to_type(chosen));
            slot.possible.assign(NUM_STRUCTURAL, false);

            std::vector<int> stack;
            int bx = best % width_;
            int by = best / width_;
            for (int d = 0; d < 4; d++) {
                int nx = bx + dx[d], ny = by + dy[d];
                if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_) {
                    stack.push_back(idx(nx, ny));
                }
            }

            while (!stack.empty()) {
                int ci = stack.back();
                stack.pop_back();
                auto& cs = slots_[ci];
                if (cs.collapsed) continue;

                int cx = ci % width_;
                int cy = ci / width_;

                bool changed = false;
                for (int k = 0; k < NUM_STRUCTURAL; k++) {
                    if (!cs.possible[k]) continue;
                    bool any_compat = false;
                    for (int d = 0; d < 4; d++) {
                        int nx = cx + dx[d], ny = cy + dy[d];
                        if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                        auto& ns = slots_[idx(nx, ny)];
                        if (ns.collapsed) {
                            if (compatible(type_to_idx(ns.value), k)) {
                                any_compat = true;
                                break;
                            }
                        } else {
                            for (int nk = 0; nk < NUM_STRUCTURAL; nk++) {
                                if (ns.possible[nk] && compatible(nk, k)) {
                                    any_compat = true;
                                    break;
                                }
                            }
                            if (any_compat) break;
                        }
                    }
                    if (!any_compat) {
                        cs.possible[k] = false;
                        changed = true;
                    }
                }

                if (changed) {
                    for (int d = 0; d < 4; d++) {
                        int nx = cx + dx[d], ny = cy + dy[d];
                        if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_) {
                            stack.push_back(idx(nx, ny));
                        }
                    }
                }
            }
        }
    }

    TileType slot_type(int x, int y) const {
        return slots_[idx(x, y)].value;
    }

    // ================================================================
    // build_placements: stamp all non-WFC entities onto the grid.
    // WFC provides: Floor, OpenSpace, Wall, Exit
    // We add: 2 Storage near Exit, Machine frames, FoodSource, ScrapPile, OpenSpace clusters
    // ================================================================

    std::vector<Placement> build_placements() {
        std::vector<Placement> P;

        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                P.push_back({x, y, slot_type(x, y)});
            }

        place_open_spaces(P);      // OpenSpace clusters (4+ tiles, guaranteed)
        place_exit_storage(P);     // 2 Storage tiles near Exit
        place_food_sources(P);     // Renewable raw food (agents build FoodMachine on top)
        place_scrap_piles(P);      // Renewable raw material (agents build OutputMachine on top)

        return P;
    }

    TileType peek(const std::vector<Placement>& P, int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return TileType::Wall;
        return P[y * width_ + x].type;
    }

    void set_placement(std::vector<Placement>& P, int x, int y, Placement pl) {
        P[y * width_ + x] = pl;
    }

    // ================================================================
    // OpenSpace: guaranteed clusters of 4+ tiles.
    // Place 1 large central plaza + 2-4 smaller clusters.
    // Minimum radius 2 (5x5 = 25 tiles, well above 4 minimum).
    // ================================================================

    void place_open_spaces(std::vector<Placement>& P) {
        // Central plaza: large gathering space
        int mid_x = width_ / 2;
        int mid_y = height_ / 2;
        std::uniform_int_distribution<int> rx_dist(3, 5);
        std::uniform_int_distribution<int> ry_dist(2, 3);
        int radius_x = rx_dist(rng_);
        int radius_y = ry_dist(rng_);

        for (int dy = -radius_y; dy <= radius_y; dy++)
            for (int dx = -radius_x; dx <= radius_x; dx++) {
                int px = mid_x + dx;
                int py = mid_y + dy;
                if (px > 0 && px < width_ - 1 && py > 0 && py < height_ - 1) {
                    if (peek(P, px, py) == TileType::Floor) {
                        set_placement(P, px, py, {px, py, TileType::OpenSpace});
                    }
                }
            }

        // Additional clusters: each guaranteed 4+ tiles (2x2 minimum)
        std::uniform_int_distribution<int> n_extra_dist(2, 4);
        int n_extra = n_extra_dist(rng_);
        std::uniform_int_distribution<int> x_dist(5, width_ - 6);
        std::uniform_int_distribution<int> y_dist(5, height_ - 6);

        for (int i = 0; i < n_extra; i++) {
            int cx = x_dist(rng_);
            int cy = y_dist(rng_);
            // Radius 2 = 5x5 cluster (guaranteed > 4 tiles)
            // Radius 1 = 3x3 cluster (9 tiles, well above minimum)
            // Never use radius 0 (single tile)
            std::uniform_int_distribution<int> r_dist(1, 2);
            int r = r_dist(rng_);
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                    int px = cx + dx;
                    int py = cy + dy;
                    if (px > 0 && px < width_ - 1 && py > 0 && py < height_ - 1) {
                        if (peek(P, px, py) == TileType::Floor) {
                            set_placement(P, px, py, {px, py, TileType::OpenSpace});
                        }
                    }
                }
        }
    }

    // ================================================================
    // Exit-adjacent Storage: 2 tiles near the Exit.
    // This is where agents deliver food and where Output gets shipped.
    // Exit position is found from the collapsed WFC grid.
    // ================================================================

    void place_exit_storage(std::vector<Placement>& P) {
        // Find the Exit tile
        int exit_x = -1, exit_y = -1;
        for (int y = 1; y < height_ - 1; y++) {
            if (slot_type(width_ - 1, y) == TileType::Exit) {
                exit_x = width_ - 1;
                exit_y = y;
                break;
            }
        }
        if (exit_x < 0) return;  // shouldn't happen

        // Place 2 Storage tiles adjacent to Exit (inside the wall)
        // Try positions: (exit_x-1, exit_y-1) and (exit_x-1, exit_y+1)
        // or (exit_x-1, exit_y) and (exit_x-2, exit_y)
        std::vector<std::pair<int,int>> candidates = {
            {exit_x - 1, exit_y},
            {exit_x - 2, exit_y},
            {exit_x - 1, exit_y - 1},
            {exit_x - 1, exit_y + 1},
            {exit_x - 2, exit_y - 1},
            {exit_x - 2, exit_y + 1},
        };

        int placed = 0;
        for (auto [sx, sy] : candidates) {
            if (placed >= 2) break;
            if (sx > 0 && sx < width_ - 1 && sy > 0 && sy < height_ - 1) {
                if (peek(P, sx, sy) == TileType::Floor || peek(P, sx, sy) == TileType::OpenSpace) {
                    set_placement(P, sx, sy,
                        {sx, sy, TileType::Storage, MachineType::Food,
                         0, 0, 0, 20.0f, true, 0.0f, false, ConveyorDir::E});  // built=true: initial infrastructure
                    placed++;
                }
            }
        }
    }

    // ================================================================
    // Machine frames: UNBUILT, agents must construct them.
    // Materials in NE quadrant, Output in SE quadrant.
    // FoodMachines are placed on FoodSource tiles (see place_food_sources).
    // ================================================================

    void place_machines(std::vector<Placement>& P) {
        struct Quadrant {
            int x0, y0, x1, y1;
            MachineType mtype;
        };

        int margin = 3;
        int zone_w = width_ / 4;
        int zone_h = height_ / 4;

        std::vector<Quadrant> quads = {
            { width_ - margin - zone_w, margin, width_ - margin, margin + zone_h, MachineType::Materials },
            { width_ - margin - zone_w, height_ - margin - zone_h, width_ - margin, height_ - margin, MachineType::Output },
        };

        std::uniform_int_distribution<int> n_dist(3, 5);

        for (auto& q : quads) {
            int range_x = q.x1 - q.x0 - 2;
            int range_y = q.y1 - q.y0 - 2;
            if (range_x < 2 || range_y < 2) continue;

            std::uniform_int_distribution<int> cx_dist(q.x0 + 1, q.x1 - 2);
            std::uniform_int_distribution<int> cy_dist(q.y0 + 1, q.y1 - 2);
            int cx = cx_dist(rng_);
            int cy = cy_dist(rng_);
            int n_machines = n_dist(rng_);

            std::vector<std::pair<int,int>> positions;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int px = cx + dx * 2;
                    int py = cy + dy * 2;
                    if (px > 0 && px < width_ - 1 && py > 0 && py < height_ - 1) {
                        positions.push_back({px, py});
                    }
                }

            std::shuffle(positions.begin(), positions.end(), rng_);

            int placed = 0;
            for (auto& [mx, my] : positions) {
                if (placed >= n_machines) break;
                if (peek(P, mx, my) != TileType::Floor) continue;
                bool too_close = false;
                for (int dy = -1; dy <= 1 && !too_close; dy++)
                    for (int dx = -1; dx <= 1 && !too_close; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        if (peek(P, mx + dx, my + dy) == TileType::Machine)
                            too_close = true;
                    }
                if (too_close) continue;

                // UNBUILT machine frame -- agents must construct it
                set_placement(P, mx, my,
                    {mx, my, TileType::Machine, q.mtype,
                     0, 0, 0, 0, false, 2.0f, false, ConveyorDir::E});
                placed++;
            }
        }

        // Place built Storage tiles adjacent to each machine.
        // These give machines local deposit targets; conveyors then transport to Exit.
        place_machine_storage(P);
    }

    void place_machine_storage(std::vector<Placement>& P) {
        for (int y = 1; y < height_ - 1; y++)
            for (int x = 1; x < width_ - 1; x++) {
                if (peek(P, x, y) != TileType::Machine) continue;
                // Check if already has adjacent Storage
                bool has_storage = false;
                for (int dy = -1; dy <= 1 && !has_storage; dy++)
                    for (int dx = -1; dx <= 1 && !has_storage; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        if (peek(P, x + dx, y + dy) == TileType::Storage)
                            has_storage = true;
                    }
                if (has_storage) continue;
                // Place one Storage in an adjacent Floor tile
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int sx = x + dx, sy = y + dy;
                        if (sx > 0 && sx < width_ - 1 && sy > 0 && sy < height_ - 1 &&
                            peek(P, sx, sy) == TileType::Floor) {
                            set_placement(P, sx, sy,
                                {sx, sy, TileType::Storage, MachineType::Food,
                                 0, 0, 0, 20.0f, true, 0.0f, false, ConveyorDir::E});
                            break;  // one per machine
                        }
                    }
                    break;  // stop after first row of neighbors
                }
            }
    }

    // ================================================================
    // FoodSource: renewable raw food scattered across the map.
    // 10-14 sources, regen 0.15/tick. Agents can BUILD a FoodMachine
    // on top of any FoodSource tile.
    // ================================================================

    void place_food_sources(std::vector<Placement>& P) {
        std::uniform_int_distribution<int> n_dist(10, 14);
        int n_sources = n_dist(rng_);
        std::uniform_int_distribution<int> x_dist(3, width_ - 4);
        std::uniform_int_distribution<int> y_dist(3, height_ - 4);

        int placed = 0;
        int attempts = 0;
        while (placed < n_sources && attempts < n_sources * 30) {
            attempts++;
            int x = x_dist(rng_);
            int y = y_dist(rng_);
            if (peek(P, x, y) != TileType::Floor) continue;

            // Min distance 4 from other food sources
            bool crowded = false;
            for (int dy2 = -4; dy2 <= 4 && !crowded; dy2++)
                for (int dx2 = -4; dx2 <= 4 && !crowded; dx2++) {
                    if (dx2 == 0 && dy2 == 0) continue;
                    int nx = x + dx2, ny = y + dy2;
                    if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_
                        && peek(P, nx, ny) == TileType::FoodSource)
                        crowded = true;
                }
            if (crowded) continue;

            set_placement(P, x, y,
                {x, y, TileType::FoodSource, MachineType::Food,
                 12.0f, 15.0f, 0.15f, 0.0f, false, 0.0f, false, ConveyorDir::E});
            placed++;
        }
    }

    // ================================================================
    // ScrapPile: renewable raw material scattered across the map.
    // 8-16 piles, regen 0.08/tick each.
    // ================================================================

    void place_scrap_piles(std::vector<Placement>& P) {
        // More ScrapPiles than FoodSources: agents need scrap to build everything
        std::uniform_int_distribution<int> n_dist(12, 20);
        int n_piles = n_dist(rng_);
        std::uniform_int_distribution<int> x_dist(2, width_ - 3);
        std::uniform_int_distribution<int> y_dist(2, height_ - 3);

        // First: ensure at least 1 ScrapPile near the Exit (radius 5)
        // so OutputMachines can produce output near Exit-adjacent Storage.
        auto exits = std::vector<std::pair<int,int>>();
        for (int y2 = 0; y2 < height_; y2++)
            for (int x2 = 0; x2 < width_; x2++)
                if (peek(P, x2, y2) == TileType::Exit)
                    exits.push_back({x2, y2});

        int placed = 0;
        if (!exits.empty()) {
            auto [ex, ey] = exits[0];
            // Strategy: find Storage tiles near Exit, place ScrapPile adjacent to one of them
            auto storages = std::vector<std::pair<int,int>>();
            for (int dy = -3; dy <= 3; dy++)
                for (int dx = -3; dx <= 3; dx++) {
                    int nx = ex + dx, ny = ey + dy;
                    if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                    if (peek(P, nx, ny) == TileType::Storage)
                        storages.push_back({nx, ny});
                }
            // Try to place ScrapPile adjacent to each Storage
            for (auto [sx, sy] : storages) {
                if (placed >= 2) break;
                int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                for (auto& [ddx, ddy] : dirs) {
                    int nx = sx + ddx, ny = sy + ddy;
                    if (nx < 2 || nx >= width_ - 2 || ny < 2 || ny >= height_ - 2) continue;
                    if (peek(P, nx, ny) != TileType::Floor) continue;
                    // Place a ScrapPile adjacent to Exit-Storage so agents can
                    // build OutputMachine here and feed output directly to Exit.
                    set_placement(P, nx, ny,
                        {nx, ny, TileType::ScrapPile, MachineType::Output,
                         10.0f, 10.0f, 0.08f, 0.0f, false, 0.0f, false, ConveyorDir::E});
                    placed++;
                    break;
                }
            }
            // Fallback: random near Exit
            if (placed == 0) {
                std::uniform_int_distribution<int> off_dist(-5, 5);
                for (int attempt = 0; attempt < 100 && placed < 2; attempt++) {
                    int x = ex + off_dist(rng_);
                    int y = ey + off_dist(rng_);
                    if (x < 2 || x >= width_ - 2 || y < 2 || y >= height_ - 2) continue;
                    if (peek(P, x, y) != TileType::Floor) continue;
                    set_placement(P, x, y,
                        {x, y, TileType::ScrapPile, MachineType::Output,
                         10.0f, 10.0f, 0.08f, 0.0f, false, 0.0f, false, ConveyorDir::E});
                    placed++;
                }
            }
        }

        // Fill the rest randomly
        int attempts = 0;
        while (placed < n_piles && attempts < n_piles * 30) {
            attempts++;
            int x = x_dist(rng_);
            int y = y_dist(rng_);
            if (peek(P, x, y) != TileType::Floor) continue;

            // Min distance 3 from other scrap piles
            bool crowded = false;
            for (int dy2 = -3; dy2 <= 3 && !crowded; dy2++)
                for (int dx2 = -3; dx2 <= 3 && !crowded; dx2++) {
                    if (dx2 == 0 && dy2 == 0) continue;
                    if (peek(P, x + dx2, y + dy2) == TileType::ScrapPile)
                        crowded = true;
                }
            if (crowded) continue;

            set_placement(P, x, y,
                {x, y, TileType::ScrapPile, MachineType::Output,
                 10.0f, 10.0f, 0.08f, 0.0f, false, 0.0f, false, ConveyorDir::E});
            placed++;
        }
    }
};
