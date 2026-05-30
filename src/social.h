#pragma once
// Social fabric systems for La Vida Misma.
// Relationship tracking, emotional contagion, and emergent leadership.

#include "components.h"
#include <entt/entt.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

// ============================================================
// Social data structures
// ============================================================

struct RelationshipEntry {
    float familiarity = 0.0f;  // [0, 1] increases with interactions
    float trust       = 0.5f;  // [0, 1] modified by shared experiences
    float last_tick   = -100;  // last interaction tick
};

struct SocialComponent {
    // Index into global relationship matrix
    // Agent's social energy (depleted by forced interaction, recharged by choice)
    float social_energy = 1.0f;  // [0, 1]

    // Leadership score: how much this agent influences neighbors
    // Emerges from compliance * interactions * survival
    float influence = 0.0f;

    // Current mood (derived from needs + stress + social context)
    float mood = 0.5f;  // [0, 1], 0 = miserable, 1 = ecstatic
};

// ============================================================
// Social system: manages relationships, contagion, leadership
// ============================================================

class SocialFabric {
public:
    SocialFabric(int max_agents)
        : max_agents_(max_agents),
          rels_(max_agents * max_agents) {}

    // Get/set relationship
    RelationshipEntry& get_rel(int a, int b) {
        return rels_[a * max_agents_ + b];
    }
    const RelationshipEntry& get_rel(int a, int b) const {
        return rels_[a * max_agents_ + b];
    }

    // --- Systems ---

    // 1. Process social interactions (called when agent SOCIALIZEs)
    void process_interaction(int agent_a, int agent_b, int tick) {
        auto& rel_ab = get_rel(agent_a, agent_b);
        auto& rel_ba = get_rel(agent_b, agent_a);

        // Familiarity grows with interaction (diminishing returns)
        float fam_gain = 0.05f * (1.0f - rel_ab.familiarity);
        rel_ab.familiarity = std::min(1.0f, rel_ab.familiarity + fam_gain);
        rel_ba.familiarity = std::min(1.0f, rel_ba.familiarity + fam_gain);

        // Trust shifts toward 0.5 + mood average (good mood → trust boost)
        rel_ab.trust += 0.02f;
        rel_ba.trust += 0.02f;
        rel_ab.trust = std::clamp(rel_ab.trust, 0.0f, 1.0f);
        rel_ba.trust = std::clamp(rel_ba.trust, 0.0f, 1.0f);

        rel_ab.last_tick = tick;
        rel_ba.last_tick = tick;
    }

    // 2. Emotional contagion: agents near stressed agents get stressed
    void apply_contagion(
        entt::registry& registry,
        const std::vector<entt::entity>& alive,
        float contagion_radius = 3.0f,
        float contagion_rate = 0.01f)
    {
        struct AgentInfo {
            entt::entity entity;
            int id;
            float x, y;
            float stress;
            float mood;
            float influence;
        };

        std::vector<AgentInfo> agents;
        agents.reserve(alive.size());
        for (auto e : alive) {
            auto& pos  = registry.get<PositionComponent>(e);
            auto& ag   = registry.get<AgentComponent>(e);
            auto& st   = registry.get<StressComponent>(e);
            agents.push_back({e, ag.id, (float)pos.x, (float)pos.y, st.value, 0.5f, 0.0f});
        }

        // Pairwise contagion
        for (size_t i = 0; i < agents.size(); i++) {
            for (size_t j = i + 1; j < agents.size(); j++) {
                float dx = agents[i].x - agents[j].x;
                float dy = agents[i].y - agents[j].y;
                float dist = std::sqrt(dx*dx + dy*dy);
                if (dist > contagion_radius) continue;

                // Familiarity modulates contagion
                float fam = get_rel(agents[i].id, agents[j].id).familiarity;
                float mod = 0.3f + 0.7f * fam;  // strangers: 30%, friends: 100%

                float proximity = 1.0f - dist / contagion_radius;
                float transfer = contagion_rate * proximity * mod;

                // Stress flows from high to low
                float stress_diff = agents[i].stress - agents[j].stress;
                auto& si = registry.get<StressComponent>(agents[i].entity);
                auto& sj = registry.get<StressComponent>(agents[j].entity);
                si.value -= stress_diff * transfer;
                sj.value += stress_diff * transfer;
                si.value = std::clamp(si.value, 0.0f, 1.0f);
                sj.value = std::clamp(sj.value, 0.0f, 1.0f);
            }
        }
    }

    // 3. Update influence (leadership) scores
    void update_influence(
        entt::registry& registry,
        const std::vector<entt::entity>& alive)
    {
        for (auto e : alive) {
            auto& soc = registry.get<SocialComponent>(e);
            auto& ps  = registry.get<PersonalityComponent>(e);
            auto& ag  = registry.get<AgentComponent>(e);
            auto& st  = registry.get<StressComponent>(e);

            // Influence = compliance * (1 - stress) * familiarity_sum
            float fam_sum = 0.0f;
            for (int other = 0; other < max_agents_; other++) {
                if (other == ag.id) continue;
                fam_sum += get_rel(ag.id, other).familiarity;
            }
            float fam_avg = alive.size() > 1 ? fam_sum / (alive.size() - 1) : 0.0f;

            // Target influence
            float target = ps.compliance * (1.0f - st.value) * (0.3f + 0.7f * fam_avg);
            // Smooth transition
            soc.influence += (target - soc.influence) * 0.05f;
        }
    }

    // 4. Update mood for all agents
    void update_mood(
        entt::registry& registry,
        const std::vector<entt::entity>& alive)
    {
        for (auto e : alive) {
            auto& soc   = registry.get<SocialComponent>(e);
            auto& needs = registry.get<NeedsComponent>(e);
            auto& st    = registry.get<StressComponent>(e);

            // Mood = inverse of average need level + stress penalty
            float need_avg = (needs.hunger + needs.rest + needs.social
                            + needs.expression + needs.purpose) / 5.0f;
            float target_mood = (1.0f - need_avg) * (1.0f - st.value * 0.5f);
            soc.mood += (target_mood - soc.mood) * 0.1f;
            soc.mood = std::clamp(soc.mood, 0.0f, 1.0f);
        }
    }

    // 5. Decay familiarity over time (relationships fade without contact)
    void decay_relationships(int tick, float decay_rate = 0.0001f) {
        for (int i = 0; i < max_agents_; i++) {
            for (int j = 0; j < max_agents_; j++) {
                if (i == j) continue;
                auto& rel = get_rel(i, j);
                if (rel.familiarity <= 0) continue;
                int ticks_since = tick - (int)rel.last_tick;
                if (ticks_since > 100) {
                    rel.familiarity -= decay_rate * (ticks_since - 100);
                    rel.familiarity = std::max(0.0f, rel.familiarity);
                }
                // Trust drifts toward 0.5 over time
                rel.trust += (0.5f - rel.trust) * 0.001f;
            }
        }
    }

private:
    int max_agents_;
    std::vector<RelationshipEntry> rels_;
};
