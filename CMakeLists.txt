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

set(VIDA_SIM_SOURCES
  src/simulation.cpp
  src/sim_utility.cpp
  src/sim_targets.cpp
  src/sim_movement.cpp
  src/sim_execute.cpp
  src/sim_conveyor.cpp
)

# --- SDL2 GUI executable (primary) ---
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  message(FATAL_ERROR "SDL2 is required but was not found")
endif()

# SDL2_ttf: find header and library manually (Homebrew on macOS)
find_path(SDL2_TTF_INCLUDE_DIR NAMES SDL_ttf.h PATHS /opt/homebrew/include/SDL2 NO_DEFAULT_PATH)
find_library(SDL2_TTF_LIBRARY NAMES SDL2_ttf PATHS /opt/homebrew/lib NO_DEFAULT_PATH)

if(NOT SDL2_TTF_INCLUDE_DIR OR NOT SDL2_TTF_LIBRARY)
  message(FATAL_ERROR "SDL2_ttf is required but was not found")
endif()

message(STATUS "SDL2_ttf include: ${SDL2_TTF_INCLUDE_DIR}")
message(STATUS "SDL2_ttf library: ${SDL2_TTF_LIBRARY}")

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
  SDL2::SDL2
  SDL2::SDL2main
  ${SDL2_TTF_LIBRARY}
)
target_include_directories(vida_gui PRIVATE
  src
  ${SDL2_TTF_INCLUDE_DIR}/..
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(vida_gui PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers)
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
