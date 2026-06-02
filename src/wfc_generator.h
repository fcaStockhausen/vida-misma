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
        ConveyorDir conveyor_dir = ConveyorDir::E;
    };

    std::vector<Placement> generate() {
        pre_collapse_boundaries();
        pre_collapse_portals();
        forbid_inner_walls();
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

    void pre_collapse_portals() {
        int mid_y = height_ / 2;
        for (int dy = -1; dy <= 1; dy++) {
            force_collapse(0, mid_y + dy, TileType::Entrance);
            force_collapse(width_ - 1, mid_y + dy, TileType::Exit);
        }
    }

    void stamp_internal_walls() {
        std::uniform_int_distribution<int> n_walls_dist(2, 5);
        int n_walls = n_walls_dist(rng_);

        for (int w = 0; w < n_walls; w++) {
            std::uniform_int_distribution<int> horiz_dist(0, 1);
            bool horizontal = horiz_dist(rng_) == 0;

            if (horizontal) {
                std::uniform_int_distribution<int> y_dist(4, height_ - 5);
                int wy = y_dist(rng_);
                std::uniform_int_distribution<int> x0_dist(2, width_ / 3);
                std::uniform_int_distribution<int> x1_dist(2 * width_ / 3, width_ - 3);
                int x0 = x0_dist(rng_);
                int x1 = x1_dist(rng_);

                int gap_pos = std::uniform_int_distribution<int>(x0 + 1, x1 - 1)(rng_);
                for (int x = x0; x <= x1; x++) {
                    if (x == gap_pos) continue;
                    force_collapse(x, wy, TileType::Wall);
                }
            } else {
                std::uniform_int_distribution<int> x_dist(4, width_ - 5);
                int wx = x_dist(rng_);
                std::uniform_int_distribution<int> y0_dist(2, height_ / 3);
                std::uniform_int_distribution<int> y1_dist(2 * height_ / 3, height_ - 3);
                int y0 = y0_dist(rng_);
                int y1 = y1_dist(rng_);

                int gap_pos = std::uniform_int_distribution<int>(y0 + 1, y1 - 1)(rng_);
                for (int y = y0; y <= y1; y++) {
                    if (y == gap_pos) continue;
                    force_collapse(wx, y, TileType::Wall);
                }
            }
        }
    }

    void forbid_inner_walls() {
        for (int y = 1; y < height_ - 1; y++)
            for (int x = 1; x < width_ - 1; x++) {
                auto& s = slots_[idx(x, y)];
                if (!s.collapsed) {
                    s.possible[IDX_WALL] = false;
                }
            }
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

    std::vector<Placement> build_placements() {
        std::vector<Placement> P;

        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                P.push_back({x, y, slot_type(x, y)});
            }

        place_exit_storage(P);
        place_machines(P);
        place_food_sources(P);
        place_scrap_piles(P);
        place_open_spaces(P);
        stamp_conveyors(P);

        return P;
    }

    TileType peek(const std::vector<Placement>& P, int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return TileType::Wall;
        return P[y * width_ + x].type;
    }

    void set_placement(std::vector<Placement>& P, int x, int y, Placement pl) {
        P[y * width_ + x] = pl;
    }

    void place_exit_storage(std::vector<Placement>& P) {
        int mid_y = height_ / 2;
        set_placement(P, width_ - 2, mid_y,
            {width_ - 2, mid_y, TileType::Storage, MachineType::Food,
             0, 0, 0, 20.0f, false, 0.0f, ConveyorDir::E});
    }

    void place_machines(std::vector<Placement>& P) {
        struct Quadrant {
            int x0, y0, x1, y1;
            MachineType mtype;
        };

        int margin = 3;
        int zone_w = width_ / 4;
        int zone_h = height_ / 4;

        std::vector<Quadrant> quads = {
            { margin, margin, margin + zone_w, margin + zone_h, MachineType::Food },
            { margin, height_ - margin - zone_h, margin + zone_w, height_ - margin, MachineType::Food },
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

                set_placement(P, mx, my,
                    {mx, my, TileType::Machine, q.mtype,
                     0, 0, 0, 0, false, 2.0f, ConveyorDir::E});
                placed++;

                place_storage_near(P, mx, my);
            }
        }

        int mid_x = width_ / 2;
        int midy = height_ / 2;
        set_placement(P, mid_x - 2, midy,
            {mid_x - 2, midy, TileType::Storage, MachineType::Food,
             0, 0, 0, 20.0f, false, 0.0f, ConveyorDir::E});
        set_placement(P, mid_x + 2, midy,
            {mid_x + 2, midy, TileType::Storage, MachineType::Food,
             0, 0, 0, 20.0f, false, 0.0f, ConveyorDir::E});
    }

    void place_storage_near(std::vector<Placement>& P, int mx, int my) {
        constexpr int ddx[] = {0, 0, -2, 2};
        constexpr int ddy[] = {-2, 2, 0, 0};
        int dirs[] = {0, 1, 2, 3};
        std::shuffle(dirs, dirs + 4, rng_);

        for (int d = 0; d < 4; d++) {
            int nx = mx + ddx[dirs[d]];
            int ny = my + ddy[dirs[d]];
            if (nx > 0 && nx < width_ - 1 && ny > 0 && ny < height_ - 1 &&
                peek(P, nx, ny) == TileType::Floor) {
                set_placement(P, nx, ny,
                    {nx, ny, TileType::Storage, MachineType::Food,
                     0, 0, 0, 20.0f, false, 0.0f, ConveyorDir::E});
                return;
            }
        }
    }

    void place_scrap_piles(std::vector<Placement>& P) {
        std::uniform_int_distribution<int> n_dist(8, 16);
        int n_piles = n_dist(rng_);
        std::uniform_int_distribution<int> x_dist(2, width_ - 3);
        std::uniform_int_distribution<int> y_dist(2, height_ - 3);

        int placed = 0;
        int attempts = 0;
        while (placed < n_piles && attempts < n_piles * 30) {
            attempts++;
            int x = x_dist(rng_);
            int y = y_dist(rng_);
            if (peek(P, x, y) != TileType::Floor) continue;

            bool too_close = false;
            for (int dy = -3; dy <= 3 && !too_close; dy++)
                for (int dx = -3; dx <= 3 && !too_close; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    if (peek(P, x + dx, y + dy) == TileType::Machine)
                        too_close = true;
                }
            if (too_close) continue;

            set_placement(P, x, y,
                {x, y, TileType::ScrapPile, MachineType::Food,
                 10.0f, 10.0f, 0.08f, 0.0f, false, 0.0f, ConveyorDir::E});
            placed++;
        }
    }

    void place_open_spaces(std::vector<Placement>& P) {
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

        std::uniform_int_distribution<int> n_extra_dist(1, 3);
        int n_extra = n_extra_dist(rng_);
        std::uniform_int_distribution<int> x_dist(5, width_ - 6);
        std::uniform_int_distribution<int> y_dist(5, height_ - 6);
        for (int i = 0; i < n_extra; i++) {
            int cx = x_dist(rng_);
            int cy = y_dist(rng_);
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

    void stamp_conveyors(std::vector<Placement>& P) {
        int mid_y = height_ / 2;
        for (int cx = width_ / 2 + 3; cx < width_ - 2; cx++) {
            if (peek(P, cx, mid_y) == TileType::Floor) {
                set_placement(P, cx, mid_y,
                    {cx, mid_y, TileType::Conveyor, MachineType::Food,
                     0, 0, 0, 0, false, 1.5f, ConveyorDir::E});
            }
        }

        std::vector<std::pair<int,int>> machines;
        std::vector<std::pair<int,int>> storages;
        for (auto& p : P) {
            if (p.type == TileType::Machine) machines.push_back({p.x, p.y});
            if (p.type == TileType::Storage) storages.push_back({p.x, p.y});
        }

        for (auto [mx, my] : machines) {
            std::pair<int,int> best_s = {-1, -1};
            int best_d = 999999;
            for (auto [sx, sy] : storages) {
                int d = std::abs(mx - sx) + std::abs(my - sy);
                if (d < best_d && d >= 2) { best_d = d; best_s = {sx, sy}; }
            }
            if (best_s.first < 0) continue;

            int cx = mx, cy = my;
            int steps = 0;
            while ((cx != best_s.first || cy != best_s.second) && steps < 40) {
                if (cx != best_s.first) cx += (best_s.first > cx) ? 1 : -1;
                else if (cy != best_s.second) cy += (best_s.second > cy) ? 1 : -1;
                else break;

                if (peek(P, cx, cy) == TileType::Floor) {
                    ConveyorDir dir;
                    if (cx != best_s.first) dir = (best_s.first > cx) ? ConveyorDir::E : ConveyorDir::W;
                    else dir = (best_s.second > cy) ? ConveyorDir::S : ConveyorDir::N;
                    set_placement(P, cx, cy,
                        {cx, cy, TileType::Conveyor, MachineType::Food,
                         0, 0, 0, 0, false, 1.5f, dir});
                }
                steps++;
            }
        }
    }

    void place_food_sources(std::vector<Placement>& P) {
        // Renewable raw food sources across the map.
        // Food is plentiful but must be processed (raw_food → food via FoodMachine).
        std::uniform_int_distribution<int> n_dist(8, 12);
        int n_sources = n_dist(rng_);
        std::uniform_int_distribution<int> x_dist(2, width_ - 3);
        std::uniform_int_distribution<int> y_dist(2, height_ - 3);

        int placed = 0;
        int attempts = 0;
        while (placed < n_sources && attempts < n_sources * 30) {
            attempts++;
            int x = x_dist(rng_);
            int y = y_dist(rng_);
            if (peek(P, x, y) != TileType::Floor) continue;

            // Don't place too close to machines
            bool too_close = false;
            for (int dy = -3; dy <= 3 && !too_close; dy++)
                for (int dx = -3; dx <= 3 && !too_close; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    if (peek(P, x + dx, y + dy) == TileType::Machine)
                        too_close = true;
                }
            if (too_close) continue;

            // Don't place too close to other food sources (min 4 manhattan)
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
                 12.0f, 15.0f, 0.10f, 0.0f, false, 0.0f, ConveyorDir::E});
            placed++;
        }
    }

    // REMOVED: all machines must be built from scratch by agents.
    void bootstrap_machines(std::vector<Placement>&) {}
};
