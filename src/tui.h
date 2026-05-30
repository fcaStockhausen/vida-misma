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
          running_(false), speed_ms_(200) {
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
    bool running_ = false;
    bool quit_ = false;
    int speed_ms_;

    bool handle_event(Event e, ScreenInteractive& screen) {
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
        if (e == Event::ArrowLeft)    { scroll_x_ = std::max(0, scroll_x_ - 5); return false; }
        if (e == Event::ArrowRight)   { scroll_x_ += 5; return false; }
        if (e == Event::ArrowUp)      { scroll_y_ = std::max(0, scroll_y_ - 3); return false; }
        if (e == Event::ArrowDown)    { scroll_y_ += 3; return false; }
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
        scroll_x_ = std::max(0, pos.x - 20);
        scroll_y_ = std::max(0, pos.y - 10);
    }

    // ---- Main layout ----
    Element build_ui() {
        return vbox({
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
    }

    Element build_header() {
        std::string status = running_ ? "RUNNING" : "PAUSED";
        return hbox({
            text(" LA VIDA MISMA ") | bold | color(Color::RGB(200, 220, 255)),
            text("  tick:" + std::to_string(sim_.tick())),
            text("  alive:" + std::to_string(sim_.alive_count()) + "/" +
                 std::to_string(sim_.config().initial_population)),
            text("  built:" + std::to_string(sim_.total_machines_built())),
            text("  storage:" + ff(sim_.total_storage_food())),
            text("  "),
            text("[" + status + "]") | bold |
                color(running_ ? Color::Green : Color::Yellow),
        });
    }

    Element build_footer() {
        return hbox({
            text(" n:tick r:run p:pause +/-:speed Tab:agent c:center Arrows:scroll q:quit") | dim,
            text("  "),
            text(std::to_string(speed_ms_) + "ms") | dim,
        });
    }

    // ---- Grid viewport ----
    Element build_grid_view() {
        const Grid& grid = sim_.grid();
        auto agents = sim_.alive_agents();

        int gw = grid.width();
        int gh = grid.height();
        int vw = std::clamp(gw - scroll_x_, 1, 60);
        int vh = std::clamp(gh - scroll_y_, 1, 20);

        Elements rows;
        for (int y = scroll_y_; y < scroll_y_ + vh; y++) {
            Elements cols;
            for (int x = scroll_x_; x < scroll_x_ + vw; x++) {
                cols.push_back(tile_char(x, y, agents));
            }
            rows.push_back(hbox(std::move(cols)));
        }

        return vbox(std::move(rows)) | border | hcenter;
    }

    Element tile_char(int x, int y, const std::vector<entt::entity>& agents) {
        const Grid& grid = sim_.grid();

        // Agent check
        int ac = 0;
        bool sel = false;
        for (size_t i = 0; i < agents.size(); i++) {
            auto& p = sim_.registry().get<PositionComponent>(agents[i]);
            if (p.x == x && p.y == y) {
                ac++;
                if (i == selected_idx_) sel = true;
            }
        }

        if (sel)
            return text("@") | color(Color::Yellow) | bgcolor(Color::RGB(60, 60, 100));
        if (ac > 1)
            return text(std::to_string(std::min(ac, 9))) | color(Color::Cyan);
        if (ac == 1)
            return text("o") | color(Color::Cyan);

        TileType t = grid.at(x, y);
        switch (t) {
            case TileType::Wall:      return text("#") | color(Color::GrayDark);
            case TileType::Floor:     return text(".");
            case TileType::Machine: {
                auto& d = grid.data_at(x, y);
                if (d.built) return text("M") | color(Color::Green);
                float pct = d.build_cost > 0 ? d.build_progress / d.build_cost : 0;
                if (pct > 0.5f) return text("m") | color(Color::RGB(255, 180, 80));
                if (pct > 0.0f) return text("m") | color(Color::RGB(255, 130, 80));
                return text("m") | color(Color::GrayLight);
            }
            case TileType::Storage: {
                auto& d = grid.data_at(x, y);
                float tot = d.stored_food + d.stored_raw_food + d.stored_raw_material;
                if (tot > 5.f) return text("S") | color(Color::Yellow);
                if (tot > 0.5f) return text("S") | color(Color::RGB(200, 200, 100));
                return text("S") | color(Color::GrayDark);
            }
            case TileType::Entrance:   return text("<") | color(Color::Green);
            case TileType::Exit:       return text(">") | color(Color::Green);
            case TileType::OpenSpace:  return text("_") | color(Color::RGB(40, 70, 50));
            case TileType::FoodSource: {
                auto& d = grid.data_at(x, y);
                if (d.resource_amount > 1.5f) return text("f") | color(Color::Green);
                if (d.resource_amount > 0.3f) return text("f") | color(Color::RGB(180, 200, 80));
                return text("f") | color(Color::GrayDark);
            }
            case TileType::ScrapPile: {
                auto& d = grid.data_at(x, y);
                if (d.resource_amount > 2.f) return text("s") | color(Color::RGB(200, 150, 80));
                if (d.resource_amount > 0.3f) return text("s") | color(Color::RGB(150, 110, 60));
                return text("s") | color(Color::GrayDark);
            }
            default: return text("?");
        }
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

        static const char* aname[] = {
            "GATHER","BUILD","WORK","EAT","REST","SOCIALIZE","CREATE","EXPLORE","IDLE"
        };

        return vbox({
            text("Agent[" + std::to_string(ag.id) + "]") | bold | color(Color::Cyan),
            hbox({text("action: "), text(aname[(int)ac.current]) | bold | color(Color::White)}),
            text("pos: (" + std::to_string(po.x) + "," + std::to_string(po.y) + ")"),
            separator(),
            text("NEEDS") | bold | underlined,
            need_bar("Hunger",  nd.hunger,     Color::Red),
            need_bar("Rest",    nd.rest,       Color::Blue),
            need_bar("Social",  nd.social,     Color::Cyan),
            need_bar("Expr",    nd.expression, Color::Magenta),
            need_bar("Purpose", nd.purpose,    Color::Yellow),
            need_bar("Stress",  st.value,      Color::RGB(255, 80, 80)),
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
            text("UTILITY") | bold | underlined,
            ur("GATHER", ac.last_utility_gather),
            ur("BUILD",  ac.last_utility_build),
            ur("WORK",   ac.last_utility_work),
            ur("EAT",    ac.last_utility_eat),
            ur("REST",   ac.last_utility_rest),
            ur("SOCIAL", ac.last_utility_socialize),
            ur("CREATE", ac.last_utility_create),
            ur("EXPLORE",ac.last_utility_explore),
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
