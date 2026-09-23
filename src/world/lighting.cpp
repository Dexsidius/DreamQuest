#include "lighting.h"

bool Lighting::Prepare(SDL_Renderer* r) {
    if (owner != r) {
        // A different renderer (or the first frame): whatever was made before
        // belonged to the old one.
        owner = r;
        target = glow = nullptr;
        target_w = target_h = 0;
    }

    if (!glow) {
        // A round falloff, brightest in the middle, eased so the edge of a
        // pool of firelight has no visible rim.
        constexpr int S = 96;
        SDL_Surface* s = SDL_CreateSurface(S, S, SDL_PIXELFORMAT_RGBA32);
        if (!s) return false;
        if (SDL_LockSurface(s)) {
            Uint8* px = static_cast<Uint8*>(s->pixels);
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    const float dx = (x + 0.5f) / S * 2.0f - 1.0f;
                    const float dy = (y + 0.5f) / S * 2.0f - 1.0f;
                    float t = 1.0f - std::min(1.0f, sqrtf(dx * dx + dy * dy));
                    t = t * t * (3.0f - 2.0f * t);
                    Uint8* p = px + y * s->pitch + x * 4;
                    p[0] = p[1] = p[2] = 255;
                    p[3] = static_cast<Uint8>(255.0f * t);
                }
            SDL_UnlockSurface(s);
        }
        glow = SDL_CreateTextureFromSurface(r, s);
        SDL_DestroySurface(s);
        if (!glow) return false;
        SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);
    }

    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    if (w <= 0 || h <= 0) return false;
    if (!target || w != target_w || h != target_h) {
        if (target) SDL_DestroyTexture(target);
        target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!target) { target_w = target_h = 0; return false; }
        SDL_SetTextureBlendMode(target, SDL_BLENDMODE_MOD);
        target_w = w;
        target_h = h;
    }
    return true;
}

void Lighting::Render(SDL_Renderer* r, const Camera& cam, SDL_Color ambient,
                      const vector<Light>& lights, const vector<SDL_FRect>* lit) {
    const bool daylight = ambient.r >= 254 && ambient.g >= 254 && ambient.b >= 254;
    if (daylight) return;
    if (!Prepare(r)) return;

    SDL_Texture* previous = SDL_GetRenderTarget(r);
    if (!SDL_SetRenderTarget(r, target)) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, ambient.r, ambient.g, ambient.b, 255);
    SDL_RenderClear(r);

    const SDL_FRect view = cam.VisibleWorldRect(160.0f);
    for (const Light& l : lights) {
        if (l.intensity <= 0.01f) continue;
        if (l.x + l.radius < view.x || l.x - l.radius > view.x + view.w ||
            l.y + l.radius < view.y || l.y - l.radius > view.y + view.h) continue;
        const SDL_FRect dst = cam.ToScreenRect({l.x - l.radius, l.y - l.radius,
                                                l.radius * 2.0f, l.radius * 2.0f});
        SDL_SetTextureColorMod(glow, l.color.r, l.color.g, l.color.b);
        SDL_SetTextureAlphaMod(glow, static_cast<Uint8>(255.0f * std::clamp(l.intensity, 0.0f, 1.0f)));
        SDL_RenderTexture(r, glow, nullptr, &dst);
    }
    if (lit && !lit->empty()) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 255, 236, 214, 255);
        for (const SDL_FRect& w : *lit) {
            const SDL_FRect dst = cam.ToScreenRect(w);
            SDL_RenderFillRect(r, &dst);
        }
    }

    SDL_SetRenderTarget(r, previous);
    SDL_RenderTexture(r, target, nullptr, nullptr);
}
