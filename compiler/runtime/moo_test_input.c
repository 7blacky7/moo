/**
 * moo_test_input.c — Simulierte Eingaben fuer automatisierte Spiel-Tests.
 * Erlaubt programmatisches Steuern von Tastatur und Maus via SDL_PushEvent.
 */

#include "moo_runtime.h"
#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>

/* === Vorwaertsdeklarationen === */
extern MooValue moo_none(void);

/* === Taste-zu-Scancode Mapping (wie in moo_graphics.c) === */
static SDL_Scancode taste_zu_scancode(const char* name) {
    if (!name) return SDL_SCANCODE_UNKNOWN;

    /* Einzelner Buchstabe */
    if (name[1] == '\0') {
        char c = name[0];
        if (c >= 'a' && c <= 'z') return SDL_SCANCODE_A + (c - 'a');
        if (c >= 'A' && c <= 'Z') return SDL_SCANCODE_A + (c - 'A');
        if (c >= '0' && c <= '9') return SDL_SCANCODE_0 + (c - '0');
        if (c == ' ') return SDL_SCANCODE_SPACE;
    }

    /* Benannte Tasten (DE + EN) */
    if (strcmp(name, "hoch") == 0 || strcmp(name, "up") == 0) return SDL_SCANCODE_UP;
    if (strcmp(name, "runter") == 0 || strcmp(name, "down") == 0) return SDL_SCANCODE_DOWN;
    if (strcmp(name, "links") == 0 || strcmp(name, "left") == 0) return SDL_SCANCODE_LEFT;
    if (strcmp(name, "rechts") == 0 || strcmp(name, "right") == 0) return SDL_SCANCODE_RIGHT;
    if (strcmp(name, "leertaste") == 0 || strcmp(name, "space") == 0) return SDL_SCANCODE_SPACE;
    if (strcmp(name, "eingabe") == 0 || strcmp(name, "enter") == 0) return SDL_SCANCODE_RETURN;
    if (strcmp(name, "escape") == 0 || strcmp(name, "esc") == 0) return SDL_SCANCODE_ESCAPE;
    if (strcmp(name, "shift") == 0) return SDL_SCANCODE_LSHIFT;
    if (strcmp(name, "tab") == 0) return SDL_SCANCODE_TAB;

    return SDL_SCANCODE_UNKNOWN;
}

/* === Taste simulieren === */
void moo_simulate_key(MooValue key, MooValue pressed) {
    MooString* key_str = MV_STR(key);
    if (!key_str) return;

    SDL_Scancode sc = taste_zu_scancode(key_str->chars);
    if (sc == SDL_SCANCODE_UNKNOWN) return;

    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = MV_NUM(pressed) ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.scancode = sc;
    event.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    event.key.state = MV_NUM(pressed) ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = 0;

    SDL_PushEvent(&event);
}

/* === Maus simulieren === */
void moo_simulate_mouse(MooValue x, MooValue y, MooValue click) {
    int mx = (int)MV_NUM(x);
    int my = (int)MV_NUM(y);
    int clicked = (int)MV_NUM(click);

    /* Maus-Bewegung */
    SDL_Event motion;
    memset(&motion, 0, sizeof(motion));
    motion.type = SDL_MOUSEMOTION;
    motion.motion.x = mx;
    motion.motion.y = my;
    SDL_PushEvent(&motion);

    /* Klick wenn gewuenscht */
    if (clicked) {
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
