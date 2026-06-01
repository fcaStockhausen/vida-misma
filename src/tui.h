#pragma once
// FTXUI-based TUI renderer for La Vida Misma.

#include "simulation.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <cmath>
#include <algorithm>
#include <thread>

using namespace ftxui;

// ============================================================
class TUI {
public:
    TUI(Simulation& sim)
        : sim_(sim), selected_idx_(0), scroll_x_(0), scroll_y_(0),
          zoom_(2), running_(false), speed_ms_(200) {
        center_on_selected();
    }

    void run() {
        auto screen = ScreenInteractive::Fullscreen();

        auto renderer = Renderer([this]() {
            return build_ui();
        });

        auto component = CatchEvent(renderer, [&](Event e) {
            return handle_event(e, screen);
        });

        std::thread tick_thread([this, &screen]() {
            // Emit initial refresh so the first frame renders immediately
            screen.PostEvent(Event::Custom);
            while (!quit_) {
                if (running_) {
                    sim_.advance();
                    screen.PostEvent(Event::Custom);
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(running_ ? speed_ms_ : 50));
            }
        });

        screen.Loop(component);
        quit_ = true;
        tick_thread.join();
    }

private:
    Simulation& sim_;
    size_t selected_idx_;
    int scroll_x_;
    int scroll_y_;
    int zoom_;                 // chars-per-tile horizontally (1..4); tile height derived
    bool show_help_ = false;   // H key toggles a legend modal
    bool running_ = false;
    bool quit_ = false;
    int speed_ms_;

    // Tile rendering size (characters). Terminal cells are ~2:1 tall:wide,
    // so we double horizontal scaling to keep tiles visually square.
    int tile_chars_w() const { return zoom_; }
    int tile_chars_h() const { return (zoom_ + 1) / 2; }

    // How many tiles fit in the rendered viewport area. Constant target screen size
    // (≈ 80 × 25 chars) so zoom in → fewer/bigger tiles, zoom out → more/smaller tiles.
    static constexpr int TARGET_CHARS_W = 80;
    static constexpr int TARGET_CHARS_H = 25;
    int view_tiles_w() const {
        return std::min(sim_.grid().width(), TARGET_CHARS_W / tile_chars_w());
    }
    int view_tiles_h() const {
        return std::min(sim_.grid().height(), TARGET_CHARS_H / tile_chars_h());
    }

    void clamp_scroll() {
        int gw = sim_.grid().width();
        int gh = sim_.grid().height();
        scroll_x_ = std::clamp(scroll_x_, 0, std::max(0, gw - view_tiles_w()));
        scroll_y_ = std::clamp(scroll_y_, 0, std::max(0, gh - view_tiles_h()));
    }

    bool handle_event(Event e, ScreenInteractive& screen) {
        // H toggles the help modal in any state. Tick-thread posts Event::Custom
        // every ~50 ms while running; that must NOT close the modal.
        if (e == Event::Character("h") || e == Event::Character("H")) {
            show_help_ = !show_help_;
            return false;
        }
        if (show_help_) {
            if (e == Event::Custom) return false;        // ignore ticker
            if (e == Event::Character("q") || e == Event::Escape) {
                screen.Exit(); quit_ = true; return true;
            }
            show_help_ = false;                          // any real key closes
            return false;
        }
        if (e == Event::Character("q") || e == Event::Escape) {
            screen.Exit();
            quit_ = true;
            return true;
        }
        if (e == Event::Character("n")) { sim_.advance(); return false; }
        if (e == Event::Character("r")) { running_ = true; return false; }
        if (e == Event::Character("p")) { running_ = false; return false; }
        if (e == Event::Character("+") || e == Event::Character("=")) {
            speed_ms_ = std::max(20, speed_ms_ - 50); return false;
        }
        if (e == Event::Character("-")) {
            speed_ms_ = std::min(2000, speed_ms_ + 50); return false;
        }
        if (e == Event::Tab)          { next_agent(); return false; }
        if (e == Event::Character("c")) { center_on_selected(); return false; }
        // Pan (arrows): move the viewport across the map, clamped to grid bounds.
        if (e == Event::ArrowLeft)    { scroll_x_ -= 3; clamp_scroll(); return false; }
        if (e == Event::ArrowRight)   { scroll_x_ += 3; clamp_scroll(); return false; }
        if (e == Event::ArrowUp)      { scroll_y_ -= 2; clamp_scroll(); return false; }
        if (e == Event::ArrowDown)    { scroll_y_ += 2; clamp_scroll(); return false; }
        // Zoom: change chars-per-tile, recenter on the previous viewport center.
        if (e == Event::Character("[") || e == Event::Character("]")) {
            int cx = scroll_x_ + view_tiles_w() / 2;
            int cy = scroll_y_ + view_tiles_h() / 2;
            if (e == Event::Character("["))  zoom_ = std::max(1, zoom_ - 1);
            else                              zoom_ = std::min(4, zoom_ + 1);
            scroll_x_ = cx - view_tiles_w() / 2;
            scroll_y_ = cy - view_tiles_h() / 2;
            clamp_scroll();
            return false;
        }
        return false;
    }

    void next_agent() {
        auto agents = sim_.alive_agents();
        if (agents.empty()) return;
        selected_idx_ = (selected_idx_ + 1) % agents.size();
        center_on_selected();
    }

    void center_on_selected() {
        auto agents = sim_.alive_agents();
        if (agents.empty() || selected_idx_ >= agents.size()) return;
        auto& pos = sim_.registry().get<PositionComponent>(agents[selected_idx_]);
        scroll_x_ = pos.x - view_tiles_w() / 2;
        scroll_y_ = pos.y - view_tiles_h() / 2;
        clamp_scroll();
    }

    // ---- Main layout ----
    Element build_ui() {
        Element main = vbox({
            build_header(),
            separator(),
            hbox({
                vbox({
                    build_grid_view() | flex,
                    separator(),
                    build_log_panel() | size(HEIGHT, LESS_THAN, 10),
                }) | flex,
                separator(),
                build_side_panel(),
            }) | flex,
            separator(),
            build_footer(),
        });
        if (show_help_) {
            return dbox({ main, build_help_modal() });
        }
        return main;
    }

    Element build_header() {
        std::string status = running_ ? "RUNNING" : "PAUSED";
        float fh = sim_.factory_health();
        Color fh_color = (fh > 0.66f) ? Color::Green
                       : (fh > 0.33f) ? Color::Yellow
                       :                 Color::Red;
        float qf = sim_.last_quota_fill();
        Color qf_color = (qf >= 1.0f) ? Color::Green
                       : (qf > 0.0f)  ? Color::Yellow
                       :                Color::Red;
        return hbox({
            text(" LA VIDA MISMA ") | bold | color(Color::RGB(200, 220, 255)),
            text("  tick:" + std::to_string(sim_.tick())),
            text("  alive:" + std::to_string(sim_.alive_count()) + "/" +
                 std::to_string(sim_.config().initial_population)),
            text("  built:" + std::to_string(sim_.total_machines_built())),
            text("  storage:" + ff(sim_.total_storage_food())),
            text("  fact:") | dim,
            text(ff(fh)) | color(fh_color) | bold,
            text("  quota:") | dim,
            text(ff(qf)) | color(qf_color),
            text("  shipped:") | dim,
            text(ff(sim_.total_food_shipped())),
            text("  broken:") | dim,
            text(std::to_string(sim_.total_machines_broken())),
            text("  "),
            text("[" + status + "]") | bold |
                color(running_ ? Color::Green : Color::Yellow),
        });
    }

    Element build_footer() {
        return hbox({
            text(" n:tick r:run p:pause +/-:speed Tab:agent c:center Arrows:pan [/]:zoom H:help q:quit") | dim,
            text("  "),
            text(std::to_string(speed_ms_) + "ms") | dim,
        });
    }

    // ---- Grid viewport ----
    // Renders exactly view_tiles_w()×view_tiles_h() tiles, each at tile_chars_w×tile_chars_h
    // characters (zoom). Arrows pan within the grid; [/] change zoom.
    Element build_grid_view() {
        auto agents = sim_.alive_agents();
        int vw = view_tiles_w();
        int vh = view_tiles_h();
        int th = tile_chars_h();

        Elements rows;
        for (int y = scroll_y_; y < scroll_y_ + vh; y++) {
            // Build the row's cell list once, then repeat th times for vertical zoom.
            for (int r = 0; r < th; r++) {
                Elements cols;
                for (int x = scroll_x_; x < scroll_x_ + vw; x++) {
                    cols.push_back(tile_char(x, y, agents));
                }
                rows.push_back(hbox(std::move(cols)));
            }
        }

        return vbox(std::move(rows)) | border | hcenter;
    }

    // Builds a glyph for one tile, repeated tile_chars_w times so the cell occupies
    // the right horizontal space at the current zoom level.
    Element styled(char glyph, Color fg) {
        return text(std::string(tile_chars_w(), glyph)) | color(fg);
    }
    Element styled_bg(char glyph, Color fg, Color bg) {
        return text(std::string(tile_chars_w(), glyph)) | color(fg) | bgcolor(bg);
    }

    Element tile_char(int x, int y, const std::vector<entt::entity>& agents) {
        const Grid& grid = sim_.grid();

        int ac = 0;
        bool sel = false;
        for (size_t i = 0; i < agents.size(); i++) {
            auto& p = sim_.registry().get<PositionComponent>(agents[i]);
            if (p.x == x && p.y == y) {
                ac++;
                if (i == selected_idx_) sel = true;
            }
        }

        if (sel)    return styled_bg('@', Color::Yellow, Color::RGB(60, 60, 100));
        if (ac > 1) return styled('0' + std::min(ac, 9), Color::Cyan);
        if (ac == 1) return styled('o', Color::Cyan);

        TileType t = grid.at(x, y);
        switch (t) {
            case TileType::Wall:      return styled('#', Color::GrayDark);
            case TileType::Floor:     return styled('.', Color::White);
            case TileType::Machine: {
                auto& d = grid.data_at(x, y);
                if (d.built) return styled('M', Color::Green);
                float pct = d.build_cost > 0 ? d.build_progress / d.build_cost : 0;
                if (pct > 0.5f) return styled('m', Color::RGB(255, 180, 80));
                if (pct > 0.0f) return styled('m', Color::RGB(255, 130, 80));
                return styled('m', Color::GrayLight);
            }
            case TileType::Storage: {
                auto& d = grid.data_at(x, y);
                float tot = d.stored_food + d.stored_raw_food + d.stored_raw_material;
                if (tot > 5.f)   return styled('S', Color::Yellow);
                if (tot > 0.5f)  return styled('S', Color::RGB(200, 200, 100));
                return styled('S', Color::GrayDark);
            }
            case TileType::Entrance:   return styled('<', Color::Green);
            case TileType::Exit:       return styled('>', Color::Green);
            case TileType::OpenSpace:  return styled('_', Color::RGB(40, 70, 50));
            case TileType::EatingZone: {
                auto& d = grid.data_at(x, y);
                if (d.built) return styled('E', Color::RGB(100, 200, 200));
                float pct = d.build_cost > 0 ? d.build_progress / d.build_cost : 0;
                if (pct > 0.5f) return styled('e', Color::RGB(80, 180, 180));
                if (pct > 0.0f) return styled('e', Color::RGB(60, 140, 140));
                return styled('e', Color::GrayLight);
            }
            case TileType::FoodSource: {
                auto& d = grid.data_at(x, y);
                if (d.resource_amount > 1.5f) return styled('f', Color::Green);
                if (d.resource_amount > 0.3f) return styled('f', Color::RGB(180, 200, 80));
                return styled('f', Color::GrayDark);
            }
            case TileType::ScrapPile: {
                auto& d = grid.data_at(x, y);
                if (d.resource_amount > 2.f)  return styled('s', Color::RGB(200, 150, 80));
                if (d.resource_amount > 0.3f) return styled('s', Color::RGB(150, 110, 60));
                return styled('s', Color::GrayDark);
            }
            case TileType::Conveyor: {
                auto& d = grid.data_at(x, y);
                // Unbuilt: dim
                if (!d.built) {
                    float pct = d.build_cost > 0 ? d.build_progress / d.build_cost : 0;
                    if (pct > 0.0f) return styled('+', Color::RGB(100, 100, 100));
                    return styled('+', Color::RGB(60, 60, 60));
                }
                // Broken: red
                if (d.conveyor_condition < 0.2f) return styled('X', Color::Red);
                // Directional arrow, color by condition
                char glyph = '?';
                switch (d.conveyor_dir) {
                    case ConveyorDir::N: glyph = '^'; break;
                    case ConveyorDir::S: glyph = 'v'; break;
                    case ConveyorDir::E: glyph = '>'; break;
                    case ConveyorDir::W: glyph = '<'; break;
                }
                // Has contents: brighter
                if (d.conveyor_contents > 0.01f) {
                    if (d.conveyor_condition > 0.7f) return styled(glyph, Color::RGB(100, 220, 255));
                    if (d.conveyor_condition > 0.3f) return styled(glyph, Color::RGB(220, 220, 100));
                    return styled(glyph, Color::RGB(220, 100, 100));
                }
                // Empty belt
                if (d.conveyor_condition > 0.7f) return styled(glyph, Color::RGB(60, 150, 200));
                if (d.conveyor_condition > 0.3f) return styled(glyph, Color::RGB(180, 180, 60));
                return styled(glyph, Color::RGB(180, 60, 60));
            }
            default: return styled('?', Color::White);
        }
    }

    // Help modal — legend of map symbols, shown over the main UI on H.
    Element build_help_modal() {
        auto row = [](std::string sym, Color c, std::string desc) {
            return hbox({
                text(" "),
                text(sym) | color(c) | bold,
                text("  "),
                text(desc) | dim,
            });
        };
        return vbox({
            text(" LEGEND — press any key to close ") | bold | color(Color::Yellow),
            text(""),
            text("  Map symbols") | underlined,
            row("@", Color::Yellow,                  "selected agent (Tab cycles)"),
            row("o", Color::Cyan,                    "agent"),
            row("0-9", Color::Cyan,                  "stack of N agents on the same tile"),
            row("#", Color::GrayDark,                "wall (impassable)"),
            row(".", Color::White,                   "floor"),
            row("_", Color::RGB(40, 70, 50),         "open space (creative / social area)"),
            row("<", Color::Green,                   "entrance"),
            row(">", Color::Green,                   "exit"),
            row("f", Color::Green,                   "food source (wild, regenerates)"),
            row("s", Color::RGB(200, 150, 80),       "scrap pile (raw material)"),
            row("m", Color::RGB(255, 130, 80),       "unbuilt machine (orange = in progress)"),
            row("M", Color::Green,                   "built machine (operational)"),
            row("S", Color::Yellow,                  "storage (yellow = stocked, gray = empty)"),
            row("e", Color::RGB(60, 140, 140),       "eating zone frame (under construction)"),
            row("E", Color::RGB(100, 200, 200),      "built eating zone (safe to eat — no penalty)"),
            text(""),
            text("  Controls") | underlined,
            row("n",      Color::White, "advance one tick"),
            row("r / p",  Color::White, "run / pause"),
            row("+/-",    Color::White, "speed up / slow down"),
            row("Tab",    Color::White, "select next agent"),
            row("c",      Color::White, "center viewport on selected agent"),
            row("Arrows", Color::White, "pan viewport"),
            row("[ / ]",  Color::White, "zoom out / in"),
            row("H",      Color::White, "this legend"),
            row("q / Esc", Color::White, "quit"),
        }) | border | bgcolor(Color::RGB(20, 20, 30)) | hcenter | vcenter;
    }

    // ---- Event log ----
    Element build_log_panel() {
        const auto& log = sim_.log();
        Elements lines;
        // Show last 8 entries (newest at bottom)
        int start = std::max(0, (int)log.size() - 8);
        for (int i = start; i < (int)log.size(); i++) {
            const auto& entry = log[i];
            Color c;
            if (entry.text.find("DIED") != std::string::npos ||
                entry.text.find("BREAKDOWN") != std::string::npos) {
                c = Color::Red;
            } else if (entry.text.find("BUILT") != std::string::npos) {
                c = Color::Green;
            } else if (entry.text.find("worked") != std::string::npos) {
                c = Color::RGB(200, 200, 100);
            } else if (entry.text.find("salvaged") != std::string::npos) {
                c = Color::RGB(200, 150, 80);
            } else if (entry.text.find("gathered") != std::string::npos) {
                c = Color::RGB(100, 200, 100);
            } else {
                c = Color::White;
            }

            std::string tick_str = std::to_string(entry.tick);
            while (tick_str.size() < 5) tick_str = " " + tick_str;

            lines.push_back(hbox({
                text("[" + tick_str + "]") | dim | color(Color::GrayLight),
                text(" A" + std::to_string(entry.agent_id) + " ") | color(Color::Cyan),
                text(entry.text) | color(c),
            }));
        }
        if (lines.empty()) {
            lines.push_back(text("  (no events yet)") | dim);
        }
        return vbox({
            text(" EVENT LOG") | bold | underlined,
            vbox(std::move(lines)) | flex,
        });
    }

    // ---- Side panel ----
    Element build_side_panel() {
        auto agents = sim_.alive_agents();
        if (agents.empty())
            return vbox({text("  No alive agents.")}) | border;

        if (selected_idx_ >= agents.size()) selected_idx_ = 0;
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

        static const char* aname[] = {
            "GATHER","BUILD","WORK","EAT","REST","SOCIALIZE","CREATE","EXPLORE","GET_FOOD","MAINTAIN","DSMNTL","SABOTAGE","IDLE"
        };

        // Death-countdown lines: only show when the agent is actually in danger.
        // Option (c) from the design discussion — explicit countdowns, not a derived health number.
        const auto& cfg = sim_.config();
        Elements danger;
        if (nd.hunger >= 1.0f) {
            int left = cfg.starvation_ticks - ag.ticks_at_max_hunger;
            danger.push_back(hbox({
                text(" STARVING ") | color(Color::Red) | bold,
                text("dies in " + std::to_string(std::max(0, left)) + " tk") | color(Color::Red),
            }));
        }
        if (nd.rest >= 1.0f) {
            int left = cfg.exhaustion_ticks - ag.ticks_at_max_rest;
            danger.push_back(hbox({
                text(" EXHAUSTED ") | color(Color::RGB(255, 120, 0)) | bold,
                text("dies in " + std::to_string(std::max(0, left)) + " tk") | color(Color::RGB(255, 120, 0)),
            }));
        }
        if (st.value >= 0.80f) {
            danger.push_back(hbox({
                text(" NEAR BREAKDOWN ") | color(Color::Magenta) | bold,
                text(stress_state_name(st.state)) | color(Color::Magenta),
                text(" stress=" + ff(st.value)) | color(Color::Magenta),
            }));
        }
        if (st.state == StressState::REDEEMED) {
            danger.push_back(hbox({
                text(" REDEEMED ") | color(Color::Green) | bold,
                text("collectivist martyr") | color(Color::Green),
            }));
        }
        if (danger.empty()) {
            danger.push_back(text(" healthy") | dim);
        }

        return vbox({
            text("Agent[" + std::to_string(ag.id) + "]") | bold | color(Color::Cyan),
            hbox({text("action: "), text(aname[(int)ac.current]) | bold | color(Color::White)}),
            text("pos: (" + std::to_string(po.x) + "," + std::to_string(po.y) + ")"),
            separator(),
            text("HEALTH") | bold | underlined,
            vbox(std::move(danger)),
            separator(),
            text("NEEDS") | bold | underlined,
            need_bar("Hunger",  nd.hunger,     Color::Red),
            need_bar("Rest",    nd.rest,       Color::Blue),
            need_bar("Social",  nd.social,     Color::Cyan),
            need_bar("Expr",    nd.expression, Color::Magenta),
            need_bar("Purpose", nd.purpose,    Color::Yellow),
            need_bar("Meaning", nd.meaning,    Color::RGB(180, 120, 255)),
            need_bar("Stress",  st.value,      Color::RGB(255, 80, 80)),
            hbox({text(" trauma: "), text(ff(st.trauma)) | color(Color::RGB(200, 100, 255))}),
            hbox({text(" state:  "), text(stress_state_name(st.state)) | (st.state == StressState::REDEEMED ? color(Color::Green) : st.state == StressState::BROKEN ? color(Color::Red) : dim)}),
            separator(),
            text("INVENTORY") | bold | underlined,
            text(" raw_food: " + ff(iv.raw_food)),
            text(" raw_mat:   " + ff(iv.raw_material)),
            text(" food:      " + ff(iv.food)),
            separator(),
            text("PERSONALITY") | bold | underlined,
            pg("comp", ps.compliance),
            pg("lazy", ps.laziness),
            pg("art",  ps.artistry),
            pg("greg", ps.gregariousness),
            pg("res",  ps.resilience),
            pg("cur",  ps.curiosity),
            separator(),
            text("SOCIAL") | bold | underlined,
            pg("mood",      soc.mood),
            pg("influence", soc.influence),
            pg("energy",    soc.social_energy),
            separator(),
            text("OPINIONS") | bold | underlined,
            pg("ethic",  op.values[0]),
            pg("risk",   op.values[1]),
            pg("trad",   op.values[2]),
            pg("solid",  op.values[3]),
            separator(),
            text("UTILITY") | bold | underlined,
            ur("GATHER",   ac.last_utility_gather),
            ur("BUILD",    ac.last_utility_build),
            ur("WORK",     ac.last_utility_work),
            ur("EAT",      ac.last_utility_eat),
            ur("REST",     ac.last_utility_rest),
            ur("SOCIAL",   ac.last_utility_socialize),
            ur("CREATE",   ac.last_utility_create),
            ur("EXPLORE",  ac.last_utility_explore),
            ur("GETFOOD",  ac.last_utility_get_food),
        }) | border | size(WIDTH, GREATER_THAN, 34) | flex;
    }

    // ---- Micro-helpers ----
    static Element need_bar(std::string label, float v, Color c) {
        int n = std::clamp((int)(v * 10), 0, 10);
        std::string bar;
        for (int i = 0; i < 10; i++) bar += (i < n) ? '#' : '-';
        // Pad label to 8 chars
        while (label.size() < 8) label += ' ';
        return hbox({text(label), text(bar) | color(c), text(" " + ff(v)) | dim});
    }

    static Element pg(std::string label, float v) {
        while (label.size() < 5) label += ' ';
        return hbox({text(label + " "), gauge(v) | color(Color::RGB(150, 150, 200)) | flex, text(" " + ff(v) + " ") | dim});
    }

    static Element ur(std::string label, float v) {
        while (label.size() < 7) label += ' ';
        int n = std::clamp((int)(v * 100), 0, 20);
        std::string bar;
        for (int i = 0; i < 20; i++) bar += (i < n) ? '#' : ' ';
        return hbox({text(label) | dim, text(bar) | dim, text(" " + ff(v))});
    }

    static std::string ff(float v) {
        char b[16];
        std::snprintf(b, sizeof(b), "%.2f", v);
        return b;
    }
};
