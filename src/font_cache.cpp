#include "font_cache.h"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

static const char* SYSTEM_FONT_PATHS[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/SFNSMono.ttf",
    "/System/Library/Fonts/Monaco.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    nullptr
};

static constexpr int FONT_SIZES[(int)FontSize::COUNT] = {
    13,  // Small
    16,  // Normal
    20,  // Large
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

    std::vector<std::string> font_paths;
    if (const char* configured = std::getenv("VIDA_FONT_PATH")) {
        font_paths.emplace_back(configured);
    }
#ifdef _WIN32
    if (const char* windows_dir = std::getenv("WINDIR")) {
        font_paths.emplace_back(std::string(windows_dir) + "\\Fonts\\consola.ttf");
        font_paths.emplace_back(std::string(windows_dir) + "\\Fonts\\lucon.ttf");
    }
#endif
    for (int i = 0; SYSTEM_FONT_PATHS[i]; i++) {
        font_paths.emplace_back(SYSTEM_FONT_PATHS[i]);
    }

    std::string found_path;
    for (const auto& path : font_paths) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f) {
            std::fclose(f);
            found_path = path;
            break;
        }
    }

    if (found_path.empty()) {
        std::fprintf(stderr, "FontCache: no suitable monospace font found\n");
        return false;
    }

    // Load each size
    for (int i = 0; i < (int)FontSize::COUNT; i++) {
        fonts_[i] = TTF_OpenFont(found_path.c_str(), FONT_SIZES[i]);
        if (!fonts_[i]) {
            std::fprintf(stderr, "TTF_OpenFont(%s, %d) failed: %s\n",
                         found_path.c_str(), FONT_SIZES[i], TTF_GetError());
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
        SDL_Surface* surf = TTF_RenderUTF8_Solid(f, text, color);
        if (surf) {
            SDL_SetColorKey(surf, SDL_TRUE, 0);
            entry.tex = SDL_CreateTextureFromSurface(renderer_, surf);
            entry.w = surf->w;
            entry.h = surf->h;
            SDL_FreeSurface(surf);
            if (entry.tex) {
                SDL_SetTextureBlendMode(entry.tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(entry.tex, SDL_ScaleModeNearest);
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
