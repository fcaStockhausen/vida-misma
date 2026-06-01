#pragma once

#include "simulation.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <unordered_set>

class GraphicalView {
public:
    GraphicalView(Simulation& sim, std::atomic<bool>& paused);
    ~GraphicalView();

    void run();

private:
    Simulation& sim_;
    std::atomic<bool>& paused_;
    bool quit_ = false;
    bool running_ = true;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    static constexpr int WIN_W = 1280;
    static constexpr int WIN_H = 720;
    static constexpr int TILE_W = 32;
    static constexpr int TILE_H = 16;

    float cam_x_ = 0;
    float cam_y_ = 0;
    float zoom_ = 1.0f;
    int selected_idx_ = 0;
    bool show_help_ = false;
    bool drag_ = false;
    int drag_last_x_ = 0;
    int drag_last_y_ = 0;

    bool follow_agent_ = false;
    int speed_ms_ = 100;
    bool show_log_ = true;
    bool show_grid_coords_ = false;
    int log_scroll_ = 0;

    std::unordered_set<SDL_Keycode> keys_held_;
    bool chord_active_ = false;
    SDL_Keycode chord_key_ = SDLK_UNKNOWN;

    void handle_events();
    void handle_held_keys();
    void render();
    void render_iso_tile(int gx, int gy, SDL_Color color, bool filled);
    void render_tile(int gx, int gy, const std::vector<entt::entity>& agents);
    void render_agent_marker(int gx, int gy, bool selected, int count);
    void render_header_bar();
    void render_side_panel();
    void render_log_panel();
    void render_help_overlay();
    void render_bar(int x, int y, int w, int h, float pct, SDL_Color fg, SDL_Color bg);
    void render_text_solid(int x, int y, const char* text, SDL_Color color);
    void render_text_solid(int x, int y, const std::string& text, SDL_Color color);
    void render_rect(int x, int y, int w, int h, SDL_Color color);
    void render_rect_outline(int x, int y, int w, int h, SDL_Color color);

    void iso_to_screen(int gx, int gy, float& sx, float& sy) const;
    bool screen_to_grid(int sx, int sy, int& gx, int& gy) const;
    void center_camera();
    void center_on_agent();
    void next_agent(int dir = 1);
    void prev_agent();

    static std::string ff(float v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%.2f", v);
        return b;
    }

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
    static constexpr SDL_Color COL_CHORD    = {255, 160, 60, 255};
};
