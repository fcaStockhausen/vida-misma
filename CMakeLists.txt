cmake_minimum_required(VERSION 3.20)
project(vida_misma VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VIDA_BUILD_GUI "Build the SDL2 graphical executable" ON)

# --- Dependencies via FetchContent ---
include(FetchContent)

# EnTT (ECS)
FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.13.2
)
FetchContent_MakeAvailable(entt)

# tomlplusplus (config)
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

set(VIDA_SIM_SOURCES
  src/simulation.cpp
  src/director.cpp
  src/sim_director.cpp
  src/sim_lifecycle.cpp
  src/sim_policy.cpp
  src/sim_space_policy.cpp
  src/sim_utility.cpp
  src/sim_targets.cpp
  src/sim_movement.cpp
  src/sim_execute.cpp
  src/sim_conveyor.cpp
)

# --- SDL2 GUI executable (primary) ---
if(VIDA_BUILD_GUI)
  find_package(SDL2 2.0.18 CONFIG QUIET)
  find_package(SDL2_ttf CONFIG QUIET)

  if((TARGET SDL2::SDL2 OR TARGET SDL2::SDL2-static) AND
     (TARGET SDL2_ttf::SDL2_ttf OR TARGET SDL2_ttf::SDL2_ttf-static))
    if(TARGET SDL2::SDL2)
      set(VIDA_SDL2_TARGET SDL2::SDL2)
    else()
      set(VIDA_SDL2_TARGET SDL2::SDL2-static)
    endif()
    if(TARGET SDL2_ttf::SDL2_ttf)
      set(VIDA_SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf)
    else()
      set(VIDA_SDL2_TTF_TARGET SDL2_ttf::SDL2_ttf-static)
    endif()

    set(VIDA_SDL_LIBRARIES ${VIDA_SDL2_TARGET} ${VIDA_SDL2_TTF_TARGET})
    if(TARGET SDL2::SDL2main)
      list(PREPEND VIDA_SDL_LIBRARIES SDL2::SDL2main)
    endif()
    message(STATUS "SDL2: CMake package targets")
  else()
    # Fallback for package managers that install libraries without CMake configs.
    find_path(VIDA_SDL2_INCLUDE_DIR NAMES SDL2/SDL.h
      PATHS /opt/homebrew/include /usr/local/include)
    find_library(VIDA_SDL2_LIBRARY NAMES SDL2
      PATHS /opt/homebrew/lib /usr/local/lib)
    find_path(VIDA_SDL2_TTF_INCLUDE_DIR NAMES SDL2/SDL_ttf.h
      PATHS /opt/homebrew/include /usr/local/include)
    find_library(VIDA_SDL2_TTF_LIBRARY NAMES SDL2_ttf
      PATHS /opt/homebrew/lib /usr/local/lib)
    find_library(VIDA_SDL2_MAIN_LIBRARY NAMES SDL2main
      PATHS /opt/homebrew/lib /usr/local/lib)

    if(NOT VIDA_SDL2_INCLUDE_DIR OR NOT VIDA_SDL2_LIBRARY)
      message(FATAL_ERROR "SDL2 not found; install its development package or provide CMAKE_PREFIX_PATH")
    endif()
    if(NOT VIDA_SDL2_TTF_INCLUDE_DIR OR NOT VIDA_SDL2_TTF_LIBRARY)
      message(FATAL_ERROR "SDL2_ttf not found; install its development package or provide CMAKE_PREFIX_PATH")
    endif()

    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${VIDA_SDL2_INCLUDE_DIR})
    check_cxx_source_compiles("#include <SDL2/SDL.h>
      #if !SDL_VERSION_ATLEAST(2, 0, 18)
      #error SDL 2.0.18 or newer is required
      #endif
      int main() { return 0; }" VIDA_SDL2_VERSION_SUPPORTED)
    unset(CMAKE_REQUIRED_INCLUDES)
    if(NOT VIDA_SDL2_VERSION_SUPPORTED)
      message(FATAL_ERROR "SDL2 2.0.18 or newer is required")
    endif()

    set(VIDA_SDL_LIBRARIES ${VIDA_SDL2_LIBRARY} ${VIDA_SDL2_TTF_LIBRARY})
    if(VIDA_SDL2_MAIN_LIBRARY)
      list(PREPEND VIDA_SDL_LIBRARIES ${VIDA_SDL2_MAIN_LIBRARY})
    endif()
    set(VIDA_SDL_INCLUDE_DIRS
      ${VIDA_SDL2_INCLUDE_DIR}
      ${VIDA_SDL2_TTF_INCLUDE_DIR}
    )
    message(STATUS "SDL2: ${VIDA_SDL2_LIBRARY}")
    message(STATUS "SDL2_ttf: ${VIDA_SDL2_TTF_LIBRARY}")
  endif()

  add_executable(vida_gui
    src/main_gui.cpp
    src/graphical_view.cpp
    src/font_cache.cpp
    src/config.cpp
    ${VIDA_SIM_SOURCES}
  )
  target_link_libraries(vida_gui PRIVATE
    EnTT::EnTT
    tomlplusplus::tomlplusplus
    ${VIDA_SDL_LIBRARIES}
  )
  target_include_directories(vida_gui PRIVATE
    src
    ${VIDA_SDL_INCLUDE_DIRS}
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vida_gui PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers)
  endif()
endif()

# --- Batch runner (headless) ---
add_executable(vida_batch
  src/batch_main.cpp
  src/config.cpp
  ${VIDA_SIM_SOURCES}
)
target_link_libraries(vida_batch PRIVATE
  EnTT::EnTT
  tomlplusplus::tomlplusplus
)
target_include_directories(vida_batch PRIVATE src)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(vida_batch PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(vida_tests
    tests/simulation_tests.cpp
    ${VIDA_SIM_SOURCES}
  )
  target_link_libraries(vida_tests PRIVATE
    EnTT::EnTT
    tomlplusplus::tomlplusplus
  )
  target_include_directories(vida_tests PRIVATE src)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vida_tests PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers)
  endif()

  add_test(NAME vida.metrics COMMAND vida_tests)
  set_tests_properties(vida.metrics PROPERTIES TIMEOUT 60)

  add_test(NAME vida.cli_metrics
    COMMAND ${CMAKE_COMMAND}
      "-DVIDA_BATCH=$<TARGET_FILE:vida_batch>"
      "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_metrics.cmake
  )
  set_tests_properties(vida.cli_metrics PROPERTIES
    TIMEOUT 60
  )

  add_test(NAME vida.policy_audit
    COMMAND ${CMAKE_COMMAND}
      "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_policy_audit.cmake
  )

  add_test(NAME vida.cli_replay
    COMMAND ${CMAKE_COMMAND}
      "-DVIDA_BATCH=$<TARGET_FILE:vida_batch>"
      "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_replay.cmake
  )
  set_tests_properties(vida.cli_replay PROPERTIES TIMEOUT 60)
endif()
