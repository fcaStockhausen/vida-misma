#pragma once
// Production chain module for La Vida Misma.
//
// Centralizes ALL production chain logic in one place:
// - Colony state assessment (what do we need?)
// - Machine routing (which machine type should an agent work?)
// - Chain bottleneck detection (what's the limiting step?)
//
// This decouples the production chain from sim_utility.cpp (drives),
// sim_targets.cpp (routing), and sim_execute.cpp (execution).
// Those modules call ProductionChain::assess() to get a snapshot,
// then use it for decision-making.

#include "grid.h"
#include <algorithm>

struct ColonyProduction {
    // Machine counts
    int food_machines = 0;
    int mat_machines = 0;
    int output_machines = 0;

    // Storage totals
    float food = 0.0f;
    float raw_material = 0.0f;
    float construction_material = 0.0f;
    float output = 0.0f;

    // Derived: what the colony needs most
    enum class Need {
        NONE,               // everything satisfied
        GATHER_RAW,         // need to gather raw materials
        OPERATE_FOOD,       // need to work food machines
        OPERATE_MATERIALS,  // need to work materials machines
        OPERATE_OUTPUT,     // need to work output machines
        BUILD_OUTPUT,       // need to build more output machines
        HAUL_OUTPUT,        // need to haul output to Exit
    };

    Need primary_need = Need::NONE;
    Need secondary_need = Need::NONE;

    // Chain health: 0 = broken, 1 = perfect flow
    float chain_health = 0.0f;

    // Bottleneck description for debugging
    const char* bottleneck = "none";

    int alive_count = 0;
    float quota_fill = 0.0f;
};

class ProductionChain {
public:
    // Assess the colony's production state by scanning the grid.
    // Call once per tick (before utility/routing decisions).
    static ColonyProduction assess(const Grid& grid, int alive, float quota_fill) {
        ColonyProduction cp;
        cp.alive_count = alive;
        cp.quota_fill = quota_fill;

        // Count machines and sum storage
        for (int y = 0; y < grid.height(); y++)
            for (int x = 0; x < grid.width(); x++) {
                auto t = grid.at(x, y);
                auto& d = grid.data_at(x, y);
                if (t == TileType::Machine && d.built) {
                    switch (d.machine_type) {
                        case MachineType::Food:      cp.food_machines++; break;
                        case MachineType::Materials: cp.mat_machines++; break;
                        case MachineType::Output:    cp.output_machines++; break;
                        default: break;
                    }
                }
                if (t == TileType::Storage && d.built) {
                    cp.food += d.stored_food;
                    cp.raw_material += d.stored_raw_material;
                    cp.construction_material += d.stored_construction_material;
                    cp.output += d.stored_output;
                }
            }

        // Machine-to-agent ratios
        float food_ratio = (alive > 0) ? (float)cp.food_machines / alive : 0;
        float mat_ratio = (alive > 0) ? (float)cp.mat_machines / alive : 0;

        // Determine primary and secondary needs
        float c_mat_threshold = std::max(2.0f, alive * 0.3f);  // need buffer proportional to pop
        if (cp.food_machines == 0) {
            cp.primary_need = ColonyProduction::Need::BUILD_OUTPUT;
            cp.bottleneck = "no food machines";
        } else if (cp.food < alive * 0.5f) {
            cp.primary_need = ColonyProduction::Need::OPERATE_FOOD;
            cp.bottleneck = "food production too low";
        } else if (cp.output_machines == 0 && cp.construction_material > 0.5f) {
            cp.primary_need = ColonyProduction::Need::BUILD_OUTPUT;
            cp.bottleneck = "no output machines, have c_mat";
        } else if (cp.output_machines > 0 && cp.construction_material < c_mat_threshold) {
            // c_mat running low — need Materials workers FIRST
            cp.primary_need = ColonyProduction::Need::OPERATE_MATERIALS;
            cp.secondary_need = ColonyProduction::Need::OPERATE_OUTPUT;
            cp.bottleneck = "c_mat supply too low for output chain";
        } else if (cp.output_machines > 0 && cp.construction_material >= c_mat_threshold) {
            // c_mat healthy — operate Output, but keep Materials as secondary
            cp.primary_need = ColonyProduction::Need::OPERATE_OUTPUT;
            cp.secondary_need = ColonyProduction::Need::OPERATE_MATERIALS;
            cp.bottleneck = "output machines need workers";
        } else if (cp.output_machines == 0 && cp.mat_machines == 0 && cp.food_machines > 0) {
            cp.primary_need = ColonyProduction::Need::OPERATE_MATERIALS;
            cp.bottleneck = "need materials machines";
        } else if (cp.output > 0.5f && quota_fill < 0.5f) {
            cp.primary_need = ColonyProduction::Need::HAUL_OUTPUT;
            cp.bottleneck = "output stranded, needs hauling to Exit";
        } else {
            cp.primary_need = ColonyProduction::Need::NONE;
            cp.bottleneck = "chain flowing";
        }

        // Chain health: product of stage completeness
        float stage1 = std::min(1.0f, (float)cp.food_machines / std::max(1, alive / 8));
        float stage2 = std::min(1.0f, (float)cp.mat_machines / std::max(1, alive / 8));
        float stage3 = std::min(1.0f, (float)cp.output_machines / 2.0f);
        float stage4 = std::min(1.0f, cp.construction_material / 5.0f);
        float stage5 = std::min(1.0f, cp.output / 5.0f);
        cp.chain_health = stage1 * stage2 * stage3 * stage4 * stage5;

        return cp;
    }

    // Map a colony need to a machine type preference
    static void need_to_preferences(
        ColonyProduction::Need need,
        bool& prefer_food, bool& prefer_output, bool& prefer_materials)
    {
        prefer_food = prefer_output = prefer_materials = false;
        switch (need) {
            case ColonyProduction::Need::OPERATE_FOOD:
                prefer_food = true; break;
            case ColonyProduction::Need::OPERATE_OUTPUT:
                prefer_output = true; break;
            case ColonyProduction::Need::OPERATE_MATERIALS:
                prefer_materials = true; break;
            default: break; // balanced / neutral
        }
    }
};
