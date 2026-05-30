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
        if (t == TileType::Wall) return false;
        // Unbuilt conveyor frames are walkable (construction markers).
        // Only built conveyors block movement (physical infrastructure).
        if (t == TileType::Conveyor && data_at(x, y).built) return false;
        return true;
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

    // --- Conveyor helpers ---

    // Find nearest unbuilt or degraded conveyor
    std::pair<int,int> find_nearest_conveyor_to_build(int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Conveyor) continue;
                const auto& d = data_at(x, y);
                if (d.built && d.conveyor_condition > 0.3f) continue; // OK, skip
                int dist = std::abs(x - fx) + std::abs(y - fy);
                if (dist < best_dist) { best_dist = dist; best = {x, y}; }
            }
        return best;
    }

    // Find nearest conveyor needing maintenance (condition < threshold)
    std::pair<int,int> find_nearest_conveyor_needing_maintain(
        int fx, int fy, float threshold = 0.7f) const
    {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Conveyor) continue;
                const auto& d = data_at(x, y);
                if (!d.built) continue;
                if (d.conveyor_condition >= threshold) continue;
                int dist = std::abs(x - fx) + std::abs(y - fy);
                if (dist < best_dist) { best_dist = dist; best = {x, y}; }
            }
        return best;
    }

    // Get the neighbor coordinates that a conveyor at (x,y) flows into
    std::pair<int,int> conveyor_target(int x, int y) const {
        const auto& d = data_at(x, y);
        switch (d.conveyor_dir) {
            case ConveyorDir::N: return {x, y - 1};
            case ConveyorDir::S: return {x, y + 1};
            case ConveyorDir::E: return {x + 1, y};
            case ConveyorDir::W: return {x - 1, y};
        }
        return {x, y};
    }

    // Auto-detect direction: point toward the nearest important tile type
    ConveyorDir auto_direction(int x, int y) const {
        // Check 4 neighbors for important tiles, prefer: Exit > Storage > Machine > Conveyor
        struct Candidate { int dx, dy; int priority; };
        Candidate best = {0, 0, -1};
        auto check = [&](int dx, int dy, int prio) {
            int nx = x + dx, ny = y + dy;
            TileType t = at(nx, ny);
            int p = -1;
            if (t == TileType::Exit) p = 4;
            else if (t == TileType::Storage) p = 3;
            else if (t == TileType::Machine && data_at(nx, ny).built) p = 2;
            else if (t == TileType::Conveyor && data_at(nx, ny).built) p = 1;
            if (p > best.priority) {
                best = {dx, dy, p};
            }
        };
        check(0, -1, 0); check(0, 1, 0); check(1, 0, 0); check(-1, 0, 0);
        if (best.dy < 0) return ConveyorDir::N;
        if (best.dy > 0) return ConveyorDir::S;
        if (best.dx > 0) return ConveyorDir::E;
        return ConveyorDir::W;
    }

    int conveyor_count() const {
        int n = 0;
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++)
                if (at(x, y) == TileType::Conveyor) n++;
        return n;
    }

    int built_conveyor_count() const {
        int n = 0;
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++)
                if (at(x, y) == TileType::Conveyor && data_at(x, y).built) n++;
        return n;
    }

    // Find a walkable tile in the 4-neighborhood of (target_x, target_y)
    // closest to (from_x, from_y). Used when the target tile itself is not walkable
    // (e.g., Conveyor) and the agent needs to stand beside it.
    std::pair<int,int> find_walkable_adjacent_to(
        int target_x, int target_y, int from_x, int from_y) const
    {
        constexpr int dx[] = {1, -1, 0, 0};
        constexpr int dy[] = {0, 0, 1, -1};
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = target_x + dx[d], ny = target_y + dy[d];
            if (!is_walkable(nx, ny)) continue;
            int dist = std::abs(nx - from_x) + std::abs(ny - from_y);
            if (dist < best_dist) {
                best_dist = dist;
                best = {nx, ny};
            }
        }
        return best;
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

    // Find the nearest built conveyor that is a "dead end" — its flow target
    // is not a useful tile (not Storage, Exit, or another built conveyor).
    // Agents may want to dismantle these to reuse the material elsewhere.
    std::pair<int,int> find_nearest_dead_end_conveyor(int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Conveyor) continue;
                const auto& d = data_at(x, y);
                if (!d.built) continue;
                // Check if this conveyor's flow target is useful
                auto [tx, ty] = conveyor_target(x, y);
                if (tx < 0 || tx >= width_ || ty < 0 || ty >= height_) continue; // edge of map
                TileType tt = at(tx, ty);
                bool useful = (tt == TileType::Storage || tt == TileType::Exit ||
                              (tt == TileType::Conveyor && data_at(tx, ty).built));
                if (useful) continue; // this conveyor flows somewhere useful
                // Dead end! Check distance
                int dist = std::abs(x - fx) + std::abs(y - fy);
                if (dist < best_dist) {
                    best_dist = dist;
                    best = {x, y};
                }
            }
        return best;
    }

    // Convert a built conveyor back to Floor (dismantle).
    // Returns the build_cost for material refund calculation, or -1 if invalid.
    float dismantle_conveyor(int x, int y) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return -1.0f;
        if (at(x, y) != TileType::Conveyor) return -1.0f;
        auto& d = data_at(x, y);
        if (!d.built) return -1.0f;
        float cost = d.build_cost;
        // Convert back to floor
        tiles_[y * width_ + x] = TileType::Floor;
        // Preserve dismantle tracking in data
        d.built = false;
        d.build_progress = 0.0f;
        d.conveyor_condition = 1.0f;
        d.conveyor_contents = 0.0f;
        return cost;
    }

    // Place a new conveyor frame at a floor tile (for rearranging).
    bool place_new_conveyor(int x, int y, ConveyorDir dir, float cost = 1.5f) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
        if (at(x, y) != TileType::Floor) return false;
        tiles_[y * width_ + x] = TileType::Conveyor;
        auto& d = data_at(x, y);
        d.built = false;
        d.build_progress = 0.0f;
        d.build_cost = cost;
        d.conveyor_dir = dir;
        d.conveyor_condition = 1.0f;
        d.conveyor_contents = 0.0f;
        return true;
    }

    // Check if a built conveyor at (x,y) is blocking a path — specifically,
    // if removing it would connect two walkable regions that are currently separated.
    // Simple heuristic: a built conveyor is "blocking" if it has walkable neighbors
    // on opposite sides (N/S or E/W) that can't reach each other except through it.
    bool is_conveyor_blocking_path(int x, int y) const {
        if (at(x, y) != TileType::Conveyor || !data_at(x, y).built) return false;
        // Check N-S passage: walkable N and walkable S of conveyor
        bool walk_n = (y > 0 && is_walkable(x, y - 1));
        bool walk_s = (y < height_ - 1 && is_walkable(x, y + 1));
        // Check E-W passage
        bool walk_e = (x < width_ - 1 && is_walkable(x + 1, y));
        bool walk_w = (x > 0 && is_walkable(x - 1, y));
        // If walkable on opposite sides, this conveyor blocks passage
        return (walk_n && walk_s) || (walk_e && walk_w);
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

        // Storage adjacent to Exit — required for quota system to drain food.
        // Without this, factory health can never be maintained.
        place_storage(width_ - 2, mid_y);

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
        // North-west cluster: FOOD machines (4)
        int m1x = width_ / 4;
        int m1y = height_ / 4;
        place_machine(m1x - 1, m1y - 1, MachineType::Food);
        place_machine(m1x + 1, m1y - 1, MachineType::Food);
        place_machine(m1x - 1, m1y + 1, MachineType::Food);
        place_machine(m1x + 1, m1y + 1, MachineType::Food);

        // South-west cluster: FOOD machines (4)
        int m2x = width_ / 4;
        int m2y = 3 * height_ / 4;
        place_machine(m2x - 1, m2y - 1, MachineType::Food);
        place_machine(m2x + 1, m2y - 1, MachineType::Food);
        place_machine(m2x - 1, m2y + 1, MachineType::Food);
        place_machine(m2x + 1, m2y + 1, MachineType::Food);

        // North-east cluster: MATERIALS machines (4)
        int m3x = 3 * width_ / 4;
        int m3y = height_ / 4;
        place_machine(m3x - 1, m3y - 1, MachineType::Materials);
        place_machine(m3x + 1, m3y - 1, MachineType::Materials);
        place_machine(m3x - 1, m3y + 1, MachineType::Materials);
        place_machine(m3x + 1, m3y + 1, MachineType::Materials);

        // South-east cluster: OUTPUT machines (4)
        int m4x = 3 * width_ / 4;
        int m4y = 3 * height_ / 4;
        place_machine(m4x - 1, m4y - 1, MachineType::Output);
        place_machine(m4x + 1, m4y - 1, MachineType::Output);
        place_machine(m4x - 1, m4y + 1, MachineType::Output);
        place_machine(m4x + 1, m4y + 1, MachineType::Output);

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

        // === CONVEYOR LINES (production pipelines) ===
        // Connect each factory cluster's storage to the central hub,
        // then from central hub to Exit (right wall).
        //
        // Layout: Machine cluster → Storage → Conveyor chain → Central Storage → Conveyor → Exit
        // Agents BUILD these; placed as unbuilt frames.

        // SE cluster (m4) → East toward Exit
        place_conveyor(m4x + 3, m4y, ConveyorDir::E);
        place_conveyor(m4x + 4, m4y, ConveyorDir::E);
        place_conveyor(m4x + 5, m4y, ConveyorDir::E);

        // NE cluster (m3) → East toward Exit
        place_conveyor(m3x + 3, m3y, ConveyorDir::E);
        place_conveyor(m3x + 4, m3y, ConveyorDir::E);
        place_conveyor(m3x + 5, m3y, ConveyorDir::E);

        // Central hub → Exit (horizontal line at mid_y)
        int hub_x = width_ / 2 + 3;
        for (int cx = hub_x; cx < width_ - 2; cx++) {
            place_conveyor(cx, mid_y, ConveyorDir::E);
        }

        // NW cluster (m1) → South to central hub
        place_conveyor(m1x, m1y + 3, ConveyorDir::S);
        place_conveyor(m1x, m1y + 4, ConveyorDir::S);
        place_conveyor(m1x, m1y + 5, ConveyorDir::S);

        // SW cluster (m2) → North to central hub
        place_conveyor(m2x, m2y - 3, ConveyorDir::N);
        place_conveyor(m2x, m2y - 4, ConveyorDir::N);
        place_conveyor(m2x, m2y - 5, ConveyorDir::N);

        // === PRE-BUILT BOOTSTRAP ===
        // The factory starts with 2 Food machines already built.
        // This represents a minimally operational factory — without it,
        // agents die before building anything (chicken-and-egg problem).
        auto& pb1 = data_at(m1x - 1, m1y - 1);
        pb1.built = true; pb1.build_progress = pb1.build_cost;
        auto& pb2 = data_at(m1x + 1, m1y + 1);
        pb2.built = true; pb2.build_progress = pb2.build_cost;
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

    void place_machine(int x, int y, MachineType mtype = MachineType::Food) {
        set(x, y, TileType::Machine);
        auto& d = data_at(x, y);
        d.built          = false;
        d.build_progress = 0.0f;
        d.build_cost     = 2.0f;
        d.machine_type   = mtype;
    }

    void place_storage(int x, int y) {
        set(x, y, TileType::Storage);
        auto& d = data_at(x, y);
        d.storage_capacity    = 20.0f;
        d.stored_food         = 0.0f;
        d.stored_raw_food     = 0.0f;
        d.stored_raw_material = 0.0f;
    }

    void place_conveyor(int x, int y, ConveyorDir dir) {
        set(x, y, TileType::Conveyor);
        auto& d = data_at(x, y);
        d.built = false;
        d.build_progress = 0.0f;
        d.build_cost = 1.5f;  // cheaper than machines
        d.conveyor_dir = dir;
        d.conveyor_condition = 1.0f;
        d.conveyor_contents = 0.0f;
    }
};
