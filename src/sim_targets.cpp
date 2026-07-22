#include "simulation.h"
#include "production.h"
#include <cstdlib>
#include <limits>

Simulation::PlaceChoice Simulation::find_preferred_place(
    entt::entity entity, ActionType action) const
{
    const auto& origin = registry_.get<PositionComponent>(entity);
    const auto& agent = registry_.get<AgentComponent>(entity);
    const auto& personality = registry_.get<PersonalityComponent>(entity);
    const auto& memory = registry_.get<PlaceMemoryComponent>(entity);

    std::vector<std::pair<int, int>> candidates{{origin.x, origin.y}};
    for (int y = std::max(1, origin.y - OBSERVATION_RADIUS);
         y <= std::min(grid_.height() - 2, origin.y + OBSERVATION_RADIUS); y += 2)
        for (int x = std::max(1, origin.x - OBSERVATION_RADIUS);
             x <= std::min(grid_.width() - 2, origin.x + OBSERVATION_RADIUS); x += 2)
            if (std::abs(x - origin.x) + std::abs(y - origin.y) <= OBSERVATION_RADIUS
                && grid_.is_walkable(x, y)) candidates.push_back({x, y});

    auto agents = registry_.view<PositionComponent, const AgentComponent>();
    for (auto other : agents) {
        if (other == entity || !registry_.get<AgentComponent>(other).alive) continue;
        const auto& pos = registry_.get<PositionComponent>(other);
        if (std::abs(pos.x - origin.x) + std::abs(pos.y - origin.y) <= OBSERVATION_RADIUS
            && grid_.is_walkable(pos.x, pos.y)) candidates.push_back({pos.x, pos.y});
    }
    if (config_.spatial_affinity_enabled) {
        for (const auto& place : memory.places)
            if (place.x >= 0 && grid_.is_walkable(place.x, place.y))
                candidates.push_back({place.x, place.y});
    }

    struct LocalArtifact { int x; int y; float strength; };
    std::vector<LocalArtifact> local_artifacts;
    auto artifacts = registry_.view<PositionComponent, const ArtifactComponent>();
    for (auto artifact : artifacts) {
        const auto& pos = registry_.get<PositionComponent>(artifact);
        if (std::abs(pos.x - origin.x) + std::abs(pos.y - origin.y)
            > OBSERVATION_RADIUS) continue;
        local_artifacts.push_back({pos.x, pos.y,
            registry_.get<ArtifactComponent>(artifact).strength});
        if (grid_.is_walkable(pos.x, pos.y)) candidates.push_back({pos.x, pos.y});
    }

    PlaceChoice best{origin.x, origin.y, -std::numeric_limits<float>::infinity()};
    for (auto [x, y] : candidates) {
        int travel = std::abs(x - origin.x) + std::abs(y - origin.y);
        bool site_observable = travel <= OBSERVATION_RADIUS;
        float affinity = 0.0f;
        if (config_.spatial_affinity_enabled) {
            for (const auto& place : memory.places) {
                int d = std::abs(place.x - x) + std::abs(place.y - y);
                if (d > 2) continue;
                float confidence = std::min(1.0f, place.exposures / 12.0f);
                affinity += place.affinity * confidence / (1.0f + d);
            }
        }

        float traffic = 0.0f;
        float familiar_people = 0.0f;
        int people_in_range = 0;
        for (auto other : agents) {
            if (other == entity || !registry_.get<AgentComponent>(other).alive) continue;
            const auto& other_pos = registry_.get<PositionComponent>(other);
            if (!site_observable
                || std::abs(other_pos.x - origin.x) + std::abs(other_pos.y - origin.y)
                   > OBSERVATION_RADIUS) continue;
            int d = std::abs(other_pos.x - x) + std::abs(other_pos.y - y);
            if (d > 3) continue;
            people_in_range++;
            float proximity = 1.0f - d / 4.0f;
            traffic += proximity;
            int other_id = registry_.get<AgentComponent>(other).id;
            const auto& relationship = social_.get_rel(agent.id, other_id);
            familiar_people += proximity * relationship.familiarity
                * (0.5f + 0.5f * std::max(0.0f, relationship.trust));
        }

        float noise = 0.0f;
        float hazard = 0.0f;
        float food_access = 0.0f;
        for (int gy = std::max(0, y - 3); gy <= std::min(grid_.height() - 1, y + 3); gy++)
            for (int gx = std::max(0, x - 3); gx <= std::min(grid_.width() - 1, x + 3); gx++) {
                int d = std::abs(gx - x) + std::abs(gy - y);
                if (d > 3) continue;
                if (!site_observable
                    || std::abs(gx - origin.x) + std::abs(gy - origin.y)
                       > OBSERVATION_RADIUS) continue;
                float proximity = 1.0f - d / 4.0f;
                TileType tile = grid_.at(gx, gy);
                const auto& data = grid_.data_at(gx, gy);
                if (tile == TileType::Machine && data.built) noise += proximity;
                if (tile == TileType::Conveyor && data.built) {
                    noise += proximity * 0.4f;
                    hazard += proximity * (1.0f - data.conveyor_condition);
                }
                if (tile == TileType::Storage)
                    food_access += proximity * std::min(1.0f,
                        data.stored_food + data.stored_raw_food);
            }

        float artifact_presence = 0.0f;
        for (const auto& artifact : local_artifacts) {
            if (!site_observable
                || std::abs(artifact.x - origin.x) + std::abs(artifact.y - origin.y)
                   > OBSERVATION_RADIUS) continue;
            int d = std::abs(artifact.x - x) + std::abs(artifact.y - y);
            if (d <= 2) artifact_presence += artifact.strength / (1.0f + d);
        }

        if (action == ActionType::SOCIALIZE && people_in_range == 0) continue;
        float score = 0.0f;
        if (action == ActionType::REST) {
            score = affinity * 0.45f - traffic * 0.08f - noise * 0.10f
                  - hazard * 0.20f + food_access * 0.04f
                  + artifact_presence * personality.artistry * 0.05f;
        } else if (action == ActionType::SOCIALIZE) {
            score = affinity * 0.30f + familiar_people * 0.20f
                  + std::min(3.0f, traffic) * 0.04f
                  - std::max(0.0f, traffic - 4.0f) * 0.08f
                  - noise * 0.04f - hazard * 0.15f;
        } else {
            score = affinity * 0.45f + familiar_people * 0.08f
                  + artifact_presence * (0.04f + personality.artistry * 0.08f)
                  - noise * 0.08f - hazard * 0.15f
                  - std::max(0.0f, traffic - 3.0f) * 0.05f;
        }
        score -= travel * 0.015f;

        if (score > best.score
            || (score == best.score && std::pair{x, y} < std::pair{best.x, best.y})) {
            best = {x, y, score};
        }
    }
    return best;
}

bool Simulation::work_target_feasible(entt::entity entity, int x, int y) const {
    const auto& inv = registry_.get<InventoryComponent>(entity);
    TileType tile = grid_.at(x, y);
    if (tile == TileType::Storage) {
        if (inv.output <= 0.01f) return false;
        const auto& storage = grid_.data_at(x, y);
        if (storage.storage_capacity - storage.total_stored() <= 0.001f) return false;
        for (auto [ex, ey] : grid_.find_all(TileType::Exit))
            if (std::abs(x - ex) + std::abs(y - ey) <= 3) return true;
        return false;
    }
    if (tile != TileType::Machine) return false;
    const auto& machine = grid_.data_at(x, y);
    if (!machine.built) return false;

    float available = machine.machine_type == MachineType::Food ? inv.raw_food + machine.stored_raw_food
        : machine.machine_type == MachineType::Materials
            ? inv.raw_material + machine.stored_raw_material
            : inv.construction_material + machine.stored_construction_material;
    for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 3; dx++) {
            int sx = x + dx, sy = y + dy;
            if (grid_.at(sx, sy) != TileType::Storage) continue;
            const auto& storage = grid_.data_at(sx, sy);
            if (machine.machine_type == MachineType::Food) available += storage.stored_raw_food;
            else if (machine.machine_type == MachineType::Materials)
                available += storage.stored_raw_material;
            else available += storage.stored_construction_material;
        }
    if (machine.built_on_resource
        && (machine.resource_amount > 0.001f || machine.resource_regen > 0.0f)) {
        available = std::max(available, config_.machine_input);
    }
    return available > 0.001f;
}

std::pair<int, int> Simulation::find_feasible_work_target(entt::entity entity) const {
    const auto& pos = registry_.get<PositionComponent>(entity);
    const auto& inv = registry_.get<InventoryComponent>(entity);
    const auto& needs = registry_.get<NeedsComponent>(entity);
    const auto& skills = registry_.get<SkillsComponent>(entity);
    int agent_id = registry_.get<AgentComponent>(entity).id;

    if (inv.output > 0.1f && needs.hunger <= 0.5f) {
        int best_dist = 999999;
        std::pair<int, int> best = {-1, -1};
        auto exits = grid_.find_all(TileType::Exit);
        for (int y = std::max(0, pos.y - OBSERVATION_RADIUS);
             y <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); y++)
            for (int x = std::max(0, pos.x - OBSERVATION_RADIUS);
                 x <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); x++) {
                if (grid_.at(x, y) != TileType::Storage) continue;
                const auto& storage = grid_.data_at(x, y);
                if (storage.storage_capacity - storage.total_stored() <= 0.001f) continue;
                bool near_exit = false;
                for (auto [ex, ey] : exits)
                    if (std::abs(x - ex) + std::abs(y - ey) <= 3) {
                        near_exit = true;
                        break;
                    }
                if (!near_exit) continue;
                int distance = std::abs(x - pos.x) + std::abs(y - pos.y);
                if (distance > OBSERVATION_RADIUS) continue;
                if (distance < best_dist) {
                    best_dist = distance;
                    best = {x, y};
                }
            }
        return best;
    }

    auto nearby_storage_input = [&](int mx, int my, MachineType type) {
        float available = 0.0f;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                int x = mx + dx, y = my + dy;
                if (grid_.at(x, y) != TileType::Storage) continue;
                const auto& storage = grid_.data_at(x, y);
                if (type == MachineType::Food) available += storage.stored_raw_food;
                else if (type == MachineType::Materials) available += storage.stored_raw_material;
                else available += storage.stored_construction_material;
            }
        return available;
    };

    float best_score = -1.0e9f;
    std::pair<int, int> best = {-1, -1};
    for (int y = std::max(0, pos.y - OBSERVATION_RADIUS);
         y <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); y++)
        for (int x = std::max(0, pos.x - OBSERVATION_RADIUS);
             x <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); x++) {
            if (grid_.at(x, y) != TileType::Machine) continue;
            const auto& machine = grid_.data_at(x, y);
            if (!work_target_feasible(entity, x, y)) continue;
            if (needs.hunger > 0.5f && machine.machine_type != MachineType::Food) continue;

            float carried = machine.machine_type == MachineType::Food ? inv.raw_food
                : machine.machine_type == MachineType::Materials ? inv.raw_material
                : inv.construction_material;
            float buffered = machine.machine_type == MachineType::Food ? machine.stored_raw_food
                : machine.machine_type == MachineType::Materials ? machine.stored_raw_material
                : machine.stored_construction_material;
            if (machine.built_on_resource
                && (machine.resource_amount > 0.001f || machine.resource_regen > 0.0f)) {
                buffered = std::max(buffered, config_.machine_input);
            }
            float stored = nearby_storage_input(x, y, machine.machine_type);
            if (carried + buffered + stored <= 0.001f) continue;

            int distance = std::abs(x - pos.x) + std::abs(y - pos.y);
            if (distance > OBSERVATION_RADIUS) continue;
            float score = carried * 300.0f + buffered * 120.0f + stored * 60.0f
                        - static_cast<float>(distance);
            if (machine.machine_type == MachineType::Food) {
                float pocket_deficit = 1.0f
                    - std::min(1.0f, inv.food / std::max(0.001f, config_.inv_food_cap));
                score += needs.hunger * 300.0f + pocket_deficit * 60.0f;
            }
            score += skills.factory_work * 2.0f;
            if (machine.claimed_by >= 0 && machine.claimed_by != agent_id) score -= 20.0f;
            if (score > best_score) {
                best_score = score;
                best = {x, y};
            }
        }
    return best;
}

bool Simulation::action_feasible(entt::entity entity, ActionType action) const {
    const auto& pos = registry_.get<PositionComponent>(entity);
    const auto& inv = registry_.get<InventoryComponent>(entity);
    const auto& needs = registry_.get<NeedsComponent>(entity);
    int agent_id = registry_.get<AgentComponent>(entity).id;
    auto in_range = [&](std::pair<int, int> target) {
        return target.first >= 0 && std::abs(target.first - pos.x)
            + std::abs(target.second - pos.y) <= OBSERVATION_RADIUS;
    };

    switch (action) {
        case ActionType::GATHER:
            return inv.total() < InventoryComponent::CAPACITY - 0.001f
                && (grid_.find_nearest(TileType::FoodSource, pos.x, pos.y,
                        OBSERVATION_RADIUS).first >= 0
                    || grid_.find_nearest(TileType::ScrapPile, pos.x, pos.y,
                        OBSERVATION_RADIUS).first >= 0);
        case ActionType::BUILD: {
            if (!config_.allow_build) return false;
            bool raw = inv.raw_material > 0.05f;
            bool cmat = inv.construction_material > 0.05f;
            int output_frames = 0;
            bool free_resource_site = false;
            for (int y = std::max(0, pos.y - OBSERVATION_RADIUS);
                 y <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); y++)
                for (int x = std::max(0, pos.x - OBSERVATION_RADIUS);
                     x <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); x++) {
                    if (std::abs(x - pos.x) + std::abs(y - pos.y) > OBSERVATION_RADIUS)
                        continue;
                    TileType tile = grid_.at(x, y);
                    const auto& data = grid_.data_at(x, y);
                    if (tile == TileType::Machine && !data.built) {
                        if ((data.machine_type == MachineType::Output && cmat)
                            || (data.machine_type != MachineType::Output && raw)) return true;
                    }
                    if (tile == TileType::Machine
                        && data.machine_type == MachineType::Output) output_frames++;
                    if (raw && ((tile == TileType::Conveyor && !data.built)
                        || (tile == TileType::EatingZone && !data.built))) return true;
                    if (raw && (tile == TileType::FoodSource || tile == TileType::ScrapPile)
                        && (data.claimed_by < 0 || data.claimed_by == agent_id)) {
                        free_resource_site = true;
                    }
                }
            if (cmat && output_frames < 2
                && in_range(grid_.find_output_machine_site(pos.x, pos.y))) return true;
            if (!raw) return false;
            if (free_resource_site) return true;
            if (in_range(grid_.find_storage_build_site(pos.x, pos.y))) return true;
            auto conveyor = grid_.find_conveyor_build_site(pos.x, pos.y);
            if (conveyor.x >= 0 && std::abs(conveyor.x - pos.x)
                + std::abs(conveyor.y - pos.y) <= OBSERVATION_RADIUS) return true;
            return false;
        }
        case ActionType::WORK:
            return find_feasible_work_target(entity).first >= 0;
        case ActionType::EAT: {
            if (inv.food > 0.01f || inv.raw_food >= config_.eat_food_per_tick) return true;
            for (int dy = -3; dy <= 3; dy++)
                for (int dx = -3; dx <= 3; dx++) {
                    int x = pos.x + dx, y = pos.y + dy;
                    if (grid_.at(x, y) != TileType::Storage) continue;
                    const auto& data = grid_.data_at(x, y);
                    if (grid_.at(x, y) == TileType::Storage
                        && (data.stored_food >= config_.eat_food_per_tick
                            || data.stored_raw_food >= config_.eat_food_per_tick)) return true;
                }
            return false;
        }
        case ActionType::REST:
            return needs.rest > 0.001f;
        case ActionType::SOCIALIZE: {
            auto agents = registry_.view<const AgentComponent>();
            for (auto other : agents)
                if (other != entity && registry_.get<AgentComponent>(other).alive) {
                    const auto& other_pos = registry_.get<PositionComponent>(other);
                    if (std::abs(other_pos.x - pos.x) + std::abs(other_pos.y - pos.y)
                        <= OBSERVATION_RADIUS) return true;
                }
            return false;
        }
        case ActionType::CREATE:
            return needs.expression > 0.001f && grid_.is_walkable(pos.x, pos.y);
        case ActionType::EXPLORE:
            for (int y = std::max(1, pos.y - OBSERVATION_RADIUS);
                 y <= std::min(grid_.height() - 2, pos.y + OBSERVATION_RADIUS); y++)
                for (int x = std::max(1, pos.x - OBSERVATION_RADIUS);
                     x <= std::min(grid_.width() - 2, pos.x + OBSERVATION_RADIUS); x++)
                    if ((x != pos.x || y != pos.y)
                        && std::abs(x - pos.x) + std::abs(y - pos.y) <= OBSERVATION_RADIUS
                        && grid_.is_walkable(x, y)) return true;
            return false;
        case ActionType::GET_FOOD:
            if (inv.food >= config_.inv_food_cap - 0.001f) return false;
            return in_range(grid_.find_nearest_storage_with_processed_food(pos.x, pos.y));
        case ActionType::MAINTAIN:
            return in_range(grid_.find_nearest_conveyor_needing_maintain(pos.x, pos.y));
        case ActionType::DISMANTLE:
            return in_range(grid_.find_nearest_dead_end_conveyor(pos.x, pos.y));
        case ActionType::SABOTAGE:
            for (int y = std::max(0, pos.y - OBSERVATION_RADIUS);
                 y <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); y++)
                for (int x = std::max(0, pos.x - OBSERVATION_RADIUS);
                     x <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); x++) {
                    if (std::abs(x - pos.x) + std::abs(y - pos.y) > OBSERVATION_RADIUS)
                        continue;
                    TileType tile = grid_.at(x, y);
                    if ((tile == TileType::Machine || tile == TileType::Conveyor)
                        && grid_.data_at(x, y).built) return true;
                }
            return false;
        case ActionType::IDLE:
            return true;
        default:
            return false;
    }
}

void Simulation::system_find_targets() {
    auto view = registry_.view<ActionComponent, PositionComponent,
                               InventoryComponent, NeedsComponent,
                               const AgentComponent>();
    for (auto e : view) {
        if (!registry_.get<AgentComponent>(e).alive) continue;

        auto& action = registry_.get<ActionComponent>(e);
        auto& pos    = registry_.get<PositionComponent>(e);
        auto& inv    = registry_.get<InventoryComponent>(e);
        auto& needs  = registry_.get<NeedsComponent>(e);
        auto& agent  = registry_.get<AgentComponent>(e);
        ActionType requested_action = action.current;
        bool target_lookup = false;
        bool target_lookup_failed = false;

        // Release claims when switching away from WORK or BUILD
        if (action.current != ActionType::WORK &&
            action.current != ActionType::BUILD &&
            action.target_x >= 0 && action.target_y >= 0) {
            auto& td = grid_.data_at(action.target_x, action.target_y);
            int my_id = registry_.get<AgentComponent>(e).id;
            if (td.claimed_by == my_id) td.claimed_by = -1;
        }

        int tx = -1, ty = -1;

        if (action.current == ActionType::WORK
            && action.sticky_ticks > 0
            && action.sticky_action == ActionType::WORK
            && action.target_x >= 0 && action.target_y >= 0
            && work_target_feasible(e, action.target_x, action.target_y)) {
            action.at_target = action.target_x == pos.x && action.target_y == pos.y;
            continue;
        }

        switch (action.current) {
            case ActionType::GATHER: {
                target_lookup = true;
                // Prefer FoodSource if agent has no raw_food and low food
                // (food chain priority). Otherwise prefer ScrapPile (for building).
                auto food_src = grid_.find_nearest(
                    TileType::FoodSource, pos.x, pos.y, OBSERVATION_RADIUS);
                auto scrap_target = grid_.find_nearest(
                    TileType::ScrapPile, pos.x, pos.y, OBSERVATION_RADIUS);

                // Priority logic:
                // - Bootstrapping: if no machines built yet, prefer ScrapPile (need to BUILD first)
                // - If carrying raw_food but no raw_material: prefer ScrapPile
                // - If carrying raw_material but no raw_food: prefer FoodSource
                // - If both empty: prefer ScrapPile (industry first, food can wait)
                // - If both have some: prefer whatever has less (balance)
                bool prefer_food = false;
                bool any_built = false;
                for (int y = std::max(0, pos.y - OBSERVATION_RADIUS);
                     y <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); y++)
                    for (int x = std::max(0, pos.x - OBSERVATION_RADIUS);
                         x <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); x++)
                        if (std::abs(x - pos.x) + std::abs(y - pos.y) <= OBSERVATION_RADIUS
                            && grid_.at(x, y) == TileType::Machine
                            && grid_.data_at(x, y).built) any_built = true;

                if (!any_built) {
                    prefer_food = false;  // Bootstrap: need raw_material to BUILD machines
                } else if (inv.raw_food < 0.1f
                           && (needs.hunger > 0.4f || inv.food < 0.2f)) {
                    prefer_food = true;
                } else if (inv.raw_food < 0.1f && inv.raw_material > 0.1f) {
                    prefer_food = true;  // need raw_food to work FoodMachine
                } else if (inv.raw_food > 0.1f && inv.raw_material < 0.1f) {
                    prefer_food = false;  // need raw_material for building/mat machines
                } else {
                    prefer_food = false;  // Default: industry first
                }

                if (prefer_food && food_src.first >= 0) {
                    tx = food_src.first;
                    ty = food_src.second;
                } else if (scrap_target.first >= 0) {
                    tx = scrap_target.first;
                    ty = scrap_target.second;
                } else if (food_src.first >= 0) {
                    tx = food_src.first;
                    ty = food_src.second;
                }
                break;
            }

            case ActionType::BUILD: {
                target_lookup = true;
                if (!config_.allow_build) {
                    target_lookup_failed = true;
                    break;
                }
                // Priority: nearest unbuilt structure with strategic bias.
                std::pair<int, int> machine_t = {-1, -1};
                int machine_dist = 999999;
                for (int gy = std::max(0, pos.y - OBSERVATION_RADIUS);
                     gy <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); gy++)
                    for (int gx = std::max(0, pos.x - OBSERVATION_RADIUS);
                         gx <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); gx++) {
                        if (grid_.at(gx, gy) != TileType::Machine) continue;
                        if (std::abs(gx - pos.x) + std::abs(gy - pos.y) > OBSERVATION_RADIUS)
                            continue;
                        const auto& data = grid_.data_at(gx, gy);
                        if (data.built) continue;
                        bool has_material = data.machine_type == MachineType::Output
                            ? inv.construction_material > 0.05f : inv.raw_material > 0.05f;
                        if (!has_material) continue;
                        int distance = std::abs(gx - pos.x) + std::abs(gy - pos.y);
                        if (distance < machine_dist) {
                            machine_dist = distance;
                            machine_t = {gx, gy};
                        }
                    }
                bool has_raw_material = inv.raw_material > 0.05f;
                auto conv_t    = grid_.find_nearest_conveyor_to_build(pos.x, pos.y);
                auto conv_site = grid_.find_conveyor_build_site(pos.x, pos.y);  // new conveyor on Floor
                auto ez_t      = grid_.find_nearest_unbuilt_eatingzone(pos.x, pos.y);
                auto stor_t    = grid_.find_storage_build_site(pos.x, pos.y);
                std::pair<int, int> fs_t = {-1, -1}, sp_t = {-1, -1};
                int fs_nearest = 999999, sp_nearest = 999999;
                for (int y = std::max(0, pos.y - OBSERVATION_RADIUS);
                     y <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); y++)
                    for (int x = std::max(0, pos.x - OBSERVATION_RADIUS);
                         x <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); x++) {
                        int distance = std::abs(x - pos.x) + std::abs(y - pos.y);
                        if (distance > OBSERVATION_RADIUS) continue;
                        TileType tile = grid_.at(x, y);
                        if (tile != TileType::FoodSource && tile != TileType::ScrapPile) continue;
                        const auto& data = grid_.data_at(x, y);
                        if (data.claimed_by >= 0 && data.claimed_by != agent.id) continue;
                        if (tile == TileType::FoodSource && distance < fs_nearest) {
                            fs_nearest = distance;
                            fs_t = {x, y};
                        } else if (tile == TileType::ScrapPile && distance < sp_nearest) {
                            sp_nearest = distance;
                            sp_t = {x, y};
                        }
                    }
                auto visible = [&](std::pair<int, int> target) {
                    return target.first >= 0 && std::abs(target.first - pos.x)
                        + std::abs(target.second - pos.y) <= OBSERVATION_RADIUS;
                };
                if (!visible(conv_t)) conv_t = {-1, -1};
                if (conv_site.x >= 0 && std::abs(conv_site.x - pos.x)
                    + std::abs(conv_site.y - pos.y) > OBSERVATION_RADIUS)
                    conv_site = {-1, -1, ConveyorDir::E};
                if (!visible(ez_t)) ez_t = {-1, -1};
                if (!visible(stor_t)) stor_t = {-1, -1};
                if (!visible(fs_t)) fs_t = {-1, -1};
                if (!visible(sp_t)) sp_t = {-1, -1};
                if (!has_raw_material) {
                    conv_t = {-1, -1};
                    conv_site = {-1, -1, ConveyorDir::E};
                    ez_t = {-1, -1};
                    stor_t = {-1, -1};
                    fs_t = {-1, -1};
                    sp_t = {-1, -1};
                }
                int output_frames = 0;
                bool unbuilt_output = false;
                for (int gy = std::max(0, pos.y - OBSERVATION_RADIUS);
                     gy <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); gy++)
                    for (int gx = std::max(0, pos.x - OBSERVATION_RADIUS);
                         gx <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); gx++)
                        if (grid_.at(gx, gy) == TileType::Machine
                            && std::abs(gx - pos.x) + std::abs(gy - pos.y) <= OBSERVATION_RADIUS
                            && grid_.data_at(gx, gy).machine_type == MachineType::Output) {
                            output_frames++;
                            unbuilt_output |= !grid_.data_at(gx, gy).built;
                        }
                std::pair<int, int> output_site = {-1, -1};
                if (inv.construction_material > 0.05f && output_frames < 2
                    && !unbuilt_output) {
                    output_site = grid_.find_output_machine_site(pos.x, pos.y);
                    if (!visible(output_site)) output_site = {-1, -1};
                }

                // Compute distances
                int mach_dist = (machine_t.first >= 0)
                    ? std::abs(machine_t.first - pos.x) + std::abs(machine_t.second - pos.y) : 999999;
                int conv_dist = (conv_t.first >= 0)
                    ? std::abs(conv_t.first - pos.x) + std::abs(conv_t.second - pos.y) : 999999;
                int csite_dist = (conv_site.x >= 0)
                    ? std::abs(conv_site.x - pos.x) + std::abs(conv_site.y - pos.y) : 999999;
                int ez_dist = (ez_t.first >= 0)
                    ? std::abs(ez_t.first - pos.x) + std::abs(ez_t.second - pos.y) : 999999;
                int stor_dist = (stor_t.first >= 0)
                    ? std::abs(stor_t.first - pos.x) + std::abs(stor_t.second - pos.y) : 999999;
                int fs_dist = (fs_t.first >= 0)
                    ? std::abs(fs_t.first - pos.x) + std::abs(fs_t.second - pos.y) : 999999;
                int sp_dist = (sp_t.first >= 0)
                    ? std::abs(sp_t.first - pos.x) + std::abs(sp_t.second - pos.y) : 999999;
                int output_dist = (output_site.first >= 0)
                    ? std::abs(output_site.first - pos.x) + std::abs(output_site.second - pos.y) : 999999;
                int machine_bonus = 0;
                if (machine_t.first >= 0
                    && grid_.data_at(machine_t.first, machine_t.second).machine_type == MachineType::Output) {
                    // Finish quota-critical recovery before opening another site.
                    machine_bonus = -30;
                }

                // Bonuses (negative = higher priority)
                // Conveyors become high priority once machines are built
                // (connecting output to Storage/Exit is critical for quota)
                // Bonuses (negative = higher priority)
                // Conveyors get high priority when machines are built but unconnected
                int built_m = 0;
                int built_food_m = 0;
                for (int gy = std::max(0, pos.y - OBSERVATION_RADIUS);
                     gy <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); gy++)
                    for (int gx = std::max(0, pos.x - OBSERVATION_RADIUS);
                         gx <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); gx++)
                        if (grid_.at(gx, gy) == TileType::Machine && grid_.data_at(gx, gy).built) {
                            if (std::abs(gx - pos.x) + std::abs(gy - pos.y)
                                > OBSERVATION_RADIUS) continue;
                            built_m++;
                            if (grid_.data_at(gx, gy).machine_type == MachineType::Food)
                                built_food_m++;
                        }
                int conv_bonus = (built_m >= 4) ? -12 : -6;  // conveyors important when infra is up
                int stor_bonus = -5;
                int fs_bonus = -8;  // FoodMachine on FoodSource
                int sp_bonus = -8;  // MaterialsMachine on ScrapPile (tier 1)
                int output_bonus = output_frames == 0 ? -30 : -15;

                // Ensure food production before output: FoodMachine must come first
                if (built_food_m == 0) {
                    fs_bonus = -15;  // urgently need FoodMachine first
                } else if (built_food_m < 2) {
                    fs_bonus = -12;  // still need more food production
                }

                // Suppress machine building once we have enough
                if (built_m >= 8) { fs_bonus = -2; sp_bonus = -2; }

                // Bonus for ScrapPiles close to Exit: OutputMachines near Exit
                // can deposit output directly into Exit-adjacent Storage, making
                // the quota pipeline work without conveyors.
                int sp_exit_bonus = 0;

                int best_dist = 999999;
                if (machine_t.first >= 0 && (mach_dist + machine_bonus) < best_dist)
                    best_dist = mach_dist + machine_bonus;
                if (output_site.first >= 0 && (output_dist + output_bonus) < best_dist)
                    best_dist = output_dist + output_bonus;
                if (fs_t.first >= 0 && (fs_dist + fs_bonus) < best_dist) best_dist = fs_dist + fs_bonus;
                if (sp_t.first >= 0 && (sp_dist + sp_bonus + sp_exit_bonus) < best_dist) best_dist = sp_dist + sp_bonus + sp_exit_bonus;
                if (stor_t.first >= 0 && (stor_dist + stor_bonus) < best_dist) best_dist = stor_dist + stor_bonus;
                if (conv_t.first >= 0 && (conv_dist + conv_bonus) < best_dist) best_dist = conv_dist + conv_bonus;
                if (conv_site.x >= 0 && (csite_dist + conv_bonus) < best_dist) best_dist = csite_dist + conv_bonus;
                if (ez_t.first >= 0 && ez_dist < best_dist) best_dist = ez_dist;

                if (machine_t.first >= 0 && (mach_dist + machine_bonus) <= best_dist) {
                    tx = machine_t.first; ty = machine_t.second;
                } else if (output_site.first >= 0 && (output_dist + output_bonus) <= best_dist) {
                    tx = output_site.first; ty = output_site.second;
                } else if (fs_t.first >= 0 && (fs_dist + fs_bonus) <= best_dist) {
                    tx = fs_t.first; ty = fs_t.second;
                } else if (sp_t.first >= 0 && (sp_dist + sp_bonus + sp_exit_bonus) <= best_dist) {
                    tx = sp_t.first; ty = sp_t.second;
                } else if (stor_t.first >= 0 && (stor_dist + stor_bonus) <= best_dist) {
                    tx = stor_t.first; ty = stor_t.second;
                } else if (conv_t.first >= 0 && (conv_dist + conv_bonus) <= best_dist) {
                    tx = conv_t.first; ty = conv_t.second;
                } else if (conv_site.x >= 0 && (csite_dist + conv_bonus) <= best_dist) {
                    tx = conv_site.x; ty = conv_site.y;
                } else if (ez_t.first >= 0) {
                    tx = ez_t.first; ty = ez_t.second;
                }

                // Claim resource tiles when chosen as BUILD target
                // (prevents other agents from targeting the same tile)
                if (tx >= 0 && ty >= 0) {
                    TileType tt = grid_.at(tx, ty);
                    if (tt == TileType::FoodSource || tt == TileType::ScrapPile) {
                        auto& td = grid_.data_at(tx, ty);
                        td.claimed_by = agent.id;
                    }
                }
                break;
            }

            case ActionType::GET_FOOD: {
                target_lookup = true;
                auto target = grid_.find_nearest_storage_with_processed_food(pos.x, pos.y);
                if (target.first >= 0 && std::abs(target.first - pos.x)
                    + std::abs(target.second - pos.y) <= OBSERVATION_RADIUS) {
                    tx = target.first; ty = target.second;
                }
                break;
            }

            case ActionType::WORK: {
                target_lookup = true;
                auto target = find_feasible_work_target(e);
                tx = target.first;
                ty = target.second;
                // Claim the target machine (RimWorld/DF pattern)
                if (tx >= 0 && ty >= 0) {
                    int my_id = registry_.get<AgentComponent>(e).id;
                    // Release old claim if target changed
                    if (action.target_x >= 0 && action.target_y >= 0 &&
                        (action.target_x != tx || action.target_y != ty)) {
                        auto& old_td = grid_.data_at(action.target_x, action.target_y);
                        if (old_td.claimed_by == my_id) old_td.claimed_by = -1;
                    }
                    auto& td = grid_.data_at(tx, ty);
                    if (td.claimed_by < 0 || td.claimed_by == my_id) {
                        td.claimed_by = my_id;
                    }
                }
                break;
            }

            case ActionType::EAT:
                tx = pos.x;
                ty = pos.y;
                break;

            case ActionType::REST: {
                size_t index = metric_index(ActionType::REST);
                tx = action.preferred_x[index];
                ty = action.preferred_y[index];
                if (tx < 0 || ty < 0) {
                    auto place = find_preferred_place(e, ActionType::REST);
                    tx = place.x; ty = place.y;
                }
                break;
            }

            case ActionType::SOCIALIZE: {
                target_lookup = true;
                size_t index = metric_index(ActionType::SOCIALIZE);
                tx = action.preferred_x[index];
                ty = action.preferred_y[index];
                if (tx < 0 || ty < 0) {
                    auto place = find_preferred_place(e, ActionType::SOCIALIZE);
                    tx = place.x; ty = place.y;
                }
                if (tx < 0) {
                    target_lookup_failed = true;
                    tx = pos.x;
                    ty = pos.y;
                }
                break;
            }

            case ActionType::CREATE: {
                target_lookup = true;
                size_t index = metric_index(ActionType::CREATE);
                tx = action.preferred_x[index];
                ty = action.preferred_y[index];
                if (tx < 0 || ty < 0) {
                    auto place = find_preferred_place(e, ActionType::CREATE);
                    tx = place.x; ty = place.y;
                }
                if (tx < 0) {
                    target_lookup_failed = true;
                    tx = pos.x; ty = pos.y;
                }
                break;
            }

            case ActionType::EXPLORE: {
                target_lookup = true;
                std::vector<std::pair<int, int>> destinations;
                for (int y = std::max(1, pos.y - OBSERVATION_RADIUS);
                     y <= std::min(grid_.height() - 2, pos.y + OBSERVATION_RADIUS); y++)
                    for (int x = std::max(1, pos.x - OBSERVATION_RADIUS);
                         x <= std::min(grid_.width() - 2, pos.x + OBSERVATION_RADIUS); x++)
                        if ((x != pos.x || y != pos.y)
                            && std::abs(x - pos.x) + std::abs(y - pos.y) <= OBSERVATION_RADIUS
                            && grid_.is_walkable(x, y)) destinations.push_back({x, y});
                if (destinations.empty()) {
                    target_lookup_failed = true;
                    tx = pos.x;
                    ty = pos.y;
                    break;
                }
                std::uniform_int_distribution<size_t> pick(0, destinations.size() - 1);
                auto destination = destinations[pick(
                    registry_.get<RandomComponent>(e).engine)];
                tx = destination.first;
                ty = destination.second;
                break;
            }

            case ActionType::MAINTAIN: {
                target_lookup = true;
                auto conv = grid_.find_nearest_conveyor_needing_maintain(
                    pos.x, pos.y, 0.9f, OBSERVATION_RADIUS);
                if (conv.first >= 0) {
                    // Agent stands adjacent to conveyor (not on it)
                    auto adj = grid_.find_walkable_adjacent_to(
                        conv.first, conv.second, pos.x, pos.y);
                    if (adj.first >= 0) {
                        tx = adj.first; ty = adj.second;
                    }
                }
                break;
            }

            case ActionType::DISMANTLE: {
                target_lookup = true;
                // Find nearest conveyor that is a dead-end or blocking a path.
                // Agent walks to an adjacent walkable tile to dismantle it.
                auto dead_end = grid_.find_nearest_dead_end_conveyor(pos.x, pos.y);
                if (dead_end.first >= 0 && std::abs(dead_end.first - pos.x)
                    + std::abs(dead_end.second - pos.y) > OBSERVATION_RADIUS)
                    dead_end = {-1, -1};
                // Also consider blocking conveyors nearby (scan vicinity)
                int block_x = -1, block_y = -1, block_dist = 999999;
                for (int sy = std::max(0, pos.y - 10); sy < std::min(grid_.height(), pos.y + 10); sy++)
                    for (int sx = std::max(0, pos.x - 10); sx < std::min(grid_.width(), pos.x + 10); sx++) {
                        if (grid_.is_conveyor_blocking_path(sx, sy)) {
                            int d = std::abs(sx - pos.x) + std::abs(sy - pos.y);
                            if (d < block_dist) {
                                block_dist = d;
                                block_x = sx; block_y = sy;
                            }
                        }
                    }
                // Prefer blocking conveyor (higher urgency)
                int cx = -1, cy = -1;
                if (block_x >= 0 && block_dist < 15) {
                    cx = block_x; cy = block_y;
                } else if (dead_end.first >= 0) {
                    cx = dead_end.first; cy = dead_end.second;
                }
                if (cx >= 0) {
                    auto adj = grid_.find_walkable_adjacent_to(cx, cy, pos.x, pos.y);
                    if (adj.first >= 0) {
                        tx = adj.first; ty = adj.second;
                    }
                }
                break;
            }

            case ActionType::SABOTAGE: {
                target_lookup = true;
                // Find nearest built infrastructure to destroy.
                // Prefer machines (more damage), fallback to conveyors.
                auto machine = grid_.find_nearest_built_machine(pos.x, pos.y);
                int mx = machine.first, my = machine.second;
                int mach_dist = (mx >= 0) ? std::abs(mx - pos.x) + std::abs(my - pos.y) : 999999;
                if (mach_dist > OBSERVATION_RADIUS) {
                    mx = -1; my = -1; mach_dist = 999999;
                }

                // Find nearest built conveyor
                int cx = -1, cy = -1, conv_dist = 999999;
                for (int sy = std::max(0, pos.y - OBSERVATION_RADIUS);
                     sy <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); sy++)
                    for (int sx = std::max(0, pos.x - OBSERVATION_RADIUS);
                         sx <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); sx++) {
                        if (grid_.at(sx, sy) == TileType::Conveyor && grid_.data_at(sx, sy).built) {
                            int d = std::abs(sx - pos.x) + std::abs(sy - pos.y);
                            if (d > OBSERVATION_RADIUS) continue;
                            if (d < conv_dist) {
                                conv_dist = d;
                                cx = sx; cy = sy;
                            }
                        }
                    }

                // Prefer machine if close enough; otherwise conveyor
                int infra_x = -1, infra_y = -1;
                if (mx >= 0 && mach_dist <= conv_dist + 5) {
                    infra_x = mx; infra_y = my;
                } else if (cx >= 0) {
                    infra_x = cx; infra_y = cy;
                }

                if (infra_x >= 0) {
                    // Walk to adjacent tile (can't stand on machine/conveyor)
                    auto adj = grid_.find_walkable_adjacent_to(infra_x, infra_y, pos.x, pos.y);
                    if (adj.first >= 0) {
                        tx = adj.first; ty = adj.second;
                    }
                }
                break;
            }

            default:
                tx = pos.x;
                ty = pos.y;
                break;
        }

        if (target_lookup) {
            size_t index = metric_index(requested_action);
            metrics_.target_lookups[index]++;
            if (target_lookup_failed || tx < 0 || ty < 0) {
                if (!action_feasible(e, requested_action))
                    metrics_.plan_invalidations[index]++;
                else
                    metrics_.target_failures[index]++;
            }
        }

        // Any invalid plan releases its commitment; IDLE is always self-targeted.
        if (tx < 0 || ty < 0) {
            action.current = ActionType::IDLE;
            action.sticky_ticks = 0;
            tx = pos.x;
            ty = pos.y;
        }

        action.target_x = tx;
        action.target_y = ty;
        action.at_target = (tx == pos.x && ty == pos.y);
        if (action.current == ActionType::GET_FOOD
            && action.sticky_action == ActionType::GET_FOOD) {
            int travel = std::abs(tx - pos.x) + std::abs(ty - pos.y);
            action.sticky_ticks = std::max(action.sticky_ticks, travel + 5);
        }
    }
}
