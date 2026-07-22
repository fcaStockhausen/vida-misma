# Repository Guide

## Build And Run

- GUI configuration resolves SDL2 and SDL2_ttf through CMake package targets (including vcpkg) with a Unix/Homebrew library fallback. On Windows pass `-DCMAKE_TOOLCHAIN_FILE="$USERPROFILE/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows`; headless builds skip SDL entirely. EnTT and tomlplusplus are fetched from their pinned Git tags on first configure.
- Configure headless development with `cmake -S . -B build -DVIDA_BUILD_GUI=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release`, then build portably with `cmake --build build --parallel --target vida_batch vida_tests` and run `ctest --test-dir build --output-on-failure`. Configure with `VIDA_BUILD_GUI=ON` and build `vida_gui` explicitly when GUI code changes.
- Visual Studio and other multi-config generators require `cmake --build build --config Release --parallel --target vida_batch vida_tests` and `ctest --test-dir build -C Release --output-on-failure`; executables land under `build/Release/`. When GUI code changes, also build `vida_gui` and smoke-launch it from the repository root.
- Run binaries from the repository root (`./build/vida_batch ...`) or from `build/` (`./vida_batch ...`). Config lookup checks only `config/default.toml` and `../config/default.toml`; another working directory fails to load config.
- Useful focused checks are `./build/vida_batch run 200 42` (smoke), `./build/vida_batch metrics 3000 42 normal` (single deterministic JSON record), `./build/vida_batch metrics 3000 42 normal 1 0 -1 100 0` (Exit blocked, sampled, restructure disabled), `./build/vida_batch production 1000 42` (production chain without cultural drives), `./build/vida_batch culture 2000 42` (calm-mode trait/action correlations), `./build/vida_batch analysis 3000 42` (structured behavior), and `./build/vida_batch map 42` (generated layout).
- CTest covers the metrics contract, same-build simulation and Director replay, the metrics/replay CLIs, and static policy/Director boundaries. There is still no CI workflow, linter, or formatter configuration.

## Behavioral Verification

- Outcomes vary materially by generated layout; never accept or reject a simulation change from one seed. The established regression set is `0 1 2 3 7`, using `vida_batch analysis 3000 <seed>` against a fresh pre-change baseline; an alive-count drop over 30% in any seed is a regression signal.
- For utility/stress redesigns, change one mechanism at a time and preserve an A/B config variant when reproducibility of an existing behavior is required. `config/default.toml` intentionally selects `urgency.curve_variant = 3` and `urgency.stress_model_variant = 1`; variant `0` is the legacy baseline.
- The project favors correctly shaped utility curves over new hard gates or overrides. The compliance/meaning kink is known to be load-bearing; `doc/plans/2026-07-21-emergence-redesign.md` records the failed removal experiment.

## Runtime Architecture

- Both executables load a `Config` and construct `Simulation`; the tick pipeline is centralized in `Simulation::advance()` in `src/simulation.cpp`.
- Agent behavior is intentionally split: scores and Boltzmann selection in `sim_utility.cpp`, target choice/claims in `sim_targets.cpp`, pathing in `sim_movement.cpp`, effects and resource transfers in `sim_execute.cpp`, and belt transport in `sim_conveyor.cpp`. Trace all relevant stages before changing an action.
- `ProductionChain::assess()` runs after actions and shipping for aggregate diagnostics and planning. Canonical utility and targeting do not consume `colony_prod_` and mostly use local observations; conveyor connectivity planning scans global topology, and A* reads the full map after target selection.
- Shared simulation `.cpp` files must be added to `VIDA_SIM_SOURCES` in `CMakeLists.txt`; that list feeds both `vida_batch` and `vida_gui`.
- Human interventions cross only the typed boundary in `director.h`/`sim_director.cpp`; no Director command may accept agent identity, action, behavioral target, personality, relationship, or utility state. Event tick semantics are before `Simulation::advance()`.

## Configuration

- `config/default.toml` is the effective runtime tuning source and is loaded once at process startup; there is no hot reload, and changes require restarting the process. The TOML currently overrides header fallbacks, including population `48` versus `Config`'s fallback `24`. `external.supply_variant = 1` and `external.policy_variant = 1` are canonical; variant `0` retains legacy factory-health or strategic/Watcher behavior for A/B comparisons.
- Adding a knob requires synchronized changes to `Config` in `src/config.h`, `load_config()` in `src/config.cpp`, and `config/default.toml`. The parser dereferences `get_as(...)` for keys in an existing section, so omitting a newly parsed key from the default TOML can crash rather than use the header fallback.
- Batch commands accept the seed positionally; they do not accept a config path. Except for that seed override and command-forced director modes, behavior comes from `config/default.toml`.
- The executable chain is three-machine: FoodMachine processes raw food, MaterialsMachine converts raw material to construction material, and OutputMachine converts construction material to output. Only output shipped from Exit-adjacent Storage satisfies canonical quota/support; use current code and `doc/bases_matematicas/secciones/12_factory.md` when older historical prose conflicts.

## Academic Document

- Edit the ordered sources under `doc/bases_matematicas/secciones/`, not the generated HTML/PDF directly. `bash doc/bases_matematicas/build.sh` regenerates both tracked artifacts.
- The document script resolves `pandoc` from PATH or `PANDOC=/path/to/pandoc` and requires `pandoc-crossref` plus `xelatex` on PATH. It regenerates both tracked artifacts.
