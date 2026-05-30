#pragma once
// Social fabric systems for La Vida Misma.
// Relationship tracking, emotional contagion, emergent leadership,
// grief cascades, and collaboration bonuses.
//
// Trust model: [-1, 1] where -1 = antagonism, 0 = neutral, +1 = deep trust
// Contagion: graph-edge-based per doc §17  Δstress_j = γ·|w(i,j)|·Δstress_i·susceptibility_j

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
    float trust       = 0.0f;  // [-1, +1] -1=antagonism, 0=neutral, +1=deep trust
    float last_tick   = -100;  // last interaction tick
};

struct SocialComponent {
    float social_energy = 1.0f;  // [0, 1] unused reserve for future
    float influence     = 0.0f;  // emergent leadership score
    float mood          = 0.5f;  // [0, 1] 0=miserable, 1=ecstatic
};

// ============================================================
// Social system: manages relationships, contagion, leadership
// ============================================================

class SocialFabric {
public:
    SocialFabric(int max_agents)
        : max_agents_(max_agents),
          rels_(max_agents * max_agents) {}

    RelationshipEntry& get_rel(int a, int b) {
        return rels_[a * max_agents_ + b];
    }
    const RelationshipEntry& get_rel(int a, int b) const {
        return rels_[a * max_agents_ + b];
    }

    // --- 1. Process social interaction ---
    // Positive interactions increase trust. Mood of both parties modulates.
    void process_interaction(int agent_a, int agent_b, int tick) {
        auto& rel_ab = get_rel(agent_a, agent_b);
        auto& rel_ba = get_rel(agent_b, agent_a);

        // Familiarity grows with interaction (diminishing returns)
        float fam_gain = 0.05f * (1.0f - rel_ab.familiarity);
        rel_ab.familiarity = std::min(1.0f, rel_ab.familiarity + fam_gain);
        rel_ba.familiarity = std::min(1.0f, rel_ba.familiarity + fam_gain);

        // Trust shifts toward positive with each interaction
        // Rate is higher for familiar agents (trust accelerates)
        float trust_rate = 0.03f * (0.5f + 0.5f * rel_ab.familiarity);
        rel_ab.trust = std::clamp(rel_ab.trust + trust_rate, -1.0f, 1.0f);
        rel_ba.trust = std::clamp(rel_ba.trust + trust_rate, -1.0f, 1.0f);

        rel_ab.last_tick = tick;
        rel_ba.last_tick = tick;
    }

    // --- 2. Emotional contagion via graph edges (doc §17) ---
    // Δstress_j = γ · |w(i,j)| · Δstress_i · susceptibility_j
    // where w(i,j) = trust ∈ [-1,1] (absolute value for contagion strength)
    // Only propagates along existing edges (familiarity > threshold)
    void apply_contagion(
        entt::registry& registry,
        const std::vector<entt::entity>& alive,
        float gamma = 0.02f,
        float fam_threshold = 0.05f)
    {
        // Collect stress levels
        struct Info { int id; float stress; float resilience; };
        std::vector<Info> infos;
        infos.reserve(alive.size());
        for (auto e : alive) {
            auto& ag = registry.get<AgentComponent>(e);
            auto& st = registry.get<StressComponent>(e);
            auto& ps = registry.get<PersonalityComponent>(e);
            infos.push_back({ag.id, st.value, ps.resilience});
        }

        // Build id→index map
        std::vector<int> id_to_idx(max_agents_, -1);
        for (size_t i = 0; i < infos.size(); i++) {
            id_to_idx[infos[i].id] = (int)i;
        }

        // Compute stress deltas along graph edges
        std::vector<float> deltas(infos.size(), 0.0f);
        for (size_t i = 0; i < infos.size(); i++) {
            for (size_t j = i + 1; j < infos.size(); j++) {
                const auto& rel = get_rel(infos[i].id, infos[j].id);
                if (rel.familiarity < fam_threshold) continue;

                // |w(i,j)| = absolute trust. Even antagonists transfer stress.
                float w = std::abs(rel.trust);
                // Familiarity amplifies the connection
                float edge_strength = w * (0.3f + 0.7f * rel.familiarity);

                float stress_diff = infos[i].stress - infos[j].stress;
                // susceptibility = 1 - resilience (low resilience = high susceptibility)
                float sus_j = 1.0f - infos[j].resilience;
                float sus_i = 1.0f - infos[i].resilience;

                float transfer_ij = gamma * edge_strength * stress_diff * sus_j;
                float transfer_ji = gamma * edge_strength * (-stress_diff) * sus_i;

                deltas[i] -= transfer_ij;
                deltas[j] += transfer_ij;
            }
        }

        // Apply deltas
        for (size_t i = 0; i < infos.size(); i++) {
            if (std::abs(deltas[i]) < 0.0001f) continue;
            for (auto e : alive) {
                auto& ag = registry.get<AgentComponent>(e);
                if (ag.id == infos[i].id) {
                    auto& st = registry.get<StressComponent>(e);
                    st.value = std::clamp(st.value + deltas[i], 0.0f, 1.0f);
                    break;
                }
            }
        }
    }

    // --- 3. Update influence (leadership) scores ---
    void update_influence(
        entt::registry& registry,
        const std::vector<entt::entity>& alive)
    {
        for (auto e : alive) {
            auto& soc = registry.get<SocialComponent>(e);
            auto& ps  = registry.get<PersonalityComponent>(e);
            auto& ag  = registry.get<AgentComponent>(e);
            auto& st  = registry.get<StressComponent>(e);

            float fam_sum = 0.0f;
            float trust_sum = 0.0f;
            int connections = 0;
            for (int other = 0; other < max_agents_; other++) {
                if (other == ag.id) continue;
                const auto& rel = get_rel(ag.id, other);
                if (rel.familiarity > 0.05f) {
                    fam_sum += rel.familiarity;
                    trust_sum += rel.trust;  // negative trust = less influence
                    connections++;
                }
            }
            float fam_avg = connections > 0 ? fam_sum / connections : 0.0f;
            float trust_avg = connections > 0 ? trust_sum / connections : 0.0f;

            // Influence: compliance * calmness * network centrality * trustworthiness
            float target = ps.compliance * (1.0f - st.value)
                         * (0.3f + 0.7f * fam_avg)
                         * (0.5f + 0.5f * std::max(0.0f, trust_avg));
            soc.influence += (target - soc.influence) * 0.05f;
            soc.influence = std::max(0.0f, soc.influence);
        }
    }

    // --- 4. Update mood for all agents ---
    void update_mood(
        entt::registry& registry,
        const std::vector<entt::entity>& alive)
    {
        for (auto e : alive) {
            auto& soc   = registry.get<SocialComponent>(e);
            auto& needs = registry.get<NeedsComponent>(e);
            auto& st    = registry.get<StressComponent>(e);

            float need_avg = (needs.hunger + needs.rest + needs.social
                            + needs.expression + needs.purpose) / 5.0f;
            float target_mood = (1.0f - need_avg) * (1.0f - st.value * 0.5f);
            soc.mood += (target_mood - soc.mood) * 0.1f;
            soc.mood = std::clamp(soc.mood, 0.0f, 1.0f);
        }
    }

    // --- 5. Grief cascade on agent death ---
    // When an agent dies, all agents with familiarity > threshold receive stress.
    // The amount scales with trust (trusted friends grieve more).
    void apply_grief(
        entt::registry& registry,
        int dead_agent_id,
        const std::vector<entt::entity>& alive,
        float base_grief = 0.15f,
        float fam_threshold = 0.1f)
    {
        for (auto e : alive) {
            auto& ag  = registry.get<AgentComponent>(e);
            if (ag.id == dead_agent_id) continue;

            const auto& rel = get_rel(ag.id, dead_agent_id);
            if (rel.familiarity < fam_threshold) continue;

            // Grief = base * familiarity * max(0, trust) * (1 - resilience)
            auto& ps = registry.get<PersonalityComponent>(e);
            auto& st = registry.get<StressComponent>(e);
            float grief = base_grief * rel.familiarity
                        * std::max(0.0f, rel.trust)  // only positive trust = grief
                        * (1.0f - ps.resilience);
            st.value = std::clamp(st.value + grief, 0.0f, 1.0f);

            // Relationship decays: the dead agent becomes a memory
            auto& rel_back = get_rel(dead_agent_id, ag.id);
            rel_back.familiarity *= 0.8f;  // partial fade
        }
    }

    // --- 6. Collaboration bonus for adjacent agents with strong ties ---
    // Returns a multiplier [1.0, 1.5] for productivity when working near friends.
    float collaboration_bonus(
        int agent_id,
        entt::registry& registry,
        const std::vector<entt::entity>& alive,
        entt::entity agent_entity,
        float fam_threshold = 0.3f,
        float trust_threshold = 0.3f) const
    {
        auto& pos = registry.get<PositionComponent>(agent_entity);
        float bonus = 0.0f;
        for (auto other : alive) {
            if (other == agent_entity) continue;
            auto& oag = registry.get<AgentComponent>(other);
            if (oag.id == agent_id) continue;

            auto& opos = registry.get<PositionComponent>(other);
            int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
            if (d > 1) continue;  // must be adjacent

            const auto& rel = get_rel(agent_id, oag.id);
            if (rel.familiarity >= fam_threshold && rel.trust >= trust_threshold) {
                bonus += 0.15f * rel.trust * rel.familiarity;
            }
        }
        return 1.0f + std::min(0.5f, bonus);  // cap at 1.5x
    }

    // --- 7. Decay relationships over time ---
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
                // Trust drifts toward 0 (neutral) over time
                rel.trust += (0.0f - rel.trust) * 0.001f;
            }
        }
    }

    // --- 8. Negative interaction (antagonism) ---
    // Call when an agent witnesses a transgression or suffers from another.
    void negative_interaction(int victim_id, int offender_id, int tick,
                              float severity = 0.1f) {
        auto& rel = get_rel(victim_id, offender_id);
        auto& rel_back = get_rel(offender_id, victim_id);

        rel.trust = std::clamp(rel.trust - severity, -1.0f, 1.0f);
        rel_back.trust = std::clamp(rel_back.trust - severity * 0.3f, -1.0f, 1.0f);
        rel.last_tick = tick;
    }

private:
    int max_agents_;
    std::vector<RelationshipEntry> rels_;
};
