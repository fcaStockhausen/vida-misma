#pragma once

#include "components.h"
#include "wfc_generator.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <queue>

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
        // Conveyors are walkable — agents walk over conveyor belts.
        // The factory is already hostile; conveyors shouldn't create impassable walls.
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
                if (prefer_output && data_at(x, y).machine_type == MachineType::Output) {
                    score = 80;
                    // Bonus for OutputMachines near Exit (output pipeline)
                    auto exits = find_all(TileType::Exit);
                    for (auto& [ex, ey] : exits) {
                        int d_exit = std::abs(x - ex) + std::abs(y - ey);
                        if (d_exit <= 3) score += 50;      // right next to Exit: highest priority
                        else if (d_exit <= 8) score += 20;  // nearby: good
                    }
                }
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
        // Find all Exit tiles for proximity scoring
        auto exits = find_all(TileType::Exit);
        int best_score = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) == TileType::ScrapPile) {
                    const auto& td = data_at(x, y);
                    // Skip tiles claimed by other agents
                    if (td.claimed_by >= 0 && td.claimed_by != agent_id) continue;
                    int d_agent = std::abs(x - fx) + std::abs(y - fy);
                    // Score: prioritize ScrapPiles near Exit so OutputMachines
                    // can feed Exit-adjacent Storage directly (no conveyors needed).
                    int d_exit = 999999;
                    if (!exits.empty())
                        for (auto& [ex, ey] : exits)
                            d_exit = std::min(d_exit, std::abs(x - ex) + std::abs(y - ey));
                    // Heavy weight on Exit proximity: d_exit * 2 makes near-Exit piles
                    // win even when far from the agent.
                    int score = d_agent / 2 + d_exit * 2;
                    if (score < best_score) {
                        best_score = score;
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

    // Manhattan distance from (x,y) to the nearest built Materials machine.
    // Returns a large sentinel if none exist.
    int dist_to_nearest_materials_machine(int x, int y) const {
        int best = 999999;
        for (int yy = 0; yy < height_; yy++)
            for (int xx = 0; xx < width_; xx++) {
                if (at(xx, yy) != TileType::Machine || !data_at(xx, yy).built) continue;
                if (data_at(xx, yy).machine_type != MachineType::Materials) continue;
                int d = std::abs(xx - x) + std::abs(yy - y);
                if (d < best) best = d;
            }
        return best;
    }

    // Find a strategic Floor tile for a new Output machine, closest to (fx, fy).
    // Placement strategy: minimize (dist to nearest Materials machine + dist to
    // nearest Exit). This puts the Output between its c_mat source (Materials)
    // and its output destination (Exit) — short hauling legs on both sides.
    // Filters: Floor tile, not already adjacent to a built Output machine
    // (avoid clustering), and a Materials machine or Exit must exist.
    // Returns {-1,-1} if no valid site.
    std::pair<int,int> find_output_machine_site(int fx, int fy) const {
        auto exits = find_all(TileType::Exit);
        bool has_materials = false;
        for (int yy = 0; yy < height_ && !has_materials; yy++)
            for (int xx = 0; xx < width_ && !has_materials; xx++)
                if (at(xx, yy) == TileType::Machine && data_at(xx, yy).built &&
                    data_at(xx, yy).machine_type == MachineType::Materials)
                    has_materials = true;
        if (exits.empty() && !has_materials) return {-1, -1};

        int best_score = 999999;
        int best_dist = 999999;
        std::pair<int,int> best = {-1, -1};
        for (int y = 1; y < height_ - 1; y++)
            for (int x = 1; x < width_ - 1; x++) {
                if (at(x, y) != TileType::Floor) continue;
                // Skip if adjacent to an existing built Output machine (no clustering)
                bool adj_output = false;
                for (int dy = -1; dy <= 1 && !adj_output; dy++)
                    for (int dx = -1; dx <= 1 && !adj_output; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                        if (at(nx, ny) == TileType::Machine && data_at(nx, ny).built &&
                            data_at(nx, ny).machine_type == MachineType::Output)
                            adj_output = true;
                    }
                if (adj_output) continue;

                int d_mat = has_materials ? dist_to_nearest_materials_machine(x, y) : 0;
                int d_exit = 999999;
                for (auto& [ex, ey] : exits) {
                    int d = std::abs(x - ex) + std::abs(y - ey);
                    if (d < d_exit) d_exit = d;
                }
                if (!exits.empty() && d_exit >= 999999) continue;  // safety
                // score: sum of both legs. If one side is missing, weight the other.
                int score = d_mat + (exits.empty() ? 0 : d_exit);
                int agent_dist = std::abs(x - fx) + std::abs(y - fy);
                // Tiebreak: prefer tiles closer to the agent (so agents spread out)
                if (score < best_score || (score == best_score && agent_dist < best_dist)) {
                    best_score = score;
                    best_dist = agent_dist;
                    best = {x, y};
                }
            }
        return best;
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

    // Trace from a machine's adjacent built conveyors, following belt flow direction.
    // Returns true if the conveyor chain reaches an Exit or Exit-adjacent (r<=3) Storage.
    // This determines whether an Output machine is genuinely "served" by logistics.
    bool machine_connected_to_exit(int mach_x, int mach_y) const {
        auto exits = find_all(TileType::Exit);
        if (exits.empty()) return false;

        auto is_exit_target = [&](int x, int y) -> bool {
            if (x < 0 || x >= width_ || y < 0 || y >= height_) return false;
            TileType t = at(x, y);
            if (t == TileType::Exit) return true;
            if (t == TileType::Storage && data_at(x, y).built) {
                for (auto& [ex, ey] : exits)
                    if (std::abs(x - ex) + std::abs(y - ey) <= 3) return true;
            }
            return false;
        };

        constexpr int ddx[] = {1, -1, 0, 0};
        constexpr int ddy[] = {0, 0, 1, -1};
        std::vector<std::vector<bool>> visited(height_, std::vector<bool>(width_, false));
        std::queue<std::pair<int,int>> q;
        for (int i = 0; i < 4; i++) {
            int nx = mach_x + ddx[i], ny = mach_y + ddy[i];
            if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
            if (at(nx, ny) == TileType::Conveyor && data_at(nx, ny).built) {
                visited[ny][nx] = true;
                q.push({nx, ny});
            }
        }
        while (!q.empty()) {
            auto [cx, cy] = q.front(); q.pop();
            auto [tx, ty] = conveyor_target(cx, cy);
            if (is_exit_target(tx, ty)) return true;
            if (tx >= 0 && tx < width_ && ty >= 0 && ty < height_ &&
                at(tx, ty) == TileType::Conveyor && data_at(tx, ty).built && !visited[ty][tx]) {
                visited[ty][tx] = true;
                q.push({tx, ty});
            }
        }
        return false;
    }

    // Trace a conveyor chain forward from (start_x, start_y) following each belt's
    // conveyor_dir. Returns true if the chain reaches a built Output machine — i.e.
    // c_mat placed on this belt will actually feed tier 2. Used by the Materials WORK
    // case to decide whether to load c_mat onto a belt (vs keeping it in inventory).
    // Cycle-safe via a visited grid; bounded by grid area.
    bool conveyor_reaches_output(int start_x, int start_y) const {
        std::vector<std::vector<bool>> visited(height_, std::vector<bool>(width_, false));
        int cx = start_x, cy = start_y;
        // Walk the chain: each belt flows to exactly one neighbor via conveyor_dir.
        for (int steps = 0; steps < width_ * height_; steps++) {
            if (cx < 0 || cx >= width_ || cy < 0 || cy >= height_) return false;
            if (at(cx, cy) != TileType::Conveyor || !data_at(cx, cy).built) return false;
            if (visited[cy][cx]) return false;  // cycle — stop
            visited[cy][cx] = true;
            auto [tx, ty] = conveyor_target(cx, cy);
            if (tx < 0 || tx >= width_ || ty < 0 || ty >= height_) return false;
            TileType tt = at(tx, ty);
            if (tt == TileType::Machine && data_at(tx, ty).built &&
                data_at(tx, ty).machine_type == MachineType::Output) return true;
            // Continue only if the target is another built conveyor
            if (tt != TileType::Conveyor || !data_at(tx, ty).built) return false;
            cx = tx; cy = ty;
        }
        return false;
    }

    // Find a Floor tile adjacent to agent where a new conveyor should be placed.
    // Strategy: extend a chain from the nearest built machine (that has no adjacent
    // built conveyor flowing away) toward the nearest Storage or Exit.
    // Returns {x, y, direction} or {-1, -1, N} if no valid site.
    struct ConveyorSite { int x, y; ConveyorDir dir; };
    ConveyorSite find_conveyor_build_site(int agent_x, int agent_y) const {
        // Strategy: BFS from machines to Storage/Exit, place full chain of frames.
        // Guard: don't create new chains if there are too many pending frames
        // or too many conveyors total. Let agents finish what's in progress.
        int unbuilt_conv = 0, total_conv = 0;
        for (int y = 0; y < height_; y++)
            for (int x = 0; x < width_; x++) {
                if (at(x, y) != TileType::Conveyor) continue;
                total_conv++;
                if (!data_at(x, y).built) unbuilt_conv++;
            }
        if (unbuilt_conv >= 30 || total_conv >= 100) return {-1, -1, ConveyorDir::E};

        auto exits = find_all(TileType::Exit);

        // 1. Find a built machine lacking a useful adjacent conveyor.
        //    Priority: Output > Materials > Food (output quota is the critical path).
        //    Output machines are only "served" if their conveyor chain reaches the Exit.
        int mach_x = -1, mach_y = -1, mach_dist = 999999;
        int best_priority = -1;  // 2=Output, 1=Materials, 0=Food
        for (int y = 1; y < height_ - 1; y++)
            for (int x = 1; x < width_ - 1; x++) {
                if (at(x, y) != TileType::Machine) continue;
                const auto& d = data_at(x, y);
                if (!d.built) continue;
                bool served;
                if (d.machine_type == MachineType::Output) {
                    served = machine_connected_to_exit(x, y);
                } else {
                    served = false;
                    for (int dy2 = -1; dy2 <= 1 && !served; dy2++)
                        for (int dx2 = -1; dx2 <= 1 && !served; dx2++) {
                            if (dx2 == 0 && dy2 == 0) continue;
                            int nx = x + dx2, ny = y + dy2;
                            if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                            if (at(nx, ny) == TileType::Conveyor && data_at(nx, ny).built)
                                served = true;
                        }
                }
                if (served) continue;
                // Reserve conveyor budget for Output chains (the quota-critical path).
                // Food/Materials deposit directly to adjacent Storage — conveyors are bonus.
                if (d.machine_type != MachineType::Output && total_conv >= 20) continue;
                int priority = (d.machine_type == MachineType::Output) ? 2 :
                               (d.machine_type == MachineType::Materials) ? 1 : 0;
                int dist = std::abs(x - agent_x) + std::abs(y - agent_y);
                // Higher priority machine wins regardless of distance.
                // Same priority: closer wins.
                if (priority > best_priority || (priority == best_priority && dist < mach_dist)) {
                    mach_dist = dist; mach_x = x; mach_y = y;
                    best_priority = priority;
                }
            }

        if (mach_x < 0) return {-1, -1, ConveyorDir::E};

        bool is_output = (data_at(mach_x, mach_y).machine_type == MachineType::Output);


        // 2. BFS from machine toward goal.
        //    Output machines: goal = Exit or Exit-adjacent (r<=3) Storage ONLY.
        //    Food/Materials:  goal = any Storage or Exit.
        //    Walkable tiles for BFS: Floor and existing unbuilt Conveyor frames.
        struct Node { int x, y, from_x, from_y; };
        std::queue<Node> q;
        std::vector<std::vector<bool>> visited(height_, std::vector<bool>(width_, false));
        std::vector<std::vector<std::pair<int,int>>> parent(height_,
            std::vector<std::pair<int,int>>(width_, {-1, -1}));

        // Seed BFS from walkable tiles within radius 3 of the machine.
        // Radius 3 matches deposit_to_adjacent_conveyor's search radius, so even
        // boxed-in machines (surrounded by Storage/Machines) can start a chain.
        constexpr int ddx[] = {1, -1, 0, 0};
        constexpr int ddy[] = {0, 0, 1, -1};
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = mach_x + dx, ny = mach_y + dy;
                if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                TileType tt = at(nx, ny);
                if (tt == TileType::Exit) {
                    return {-1, -1, ConveyorDir::E};  // accessible Exit — no conveyor needed
                }
                if (tt == TileType::Storage && data_at(nx, ny).built) {
                    if (!is_output)
                        return {-1, -1, ConveyorDir::E};  // Food/Mat: nearby storage suffices
                    // Output: only skip if this Storage is Exit-adjacent
                    bool near_exit = false;
                    for (auto& [ex, ey] : exits)
                        if (std::abs(nx - ex) + std::abs(ny - ey) <= 3) { near_exit = true; break; }
                    if (near_exit) return {-1, -1, ConveyorDir::E};
                    continue;  // intermediate Storage — not a seed tile
                }
                if (tt == TileType::Floor ||
                    (tt == TileType::Conveyor && !data_at(nx, ny).built) ||
                    (is_output && tt == TileType::Conveyor && data_at(nx, ny).built)) {
                    if (!visited[ny][nx]) {
                        visited[ny][nx] = true;
                        parent[ny][nx] = {mach_x, mach_y};
                        q.push({nx, ny, mach_x, mach_y});
                    }
                }
            }

        int goal_x = -1, goal_y = -1;
        while (!q.empty()) {
            auto [cx, cy, fx, fy] = q.front(); q.pop();
            // Check if we reached a tile adjacent to a valid goal.
            // Output: only Exit-adjacent (r<=3) Storage or Exit.
            // Others: any built Storage or Exit.
            for (int i = 0; i < 4; i++) {
                int nx = cx + ddx[i], ny = cy + ddy[i];
                if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                TileType tt = at(nx, ny);
                if (tt == TileType::Exit) {
                    goal_x = cx; goal_y = cy;
                    goto bfs_done;
                }
                if (tt == TileType::Storage && data_at(nx, ny).built) {
                    if (!is_output) {
                        goal_x = cx; goal_y = cy;
                        goto bfs_done;
                    }
                    for (auto& [ex, ey] : exits)
                        if (std::abs(nx - ex) + std::abs(ny - ey) <= 3) {
                            goal_x = cx; goal_y = cy;
                            goto bfs_done;
                        }
                }
            }
            for (int i = 0; i < 4; i++) {
                int nx = cx + ddx[i], ny = cy + ddy[i];
                if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                if (visited[ny][nx]) continue;
                TileType tt = at(nx, ny);
                if (tt == TileType::Floor ||
                    (tt == TileType::Conveyor && !data_at(nx, ny).built) ||
                    (is_output && tt == TileType::Conveyor && data_at(nx, ny).built)) {
                    visited[ny][nx] = true;
                    parent[ny][nx] = {cx, cy};
                    q.push({nx, ny, cx, cy});
                }
            }
        }
        bfs_done:

        if (goal_x < 0) return {-1, -1, ConveyorDir::E};

        // 3. Trace path back from goal to machine, collect tiles
        std::vector<std::pair<int,int>> path;
        int px = goal_x, py = goal_y;
        while (px != mach_x || py != mach_y) {
            path.push_back({px, py});
            auto [ppx, ppy] = parent[py][px];
            if (ppx < 0) break;  // safety
            px = px; py = py;
            // use parent directly
            int opx = px, opy = py;
            px = parent[opy][opx].first;
            py = parent[opy][opx].second;
            if (px == opx && py == opy) break;  // infinite loop guard
        }
        std::reverse(path.begin(), path.end());

        if (path.empty()) return {-1, -1, ConveyorDir::E};

        // 4. Place all unbuilt conveyor frames along the path
        //    Skip tiles that are already Conveyor (unbuilt frames)
        //    Respect cap: stop placing if we exceed limits
        Grid* self = const_cast<Grid*>(this);
        for (int i = 0; i < (int)path.size(); i++) {
            // Re-check cap during placement
            if (total_conv >= 100) break;
            auto [tx, ty] = path[i];
            if (at(tx, ty) == TileType::Conveyor) continue;  // already a frame
            total_conv++;  // track increment

            // Determine direction: toward next tile, or toward goal
            ConveyorDir dir = ConveyorDir::E;
            if (i + 1 < (int)path.size()) {
                int nx2 = path[i+1].first, ny2 = path[i+1].second;
                int dx2 = nx2 - tx, dy2 = ny2 - ty;
                if (std::abs(dx2) >= std::abs(dy2))
                    dir = (dx2 > 0) ? ConveyorDir::E : ConveyorDir::W;
                else
                    dir = (dy2 > 0) ? ConveyorDir::S : ConveyorDir::N;
            } else {
                // Last tile: direction toward goal neighbor (Storage/Exit)
                int best_d = 999999;
                for (int di = 0; di < 4; di++) {
                    int ax = tx + ddx[di], ay = ty + ddy[di];
                    if (ax < 0 || ax >= width_ || ay < 0 || ay >= height_) continue;
                    TileType tt = at(ax, ay);
                    if ((tt == TileType::Storage && data_at(ax, ay).built) || tt == TileType::Exit) {
                        int d = std::abs(ax - goal_x) + std::abs(ay - goal_y);
                        if (d < best_d) {
                            best_d = d;
                            dir = (ddx[di] > 0) ? ConveyorDir::E :
                                  (ddx[di] < 0) ? ConveyorDir::W :
                                  (ddy[di] > 0) ? ConveyorDir::S : ConveyorDir::N;
                        }
                    }
                }
            }
            self->place_new_conveyor(tx, ty, dir, 0.15f);
        }

        // 5. Return the frame closest to the agent for building
        int best_dist = 999999;
        ConveyorSite best = {-1, -1, ConveyorDir::E};
        for (auto [tx, ty] : path) {
            if (at(tx, ty) == TileType::Conveyor && !data_at(tx, ty).built) {
                int d = std::abs(tx - agent_x) + std::abs(ty - agent_y);
                if (d < best_dist) {
                    best_dist = d;
                    best = {tx, ty, data_at(tx, ty).conveyor_dir};
                }
            }
        }
        return best;
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
                d.built              = true;   // pre-placed infrastructure
                d.storage_capacity = p.storage_capacity > 0.0f ? p.storage_capacity : 20.0f;
                d.stored_food         = 0.0f;
                d.stored_raw_food     = 0.0f;
                d.stored_raw_material = 0.0f;
                d.stored_output       = 0.0f;
            }
            if (p.type == TileType::EatingZone) {
                d.built          = p.built;   // pre-placed EatingZone is built
                d.build_progress = p.built ? 1.0f : 0.0f;
                d.build_cost     = p.build_cost > 0.0f ? p.build_cost : 2.0f;
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
