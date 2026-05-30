#pragma once

#include "components.h"
#include <vector>
#include <algorithm>
#include <cmath>

class Grid {
public:
    Grid(int w, int h)
        : width_(w)
        , height_(h)
        , tiles_(w * h, TileType::Floor)
        , tile_data_(w * h)
    {}

    int width() const  { return width_; }
    int height() const { return height_; }

    // --- Tile access ---

    TileType at(int x, int y) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return TileType::Wall;
        return tiles_[y * width_ + x];
    }

    void set(int x, int y, TileType t) {
        if (x >= 0 && x < width_ && y >= 0 && y < height_) {
            tiles_[y * width_ + x] = t;
        }
    }

    bool is_walkable(int x, int y) const {
        TileType t = at(x, y);
        return t != TileType::Wall;
    }

    // --- Tile data access ---

    TileData& data_at(int x, int y) {
        return tile_data_[y * width_ + x];
    }

    const TileData& data_at(int x, int y) const {
        return tile_data_[y * width_ + x];
    }

    // --- Queries ---

    // Find all tiles of a given type
    std::vector<std::pair<int,int>> find_all(TileType type) const {
        std::vector<std::pair<int,int>> result;
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++)
                if (at(x, y) == type)
                    result.push_back({x, y});
        return result;
    }

    // Find nearest tile of a given type from (fx, fy)
    std::pair<int,int> find_nearest(TileType type, int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != type) continue;
                // Skip exhausted resource sources
                if (type == TileType::FoodSource || type == TileType::ScrapPile) {
                    if (data_at(x, y).resource_amount < 0.01f) continue;
                }
                // Skip built machines when looking for unbuilt
                if (type == TileType::Machine) {
                    // Caller filters by build state externally
                }
                int d = std::abs(x - fx) + std::abs(y - fy);
                if (d < best_dist) {
                    best_dist = d;
                    best = {x, y};
                }
            }
        return best;
    }

    // Find nearest operational machine
    std::pair<int,int> find_nearest_built_machine(int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Machine) continue;
                if (!data_at(x, y).built) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                if (d < best_dist) {
                    best_dist = d;
                    best = {x, y};
                }
            }
        return best;
    }

    // Find nearest unbuilt machine
    std::pair<int,int> find_nearest_unbuilt_machine(int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Machine) continue;
                if (data_at(x, y).built) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                if (d < best_dist) {
                    best_dist = d;
                    best = {x, y};
                }
            }
        return best;
    }

    // EatingZone helpers
    int min_distance_to_built_machine(int fx, int fy) const {
        int best = 999999;
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Machine) continue;
                if (!data_at(x, y).built) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                if (d < best) best = d;
            }
        return best;
    }

    int min_distance_to_any_machine(int fx, int fy) const {
        int best = 999999;
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Machine) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                if (d < best) best = d;
            }
        return best;
    }

    // From (fx, fy), find the closest Floor tile whose Manhattan distance to any
    // Machine is at least min_dist. Used by agents who decide to build a new EatingZone.
    std::pair<int,int> find_nearest_valid_eatingzone_site(int fx, int fy, int min_dist) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 1; y < height_ - 1; y++)
            for (int x = 1; x < width_ - 1; x++) {
                if (at(x, y) != TileType::Floor) continue;
                if (min_distance_to_any_machine(x, y) < min_dist) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                if (d < best_dist) {
                    best_dist = d;
                    best = {x, y};
                }
            }
        return best;
    }

    std::pair<int,int> find_nearest_unbuilt_eatingzone(int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::EatingZone) continue;
                if (data_at(x, y).built) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                if (d < best_dist) {
                    best_dist = d;
                    best = {x, y};
                }
            }
        return best;
    }

    std::pair<int,int> find_nearest_built_eatingzone(int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::EatingZone) continue;
                if (!data_at(x, y).built) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                if (d < best_dist) {
                    best_dist = d;
                    best = {x, y};
                }
            }
        return best;
    }

    int built_eatingzone_count() const {
        int n = 0;
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) == TileType::EatingZone && data_at(x, y).built) n++;
            }
        return n;
    }

    // Find nearest storage with food
    std::pair<int,int> find_nearest_storage_with_food(int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Storage) continue;
                const auto& d = data_at(x, y);
                if (d.stored_food < 0.01f && d.stored_raw_food < 0.01f) continue;
                int dist = std::abs(x - fx) + std::abs(y - fy);
                if (dist < best_dist) {
                    best_dist = dist;
                    best = {x, y};
                }
            }
        return best;
    }

    // --- Factory Layout ---

    void generate_default() {
        // Clear everything
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                set(x, y, TileType::Floor);
            }

        // Perimeter walls
        for (int x = 0; x < width_; x++) {
            set(x, 0, TileType::Wall);
            set(x, height_ - 1, TileType::Wall);
        }
        for (int y = 0; y < height_; y++) {
            set(0, y, TileType::Wall);
            set(width_ - 1, y, TileType::Wall);
        }

        // Entrances (left wall, middle)
        int mid_y = height_ / 2;
        set(0, mid_y - 1, TileType::Entrance);
        set(0, mid_y,     TileType::Entrance);
        set(0, mid_y + 1, TileType::Entrance);

        // Exits (right wall, middle)
        set(width_ - 1, mid_y - 1, TileType::Exit);
        set(width_ - 1, mid_y,     TileType::Exit);
        set(width_ - 1, mid_y + 1, TileType::Exit);

        // NOTE: FoodSource tiles intentionally removed. In this model, food is only
        // produced by Machines (industrial pathway). Agents who don't operate machines
        // can't sustain themselves — that is what creates the cooperation pressure.

        // === SCRAP PILES (raw material, finite) ===
        // North corridor
        place_scrap_pile(width_ / 2 - 2, 3);
        place_scrap_pile(width_ / 2, 3);
        place_scrap_pile(width_ / 2 + 2, 3);

        // South corridor
        place_scrap_pile(width_ / 2 - 2, height_ - 4);
        place_scrap_pile(width_ / 2, height_ - 4);
        place_scrap_pile(width_ / 2 + 2, height_ - 4);

        // West mid
        place_scrap_pile(3, mid_y - 5);
        place_scrap_pile(3, mid_y + 5);

        // East mid
        place_scrap_pile(width_ - 4, mid_y - 5);
        place_scrap_pile(width_ - 4, mid_y + 5);

        // === MACHINE FRAMES (unbuilt, need construction) ===
        // North factory cluster
        int m1x = width_ / 4;
        int m1y = height_ / 4;
        place_machine(m1x - 1, m1y - 1);
        place_machine(m1x + 1, m1y - 1);
        place_machine(m1x - 1, m1y + 1);
        place_machine(m1x + 1, m1y + 1);

        // South factory cluster
        int m2x = width_ / 4;
        int m2y = 3 * height_ / 4;
        place_machine(m2x - 1, m2y - 1);
        place_machine(m2x + 1, m2y - 1);
        place_machine(m2x - 1, m2y + 1);
        place_machine(m2x + 1, m2y + 1);

        // NE factory cluster
        int m3x = 3 * width_ / 4;
        int m3y = height_ / 4;
        place_machine(m3x - 1, m3y - 1);
        place_machine(m3x + 1, m3y - 1);
        place_machine(m3x - 1, m3y + 1);
        place_machine(m3x + 1, m3y + 1);

        // SE factory cluster
        int m4x = 3 * width_ / 4;
        int m4y = 3 * height_ / 4;
        place_machine(m4x - 1, m4y - 1);
        place_machine(m4x + 1, m4y - 1);
        place_machine(m4x - 1, m4y + 1);
        place_machine(m4x + 1, m4y + 1);

        // === STORAGE BAYS — placed at (mx, my±2) so they are 8-adjacent to
        // all 4 machines in a cluster. This is what makes WORK/EAT actually
        // reach the storage helpers (3x3 neighborhood).
        place_storage(m1x, m1y - 2);
        place_storage(m1x, m1y + 2);

        place_storage(m2x, m2y - 2);
        place_storage(m2x, m2y + 2);

        place_storage(m3x, m3y - 2);
        place_storage(m3x, m3y + 2);

        place_storage(m4x, m4y - 2);
        place_storage(m4x, m4y + 2);

        // Central storage (mid-grid hub)
        place_storage(width_ / 2 - 2, mid_y);
        place_storage(width_ / 2 + 2, mid_y);

        // === OPEN SPACES (social/creative) ===
        // Center area
        int ox = width_ / 2;
        int oy = mid_y;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                int px = ox + dx, py = oy + dy;
                if (at(px, py) == TileType::Floor)
                    set(px, py, TileType::OpenSpace);
            }

        // No internal walls for now -- open floor plan
        // This allows simple greedy pathfinding to work.
        // Internal walls will be added back once A* pathfinding is implemented.
    }

private:
    int width_;
    int height_;
    std::vector<TileType> tiles_;
    std::vector<TileData> tile_data_;

    void place_food_source(int x, int y) {
        set(x, y, TileType::FoodSource);
        auto& d = data_at(x, y);
        d.resource_amount = 5.0f;
        d.resource_max    = 8.0f;
        d.resource_regen  = 0.02f;  // regenerates: ~250 ticks to refill
    }

    void place_scrap_pile(int x, int y) {
        set(x, y, TileType::ScrapPile);
        auto& d = data_at(x, y);
        d.resource_amount = 5.0f;
        d.resource_max    = 5.0f;
        d.resource_regen  = 0.001f;  // very slow regen
    }

    void place_machine(int x, int y) {
        set(x, y, TileType::Machine);
        auto& d = data_at(x, y);
        d.built          = false;
        d.build_progress = 0.0f;
        d.build_cost     = 3.0f;  // solo: ~25 ticks; two builders: ~12; four: ~6.
                                  // Build is NOT instantaneous so collaboration matters
                                  // and agents can pause to attend survival needs.
    }

    void place_storage(int x, int y) {
        set(x, y, TileType::Storage);
        auto& d = data_at(x, y);
        d.storage_capacity    = 20.0f;
        d.stored_food         = 0.0f;
        d.stored_raw_food     = 0.0f;
        d.stored_raw_material = 0.0f;
    }
};
