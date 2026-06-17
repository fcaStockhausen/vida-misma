#pragma once

#include "simulation.h"
#include "sprite_atlas.h"
#include "font_cache.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>

class GraphicalView {
public:
    GraphicalView(Simulation& sim, std::atomic<bool>& paused);
    ~GraphicalView();

    void run();
    void set_speed_callback(std::function<void(int)> cb) { speed_cb_ = std::move(cb); }

private:
    Simulation& sim_;
    std::atomic<bool>& paused_;
    bool quit_ = false;
    bool running_ = true;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SpriteAtlas atlas_;
    FontCache fonts_;

    static constexpr int WIN_W = 1280;
    static constexpr int WIN_H = 720;
    static constexpr int TILE_W = 32;
    static constexpr int TILE_H = 16;

    // Camera
    float cam_x_ = 0;
    float cam_y_ = 0;
    float cam_target_x_ = 0;
    float cam_target_y_ = 0;
    float zoom_ = 1.0f;
    float zoom_target_ = 1.0f;

    // Selection
    int selected_idx_ = 0;
    bool follow_agent_ = false;

    // Speed
    int speed_idx_ = 5; // index into SPEED_PRESETS (default 300ms = slow)
    static constexpr int SPEED_PRESETS[] = {20, 50, 100, 150, 200, 300, 500, 800, 1200};
    static constexpr int SPEED_COUNT = 9;
    std::function<void(int)> speed_cb_;  // notifies main when speed changes

    // View toggles
    bool show_help_ = false;
    bool show_log_ = true;
    bool show_grid_coords_ = false;
    bool show_quit_confirm_ = false;

    // Drag
    bool drag_ = false;
    int drag_last_x_ = 0;
    int drag_last_y_ = 0;

    // Panel
    static constexpr int PANEL_W = 280;
    int panel_scroll_ = 0;

    // Held keys for smooth input
    std::unordered_set<SDL_Keycode> keys_held_;

    // Panel tab
    enum class PanelTab : int { Needs = 0, Personality, Social, Utility, COUNT };
    PanelTab panel_tab_ = PanelTab::Needs;

    // Hover
    int hover_gx_ = -1;
    int hover_gy_ = -1;

    // Movement interpolation: stores previous tick's positions for smooth
    // agent rendering between ticks. Keyed by agent entity ID.
    struct PrevPos { int x, y; };
    std::unordered_map<int, PrevPos> prev_positions_;
    std::unordered_map<int, PrevPos> render_positions_;
    int last_render_tick_ = -1;

    // --- Methods ---
    void handle_events();
    void handle_held_keys();
    void update_camera_smooth();
    void render();
    void render_tile(int gx, int gy, const std::vector<entt::entity>& agents);
    void render_header_bar();
    void render_side_panel();
    void render_help_overlay();
    void render_quit_confirm();
    void render_tooltip();
    void render_bar(int x, int y, int w, int h, float pct, SDL_Color fg, SDL_Color bg);
    void render_rect(int x, int y, int w, int h, SDL_Color color);
    void render_rect_outline(int x, int y, int w, int h, SDL_Color color);
    void render_separator(int x, int y, int w);

    void get_output_size(int& w, int& h) const;

    void iso_to_screen(int gx, int gy, float& sx, float& sy) const;
    bool screen_to_grid(int sx, int sy, int& gx, int& gy) const;
    void center_camera();
    void center_on_agent();
    void next_agent(int dir = 1);
    void prev_agent();
    int speed_ms() const;
    void cycle_speed(int dir);

    static std::string ff(float v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%.2f", v);
        return b;
    }

    // --- Color palette ---
    static constexpr SDL_Color COL_BG       = {20, 20, 30, 255};
    static constexpr SDL_Color COL_WALL     = {60, 60, 80, 255};
    static constexpr SDL_Color COL_FLOOR    = {100, 100, 110, 255};
    static constexpr SDL_Color COL_OPEN     = {40, 80, 50, 255};
    static constexpr SDL_Color COL_MACHINE  = {100, 200, 100, 255};
    static constexpr SDL_Color COL_MACH_UB  = {120, 120, 120, 255};
    static constexpr SDL_Color COL_STORAGE  = {200, 200, 100, 255};
    static constexpr SDL_Color COL_STOR_E   = {100, 100, 80, 255};
    static constexpr SDL_Color COL_ENTRANCE = {100, 200, 100, 255};
    static constexpr SDL_Color COL_EXIT     = {100, 200, 100, 255};
    static constexpr SDL_Color COL_SCRAP    = {180, 140, 80, 255};
    static constexpr SDL_Color COL_SCRAP_E  = {100, 80, 50, 255};
    static constexpr SDL_Color COL_CONV     = {60, 150, 200, 255};
    static constexpr SDL_Color COL_CONV_UB  = {60, 60, 60, 255};
    static constexpr SDL_Color COL_EZ       = {100, 200, 200, 255};
    static constexpr SDL_Color COL_EZ_UB    = {80, 100, 100, 255};
    static constexpr SDL_Color COL_AGENT    = {100, 200, 255, 255};
    static constexpr SDL_Color COL_SELECTED = {255, 255, 100, 255};
    static constexpr SDL_Color COL_TEXT     = {200, 200, 200, 255};
    static constexpr SDL_Color COL_DIM      = {120, 120, 130, 255};
    static constexpr SDL_Color COL_WHITE    = {255, 255, 255, 255};
    static constexpr SDL_Color COL_RED      = {255, 80, 80, 255};
    static constexpr SDL_Color COL_GREEN    = {100, 255, 100, 255};
    static constexpr SDL_Color COL_YELLOW   = {255, 220, 100, 255};
    static constexpr SDL_Color COL_CYAN     = {100, 220, 255, 255};
    static constexpr SDL_Color COL_MAGENTA  = {220, 100, 220, 255};
    static constexpr SDL_Color COL_PANEL_BG = {25, 25, 35, 255};
    static constexpr SDL_Color COL_BAR_BG   = {50, 50, 60, 255};
    static constexpr SDL_Color COL_HEADER   = {30, 30, 45, 255};
    static constexpr SDL_Color COL_HIGHLIGHT= {255, 200, 60, 255};
    static constexpr SDL_Color COL_TAB_ACTIVE = {45, 45, 65, 255};
    static constexpr SDL_Color COL_TAB_INACTIVE = {25, 25, 40, 255};
    static constexpr SDL_Color COL_TOOLTIP_BG = {15, 15, 25, 230};
};
