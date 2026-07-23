if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/src/sim_policy.cpp" policy_source)
set(forbidden_terms
  "registry_"
  "rng_"
  "AgentComponent"
  "PersonalityComponent"
  "OpinionComponent"
  "SocialComponent"
  "SocialFabric"
  "faction"
  "trust"
  "noncompliance"
  "ActionType"
  "agent_id"
  "stored_output"
  "MachineType::Output"
  "last_quota_fill"
)

foreach(term IN LISTS forbidden_terms)
  string(FIND "${policy_source}" "${term}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "canonical policy contains forbidden dependency: ${term}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/sim_targets.cpp" target_source)
foreach(term "agent.id %" "agent.id%" "agent.id % 10")
  string(FIND "${target_source}" "${term}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "target routing contains identity partition: ${term}")
  endif()
endforeach()

foreach(source_file sim_utility.cpp sim_targets.cpp sim_execute.cpp simulation.cpp)
  file(READ "${SOURCE_DIR}/src/${source_file}" behavior_source)
  string(FIND "${behavior_source}" "StressState::REDEEMED" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${source_file} still gives REDEEMED behavioral meaning")
  endif()
endforeach()

foreach(source_file sim_utility.cpp sim_targets.cpp sim_execute.cpp sim_policy.cpp sim_space_policy.cpp)
  file(READ "${SOURCE_DIR}/src/${source_file}" behavior_source)
  foreach(term "faction_id" "community_id")
    string(FIND "${behavior_source}" "${term}" position)
    if(NOT position EQUAL -1)
      message(FATAL_ERROR "${source_file} consumes an observational group label: ${term}")
    endif()
  endforeach()
endforeach()

foreach(source_file sim_utility.cpp sim_targets.cpp sim_execute.cpp sim_movement.cpp sim_lifecycle.cpp)
  file(READ "${SOURCE_DIR}/src/${source_file}" agent_source)
  string(FIND "${agent_source}" "rng_" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${source_file} consumes the shared simulation RNG")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/sim_lifecycle.cpp" lifecycle_source)
foreach(term "community_id" "archetype_traits(" "archetype_opinion_priors(")
  string(FIND "${lifecycle_source}" "${term}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "lifecycle predestines a descendant label: ${term}")
  endif()
endforeach()

foreach(source_file sim_utility.cpp sim_targets.cpp)
  file(READ "${SOURCE_DIR}/src/${source_file}" cultural_target_source)
  string(FIND "${cultural_target_source}" "TileType::OpenSpace" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${source_file} still hard-gates cultural action to OpenSpace")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/sim_space_policy.cpp" space_policy_source)
set(space_forbidden_terms
  "PersonalityComponent"
  "OpinionComponent"
  "SocialComponent"
  "SocialFabric"
  "faction"
  "trust"
  "noncompliance"
  "ActionType"
  "agent_id"
  "TileType::HiddenSpace"
)

foreach(term IN LISTS space_forbidden_terms)
  string(FIND "${space_policy_source}" "${term}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "space policy contains forbidden dependency: ${term}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/director.h" director_api)
file(READ "${SOURCE_DIR}/src/sim_director.cpp" director_source)
foreach(term "agent_id" "ActionType" "Utility" "target_x" "target_y"
             "PersonalityComponent" "OpinionComponent" "SocialComponent")
  string(FIND "${director_api}" "${term}" api_position)
  string(FIND "${director_source}" "${term}" source_position)
  if(NOT api_position EQUAL -1 OR NOT source_position EQUAL -1)
    message(FATAL_ERROR "Director boundary contains behavioral dependency: ${term}")
  endif()
endforeach()
