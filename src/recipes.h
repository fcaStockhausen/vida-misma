#pragma once
// ============================================================
// Recipe System — formal definition of what each structure costs
// and where it can be built.
//
// This documents (centrally) the build rules that sim_execute.cpp's
// BUILD case already applies ad-hoc. It is the reference for the
// colony blueprint (production.h BuildStep) and the build-target
// selectors (sim_targets.cpp, grid.h find_*_site).
//
// Production chain (3-tier):
//   ScrapPile --GATHER--> raw_material
//     --BUILD--> MaterialsMachine (on ScrapPile)
//     --WORK-->  raw_material → construction_material (c_mat)
//     --BUILD--> OutputMachine (on Floor, costs c_mat)
//     --WORK-->  construction_material → output product
//     --haul/conveyor--> Exit-adjacent Storage → quota drain
//
// Food chain (parallel):
//   FoodSource --GATHER--> raw_food
//     --BUILD--> FoodMachine (on FoodSource)
//     --WORK-->  raw_food → food (eaten by agents)
// ============================================================

#include "components.h"

struct Recipe {
    MachineType  output;            // what machine this builds
    ResourceType input_material;    // what resource the build consumes
    float        input_amount;      // build cost (progress units)
    TileType     required_tile;     // tile the agent must stand on to build it
};

namespace Recipes {

// FoodMachine: raw_food source, costs raw_material.
//   Built on a FoodSource tile. Produces processed food from raw_food.
inline constexpr Recipe FOOD = {
    MachineType::Food,
    ResourceType::RAW_MATERIAL,
    0.15f,
    TileType::FoodSource
};

// MaterialsMachine: tier 1 of the production chain.
//   Built on a ScrapPile tile. Converts raw_material → construction_material.
inline constexpr Recipe MATERIALS = {
    MachineType::Materials,
    ResourceType::RAW_MATERIAL,
    0.15f,
    TileType::ScrapPile
};

// OutputMachine: tier 2 of the production chain.
//   Built on a Floor tile. Costs construction_material (not raw_material).
//   Converts construction_material → output product (shipped as quota).
//   Placement should be strategic — between Materials machines (c_mat source)
//   and the Exit (output destination). See grid.h find_output_machine_site.
inline constexpr Recipe OUTPUT = {
    MachineType::Output,
    ResourceType::CONSTRUCTION_MATERIAL,
    0.15f,
    TileType::Floor
};

} // namespace Recipes
