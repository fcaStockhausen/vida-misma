#pragma once

#include <SDL2/SDL.h>
#include <vector>
#include <cstdio>

// ============================================================
// Procedural isometric sprite atlas
// ============================================================
// Each sprite is SW x SH pixels, stored in a horizontal atlas strip.
// Sprites are generated at runtime with pixel-art detail for each
// tile type + state variant.
//
// Atlas layout (enum index → sprite slot):
//   0  Floor            1  Wall             2  OpenSpace
//   3  Machine built    4  Machine unbuilt  5  Machine building (>50%)
//   6  Machine building (<50%)              7  Storage full
//   8  Storage partial  9  Storage empty   10  Entrance
//  11  Exit            12  ScrapPile rich  13  ScrapPile depleted
//  14  Conveyor N      15  Conveyor S      16  Conveyor E
//  17  Conveyor W      18  Conveyor unbuilt
//  19  EatingZone built                   20  EatingZone unbuilt
//  21  FoodSource lush 22  FoodSource sparse              23  HiddenSpace
//  24  Agent Normal    25  Agent Dissociated
//  26  Agent Euphoric  27  Agent Broken    28  Agent Redeemed
//  29  Agent Selected  30  Unknown/fallback

enum class SpriteID : int {
    Floor = 0,
    Wall,
    OpenSpace,
    MachineBuilt,
    MachineUnbuilt,
    MachineBuildHigh,
    MachineBuildLow,
    StorageFull,
    StoragePartial,
    StorageEmpty,
    Entrance,
    Exit,
    ScrapRich,
    ScrapDepleted,
    ConveyorN,
    ConveyorS,
    ConveyorE,
    ConveyorW,
    ConveyorUnbuilt,
    EatingZoneBuilt,
    EatingZoneUnbuilt,
    FoodSourceLush,
    FoodSourceSparse,
    HiddenSpace,
    AgentNormal,
    AgentDissociated,
    AgentEuphoric,
    AgentBroken,
    AgentRedeemed,
    AgentSelected,
    Fallback,
    COUNT
};

class SpriteAtlas {
public:
    static constexpr int SW = 64;
    static constexpr int SH = 40;

    SpriteAtlas() = default;

    void init(SDL_Renderer* renderer) {
        renderer_ = renderer;
        generate_all();
        atlas_tex_ = SDL_CreateTextureFromSurface(renderer_, atlas_surf_);
        SDL_SetTextureBlendMode(atlas_tex_, SDL_BLENDMODE_BLEND);
    }

    ~SpriteAtlas() {
        if (atlas_tex_) SDL_DestroyTexture(atlas_tex_);
        if (atlas_surf_) SDL_FreeSurface(atlas_surf_);
    }

    void draw(SpriteID id, int screen_x, int screen_y, float zoom) const {
        int idx = (int)id;
        if (idx < 0 || idx >= (int)SpriteID::COUNT) idx = (int)SpriteID::Fallback;

        int w = (int)(SW * zoom);
        int h = (int)(SH * zoom);
        SDL_Rect src = {idx * SW, 0, SW, SH};
        SDL_Rect dst = {screen_x - w / 2, screen_y - h / 2 + (int)(4 * zoom), w, h};
        SDL_RenderCopy(renderer_, atlas_tex_, &src, &dst);
    }

    void draw_agent(SpriteID id, int screen_x, int screen_y, float zoom) const {
        int idx = (int)id;
        if (idx < 0 || idx >= (int)SpriteID::COUNT) idx = (int)SpriteID::AgentNormal;

        int w = (int)(SW * zoom);
        int h = (int)(SH * zoom);
        SDL_Rect src = {idx * SW, 0, SW, SH};
        SDL_Rect dst = {screen_x - w / 2, screen_y - (int)(h * 0.7), w, h};
        SDL_RenderCopy(renderer_, atlas_tex_, &src, &dst);
    }

private:
    SDL_Renderer* renderer_ = nullptr;
    SDL_Surface* atlas_surf_ = nullptr;
    SDL_Texture* atlas_tex_ = nullptr;

    // Pixel helpers
    struct PX { uint8_t r, g, b, a; };

    void put(int sprite_idx, int x, int y, PX c) {
        if (x < 0 || x >= SW || y < 0 || y >= SH) return;
        uint8_t* p = (uint8_t*)atlas_surf_->pixels
            + (y * atlas_surf_->pitch)
            + ((sprite_idx * SW + x) * 4);
        p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
    }

    void fill_sprite(int idx, PX c) {
        for (int y = 0; y < SH; y++)
            for (int x = 0; x < SW; x++)
                put(idx, x, y, c);
    }

    // Draw an isometric diamond at sprite-local coords (cx, cy is center)
    void iso_diamond(int idx, int cx, int cy, int hw, int hh, PX top, PX left, PX right, PX edge) {
        for (int row = 0; row < hh * 2; row++) {
            int ry = cy - hh + row;
            float t = (float)row / (hh * 2 - 1);
            int x_extent;
            if (t < 0.5f) x_extent = (int)(hw * t * 2);
            else x_extent = (int)(hw * (1.0f - t) * 2);
            x_extent = std::max(1, x_extent);
            for (int dx = -x_extent; dx <= x_extent; dx++) {
                PX c = top;
                if (row < hh) {
                    // top half → left/right faces visible at edges
                    if (dx == -x_extent) c = left;
                    else if (dx == x_extent) c = right;
                } else {
                    // bottom half → left/right faces
                    if (dx == -x_extent || dx == -x_extent + 1) c = left;
                    else if (dx == x_extent || dx == x_extent - 1) c = right;
                }
                put(idx, cx + dx, ry, c);
            }
        }
        // Edge outline
        for (int i = 0; i < hh * 2; i++) {
            int ry = cy - hh + i;
            float t = (float)i / (hh * 2 - 1);
            int xe;
            if (t < 0.5f) xe = (int)(hw * t * 2);
            else xe = (int)(hw * (1.0f - t) * 2);
            xe = std::max(1, xe);
            put(idx, cx - xe, ry, edge);
            put(idx, cx + xe, ry, edge);
        }
        put(idx, cx, cy - hh, edge);
        put(idx, cx, cy + hh - 1, edge);
    }

    // Raised block: top face + left face + right face
    void iso_block(int idx, int cx, int cy, int hw, int hh, int height,
                   PX top, PX left, PX right, PX edge)
    {
        // Left face
        for (int h = 0; h < height; h++) {
            int y = cy + h;
            float t_top = 0.5f;
            float t_bot = 1.0f;
            int x_top = (int)(hw * t_top * 2);
            int x_bot = hw;
            for (int col = 0; col <= x_bot; col++) {
                float frac = (float)col / x_bot;
                int x_off = (int)(x_top + (x_bot - x_top) * (1.0f - frac));
                if (h == 0) {
                    put(idx, cx - x_off, y, edge);
                } else {
                    put(idx, cx - x_off, y, left);
                }
            }
            put(idx, cx, y, edge);
        }
        // Right face
        for (int h = 0; h < height; h++) {
            int y = cy + h;
            float t_top = 0.5f;
            float t_bot = 1.0f;
            int x_top = (int)(hw * t_top * 2);
            int x_bot = hw;
            for (int col = 0; col <= x_bot; col++) {
                float frac = (float)col / x_bot;
                int x_off = (int)(x_top + (x_bot - x_top) * frac);
                if (h == 0) {
                    put(idx, cx + x_off, y, edge);
                } else {
                    put(idx, cx + x_off, y, right);
                }
            }
            put(idx, cx, y, edge);
        }
        // Top face
        iso_diamond(idx, cx, cy, hw, hh, top, top, top, edge);
    }

    void generate_all() {
        int count = (int)SpriteID::COUNT;
        atlas_surf_ = SDL_CreateRGBSurfaceWithFormat(0, count * SW, SH, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_LockSurface(atlas_surf_);

        int cx = SW / 2;
        int cy = SH / 2 - 2;
        int hw = 28;
        int hh = 14;
        int blk_h = 6;

        PX TRANSP = {0, 0, 0, 0};

        // ---- Floor (0) ----
        fill_sprite((int)SpriteID::Floor, TRANSP);
        iso_diamond((int)SpriteID::Floor, cx, cy, hw, hh,
            {90, 90, 100, 255}, {70, 70, 80, 255}, {60, 60, 70, 255}, {50, 50, 60, 255});
        // Small dots for texture
        put((int)SpriteID::Floor, cx - 8, cy - 2, {80, 80, 90, 255});
        put((int)SpriteID::Floor, cx + 6, cy + 3, {85, 85, 95, 255});
        put((int)SpriteID::Floor, cx - 2, cy + 6, {80, 80, 90, 255});

        // ---- Wall (1) ----
        fill_sprite((int)SpriteID::Wall, TRANSP);
        iso_block((int)SpriteID::Wall, cx, cy, hw, hh, blk_h,
            {75, 75, 95, 255}, {55, 55, 75, 255}, {45, 45, 65, 255}, {35, 35, 50, 255});
        // Brick lines on top face
        for (int x = cx - 12; x <= cx + 12; x += 3)
            put((int)SpriteID::Wall, x, cy - 4, {65, 65, 85, 255});
        for (int x = cx - 10; x <= cx + 10; x += 3)
            put((int)SpriteID::Wall, x, cy, {65, 65, 85, 255});

        // ---- OpenSpace (2) ----
        fill_sprite((int)SpriteID::OpenSpace, TRANSP);
        iso_diamond((int)SpriteID::OpenSpace, cx, cy, hw, hh,
            {35, 75, 45, 255}, {25, 60, 35, 255}, {20, 50, 30, 255}, {15, 40, 25, 255});
        // Little grass tufts
        put((int)SpriteID::OpenSpace, cx - 10, cy - 3, {50, 110, 50, 255});
        put((int)SpriteID::OpenSpace, cx - 9, cy - 4, {60, 130, 60, 255});
        put((int)SpriteID::OpenSpace, cx + 8, cy + 2, {50, 110, 50, 255});
        put((int)SpriteID::OpenSpace, cx + 9, cy + 1, {60, 130, 60, 255});
        put((int)SpriteID::OpenSpace, cx, cy + 7, {50, 110, 50, 255});
        put((int)SpriteID::OpenSpace, cx + 1, cy + 6, {60, 130, 60, 255});

        // ---- Machine Built (3) ----
        fill_sprite((int)SpriteID::MachineBuilt, TRANSP);
        iso_block((int)SpriteID::MachineBuilt, cx, cy, hw, hh, blk_h,
            {90, 190, 90, 255}, {60, 130, 60, 255}, {50, 110, 50, 255}, {40, 80, 40, 255});
        // Gear symbol on top face
        for (int a = 0; a < 360; a += 45) {
            int gx = cx + (int)(6 * cos(a * 3.14159f / 180.0f));
            int gy = cy + (int)(3 * sin(a * 3.14159f / 180.0f));
            put((int)SpriteID::MachineBuilt, gx, gy - 2, {40, 100, 40, 255});
        }
        put((int)SpriteID::MachineBuilt, cx, cy - 2, {40, 100, 40, 255});
        // Chimney
        for (int dy = -3; dy <= 0; dy++)
            for (int dx = -2; dx <= 2; dx++)
                put((int)SpriteID::MachineBuilt, cx + 14 + dx, cy + dy - 3, {80, 80, 80, 255});

        // ---- Machine Unbuilt (4) ----
        fill_sprite((int)SpriteID::MachineUnbuilt, TRANSP);
        iso_block((int)SpriteID::MachineUnbuilt, cx, cy, hw, hh, 3,
            {110, 110, 115, 255}, {80, 80, 85, 255}, {70, 70, 75, 255}, {50, 50, 55, 255});
        // X mark on top
        for (int i = -4; i <= 4; i++) {
            put((int)SpriteID::MachineUnbuilt, cx + i, cy - 2 + i/2, {140, 60, 60, 255});
            put((int)SpriteID::MachineUnbuilt, cx + i, cy - 2 - i/2, {140, 60, 60, 255});
        }

        // ---- Machine Building >50% (5) ----
        fill_sprite((int)SpriteID::MachineBuildHigh, TRANSP);
        iso_block((int)SpriteID::MachineBuildHigh, cx, cy, hw, hh, 5,
            {240, 170, 70, 255}, {180, 120, 40, 255}, {150, 100, 30, 255}, {100, 70, 20, 255});
        // Progress bar on top
        for (int x = cx - 10; x <= cx + 10; x++)
            put((int)SpriteID::MachineBuildHigh, x, cy - 1, {60, 60, 60, 255});
        for (int x = cx - 10; x <= cx; x++)
            put((int)SpriteID::MachineBuildHigh, x, cy - 1, {255, 255, 100, 255});

        // ---- Machine Building <50% (6) ----
        fill_sprite((int)SpriteID::MachineBuildLow, TRANSP);
        iso_block((int)SpriteID::MachineBuildLow, cx, cy, hw, hh, 4,
            {240, 120, 70, 255}, {180, 80, 40, 255}, {150, 60, 30, 255}, {100, 40, 20, 255});
        for (int x = cx - 10; x <= cx + 10; x++)
            put((int)SpriteID::MachineBuildLow, x, cy - 1, {60, 60, 60, 255});
        for (int x = cx - 10; x <= cx - 5; x++)
            put((int)SpriteID::MachineBuildLow, x, cy - 1, {255, 200, 80, 255});

        // ---- Storage Full (7) ----
        fill_sprite((int)SpriteID::StorageFull, TRANSP);
        iso_block((int)SpriteID::StorageFull, cx, cy, hw, hh, blk_h,
            {200, 190, 90, 255}, {150, 140, 60, 255}, {130, 120, 50, 255}, {90, 80, 35, 255});
        // Crate cross pattern
        for (int x = cx - 8; x <= cx + 8; x++)
            put((int)SpriteID::StorageFull, x, cy - 2, {160, 150, 70, 255});
        put((int)SpriteID::StorageFull, cx, cy - 6, {160, 150, 70, 255});
        put((int)SpriteID::StorageFull, cx, cy + 2, {160, 150, 70, 255});
        // Overflow indicators
        put((int)SpriteID::StorageFull, cx - 6, cy - 5, {220, 200, 60, 255});
        put((int)SpriteID::StorageFull, cx + 6, cy - 5, {220, 200, 60, 255});

        // ---- Storage Partial (8) ----
        fill_sprite((int)SpriteID::StoragePartial, TRANSP);
        iso_block((int)SpriteID::StoragePartial, cx, cy, hw, hh, blk_h,
            {170, 170, 75, 255}, {130, 130, 50, 255}, {110, 110, 45, 255}, {80, 80, 30, 255});
        for (int x = cx - 8; x <= cx + 8; x++)
            put((int)SpriteID::StoragePartial, x, cy - 2, {140, 140, 60, 255});

        // ---- Storage Empty (9) ----
        fill_sprite((int)SpriteID::StorageEmpty, TRANSP);
        iso_block((int)SpriteID::StorageEmpty, cx, cy, hw, hh, blk_h,
            {95, 95, 75, 255}, {70, 70, 55, 255}, {60, 60, 45, 255}, {45, 45, 35, 255});

        // ---- Entrance (10) ----
        fill_sprite((int)SpriteID::Entrance, TRANSP);
        iso_diamond((int)SpriteID::Entrance, cx, cy, hw, hh,
            {80, 200, 80, 255}, {50, 150, 50, 255}, {40, 120, 40, 255}, {30, 90, 30, 255});
        // Arrow pointing inward
        put((int)SpriteID::Entrance, cx, cy - 4, {200, 255, 200, 255});
        put((int)SpriteID::Entrance, cx - 1, cy - 3, {200, 255, 200, 255});
        put((int)SpriteID::Entrance, cx + 1, cy - 3, {200, 255, 200, 255});
        put((int)SpriteID::Entrance, cx - 2, cy - 2, {200, 255, 200, 255});
        put((int)SpriteID::Entrance, cx + 2, cy - 2, {200, 255, 200, 255});
        for (int dy = -1; dy <= 4; dy++)
            put((int)SpriteID::Entrance, cx, cy + dy, {200, 255, 200, 255});

        // ---- Exit (11) ----
        fill_sprite((int)SpriteID::Exit, TRANSP);
        iso_diamond((int)SpriteID::Exit, cx, cy, hw, hh,
            {80, 200, 80, 255}, {50, 150, 50, 255}, {40, 120, 40, 255}, {30, 90, 30, 255});
        // Arrow pointing outward
        put((int)SpriteID::Exit, cx, cy + 4, {200, 255, 200, 255});
        put((int)SpriteID::Exit, cx - 1, cy + 3, {200, 255, 200, 255});
        put((int)SpriteID::Exit, cx + 1, cy + 3, {200, 255, 200, 255});
        put((int)SpriteID::Exit, cx - 2, cy + 2, {200, 255, 200, 255});
        put((int)SpriteID::Exit, cx + 2, cy + 2, {200, 255, 200, 255});
        for (int dy = -4; dy <= 1; dy++)
            put((int)SpriteID::Exit, cx, cy + dy, {200, 255, 200, 255});

        // ---- ScrapPile Rich (12) ----
        fill_sprite((int)SpriteID::ScrapRich, TRANSP);
        iso_block((int)SpriteID::ScrapRich, cx, cy, hw - 2, hh - 1, 5,
            {180, 140, 75, 255}, {140, 100, 50, 255}, {120, 80, 40, 255}, {80, 55, 25, 255});
        // Sparkly bits
        put((int)SpriteID::ScrapRich, cx - 5, cy - 3, {230, 200, 100, 255});
        put((int)SpriteID::ScrapRich, cx + 4, cy - 1, {210, 180, 90, 255});
        put((int)SpriteID::ScrapRich, cx - 2, cy + 1, {220, 190, 95, 255});
        put((int)SpriteID::ScrapRich, cx + 7, cy - 4, {240, 210, 110, 255});
        put((int)SpriteID::ScrapRich, cx - 8, cy + 2, {200, 170, 85, 255});

        // ---- ScrapPile Depleted (13) ----
        fill_sprite((int)SpriteID::ScrapDepleted, TRANSP);
        iso_block((int)SpriteID::ScrapDepleted, cx, cy, hw - 2, hh - 1, 3,
            {95, 75, 45, 255}, {70, 55, 35, 255}, {60, 45, 30, 255}, {40, 30, 20, 255});

        // ---- Conveyor N (14) ----
        fill_sprite((int)SpriteID::ConveyorN, TRANSP);
        iso_diamond((int)SpriteID::ConveyorN, cx, cy, hw, hh,
            {50, 140, 195, 255}, {35, 100, 145, 255}, {30, 85, 125, 255}, {20, 60, 90, 255});
        // Arrow pointing up (north)
        for (int dy = -5; dy <= 5; dy++)
            put((int)SpriteID::ConveyorN, cx, cy + dy, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorN, cx - 1, cy - 4, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorN, cx + 1, cy - 4, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorN, cx - 2, cy - 3, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorN, cx + 2, cy - 3, {140, 210, 255, 255});

        // ---- Conveyor S (15) ----
        fill_sprite((int)SpriteID::ConveyorS, TRANSP);
        iso_diamond((int)SpriteID::ConveyorS, cx, cy, hw, hh,
            {50, 140, 195, 255}, {35, 100, 145, 255}, {30, 85, 125, 255}, {20, 60, 90, 255});
        for (int dy = -5; dy <= 5; dy++)
            put((int)SpriteID::ConveyorS, cx, cy + dy, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorS, cx - 1, cy + 4, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorS, cx + 1, cy + 4, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorS, cx - 2, cy + 3, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorS, cx + 2, cy + 3, {140, 210, 255, 255});

        // ---- Conveyor E (16) ----
        fill_sprite((int)SpriteID::ConveyorE, TRANSP);
        iso_diamond((int)SpriteID::ConveyorE, cx, cy, hw, hh,
            {50, 140, 195, 255}, {35, 100, 145, 255}, {30, 85, 125, 255}, {20, 60, 90, 255});
        for (int dx = -5; dx <= 5; dx++)
            put((int)SpriteID::ConveyorE, cx + dx, cy, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorE, cx + 4, cy - 1, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorE, cx + 4, cy + 1, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorE, cx + 3, cy - 2, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorE, cx + 3, cy + 2, {140, 210, 255, 255});

        // ---- Conveyor W (17) ----
        fill_sprite((int)SpriteID::ConveyorW, TRANSP);
        iso_diamond((int)SpriteID::ConveyorW, cx, cy, hw, hh,
            {50, 140, 195, 255}, {35, 100, 145, 255}, {30, 85, 125, 255}, {20, 60, 90, 255});
        for (int dx = -5; dx <= 5; dx++)
            put((int)SpriteID::ConveyorW, cx + dx, cy, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorW, cx - 4, cy - 1, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorW, cx - 4, cy + 1, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorW, cx - 3, cy - 2, {140, 210, 255, 255});
        put((int)SpriteID::ConveyorW, cx - 3, cy + 2, {140, 210, 255, 255});

        // ---- Conveyor Unbuilt (18) ----
        fill_sprite((int)SpriteID::ConveyorUnbuilt, TRANSP);
        iso_diamond((int)SpriteID::ConveyorUnbuilt, cx, cy, hw, hh,
            {55, 55, 60, 255}, {40, 40, 45, 255}, {35, 35, 40, 255}, {25, 25, 30, 255});

        // ---- EatingZone Built (19) ----
        fill_sprite((int)SpriteID::EatingZoneBuilt, TRANSP);
        iso_diamond((int)SpriteID::EatingZoneBuilt, cx, cy, hw, hh,
            {90, 195, 195, 255}, {60, 145, 145, 255}, {50, 125, 125, 255}, {35, 90, 90, 255});
        // Plate/circle
        for (int a = 0; a < 360; a += 30) {
            int px = cx + (int)(4 * cos(a * 3.14159f / 180.0f));
            int py = cy + (int)(2 * sin(a * 3.14159f / 180.0f));
            put((int)SpriteID::EatingZoneBuilt, px, py, {200, 240, 240, 255});
        }
        put((int)SpriteID::EatingZoneBuilt, cx, cy, {220, 255, 255, 255});

        // ---- EatingZone Unbuilt (20) ----
        fill_sprite((int)SpriteID::EatingZoneUnbuilt, TRANSP);
        iso_diamond((int)SpriteID::EatingZoneUnbuilt, cx, cy, hw, hh,
            {75, 95, 95, 255}, {55, 70, 70, 255}, {45, 60, 60, 255}, {35, 45, 45, 255});

        // ---- FoodSource Lush (21) ----
        fill_sprite((int)SpriteID::FoodSourceLush, TRANSP);
        iso_block((int)SpriteID::FoodSourceLush, cx, cy, hw - 4, hh - 2, 4,
            {90, 195, 70, 255}, {65, 145, 45, 255}, {55, 125, 35, 255}, {40, 90, 25, 255});
        // Fruit dots
        put((int)SpriteID::FoodSourceLush, cx - 4, cy - 3, {255, 100, 80, 255});
        put((int)SpriteID::FoodSourceLush, cx + 3, cy - 2, {255, 120, 90, 255});
        put((int)SpriteID::FoodSourceLush, cx, cy + 1, {255, 90, 70, 255});
        put((int)SpriteID::FoodSourceLush, cx - 6, cy + 1, {255, 110, 85, 255});
        put((int)SpriteID::FoodSourceLush, cx + 5, cy, {255, 100, 75, 255});

        // ---- FoodSource Sparse (22) ----
        fill_sprite((int)SpriteID::FoodSourceSparse, TRANSP);
        iso_block((int)SpriteID::FoodSourceSparse, cx, cy, hw - 4, hh - 2, 3,
            {75, 95, 55, 255}, {55, 70, 35, 255}, {45, 60, 30, 255}, {30, 40, 20, 255});

        // ---- HiddenSpace (23) ----
        fill_sprite((int)SpriteID::HiddenSpace, TRANSP);
        iso_block((int)SpriteID::HiddenSpace, cx, cy, hw, hh, blk_h,
            {55, 35, 75, 255}, {40, 25, 55, 255}, {35, 20, 50, 255}, {25, 15, 35, 255});
        // Glowing eye
        put((int)SpriteID::HiddenSpace, cx - 2, cy - 2, {120, 80, 180, 255});
        put((int)SpriteID::HiddenSpace, cx + 2, cy - 2, {120, 80, 180, 255});
        put((int)SpriteID::HiddenSpace, cx, cy, {80, 50, 130, 255});

        // ---- Agent sprites (24-29): humanoid isometric figures ----
        make_agent((int)SpriteID::AgentNormal,     {100, 200, 255, 255}, {70, 150, 200, 255});
        make_agent((int)SpriteID::AgentDissociated, {100, 220, 255, 255}, {70, 160, 200, 255});
        make_agent((int)SpriteID::AgentEuphoric,    {220, 100, 220, 255}, {180, 70, 180, 255});
        make_agent((int)SpriteID::AgentBroken,      {255, 80, 80, 255},  {200, 50, 50, 255});
        make_agent((int)SpriteID::AgentRedeemed,    {100, 255, 100, 255}, {70, 200, 70, 255});
        make_agent((int)SpriteID::AgentSelected,    {255, 255, 100, 255}, {220, 200, 60, 255});

        // ---- Fallback (30) ----
        fill_sprite((int)SpriteID::Fallback, TRANSP);
        iso_diamond((int)SpriteID::Fallback, cx, cy, hw, hh,
            {150, 50, 150, 255}, {120, 30, 120, 255}, {100, 20, 100, 255}, {70, 10, 70, 255});
        put((int)SpriteID::Fallback, cx, cy, {255, 255, 255, 255});

        SDL_UnlockSurface(atlas_surf_);
    }

    void make_agent(int idx, PX body, PX dark) {
        fill_sprite(idx, {0, 0, 0, 0});
        int ax = SW / 2;
        int ay = 12;

        // Head (diamond, 4x3)
        put(idx, ax, ay, body);
        put(idx, ax - 1, ay + 1, body);
        put(idx, ax, ay + 1, body);
        put(idx, ax + 1, ay + 1, body);
        put(idx, ax - 1, ay + 2, dark);
        put(idx, ax, ay + 2, body);
        put(idx, ax + 1, ay + 2, dark);
        put(idx, ax, ay + 3, dark);

        // Body (wider diamond, 6x4)
        for (int dy = 0; dy < 4; dy++) {
            int w = (dy < 2) ? (2 + dy) : (4 - (dy - 2));
            for (int dx = -w; dx <= w; dx++) {
                PX c = (dx < 0) ? dark : body;
                if (dy == 0 || dy == 3) c = dark;
                put(idx, ax + dx, ay + 4 + dy, c);
            }
        }

        // Selection ring for selected agent
        if (idx == (int)SpriteID::AgentSelected) {
            for (int a = 0; a < 360; a += 20) {
                int rx = ax + (int)(8 * cos(a * 3.14159f / 180.0f));
                int ry = ay + 8 + (int)(4 * sin(a * 3.14159f / 180.0f));
                put(idx, rx, ry, {255, 255, 200, 200});
            }
        }

        // Eyes
        put(idx, ax - 1, ay + 1, {30, 30, 30, 255});
        put(idx, ax + 1, ay + 1, {30, 30, 30, 255});

        // Broken agent: red X eyes
        if (idx == (int)SpriteID::AgentBroken) {
            put(idx, ax - 1, ay + 1, {255, 40, 40, 255});
            put(idx, ax + 1, ay + 1, {255, 40, 40, 255});
        }
        // Euphoric agent: big eyes
        if (idx == (int)SpriteID::AgentEuphoric) {
            put(idx, ax - 1, ay + 1, {255, 255, 100, 255});
            put(idx, ax + 1, ay + 1, {255, 255, 100, 255});
        }
        // Redeemed agent: halo
        if (idx == (int)SpriteID::AgentRedeemed) {
            for (int dx = -3; dx <= 3; dx++)
                put(idx, ax + dx, ay - 2, {255, 255, 200, 180});
        }
    }
};
