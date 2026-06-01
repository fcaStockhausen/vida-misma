# La Vida Misma — GUI Interface Specification

> Status: Analysis of current state + improvement plan.  
> Last updated: 2025-06-01

---

## 1. Current State Analysis

### 1.1 Architecture

| Component | Technology | Notes |
|---|---|---|
| Window | SDL2 | 1280x720 default, resizable |
| Rendering | SDL2 software renderer | `SDL_RENDERER_ACCELERATED + VSYNC` |
| Font system | Custom 5x7 bitmap glyphs | 95 chars (ASCII printable), 1px per pixel, no scaling |
| Layout | Hardcoded pixel positions | No layout engine, no DPI scaling |
| Sprite system | Procedural atlas (64x40 per sprite) | 31 sprites, generated at runtime |

### 1.2 Rendering Layers (draw order)

```
Layer 0: Isometric tile grid (depth-sorted, back-to-front by Y)
Layer 1: Agent sprites (per-tile, with stress-state variants)
Layer 2: Agent count badges (tiny text on multi-agent tiles)
Layer 3: Header bar (16px, top of window)
Layer 4: Side panel (220px, right edge, toggleable)
Layer 5: Help overlay (centered modal, 300x360)
```

### 1.3 Current Key Binding Map

```
┌─────────────────────────────────────────────────────────┐
│  CAMERA NAVIGATION                                      │
│  WASD (hold)    Smooth pan (6px/frame)                  │
│  Arrow keys     Jump pan (40px single-press)            │
│  Z / X          Zoom out / in (×1.15)                   │
│  Mouse wheel    Zoom (×1.1)                             │
│  RMB drag       Pan camera                              │
│  C              Center on selected agent                │
│  Shift+C        Center on map                           │
│  F              Toggle follow-agent mode                │
│                                                         │
│  SIMULATION CONTROL                                     │
│  Space          Toggle pause (if running) / step (paused)│
│  Shift+Space    Force pause                             │
│  Enter          Toggle run/pause                        │
│  1-7            Speed presets (20/50/100/150/200/300/500ms)│
│  +/-            Adjust speed ±25ms                      │
│                                                         │
│  AGENT SELECTION                                        │
│  Tab            Next agent                              │
│  I              Previous agent                          │
│  LMB click      Select agent on clicked tile            │
│                                                         │
│  PANELS & OVERLAYS                                      │
│  L              Toggle side panel                       │
│  E              Toggle log mode (all vs. agent)         │
│  G              Toggle grid coordinate overlay          │
│  H              Toggle help overlay                     │
│                                                         │
│  CHORD PREFIX 'J'                                       │
│  J 1-7          Speed presets (duplicate of direct 1-7) │
│  J R / P / N    Run / Pause / step-N                   │
│  J F            Toggle follow (duplicate of F)          │
│  J L            Toggle panel (duplicate of L)           │
│  J G            Toggle grid coords (duplicate of G)     │
│                                                         │
│  CHORD PREFIX 'K'                                       │
│  K A / M / F    (UNIMPLEMENTED — empty switch cases)    │
│                                                         │
│  SYSTEM                                                 │
│  Escape         Quit immediately (no confirmation)      │
└─────────────────────────────────────────────────────────┘
```

### 1.4 Identified Problems

#### Graphical Issues

| # | Issue | Severity | Description |
|---|---|---|---|
| G1 | No font library | HIGH | 5x7 bitmap glyphs are illegible at low zoom, cannot scale, no international chars. All text rendering goes through `render_text_solid()` which draws individual pixels. |
| G2 | No DPI awareness | MEDIUM | Hardcoded pixel sizes (16px header, 220px panel, 5x7 glyphs). On Retina/HiDPI displays everything is tiny. |
| G3 | Side panel too narrow | HIGH | 220px must contain: agent ID, action, position, 7 need bars, stress state, inventory, 6 personality bars, 3 social bars, 4 opinion bars, 8 utility bars, and a chronicle log. All with 5px-tall bars and 8px line spacing. |
| G4 | No visual hierarchy | MEDIUM | Same font size/style for section titles (NEEDS, PERSONALITY), labels (Hunger, Rest), and values (0.73). No bold, no size variation. |
| G5 | Header bar cramped | LOW | 16px height with tick count, alive count, built machines, food, factory health, quota, shipped, broken, status, speed, chord indicator, follow indicator — all on one line. |
| G6 | No animations | LOW | Panels pop in/out instantly. No smooth camera transitions (follow mode uses lerp but manual pan is jumpy). |
| G7 | Agent count badge | LOW | Tiny number above agent sprite, easy to miss when multiple agents share a tile. |
| G8 | No tooltip/hover info | MEDIUM | Hovering over tiles/agents shows nothing — must click to select and read side panel. |

#### Keyboard Navigation Issues

| # | Issue | Severity | Description |
|---|---|---|---|
| K1 | Dual camera systems | MEDIUM | WASD (hold, smooth 6px) and Arrows (single-press, 40px jump) both move the camera. Two overlapping systems for the same action increases cognitive load. |
| K2 | Chord duplication | LOW | J prefix duplicates many direct keys (J+1 = 1, J+F = F, J+L = L, J+G = G). The chord adds complexity without adding capability. |
| K3 | K chord is empty | MEDIUM | K prefix switches on a/m/f but all cases are empty. Dead UI path. |
| K4 | Tab/I asymmetry | LOW | Tab=next, I=prev. Non-standard. Most keyboard-driven interfaces use consistent pairs (n/p, j/k, Tab/Shift+Tab). |
| K5 | No panel scrolling | HIGH | `log_scroll_` member exists but no key binds to scroll the side panel. When the chronicle log has many entries, there's no way to read older ones. |
| K6 | Escape = instant quit | MEDIUM | No confirmation, no "go back" / close overlay first. One wrong press and the session is gone. |
| K7 | Space overload | MEDIUM | Space means "toggle run↔pause" OR "step once if paused" depending on running_ state. Confusing. Enter also toggles run/pause. Two keys do the same thing with different edge-case behavior. |
| K8 | No vi-keys for camera | LOW | HJKL would be natural for camera (H=left, J=down, K=up, L=right) but J/K are chord prefixes. Wasted ergonomic opportunity. |
| K9 | No tile inspection | MEDIUM | Can't focus on a specific tile without clicking. No keyboard-driven tile cursor. |
| K10 | No way to filter/sort agents | LOW | Tab cycles agents in entity-order. No way to filter by state (stressed, idle, building), or sort by need level. |

---

## 2. Proposed Key Binding Redesign

### 2.1 Design Principles

1. **One hand for camera, one hand for actions** — left hand on keyboard home row, right hand optional for mouse.
2. **Modal simplicity** — no chords for common operations. Chords reserved for infrequent power-user commands.
3. **Consistent pairs** — next/prev uses the same modifier pattern (e.g., Tab/Shift+Tab).
4. **Escape always goes back** — close overlay, cancel chord, THEN quit with confirmation.
5. **Mnemonics** — key should hint at function (F=follow, H=help, P=pause).

### 2.2 Proposed Layout

```
LEFT HAND (camera + view)          RIGHT HAND (simulation + agents)
─────────────────────────          ─────────────────────────────────
                                   
  Q   W   E   R   T   Y             U   I   O   P
  |   |   |   |   |   |             |   |   |   └─ Pause/Play toggle
  |   |   |   |   |   └─ (free)     |   |   └───── Zoom in
  |   |   |   |   └───── Center map │   └───────── Zoom out  [MOVED from Z/X]
  |   |   |   └───────── Follow toggle
  |   |   └───────────── Toggle panel
  |   └───────────────── (reserved)
  └───────────────────── (reserved)

  A   S   D   F   G                 H   J   K   L
  |   |   |   |   |                 |   |   |   └─ (free)
  |   |   |   |   └─ Grid coords    |   |   └───── (free)
  |   |   |   └───── (free)         |   └───────── (free)  
  |   |   └───────── (free)         └───────────── Help
  |   └───────────── (free)
  └───────────────── (free)

      Movement: WASD or Arrow keys (unified behavior: smooth when held)
```

### 2.3 Proposed Binding Table

```
╔══════════════════════════════════════════════════════════════════╗
║  CAMERA / VIEW                                                  ║
║  W/↑ S/↓ A/← D/→   Pan camera (smooth, same speed held)       ║
║  Mouse wheel        Zoom in/out                                 ║
║  O / P              Zoom out / Zoom in                          ║
║  R                  Center on map                                ║
║  F                  Toggle follow selected agent                 ║
║  T                  Toggle side panel                            ║
║  G                  Toggle grid coordinates                      ║
║  H                  Toggle help overlay                          ║
║                                                                  ║
║  SIMULATION                                                      ║
║  Space              Play / Pause toggle                          ║
║  > (Shift+.)        Speed up (next preset)                       ║
║  < (Shift+,)        Speed down (previous preset)                 ║
║  N                  Step forward (only while paused)             ║
║                                                                  ║
║  AGENT SELECTION                                                 ║
║  Tab               Next agent                                    ║
║  Shift+Tab         Previous agent                                ║
║  LMB click          Select agent on tile                         ║
║  [ / ]              First / Last agent (wrap)                    ║
║                                                                  ║
║  PANEL INTERACTION                                               ║
║  PgUp / PgDn       Scroll side panel log (up / down)            ║
║  E                  Toggle log mode (all / agent)                ║
║                                                                  ║
║  SYSTEM                                                          ║
║  Escape            Close overlay → cancel chord → quit confirm  ║
╚══════════════════════════════════════════════════════════════════╝
```

### 2.4 Chord System (simplified)

Remove J/K chords entirely. They duplicate direct keys and add cognitive overhead without benefit for a single-user simulation viewer. If power-user shortcuts are needed in the future, use a single modifier prefix (e.g., hold Shift for extended commands).

### 2.5 Migration Notes

| Old binding | New binding | Rationale |
|---|---|---|
| Arrow keys (40px jump) | Unified with WASD (smooth) | Remove dual system |
| Z/X zoom | O/P zoom | Z/X freed for future undo/redo or other |
| I = prev agent | Shift+Tab = prev agent | Consistent pair with Tab |
| Enter = toggle run | Removed (Space only) | Space is standard play/pause |
| J chord | Removed | All commands available directly |
| K chord | Removed | Was unimplemented |
| Escape = quit | Escape = back/quit with confirm | Prevent accidental exit |
| 1-7 speed presets | </> cycle through presets | Fewer keys to remember |

---

## 3. Graphical Improvements Plan

### 3.1 Font System (Priority: HIGH)

**Current**: 5x7 pixel glyphs drawn 1px at a time.  
**Target**: SDL2_ttf with a bundled monospace font (e.g., IBM Plex Mono, 8px and 12px sizes).

Implementation:
```
- Add SDL2_ttf dependency to CMakeLists.txt
- Bundle a .ttf font (or use system monospace as fallback)
- Create FontCache class: render to texture, cache glyphs
- Replace all render_text_solid() calls with FontCache::draw()
- Support 2 sizes: small (8-10px for values/labels), title (12-14px for section headers)
```

### 3.2 Side Panel Redesign (Priority: HIGH)

**Current**: 220px wide, everything crammed vertically.  
**Target**: 280px, tabbed sections.

```
┌─────────────────────────────┐
│ Agent[7] · GATHER           │  ← Title + current action
│ pos: 14,22 · FOLLOW         │  ← Position + status badges
├─────────────────────────────┤
│ [Needs] [Pers] [Soc] [Util] │  ← Tab bar (keyboard: 1-4)
├─────────────────────────────┤
│                             │
│  (content area for active   │
│   tab — needs/personality/  │
│   social/utility)           │
│                             │
├─────────────────────────────┤
│ CHRONICLE · E:toggle        │
│ [event log lines]           │  ← Always visible at bottom
│ [event log lines]           │
├─────────────────────────────┤
│ tick:142 alive:8 built:3    │  ← Footer (always visible)
└─────────────────────────────┘
```

Panel tabs navigable with number keys 1-4 when panel is focused, or directly mapped.

### 3.3 Header Bar Improvement (Priority: MEDIUM)

- Increase height to 24px
- Group related stats with separators: `[Simulation: tick/alive] [Factory: built/food/health/quota] [View: speed/follow]`
- Use color-coded values (already partially done)
- Add FPS counter (debug, toggleable)

### 3.4 Tile Hover Tooltip (Priority: MEDIUM)

When mouse hovers over a tile (no click needed), show a small tooltip:
- Tile type and state (e.g., "Machine (built, 87% condition)")
- Agent count on tile
- Agent names/IDs if present

Implementation: track mouse position each frame, convert to grid coords, render a small floating rect near cursor.

### 3.5 Visual Hierarchy (Priority: MEDIUM)

Establish a type scale:
```
Title:     14px bold, COL_HIGHLIGHT
Section:   12px bold, COL_WHITE  
Label:     10px regular, COL_DIM
Value:     10px bold, COL_TEXT
Badges:     8px, colored backgrounds
```

### 3.6 Smooth Transitions (Priority: LOW)

- Camera pan: already smooth via held keys, but jump commands (center_on_agent) should animate over ~10 frames instead of teleporting.
- Panel toggle: slide in/out over 8-10 frames.
- Zoom: smooth interpolation instead of discrete ×1.15 jumps.

### 3.7 Minimap (Priority: LOW)

Small minimap in a corner showing:
- Factory overview (tile types as colored dots)
- Agent positions as bright dots
- Current viewport as a rectangle

Navigable: click on minimap to jump camera.

---

## 4. Implementation Priority

| Phase | Items | Effort | Impact |
|---|---|---|---|
| **P0 — Keyboard cleanup** | Remove J/K chords, unify WASD/Arrow, fix Escape, add panel scroll (K1-K6, K8) | 2-3h | HIGH — fixes daily usability |
| **P1 — Font system** | SDL2_ttf integration, FontCache, replace all render_text_solid calls (G1, G4) | 4-6h | HIGH — readability foundation |
| **P2 — Panel redesign** | Wider panel, tabbed sections, always-visible log (G3, G4) | 6-8h | HIGH — information density |
| **P3 — Header + tooltip** | Taller header, tile hover tooltip (G5, G8) | 3-4h | MEDIUM — quality of life |
| **P4 — Polish** | Animations, minimap, DPI awareness (G2, G6, G7) | 8-12h | LOW — visual refinement |

---

## 5. File Map (affected files)

```
src/graphical_view.h     — Key binding enum, panel state, tab tracking
src/graphical_view.cpp   — handle_events(), render_*() methods
src/sprite_atlas.h       — Unchanged (sprite system is separate from UI)
CMakeLists.txt           — Add SDL2_ttf dependency
```

New files potentially needed:
```
src/font_cache.h         — SDL2_ttf wrapper with glyph caching
src/font_cache.cpp       — Implementation
assets/                  — Bundled .ttf font file
```
