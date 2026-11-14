/* Test-Ersatzheader NUR fuer test_sim_input_asan (run_sanitize.sh).
 * moo_3d.c bindet seit raum_textur_laden <SDL2/SDL.h> ein; die Sanitizer-Runner
 * installieren kein SDL2. Der Harness prueft nur die Eingabe-Simulation, die
 * Texturpfade werden nie aufgerufen (Symbole: tests/sim_input_extra_stubs.c).
 * Enthaelt nur, was moo_3d.c davon verwendet. Nicht im Produktbuild benutzen. */
#ifndef MOO_TEST_SDL_STUB_H
#define MOO_TEST_SDL_STUB_H
#include <stdint.h>
typedef struct SDL_Surface {
    uint32_t flags;
    void* format;
    int w, h;
    int pitch;
    void* pixels;
} SDL_Surface;
#define SDL_PIXELFORMAT_RGBA32 0x16762004u
SDL_Surface* SDL_ConvertSurfaceFormat(SDL_Surface* src, uint32_t pixel_format, uint32_t flags);
void SDL_FreeSurface(SDL_Surface* surface);
#endif
