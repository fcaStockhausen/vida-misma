if(NOT DEFINED VIDA_BATCH OR NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "VIDA_BATCH and SOURCE_DIR are required")
endif()

set(fixture "${SOURCE_DIR}/tests/replay_fixture.toml")
execute_process(
  COMMAND "${VIDA_BATCH}" replay 30 42 "${fixture}"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE first_result
  OUTPUT_VARIABLE first_output
  ERROR_VARIABLE first_error
)
if(NOT first_result EQUAL 0)
  message(FATAL_ERROR "first replay failed: ${first_error}")
endif()

execute_process(
  COMMAND "${VIDA_BATCH}" replay 30 42 "${fixture}"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE second_result
  OUTPUT_VARIABLE second_output
  ERROR_VARIABLE second_error
)
if(NOT second_result EQUAL 0)
  message(FATAL_ERROR "second replay failed: ${second_error}")
endif()
if(NOT first_output STREQUAL second_output)
  message(FATAL_ERROR "same-build replay output is not byte-identical")
endif()

string(JSON schema GET "${first_output}" schema_version)
string(JSON command GET "${first_output}" command)
string(JSON interventions GET "${first_output}" interventions)
if(NOT schema EQUAL 1 OR NOT command STREQUAL "replay" OR NOT interventions EQUAL 2)
  message(FATAL_ERROR "replay output contract is invalid: ${first_output}")
endif()

execute_process(
  COMMAND "${VIDA_BATCH}" replay 30 7 "${fixture}"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE mismatch_result
  OUTPUT_VARIABLE mismatch_output
  ERROR_VARIABLE mismatch_error
)
if(mismatch_result EQUAL 0)
  message(FATAL_ERROR "replay accepted a seed that differs from the log")
endif()

file(READ "${fixture}" mismatched_config_fixture)
string(REGEX REPLACE
  "config_fingerprint = \"[0-9a-f]+\""
  "config_fingerprint = \"0000000000000000\""
  mismatched_config_fixture "${mismatched_config_fixture}")
set(mismatched_config_path "${CMAKE_CURRENT_BINARY_DIR}/replay_config_mismatch.toml")
file(WRITE "${mismatched_config_path}" "${mismatched_config_fixture}")
execute_process(
  COMMAND "${VIDA_BATCH}" replay 30 42 "${mismatched_config_path}"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE config_mismatch_result
  ERROR_VARIABLE config_mismatch_error
)
file(REMOVE "${mismatched_config_path}")
if(config_mismatch_result EQUAL 0)
  message(FATAL_ERROR "replay accepted a configuration that differs from the log")
endif()
