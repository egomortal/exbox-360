// Included inside main.cpp's private namespace; see docs/AD_RENDERING.md.
namespace promotion_art {
std::map<std::string, bool> transparency_cache;

const dash::AdEntry *find_metadata(const std::string &path) {
    const auto found = std::find_if(model.ad_catalog.begin(), model.ad_catalog.end(),
        [&](const dash::AdEntry &entry) { return entry.image == path; });
    return found == model.ad_catalog.end() ? nullptr : &*found;
}

bool has_caption(const dash::Card &card) {
    // Filenames and store titles remain picker metadata. Only an original
    // feed caption may add a caption band over recovered dashboard artwork.
    const auto *entry = find_metadata(card.image);
    return entry && entry->caption_source == "original-feed";
}

std::string label(const dash::Card &card) {
    if (card.promo < 0 || has_caption(card)) {
        return card.title;
    }
    const auto *entry = find_metadata(card.image);
    if (entry && entry->caption_source == "official-store-title") {
        return card.title;
    }
    return "Custom artwork " + std::to_string(card.promo + 1);
}

bool has_transparency(const std::string &path, SDL_Surface *image) {
    const auto found = transparency_cache.find(path);
    if (found != transparency_cache.end()) {
        return found->second;
    }
    // title_icon normalizes the cached source to ARGB8888 without changing
    // its dimensions. The alpha scan therefore reads one Uint32 per pixel.
    bool transparent = false;
    if (image->format->Amask) {
        if (SDL_MUSTLOCK(image)) {
            SDL_LockSurface(image);
        }
        for (int y = 0; y < image->h && !transparent; ++y) {
            const auto *row = reinterpret_cast<const Uint32 *>(
                static_cast<const Uint8 *>(image->pixels) + y * image->pitch);
            for (int x = 0; x < image->w; ++x) {
                if ((row[x] & image->format->Amask) != image->format->Amask) {
                    transparent = true;
                    break;
                }
            }
        }
        if (SDL_MUSTLOCK(image)) {
            SDL_UnlockSurface(image);
        }
    }
    transparency_cache[path] = transparent;
    return transparent;
}

#ifndef __PROSPERO__
void write_json_string(FILE *file, const std::string &value) {
    fputc('"', file);
    for (unsigned char character : value) {
        if (character == '"' || character == '\\') {
            fputc('\\', file);
            fputc(character, file);
        } else if (character < 32) {
            fprintf(file, "\\u%04x", unsigned(character));
        } else {
            fputc(character, file);
        }
    }
    fputc('"', file);
}

void write_draw_audit(const std::string &path, const std::string &role,
    const SDL_Rect &box, const SDL_Rect &source, const SDL_Rect &destination,
    const SDL_Rect &caption, SDL_Surface *image, bool transparent,
    bool near_aspect_cover, int index, bool cache_hit, double draw_ms) {
    static FILE *file = [] {
        const char *path = getenv("NXE_AD_DRAW_AUDIT");
        return path ? fopen(path, "w") : nullptr;
    }();
    if (!file) {
        return;
    }
    SDL_Rect clip;
    SDL_GetClipRect(screen, &clip);
    fprintf(file, "{\"ticks\":%u,\"era\":%d,\"build\":%d,\"index\":%d,\"selected\":%d,\"path\":",
        SDL_GetTicks(), model.era, model.dashboard_build, index, model.current());
    write_json_string(file, path);
    fputs(",\"role\":", file);
    write_json_string(file, role);
    fprintf(file,
        ",\"decoded\":[%d,%d],\"slot\":[%d,%d,%d,%d],\"source\":[%d,%d,%d,%d]"
        ",\"destination\":[%d,%d,%d,%d],\"caption\":[%d,%d,%d,%d],\"clip\":[%d,%d,%d,%d]"
        ",\"transparent\":%s,\"near_aspect_cover\":%s,\"source_cache_hit\":%s,\"draw_ms\":%.4f}\n",
        image->w, image->h,
        box.x, box.y, box.w, box.h,
        source.x, source.y, source.w, source.h,
        destination.x, destination.y, destination.w, destination.h,
        caption.x, caption.y, caption.w, caption.h,
        clip.x, clip.y, clip.w, clip.h,
        transparent ? "true" : "false", near_aspect_cover ? "true" : "false",
        cache_hit ? "true" : "false", draw_ms);
}
#endif

bool draw(const std::string &path, const SDL_Rect &box,
    const SDL_Rect &caption, int index = -1) {
#ifndef __PROSPERO__
    const Uint64 started = SDL_GetPerformanceCounter();
    const bool cache_hit = icon_cache.find("original:" + path) != icon_cache.end();
#endif
    SDL_Surface *image = title_icon(path, true);
    if (!image || box.w < 1 || box.h < 1) {
        return false;
    }
    const auto *entry = find_metadata(path);
    const std::string role = entry ? entry->role : "unknown";
    const bool transparent = has_transparency(path, image);
    const double ratio = double(image->w) * box.h / (double(image->h) * box.w);

    // The 1% limit is a conservative project policy, not a recovered host
    // stretch mode. Unknown, transparent and mismatched art stays complete.
    const bool near_aspect_cover = entry && entry->auto_eligible &&
        entry->preferred_fit == "match_slot_aspect" && !transparent &&
        std::abs(ratio - 1.0) <= 0.01;
    SDL_Rect source{0, 0, image->w, image->h};
    SDL_Rect destination = contained_rect(image->w, image->h, box);
    if (near_aspect_cover) {
        if (ratio > 1) {
            source.w = std::max(1, int(double(image->h) * box.w / box.h));
            source.x = (image->w - source.w) / 2;
        } else {
            source.h = std::max(1, int(double(image->w) * box.h / box.w));
            source.y = (image->h - source.h) / 2;
        }
        destination = box;
    }

    // Slot coordinates are the final 1920x1080 era layout units. Clip before
    // painting so resampling cannot bleed into adjacent original tile gaps.
    SDL_Rect saved;
    SDL_Rect clip{};
    SDL_GetClipRect(screen, &saved);
    SDL_IntersectRect(&saved, &box, &clip);
    SDL_SetClipRect(screen, &clip);
#ifndef __PROSPERO__
    if (!getenv("NXE_AD_DRAW_SUPPRESS")) {
        blit_quality(image, &source, screen, &destination);
    }
#else
    blit_quality(image, &source, screen, &destination);
#endif

#ifndef __PROSPERO__
    const double draw_ms = 1000.0 * double(SDL_GetPerformanceCounter() - started) /
        double(SDL_GetPerformanceFrequency());
    write_draw_audit(path, role, box, source, destination, caption, image,
        transparent, near_aspect_cover, index, cache_hit, draw_ms);
#else
    (void)index;
    (void)caption;
#endif
    SDL_SetClipRect(screen, &saved);
    return true;
}
}
