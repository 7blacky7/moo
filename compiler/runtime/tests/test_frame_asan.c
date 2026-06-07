/**
 * test_frame_asan.c — Standalone-ASan-Harness fuer den MOO_FRAME-Heap-Typ
 * (Plan-008 A3A, Agent p008-a3a). HARTES GATE vor A3B.
 *
 * Strategie (wie die Voxel-Harnesses): wir linken die ECHTEN moo_frame.c +
 * moo_memory.c und stubben nur das Drumherum (Heap-Konstruktoren, Dict/String,
 * die uebrigen *_free-Dispatch-Ziele). Damit testet die Harness den ECHTEN
 * is_heap_type()/moo_release()-Dispatch nach moo_frame_free — nicht eine
 * Nachbildung. Eine eigene Backend-Pixelquelle (fake_grab_rgba) liefert ein
 * deterministisches RGBA-Gradient als Frame-Inhalt (kein GL/Vulkan noetig).
 *
 * Geprueft:
 *   1. grab -> pixel -> save -> release ist LEAK-CLEAN (ASan LeakSanitizer).
 *   2. Refcount korrekt: new=1, retain=2, release=1 (kein Free), release=0 (Free).
 *   3. Doppeltes release nach Free fuehrt NICHT zu UAF (refcount-Guard
 *      *rc<=0 -> return; wir testen die sichere retain/2x-release-Sequenz).
 *   4. Pixel-Lesen liefert exakt die geschriebenen RGBA-Werte (top-left origin).
 *   5. save_bmp schreibt eine valide, nicht-leere Datei und gibt true zurueck.
 *   6. moo_frame_new_take mit NULL/ungueltigen Dims gibt NONE und leakt nicht.
 *
 * Kompilieren/Ausfuehren (Beispiel gl33-Variante; -DMOO_HAS_GL33 etc. nur fuer
 * die Build-Matrix-Doku — die Harness selbst ist backend-unabhaengig):
 *   gcc -fsanitize=address -g -std=c11 -Wall -Wextra -I.. \
 *       test_frame_asan.c ../moo_frame.c ../moo_memory.c -lm -o /tmp/t_frame
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_frame
 */

#include "moo_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ===================== moo-Runtime-Stubs ===================== */

MooValue moo_number(double n) { MooValue v; v.tag = MOO_NUMBER; moo_val_set_double(&v, n); return v; }
MooValue moo_bool(bool b)     { MooValue v; v.tag = MOO_BOOL;   v.data = (uint64_t)b; return v; }
MooValue moo_none(void)       { MooValue v; v.tag = MOO_NONE;   v.data = 0; return v; }

/* moo_throw: Tests duerfen werfen (z.B. OOB-Pixel) — wir merken es und kehren
 * via longjmp zurueck. */
#include <setjmp.h>
static jmp_buf g_throw_jmp;
static int     g_threw = 0;
void moo_throw(MooValue err) { (void)err; g_threw = 1; longjmp(g_throw_jmp, 1); }
MooValue moo_error(const char* msg) { (void)msg; return moo_none(); }

/* String: muss layout-kompatibel zu MooString sein, weil moo_frame.c
 * MV_STR(pfad)->chars liest. Wir allokieren eine echte MooString-Struktur und
 * geben Test-Strings explizit via str_destroy() frei (wir releasen sie nie ueber
 * den moo_release-Pfad, daher kein free_string noetig). */
static MooString* g_strings[64];
static int g_str_n = 0;
MooValue moo_string_new(const char* s) {
    MooString* str = (MooString*)calloc(1, sizeof(MooString));
    str->refcount = 1;
    str->length = (int32_t)strlen(s);
    str->capacity = str->length + 1;
    str->chars = (char*)malloc((size_t)str->capacity);
    memcpy(str->chars, s, (size_t)str->capacity);
    if (g_str_n < 64) g_strings[g_str_n++] = str;
    MooValue v; v.tag = MOO_STRING; moo_val_set_ptr(&v, str); return v;
}
static void strings_destroy(void) {
    for (int i = 0; i < g_str_n; i++) {
        if (g_strings[i]) { free(g_strings[i]->chars); free(g_strings[i]); g_strings[i] = NULL; }
    }
    g_str_n = 0;
}

/* Minimales Dict: zaehlt nur Eintraege, damit ASan ein echtes malloc/free sieht.
 * moo_frame_pixel_dict ruft moo_dict_new + 4x moo_dict_set. Wir geben es nach
 * Pruefung explizit frei (free_dict ist in moo_memory.c, aber MOO_DICT-Release
 * wuerde dort free_dict aufrufen -> wir releasen Dicts hier NICHT, sondern
 * lesen Werte direkt und free()en den Dict selbst). */
typedef struct { const char* k; double v; } DEntry;
typedef struct { int32_t refcount; DEntry e[8]; int n; } TestDict;
MooValue moo_dict_new(void) {
    TestDict* d = (TestDict*)calloc(1, sizeof(TestDict));
    d->refcount = 1;
    MooValue v; v.tag = MOO_DICT; moo_val_set_ptr(&v, d); return v;
}
void moo_dict_set(MooValue dict, MooValue key, MooValue val) {
    TestDict* d = (TestDict*)moo_val_as_ptr(dict);
    /* key ist ein MooString* (siehe moo_string_new) -> ->chars. */
    if (d->n < 8) { d->e[d->n].k = MV_STR(key)->chars; d->e[d->n].v = MV_NUM(val); d->n++; }
}
static double dict_get(MooValue dict, const char* k) {
    TestDict* d = (TestDict*)moo_val_as_ptr(dict);
    for (int i = 0; i < d->n; i++) if (strcmp(d->e[i].k, k) == 0) return d->e[i].v;
    return -1;
}
static void dict_destroy(MooValue dict) { free(moo_val_as_ptr(dict)); }

/* Uebrige Release-Dispatch-Ziele aus moo_memory.c — hier nie aufgerufen, aber
 * fuer den Link noetig. */
void moo_thread_free(void* p)  { (void)p; }
void moo_channel_free(void* p) { (void)p; }
void moo_db_free(void* p)      { (void)p; }
void moo_db_stmt_free(void* p) { (void)p; }
void moo_window_free(void* p)  { (void)p; }
void moo_web_free(void* p)     { (void)p; }
void moo_voxel_free(void* p)   { (void)p; }
void moo_socket_free(void* p)  { (void)p; }

/* ===================== Backend-Pixelquelle (Stub) ===================== */

/* Deterministisches RGBA-Gradient, top-left origin (wie ein echter Backend-
 * Grab nach Y-Flip/Swizzle liefern wuerde). Buffer via malloc -> moo_frame_new_take
 * uebernimmt das Eigentum. */
static uint8_t* fake_grab_rgba(int w, int h) {
    uint8_t* buf = (uint8_t*)malloc((size_t)w * (size_t)h * 4);
    if (!buf) return NULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t o = ((size_t)y * (size_t)w + (size_t)x) * 4;
            buf[o + 0] = (uint8_t)(x & 0xFF);          /* R */
            buf[o + 1] = (uint8_t)(y & 0xFF);          /* G */
            buf[o + 2] = (uint8_t)((x + y) & 0xFF);    /* B */
            buf[o + 3] = 255;                          /* A */
        }
    }
    return buf;
}

/* ===================== Tests ===================== */

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); g_fail = 1; } } while (0)

int main(void) {
    const int W = 17, H = 9; /* ungerade -> Y-Flip/Stride-Kanten testen */

    /* --- Test 1+4: grab -> Frame -> Pixel lesen (top-left exakt) --- */
    {
        uint8_t* px = fake_grab_rgba(W, H);
        CHECK(px != NULL, "fake_grab_rgba lieferte NULL");
        MooValue frame = moo_frame_new_take(W, H, px); /* uebernimmt px */
        CHECK(frame.tag == MOO_FRAME, "moo_frame_new_take ergab kein MOO_FRAME");
        MooFrame* f = MV_FRAME(frame);
        CHECK(f->width == W && f->height == H, "Frame-Dims falsch");
        CHECK(f->stride == W * 4, "Frame-Stride falsch");
        CHECK(f->refcount == 1, "Frame-Refcount nach new != 1");

        /* Pixel (0,0) = top-left: R=0,G=0,B=0,A=255 */
        if (setjmp(g_throw_jmp) == 0) {
            MooValue p = moo_frame_read_pixel(frame, moo_number(0), moo_number(0));
            CHECK(dict_get(p, "rot") == 0 && dict_get(p, "gruen") == 0 &&
                  dict_get(p, "blau") == 0 && dict_get(p, "alpha") == 255,
                  "Pixel (0,0) falsch");
            dict_destroy(p);
        } else { CHECK(0, "unerwarteter throw bei Pixel (0,0)"); }

        /* Pixel (3,5): R=3,G=5,B=8,A=255 */
        if (setjmp(g_throw_jmp) == 0) {
            MooValue p = moo_frame_read_pixel(frame, moo_number(3), moo_number(5));
            CHECK(dict_get(p, "rot") == 3 && dict_get(p, "gruen") == 5 &&
                  dict_get(p, "blau") == 8 && dict_get(p, "alpha") == 255,
                  "Pixel (3,5) falsch");
            dict_destroy(p);
        } else { CHECK(0, "unerwarteter throw bei Pixel (3,5)"); }

        /* OOB muss werfen (kein OOB-Read -> ASan-sauber). */
        g_threw = 0;
        if (setjmp(g_throw_jmp) == 0) {
            MooValue p = moo_frame_read_pixel(frame, moo_number(W), moo_number(0));
            (void)p;
            CHECK(0, "OOB-Pixel warf nicht");
        } else { CHECK(g_threw, "OOB-Pixel: throw-Flag nicht gesetzt"); }

        /* --- Test 5: save_bmp valide, nicht-leer --- */
        g_threw = 0;
        const char* path = "/tmp/moo_frame_asan_test.bmp";
        if (setjmp(g_throw_jmp) == 0) {
            MooValue r = moo_test_frame_save_bmp(frame, moo_string_new(path));
            CHECK(r.tag == MOO_BOOL && MV_BOOL(r), "save_bmp gab nicht true");
        } else { CHECK(0, "save_bmp warf unerwartet"); }
        FILE* fp = fopen(path, "rb");
        CHECK(fp != NULL, "BMP-Datei nicht erstellt");
        if (fp) {
            unsigned char sig[2] = {0,0};
            size_t n = fread(sig, 1, 2, fp);
            CHECK(n == 2 && sig[0] == 'B' && sig[1] == 'M', "BMP-Signatur falsch");
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            CHECK(sz > 54, "BMP-Datei zu klein (kein Pixel-Daten)");
            fclose(fp);
        }
        remove(path);

        /* --- Test 2+3: Refcount + sichere Doppel-Release --- */
        moo_retain(frame);              /* rc 1 -> 2 */
        CHECK(MV_FRAME(frame)->refcount == 2, "retain: rc != 2");
        moo_release(frame);             /* rc 2 -> 1, KEIN Free */
        CHECK(MV_FRAME(frame)->refcount == 1, "release(1): rc != 1");
        moo_release(frame);             /* rc 1 -> 0, FREE (px + struct) */
        /* Ab hier ist frame ungueltig — wir fassen es NICHT mehr an.
         * ASan wuerde ein UAF/Double-Free hier melden. */
    }

    /* --- Test 6: NULL/ungueltige Dims -> NONE, kein Leak --- */
    {
        MooValue n1 = moo_frame_new_take(W, H, NULL);          /* pixels NULL */
        CHECK(n1.tag == MOO_NONE, "new_take(NULL) gab nicht NONE");
        uint8_t* px = fake_grab_rgba(4, 4);
        MooValue n2 = moo_frame_new_take(0, 4, px);            /* width 0 -> px wird gefreet */
        CHECK(n2.tag == MOO_NONE, "new_take(w=0) gab nicht NONE");
    }

    strings_destroy();  /* Test-Strings freigeben (kein Leak) */

    if (g_fail) { printf("\n=== MOO_FRAME ASan-Harness: FEHLGESCHLAGEN ===\n"); return 1; }
    printf("=== MOO_FRAME ASan-Harness: ALLE TESTS BESTANDEN ===\n");
    return 0;
}
