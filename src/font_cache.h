#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// Font cache: SDL2_ttf wrapper with glyph-texture caching
// ============================================================

enum class FontSize {
    Small = 0,   // ~11px — labels, values, log lines
    Normal = 1,  // ~14px — section titles, agent info
    Large = 2,   // ~18px — panel title
    COUNT
};

class FontCache {
public:
    FontCache() = default;
    ~FontCache();

    // Non-copyable
    FontCache(const FontCache&) = delete;
    FontCache& operator=(const FontCache&) = delete;

    bool init(SDL_Renderer* renderer);
    void destroy();

    // Draw text, returns the advance width
    int draw(int x, int y, const char* text, SDL_Color color,
             FontSize size = FontSize::Small);
    int draw(int x, int y, const std::string& text, SDL_Color color,
             FontSize size = FontSize::Small);

    // Measure text width/height without drawing
    int text_width(const char* text, FontSize size = FontSize::Small);
    int text_height(FontSize size = FontSize::Small);

    // Line height for layout
    int line_height(FontSize size = FontSize::Small) const;

    // Formatted draw (printf-style)
    int drawf(int x, int y, SDL_Color color, FontSize size, const char* fmt, ...);

private:
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* fonts_[(int)FontSize::COUNT] = {};

    // Cache: key = (font_idx << 20) | (r << 12) | (g << 4) | b, value = texture
    // For simplicity, cache rendered strings as textures
    struct CacheEntry {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
    };

    // Simple string-based cache (hash of text+color+size)
    std::unordered_map<uint64_t, CacheEntry> cache_;
    static constexpr int MAX_CACHE = 2048;

    uint64_t make_key(const char* text, SDL_Color color, FontSize size);
    CacheEntry& get_or_render(const char* text, SDL_Color color, FontSize size);

    TTF_Font* font(FontSize size) const;
};
