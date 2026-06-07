/**
 * test_gif_core_asan.c - Standalone-Harness fuer den isolierten GIF89a+LZW-
 *   Encoder-Kern (Plan-008 P008-A3B Teil 1, Agent p008-gifcore).
 *
 * Der Encoder (moo_gif.c) hat KEINE MOO-Abhaengigkeit -> dieser Harness braucht
 * KEINE Runtime-Stubs. Er linkt nur ../moo_gif.c und ruft die oeffentliche
 * moo_gif.h-API auf.
 *
 * Geprueft:
 *   1. Argument-Validierung: NULL-Pfad, w/h<=0, fps<=0 -> NULL; Dimensions-
 *      Mismatch in add_frame -> MOO_GIF_ERR_DIM; close(NULL) == OK.
 *   2. Mehrere synthetische RGBA-Frames (Farbverlauf + animierter Balken) ->
 *      open/add_frame/close. Streaming, keine komplette Sequenz im RAM.
 *   3. Erzeugtes GIF auf Validitaet: Header "GIF89a", Trailer 0x3B, plausible
 *      Mindestgroesse. (Externe Tiefenpruefung via PIL/file im Runner.)
 *   4. Stress fuer den LZW-Pfad: zufaelliges/feinkoerniges Bild erzwingt
 *      Tabellen-Wachstum bis 12 Bit + mindestens einen Clear/Reset -> deckt die
 *      heisse unsigned-Bitpacking-Schleife unter UBSan ab.
 *   5. Einzel-Pixel-GIF (1x1) und leerer/kleinster Pfad.
 *
 * Kompilieren/Ausfuehren (so wie die Voxel-Harnesses + run_sanitize.sh):
 *   gcc -fsanitize=address -g -std=c11 -Wall -Wextra -I.. \
 *       test_gif_core_asan.c ../moo_gif.c -o /tmp/t_gif_core
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_gif_core
 *
 *   gcc -fsanitize=undefined -fno-sanitize-recover=undefined -g -std=c11 \
 *       -Wall -Wextra -I.. test_gif_core_asan.c ../moo_gif.c -o /tmp/t_gif_core_ub
 *   UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/t_gif_core_ub
 */

#include "moo_gif.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Deterministischer PRNG (xorshift32), damit der Stresstest reproduzierbar
 * ist und ASan/UBSan-Laeufe identisch bleiben. Alles unsigned. */
static uint32_t g_rng = 0x1234567u;
static uint32_t rng_next(void) {
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

/* Fuellt ein RGBA-Frame mit einem Farbverlauf plus einem vertikalen Balken,
 * dessen Position vom Frame-Index abhaengt (=> sichtbare Animation). */
static void fill_gradient(uint8_t* rgba, int w, int h, int frame) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* px = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            px[0] = (uint8_t)((x * 255) / (w > 1 ? w - 1 : 1));      /* R: horizontal */
            px[1] = (uint8_t)((y * 255) / (h > 1 ? h - 1 : 1));      /* G: vertikal */
            px[2] = (uint8_t)((frame * 40) & 0xFF);                  /* B: Frame */
            px[3] = 255u;
            /* Animierter weisser Balken. */
            int bar = (frame * 7) % w;
            if (x == bar) { px[0] = 255; px[1] = 255; px[2] = 255; }
        }
    }
}

/* Fuellt ein Frame mit Rauschen -> maximale LZW-Entropie (Tabellen-Stress). */
static void fill_noise(uint8_t* rgba, int w, int h) {
    size_t n = (size_t)w * (size_t)h;
    for (size_t p = 0; p < n; ++p) {
        uint32_t r = rng_next();
        uint8_t* px = rgba + p * 4u;
        px[0] = (uint8_t)(r & 0xFF);
        px[1] = (uint8_t)((r >> 8) & 0xFF);
        px[2] = (uint8_t)((r >> 16) & 0xFF);
        px[3] = 255u;
    }
}

/* Validiert grundlegende GIF-Struktur auf der geschriebenen Datei:
 * Magic "GIF89a" am Anfang, Trailer 0x3B am Ende, Mindestgroesse. */
static int validate_gif(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "  validate: kann %s nicht oeffnen\n", path); return -1; }

    char magic[6];
    if (fread(magic, 1, 6, f) != 6 || memcmp(magic, "GIF89a", 6) != 0) {
        fprintf(stderr, "  validate: kein GIF89a-Header in %s\n", path);
        fclose(f);
        return -1;
    }
    if (fseek(f, -1, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    int last = fgetc(f);
    fclose(f);

    if (last != 0x3B) {
        fprintf(stderr, "  validate: Trailer 0x3B fehlt (last=0x%02X)\n", last);
        return -1;
    }
    /* Header(6)+LSD(7)+GCT(768)+Netscape(19)+Trailer(1) = 801 Mindestbytes. */
    if (size < 801) {
        fprintf(stderr, "  validate: Datei zu klein (%ld Bytes)\n", size);
        return -1;
    }
    printf("  validate: OK (%ld Bytes, GIF89a + Trailer 0x3B)\n", size);
    return 0;
}

/* --- Test 1: Argument-Validierung ----------------------------------------- */
static void test_args(void) {
    printf("[test] Argument-Validierung\n");
    assert(moo_gif_open(NULL, 4, 4, 10) == NULL);
    assert(moo_gif_open("/tmp/moo_gif_bad.gif", 0, 4, 10) == NULL);
    assert(moo_gif_open("/tmp/moo_gif_bad.gif", 4, -1, 10) == NULL);
    assert(moo_gif_open("/tmp/moo_gif_bad.gif", 4, 4, 0) == NULL);
    assert(moo_gif_close(NULL) == MOO_GIF_OK); /* NULL = No-Op */

    /* Dimensions-Mismatch. */
    MooGifWriter* g = moo_gif_open("/tmp/moo_gif_dim.gif", 8, 8, 10);
    assert(g != NULL);
    uint8_t buf[8 * 8 * 4];
    memset(buf, 0, sizeof(buf));
    assert(moo_gif_add_frame(g, buf, 8, 8) == MOO_GIF_OK);
    assert(moo_gif_add_frame(g, buf, 4, 8) == MOO_GIF_ERR_DIM);
    assert(moo_gif_add_frame(NULL, buf, 8, 8) == MOO_GIF_ERR_ARG);
    assert(moo_gif_add_frame(g, NULL, 8, 8) == MOO_GIF_ERR_ARG);
    assert(moo_gif_close(g) == MOO_GIF_OK);
    printf("  OK\n");
}

/* --- Test 2: animierter Farbverlauf, mehrere Frames ----------------------- */
static int test_animation(void) {
    printf("[test] Animation (Farbverlauf + Balken, 6 Frames, 64x48)\n");
    const int w = 64, h = 48, frames = 6;
    const char* path = "/tmp/moo_gif_anim.gif";

    MooGifWriter* g = moo_gif_open(path, w, h, 12);
    if (!g) { fprintf(stderr, "  open fehlgeschlagen\n"); return -1; }

    uint8_t* frame = (uint8_t*)malloc((size_t)w * (size_t)h * 4u);
    if (!frame) { moo_gif_close(g); return -1; }

    for (int i = 0; i < frames; ++i) {
        fill_gradient(frame, w, h, i);
        int rc = moo_gif_add_frame(g, frame, w, h);
        if (rc != MOO_GIF_OK) {
            fprintf(stderr, "  add_frame %d -> %d\n", i, rc);
            free(frame);
            moo_gif_close(g);
            return -1;
        }
    }
    free(frame);

    size_t cnt = moo_gif_frame_count(g);
    int rc = moo_gif_close(g);
    if (rc != MOO_GIF_OK) { fprintf(stderr, "  close -> %d\n", rc); return -1; }
    if (cnt != (size_t)frames) {
        fprintf(stderr, "  frame_count %zu != %d\n", cnt, frames);
        return -1;
    }
    printf("  %zu Frames geschrieben\n", cnt);
    return validate_gif(path);
}

/* --- Test 3: LZW-Stress (Rauschen erzwingt Tabellen-Wachstum + Clear) ----- */
static int test_lzw_stress(void) {
    printf("[test] LZW-Stress (Rauschen 128x128, erzwingt 12-Bit + Clear/Reset)\n");
    const int w = 128, h = 128;
    const char* path = "/tmp/moo_gif_stress.gif";

    MooGifWriter* g = moo_gif_open(path, w, h, 5);
    if (!g) return -1;

    uint8_t* frame = (uint8_t*)malloc((size_t)w * (size_t)h * 4u);
    if (!frame) { moo_gif_close(g); return -1; }

    for (int i = 0; i < 3; ++i) {
        fill_noise(frame, w, h);
        if (moo_gif_add_frame(g, frame, w, h) != MOO_GIF_OK) {
            free(frame);
            moo_gif_close(g);
            return -1;
        }
    }
    free(frame);
    if (moo_gif_close(g) != MOO_GIF_OK) return -1;
    return validate_gif(path);
}

/* --- Test 4: 1x1-Pixel (Randfall, count==1 LZW-Pfad) ---------------------- */
static int test_single_pixel(void) {
    printf("[test] 1x1-Pixel-GIF (Randfall)\n");
    const char* path = "/tmp/moo_gif_1x1.gif";
    MooGifWriter* g = moo_gif_open(path, 1, 1, 1);
    if (!g) return -1;
    uint8_t px[4] = { 200u, 100u, 50u, 255u };
    if (moo_gif_add_frame(g, px, 1, 1) != MOO_GIF_OK) { moo_gif_close(g); return -1; }
    if (moo_gif_close(g) != MOO_GIF_OK) return -1;
    return validate_gif(path);
}

int main(void) {
    printf("=== test_gif_core_asan: GIF89a+LZW Encoder-Kern (P008-A3B Teil 1) ===\n");

    test_args();

    int fail = 0;
    fail |= (test_animation()    != 0);
    fail |= (test_lzw_stress()   != 0);
    fail |= (test_single_pixel() != 0);

    if (fail) {
        printf("=== FEHLER: mindestens ein Test fehlgeschlagen ===\n");
        return 1;
    }
    printf("=== ALLE TESTS GRUEN ===\n");
    return 0;
}
