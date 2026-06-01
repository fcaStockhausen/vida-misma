#include "font_cache.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>

// Font paths to try (in order of preference)
static const char* FONT_PATHS[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/SFNSMono.ttf",
    "/System/Library/Fonts/Monaco.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    nullptr
};

static constexpr int FONT_SIZES[(int)FontSize::COUNT] = {
    11,  // Small
    14,  // Normal
    18,  // Large
};

FontCache::~FontCache() {
    destroy();
}

void FontCache::destroy() {
    for (auto& [k, entry] : cache_) {
        if (entry.tex) SDL_DestroyTexture(entry.tex);
    }
    cache_.clear();
    for (int i = 0; i < (int)FontSize::COUNT; i++) {
        if (fonts_[i]) {
            TTF_CloseFont(fonts_[i]);
            fonts_[i] = nullptr;
        }
    }
    renderer_ = nullptr;
}

bool FontCache::init(SDL_Renderer* renderer) {
    renderer_ = renderer;

    if (TTF_Init() < 0) {
        std::fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return false;
    }

    // Try each font path
    const char* found_path = nullptr;
    for (int i = 0; FONT_PATHS[i]; i++) {
        FILE* f = std::fopen(FONT_PATHS[i], "rb");
        if (f) {
            std::fclose(f);
            found_path = FONT_PATHS[i];
            break;
        }
    }

    if (!found_path) {
        std::fprintf(stderr, "FontCache: no suitable monospace font found\n");
        return false;
    }

    // Load each size
    for (int i = 0; i < (int)FontSize::COUNT; i++) {
        fonts_[i] = TTF_OpenFont(found_path, FONT_SIZES[i]);
        if (!fonts_[i]) {
            std::fprintf(stderr, "TTF_OpenFont(%s, %d) failed: %s\n",
                         found_path, FONT_SIZES[i], TTF_GetError());
            return false;
        }
    }

    return true;
}

TTF_Font* FontCache::font(FontSize size) const {
    int idx = (int)size;
    if (idx < 0 || idx >= (int)FontSize::COUNT) idx = 0;
    return fonts_[idx];
}

int FontCache::line_height(FontSize size) const {
    TTF_Font* f = font(size);
    if (!f) return FONT_SIZES[(int)size] + 2;
    return TTF_FontLineSkip(f);
}

uint64_t FontCache::make_key(const char* text, SDL_Color color, FontSize size) {
    // Simple hash: FNV-1a on text, combine with color and size
    uint64_t h = 2166136261ULL;
    for (const char* p = text; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619ULL;
    }
    h ^= (uint64_t)color.r;
    h *= 16777619ULL;
    h ^= (uint64_t)color.g;
    h *= 16777619ULL;
    h ^= (uint64_t)color.b;
    h *= 16777619ULL;
    h ^= (uint64_t)(int)size;
    h *= 16777619ULL;
    return h;
}

FontCache::CacheEntry& FontCache::get_or_render(const char* text, SDL_Color color, FontSize size) {
    uint64_t key = make_key(text, color, size);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    // Evict if cache is full
    if ((int)cache_.size() >= MAX_CACHE) {
        // Simple eviction: clear half
        int to_remove = MAX_CACHE / 2;
        auto rit = cache_.begin();
        for (int i = 0; i < to_remove && rit != cache_.end(); i++) {
            if (rit->second.tex) SDL_DestroyTexture(rit->second.tex);
            rit = cache_.erase(rit);
        }
    }

    CacheEntry entry{};
    TTF_Font* f = font(size);
    if (f && text && text[0]) {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, text, color);
        if (surf) {
            entry.tex = SDL_CreateTextureFromSurface(renderer_, surf);
            entry.w = surf->w;
            entry.h = surf->h;
            SDL_FreeSurface(surf);
            if (entry.tex) {
                SDL_SetTextureBlendMode(entry.tex, SDL_BLENDMODE_BLEND);
            }
        }
    }

    auto& ref = cache_[key];
    ref = entry;
    return ref;
}

int FontCache::draw(int x, int y, const char* text, SDL_Color color, FontSize size) {
    if (!text || !text[0]) return 0;
    CacheEntry& e = get_or_render(text, color, size);
    if (e.tex) {
        SDL_Rect dst = {x, y, e.w, e.h};
        SDL_RenderCopy(renderer_, e.tex, nullptr, &dst);
    }
    return e.w;
}

int FontCache::draw(int x, int y, const std::string& text, SDL_Color color, FontSize size) {
    return draw(x, y, text.c_str(), color, size);
}

int FontCache::text_width(const char* text, FontSize size) {
    TTF_Font* f = font(size);
    if (!f || !text) return 0;
    int w = 0;
    TTF_SizeUTF8(f, text, &w, nullptr);
    return w;
}

int FontCache::text_height(FontSize size) {
    return line_height(size);
}

int FontCache::drawf(int x, int y, SDL_Color color, FontSize size, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return draw(x, y, buf, color, size);
}
