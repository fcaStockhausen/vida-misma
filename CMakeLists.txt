cmake_minimum_required(VERSION 3.20)
project(vida_misma VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# FTXUI (terminal UI)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
  GIT_TAG v6.0.1
)
FetchContent_MakeAvailable(ftxui)

set(VIDA_SIM_SOURCES
  src/simulation.cpp
  src/sim_utility.cpp
  src/sim_targets.cpp
  src/sim_movement.cpp
  src/sim_execute.cpp
  src/sim_conveyor.cpp
)

# --- Main executable (interactive TUI) ---
add_executable(vida_misma
  src/main.cpp
  src/config.cpp
  ${VIDA_SIM_SOURCES}
)

# --- Batch runner (no TUI) ---
add_executable(vida_batch
  src/batch_main.cpp
  src/config.cpp
  ${VIDA_SIM_SOURCES}
)

# --- Link both targets ---
foreach(tgt vida_misma vida_batch)
  target_link_libraries(${tgt} PRIVATE
    EnTT::EnTT
    tomlplusplus::tomlplusplus
  )
  target_include_directories(${tgt} PRIVATE src)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers)
  endif()
endforeach()

# FTXUI only for interactive target
target_link_libraries(vida_misma PRIVATE ftxui::screen ftxui::dom ftxui::component)

# --- SDL2 GUI executable ---
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(vida_gui
    src/main_gui.cpp
    src/config.cpp
    ${VIDA_SIM_SOURCES}
  )
  target_link_libraries(vida_gui PRIVATE
    EnTT::EnTT
    tomlplusplus::tomlplusplus
    SDL2::SDL2
    SDL2::SDL2main
  )
  target_include_directories(vida_gui PRIVATE src)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(vida_gui PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers)
  endif()
else()
  message(STATUS "SDL2 not found — vida_gui target skipped")
endif()
