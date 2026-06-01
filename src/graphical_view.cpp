#include "graphical_view.h"
#include <algorithm>

GraphicalView::GraphicalView(Simulation& sim, std::atomic<bool>& paused)
    : sim_(sim), paused_(paused) {}

GraphicalView::~GraphicalView() {
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
        "La Vida Misma — 2.5D Factory View",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        std::fprintf(stderr, "SDL window failed: %s\n", SDL_GetError());
        return;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::fprintf(stderr, "SDL renderer failed: %s\n", SDL_GetError());
        return;
    }

    center_camera();

    while (!quit_) {
        handle_events();
        handle_held_keys();
        if (follow_agent_) center_on_agent();
        render();
        SDL_Delay(16);
    }
}

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
    if (window_) SDL_GetWindowSize(window_, &ww, &wh);
    else { ww = WIN_W; wh = WIN_H; }
    cam_x_ = ww * 0.5f - cx;
    cam_y_ = wh * 0.3f - cy;
}

void GraphicalView::center_on_agent() {
    auto agents = sim_.alive_agents();
    if (agents.empty() || (size_t)selected_idx_ >= agents.size()) return;
    auto& pos = sim_.registry().get<PositionComponent>(agents[selected_idx_]);
    float sx, sy;
    iso_to_screen(pos.x, pos.y, sx, sy);
    int ww, wh;
    SDL_GetWindowSize(window_, &ww, &wh);
    int pw = show_log_ ? 280 : 0;
    float target_x = (ww - pw) * 0.5f;
    float target_y = wh * 0.4f;
    cam_x_ += (target_x - sx) * 0.15f;
    cam_y_ += (target_y - sy) * 0.15f;
}

void GraphicalView::next_agent(int dir) {
    auto agents = sim_.alive_agents();
    if (agents.empty()) return;
    selected_idx_ = ((int)selected_idx_ + dir + (int)agents.size()) % (int)agents.size();
}

void GraphicalView::prev_agent() { next_agent(-1); }

void GraphicalView::handle_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                quit_ = true;
                break;

            case SDL_KEYDOWN: {
                SDL_Keycode key = e.key.keysym.sym;
                bool shift = e.key.keysym.mod & KMOD_SHIFT;
                keys_held_.insert(key);

                if (show_help_) {
                    show_help_ = false;
                    break;
                }

                if (chord_active_) {
                    if (chord_key_ == SDLK_j) {
                        switch (key) {
                            case SDLK_1: speed_ms_ = 20; break;
                            case SDLK_2: speed_ms_ = 50; break;
                            case SDLK_3: speed_ms_ = 100; break;
                            case SDLK_4: speed_ms_ = 150; break;
                            case SDLK_5: speed_ms_ = 200; break;
                            case SDLK_6: speed_ms_ = 300; break;
                            case SDLK_7: speed_ms_ = 500; break;
                            case SDLK_r: running_ = true; paused_ = false; break;
                            case SDLK_p: running_ = false; paused_ = true; break;
                            case SDLK_n: if (!running_) sim_.advance(); break;
                            case SDLK_f: follow_agent_ = !follow_agent_; break;
                            case SDLK_l: show_log_ = !show_log_; break;
                            case SDLK_g: show_grid_coords_ = !show_grid_coords_; break;
                        }
                    } else if (chord_key_ == SDLK_k) {
                        switch (key) {
                            case SDLK_a: break;
                            case SDLK_m: break;
                            case SDLK_f: break;
                        }
                    }
                    chord_active_ = false;
                    chord_key_ = SDLK_UNKNOWN;
                    break;
                }

                switch (key) {
                    case SDLK_ESCAPE:
                        quit_ = true; break;
                    case SDLK_SPACE:
                        if (shift) { running_ = false; paused_ = true; }
                        else if (running_) { running_ = false; paused_ = true; }
                        else { sim_.advance(); }
                        break;
                    case SDLK_RETURN: case SDLK_KP_ENTER:
                        running_ = !running_; paused_ = !running_; break;
                    case SDLK_TAB:
                        if (shift) prev_agent(); else next_agent(); break;
                    case SDLK_c:
                        if (shift) center_camera();
                        else center_on_agent();
                        break;
                    case SDLK_z:
                        zoom_ = std::max(0.3f, zoom_ / 1.15f); break;
                    case SDLK_x:
                        zoom_ = std::min(4.0f, zoom_ * 1.15f); break;
                    case SDLK_h:
                        show_help_ = !show_help_; break;
                    case SDLK_f:
                        follow_agent_ = !follow_agent_; break;
                    case SDLK_l:
                        show_log_ = !show_log_; break;
                    case SDLK_g:
                        show_grid_coords_ = !show_grid_coords_; break;
                    case SDLK_j:
                        chord_active_ = true; chord_key_ = SDLK_j; break;
                    case SDLK_k:
                        chord_active_ = true; chord_key_ = SDLK_k; break;
                    case SDLK_i:
                        prev_agent(); break;
                    case SDLK_EQUALS: case SDLK_KP_PLUS:
                        speed_ms_ = std::max(20, speed_ms_ - 25); break;
                    case SDLK_MINUS: case SDLK_KP_MINUS:
                        speed_ms_ = std::min(1000, speed_ms_ + 25); break;
                    case SDLK_1: speed_ms_ = 20; break;
                    case SDLK_2: speed_ms_ = 50; break;
                    case SDLK_3: speed_ms_ = 100; break;
                    case SDLK_4: speed_ms_ = 150; break;
                    case SDLK_5: speed_ms_ = 200; break;
                    case SDLK_6: speed_ms_ = 300; break;
                    case SDLK_7: speed_ms_ = 500; break;
                    case SDLK_LEFT:  cam_x_ += 40; break;
                    case SDLK_RIGHT: cam_x_ -= 40; break;
                    case SDLK_UP:    cam_y_ += 25; break;
                    case SDLK_DOWN:  cam_y_ -= 25; break;
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
                    cam_x_ += e.motion.x - drag_last_x_;
                    cam_y_ += e.motion.y - drag_last_y_;
                    drag_last_x_ = e.motion.x;
                    drag_last_y_ = e.motion.y;
                }
                break;
            case SDL_MOUSEWHEEL:
                if (e.wheel.y > 0) zoom_ = std::min(4.0f, zoom_ * 1.1f);
                else if (e.wheel.y < 0) zoom_ = std::max(0.3f, zoom_ / 1.1f);
                break;
        }
    }
}

void GraphicalView::handle_held_keys() {
    float pan_speed = 6.0f;
    if (keys_held_.count(SDLK_w)) cam_y_ += pan_speed;
    if (keys_held_.count(SDLK_s)) cam_y_ -= pan_speed;
    if (keys_held_.count(SDLK_a)) cam_x_ += pan_speed;
    if (keys_held_.count(SDLK_d)) cam_x_ -= pan_speed;
}

void GraphicalView::render() {
    SDL_SetRenderDrawColor(renderer_, COL_BG.r, COL_BG.g, COL_BG.b, 255);
    SDL_RenderClear(renderer_);

    const Grid& grid = sim_.grid();
    auto agents = sim_.alive_agents();

    int ww, wh;
    SDL_GetWindowSize(window_, &wh, &wh);
    SDL_GetWindowSize(window_, &ww, &wh);
    int panel_w = show_log_ ? 280 : 0;
    int map_w = ww - panel_w;

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

    if (show_help_)
        render_help_overlay();

    SDL_RenderPresent(renderer_);
}

void GraphicalView::render_iso_tile(int gx, int gy, SDL_Color color, bool filled) {
    float sx, sy;
    iso_to_screen(gx, gy, sx, sy);
    float hw = TILE_W * zoom_ * 0.5f;
    float hh = TILE_H * zoom_ * 0.5f;

    SDL_Vertex verts[4] = {
        {{sx, sy - hh}, color, {0, 0}},
        {{sx + hw, sy}, color, {0, 0}},
        {{sx, sy + hh}, color, {0, 0}},
        {{sx - hw, sy}, color, {0, 0}},
    };

    if (filled) {
        SDL_RenderGeometry(renderer_, nullptr, verts, 4, nullptr, 0);
    }

    SDL_Color edge = {(Uint8)(color.r / 2), (Uint8)(color.g / 2), (Uint8)(color.b / 2), 255};
    SDL_SetRenderDrawColor(renderer_, edge.r, edge.g, edge.b, 255);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        SDL_RenderDrawLine(renderer_,
            (int)verts[i].position.x, (int)verts[i].position.y,
            (int)verts[j].position.x, (int)verts[j].position.y);
    }
}

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

    SDL_Color color = COL_FLOOR;
    bool filled = true;
    bool raised = false;
    SDL_Color top_color = {0, 0, 0, 0};

    switch (t) {
        case TileType::Wall:
            color = COL_WALL; raised = true; top_color = {80, 80, 100, 255}; break;
        case TileType::Floor:
            color = COL_FLOOR; break;
        case TileType::OpenSpace:
            color = COL_OPEN; break;
        case TileType::Machine:
            if (d.built) {
                color = COL_MACHINE;
                top_color = {140, 255, 140, 255};
            } else {
                float pct = d.build_cost > 0 ? d.build_progress / d.build_cost : 0;
                if (pct > 0.5f) color = {255, 180, 80, 255};
                else if (pct > 0.0f) color = {255, 130, 80, 255};
                else color = COL_MACH_UB;
                top_color = {color.r, (Uint8)(color.g + 20), (Uint8)(color.b + 20), 255};
            }
            raised = true;
            break;
        case TileType::Storage: {
            float tot = d.stored_food + d.stored_raw_food + d.stored_raw_material;
            color = (tot > 5.f) ? COL_STORAGE : (tot > 0.5f) ? SDL_Color{180, 180, 80, 255} : COL_STOR_E;
            top_color = {color.r, color.g, (Uint8)(color.b + 30), 255};
            raised = true;
            break;
        }
        case TileType::Entrance: color = COL_ENTRANCE; break;
        case TileType::Exit: color = COL_EXIT; break;
        case TileType::ScrapPile:
            color = (d.resource_amount > 2.f) ? COL_SCRAP : COL_SCRAP_E;
            top_color = {(Uint8)(color.r + 20), (Uint8)(color.g + 20), color.b, 255};
            raised = true;
            break;
        case TileType::Conveyor: {
            if (!d.built) {
                float pct = d.build_cost > 0 ? d.build_progress / d.build_cost : 0;
                color = (pct > 0.f) ? SDL_Color{100, 100, 100, 255} : COL_CONV_UB;
            } else if (d.conveyor_condition < 0.2f) {
                color = COL_RED;
            } else if (d.conveyor_contents > 0.01f) {
                color = (d.conveyor_condition > 0.7f) ? SDL_Color{100, 220, 255, 255}
                       : SDL_Color{220, 220, 100, 255};
            } else {
                color = (d.conveyor_condition > 0.7f) ? COL_CONV
                       : SDL_Color{180, 180, 60, 255};
            }
            break;
        }
        case TileType::EatingZone:
            if (d.built) color = COL_EZ;
            else {
                float pct = d.build_cost > 0 ? d.build_progress / d.build_cost : 0;
                color = (pct > 0.5f) ? SDL_Color{80, 180, 180, 255} : COL_EZ_UB;
            }
            break;
        case TileType::FoodSource:
            color = (d.resource_amount > 1.5f) ? SDL_Color{100, 200, 80, 255}
                   : (d.resource_amount > 0.3f) ? SDL_Color{180, 200, 80, 255}
                   : SDL_Color{80, 100, 60, 255};
            raised = true;
            break;
        case TileType::HiddenSpace:
            color = {60, 40, 80, 255}; raised = true;
            top_color = {80, 60, 110, 255};
            break;
        default:
            color = {150, 50, 150, 255}; break;
    }

    render_iso_tile(gx, gy, color, filled);

    if (raised) {
        float sx, sy;
        iso_to_screen(gx, gy, sx, sy);
        float hw = TILE_W * zoom_ * 0.5f;
        float hh = TILE_H * zoom_ * 0.5f;
        float rh = 4.0f * zoom_;

        SDL_Color side = {(Uint8)(top_color.r / 2), (Uint8)(top_color.g / 2), (Uint8)(top_color.b / 2), 255};

        SDL_Vertex left_face[3] = {
            {{sx - hw, sy}, side, {0, 0}},
            {{sx, sy + hh}, side, {0, 0}},
            {{sx, sy + hh + rh}, side, {0, 0}},
        };
        SDL_RenderGeometry(renderer_, nullptr, left_face, 3, nullptr, 0);

        SDL_Vertex right_face[3] = {
            {{sx + hw, sy}, side, {0, 0}},
            {{sx, sy + hh}, side, {0, 0}},
            {{sx, sy + hh + rh}, side, {0, 0}},
        };
        SDL_RenderGeometry(renderer_, nullptr, right_face, 3, nullptr, 0);

        SDL_Vertex top[4] = {
            {{sx, sy - hh - rh}, top_color, {0, 0}},
            {{sx + hw, sy - rh}, top_color, {0, 0}},
            {{sx, sy + hh - rh}, top_color, {0, 0}},
            {{sx - hw, sy - rh}, top_color, {0, 0}},
        };
        SDL_RenderGeometry(renderer_, nullptr, top, 4, nullptr, 0);
    }

    if (t == TileType::Conveyor && d.built && d.conveyor_condition >= 0.2f) {
        float sx, sy;
        iso_to_screen(gx, gy, sx, sy);
        float as = 4.0f * zoom_;
        SDL_SetRenderDrawColor(renderer_, 200, 200, 200, 255);
        int dx = 0, dy = 0;
        switch (d.conveyor_dir) {
            case ConveyorDir::N: dy = -1; break;
            case ConveyorDir::S: dy = 1; break;
            case ConveyorDir::E: dx = 1; break;
            case ConveyorDir::W: dx = -1; break;
        }
        float ox = dx * as, oy = dy * as * 0.5f;
        SDL_RenderDrawLine(renderer_, (int)(sx - ox), (int)(sy - oy), (int)(sx + ox), (int)(sy + oy));
        SDL_RenderDrawLine(renderer_, (int)(sx + ox), (int)(sy + oy),
            (int)(sx + ox - as * 0.5f * (dx - dy)), (int)(sy + oy - as * 0.5f * (dy - dx)));
    }

    if (ac > 0) render_agent_marker(gx, gy, sel, ac);
}

void GraphicalView::render_agent_marker(int gx, int gy, bool selected, int count) {
    float sx, sy;
    iso_to_screen(gx, gy, sx, sy);
    float r = 4.0f * zoom_;

    auto agents = sim_.alive_agents();
    SDL_Color c = COL_AGENT;
    if ((size_t)selected_idx_ < agents.size()) {
        auto& st = sim_.registry().get<StressComponent>(agents[selected_idx_]);
        if (selected) {
            switch (st.state) {
                case StressState::NORMAL:          c = COL_SELECTED; break;
                case StressState::DISSOCIATED:     c = COL_CYAN; break;
                case StressState::HOSTILE_EUPHORIA:c = COL_MAGENTA; break;
                case StressState::BROKEN:          c = COL_RED; break;
                case StressState::REDEEMED:        c = COL_GREEN; break;
            }
        }
    }

    float cx = sx;
    float cy = sy - 2.0f * zoom_;

    std::vector<SDL_Vertex> verts;
    int segments = 12;
    for (int i = 0; i < segments; i++) {
        float a1 = 2.0f * 3.14159265f * i / segments;
        float a2 = 2.0f * 3.14159265f * (i + 1) / segments;
        verts.push_back({{cx, cy}, c, {0, 0}});
        verts.push_back({{cx + std::cos(a1) * r, cy + std::sin(a1) * r}, c, {0, 0}});
        verts.push_back({{cx + std::cos(a2) * r, cy + std::sin(a2) * r}, c, {0, 0}});
    }
    SDL_RenderGeometry(renderer_, nullptr, verts.data(), (int)verts.size(), nullptr, 0);

    if (selected) {
        SDL_SetRenderDrawColor(renderer_, 255, 255, 200, 255);
        SDL_RenderDrawLine(renderer_, (int)cx, (int)(cy - r - 6 * zoom_), (int)cx, (int)(cy - r - 1));
        SDL_RenderDrawLine(renderer_, (int)(cx - 3 * zoom_), (int)(cy - r - 3 * zoom_),
                           (int)(cx + 3 * zoom_), (int)(cy - r - 3 * zoom_));
    }

    if (count > 1) {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%d", std::min(count, 9));
        render_text_solid((int)(cx + r), (int)(cy - r), buf, COL_WHITE);
    }
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

void GraphicalView::render_text_solid(int x, int y, const std::string& text, SDL_Color color) {
    render_text_solid(x, y, text.c_str(), color);
}

void GraphicalView::render_text_solid(int x, int y, const char* text, SDL_Color color) {
    if (!text || !text[0]) return;

    // 5-wide x 7-tall bitmap font. Each byte = one row, bits 4..0 = columns left-to-right.
    struct Glyph { uint8_t rows[7]; };

    auto lookup = [](unsigned char ch) -> Glyph {
        switch (ch) {
            case ' ': return {{0x00,0x00,0x00,0x00,0x00,0x00,0x00}};
            case '!': return {{0x04,0x04,0x04,0x04,0x04,0x00,0x04}};
            case '#': return {{0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}};
            case '%': return {{0x18,0x19,0x02,0x04,0x08,0x13,0x03}};
            case '(': return {{0x02,0x04,0x08,0x08,0x08,0x04,0x02}};
            case ')': return {{0x08,0x04,0x02,0x02,0x02,0x04,0x08}};
            case '+': return {{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}};
            case '-': return {{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}};
            case '.': return {{0x00,0x00,0x00,0x00,0x00,0x00,0x04}};
            case ':': return {{0x00,0x00,0x04,0x00,0x04,0x00,0x00}};
            case ';': return {{0x00,0x00,0x04,0x00,0x04,0x04,0x08}};
            case '/': return {{0x01,0x01,0x02,0x04,0x08,0x10,0x10}};
            case '_': return {{0x00,0x00,0x00,0x00,0x00,0x00,0x1F}};
            case '<': return {{0x02,0x04,0x08,0x10,0x08,0x04,0x02}};
            case '>': return {{0x08,0x04,0x02,0x01,0x02,0x04,0x08}};
            case '=': return {{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}};
            case '?': return {{0x0E,0x11,0x01,0x02,0x04,0x00,0x04}};
            case '0': return {{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}};
            case '1': return {{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}};
            case '2': return {{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}};
            case '3': return {{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}};
            case '4': return {{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}};
            case '5': return {{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}};
            case '6': return {{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}};
            case '7': return {{0x1F,0x01,0x02,0x04,0x04,0x04,0x04}};
            case '8': return {{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}};
            case '9': return {{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
            case 'A': case 'a': return {{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}};
            case 'B': case 'b': return {{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}};
            case 'C': case 'c': return {{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}};
            case 'D': case 'd': return {{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}};
            case 'E': case 'e': return {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}};
            case 'F': case 'f': return {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}};
            case 'G': case 'g': return {{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}};
            case 'H': case 'h': return {{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}};
            case 'I': case 'i': return {{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}};
            case 'J': case 'j': return {{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}};
            case 'K': case 'k': return {{0x11,0x12,0x14,0x18,0x14,0x12,0x11}};
            case 'L': case 'l': return {{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}};
            case 'M': case 'm': return {{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}};
            case 'N': case 'n': return {{0x11,0x19,0x15,0x13,0x11,0x11,0x11}};
            case 'O': case 'o': return {{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}};
            case 'P': case 'p': return {{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}};
            case 'Q': case 'q': return {{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}};
            case 'R': case 'r': return {{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}};
            case 'S': case 's': return {{0x0E,0x10,0x10,0x0E,0x01,0x01,0x1E}};
            case 'T': case 't': return {{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}};
            case 'U': case 'u': return {{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}};
            case 'V': case 'v': return {{0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}};
            case 'W': case 'w': return {{0x11,0x11,0x11,0x15,0x15,0x1B,0x11}};
            case 'X': case 'x': return {{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}};
            case 'Y': case 'y': return {{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}};
            case 'Z': case 'z': return {{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}};
            default:  return {{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}};
        }
    };

    static const int PX = 2;  // each font pixel = PX x PX screen pixels
    int cx = x;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);

    while (*text) {
        Glyph g = lookup((unsigned char)*text);
        for (int row = 0; row < 7; row++) {
            uint8_t bits = g.rows[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (0x10 >> col)) {
                    SDL_Rect r = {cx + col * PX, y + row * PX, PX, PX};
                    SDL_RenderFillRect(renderer_, &r);
                }
            }
        }
        cx += 5 * PX + PX;  // 5 cols + 1px spacing
        text++;
    }
}

void GraphicalView::render_header_bar() {
    int ww, wh;
    SDL_GetWindowSize(window_, &ww, &wh);
    int panel_w = show_log_ ? 300 : 0;
    int header_h = 32;

    render_rect(0, 0, ww, header_h, COL_HEADER);
    SDL_SetRenderDrawColor(renderer_, 50, 50, 65, 255);
    SDL_RenderDrawLine(renderer_, 0, header_h, ww, header_h);

    int x = 8;
    render_text_solid(x, 8, "LA VIDA MISMA", {200, 220, 255, 255}); x += 130;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "tick:%d", sim_.tick());
    render_text_solid(x, 9, buf, COL_TEXT); x += 75;

    std::snprintf(buf, sizeof(buf), "alive:%d/%d", sim_.alive_count(), sim_.config().initial_population);
    render_text_solid(x, 9, buf, COL_TEXT); x += 90;

    std::snprintf(buf, sizeof(buf), "built:%d", sim_.total_machines_built());
    render_text_solid(x, 9, buf, COL_GREEN); x += 65;

    std::snprintf(buf, sizeof(buf), "food:%.1f", sim_.total_storage_food());
    render_text_solid(x, 9, buf, COL_YELLOW); x += 65;

    float fh = sim_.factory_health();
    SDL_Color fh_c = (fh > 0.66f) ? COL_GREEN : (fh > 0.33f) ? COL_YELLOW : COL_RED;
    std::snprintf(buf, sizeof(buf), "factory:%.0f%%", fh * 100);
    render_text_solid(x, 9, buf, fh_c); x += 80;

    float qf = sim_.last_quota_fill();
    SDL_Color qf_c = (qf >= 1.0f) ? COL_GREEN : (qf > 0.0f) ? COL_YELLOW : COL_RED;
    std::snprintf(buf, sizeof(buf), "quota:%.0f%%", qf * 100);
    render_text_solid(x, 9, buf, qf_c); x += 75;

    std::snprintf(buf, sizeof(buf), "shipped:%.0f", sim_.total_food_shipped());
    render_text_solid(x, 9, buf, COL_DIM); x += 85;

    std::snprintf(buf, sizeof(buf), "broken:%d", sim_.total_machines_broken());
    render_text_solid(x, 9, buf, COL_DIM); x += 65;

    int rx = ww - panel_w - 220;
    if (chord_active_) {
        const char* cname = (chord_key_ == SDLK_j) ? "J" : "K";
        char cb[8];
        std::snprintf(cb, sizeof(cb), "[%s]", cname);
        render_text_solid(rx, 9, cb, COL_CHORD); rx += 30;
    }

    if (follow_agent_) {
        render_text_solid(rx, 9, "FOLLOW", COL_CYAN); rx += 55;
    }

    const char* status = running_ ? "RUN" : "PAUSED";
    SDL_Color sc = running_ ? COL_GREEN : COL_YELLOW;
    render_text_solid(rx, 9, status, sc); rx += 55;

    std::snprintf(buf, sizeof(buf), "%dms", speed_ms_);
    render_text_solid(rx, 9, buf, COL_DIM);
}

void GraphicalView::render_side_panel() {
    int ww, wh;
    SDL_GetWindowSize(window_, &ww, &wh);
    int pw = 300;
    int px = ww - pw;
    int header_h = 32;

    render_rect(px, 0, pw, wh, COL_PANEL_BG);
    SDL_SetRenderDrawColor(renderer_, 50, 50, 65, 255);
    SDL_RenderDrawLine(renderer_, px, 0, px, wh);

    int y = header_h + 8;
    auto agents = sim_.alive_agents();

    if (agents.empty()) {
        render_text_solid(px + 10, y, "No alive agents.", COL_DIM);
        return;
    }

    if ((size_t)selected_idx_ >= agents.size()) selected_idx_ = 0;
    auto e = agents[selected_idx_];
    [[maybe_unused]] auto& ag  = sim_.registry().get<AgentComponent>(e);
    auto& nd  = sim_.registry().get<NeedsComponent>(e);
    auto& ps  = sim_.registry().get<PersonalityComponent>(e);
    auto& ac  = sim_.registry().get<ActionComponent>(e);
    auto& st  = sim_.registry().get<StressComponent>(e);
    auto& iv  = sim_.registry().get<InventoryComponent>(e);
    auto& po  = sim_.registry().get<PositionComponent>(e);
    auto& soc = sim_.registry().get<SocialComponent>(e);
    auto& op  = sim_.registry().get<OpinionComponent>(e);

    int bw = pw - 20;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "Agent[%d]", ag.id);
    render_text_solid(px + 10, y, buf, COL_CYAN); y += 16;

    static const char* aname[] = {
        "GATHER","BUILD","WORK","EAT","REST","SOCIAL","CREATE","EXPLORE","GETFOOD","MAINT","DSMNTL","SABOTAGE","IDLE"
    };
    render_text_solid(px + 10, y, aname[(int)ac.current], COL_WHITE); y += 16;

    std::snprintf(buf, sizeof(buf), "pos: %d %d", po.x, po.y);
    render_text_solid(px + 10, y, buf, COL_DIM); y += 20;

    SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
    SDL_RenderDrawLine(renderer_, px + 5, y, px + pw - 5, y); y += 6;

    render_text_solid(px + 10, y, "NEEDS", COL_WHITE); y += 16;

    auto need_line = [&](const char* label, float val, SDL_Color c) {
        render_text_solid(px + 10, y, label, COL_DIM);
        render_bar(px + 70, y, bw - 70, 8, val, c, COL_BAR_BG);
        std::snprintf(buf, sizeof(buf), "%.2f", val);
        render_text_solid(px + bw - 20, y, buf, COL_DIM);
        y += 14;
    };
    need_line("Hunger", nd.hunger, COL_RED);
    need_line("Rest", nd.rest, {100, 100, 255, 255});
    need_line("Social", nd.social, COL_CYAN);
    need_line("Expr", nd.expression, COL_MAGENTA);
    need_line("Purpose", nd.purpose, COL_YELLOW);
    need_line("Meaning", nd.meaning, {180, 120, 255, 255});
    need_line("Stress", st.value, {255, 80, 80, 255});
    y += 4;

    std::snprintf(buf, sizeof(buf), "trauma: %.2f", st.trauma);
    render_text_solid(px + 10, y, buf, {200, 100, 255, 255}); y += 14;

    render_text_solid(px + 10, y, stress_state_name(st.state),
        st.state == StressState::REDEEMED ? COL_GREEN :
        st.state == StressState::BROKEN ? COL_RED : COL_DIM); y += 14;

    if (ag.noncompliance > 0.01f) {
        std::snprintf(buf, sizeof(buf), "noncomp: %.2f", ag.noncompliance);
        render_text_solid(px + 10, y, buf, {200, 150, 60, 255}); y += 16;
    }
    y += 4;

    SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
    SDL_RenderDrawLine(renderer_, px + 5, y, px + pw - 5, y); y += 6;

    render_text_solid(px + 10, y, "INVENTORY", COL_WHITE); y += 16;
    std::snprintf(buf, sizeof(buf), "raw_f %.1f raw_m %.1f", iv.raw_food, iv.raw_material);
    render_text_solid(px + 10, y, buf, COL_DIM); y += 14;
    std::snprintf(buf, sizeof(buf), "food %.1f  c_mat %.1f", iv.food, iv.construction_material);
    render_text_solid(px + 10, y, buf, COL_DIM); y += 20;

    SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
    SDL_RenderDrawLine(renderer_, px + 5, y, px + pw - 5, y); y += 6;

    render_text_solid(px + 10, y, "PERSONALITY", COL_WHITE); y += 16;
    auto pg = [&](const char* label, float val) {
        render_text_solid(px + 10, y, label, COL_DIM);
        render_bar(px + 70, y, bw - 70, 6, val, {150, 150, 200, 255}, COL_BAR_BG);
        y += 12;
    };
    pg("comp", ps.compliance);
    pg("lazy", ps.laziness);
    pg("art", ps.artistry);
    pg("greg", ps.gregariousness);
    pg("res", ps.resilience);
    pg("cur", ps.curiosity);
    y += 4;

    SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
    SDL_RenderDrawLine(renderer_, px + 5, y, px + pw - 5, y); y += 6;

    render_text_solid(px + 10, y, "SOCIAL", COL_WHITE); y += 16;
    pg("mood", soc.mood);
    pg("infl", soc.influence);
    pg("enrg", soc.social_energy);
    y += 4;

    SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
    SDL_RenderDrawLine(renderer_, px + 5, y, px + pw - 5, y); y += 6;

    render_text_solid(px + 10, y, "OPINIONS", COL_WHITE); y += 16;
    pg("ethic", op.values[0]);
    pg("risk",  op.values[1]);
    pg("trad",  op.values[2]);
    pg("solid", op.values[3]);
    y += 4;

    SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
    SDL_RenderDrawLine(renderer_, px + 5, y, px + pw - 5, y); y += 6;

    render_text_solid(px + 10, y, "UTILITY", COL_WHITE); y += 16;
    auto ur = [&](const char* label, float val) {
        render_text_solid(px + 10, y, label, COL_DIM);
        render_bar(px + 70, y, bw - 70, 6, val / 2.0f, {100, 100, 130, 255}, COL_BAR_BG);
        y += 12;
    };
    ur("GATH", ac.last_utility_gather);
    ur("BULD", ac.last_utility_build);
    ur("WORK", ac.last_utility_work);
    ur("EAT", ac.last_utility_eat);
    ur("REST", ac.last_utility_rest);
    ur("SOC", ac.last_utility_socialize);
    ur("CREA", ac.last_utility_create);
    ur("EXPL", ac.last_utility_explore);

    int footer_y = wh - 24;
    SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
    SDL_RenderDrawLine(renderer_, px + 5, footer_y - 4, px + pw - 5, footer_y - 4);

    std::snprintf(buf, sizeof(buf), "tick:%d alive:%d built:%d",
        sim_.tick(), sim_.alive_count(), sim_.total_machines_built());
    render_text_solid(px + 10, footer_y, buf, COL_DIM);
}

void GraphicalView::render_help_overlay() {
    int ww, wh;
    SDL_GetWindowSize(window_, &ww, &wh);

    int ow = 560, oh = 640;
    int ox = (ww - ow) / 2, oy = (wh - oh) / 2;

    render_rect(ox, oy, ow, oh, {15, 15, 25, 240});
    render_rect_outline(ox, oy, ow, oh, {100, 100, 140, 255});

    int y = oy + 12;
    render_text_solid(ox + 15, y, "CONTROLS", COL_HIGHLIGHT); y += 24;

    auto row = [&](const char* key, const char* desc) {
        render_text_solid(ox + 15, y, key, COL_WHITE);
        render_text_solid(ox + 160, y, desc, COL_DIM);
        y += 17;
    };

    render_text_solid(ox + 15, y, "Navigation", COL_CYAN); y += 18;
    row("W A S D", "Pan camera (hold for continuous)");
    row("Z / X", "Zoom out / in");
    row("Scroll", "Zoom");
    row("RMB drag", "Pan camera");
    row("C", "Center on selected agent");
    row("Shift+C", "Center on map");
    row("F", "Follow selected agent (auto-center)");
    y += 6;

    render_text_solid(ox + 15, y, "Simulation", COL_CYAN); y += 18;
    row("Space", "Step 1 tick (paused) / toggle run");
    row("Enter", "Toggle run / pause");
    row("1-7", "Speed presets (20ms..500ms)");
    row("+/-", "Speed up / slow down");
    y += 6;

    render_text_solid(ox + 15, y, "Agents", COL_CYAN); y += 18;
    row("Tab / I", "Next / previous agent");
    row("LMB", "Select agent on tile");
    row("L", "Toggle side panel");
    y += 6;

    render_text_solid(ox + 15, y, "Chords (press then sub-key)", COL_CYAN); y += 18;
    row("J 1-7", "Speed preset");
    row("J R/P/N", "Run / Pause / step");
    row("J F", "Follow toggle");
    row("J L", "Toggle panel");
    row("J G", "Grid coordinates");
    y += 6;

    render_text_solid(ox + 15, y, "Other", COL_CYAN); y += 18;
    row("H", "This help");
    row("G", "Grid coordinates");
    row("Esc", "Quit");
    y += 14;

    SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
    SDL_RenderDrawLine(renderer_, ox + 10, y, ox + ow - 10, y);
    y += 10;

    render_text_solid(ox + 15, y, "LEGEND", COL_HIGHLIGHT); y += 20;
    auto leg = [&](SDL_Color c, const char* t) {
        render_rect(ox + 15, y, 10, 10, c);
        render_text_solid(ox + 32, y, t, COL_DIM);
        y += 16;
    };
    leg(COL_WALL, "Wall");
    leg(COL_MACHINE, "Machine (built)");
    leg(COL_MACH_UB, "Machine (unbuilt)");
    leg(COL_STORAGE, "Storage");
    leg(COL_SCRAP, "Scrap pile");
    leg(COL_CONV, "Conveyor");
    leg(COL_OPEN, "Open space");
    leg(COL_AGENT, "Agent");
    leg(COL_SELECTED, "Selected agent");
    leg(COL_RED, "Agent: Broken stress");
    leg(COL_MAGENTA, "Agent: Euphoric stress");
    leg(COL_GREEN, "Agent: Redeemed");
}
