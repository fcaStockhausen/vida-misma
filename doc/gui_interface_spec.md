# La Vida Misma — Current GUI and Director Interface

> **Status (2026-07-22):** Implemented interface reference. This replaces the
> 2025 keyboard/font improvement proposal. Player view, explicit debug view and
> indirect Director controls are all present in the current SDL2 executable.

## 1. Launch Contract

```text
vida_gui [--seed N] [--record FILE] [--debug]
```

- `--seed N` overrides `simulation.seed` after startup configuration is loaded.
- `--record FILE` writes accepted Director interventions as TOML when the GUI
  exits. The schema-2 file includes seed, startup mode, an FNV-1a fingerprint of
  the loaded configuration source, tick and global sequence.
- `--debug` starts with internal agent inspection visible. The same view can be
  toggled explicitly with `F12`.
- Without an override, configuration comes from `config/default.toml`, loaded
  once at process startup. The GUI does not hot-reload configuration.

Run from the repository root or the build directory so lookup can find
`config/default.toml` or `../config/default.toml`.

## 2. Runtime Architecture

| Component | Current implementation |
|---|---|
| Window | SDL2, resizable and HiDPI-aware, logical size `1280x720` |
| Rendering | Accelerated SDL2 renderer with VSync and a procedural isometric sprite atlas |
| Text | SDL2_ttf cache at 13/16/20 px; `VIDA_FONT_PATH`, Windows fonts, or known Unix/macOS monospace paths |
| Layout | Isometric map, dynamic header, 280 px side panel, hover tooltip and modal overlays |
| Threading | Simulation, input and rendering run on one thread |
| Simulation cadence | Nine presets from 20 ms to 1200 ms per tick; render loop targets roughly 60 Hz |

The GUI owns no behavioral decision path. It invokes the same `Simulation` tick
pipeline as batch and sends institutional changes through typed Director commands.

## 3. Information Boundaries

### Player View

Player view is the default. It exposes observable institutional consequences:

- current demand and quota fulfillment;
- stored food and output, cumulative shipping and infrastructure wear;
- population and map density;
- anonymous occupancy zones and priority conveyor count;
- accepted intervention count and factual Chronicle events;
- map structures, occupants, flow state and tile hover details.

It does not expose exact needs, personality, relationships or action utility.
Selecting or following an agent changes the camera only; it is not an order.

### Debug View

`F12` enters or leaves explicit debug view. This view adds the selected resident's
exact needs, stress/trauma, inventory, personality, opinions, social state, utility
scores and individual/colony journal. Keys `1-5` choose the Needs, Personality,
Social, Utility and Journal tabs only while debug view is active.

Debug information is diagnostic and is not treated as knowledge available to the
Director or fed back into simulation policy.

## 4. Director Controls

Press `E` to enter Director edit mode. Entering pauses the simulation; leaving
restores its previous run/pause state. `Escape` cancels edit mode before opening
the quit confirmation.

| Key | Director operation |
|---|---|
| `1` | Set quota |
| `2` | Set anonymous occupancy capacity (`0`, `1`, `2`, or `4`) |
| `3` | Place Wall, Storage, FoodMachine, MaterialsMachine, OutputMachine or Conveyor |
| `4` | Remove an eligible institutional structure |
| `5` | Set normal/high maintenance priority on a conveyor |
| `[` / `]` | Cycle the selected tool option |
| `-` / `=` | Decrease/increase pending quota by `0.01` per tick |
| `Enter` | Apply the pending quota |
| `R` | Rotate the pending conveyor direction |
| Left click | Apply zoning, placement, removal or maintenance to the hovered tile |

Commands are validated before application. Rejected commands report an error and
are not recorded. Accepted commands may address quota and physical coordinates,
capacity, structure type, conveyor direction or maintenance priority. They cannot
accept an agent identity, action, behavioral target, personality, relationship or
utility state.

Director event ticks are defined before `Simulation::advance()`. A recorded event
at tick `t` is replayed immediately before advancing that tick.

## 5. General Controls

### Simulation And Camera

| Key/input | Operation |
|---|---|
| `Space` | Play/pause |
| `N` | Advance one tick while paused |
| `<` / `>` | Slower/faster preset |
| `WASD` or arrows | Smooth camera pan while held |
| Right/middle drag | Pan camera |
| Mouse wheel or `O` / `P` | Zoom out/in |
| `C` | Center selected resident |
| `Shift+C` or `Shift+R` | Center map |
| `F` | Toggle follow for selected resident |

### View And Inspection

| Key/input | Operation |
|---|---|
| `E` | Toggle Director edit mode |
| `F12` | Toggle explicit debug view |
| `T` | Toggle side panel |
| `G` | Toggle grid coordinates |
| `H` | Toggle help overlay |
| `Tab` / `Shift+Tab` | Select next/previous living resident by stable ID |
| `[` / `]` | Select first/last living resident outside Director edit mode |
| Left click | Select a resident on a tile outside Director edit mode |
| `L` | Toggle individual/colony journal in debug view |
| `PageUp` / `PageDown` | Scroll the debug panel or journal |
| `Escape` | Close Director/help/confirmation first, otherwise request quit |
| `Y` or `Enter` | Confirm quit |

Hover tooltips show tile coordinates and type plus relevant physical state such as
occupants, zone capacity, Storage contents, machine buffers, conveyor condition,
contents and maintenance priority.

## 6. Recording And Batch Replay

Example recorded session:

```bash
./build/vida_gui --seed 42 --record interventions.toml
```

Replay the accepted interventions against the same seed:

```bash
./build/vida_batch replay 3000 42 interventions.toml
```

Replay validates schema, seed, startup mode, configuration-source fingerprint,
event ordering, tick range and command transitions. The deterministic test fixture
compares replay with the original session across metrics, grid, population, quota,
Chronicle and per-agent ledger. The CLI emits a separate schema-1 replay JSON with
a state fingerprint for automation; schema-3 JSON remains the metrics contract.

## 7. Build And Platform Notes

The GUI requires SDL2 and SDL2_ttf. CMake resolves package targets, including
vcpkg, before using its Unix/Homebrew fallback. A headless build with
`VIDA_BUILD_GUI=OFF` does not require SDL.

```bash
cmake -S . -B build-gui -DVIDA_BUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-gui --parallel --target vida_gui
```

On Windows, pass the vcpkg toolchain and `x64-windows` triplet as documented in
README. The MSVC build copies required DLLs next to the executable. Text rendering
requires a discoverable monospace font; set `VIDA_FONT_PATH` when platform lookup
does not find one.

## 8. Deliberate Constraints

- There is no GUI command for assigning work, movement or a target to a resident.
- Zoning expresses anonymous physical capacity, not profession, faction or culture.
- Maintenance priority weights feasible resident utility; it does not select a
  resident or force repair.
- Player view intentionally withholds exact internal state. `F12` is an explicit
  diagnostic boundary, not a normal gameplay overlay.
- Recording is written on orderly GUI exit; it is not a continuous autosave or a
  complete world-state save.
