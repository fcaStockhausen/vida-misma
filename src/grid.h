#pragma once

#include "components.h"
#include "wfc_generator.h"
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

    // Find nearest operational machine with preference scoring.
    // Pattern from RimWorld/ONI: preference score DOMINATES distance.
    // A preferred machine 20 tiles away beats a non-preferred one 5 tiles away.
    // This prevents all agents from converging on the single nearest machine.
    std::pair<int,int> find_nearest_built_machine(int fx, int fy,
        bool prefer_food = false, bool prefer_output = false,
        bool prefer_materials = false) const {
        int best_dist = 999999;
        int best_score = -1;  // start at -1 so ANY machine beats "no preference"
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Machine) continue;
                if (!data_at(x, y).built) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                int score = 1;  // base: any built machine
                if (prefer_food && data_at(x, y).machine_type == MachineType::Food)
                    score = 100;
                if (prefer_output && data_at(x, y).machine_type == MachineType::Output)
                    score = 80;
                if (prefer_materials && data_at(x, y).machine_type == MachineType::Materials)
                    score = 70;
                // Soft claim penalty (RimWorld/ONI pattern):
                // Machines claimed by other agents get -30 score.
                // This diversifies agents across machines while still allowing
                // sharing when the machine really needs workers.
                if (data_at(x, y).claimed_by >= 0) {
                    score -= 30;
                }
                // Score dominates: preferred machine always wins over closer non-preferred
                if (score > best_score || (score == best_score && d < best_dist)) {
                    best_dist = d;
                    best_score = score;
                    best = {x, y};
                }
            }
        return best;
    }

    // Find nearest unbuilt machine
    std::pair<int,int> find_nearest_unbuilt_machine(int fx, int fy) const {
        int best_dist = 999999;
        int best_storage = 0;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Machine) continue;
                if (data_at(x, y).built) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                int nearby_storage = 0;
                for (int dy = -3; dy <= 3; dy++)
                    for (int dx = -3; dx <= 3; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                        if (at(nx, ny) == TileType::Storage) nearby_storage++;
                    }
                // Prefer closer; break ties by storage adjacency
                if (d < best_dist || (d == best_dist && nearby_storage > best_storage)) {
                    best_dist = d;
                    best_storage = nearby_storage;
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

    // --- FoodSource helpers ---

    // Find nearest FoodSource tile that hasn't been built over with a machine
    std::pair<int,int> find_nearest_free_foodsource(int fx, int fy, int agent_id = -1) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) == TileType::FoodSource) {
                    const auto& td = data_at(x, y);
                    // Skip tiles claimed by other agents
                    if (td.claimed_by >= 0 && td.claimed_by != agent_id) continue;
                    int d = std::abs(x - fx) + std::abs(y - fy);
                    if (d < best_dist) {
                        best_dist = d;
                        best = {x, y};
                    }
                }
            }
        return best;
    }

    // Find nearest ScrapPile tile that hasn't been built over with a machine
    std::pair<int,int> find_nearest_free_scrappile(int fx, int fy, int agent_id = -1) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) == TileType::ScrapPile) {
                    const auto& td = data_at(x, y);
                    // Skip tiles claimed by other agents
                    if (td.claimed_by >= 0 && td.claimed_by != agent_id) continue;
                    int d = std::abs(x - fx) + std::abs(y - fy);
                    if (d < best_dist) {
                        best_dist = d;
                        best = {x, y};
                    }
                }
            }
        return best;
    }

    // --- Storage helpers ---

    // Count storage tiles within manhattan radius of (cx, cy)
    int count_storage_near(int cx, int cy, int radius = 3) const {
        int n = 0;
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++) {
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                if (at(nx, ny) == TileType::Storage) n++;
            }
        return n;
    }

    // Find a Floor tile adjacent to a built machine that lacks nearby storage.
    // Returns the best Floor tile to build storage on, closest to (fx, fy).
    std::pair<int,int> find_storage_build_site(int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 1; y < height_ - 1; y++)
            for (int x = 1; x < width_ - 1; x++) {
                if (at(x, y) != TileType::Floor) continue;
                // Must be adjacent to a built machine (8-connectivity)
                bool adj_built_machine = false;
                for (int dy = -1; dy <= 1 && !adj_built_machine; dy++)
                    for (int dx = -1; dx <= 1 && !adj_built_machine; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                        if (at(nx, ny) == TileType::Machine && data_at(nx, ny).built)
                            adj_built_machine = true;
                    }
                if (!adj_built_machine) continue;
                // The machine must lack nearby storage
                bool machine_needs_storage = false;
                for (int dy = -1; dy <= 1 && !machine_needs_storage; dy++)
                    for (int dx = -1; dx <= 1 && !machine_needs_storage; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                        if (at(nx, ny) == TileType::Machine && data_at(nx, ny).built) {
                            if (count_storage_near(nx, ny, 2) == 0)
                                machine_needs_storage = true;
                        }
                    }
                if (!machine_needs_storage) continue;
                int d = std::abs(x - fx) + std::abs(y - fy);
                if (d < best_dist) {
                    best_dist = d;
                    best = {x, y};
                }
            }
        return best;
    }

    // --- Conveyor helpers ---

    // Find nearest unbuilt or degraded conveyor
    std::pair<int,int> find_nearest_conveyor_to_build(int fx, int fy) const {
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        // Existing unbuilt or degraded conveyor frames
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Conveyor) continue;
                const auto& d = data_at(x, y);
                if (d.built && d.conveyor_condition > 0.3f) continue; // OK, skip
                int dist = std::abs(x - fx) + std::abs(y - fy);
                if (dist < best_dist) { best_dist = dist; best = {x, y}; }
            }
        // Also check Floor tiles that are good conveyor creation sites
        // (adjacent to built machines that lack conveyor output)
        if (best.first < 0) {
            auto site = find_conveyor_build_site(fx, fy);
            if (site.x >= 0) {
                int dist = std::abs(site.x - fx) + std::abs(site.y - fy);
                if (dist < best_dist) {
                    best_dist = dist;
                    // Agent walks to a Floor tile adjacent to the conveyor site
                    // But the site IS a Floor tile, so return it directly
                    best = {site.x, site.y};
                }
            }
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

    // Find a Floor tile adjacent to agent where a new conveyor should be placed.
    // Strategy: extend a chain from the nearest built machine (that has no adjacent
    // built conveyor flowing away) toward the nearest Storage or Exit.
    // Returns {x, y, direction} or {-1, -1, N} if no valid site.
    struct ConveyorSite { int x, y; ConveyorDir dir; };
    ConveyorSite find_conveyor_build_site(int agent_x, int agent_y) const {
        // 1. Find nearest built machine that needs conveyor connection
        int mach_x = -1, mach_y = -1, mach_dist = 999999;
        for (int y = 1; y < height_ - 1; y++)
            for (int x = 1; x < width_ - 1; x++) {
                if (at(x, y) != TileType::Machine) continue;
                const auto& d = data_at(x, y);
                if (!d.built) continue;
                // Check if machine already has an adjacent built conveyor flowing away
                bool has_output_conveyor = false;
                for (int dy2 = -1; dy2 <= 1 && !has_output_conveyor; dy2++)
                    for (int dx2 = -1; dx2 <= 1 && !has_output_conveyor; dx2++) {
                        if (dx2 == 0 && dy2 == 0) continue;
                        int nx = x + dx2, ny = y + dy2;
                        if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                        if (at(nx, ny) == TileType::Conveyor && data_at(nx, ny).built) {
                            has_output_conveyor = true;
                        }
                    }
                if (has_output_conveyor) continue;
                int dist = std::abs(x - agent_x) + std::abs(y - agent_y);
                if (dist < mach_dist) { mach_dist = dist; mach_x = x; mach_y = y; }
            }

        // 2. Find nearest Storage or Exit as target
        int targ_x = -1, targ_y = -1, targ_dist = 999999;
        for (int y = 1; y < height_ - 1; y++)
            for (int x = 1; x < width_ - 1; x++) {
                TileType t = at(x, y);
                if (t != TileType::Storage && t != TileType::Exit) continue;
                // Only Storage that's built, or Exit
                if (t == TileType::Storage && !data_at(x, y).built) continue;
                int dist = std::abs(x - agent_x) + std::abs(y - agent_y);
                if (dist < targ_dist) { targ_dist = dist; targ_x = x; targ_y = y; }
            }

        // If no machine needs connection or no target, try extending from
        // existing built conveyors toward target
        if (mach_x < 0) {
            // Find a built conveyor that is a dead-end (its target is not useful)
            for (int y = 1; y < height_ - 1; y++)
                for (int x = 1; x < width_ - 1; x++) {
                    if (at(x, y) != TileType::Conveyor) continue;
                    const auto& d = data_at(x, y);
                    if (!d.built) continue;
                    auto [tx, ty] = conveyor_target(x, y);
                    if (tx < 0 || tx >= width_ || ty < 0 || ty >= height_) continue;
                    TileType tt = at(tx, ty);
                    if (tt == TileType::Storage || tt == TileType::Exit ||
                        (tt == TileType::Conveyor && data_at(tx, ty).built))
                        continue;  // flows somewhere useful, skip
                    // Dead-end conveyor! Find adjacent Floor to extend chain
                    int dx = (tx > x) ? 1 : (tx < x) ? -1 : 0;
                    int dy = (ty > y) ? 1 : (ty < y) ? -1 : 0;
                    // Try the dead-end's target direction first
                    for (int ddy = -1; ddy <= 1; ddy++)
                        for (int ddx = -1; ddx <= 1; ddx++) {
                            if (ddx == 0 && ddy == 0) continue;
                            int nx = x + ddx, ny = y + ddy;
                            if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                            if (at(nx, ny) != TileType::Floor) continue;
                            int dist = std::abs(nx - agent_x) + std::abs(ny - agent_y);
                            if (dist < mach_dist) {
                                mach_dist = dist;
                                mach_x = nx; mach_y = ny;
                                // Direction: toward storage/exit or away from dead-end conveyor
                                targ_x = targ_x; // already set above
                            }
                        }
                }
        }

        if (mach_x < 0) return {-1, -1, ConveyorDir::E};

        // 3. Find the best adjacent Floor tile to the machine (or dead-end)
        //    Prefer tiles closer to the target
        // If we found a machine, look at tiles adjacent to the machine
        int best_x = -1, best_y = -1, best_score = 999999;
        int src_x = mach_x, src_y = mach_y;
        // If source is a Floor tile (from dead-end extension), use it directly
        if (at(src_x, src_y) == TileType::Floor) {
            // Compute direction toward target
            int dx = (targ_x > src_x) ? 1 : (targ_x < src_x) ? -1 : 0;
            int dy = (targ_y > src_y) ? 1 : (targ_y < src_y) ? -1 : 0;
            ConveyorDir dir = ConveyorDir::E;
            if (std::abs(dx) >= std::abs(dy)) {
                dir = (dx > 0) ? ConveyorDir::E : ConveyorDir::W;
            } else {
                dir = (dy > 0) ? ConveyorDir::S : ConveyorDir::N;
            }
            return {src_x, src_y, dir};
        }

        // Look at tiles adjacent to the machine
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = src_x + dx, ny = src_y + dy;
                if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                if (at(nx, ny) != TileType::Floor) continue;
                if (targ_x < 0) continue;
                // Score: distance from this tile to target (lower = better)
                int to_target = std::abs(nx - targ_x) + std::abs(ny - targ_y);
                int to_agent = std::abs(nx - agent_x) + std::abs(ny - agent_y);
                int score = to_target * 3 + to_agent;  // prefer toward target, tiebreak by nearness
                if (score < best_score) {
                    best_score = score;
                    best_x = nx; best_y = ny;
                }
            }

        if (best_x < 0) return {-1, -1, ConveyorDir::E};

        // Direction: toward target
        ConveyorDir dir = ConveyorDir::E;
        if (targ_x >= 0) {
            int dx = (targ_x > best_x) ? 1 : (targ_x < best_x) ? -1 : 0;
            int dy = (targ_y > best_y) ? 1 : (targ_y < best_y) ? -1 : 0;
            if (std::abs(dx) >= std::abs(dy)) {
                dir = (dx > 0) ? ConveyorDir::E : ConveyorDir::W;
            } else {
                dir = (dy > 0) ? ConveyorDir::S : ConveyorDir::N;
            }
        }
        return {best_x, best_y, dir};
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

    void generate_wfc(uint32_t seed) {
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                set(x, y, TileType::Floor);
            }

        WFCGenerator wfc(width_, height_, seed);
        auto placements = wfc.generate();

        for (auto& p : placements) {
            set(p.x, p.y, p.type);
            auto& d = data_at(p.x, p.y);

            if (p.type == TileType::ScrapPile || p.type == TileType::FoodSource) {
                d.resource_amount = p.resource_amount;
                d.resource_max    = p.resource_max;
                d.resource_regen  = p.resource_regen;
            }
            if (p.type == TileType::Machine) {
                d.built          = p.built;
                d.build_progress = p.built ? p.build_cost : 0.0f;
                d.build_cost     = p.build_cost > 0.0f ? p.build_cost : 2.0f;
                d.machine_type   = p.machine_type;
            }
            if (p.type == TileType::Storage) {
                d.storage_capacity = p.storage_capacity > 0.0f ? p.storage_capacity : 20.0f;
                d.stored_food         = 0.0f;
                d.stored_raw_food     = 0.0f;
                d.stored_raw_material = 0.0f;
                d.stored_output       = 0.0f;
            }
            if (p.type == TileType::Conveyor) {
                d.built              = false;
                d.build_progress     = 0.0f;
                d.build_cost         = p.build_cost > 0.0f ? p.build_cost : 1.5f;
                d.conveyor_dir       = p.conveyor_dir;
                d.conveyor_condition = 1.0f;
                d.conveyor_contents  = 0.0f;
            }
        }
    }

private:
    int width_;
    int height_;
    std::vector<TileType> tiles_;
    std::vector<TileData> tile_data_;

};
