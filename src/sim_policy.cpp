#include "simulation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

uint64_t mix_policy_bits(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

float stable_policy_unit(int seed, int epoch, int x, int y, uint64_t salt) {
    uint64_t value = static_cast<uint32_t>(seed);
    value = mix_policy_bits(value ^ (static_cast<uint64_t>(epoch) << 32));
    value = mix_policy_bits(value ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 16));
    value = mix_policy_bits(value ^ static_cast<uint32_t>(y) ^ salt);
    return static_cast<float>((value >> 40) * (1.0 / 16777216.0));
}

}  // namespace

void Simulation::system_factory_restructure_indifferent() {
    if (tick_ % config_.restructure_interval != 0) return;
    int epoch = tick_ / config_.restructure_interval;
    if (stable_policy_unit(config_.seed, epoch, 0, 0, 0x41ULL)
        >= config_.restructure_probability) {
        return;
    }

    enum class PhysicalKind : uint8_t { Conveyor, Storage };
    struct Candidate {
        int x;
        int y;
        PhysicalKind kind;
        float priority;
    };

    std::vector<Candidate> candidates;
    for (int y = 0; y < grid_.height(); y++)
        for (int x = 0; x < grid_.width(); x++) {
            TileType type = grid_.at(x, y);
            const auto& data = grid_.data_at(x, y);
            if (type == TileType::Conveyor && data.built
                && data.conveyor_condition > 0.2f) {
                float wear = 1.0f - std::clamp(data.conveyor_condition, 0.0f, 1.0f);
                float load = std::clamp(data.conveyor_contents
                    / std::max(0.001f, config_.conveyor_throughput), 0.0f, 1.0f);
                float jitter = stable_policy_unit(
                    config_.seed, epoch, x, y, 0x92ULL) * 0.05f;
                candidates.push_back({x, y, PhysicalKind::Conveyor,
                    wear * 0.6f + load * 0.4f + jitter});
            } else if (type == TileType::Storage && data.storage_capacity > 0.0f
                       && data.total_stored() > 0.001f) {
                float load = std::clamp(
                    data.total_stored() / data.storage_capacity, 0.0f, 1.0f);
                float jitter = stable_policy_unit(
                    config_.seed, epoch, x, y, 0xb3ULL) * 0.05f;
                candidates.push_back({x, y, PhysicalKind::Storage,
                    load * 0.4f + jitter});
            }
        }

    if (candidates.empty()) return;
    auto chosen = std::max_element(candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return left.priority < right.priority;
        });

    auto& data = grid_.data_at(chosen->x, chosen->y);
    if (chosen->kind == PhysicalKind::Conveyor) {
        data.conveyor_condition = std::max(
            0.2f, data.conveyor_condition - 0.15f);
        emit_log(-1, "MAINTENANCE load adjustment at ("
            + std::to_string(chosen->x) + "," + std::to_string(chosen->y)
            + ") priority=" + ff2(chosen->priority),
            EventType::FACTORY_RESTRUCTURE);
    } else {
        auto removed = data.remove_stored_fraction(0.10f);
        for (size_t i = 0; i < removed.size(); i++) {
            metrics_.resources_lost[i] += removed[i];
        }
        emit_log(-1, "STORAGE load purge at ("
            + std::to_string(chosen->x) + "," + std::to_string(chosen->y)
            + ") priority=" + ff2(chosen->priority),
            EventType::FACTORY_RESTRUCTURE);
    }
    total_restructures_++;
}
