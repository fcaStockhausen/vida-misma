if(NOT DEFINED VIDA_BATCH OR NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "VIDA_BATCH and SOURCE_DIR are required")
endif()

function(run_metrics output_name)
  execute_process(
    COMMAND "${VIDA_BATCH}" metrics 20 42
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "vida_batch metrics failed: ${error_output}")
  endif()
  set(${output_name} "${output}" PARENT_SCOPE)
endfunction()

run_metrics(first)
run_metrics(second)

if(NOT first STREQUAL second)
  message(FATAL_ERROR "metrics output is not deterministic within the same build")
endif()

string(JSON schema ERROR_VARIABLE json_error GET "${first}" schema_version)
if(json_error OR NOT schema EQUAL 3)
  message(FATAL_ERROR "invalid metrics JSON schema: ${json_error}")
endif()
string(JSON default_supply_variant GET "${first}" factory supply_variant)
string(JSON default_policy_variant GET "${first}" factory policy_variant)
if(NOT default_supply_variant EQUAL 1)
  message(FATAL_ERROR "default metrics run must use supply variant 1")
endif()
if(NOT default_policy_variant EQUAL 1)
  message(FATAL_ERROR "default metrics run must use policy variant 1")
endif()

foreach(section population demography factory actions resources machines social emergence needs skills events)
  string(JSON value ERROR_VARIABLE section_error GET "${first}" "${section}")
  if(section_error)
    message(FATAL_ERROR "missing or invalid '${section}' section: ${section_error}")
  endif()
endforeach()

string(JSON initial_population GET "${first}" population initial)
string(JSON alive_population GET "${first}" population alive)
string(JSON ever_created GET "${first}" population ever_created)
string(JSON arrival_count GET "${first}" population arrivals)
string(JSON birth_count GET "${first}" population births)
string(JSON death_object GET "${first}" population deaths)
set(total_deaths 0)
foreach(cause starvation exhaustion breakdown suicide natural other)
  string(JSON cause_count GET "${first}" population deaths "${cause}")
  math(EXPR total_deaths "${total_deaths} + ${cause_count}")
endforeach()
math(EXPR expected_created "${initial_population} + ${arrival_count} + ${birth_count}")
math(EXPR expected_accounted "${alive_population} + ${total_deaths}")
if(NOT ever_created EQUAL expected_created OR NOT ever_created EQUAL expected_accounted)
  message(FATAL_ERROR "invalid historical population accounting")
endif()

string(JSON action_count LENGTH "${first}" actions)
if(NOT action_count EQUAL 13)
  message(FATAL_ERROR "expected 13 action entries, got ${action_count}")
endif()

execute_process(
  COMMAND "${VIDA_BATCH}" metrics 20 42 calm 1 -1 -1 0 0 1 0 0 0 0 0 0
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE counterfactual_result
  OUTPUT_VARIABLE counterfactual
  ERROR_VARIABLE counterfactual_error
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT counterfactual_result EQUAL 0)
  message(FATAL_ERROR "culture counterfactual failed: ${counterfactual_error}")
endif()
string(JSON social_learning GET "${counterfactual}" emergence toggles social_learning)
string(JSON spatial_affinity GET "${counterfactual}" emergence toggles spatial_affinity)
string(JSON artifact_effects GET "${counterfactual}" emergence toggles artifact_effects)
string(JSON natural_mortality GET "${counterfactual}" demography toggles natural_mortality)
string(JSON arrivals GET "${counterfactual}" demography toggles arrivals)
string(JSON reproduction GET "${counterfactual}" demography toggles reproduction)
if(social_learning OR spatial_affinity OR artifact_effects
   OR natural_mortality OR arrivals OR reproduction)
  message(FATAL_ERROR "culture counterfactual toggles were not applied")
endif()

string(JSON resource_count LENGTH "${first}" resources)
if(NOT resource_count EQUAL 5)
  message(FATAL_ERROR "expected 5 resource entries, got ${resource_count}")
endif()

execute_process(
  COMMAND "${VIDA_BATCH}" metrics 20 42 normal 1 5 15 5 0
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE experiment_result
  OUTPUT_VARIABLE experiment
  ERROR_VARIABLE experiment_error
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT experiment_result EQUAL 0)
  message(FATAL_ERROR "supply experiment failed: ${experiment_error}")
endif()
string(JSON supply_variant GET "${experiment}" factory supply_variant)
string(JSON blocked_ticks GET "${experiment}" factory shipping_blocked_ticks)
string(JSON sample_count LENGTH "${experiment}" timeline)
string(JSON requested GET "${experiment}" resources RAW_FOOD regeneration_requested)
string(JSON restructure_probability GET "${experiment}" factory restructure_probability)
string(JSON policy_variant GET "${experiment}" factory policy_variant)
if(NOT supply_variant EQUAL 1 OR NOT blocked_ticks EQUAL 10
   OR NOT sample_count EQUAL 4 OR NOT restructure_probability EQUAL 0
   OR NOT policy_variant EQUAL 1)
  message(FATAL_ERROR "invalid supply experiment contract")
endif()
if(requested LESS 0)
  message(FATAL_ERROR "invalid regeneration-request metric")
endif()

execute_process(
  COMMAND "${VIDA_BATCH}" analysis 20 42 1
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE analysis_result
  OUTPUT_VARIABLE analysis_output
  ERROR_VARIABLE analysis_error
)
if(NOT analysis_result EQUAL 0)
  message(FATAL_ERROR "canonical analysis failed: ${analysis_error}")
endif()
string(FIND "${analysis_output}" "Restructures vs factions" forbidden_metric)
if(NOT forbidden_metric EQUAL -1)
  message(FATAL_ERROR "canonical analysis exposes legacy faction-targeting metric")
endif()

execute_process(
  COMMAND "${VIDA_BATCH}" metrics 1 42 normal invalid
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE invalid_supply_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(invalid_supply_result EQUAL 0)
  message(FATAL_ERROR "metrics accepts a nonnumeric supply variant")
endif()

execute_process(
  COMMAND "${VIDA_BATCH}" analysis 1 42 invalid
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE invalid_policy_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(invalid_policy_result EQUAL 0)
  message(FATAL_ERROR "analysis accepts a nonnumeric policy variant")
endif()
