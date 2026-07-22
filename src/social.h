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
    SocialFabric(int max_agents, bool learning_enabled = true)
        : max_agents_(max_agents),
          rels_(max_agents * max_agents),
          learning_enabled_(learning_enabled) {}

    RelationshipEntry& get_rel(int a, int b) {
        return rels_[a * max_agents_ + b];
    }
    const RelationshipEntry& get_rel(int a, int b) const {
        return rels_[a * max_agents_ + b];
    }

    void ensure_agent_id(int id) {
        if (id < max_agents_) return;
        int new_size = std::max(id + 1, std::max(1, max_agents_ * 2));
        std::vector<RelationshipEntry> expanded(new_size * new_size);
        for (int a = 0; a < max_agents_; a++)
            for (int b = 0; b < max_agents_; b++)
                expanded[a * new_size + b] = rels_[a * max_agents_ + b];
        rels_ = std::move(expanded);
        max_agents_ = new_size;
    }

    // Positive interaction observed by both participants.
    void process_interaction(int agent_a, int agent_b, int tick) {
        if (!learning_enabled_) return;
        auto& rel_ab = get_rel(agent_a, agent_b);
        auto& rel_ba = get_rel(agent_b, agent_a);
        reinforce(rel_ab, tick, 0.05f, 0.03f);
        reinforce(rel_ba, tick, 0.05f, 0.03f);
    }

    // Repeated proximity makes people known, but is not evidence of goodwill.
    void record_copresence(int agent_a, int agent_b, int tick) {
        if (!learning_enabled_) return;
        auto& rel_ab = get_rel(agent_a, agent_b);
        auto& rel_ba = get_rel(agent_b, agent_a);
        reinforce(rel_ab, tick, 0.001f, 0.0f);
        reinforce(rel_ba, tick, 0.001f, 0.0f);
    }

    // A shared effective task is reciprocal evidence of reliability.
    void record_collaboration(int agent_a, int agent_b, int tick) {
        if (!learning_enabled_) return;
        auto& rel_ab = get_rel(agent_a, agent_b);
        auto& rel_ba = get_rel(agent_b, agent_a);
        reinforce(rel_ab, tick, 0.003f, 0.002f);
        reinforce(rel_ba, tick, 0.003f, 0.002f);
    }

    // Help is directional: the recipient gains trust in the helper. The helper
    // only gains familiarity from observing the recipient.
    void record_help(int helper_id, int recipient_id, int tick, float amount) {
        if (!learning_enabled_) return;
        float evidence = std::clamp(amount, 0.0f, 1.0f);
        reinforce(get_rel(recipient_id, helper_id), tick,
                  0.01f * evidence, 0.03f * evidence);
        reinforce(get_rel(helper_id, recipient_id), tick,
                  0.005f * evidence, 0.0f);
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

                // Stress flows from the more-stressed agent to the less-stressed one,
                // scaled by the receiver's susceptibility. The single transfer_ij term
                // models the bidirectional exchange (i sheds what j absorbs).
                float transfer_ij = gamma * edge_strength * stress_diff * sus_j;

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
            for (auto other_entity : alive) {
                const auto& other_agent = registry.get<AgentComponent>(other_entity);
                if (other_agent.id == ag.id) continue;
                // Influence reflects being known and trusted by living others.
                const auto& rel = get_rel(other_agent.id, ag.id);
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
        float base_grief = 0.05f,
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

            // The survivor's relationship becomes a memory.
            auto& survivor_rel = get_rel(ag.id, dead_agent_id);
            survivor_rel.familiarity *= 0.8f;
        }
    }

    // --- 6. Collaboration bonus for adjacent agents with strong ties ---
    // Returns a multiplier [1.0, 1.5] for productivity when working near friends.
    float collaboration_bonus(
        int agent_id,
        entt::registry& registry,
        const std::vector<entt::entity>& alive,
        entt::entity agent_entity,
        float fam_threshold = 0.1f,
        float trust_threshold = 0.1f) const
    {
        auto& pos = registry.get<PositionComponent>(agent_entity);
        float bonus = 0.0f;
        for (auto other : alive) {
            if (other == agent_entity) continue;
            auto& oag = registry.get<AgentComponent>(other);
            if (oag.id == agent_id) continue;

            auto& opos = registry.get<PositionComponent>(other);
            int d = std::abs(opos.x - pos.x) + std::abs(opos.y - pos.y);
            if (d > 2) continue;  // expanded from 1 to 2 for more collaboration

            const auto& rel = get_rel(agent_id, oag.id);
            if (rel.familiarity >= fam_threshold && rel.trust >= trust_threshold) {
                // Higher bonus per friend, scales with trust
                bonus += 0.2f * std::max(0.0f, rel.trust) * (0.5f + 0.5f * rel.familiarity);
            }
        }
        return 1.0f + std::min(1.0f, bonus);  // cap at 2.0x (up from 1.5x)
    }

    // --- 7. Decay relationships over time ---
    void decay_relationships(int tick, float decay_rate = 0.0001f) {
        if (!learning_enabled_) return;
        for (int i = 0; i < max_agents_; i++) {
            for (int j = 0; j < max_agents_; j++) {
                if (i == j) continue;
                auto& rel = get_rel(i, j);
                int ticks_since = tick - (int)rel.last_tick;
                if (rel.familiarity > 0.0f && ticks_since > 100) {
                    rel.familiarity -= decay_rate;
                    rel.familiarity = std::max(0.0f, rel.familiarity);
                }
                // Trust drifts toward 0 (neutral) over time
                rel.trust += (0.0f - rel.trust) * 0.001f;
            }
        }
    }

    // An observer updates only its own view of the observed actor.
    void record_negative_observation(int observer_id, int actor_id, int tick,
                                     float severity = 0.1f) {
        if (!learning_enabled_) return;
        auto& rel = get_rel(observer_id, actor_id);
        reinforce(rel, tick, 0.01f, -std::max(0.0f, severity));
    }

    // --- 9. Opinion exchange via bounded confidence (Hegselmann-Krause, doc §8.5) ---
    // Agent i only averages opinions with agent j if |x_i - x_j| < epsilon.
    // Leaders (high influence) exert stronger pull via DeGroot-weighted averaging.
    // Returns true if opinions changed.
    bool exchange_opinions(
        int agent_a, int agent_b,
        OpinionComponent& op_a, OpinionComponent& op_b,
        float influence_a, float influence_b,
        float epsilon = 0.3f)
    {
        if (!learning_enabled_) return false;
        // DeGroot weights: higher influence = more pull.
        // w_a = influence_a / (influence_a + influence_b), clamped for stability.
        float total_inf = influence_a + influence_b;
        if (total_inf < 0.01f) return false;
        float w_a = influence_a / total_inf;  // weight of A's opinion in the blend
        float w_b = 1.0f - w_a;              // weight of B's opinion

        // Learning rate: trust amplifies how much each agent shifts.
        const auto& rel_ab = get_rel(agent_a, agent_b);
        const auto& rel_ba = get_rel(agent_b, agent_a);
        float lr_a = 0.1f * (0.5f + 0.5f * std::max(0.0f, rel_ab.trust));
        float lr_b = 0.1f * (0.5f + 0.5f * std::max(0.0f, rel_ba.trust));

        bool changed = false;
        for (int d = 0; d < OpinionComponent::DIMS; d++) {
            float diff = std::abs(op_a.values[d] - op_b.values[d]);
            if (diff < epsilon) {
                // Bounded confidence: close enough to influence each other
                // DeGroot weighted average: each agent moves toward the blend
                float blend = w_a * op_a.values[d] + w_b * op_b.values[d];

                float old_a = op_a.values[d];
                float old_b = op_b.values[d];
                op_a.values[d] += (blend - op_a.values[d]) * lr_a;
                op_b.values[d] += (blend - op_b.values[d]) * lr_b;

                // Clamp to [0, 1]
                op_a.values[d] = std::clamp(op_a.values[d], 0.0f, 1.0f);
                op_b.values[d] = std::clamp(op_b.values[d], 0.0f, 1.0f);

                if (std::abs(op_a.values[d] - old_a) > 0.001f ||
                    std::abs(op_b.values[d] - old_b) > 0.001f)
                    changed = true;
            }
        }
        return changed;
    }

    // --- 10. Euclidean distance between two opinion vectors ---
    static float opinion_distance(const OpinionComponent& a, const OpinionComponent& b) {
        float sum = 0.0f;
        for (int d = 0; d < OpinionComponent::DIMS; d++) {
            float diff = a.values[d] - b.values[d];
            sum += diff * diff;
        }
        return std::sqrt(sum);
    }

private:
    static void reinforce(RelationshipEntry& rel, int tick,
                          float familiarity_gain, float trust_gain) {
        rel.familiarity = std::clamp(
            rel.familiarity + familiarity_gain * (1.0f - rel.familiarity),
            0.0f, 1.0f);
        rel.trust = std::clamp(rel.trust + trust_gain, -1.0f, 1.0f);
        rel.last_tick = static_cast<float>(tick);
    }

    int max_agents_;
    std::vector<RelationshipEntry> rels_;
    bool learning_enabled_;
};
