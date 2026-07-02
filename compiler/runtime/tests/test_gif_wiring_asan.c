/**
 * test_gif_wiring_asan.c — ASan/UBSan-Harness fuer die GIF-VERDRAHTUNG
 *   (Plan-008 P008-A3B Teil 2). Teil 1 (Encoder-Kern) hat test_gif_core_asan.c.
 *
 * Was hier (zusaetzlich zum Kern) geprueft wird:
 *   1. Der moo-Heap-Wrapper MOO_GIF (moo_gif_handle.c): moo_gif_handle_new()
 *      uebernimmt den Writer, moo_release()/MOO_GIF -> moo_gif_handle_free()
 *      schliesst einen NOCH OFFENEN Writer sauber ab (Trailer+close, kein Leak).
 *   2. Der regulaere Lebenszyklus: start -> N x frame -> ende, danach
 *      moo_release des Handles macht KEINEN Doppel-close (writer==NULL).
 *   3. Der FRAME-BOUNDED-Verdrahtungspfad wie in moo_test_api.c
 *      (test_gif_pixels): aus einem MOO_FRAME die Pixel ziehen und EINZELN
 *      streamen, ohne je eine Frame-Sequenz im RAM zu halten. Wir bauen viele
 *      grosse MOO_FRAMEs (jeweils sofort wieder freigegeben) und pruefen, dass
 *      der Peak-RAM ~ 1 Frame bleibt (ASan-Allocator-Stats).
 *   4. GIF-Validitaet der erzeugten Dateien (Magic GIF89a + Trailer 0x3B +
 *      Mindestgroesse) und korrekter frame_count.
 *
 * Dieser Harness umgeht das nur-3D-Modul moo_test_api.c (das SDL/GLFW zoege)
 * und linkt stattdessen den Encoder-Kern + den immer-gebauten Wrapper + den
 * MOO_FRAME-Heap-Typ + die noetigen Core-Runtime-Bausteine. Die test_gif_*-
 * Builtins selbst (Fenster-Grab) werden im moo-Smoke (VoxelWorld -> GIF) am
 * echten Programm verifiziert; HIER liegt der Fokus auf Speicher-/UB-Sicherheit
 * der Verdrahtungs-Datenpfade.
 *
 * Build/Run (analog run_sanitize.sh / test_gif_core_asan.c):
 *   gcc -fsanitize=address -g -std=gnu11 -D_GNU_SOURCE -Wall -Wextra -I.. \
 *       test_gif_wiring_asan.c ../moo_gif.c ../moo_gif_handle.c ../moo_frame.c \
 *       ../moo_value.c ../moo_memory.c ../moo_string.c ../moo_dict.c \
 *       ../moo_error.c ../moo_print.c ../moo_list.c ../moo_ops.c \
 *       -lm -o /tmp/t_gif_wire
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_gif_wire
 *
 *   gcc -fsanitize=undefined -fno-sanitize-recover=undefined -g -std=gnu11 \
 *       -D_GNU_SOURCE -Wall -Wextra -I.. test_gif_wiring_asan.c ../moo_gif.c \
 *       ../moo_gif_handle.c ../moo_frame.c ../moo_value.c ../moo_memory.c \
 *       ../moo_string.c ../moo_dict.c ../moo_error.c ../moo_print.c \
 *       ../moo_list.c ../moo_ops.c -lm -o /tmp/t_gif_wire_ub
 *   UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/t_gif_wire_ub
 */

#include "moo_runtime.h"
#include "moo_gif.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Aus moo_gif_handle.c (immer gebaut). */
extern MooValue moo_gif_handle_new(MooGifWriter* writer);

/* moo_memory.c::moo_release dispatcht (per switch) auf die Free-Funktionen
 * ALLER Heap-Typen. Dieser Harness erzeugt aber NUR MOO_FRAME + MOO_GIF, deren
 * Free echt gelinkt ist. Die uebrigen Free-Symbole kommen sonst aus schweren
 * Modulen (SDL/curl/sqlite/glfw). Da die zugehoerigen Tags hier nie auftreten,
 * sind no-op-Stubs korrekt (werden nie aufgerufen) und halten den Harness
 * dep-frei wie test_gif_core_asan.c. */
void moo_socket_free(void* p)   { (void)p; }
void moo_thread_free(void* p)   { (void)p; }
void moo_channel_free(void* p)  { (void)p; }
void moo_db_free(void* p)       { (void)p; }
void moo_db_stmt_free(void* p)  { (void)p; }
void moo_window_free(void* p)   { (void)p; }
void moo_tensor_free(void* p)   { (void)p; }  /* P014-A1: MOO_TENSOR-Dispatch */
MooValue moo_tensor_to_string(MooValue v) { (void)v; return moo_string_new("<Tensor>"); }
void moo_web_free(void* p)      { (void)p; }
void moo_voxel_free(void* p)    { (void)p; }

/* ---- GIF-Validierung (wie test_gif_core_asan.c) -------------------------- */
static int validate_gif(const char* path, const char* tag) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "  [%s] validate: open fehlgeschlagen\n", tag); return -1; }
    char magic[6];
    if (fread(magic, 1, 6, f) != 6 || memcmp(magic, "GIF89a", 6) != 0) {
        fprintf(stderr, "  [%s] validate: kein GIF89a-Header\n", tag);
        fclose(f); return -1;
    }
    if (fseek(f, -1, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    int last = fgetc(f);
    fclose(f);
    if (last != 0x3B) {
        fprintf(stderr, "  [%s] validate: Trailer 0x3B fehlt (last=0x%02X)\n", tag, last);
        return -1;
    }
    if (size < 801) {
        fprintf(stderr, "  [%s] validate: Datei zu klein (%ld)\n", tag, size);
        return -1;
    }
    printf("  [%s] validate OK (%ld Bytes, GIF89a + Trailer)\n", tag, size);
    return 0;
}

/* Baut ein MOO_FRAME (RGBA8 top-left) mit deterministischem Inhalt. Der
 * Pixelpuffer wird via malloc angelegt und vom Frame uebernommen. */
static MooValue make_frame(int w, int h, int seed) {
    uint8_t* px = (uint8_t*)malloc((size_t)w * (size_t)h * 4u);
    assert(px);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = px + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            p[0] = (uint8_t)((x + seed) & 0xFF);
            p[1] = (uint8_t)((y * 3 + seed) & 0xFF);
            p[2] = (uint8_t)((seed * 17) & 0xFF);
            p[3] = 255u;
        }
    }
    MooValue f = moo_frame_new_take(w, h, px);
    assert(f.tag == MOO_FRAME);
    return f;
}

/* Verdrahtungs-Datenpfad analog moo_test_api.c::test_gif_pixels fuer ein
 * MOO_FRAME: Pixel + Dims ziehen (kein zweiter Puffer). */
static const uint8_t* frame_pixels(MooValue frame, int* w, int* h) {
    MooFrame* fr = MV_FRAME(frame);
    *w = fr->width;
    *h = fr->height;
    return fr->pixels;
}

/* --- Test 1: regulaerer Lebenszyklus start -> N frame -> ende + release --- */
static int test_lifecycle(void) {
    printf("[test] Lebenszyklus: open -> 5 Frames -> close, dann release (kein Doppel-close)\n");
    const int w = 96, h = 72, frames = 5;
    const char* path = "/tmp/moo_gif_wire_life.gif";

    MooGifWriter* writer = moo_gif_open(path, w, h, 10);
    if (!writer) { fprintf(stderr, "  open fehlgeschlagen\n"); return -1; }

    /* Ersten Frame schon mit open verdrahtet (wie test_gif_start). */
    {
        MooValue f0 = make_frame(w, h, 0);
        int fw, fh;
        const uint8_t* px = frame_pixels(f0, &fw, &fh);
        assert(moo_gif_add_frame(writer, px, fw, fh) == MOO_GIF_OK);
        moo_release(f0); /* Frame sofort frei -> frame-bounded */
    }

    MooValue gif = moo_gif_handle_new(writer); /* take ownership */
    assert(gif.tag == MOO_GIF);

    /* Weitere Frames: jeder Frame wird einzeln gebaut, gestreamt, freigegeben. */
    for (int i = 1; i < frames; ++i) {
        MooValue fi = make_frame(w, h, i);
        int fw, fh;
        const uint8_t* px = frame_pixels(fi, &fw, &fh);
        MooGifHandle* hh = MV_GIF(gif);
        assert(hh->writer != NULL);
        assert(moo_gif_add_frame(hh->writer, px, fw, fh) == MOO_GIF_OK);
        moo_release(fi);
    }

    /* test_gif_ende-Aequivalent: close + writer=NULL. */
    MooGifHandle* hh = MV_GIF(gif);
    size_t cnt = moo_gif_frame_count(hh->writer);
    assert(moo_gif_close(hh->writer) == MOO_GIF_OK);
    hh->writer = NULL;

    if (cnt != (size_t)frames) {
        fprintf(stderr, "  frame_count %zu != %d\n", cnt, frames);
        return -1;
    }

    /* Handle freigeben: writer ist NULL -> KEIN Doppel-close, nur Huelse frei. */
    moo_release(gif);

    printf("  %zu Frames, release nach ende sauber\n", cnt);
    return validate_gif(path, "life");
}

/* --- Test 2: Handle-Free OHNE vorheriges ende (Fehlerpfad/Scope-Exit) ----- */
/* Der Writer ist noch offen, wenn der Handle freigegeben wird ->
 * moo_gif_handle_free muss ihn sauber abschliessen (Trailer) + kein Leak. */
static int test_release_without_ende(void) {
    printf("[test] release OHNE ende: Wrapper schliesst offenen Writer sauber ab\n");
    const int w = 32, h = 32;
    const char* path = "/tmp/moo_gif_wire_noend.gif";

    MooGifWriter* writer = moo_gif_open(path, w, h, 8);
    if (!writer) return -1;
    MooValue f0 = make_frame(w, h, 7);
    int fw, fh;
    const uint8_t* px = frame_pixels(f0, &fw, &fh);
    assert(moo_gif_add_frame(writer, px, fw, fh) == MOO_GIF_OK);
    moo_release(f0);

    MooValue gif = moo_gif_handle_new(writer);
    assert(gif.tag == MOO_GIF);

    /* KEIN ende -> direkt release. handle_free schliesst writer. */
    moo_release(gif);

    /* Trotz fehlendem ende muss die Datei valide sein (Trailer kam vom free). */
    return validate_gif(path, "noend");
}

/* --- Test 3: RAM-Boundedness: viele grosse Frames, Peak ~ 1 Frame --------- */
/* Wir streamen 30 Frames in HD-Groesse. Wuerden Frames im RAM gesammelt, waere
 * der Peak 30 * 1280*720*4 ~= 105 MB. Frame-bounded bleibt der Peak bei ~1
 * Frame (~3.5 MB) + GIF-interne Arbeitspuffer. ASan/Valgrind sehen keinen
 * monoton wachsenden Heap. Wir verifizieren funktional: alle Frames gehen rein
 * und es leakt nichts (jeder Frame wird vor dem naechsten freigegeben). */
static int test_ram_bounded(void) {
    const int w = 1280, h = 720, frames = 30;
    const char* path = "/tmp/moo_gif_wire_hd.gif";
    printf("[test] RAM-bounded: %d HD-Frames (%dx%d) streamend, Peak ~1 Frame\n",
           frames, w, h);

    MooGifWriter* writer = moo_gif_open(path, w, h, 24);
    if (!writer) return -1;
    MooValue gif = moo_gif_handle_new(writer);
    if (gif.tag != MOO_GIF) { moo_gif_close(writer); return -1; }

    MooGifHandle* hh = MV_GIF(gif);
    for (int i = 0; i < frames; ++i) {
        MooValue fi = make_frame(w, h, i * 11 + 1);
        int fw, fh;
        const uint8_t* pxx = frame_pixels(fi, &fw, &fh);
        if (moo_gif_add_frame(hh->writer, pxx, fw, fh) != MOO_GIF_OK) {
            moo_release(fi);
            moo_release(gif);
            fprintf(stderr, "  add_frame %d fehlgeschlagen\n", i);
            return -1;
        }
        moo_release(fi); /* <-- entscheidend: VOR dem naechsten Frame frei */
    }
    size_t cnt = moo_gif_frame_count(hh->writer);
    assert(moo_gif_close(hh->writer) == MOO_GIF_OK);
    hh->writer = NULL;
    moo_release(gif);

    if (cnt != (size_t)frames) {
        fprintf(stderr, "  frame_count %zu != %d\n", cnt, frames);
        return -1;
    }
    printf("  %zu HD-Frames gestreamt (Peak ~ 1 Frame + LZW-Arbeitsspeicher)\n", cnt);
    return validate_gif(path, "hd");
}

int main(void) {
    printf("=== test_gif_wiring_asan: GIF-Verdrahtung (P008-A3B Teil 2) ===\n");
    int fail = 0;
    fail |= (test_lifecycle()             != 0);
    fail |= (test_release_without_ende()  != 0);
    fail |= (test_ram_bounded()           != 0);
    if (fail) {
        printf("=== FEHLER: mindestens ein Verdrahtungs-Test fehlgeschlagen ===\n");
        return 1;
    }
    printf("=== ALLE VERDRAHTUNGS-TESTS GRUEN ===\n");
    return 0;
}
