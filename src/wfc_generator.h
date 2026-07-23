#pragma once

#include "components.h"
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <stdexcept>

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
        float conveyor_condition = 1.0f;
        float stored_food = 0.0f;
        float stored_raw_food = 0.0f;
        float stored_raw_material = 0.0f;
        float stored_construction_material = 0.0f;
        float stored_output = 0.0f;
    };

    // ================================================================
    // WFC generates the inherited institutional layout:
    //   - Wall perimeter
    //   - Floor tiles (majority)
    //   - OpenSpace clusters (4+ tiles, for social/expression)
    //   - 1 Exit tile on the right and 1 Entrance on the left
    //   - A minimal built production chain near the Exit
    //   - FoodSource tiles (renewable raw food)
    //   - ScrapPile tiles (renewable raw material)
    //
    // Agents inherit, maintain, repair, and expand this installation.
    // ================================================================

    std::vector<Placement> generate() {
        pre_collapse_boundaries();
        pre_collapse_exit();
        pre_collapse_entrance();
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
    int exit_y_ = 3;

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
        exit_y_ = y_dist(rng_);
        force_collapse(width_ - 1, exit_y_, TileType::Exit);
    }

    // Deterministic from the already sampled Exit, so adding Entrance consumes
    // no additional WFC randomness.
    void pre_collapse_entrance() {
        int entrance_y = std::clamp(height_ - 1 - exit_y_, 2, height_ - 3);
        force_collapse(0, entrance_y, TileType::Entrance);
        force_collapse(1, entrance_y, TileType::Floor);
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
    // We add: inherited factory, FoodSource, ScrapPile, and OpenSpace clusters.
    // ================================================================

    std::vector<Placement> build_placements() {
        std::vector<Placement> P;

        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                P.push_back({x, y, slot_type(x, y)});
            }

        place_open_spaces(P);      // OpenSpace clusters (4+ tiles, guaranteed)
        place_eating_zone(P);      // Central communal eating space (large, pre-built)
        place_inherited_factory(P);// Minimal degraded production chain
        place_food_sources(P);     // Renewable raw food (agents build FoodMachine on top)
        place_scrap_piles(P);      // Renewable raw material (agents build MaterialsMachine on top)

        return P;
    }

    void place_inherited_factory(std::vector<Placement>& P) {
        int exit_x = -1, exit_y = -1;
        for (int y = 1; y < height_ - 1; y++) {
            if (slot_type(width_ - 1, y) == TileType::Exit) {
                exit_x = width_ - 1;
                exit_y = y;
                break;
            }
        }
        if (exit_x < 0 || exit_x - 12 < 1) {
            throw std::runtime_error("grid is too narrow for inherited factory");
        }

        Placement food{exit_x - 12, exit_y, TileType::Machine,
            MachineType::Food, 6.0f, 15.0f, 0.15f, 0.0f,
            true, 0.15f, true, ConveyorDir::E};
        food.stored_raw_food = 0.15f;
        set_placement(P, food.x, food.y, food);

        Placement service_storage{exit_x - 11, exit_y, TileType::Storage,
            MachineType::Food, 0, 0, 0, 8.0f, true};
        service_storage.stored_food = 1.0f;
        set_placement(P, service_storage.x, service_storage.y, service_storage);

        Placement materials{exit_x - 9, exit_y, TileType::Machine,
            MachineType::Materials, 4.0f, 10.0f, 0.15f, 0.0f,
            true, 0.15f, true, ConveyorDir::E};
        materials.stored_raw_material = 0.15f;
        set_placement(P, materials.x, materials.y, materials);

        Placement output{exit_x - 6, exit_y, TileType::Machine,
            MachineType::Output, 0, 0, 0, 0, true, 0.15f, false,
            ConveyorDir::E};
        output.stored_construction_material = 0.30f;
        set_placement(P, output.x, output.y, output);

        constexpr float conditions[] = {0.55f, 0.65f, 0.75f, 0.85f};
        for (int i = 0; i < 4; i++) {
            Placement conveyor{exit_x - 5 + i, exit_y, TileType::Conveyor,
                MachineType::Food, 0, 0, 0, 0, true, 0.15f, false,
                ConveyorDir::E};
            conveyor.conveyor_condition = conditions[i];
            set_placement(P, conveyor.x, conveyor.y, conveyor);
        }

        Placement output_storage{exit_x - 1, exit_y, TileType::Storage,
            MachineType::Food, 0, 0, 0, 8.0f, true};
        set_placement(P, output_storage.x, output_storage.y, output_storage);
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
    // EatingZone: a large communal eating space at map center.
    // Pre-built (part of the factory's initial infrastructure).
    // Size: 5x3 to 7x4 tiles — enough for 8-12 agents to gather.
    // Includes 1-2 Storage tiles at the edge for food access.
    // ================================================================

    void place_eating_zone(std::vector<Placement>& P) {
        int mid_x = width_ / 2;
        int mid_y = height_ / 2;

        // Random size: wide enough for communal eating
        std::uniform_int_distribution<int> rx(2, 3);  // half-width
        std::uniform_int_distribution<int> ry(1, 2);  // half-height
        int hw = rx(rng_);
        int hh = ry(rng_);

        // Stamp EatingZone tiles
        int placed = 0;
        for (int dy = -hh; dy <= hh; dy++)
            for (int dx = -hw; dx <= hw; dx++) {
                int px = mid_x + dx;
                int py = mid_y + dy;
                if (px > 1 && px < width_ - 2 && py > 1 && py < height_ - 2) {
                    TileType cur = peek(P, px, py);
                    if (cur == TileType::Floor || cur == TileType::OpenSpace) {
                        set_placement(P, px, py,
                            {px, py, TileType::EatingZone, MachineType::Food,
                             0, 0, 0, 0, true, 0.0f, false, ConveyorDir::E});
                        placed++;
                    }
                }
            }

        if (placed < 3) return;  // too small, skip storage

        // Place 1-2 Storage tiles adjacent to the EatingZone edge.
        // Search outward from the zone perimeter.
        for (int side = 0; side < 2; side++) {
            int sy = mid_y + (side == 0 ? hh + 1 : -(hh + 1));
            for (int dx = -hw; dx <= hw && placed > 0; dx++) {
                int sx = mid_x + dx;
                if (sx > 1 && sx < width_ - 2 && sy > 1 && sy < height_ - 2) {
                    TileType cur = peek(P, sx, sy);
                    if (cur == TileType::Floor || cur == TileType::OpenSpace) {
                        set_placement(P, sx, sy,
                            {sx, sy, TileType::Storage, MachineType::Food,
                             0, 0, 0, 20.0f, true, 0.0f, false, ConveyorDir::E});
                        return;  // one storage is enough
                    }
                }
            }
        }
        // Fallback: try left/right edges
        for (int side = 0; side < 2; side++) {
            int sx = mid_x + (side == 0 ? hw + 1 : -(hw + 1));
            for (int dy = -hh; dy <= hh && placed > 0; dy++) {
                int sy = mid_y + dy;
                if (sx > 1 && sx < width_ - 2 && sy > 1 && sy < height_ - 2) {
                    TileType cur = peek(P, sx, sy);
                    if (cur == TileType::Floor || cur == TileType::OpenSpace) {
                        set_placement(P, sx, sy,
                            {sx, sy, TileType::Storage, MachineType::Food,
                             0, 0, 0, 20.0f, true, 0.0f, false, ConveyorDir::E});
                        return;
                    }
                }
            }
        }
    }

    // ================================================================
    // Additional machines remain agent-built; the inherited chain is deliberately
    // minimal so residents still have room to repair and expand it.
    // ================================================================

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

        // All ScrapPiles placed randomly with min-distance enforcement.
        // NO ScrapPiles near Exit — forces conveyor chains to ALL machines.
        int placed = 0;

        // Fill randomly
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
                 10.0f, 10.0f, 0.15f, 0.0f, false, 0.0f, false, ConveyorDir::E});
            placed++;
        }
    }
};
