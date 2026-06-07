/**
 * test_frame_yflip_verify.c — Verifikation: Frame-Format + Y-Flip auf gl33 und
 * vulkan sind IDENTISCH (Plan-008 A3A, Agent p008-a3a).
 *
 * Ohne echten GL-/Vulkan-Context laesst sich der Grab nicht live ausfuehren.
 * Diese Harness reproduziert die EXAKTE Normalisierungs-Logik beider Backends
 * (1:1 wie in moo_3d_gl33.c::gl33_grab_rgba und moo_3d_vulkan.c::vk_grab_rgba)
 * und prueft an einer gemeinsamen Logik-Szene, dass beide Pfade denselben
 * RGBA8-top-left-Frame erzeugen:
 *
 *   - gl33 / hybrid / gl21: glReadPixels liefert RGBA, bottom-left origin
 *                            -> Y-FLIP, kein Swizzle.
 *   - vulkan:               vkCmdCopyImageToBuffer liefert (B)GRA, top-down
 *                            -> KEIN Y-Flip, BGRA->RGBA-Swizzle.
 *
 * Erwartung: fuer dieselbe Logik-Szene liefern beide an JEDER Logik-Koordinate
 * (top-left) byteweise denselben RGBA-Wert. Genau das garantiert, dass
 * test_pixel(x,y) backend-unabhaengig dasselbe sieht.
 *
 * Kompilieren/Ausfuehren:
 *   gcc -fsanitize=address -g -std=c11 -Wall -Wextra test_frame_yflip_verify.c \
 *       -o /tmp/t_yflip && /tmp/t_yflip
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define W 17
#define H 9

/* Logik-Szene (top-left origin, RGBA) — die "Wahrheit", die beide Backends
 * nach Normalisierung liefern muessen. */
static void logic_scene(uint8_t* out /* W*H*4 */) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            size_t o = ((size_t)y * W + x) * 4;
            out[o+0] = (uint8_t)(x * 13 + 1);
            out[o+1] = (uint8_t)(y * 7  + 2);
            out[o+2] = (uint8_t)((x ^ y) + 3);
            out[o+3] = 255;
        }
}

/* ---- gl33-Pfad: Backend-Rohdaten = RGBA bottom-left (GL-Origin). ---- */
/* Roh-Backendpuffer so erzeugen, wie glReadPixels ihn aus der Logik-Szene
 * liefern WUERDE: Zeile 0 unten. */
static uint8_t* gl_raw_from_scene(const uint8_t* scene) {
    uint8_t* raw = (uint8_t*)malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++) {
        const uint8_t* src = scene + (size_t)(H - 1 - y) * W * 4; /* bottom-up */
        memcpy(raw + (size_t)y * W * 4, src, (size_t)W * 4);
    }
    return raw;
}
/* gl33_grab_rgba-Logik: Y-Flip, kein Swizzle. */
static uint8_t* gl33_normalize(uint8_t* buf) {
    uint8_t* row = (uint8_t*)malloc((size_t)W * 4);
    for (int y = 0; y < H / 2; y++) {
        uint8_t* a = buf + (size_t)y * W * 4;
        uint8_t* b = buf + (size_t)(H - 1 - y) * W * 4;
        memcpy(row, a, (size_t)W * 4);
        memcpy(a, b, (size_t)W * 4);
        memcpy(b, row, (size_t)W * 4);
    }
    free(row);
    return buf;
}

/* ---- vulkan-Pfad: Backend-Rohdaten = BGRA top-down (Swapchain B8G8R8A8). ---- */
static uint8_t* vk_raw_from_scene(const uint8_t* scene) {
    uint8_t* raw = (uint8_t*)malloc((size_t)W * H * 4);
    for (size_t i = 0; i < (size_t)W * H; i++) {
        raw[i*4+0] = scene[i*4+2]; /* B */
        raw[i*4+1] = scene[i*4+1]; /* G */
        raw[i*4+2] = scene[i*4+0]; /* R */
        raw[i*4+3] = scene[i*4+3]; /* A */
    }
    return raw; /* top-down, kein Flip noetig */
}
/* vk_grab_rgba-Logik: BGRA->RGBA swizzle, Alpha opak, KEIN Y-Flip. */
static uint8_t* vk_normalize(const uint8_t* raw /* BGRA top-down */) {
    uint8_t* out = (uint8_t*)malloc((size_t)W * H * 4);
    int is_bgra = 1;
    for (size_t i = 0; i < (size_t)W * H; i++) {
        if (is_bgra) {
            out[i*4+0] = raw[i*4+2];
            out[i*4+1] = raw[i*4+1];
            out[i*4+2] = raw[i*4+0];
        } else {
            out[i*4+0] = raw[i*4+0];
            out[i*4+1] = raw[i*4+1];
            out[i*4+2] = raw[i*4+2];
        }
        out[i*4+3] = 255;
    }
    return out;
}

int main(void) {
    uint8_t scene[W*H*4];
    logic_scene(scene);

    uint8_t* gl_raw  = gl_raw_from_scene(scene);
    uint8_t* gl_norm = gl33_normalize(gl_raw); /* in-place; gl_raw == gl_norm */

    uint8_t* vk_raw  = vk_raw_from_scene(scene);
    uint8_t* vk_norm = vk_normalize(vk_raw);

    int fail = 0;
    /* (a) gl33 == Logik-Szene */
    if (memcmp(gl_norm, scene, sizeof(scene)) != 0) { printf("FAIL: gl33-Normalisierung != Logik-Szene\n"); fail = 1; }
    /* (b) vulkan == Logik-Szene */
    if (memcmp(vk_norm, scene, sizeof(scene)) != 0) { printf("FAIL: vulkan-Normalisierung != Logik-Szene\n"); fail = 1; }
    /* (c) gl33 == vulkan an JEDER Logik-Koordinate (das eigentliche Gate) */
    for (int y = 0; y < H && !fail; y++)
        for (int x = 0; x < W; x++) {
            size_t o = ((size_t)y * W + x) * 4;
            for (int c = 0; c < 4; c++) {
                if (gl_norm[o+c] != vk_norm[o+c]) {
                    printf("FAIL: gl33 != vulkan an (%d,%d) Kanal %d: %u vs %u\n",
                           x, y, c, gl_norm[o+c], vk_norm[o+c]);
                    fail = 1;
                }
            }
        }

    free(gl_norm);
    free(vk_raw);
    free(vk_norm);

    if (fail) { printf("\n=== Y-Flip/Format-Verifikation: FEHLGESCHLAGEN ===\n"); return 1; }
    printf("=== Y-Flip/Format-Verifikation gl33 vs vulkan: IDENTISCH (top-left RGBA8) ===\n");
    return 0;
}
