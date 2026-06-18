#include "graphical_view.h"
#include <algorithm>
#include <cmath>

// Out-of-line definitions for static constexpr members
constexpr int GraphicalView::SPEED_PRESETS[];
constexpr int GraphicalView::PANEL_W;

// ============================================================
// Construction & lifecycle
// ============================================================

GraphicalView::GraphicalView(Simulation& sim, std::atomic<bool>& paused)
    : sim_(sim), paused_(paused) {}

GraphicalView::~GraphicalView() {
    fonts_.destroy();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void GraphicalView::run() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return;
    }

    window_ = SDL_CreateWindow(
        "La Vida Misma — Isometric Factory",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    if (!window_) {
        std::fprintf(stderr, "SDL window failed: %s\n", SDL_GetError());
        return;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::fprintf(stderr, "SDL renderer failed: %s\n", SDL_GetError());
        return;
    }

    // Logical size: all code uses 1280×720, SDL2 scales to physical framebuffer
    SDL_RenderSetLogicalSize(renderer_, WIN_W, WIN_H);

    if (!fonts_.init(renderer_)) {
        std::fprintf(stderr, "Font init failed — continuing without text cache\n");
    }

    atlas_.init(renderer_);
    center_camera();
    cam_target_x_ = cam_x_;
    cam_target_y_ = cam_y_;

    while (!quit_) {
        handle_events();
        handle_held_keys();
        update_camera_smooth();
        if (follow_agent_) center_on_agent();
        render();
        SDL_Delay(16);
    }
}

// ============================================================
// Coordinate transforms
// ============================================================

void GraphicalView::iso_to_screen(int gx, int gy, float& sx, float& sy) const {
    float tw = TILE_W * zoom_;
    float th = TILE_H * zoom_;
    sx = (gx - gy) * tw * 0.5f + cam_x_;
    sy = (gx + gy) * th * 0.5f + cam_y_;
}

bool GraphicalView::screen_to_grid(int sx, int sy, int& gx, int& gy) const {
    float tw = TILE_W * zoom_;
    float th = TILE_H * zoom_;
    float rx = (float)(sx - cam_x_);
    float ry = (float)(sy - cam_y_);
    float a = rx / (tw * 0.5f);
    float b = ry / (th * 0.5f);
    gx = (int)std::round((a + b) * 0.5f);
    gy = (int)std::round((b - a) * 0.5f);
    return gx >= 0 && gx < sim_.grid().width() && gy >= 0 && gy < sim_.grid().height();
}

void GraphicalView::center_camera() {
    int gw = sim_.grid().width();
    int gh = sim_.grid().height();
    float cx = (gw - gh) * TILE_W * zoom_ * 0.5f;
    float cy = (gw + gh) * TILE_H * zoom_ * 0.5f;
    int ww, wh;
    if (renderer_) get_output_size(ww, wh);
    else { ww = WIN_W; wh = WIN_H; }
    cam_x_ = ww * 0.5f - cx;
    cam_y_ = wh * 0.3f - cy;
    cam_target_x_ = cam_x_;
    cam_target_y_ = cam_y_;
}

void GraphicalView::center_on_agent() {
    auto agents = sim_.alive_agents();
    if (agents.empty() || (size_t)selected_idx_ >= agents.size()) return;
    auto& pos = sim_.registry().get<PositionComponent>(agents[selected_idx_]);
    float sx, sy;
    iso_to_screen(pos.x, pos.y, sx, sy);
    int ww, wh;
    get_output_size(ww, wh);
    int pw = show_log_ ? PANEL_W : 0;
    cam_target_x_ = (ww - pw) * 0.5f - sx + cam_x_;
    cam_target_y_ = wh * 0.4f - sy + cam_y_;
}

void GraphicalView::next_agent(int dir) {
    auto agents = sim_.alive_agents();
    if (agents.empty()) return;
    selected_idx_ = ((int)selected_idx_ + dir + (int)agents.size()) % (int)agents.size();
}

void GraphicalView::prev_agent() { next_agent(-1); }

int GraphicalView::speed_ms() const {
    return SPEED_PRESETS[speed_idx_];
}

void GraphicalView::cycle_speed(int dir) {
    speed_idx_ = std::clamp(speed_idx_ + dir, 0, SPEED_COUNT - 1);
    if (speed_cb_) speed_cb_(speed_ms());
}

// ============================================================
// Smooth camera
// ============================================================

void GraphicalView::update_camera_smooth() {
    float lerp = 0.12f;
    cam_x_ += (cam_target_x_ - cam_x_) * lerp;
    cam_y_ += (cam_target_y_ - cam_y_) * lerp;
    zoom_ += (zoom_target_ - zoom_) * lerp;
}

// ============================================================
// Input
// ============================================================

void GraphicalView::handle_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                if (show_help_) { show_help_ = false; break; }
                if (show_quit_confirm_) { quit_ = true; break; }
                show_quit_confirm_ = true;
                break;

            case SDL_KEYDOWN: {
                SDL_Keycode key = e.key.keysym.sym;
                bool shift = e.key.keysym.mod & KMOD_SHIFT;
                keys_held_.insert(key);

                // Escape: back/quit
                if (key == SDLK_ESCAPE) {
                    if (show_quit_confirm_) { show_quit_confirm_ = false; break; }
                    if (show_help_) { show_help_ = false; break; }
                    show_quit_confirm_ = true;
                    break;
                }

                // Help overlay eats all keys
                if (show_help_) { show_help_ = false; break; }

                // Quit confirm overlay
                if (show_quit_confirm_) {
                    if (key == SDLK_y || key == SDLK_RETURN) { quit_ = true; }
                    else { show_quit_confirm_ = false; }
                    break;
                }

                switch (key) {
                    // Simulation
                    case SDLK_SPACE:
                        if (running_) { running_ = false; paused_ = true; }
                        else { running_ = true; paused_ = false; }
                        break;
                    case SDLK_n:
                        if (!running_) sim_.advance();
                        break;
                    case SDLK_COMMA:
                        if (shift) cycle_speed(-1);
                        break;
                    case SDLK_PERIOD:
                        if (shift) cycle_speed(1);
                        break;

                    // Camera
                    case SDLK_c:
                        if (shift) center_camera();
                        else center_on_agent();
                        break;
                    case SDLK_f:
                        follow_agent_ = !follow_agent_; break;
                    case SDLK_o:
                        zoom_target_ = std::max(0.3f, zoom_target_ / 1.15f); break;
                    case SDLK_p:
                        zoom_target_ = std::min(4.0f, zoom_target_ * 1.15f); break;
                    case SDLK_r:
                        if (shift) center_camera();
                        break;

                    // View
                    case SDLK_t:
                        show_log_ = !show_log_; break;
                    case SDLK_g:
                        show_grid_coords_ = !show_grid_coords_; break;
                    case SDLK_h:
                        show_help_ = !show_help_; break;

                    // Agents
                    case SDLK_TAB:
                        if (shift) prev_agent(); else next_agent(); break;
                    case SDLK_LEFTBRACKET:
                        selected_idx_ = 0; break;
                    case SDLK_RIGHTBRACKET: {
                        auto agents = sim_.alive_agents();
                        if (!agents.empty()) selected_idx_ = (int)agents.size() - 1;
                        break;
                    }

                    // Panel tabs
                    case SDLK_1: panel_tab_ = PanelTab::Needs; break;
                    case SDLK_2: panel_tab_ = PanelTab::Personality; break;
                    case SDLK_3: panel_tab_ = PanelTab::Social; break;
                    case SDLK_4: panel_tab_ = PanelTab::Utility; break;

                    // Panel scroll
                    case SDLK_PAGEUP:   panel_scroll_ = std::max(0, panel_scroll_ - 5); break;
                    case SDLK_PAGEDOWN:  panel_scroll_ += 5; break;

                    // Arrow keys — unified smooth pan
                    case SDLK_LEFT:  cam_target_x_ += 40; break;
                    case SDLK_RIGHT: cam_target_x_ -= 40; break;
                    case SDLK_UP:    cam_target_y_ += 25; break;
                    case SDLK_DOWN:  cam_target_y_ -= 25; break;
                }
                break;
            }

            case SDL_KEYUP:
                keys_held_.erase(e.key.keysym.sym);
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_RIGHT ||
                    e.button.button == SDL_BUTTON_MIDDLE) {
                    drag_ = true;
                    drag_last_x_ = e.button.x;
                    drag_last_y_ = e.button.y;
                }
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int gx, gy;
                    if (screen_to_grid(e.button.x, e.button.y, gx, gy)) {
                        auto agents = sim_.alive_agents();
                        for (size_t i = 0; i < agents.size(); i++) {
                            auto& pos = sim_.registry().get<PositionComponent>(agents[i]);
                            if (pos.x == gx && pos.y == gy) {
                                selected_idx_ = (int)i;
                                break;
                            }
                        }
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                drag_ = false;
                break;
            case SDL_MOUSEMOTION:
                if (drag_) {
                    cam_target_x_ += e.motion.x - drag_last_x_;
                    cam_target_y_ += e.motion.y - drag_last_y_;
                    drag_last_x_ = e.motion.x;
                    drag_last_y_ = e.motion.y;
                }
                // Track hover
                screen_to_grid(e.motion.x, e.motion.y, hover_gx_, hover_gy_);
                break;
            case SDL_MOUSEWHEEL:
                if (e.wheel.y > 0) zoom_target_ = std::min(4.0f, zoom_target_ * 1.1f);
                else if (e.wheel.y < 0) zoom_target_ = std::max(0.3f, zoom_target_ / 1.1f);
                break;
        }
    }
}

void GraphicalView::handle_held_keys() {
    float pan_speed = 6.0f;
    if (keys_held_.count(SDLK_w) || keys_held_.count(SDLK_UP))    cam_target_y_ += pan_speed;
    if (keys_held_.count(SDLK_s) || keys_held_.count(SDLK_DOWN))  cam_target_y_ -= pan_speed;
    if (keys_held_.count(SDLK_a) || keys_held_.count(SDLK_LEFT))  cam_target_x_ += pan_speed;
    if (keys_held_.count(SDLK_d) || keys_held_.count(SDLK_RIGHT)) cam_target_x_ -= pan_speed;
}

// ============================================================
// Rendering — main loop
// ============================================================

void GraphicalView::render() {
    SDL_SetRenderDrawColor(renderer_, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(renderer_);

    const Grid& grid = sim_.grid();
    auto agents = sim_.alive_agents();

    int ww, wh;
    get_output_size(ww, wh);
    int map_w = ww - (show_log_ ? PANEL_W : 0);

    for (int y = 0; y < grid.height(); y++) {
        for (int x = 0; x < grid.width(); x++) {
            float sx, sy;
            iso_to_screen(x, y, sx, sy);
            float tw = TILE_W * zoom_;
            float th = TILE_H * zoom_;
            if (sx + tw < 0 || sx - tw > map_w || sy + th < 0 || sy - th > wh)
                continue;
            render_tile(x, y, agents);
        }
    }

    render_header_bar();
    if (show_log_) render_side_panel();
    render_tooltip();

    if (show_help_) render_help_overlay();
    if (show_quit_confirm_) render_quit_confirm();

    SDL_RenderPresent(renderer_);
}

// ============================================================
// Tile rendering (sprite-based)
// ============================================================

void GraphicalView::render_tile(int gx, int gy, const std::vector<entt::entity>& agents) {
    const Grid& grid = sim_.grid();
    TileType t = grid.at(gx, gy);
    const auto& d = grid.data_at(gx, gy);

    int ac = 0;
    bool sel = false;
    for (size_t i = 0; i < agents.size(); i++) {
        auto& pos = sim_.registry().get<PositionComponent>(agents[i]);
        if (pos.x == gx && pos.y == gy) {
            ac++;
            if ((int)i == selected_idx_) sel = true;
        }
    }

    SpriteID sid = SpriteID::Fallback;
    switch (t) {
        case TileType::Floor:       sid = SpriteID::Floor; break;
        case TileType::Wall:        sid = SpriteID::Wall; break;
        case TileType::OpenSpace:   sid = SpriteID::OpenSpace; break;
        case TileType::Machine:
            if (d.built) sid = SpriteID::MachineBuilt;
            else {
                float pct = d.build_cost > 0 ? d.build_progress / d.build_cost : 0;
                if (pct > 0.5f) sid = SpriteID::MachineBuildHigh;
                else if (pct > 0.0f) sid = SpriteID::MachineBuildLow;
                else sid = SpriteID::MachineUnbuilt;
            }
            break;
        case TileType::Storage: {
            float tot = d.stored_food + d.stored_raw_food + d.stored_raw_material + d.stored_output;
            if (tot > 5.f) sid = SpriteID::StorageFull;
            else if (tot > 0.5f) sid = SpriteID::StoragePartial;
            else sid = SpriteID::StorageEmpty;
            break;
        }
        case TileType::Entrance:    sid = SpriteID::Entrance; break;
        case TileType::Exit:        sid = SpriteID::Exit; break;
        case TileType::ScrapPile:
            sid = (d.resource_amount > 2.f) ? SpriteID::ScrapRich : SpriteID::ScrapDepleted;
            break;
        case TileType::Conveyor:
            if (!d.built) sid = SpriteID::ConveyorUnbuilt;
            else {
                switch (d.conveyor_dir) {
                    case ConveyorDir::N: sid = SpriteID::ConveyorN; break;
                    case ConveyorDir::S: sid = SpriteID::ConveyorS; break;
                    case ConveyorDir::E: sid = SpriteID::ConveyorE; break;
                    case ConveyorDir::W: sid = SpriteID::ConveyorW; break;
                }
            }
            break;
        case TileType::EatingZone:
            sid = d.built ? SpriteID::EatingZoneBuilt : SpriteID::EatingZoneUnbuilt;
            break;
        case TileType::FoodSource:
            sid = (d.resource_amount > 1.5f) ? SpriteID::FoodSourceLush : SpriteID::FoodSourceSparse;
            break;
        case TileType::HiddenSpace: sid = SpriteID::HiddenSpace; break;
        default: break;
    }

    float sx, sy;
    iso_to_screen(gx, gy, sx, sy);
    atlas_.draw(sid, (int)sx, (int)sy, zoom_);

    if (ac > 0) {
        SpriteID agent_sid = SpriteID::AgentNormal;
        if (sel && (size_t)selected_idx_ < agents.size()) {
            auto& st = sim_.registry().get<StressComponent>(agents[selected_idx_]);
            switch (st.state) {
                case StressState::NORMAL:          agent_sid = SpriteID::AgentSelected; break;
                case StressState::DISSOCIATED:     agent_sid = SpriteID::AgentDissociated; break;
                case StressState::HOSTILE_EUPHORIA:agent_sid = SpriteID::AgentEuphoric; break;
                case StressState::BROKEN:          agent_sid = SpriteID::AgentBroken; break;
                case StressState::REDEEMED:        agent_sid = SpriteID::AgentRedeemed; break;
            }
        }
        atlas_.draw_agent(agent_sid, (int)sx, (int)sy, zoom_);

        if (ac > 1) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%d", std::min(ac, 9));
            fonts_.draw((int)(sx + 10 * zoom_), (int)(sy - 16 * zoom_), buf, COL_WHITE);
        }
    }

    if (show_grid_coords_) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d,%d", gx, gy);
        fonts_.draw((int)(sx - 12), (int)(sy + 4), buf, {100, 100, 120, 200}, FontSize::Small);
    }
}

// ============================================================
// Drawing helpers
// ============================================================

void GraphicalView::get_output_size(int& w, int& h) const {
    SDL_GetWindowSize(window_, &w, &h);
}

void GraphicalView::render_rect(int x, int y, int w, int h, SDL_Color color) {
    SDL_Rect r = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
    SDL_RenderFillRect(renderer_, &r);
}

void GraphicalView::render_rect_outline(int x, int y, int w, int h, SDL_Color color) {
    SDL_Rect r = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
    SDL_RenderDrawRect(renderer_, &r);
}

void GraphicalView::render_separator(int x, int y, int w) {
    SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
    SDL_RenderDrawLine(renderer_, x, y, x + w, y);
}

void GraphicalView::render_bar(int x, int y, int w, int h, float pct, SDL_Color fg, SDL_Color bg) {
    SDL_Rect bg_r = {x, y, w, h};
    SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, 255);
    SDL_RenderFillRect(renderer_, &bg_r);
    int filled_w = (int)(w * std::clamp(pct, 0.0f, 1.0f));
    SDL_Rect fg_r = {x, y, filled_w, h};
    SDL_SetRenderDrawColor(renderer_, fg.r, fg.g, fg.b, 255);
    SDL_RenderFillRect(renderer_, &fg_r);
    SDL_SetRenderDrawColor(renderer_, 80, 80, 90, 255);
    SDL_RenderDrawRect(renderer_, &bg_r);
}

// ============================================================
// Header bar
// ============================================================

void GraphicalView::render_header_bar() {
    int ww, wh;
    get_output_size(ww, wh);
    int panel_w = show_log_ ? PANEL_W : 0;
    int hh = fonts_.line_height(FontSize::Small) + 6;

    render_rect(0, 0, ww, hh, COL_HEADER);
    render_separator(0, hh, ww);

    int x = 6;
    int y = 3;
    char buf[64];

    x += fonts_.draw(x, y, "LA VIDA MISMA", {200, 220, 255, 255}, FontSize::Normal);
    x += 10;

    x += fonts_.drawf(x, y, COL_TEXT, FontSize::Small, "tick:%d", sim_.tick());
    x += 8;
    x += fonts_.drawf(x, y, COL_TEXT, FontSize::Small, "alive:%d/%d",
        sim_.alive_count(), sim_.config().initial_population);
    x += 8;
    x += fonts_.drawf(x, y, COL_GREEN, FontSize::Small, "built:%d", sim_.total_machines_built());
    x += 8;
    x += fonts_.drawf(x, y, COL_YELLOW, FontSize::Small, "food:%.0f", sim_.total_storage_food());
    x += 8;

    float fh = sim_.factory_health();
    SDL_Color fh_c = (fh > 0.66f) ? COL_GREEN : (fh > 0.33f) ? COL_YELLOW : COL_RED;
    x += fonts_.drawf(x, y, fh_c, FontSize::Small, "factory:%.0f%%", fh * 100);
    x += 8;

    float qf = sim_.last_quota_fill();
    SDL_Color qf_c = (qf >= 1.0f) ? COL_GREEN : (qf > 0.0f) ? COL_YELLOW : COL_RED;
    x += fonts_.drawf(x, y, qf_c, FontSize::Small, "quota:%.0f%%", qf * 100);
    x += 8;

    x += fonts_.drawf(x, y, COL_DIM, FontSize::Small, "shipped:%.0f", sim_.total_food_shipped());
    x += 8;
    x += fonts_.drawf(x, y, COL_DIM, FontSize::Small, "broken:%d", sim_.total_machines_broken());

    // Right side — status
    int rx = ww - panel_w - 6;
    std::snprintf(buf, sizeof(buf), "%dms", speed_ms());
    rx -= fonts_.text_width(buf, FontSize::Small);
    fonts_.draw(rx, y, buf, COL_DIM, FontSize::Small);

    if (running_) {
        rx -= fonts_.text_width(" RUN", FontSize::Small) + 4;
        fonts_.draw(rx, y, "RUN", COL_GREEN, FontSize::Small);
    } else {
        rx -= fonts_.text_width(" PAUSED", FontSize::Small) + 4;
        fonts_.draw(rx, y, "PAUSED", COL_YELLOW, FontSize::Small);
    }

    if (follow_agent_) {
        rx -= fonts_.text_width(" FOLLOW", FontSize::Small) + 4;
        fonts_.draw(rx, y, "FOLLOW", COL_CYAN, FontSize::Small);
    }
}

// ============================================================
// Side panel (tabbed)
// ============================================================

void GraphicalView::render_side_panel() {
    int ww, wh;
    get_output_size(ww, wh);
    int pw = PANEL_W;
    int px = ww - pw;
    int header_h = fonts_.line_height(FontSize::Small) + 6;

    render_rect(px, 0, pw, wh, COL_PANEL_BG);
    SDL_SetRenderDrawColor(renderer_, 50, 50, 65, 255);
    SDL_RenderDrawLine(renderer_, px, 0, px, wh);

    int y = header_h + 4;
    int margin = 6;
    int bw = pw - margin * 2;

    auto agents = sim_.alive_agents();
    if (agents.empty()) {
        fonts_.draw(px + margin, y, "No alive agents.", COL_DIM, FontSize::Normal);
        return;
    }

    if ((size_t)selected_idx_ >= agents.size()) selected_idx_ = 0;
    auto e = agents[selected_idx_];
    auto& ag  = sim_.registry().get<AgentComponent>(e);
    auto& nd  = sim_.registry().get<NeedsComponent>(e);
    auto& ps  = sim_.registry().get<PersonalityComponent>(e);
    auto& ac  = sim_.registry().get<ActionComponent>(e);
    auto& st  = sim_.registry().get<StressComponent>(e);
    auto& iv  = sim_.registry().get<InventoryComponent>(e);
    auto& po  = sim_.registry().get<PositionComponent>(e);
    auto& soc = sim_.registry().get<SocialComponent>(e);
    auto& op  = sim_.registry().get<OpinionComponent>(e);

    char buf[64];
    int lh_s = fonts_.line_height(FontSize::Small);
    int lh_n = fonts_.line_height(FontSize::Normal);

    // Title
    std::snprintf(buf, sizeof(buf), "Agent[%d]", ag.id);
    fonts_.draw(px + margin, y, buf, COL_CYAN, FontSize::Normal);
    y += lh_n + 2;

    static const char* aname[] = {
        "GATHER","BUILD","WORK","EAT","REST","SOCIAL","CREATE","EXPLORE","GETFOOD","MAINT","DSMNTL","SABOTAGE","IDLE"
    };
    std::snprintf(buf, sizeof(buf), "%s  pos:%d,%d", aname[(int)ac.current], po.x, po.y);
    fonts_.draw(px + margin, y, buf, COL_DIM, FontSize::Small);
    y += lh_s + 4;

    // Tabs
    render_separator(px + 3, y, pw - 6); y += 2;
    static const char* tab_names[] = {"Needs", "Pers", "Soc", "Util"};
    int tab_w = bw / 4;
    for (int i = 0; i < 4; i++) {
        SDL_Color bg = (i == (int)panel_tab_) ? COL_TAB_ACTIVE : COL_TAB_INACTIVE;
        render_rect(px + margin + i * tab_w, y, tab_w - 1, lh_s + 4, bg);
        SDL_Color tc = (i == (int)panel_tab_) ? COL_HIGHLIGHT : COL_DIM;
        int tw = fonts_.text_width(tab_names[i], FontSize::Small);
        fonts_.draw(px + margin + i * tab_w + (tab_w - tw) / 2, y + 2, tab_names[i], tc, FontSize::Small);
    }
    y += lh_s + 6;
    render_separator(px + 3, y, pw - 6); y += 4;

    // Tab content
    int bar_label_w = 50;
    int bar_x = px + margin + bar_label_w;
    int bar_w = bw - bar_label_w - 36;

    auto need_line = [&](const char* label, float val, SDL_Color c) {
        fonts_.draw(px + margin, y, label, COL_DIM, FontSize::Small);
        render_bar(bar_x, y + 1, bar_w, lh_s - 2, val, c, COL_BAR_BG);
        std::snprintf(buf, sizeof(buf), "%.2f", val);
        fonts_.draw(bar_x + bar_w + 3, y, buf, COL_DIM, FontSize::Small);
        y += lh_s + 1;
    };

    auto pg = [&](const char* label, float val) {
        fonts_.draw(px + margin, y, label, COL_DIM, FontSize::Small);
        render_bar(bar_x, y + 1, bar_w, lh_s - 2, val, {150, 150, 200, 255}, COL_BAR_BG);
        y += lh_s + 1;
    };

    switch (panel_tab_) {
        case PanelTab::Needs:
            need_line("Hunger", nd.hunger, COL_RED);
            need_line("Rest", nd.rest, {100, 100, 255, 255});
            need_line("Social", nd.social, COL_CYAN);
            need_line("Expr", nd.expression, COL_MAGENTA);
            need_line("Purpose", nd.purpose, COL_YELLOW);
            need_line("Meaning", nd.meaning, {180, 120, 255, 255});
            need_line("Stress", st.value, {255, 80, 80, 255});
            y += 2;
            fonts_.drawf(px + margin, y, {200, 100, 255, 255}, FontSize::Small, "trauma: %.2f", st.trauma);
            y += lh_s + 1;
            fonts_.draw(px + margin, y, stress_state_name(st.state),
                st.state == StressState::REDEEMED ? COL_GREEN :
                st.state == StressState::BROKEN ? COL_RED : COL_DIM, FontSize::Small);
            y += lh_s + 1;
            if (ag.noncompliance > 0.01f) {
                fonts_.drawf(px + margin, y, {200, 150, 60, 255}, FontSize::Small, "noncomp: %.2f", ag.noncompliance);
                y += lh_s + 1;
            }
            y += 2;
            render_separator(px + 3, y, pw - 6); y += 4;
            fonts_.draw(px + margin, y, "INVENTORY", COL_WHITE, FontSize::Small);
            y += lh_s + 1;
            fonts_.drawf(px + margin, y, COL_DIM, FontSize::Small, "raw_f %.1f raw_m %.1f", iv.raw_food, iv.raw_material);
            y += lh_s + 1;
            fonts_.drawf(px + margin, y, COL_DIM, FontSize::Small, "food %.1f  c_mat %.1f", iv.food, iv.construction_material);
            y += lh_s + 2;
            break;

        case PanelTab::Personality:
            pg("comp", ps.compliance);
            pg("lazy", ps.laziness);
            pg("art", ps.artistry);
            pg("greg", ps.gregariousness);
            pg("res", ps.resilience);
            pg("cur", ps.curiosity);
            y += 4;
            break;

        case PanelTab::Social:
            pg("mood", soc.mood);
            pg("infl", soc.influence);
            pg("enrg", soc.social_energy);
            y += 4;
            render_separator(px + 3, y, pw - 6); y += 4;
            fonts_.draw(px + margin, y, "OPINIONS", COL_WHITE, FontSize::Small);
            y += lh_s + 1;
            pg("ethic", op.values[0]);
            pg("risk",  op.values[1]);
            pg("trad",  op.values[2]);
            pg("solid", op.values[3]);
            y += 4;
            break;

        case PanelTab::Utility: {
            auto ur = [&](const char* label, float val) {
                fonts_.draw(px + margin, y, label, COL_DIM, FontSize::Small);
                render_bar(bar_x, y + 1, bar_w, lh_s - 2, val / 2.0f, {100, 100, 130, 255}, COL_BAR_BG);
                y += lh_s + 1;
            };
            ur("GATH", ac.last_utility_gather);
            ur("BULD", ac.last_utility_build);
            ur("WORK", ac.last_utility_work);
            ur("EAT", ac.last_utility_eat);
            ur("REST", ac.last_utility_rest);
            ur("SOC", ac.last_utility_socialize);
            ur("CREA", ac.last_utility_create);
            ur("EXPL", ac.last_utility_explore);
            y += 4;
            break;
        }
    }

    // Chronicle log — first-person narrative from selected agent
    render_separator(px + 3, y, pw - 6); y += 4;
    fonts_.draw(px + margin, y, "JOURNAL", COL_WHITE, FontSize::Small);
    fonts_.draw(px + margin + fonts_.text_width("JOURNAL", FontSize::Small) + 6, y, "PgUp/Dn scroll", COL_DIM, FontSize::Small);
    y += lh_s + 2;

    int footer_y = wh - lh_s - 4;
    int log_line_h = lh_s + 1;
    int max_log_lines = (footer_y - y - 4) / log_line_h;
    if (max_log_lines < 1) max_log_lines = 1;
    if (max_log_lines > 30) max_log_lines = 30;

    auto stress_color = [](StressState ss) -> SDL_Color {
        switch (ss) {
            case StressState::NORMAL:          return {180, 200, 180, 255};
            case StressState::DISSOCIATED:     return {120, 120, 160, 255};
            case StressState::HOSTILE_EUPHORIA: return {220, 80, 80, 255};
            case StressState::BROKEN:          return {90, 90, 90, 255};
            case StressState::REDEEMED:        return {80, 220, 160, 255};
            default:                           return COL_DIM;
        }
    };

    // Fetch agent's events
    std::vector<const ChronicleEvent*> events;
    {
        int total = sim_.chronicle().count_for_agent(ag.id);
        int skip = std::max(0, total - max_log_lines - panel_scroll_);
        auto all = sim_.chronicle().by_agent(ag.id);
        for (int i = skip; i < (int)all.size() && (int)events.size() < max_log_lines; i++)
            events.push_back(all[i]);
    }

    int max_ch = (pw - margin * 2) / (fonts_.text_width("m", FontSize::Small));
    SDL_Color line_col = stress_color(st.state);
    for (auto* ev : events) {
        std::string line = ev->narrative(ps.archetype, st.state);
        // Word-wrap instead of truncating
        size_t pos = 0;
        while (pos < line.size()) {
            if (y + log_line_h > footer_y - 2) break;
            size_t end = std::min(pos + max_ch, line.size());
            if (end < line.size()) {
                size_t sp = line.rfind(' ', end);
                if (sp != std::string::npos && sp > pos) end = sp;
            }
            std::string chunk = line.substr(pos, end - pos);
            // Trim leading space on continuation lines
            if (pos > 0 && !chunk.empty() && chunk[0] == ' ') chunk = chunk.substr(1);
            fonts_.draw(px + margin, y, chunk, line_col, FontSize::Small);
            y += log_line_h;
            pos = end;
            if (pos < line.size() && line[pos] == ' ') pos++;
        }
        if (y + log_line_h > footer_y - 2) break;
    }

    if (events.empty()) {
        fonts_.draw(px + margin, y, "No memories yet.", COL_DIM, FontSize::Small);
    }

    // Footer
    render_separator(px + 3, footer_y - 2, pw - 6);
    fonts_.drawf(px + margin, footer_y, COL_DIM, FontSize::Small,
        "tick:%d alive:%d built:%d",
        sim_.tick(), sim_.alive_count(), sim_.total_machines_built());
}

// ============================================================
// Tooltip
// ============================================================

void GraphicalView::render_tooltip() {
    if (hover_gx_ < 0 || hover_gy_ < 0) return;
    const Grid& grid = sim_.grid();
    if (hover_gx_ >= grid.width() || hover_gy_ >= grid.height()) return;

    TileType t = grid.at(hover_gx_, hover_gy_);
    const auto& d = grid.data_at(hover_gx_, hover_gy_);

    // Count agents on tile
    int ac = 0;
    auto agents = sim_.alive_agents();
    for (auto e : agents) {
        auto& pos = sim_.registry().get<PositionComponent>(e);
        if (pos.x == hover_gx_ && pos.y == hover_gy_) ac++;
    }

    char buf[128];
    const char* tname = "Unknown";
    switch (t) {
        case TileType::Floor: tname = "Floor"; break;
        case TileType::Wall: tname = "Wall"; break;
        case TileType::OpenSpace: tname = "OpenSpace"; break;
        case TileType::Machine: tname = d.built ? "Machine (built)" : "Machine (unbuilt)"; break;
        case TileType::Storage: tname = "Storage"; break;
        case TileType::Entrance: tname = "Entrance"; break;
        case TileType::Exit: tname = "Exit"; break;
        case TileType::ScrapPile: tname = "ScrapPile"; break;
        case TileType::Conveyor: tname = "Conveyor"; break;
        case TileType::EatingZone: tname = "EatingZone"; break;
        case TileType::FoodSource: tname = "FoodSource"; break;
        case TileType::HiddenSpace: tname = "HiddenSpace"; break;
        default: break;
    }

    int mx, my;
    SDL_GetMouseState(&mx, &my);
    int lh = fonts_.line_height(FontSize::Small);

    std::snprintf(buf, sizeof(buf), "%s (%d,%d)", tname, hover_gx_, hover_gy_);
    int tw = fonts_.text_width(buf, FontSize::Small) + 10;
    int th = lh + 6;
    if (ac > 0) th += lh + 2;

    int tx = mx + 12, ty = my + 12;
    int ww, wh;
    get_output_size(ww, wh);
    if (tx + tw > ww) tx = mx - tw - 4;
    if (ty + th > wh) ty = my - th - 4;

    render_rect(tx, ty, tw, th, COL_TOOLTIP_BG);
    render_rect_outline(tx, ty, tw, th, {100, 100, 140, 255});
    fonts_.draw(tx + 5, ty + 3, buf, COL_TEXT, FontSize::Small);

    if (ac > 0) {
        std::snprintf(buf, sizeof(buf), "Agents: %d", ac);
        fonts_.draw(tx + 5, ty + 3 + lh + 2, buf, COL_CYAN, FontSize::Small);
    }
}

// ============================================================
// Help overlay
// ============================================================

void GraphicalView::render_help_overlay() {
    int ww, wh;
    get_output_size(ww, wh);
    int lh = fonts_.line_height(FontSize::Small);
    int lh_n = fonts_.line_height(FontSize::Normal);

    int ow = 380, oh = lh_n + 12 + (20 * (lh + 2));
    int ox = (ww - ow) / 2, oy = (wh - oh) / 2;

    render_rect(ox, oy, ow, oh, {15, 15, 25, 240});
    render_rect_outline(ox, oy, ow, oh, {100, 100, 140, 255});

    int y = oy + 6;
    int x = ox + 8;
    fonts_.draw(x, y, "CONTROLS", COL_HIGHLIGHT, FontSize::Normal);
    y += lh_n + 6;

    auto row = [&](const char* key, const char* desc) {
        fonts_.draw(x, y, key, COL_WHITE, FontSize::Small);
        fonts_.draw(x + 90, y, desc, COL_DIM, FontSize::Small);
        y += lh + 2;
    };

    fonts_.draw(x, y, "Camera", COL_CYAN, FontSize::Small); y += lh + 2;
    row("WASD/Arrows", "Pan camera (hold=smooth)");
    row("O / P", "Zoom out / in");
    row("Scroll", "Zoom");
    row("RMB drag", "Pan camera");
    row("C", "Center agent");
    row("Shift+C", "Center map");
    row("F", "Follow agent");
    y += 4;

    fonts_.draw(x, y, "Simulation", COL_CYAN, FontSize::Small); y += lh + 2;
    row("Space", "Play / Pause");
    row("N", "Step forward (paused)");
    row("< / >", "Speed down / up");
    y += 4;

    fonts_.draw(x, y, "Agents & Panel", COL_CYAN, FontSize::Small); y += lh + 2;
    row("Tab / S-Tab", "Next / prev agent");
    row("[ / ]", "First / last agent");
    row("LMB", "Select agent on tile");
    row("T", "Toggle panel");
    row("1-4", "Panel tabs (Needs/Pers/Soc/Util)");
    row("PgUp/PgDn", "Scroll journal");
    y += 4;

    fonts_.draw(x, y, "Other", COL_CYAN, FontSize::Small); y += lh + 2;
    row("G", "Grid coordinates");
    row("H", "Help");
    row("Esc", "Back / Quit (Y to confirm)");
}

// ============================================================
// Quit confirmation
// ============================================================

void GraphicalView::render_quit_confirm() {
    int ww, wh;
    get_output_size(ww, wh);
    int lh = fonts_.line_height(FontSize::Normal);
    int lh_s = fonts_.line_height(FontSize::Small);

    int cw = 260, ch = lh + lh_s * 2 + 20;
    int cx = (ww - cw) / 2, cy = (wh - ch) / 2;

    render_rect(cx, cy, cw, ch, {30, 15, 15, 240});
    render_rect_outline(cx, cy, cw, ch, COL_RED);

    int y = cy + 8;
    fonts_.draw(cx + 12, y, "Quit?", COL_HIGHLIGHT, FontSize::Large);
    y += lh + 4;
    fonts_.draw(cx + 12, y, "Press Y or Enter to quit", COL_DIM, FontSize::Small);
    y += lh_s + 2;
    fonts_.draw(cx + 12, y, "Any other key to cancel", COL_DIM, FontSize::Small);
}
