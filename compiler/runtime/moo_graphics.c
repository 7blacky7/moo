/**
 * moo_graphics.c — SDL2-basierte Grafik, Zeichnen + Input fuer moo.
 * Merged: K2 (Fenster/Zeichnen mit MOO_WINDOW + Farbstrings) + K4 (Input/Timing)
 */

#include "moo_runtime.h"
#include <SDL2/SDL.h>

// === MooWindow Struktur ===
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    bool open;
} MooWindow;

// Vorwaertsdeklarationen
extern MooValue moo_string_new(const char* s);
extern MooValue moo_bool(bool b);
extern MooValue moo_number(double n);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

// ============================================================
// Farb-Parsing (K2): deutsch + englisch + #RRGGBB hex
// ============================================================
typedef struct { uint8_t r, g, b, a; } Color;

static Color parse_color(MooValue color_val) {
    Color c = {0, 0, 0, 255};
    if (color_val.tag != MOO_STRING) return c;
    const char* s = MV_STR(color_val)->chars;

    if (s[0] == '#' && strlen(s) == 7) {
        unsigned int hex;
        sscanf(s + 1, "%06x", &hex);
        c.r = (hex >> 16) & 0xFF;
        c.g = (hex >> 8) & 0xFF;
        c.b = hex & 0xFF;
        return c;
    }

    if (strcmp(s, "rot") == 0 || strcmp(s, "red") == 0)
        { c.r = 255; return c; }
    if (strcmp(s, "gruen") == 0 || strcmp(s, "green") == 0 || strcmp(s, "grün") == 0)
        { c.g = 255; return c; }
    if (strcmp(s, "blau") == 0 || strcmp(s, "blue") == 0)
        { c.b = 255; return c; }
    if (strcmp(s, "weiss") == 0 || strcmp(s, "white") == 0 || strcmp(s, "weiß") == 0)
        { c.r = 255; c.g = 255; c.b = 255; return c; }
    if (strcmp(s, "schwarz") == 0 || strcmp(s, "black") == 0)
        { return c; }
    if (strcmp(s, "gelb") == 0 || strcmp(s, "yellow") == 0)
        { c.r = 255; c.g = 255; return c; }
    if (strcmp(s, "cyan") == 0)
        { c.g = 255; c.b = 255; return c; }
    if (strcmp(s, "magenta") == 0 || strcmp(s, "pink") == 0)
        { c.r = 255; c.b = 255; return c; }
    if (strcmp(s, "orange") == 0)
        { c.r = 255; c.g = 165; return c; }
    if (strcmp(s, "grau") == 0 || strcmp(s, "gray") == 0 || strcmp(s, "grey") == 0)
        { c.r = 128; c.g = 128; c.b = 128; return c; }

    return c;
}

static void set_color(SDL_Renderer* r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

// ============================================================
// Fenster-Funktionen (K2)
// ============================================================

MooValue moo_window_create(MooValue title, MooValue width, MooValue height) {
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            moo_throw(moo_string_new(SDL_GetError()));
            return moo_none();
        }
    }

    const char* t = (title.tag == MOO_STRING) ? MV_STR(title)->chars : "moo";
    int w = (width.tag == MOO_NUMBER) ? (int)MV_NUM(width) : 800;
    int h = (height.tag == MOO_NUMBER) ? (int)MV_NUM(height) : 600;

    SDL_Window* win = SDL_CreateWindow(t,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h, SDL_WINDOW_SHOWN);
    if (!win) {
        moo_throw(moo_string_new(SDL_GetError()));
        return moo_none();
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        SDL_DestroyWindow(win);
        moo_throw(moo_string_new(SDL_GetError()));
        return moo_none();
    }

    MooWindow* mw = (MooWindow*)malloc(sizeof(MooWindow));
    mw->window = win;
    mw->renderer = ren;
    mw->open = true;

    MooValue result;
    result.tag = MOO_WINDOW;
    moo_val_set_ptr(&result, mw);
    return result;
}

MooValue moo_window_is_open(MooValue window) {
    if (window.tag != MOO_WINDOW) return moo_bool(false);
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(window);
    if (!mw->open) return moo_bool(false);

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            mw->open = false;
            return moo_bool(false);
        }
    }
    return moo_bool(true);
}

void moo_window_clear(MooValue window, MooValue color) {
    if (window.tag != MOO_WINDOW) return;
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(window);
    Color c = parse_color(color);
    set_color(mw->renderer, c);
    SDL_RenderClear(mw->renderer);
}

void moo_window_update(MooValue window) {
    if (window.tag != MOO_WINDOW) return;
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(window);
    SDL_RenderPresent(mw->renderer);
}

void moo_window_close(MooValue window) {
    if (window.tag != MOO_WINDOW) return;
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(window);
    if (mw->renderer) SDL_DestroyRenderer(mw->renderer);
    if (mw->window) SDL_DestroyWindow(mw->window);
    mw->renderer = NULL;
    mw->window = NULL;
    mw->open = false;
}

// ============================================================
// Zeichenfunktionen (K2) — alle mit Farbstring
// ============================================================

void moo_draw_rect(MooValue win, MooValue x, MooValue y,
                   MooValue w, MooValue h, MooValue color) {
    if (win.tag != MOO_WINDOW) return;
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(win);
    Color c = parse_color(color);
    set_color(mw->renderer, c);
    SDL_Rect rect = {
        (int)MV_NUM(x), (int)MV_NUM(y),
        (int)MV_NUM(w), (int)MV_NUM(h)
    };
    SDL_RenderFillRect(mw->renderer, &rect);
}

void moo_draw_line(MooValue win, MooValue x1, MooValue y1,
                   MooValue x2, MooValue y2, MooValue color) {
    if (win.tag != MOO_WINDOW) return;
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(win);
    Color c = parse_color(color);
    set_color(mw->renderer, c);
    SDL_RenderDrawLine(mw->renderer,
        (int)MV_NUM(x1), (int)MV_NUM(y1),
        (int)MV_NUM(x2), (int)MV_NUM(y2));
}

void moo_draw_pixel(MooValue win, MooValue x, MooValue y, MooValue color) {
    if (win.tag != MOO_WINDOW) return;
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(win);
    Color c = parse_color(color);
    set_color(mw->renderer, c);
    SDL_RenderDrawPoint(mw->renderer, (int)MV_NUM(x), (int)MV_NUM(y));
}

void moo_draw_circle(MooValue win, MooValue cx, MooValue cy,
                     MooValue r, MooValue color) {
    if (win.tag != MOO_WINDOW) return;
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(win);
    Color c = parse_color(color);
    set_color(mw->renderer, c);

    // Bresenham Kreis-Algorithmus (gefuellt)
    int centerX = (int)MV_NUM(cx);
    int centerY = (int)MV_NUM(cy);
    int radius = (int)MV_NUM(r);

    int x = radius;
    int y = 0;
    int err = 1 - radius;

    while (x >= y) {
        SDL_RenderDrawLine(mw->renderer, centerX - x, centerY + y, centerX + x, centerY + y);
        SDL_RenderDrawLine(mw->renderer, centerX - x, centerY - y, centerX + x, centerY - y);
        SDL_RenderDrawLine(mw->renderer, centerX - y, centerY + x, centerX + y, centerY + x);
        SDL_RenderDrawLine(mw->renderer, centerX - y, centerY - x, centerX + y, centerY - x);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

// ============================================================
// Input: Tastatur (K4)
// ============================================================

static SDL_Scancode key_name_to_scancode(const char* name) {
    if (strcmp(name, "links") == 0 || strcmp(name, "left") == 0) return SDL_SCANCODE_LEFT;
    if (strcmp(name, "rechts") == 0 || strcmp(name, "right") == 0) return SDL_SCANCODE_RIGHT;
    if (strcmp(name, "oben") == 0 || strcmp(name, "up") == 0) return SDL_SCANCODE_UP;
    if (strcmp(name, "unten") == 0 || strcmp(name, "down") == 0) return SDL_SCANCODE_DOWN;
    if (strcmp(name, "leertaste") == 0 || strcmp(name, "space") == 0) return SDL_SCANCODE_SPACE;
    if (strcmp(name, "escape") == 0 || strcmp(name, "esc") == 0) return SDL_SCANCODE_ESCAPE;
    if (strcmp(name, "eingabe") == 0 || strcmp(name, "enter") == 0) return SDL_SCANCODE_RETURN;
    if (strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z')
        return (SDL_Scancode)(SDL_SCANCODE_A + (name[0] - 'a'));
    if (strlen(name) == 1 && name[0] >= 'A' && name[0] <= 'Z')
        return (SDL_Scancode)(SDL_SCANCODE_A + (name[0] - 'A'));
    return SDL_SCANCODE_UNKNOWN;
}

MooValue moo_key_pressed(MooValue key) {
    if (key.tag != MOO_STRING) return moo_bool(false);
    const char* name = MV_STR(key)->chars;
    SDL_Scancode sc = key_name_to_scancode(name);
    if (sc == SDL_SCANCODE_UNKNOWN) return moo_bool(false);
    const Uint8* state = SDL_GetKeyboardState(NULL);
    return moo_bool(state[sc] != 0);
}

// ============================================================
// Input: Maus (K4)
// ============================================================

MooValue moo_mouse_x(MooValue window) {
    (void)window;
    int x, y;
    SDL_GetMouseState(&x, &y);
    return moo_number((double)x);
}

MooValue moo_mouse_y(MooValue window) {
    (void)window;
    int x, y;
    SDL_GetMouseState(&x, &y);
    return moo_number((double)y);
}

MooValue moo_mouse_pressed(MooValue window) {
    (void)window;
    Uint32 buttons = SDL_GetMouseState(NULL, NULL);
    return moo_bool((buttons & SDL_BUTTON_LMASK) != 0);
}

// ============================================================
// Timing (K4)
// ============================================================

void moo_delay(MooValue ms) {
    if (ms.tag == MOO_NUMBER) {
        int delay_ms = (int)MV_NUM(ms);
        if (delay_ms > 0) SDL_Delay((Uint32)delay_ms);
    }
}

// ============================================================
// Test-API: Screenshot, Tastatur-Simulation, Maus-Simulation
// ============================================================

MooValue moo_screenshot(MooValue window, MooValue path) {
    if (window.tag != MOO_WINDOW || path.tag != MOO_STRING) return moo_bool(false);
    MooWindow* mw = (MooWindow*)moo_val_as_ptr(window);
    if (!mw || !mw->renderer) return moo_bool(false);

    int w, h;
    SDL_GetRendererOutputSize(mw->renderer, &w, &h);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return moo_bool(false);

    if (SDL_RenderReadPixels(mw->renderer, NULL, SDL_PIXELFORMAT_RGBA32,
                             surface->pixels, surface->pitch) != 0) {
        SDL_FreeSurface(surface);
        return moo_bool(false);
    }

    const char* filepath = MV_STR(path)->chars;
    int result = SDL_SaveBMP(surface, filepath);
    SDL_FreeSurface(surface);
    return moo_bool(result == 0);
}

void moo_simulate_key(MooValue key, MooValue pressed) {
    if (key.tag != MOO_STRING) return;
    const char* name = MV_STR(key)->chars;
    int is_down = (pressed.tag == MOO_BOOL) ? MV_BOOL(pressed) : 1;

    SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
    if (strcmp(name, "links") == 0 || strcmp(name, "left") == 0) sc = SDL_SCANCODE_LEFT;
    else if (strcmp(name, "rechts") == 0 || strcmp(name, "right") == 0) sc = SDL_SCANCODE_RIGHT;
    else if (strcmp(name, "oben") == 0 || strcmp(name, "up") == 0) sc = SDL_SCANCODE_UP;
    else if (strcmp(name, "unten") == 0 || strcmp(name, "down") == 0) sc = SDL_SCANCODE_DOWN;
    else if (strcmp(name, "leertaste") == 0 || strcmp(name, "space") == 0) sc = SDL_SCANCODE_SPACE;
    else if (strcmp(name, "escape") == 0) sc = SDL_SCANCODE_ESCAPE;
    else if (strcmp(name, "eingabe") == 0 || strcmp(name, "enter") == 0) sc = SDL_SCANCODE_RETURN;
    else if (strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z')
        sc = (SDL_Scancode)(SDL_SCANCODE_A + (name[0] - 'a'));
    if (sc == SDL_SCANCODE_UNKNOWN) return;

    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = is_down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.scancode = sc;
    event.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    event.key.state = is_down ? SDL_PRESSED : SDL_RELEASED;
    SDL_PushEvent(&event);
}

void moo_simulate_mouse(MooValue x, MooValue y, MooValue click) {
    int mx = (x.tag == MOO_NUMBER) ? (int)MV_NUM(x) : 0;
    int my = (y.tag == MOO_NUMBER) ? (int)MV_NUM(y) : 0;
    int do_click = (click.tag == MOO_BOOL) ? MV_BOOL(click) : 0;

    /* Mausposition setzen */
    SDL_Event motion;
    memset(&motion, 0, sizeof(motion));
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = mx;
    motion.motion.y = my;
    SDL_PushEvent(&motion);

    if (do_click) {
        SDL_Event down;
        memset(&down, 0, sizeof(down));
        down.type = SDL_MOUSEBUTTONDOWN;
        down.button.button = SDL_BUTTON_LEFT;
        down.button.x = mx;
        down.button.y = my;
        down.button.state = SDL_PRESSED;
        SDL_PushEvent(&down);

        SDL_Event up;
        memset(&up, 0, sizeof(up));
        up.type = SDL_MOUSEBUTTONUP;
        up.button.button = SDL_BUTTON_LEFT;
        up.button.x = mx;
        up.button.y = my;
        up.button.state = SDL_RELEASED;
        SDL_PushEvent(&up);
    }
}
