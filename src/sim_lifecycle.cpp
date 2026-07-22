#include "simulation.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr uint64_t HASH_ARRIVAL = 0x4152524956414c31ULL;
constexpr uint64_t HASH_ARRIVAL_TRAIT = 0x4152525452414954ULL;
constexpr uint64_t HASH_ARRIVAL_AGE = 0x4152524956414745ULL;
constexpr uint64_t HASH_BIRTH = 0x4249525448524f4cULL;
constexpr uint64_t HASH_MUTATION = 0x4d55544154494f4eULL;
constexpr uint64_t HASH_LIFESPAN = 0x4c4946455350414eULL;
constexpr uint64_t HASH_AGENT_RNG = 0x4147454e54524e47ULL;

uint64_t splitmix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

}  // namespace

uint64_t Simulation::lifecycle_hash(uint64_t salt, int a, int b) const {
    uint64_t value = static_cast<uint32_t>(config_.seed);
    value = splitmix64(value ^ salt);
    value = splitmix64(value ^ static_cast<uint32_t>(a));
    return splitmix64(value ^ static_cast<uint32_t>(b));
}

float Simulation::lifecycle_unit(uint64_t salt, int a, int b) const {
    constexpr double denominator = 1.0 / 9007199254740992.0;
    return static_cast<float>((lifecycle_hash(salt, a, b) >> 11) * denominator);
}

int Simulation::lifecycle_age(const LifecycleComponent& lifecycle) const {
    return lifecycle.age_at_entry + std::max(0, tick_ - lifecycle.entry_tick);
}

void Simulation::ensure_agent_metrics(int agent_id) {
    size_t required = static_cast<size_t>(agent_id) + 1;
    if (metrics_.agent_action_ticks.size() < required)
        metrics_.agent_action_ticks.resize(required);
    if (metrics_.agent_productive_effect_ticks.size() < required)
        metrics_.agent_productive_effect_ticks.resize(required);
    if (metrics_.agent_food_shared_given.size() < required)
        metrics_.agent_food_shared_given.resize(required);
    if (metrics_.agent_food_received.size() < required)
        metrics_.agent_food_received.resize(required);
    if (metrics_.agent_food_consumed.size() < required)
        metrics_.agent_food_consumed.resize(required);
    if (metric_death_recorded_.size() < required)
        metric_death_recorded_.resize(required, 0);
}

entt::entity Simulation::spawn_agent(
    int x, int y, AgentOrigin origin,
    const PersonalityComponent& personality,
    const OpinionComponent& opinion,
    const NeedsComponent& needs,
    const InventoryComponent& inventory,
    int age_at_entry, int parent_a, int parent_b, int generation)
{
    int id = next_agent_id_++;
    social_.ensure_agent_id(id);
    ensure_agent_metrics(id);

    LifecycleComponent lifecycle;
    lifecycle.origin = origin;
    lifecycle.parent_a = parent_a;
    lifecycle.parent_b = parent_b;
    lifecycle.entry_tick = tick_;
    lifecycle.age_at_entry = std::max(0, age_at_entry);
    float lifespan_factor = 1.0f + config_.lifespan_spread
        * (2.0f * lifecycle_unit(HASH_LIFESPAN, id) - 1.0f);
    lifecycle.lifespan = std::max(config_.maturity_age_ticks + 1,
        static_cast<int>(std::round(config_.life_expectancy_ticks * lifespan_factor)));
    lifecycle.cohort = tick_ / config_.cohort_width_ticks;
    lifecycle.generation = generation;

    auto entity = registry_.create();
    registry_.emplace<PositionComponent>(entity, x, y);
    AgentComponent agent;
    agent.id = id;
    registry_.emplace<AgentComponent>(entity, std::move(agent));
    registry_.emplace<PersonalityComponent>(entity, personality);
    registry_.emplace<NeedsComponent>(entity, needs);
    registry_.emplace<ActionComponent>(entity, ActionType::IDLE);
    registry_.emplace<StressComponent>(entity, 0.0f);
    registry_.emplace<SocialComponent>(entity);
    registry_.emplace<OpinionComponent>(entity, opinion);
    registry_.emplace<InventoryComponent>(entity, inventory);
    registry_.emplace<SkillsComponent>(entity);
    registry_.emplace<PlaceMemoryComponent>(entity);
    registry_.emplace<CreativeWorkComponent>(entity);
    registry_.emplace<LifecycleComponent>(entity, lifecycle);
    registry_.emplace<RandomComponent>(entity,
        static_cast<uint32_t>(lifecycle_hash(HASH_AGENT_RNG, id)));

    EventType event = EventType::SPAWNED;
    std::string text = "initial resident awakened";
    if (origin == AgentOrigin::ARRIVAL) {
        event = EventType::ARRIVED;
        text = "arrived through Entrance";
        arrivals_admitted_++;
    } else if (origin == AgentOrigin::BIRTH) {
        event = EventType::BORN;
        text = "born to A" + std::to_string(parent_a) + " and A"
             + std::to_string(parent_b);
        births_total_++;
    }
    chronicle_.log(tick_, event, id, text, x, y, 0.0f, parent_a);
    peak_population_ = std::max(peak_population_, alive_count());
    return entity;
}

void Simulation::system_lifecycle() {
    auto alive = alive_agents();
    std::sort(alive.begin(), alive.end(), [&](entt::entity left, entt::entity right) {
        return registry_.get<AgentComponent>(left).id
             < registry_.get<AgentComponent>(right).id;
    });

    for (auto entity : alive) {
        auto& lifecycle = registry_.get<LifecycleComponent>(entity);
        const auto& social = registry_.get<SocialComponent>(entity);
        lifecycle.peak_influence = std::max(lifecycle.peak_influence, social.influence);
        if (lifecycle.first_trusted_edge_tick >= 0) continue;
        int id = registry_.get<AgentComponent>(entity).id;
        for (auto other : alive) {
            if (other == entity) continue;
            int other_id = registry_.get<AgentComponent>(other).id;
            const auto& outward = social_.get_rel(id, other_id);
            const auto& inward = social_.get_rel(other_id, id);
            if (outward.familiarity >= 0.1f && inward.familiarity >= 0.1f
                && outward.trust >= 0.1f && inward.trust >= 0.1f) {
                lifecycle.first_trusted_edge_tick = tick_;
                break;
            }
        }
    }

    if (config_.arrivals_enabled && config_.arrival_rate_per_1000_ticks > 0.0f) {
        float probability = 1.0f - std::exp(
            -config_.arrival_rate_per_1000_ticks / 1000.0f);
        if (lifecycle_unit(HASH_ARRIVAL, tick_) < probability) {
            arrival_attempts_++;
            if (alive_count() >= config_.max_population) {
                arrivals_blocked_capacity_++;
            } else {
                auto entrances = grid_.find_all(TileType::Entrance);
                if (!entrances.empty()) {
                    int id = next_agent_id_;
                    auto trait = [&](int dimension) {
                        return 0.1f + 0.8f * lifecycle_unit(
                            HASH_ARRIVAL_TRAIT + dimension, id);
                    };
                    PersonalityComponent personality;
                    personality.compliance = trait(0);
                    personality.laziness = trait(1);
                    personality.artistry = trait(2);
                    personality.gregariousness = trait(3);
                    personality.resilience = trait(4);
                    personality.curiosity = trait(5);
                    personality.archetype = Archetype::COUNT;

                    OpinionComponent opinion;
                    for (int d = 0; d < OpinionComponent::DIMS; d++)
                        opinion.values[d] = 0.45f + 0.1f * lifecycle_unit(
                            HASH_ARRIVAL_TRAIT + 16 + d, id);
                    NeedsComponent needs;
                    needs.hunger = lifecycle_unit(HASH_ARRIVAL_TRAIT + 32, id) * 0.2f;
                    needs.rest = lifecycle_unit(HASH_ARRIVAL_TRAIT + 33, id) * 0.2f;
                    needs.social = lifecycle_unit(HASH_ARRIVAL_TRAIT + 34, id) * 0.2f;
                    needs.expression = lifecycle_unit(HASH_ARRIVAL_TRAIT + 35, id) * 0.2f;
                    needs.purpose = lifecycle_unit(HASH_ARRIVAL_TRAIT + 36, id) * 0.2f;
                    InventoryComponent inventory;
                    int age_span = config_.arrival_age_max_ticks
                                 - config_.arrival_age_min_ticks;
                    int age = config_.arrival_age_min_ticks + static_cast<int>(
                        lifecycle_unit(HASH_ARRIVAL_AGE, id) * (age_span + 1));
                    spawn_agent(entrances.front().first, entrances.front().second,
                        AgentOrigin::ARRIVAL, personality, opinion, needs,
                        inventory, age);
                }
            }
        }
    }

    if (!config_.reproduction_enabled
        || tick_ % config_.reproduction_check_interval_ticks != 0
        || config_.reproduction_rate_per_1000_ticks <= 0.0f) return;

    alive = alive_agents();
    std::sort(alive.begin(), alive.end(), [&](entt::entity left, entt::entity right) {
        return registry_.get<AgentComponent>(left).id
             < registry_.get<AgentComponent>(right).id;
    });
    std::vector<uint8_t> used(alive.size(), 0);
    int epoch = tick_ / config_.reproduction_check_interval_ticks;

    auto local_food_security = [&](entt::entity entity) {
        const auto& pos = registry_.get<PositionComponent>(entity);
        const auto& inventory = registry_.get<InventoryComponent>(entity);
        float food = inventory.food;
        for (int y = std::max(0, pos.y - OBSERVATION_RADIUS);
             y <= std::min(grid_.height() - 1, pos.y + OBSERVATION_RADIUS); y++)
            for (int x = std::max(0, pos.x - OBSERVATION_RADIUS);
                 x <= std::min(grid_.width() - 1, pos.x + OBSERVATION_RADIUS); x++) {
                if (std::abs(x - pos.x) + std::abs(y - pos.y) > OBSERVATION_RADIUS
                    || grid_.at(x, y) != TileType::Storage) continue;
                const auto& storage = grid_.data_at(x, y);
                food += (storage.stored_food + storage.stored_raw_food) * 0.1f;
            }
        return std::clamp(food / 2.0f, 0.0f, 1.0f);
    };

    for (size_t i = 0; i < alive.size(); i++) {
        if (used[i]) continue;
        auto& life_a = registry_.get<LifecycleComponent>(alive[i]);
        int age_a = lifecycle_age(life_a);
        if (age_a < config_.maturity_age_ticks
            || tick_ - life_a.last_reproduction_tick
               < config_.reproduction_cooldown_ticks) continue;

        for (size_t j = i + 1; j < alive.size(); j++) {
            if (used[j]) continue;
            auto& life_b = registry_.get<LifecycleComponent>(alive[j]);
            int age_b = lifecycle_age(life_b);
            if (age_b < config_.maturity_age_ticks
                || tick_ - life_b.last_reproduction_tick
                   < config_.reproduction_cooldown_ticks) continue;

            const auto& pos_a = registry_.get<PositionComponent>(alive[i]);
            const auto& pos_b = registry_.get<PositionComponent>(alive[j]);
            if (std::abs(pos_a.x - pos_b.x) + std::abs(pos_a.y - pos_b.y) > 3)
                continue;

            int id_a = registry_.get<AgentComponent>(alive[i]).id;
            int id_b = registry_.get<AgentComponent>(alive[j]).id;
            const auto& rel_ab = social_.get_rel(id_a, id_b);
            const auto& rel_ba = social_.get_rel(id_b, id_a);
            float social_score = std::sqrt(
                rel_ab.familiarity * rel_ba.familiarity
                * std::max(0.0f, rel_ab.trust) * std::max(0.0f, rel_ba.trust));
            if (social_score <= 0.0f) continue;

            const auto& needs_a = registry_.get<NeedsComponent>(alive[i]);
            const auto& needs_b = registry_.get<NeedsComponent>(alive[j]);
            const auto& stress_a = registry_.get<StressComponent>(alive[i]);
            const auto& stress_b = registry_.get<StressComponent>(alive[j]);
            const auto& mood_a = registry_.get<SocialComponent>(alive[i]);
            const auto& mood_b = registry_.get<SocialComponent>(alive[j]);
            float material_a = std::sqrt(std::max(0.0f,
                (1.0f - needs_a.hunger) * (1.0f - needs_a.rest)))
                * local_food_security(alive[i]);
            float material_b = std::sqrt(std::max(0.0f,
                (1.0f - needs_b.hunger) * (1.0f - needs_b.rest)))
                * local_food_security(alive[j]);
            float wellbeing = std::sqrt(std::max(0.0f,
                (1.0f - stress_a.value) * (1.0f - stress_b.value)
                * mood_a.mood * mood_b.mood));
            float age_factor_a = smoothstep(
                static_cast<float>(config_.maturity_age_ticks),
                static_cast<float>(config_.maturity_age_ticks + 500),
                static_cast<float>(age_a));
            age_factor_a *= 1.0f - smoothstep(
                life_a.lifespan * 0.75f, life_a.lifespan * 0.95f,
                static_cast<float>(age_a));
            float age_factor_b = smoothstep(
                static_cast<float>(config_.maturity_age_ticks),
                static_cast<float>(config_.maturity_age_ticks + 500),
                static_cast<float>(age_b));
            age_factor_b *= 1.0f - smoothstep(
                life_b.lifespan * 0.75f, life_b.lifespan * 0.95f,
                static_cast<float>(age_b));
            float score = age_factor_a * age_factor_b
                * std::sqrt(material_a * material_b)
                * social_score * wellbeing;
            float probability = 1.0f - std::exp(
                -config_.reproduction_rate_per_1000_ticks
                * config_.reproduction_check_interval_ticks / 1000.0f * score);
            if (lifecycle_unit(HASH_BIRTH + epoch, id_a, id_b) >= probability)
                continue;

            if (alive_count() >= config_.max_population) {
                births_blocked_capacity_++;
                used[i] = used[j] = 1;
                break;
            }

            const auto& parent_a = registry_.get<PersonalityComponent>(alive[i]);
            const auto& parent_b = registry_.get<PersonalityComponent>(alive[j]);
            int child_id = next_agent_id_;
            auto inherit = [&](float a, float b, int dimension) {
                float mutation = config_.personality_mutation_amplitude
                    * (2.0f * lifecycle_unit(HASH_MUTATION + dimension,
                                             child_id, id_a ^ id_b) - 1.0f);
                return std::clamp((a + b) * 0.5f + mutation, 0.05f, 0.95f);
            };
            PersonalityComponent child;
            child.compliance = inherit(parent_a.compliance, parent_b.compliance, 0);
            child.laziness = inherit(parent_a.laziness, parent_b.laziness, 1);
            child.artistry = inherit(parent_a.artistry, parent_b.artistry, 2);
            child.gregariousness = inherit(
                parent_a.gregariousness, parent_b.gregariousness, 3);
            child.resilience = inherit(parent_a.resilience, parent_b.resilience, 4);
            child.curiosity = inherit(parent_a.curiosity, parent_b.curiosity, 5);
            child.archetype = Archetype::COUNT;
            OpinionComponent opinion;
            for (float& value : opinion.values) value = 0.5f;
            NeedsComponent child_needs;
            child_needs.hunger = 0.05f;
            child_needs.rest = 0.05f;
            child_needs.social = 0.05f;
            child_needs.expression = 0.05f;
            child_needs.purpose = 0.05f;
            InventoryComponent child_inventory;

            life_a.last_reproduction_tick = tick_;
            life_b.last_reproduction_tick = tick_;
            int generation = std::max(life_a.generation, life_b.generation) + 1;
            spawn_agent(pos_a.x, pos_a.y, AgentOrigin::BIRTH,
                child, opinion, child_needs, child_inventory, 0,
                id_a, id_b, generation);
            used[i] = used[j] = 1;
            break;
        }
    }
}
