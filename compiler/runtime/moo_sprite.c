/**
 * moo_sprite.c — Sprite-System fuer moo (SDL2_Image).
 * Laedt PNG/BMP Bilder und zeichnet sie als SDL2 Textures.
 */

#include "moo_runtime.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdlib.h>
#include <string.h>

extern MooValue moo_number(double v);
extern MooValue moo_bool(bool b);
extern MooValue moo_none(void);
extern MooValue moo_string_new(const char* s);
extern void moo_throw(MooValue v);

/* ========================================================
 * Sprite-Verwaltung
 * ======================================================== */

#define MAX_SPRITES 256

typedef struct {
    SDL_Texture* texture;
    int width;
    int height;
    bool used;
} MooSprite;

static MooSprite g_sprites[MAX_SPRITES] = {0};
static int img_initialized = 0;

/* Holt den SDL_Renderer aus einem MooWindow (MOO_WINDOW Tag) */
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    bool open;
} MooWindow;

static SDL_Renderer* get_renderer(MooValue win) {
    if (win.tag != MOO_WINDOW) return NULL;
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(win);
    return mw ? mw->renderer : NULL;
}

/* ========================================================
 * Sprite laden: sprite_laden(win, pfad) → Sprite-ID
 * ======================================================== */

MooValue moo_sprite_load(MooValue win, MooValue path) {
    SDL_Renderer* renderer = get_renderer(win);
    if (!renderer) {
        moo_throw(moo_string_new("Sprite laden: Kein gültiges Fenster"));
        return moo_none();
    }
    if (path.tag != MOO_STRING) {
        moo_throw(moo_string_new("Sprite laden: Pfad muss ein String sein"));
        return moo_none();
    }

    /* SDL_image initialisieren (einmalig) */
    if (!img_initialized) {
        int flags = IMG_INIT_PNG | IMG_INIT_JPG;
        if (!(IMG_Init(flags) & flags)) {
            moo_throw(moo_string_new("SDL_image Init fehlgeschlagen"));
            return moo_none();
        }
        img_initialized = 1;
    }

    /* Freien Slot finden */
    int slot = -1;
    for (int i = 0; i < MAX_SPRITES; i++) {
        if (!g_sprites[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        moo_throw(moo_string_new("Maximale Sprite-Anzahl erreicht"));
        return moo_none();
    }

    /* Bild laden */
    const char* filepath = MV_STR(path)->chars;
    SDL_Surface* surface = IMG_Load(filepath);
    if (!surface) {
        moo_throw(moo_string_new(IMG_GetError()));
        return moo_none();
    }

    /* Texture erstellen */
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    g_sprites[slot].width = surface->w;
    g_sprites[slot].height = surface->h;
    SDL_FreeSurface(surface);

    if (!tex) {
        moo_throw(moo_string_new(SDL_GetError()));
        return moo_none();
    }

    g_sprites[slot].texture = tex;
    g_sprites[slot].used = true;
    return moo_number((double)slot);
}

/* ========================================================
 * Sprite zeichnen: sprite_zeichnen(win, id, x, y)
 * ======================================================== */

void moo_sprite_draw(MooValue win, MooValue id, MooValue x, MooValue y) {
    SDL_Renderer* renderer = get_renderer(win);
    if (!renderer) return;
    int idx = (int)MV_NUM(id);
    if (idx < 0 || idx >= MAX_SPRITES || !g_sprites[idx].used) return;

    SDL_Rect dst = {
        (int)MV_NUM(x), (int)MV_NUM(y),
        g_sprites[idx].width, g_sprites[idx].height
    };
    SDL_RenderCopy(renderer, g_sprites[idx].texture, NULL, &dst);
}

/* ========================================================
 * Sprite skaliert zeichnen: sprite_zeichnen_skaliert(win, id, x, y, w, h)
 * ======================================================== */

void moo_sprite_draw_scaled(MooValue win, MooValue id,
                            MooValue x, MooValue y,
                            MooValue w, MooValue h) {
    SDL_Renderer* renderer = get_renderer(win);
    if (!renderer) return;
    int idx = (int)MV_NUM(id);
    if (idx < 0 || idx >= MAX_SPRITES || !g_sprites[idx].used) return;

    SDL_Rect dst = {
        (int)MV_NUM(x), (int)MV_NUM(y),
        (int)MV_NUM(w), (int)MV_NUM(h)
    };
    SDL_RenderCopy(renderer, g_sprites[idx].texture, NULL, &dst);
}

/* ========================================================
 * Sprite-Ausschnitt: sprite_ausschnitt(win, id, sx, sy, sw, sh, dx, dy, dw, dh)
 * Fuer Sprite-Sheets / Animation
 * ======================================================== */

void moo_sprite_draw_region(MooValue win, MooValue id,
                            MooValue sx, MooValue sy, MooValue sw, MooValue sh,
                            MooValue dx, MooValue dy, MooValue dw, MooValue dh) {
    SDL_Renderer* renderer = get_renderer(win);
    if (!renderer) return;
    int idx = (int)MV_NUM(id);
    if (idx < 0 || idx >= MAX_SPRITES || !g_sprites[idx].used) return;

    SDL_Rect src = {
        (int)MV_NUM(sx), (int)MV_NUM(sy),
        (int)MV_NUM(sw), (int)MV_NUM(sh)
    };
    SDL_Rect dst = {
        (int)MV_NUM(dx), (int)MV_NUM(dy),
        (int)MV_NUM(dw), (int)MV_NUM(dh)
    };
    SDL_RenderCopy(renderer, g_sprites[idx].texture, &src, &dst);
}

/* ========================================================
 * Sprite-Dimensionen abfragen
 * ======================================================== */

MooValue moo_sprite_width(MooValue id) {
    int idx = (int)MV_NUM(id);
    if (idx < 0 || idx >= MAX_SPRITES || !g_sprites[idx].used) return moo_number(0);
    return moo_number((double)g_sprites[idx].width);
}

MooValue moo_sprite_height(MooValue id) {
    int idx = (int)MV_NUM(id);
    if (idx < 0 || idx >= MAX_SPRITES || !g_sprites[idx].used) return moo_number(0);
    return moo_number((double)g_sprites[idx].height);
}

/* ========================================================
 * Sprite freigeben
 * ======================================================== */

void moo_sprite_free(MooValue id) {
    int idx = (int)MV_NUM(id);
    if (idx < 0 || idx >= MAX_SPRITES || !g_sprites[idx].used) return;
    if (g_sprites[idx].texture) SDL_DestroyTexture(g_sprites[idx].texture);
    g_sprites[idx].texture = NULL;
    g_sprites[idx].used = false;
}
