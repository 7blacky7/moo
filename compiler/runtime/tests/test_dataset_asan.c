/**
 * test_dataset_asan.c — Plan-014 E1: Daten-Pipeline (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES); Test-throw-Modell.
 *
 * PRUEFT (alle Fixtures werden vom Harness selbst nach /tmp geschrieben —
 * kein echtes MNIST im CI):
 *   1. mnist_laden: Mini-IDX-Fixture (2 Bilder 2x2 + 2 Labels) —
 *      Header BE korrekt gelesen, Werte /255, Dict-Form; kaputte Magic
 *      und zu kurze Datei werfen erklaerend
 *   2. datensatz_csv: Kopfzeile wird uebersprungen, Komma/Semikolon,
 *      Werte exakt; inkonsistente Spaltenzahl wirft mit Zeilennummer
 *   3. bild_laden: PGM P5 (binaer, /maxval) und PPM P3 (ASCII, [h,w,3]),
 *      #-Kommentare im Kopf; unbekanntes Format wirft
 *   4. mischen: seed-deterministisch (2 Laeufe identisch), Paare
 *      (x-Zeile, y-Wert) bleiben zusammen, echte Permutation
 *   5. normalisieren: minmax auf 0..1 handgerechnet, standard hat
 *      Mittel ~0 / Streuung ~1; konstante Daten crashen nicht
 *   6. text_tokenizer (P014-G1): Codepoints statt Bytes (Umlaut-Beweis),
 *      Ids nach Codepoint sortiert (handgeprueft), bit-genauer Roundtrip,
 *      zeichen_zu_id<->id_zu_zeichen konsistent; kaputtes UTF-8 (Truncated/
 *      Overlong), leerer Text und Nicht-Text werfen erklaerend
 * ============================================================================
 */
#include "../moo_runtime.h"

/* --- Test-throw-Modell (ersetzt moo_error.c) ------------------------------ */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    moo_release(error);
    moo_error_flag = 1;
}
static void fehler_reset(void) { moo_error_flag = 0; }

/* --- Stubs fuer moo_release()-Dispatch-Ziele ------------------------------ */
void moo_socket_free(void* p)       { (void)p; }
void moo_thread_free(void* p)       { (void)p; }
void moo_channel_free(void* p)      { (void)p; }
void moo_db_free(void* p)           { (void)p; }
void moo_db_stmt_free(void* p)      { (void)p; }
void moo_window_free(void* p)       { (void)p; }
void moo_web_free(void* p)          { (void)p; }
void moo_voxel_free(void* p)        { (void)p; }
void moo_frame_free(void* p)        { (void)p; }
void moo_gif_handle_free(void* p)   { (void)p; }
void moo_video_handle_free(void* p) { (void)p; }

static int checks = 0;
#define CHECK(cond, name) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__); return 1; } \
    checks++; \
} while (0)
#define NAHE(x, y) (fabs((double)(x) - (double)(y)) < 1e-5)

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

static MooValue str_(const char* s) { return moo_string_new(s); }
static MooValue dget_(MooValue d, const char* k) {
    return moo_dict_get(d, moo_string_new(k));
}

/* u32 als big-endian in Datei schreiben. */
static void schreib_u32be(FILE* f, uint32_t v) {
    unsigned char b[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16),
                           (unsigned char)(v >> 8), (unsigned char)v };
    fwrite(b, 1, 4, f);
}

int main(void) {
    /* ===== Fixtures schreiben ===== */
    /* Mini-MNIST: 2 Bilder 2x2 (Werte 0,51,102,153 / 204,255,0,51), Labels 3,7 */
    {
        FILE* f = fopen("/tmp/moo_ds_test-images-idx3-ubyte", "wb");
        CHECK(f != NULL, "Fixture images schreibbar");
        schreib_u32be(f, 2051); schreib_u32be(f, 2);
        schreib_u32be(f, 2); schreib_u32be(f, 2);
        unsigned char px[8] = { 0, 51, 102, 153, 204, 255, 0, 51 };
        fwrite(px, 1, 8, f);
        fclose(f);
        f = fopen("/tmp/moo_ds_test-labels-idx1-ubyte", "wb");
        CHECK(f != NULL, "Fixture labels schreibbar");
        schreib_u32be(f, 2049); schreib_u32be(f, 2);
        unsigned char lb[2] = { 3, 7 };
        fwrite(lb, 1, 2, f);
        fclose(f);
    }

    /* ===== 1. mnist_laden ===== */
    {
        MooValue p = str_("/tmp/moo_ds_test");
        MooValue d = moo_ds_mnist(p);
        moo_release(p);
        CHECK(d.tag == MOO_DICT, "mnist_laden liefert Dict");
        MooValue b = dget_(d, "bilder");
        MooValue l = dget_(d, "labels");
        MooValue br = dget_(d, "breite");
        CHECK(b.tag == MOO_TENSOR && T(b)->ndim == 2 &&
              T(b)->shape[0] == 2 && T(b)->shape[1] == 4, "bilder [2,4]");
        CHECK(l.tag == MOO_TENSOR && T(l)->ndim == 1 && T(l)->shape[0] == 2,
              "labels [2]");
        CHECK(br.tag == MOO_NUMBER && NAHE(MV_NUM(br), 2.0), "breite == 2");
        CHECK(NAHE(T(b)->data[0], 0.0) && NAHE(T(b)->data[1], 51.0f / 255.0f) &&
              NAHE(T(b)->data[5], 1.0), "Pixel /255 normalisiert");
        CHECK(NAHE(T(l)->data[0], 3.0) && NAHE(T(l)->data[1], 7.0),
              "Labels 3 und 7");
        moo_release(br); moo_release(l); moo_release(b); moo_release(d);
    }
    /* kaputte Magic wirft */
    fehler_reset();
    {
        FILE* f = fopen("/tmp/moo_ds_bad-images-idx3-ubyte", "wb");
        schreib_u32be(f, 1234); schreib_u32be(f, 1);
        schreib_u32be(f, 2); schreib_u32be(f, 2);
        fclose(f);
        MooValue p = str_("/tmp/moo_ds_bad");
        MooValue d = moo_ds_mnist(p);
        moo_release(p);
        CHECK(moo_error_flag == 1 && d.tag == MOO_NONE, "kaputte Magic wirft");
        fehler_reset();
        remove("/tmp/moo_ds_bad-images-idx3-ubyte");
    }
    /* zu kurze Bilddatei wirft */
    {
        FILE* f = fopen("/tmp/moo_ds_kurz-images-idx3-ubyte", "wb");
        schreib_u32be(f, 2051); schreib_u32be(f, 2);
        schreib_u32be(f, 2); schreib_u32be(f, 2);
        unsigned char px[3] = { 1, 2, 3 };   /* statt 8 Bytes */
        fwrite(px, 1, 3, f);
        fclose(f);
        f = fopen("/tmp/moo_ds_kurz-labels-idx1-ubyte", "wb");
        schreib_u32be(f, 2049); schreib_u32be(f, 2);
        unsigned char lb[2] = { 0, 1 };
        fwrite(lb, 1, 2, f);
        fclose(f);
        MooValue p = str_("/tmp/moo_ds_kurz");
        MooValue d = moo_ds_mnist(p);
        moo_release(p);
        CHECK(moo_error_flag == 1 && d.tag == MOO_NONE, "zu kurze Datei wirft");
        fehler_reset();
        remove("/tmp/moo_ds_kurz-images-idx3-ubyte");
        remove("/tmp/moo_ds_kurz-labels-idx1-ubyte");
    }

    /* ===== 2. datensatz_csv ===== */
    {
        FILE* f = fopen("/tmp/moo_ds_test.csv", "w");
        fprintf(f, "alter,groesse,gewicht\n1,2,3\n4.5, 5 ;6\n\n7,8,9\n");
        fclose(f);
        MooValue p = str_("/tmp/moo_ds_test.csv");
        MooValue t = moo_ds_csv(p);
        moo_release(p);
        CHECK(t.tag == MOO_TENSOR && T(t)->shape[0] == 3 && T(t)->shape[1] == 3,
              "csv: Kopfzeile uebersprungen, [3,3]");
        CHECK(NAHE(T(t)->data[0], 1.0) && NAHE(T(t)->data[3], 4.5) &&
              NAHE(T(t)->data[5], 6.0) && NAHE(T(t)->data[8], 9.0),
              "csv: Werte exakt (Komma UND Semikolon)");
        moo_release(t);
        remove("/tmp/moo_ds_test.csv");
    }
    fehler_reset();
    {
        FILE* f = fopen("/tmp/moo_ds_schief.csv", "w");
        fprintf(f, "1,2,3\n4,5\n");
        fclose(f);
        MooValue p = str_("/tmp/moo_ds_schief.csv");
        MooValue t = moo_ds_csv(p);
        moo_release(p);
        CHECK(moo_error_flag == 1 && t.tag == MOO_NONE,
              "csv: schiefe Spaltenzahl wirft");
        fehler_reset();
        remove("/tmp/moo_ds_schief.csv");
    }

    /* ===== 3. bild_laden ===== */
    {
        /* PGM P5: 3x2, maxval 255, ein #-Kommentar */
        FILE* f = fopen("/tmp/moo_ds_test.pgm", "wb");
        fprintf(f, "P5\n# testbild\n3 2\n255\n");
        unsigned char px[6] = { 0, 128, 255, 64, 32, 16 };
        fwrite(px, 1, 6, f);
        fclose(f);
        MooValue p = str_("/tmp/moo_ds_test.pgm");
        MooValue t = moo_ds_bild(p);
        moo_release(p);
        CHECK(t.tag == MOO_TENSOR && T(t)->ndim == 2 &&
              T(t)->shape[0] == 2 && T(t)->shape[1] == 3, "pgm: [h=2, w=3]");
        CHECK(NAHE(T(t)->data[0], 0.0) && NAHE(T(t)->data[2], 1.0) &&
              NAHE(T(t)->data[3], 64.0 / 255.0), "pgm: Werte /maxval");
        moo_release(t);
        remove("/tmp/moo_ds_test.pgm");
    }
    {
        /* PPM P3 (ASCII): 2x1 RGB */
        FILE* f = fopen("/tmp/moo_ds_test.ppm", "w");
        fprintf(f, "P3\n2 1\n255\n255 0 0  0 255 0\n");
        fclose(f);
        MooValue p = str_("/tmp/moo_ds_test.ppm");
        MooValue t = moo_ds_bild(p);
        moo_release(p);
        CHECK(t.tag == MOO_TENSOR && T(t)->ndim == 3 && T(t)->shape[0] == 1 &&
              T(t)->shape[1] == 2 && T(t)->shape[2] == 3, "ppm: [1,2,3]");
        CHECK(NAHE(T(t)->data[0], 1.0) && NAHE(T(t)->data[1], 0.0) &&
              NAHE(T(t)->data[4], 1.0), "ppm: RGB-Werte korrekt");
        moo_release(t);
        remove("/tmp/moo_ds_test.ppm");
    }
    fehler_reset();
    {
        FILE* f = fopen("/tmp/moo_ds_test.png", "wb");
        fprintf(f, "\x89PNG unfug");
        fclose(f);
        MooValue p = str_("/tmp/moo_ds_test.png");
        MooValue t = moo_ds_bild(p);
        moo_release(p);
        CHECK(moo_error_flag == 1 && t.tag == MOO_NONE,
              "bild_laden: PNG wirft erklaerend");
        fehler_reset();
        remove("/tmp/moo_ds_test.png");
    }

    /* ===== 4. mischen ===== */
    {
        int32_t xs[2] = { 6, 2 };
        MooTensor* xt = moo_tensor_raw(2, xs);
        int32_t ys[1] = { 6 };
        MooTensor* yt = moo_tensor_raw(1, ys);
        for (int i = 0; i < 6; i++) {
            xt->data[i * 2] = (float)i;          /* Zeile r traegt Wert r */
            xt->data[i * 2 + 1] = (float)i * 10;
            yt->data[i] = (float)i;              /* Label == Zeilen-Wert */
        }
        MooValue x; x.tag = MOO_TENSOR; moo_val_set_ptr(&x, xt);
        MooValue y; y.tag = MOO_TENSOR; moo_val_set_ptr(&y, yt);
        MooValue s = moo_number(123.0);
        MooValue l1 = moo_ds_mischen(x, y, s);
        MooValue l2 = moo_ds_mischen(x, y, s);
        CHECK(l1.tag == MOO_LIST && MV_LIST(l1)->length == 2,
              "mischen: Liste [x, y]");
        MooTensor* a1 = T(MV_LIST(l1)->items[0]);
        MooTensor* b1 = T(MV_LIST(l1)->items[1]);
        MooTensor* a2 = T(MV_LIST(l2)->items[0]);
        bool det = true, paare = true, permutiert = false;
        for (int i = 0; i < 6; i++) {
            if (a1->data[i * 2] != a2->data[i * 2]) det = false;
            if (a1->data[i * 2] != b1->data[i]) paare = false;   /* zusammen? */
            if (b1->data[i] != (float)i) permutiert = true;
            if (a1->data[i * 2 + 1] != a1->data[i * 2] * 10) paare = false;
        }
        CHECK(det, "mischen: gleicher Seed -> gleiche Reihenfolge");
        CHECK(paare, "mischen: (x-Zeile, y-Wert)-Paare bleiben zusammen");
        CHECK(permutiert, "mischen: Reihenfolge tatsaechlich veraendert");
        moo_release(l1); moo_release(l2);
        moo_release(x); moo_release(y);
    }

    /* ===== 5. normalisieren ===== */
    {
        int32_t sh[2] = { 2, 2 };
        MooTensor* t = moo_tensor_raw(2, sh);
        t->data[0] = 2; t->data[1] = 4; t->data[2] = 6; t->data[3] = 10;
        MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
        MooValue mm = moo_ds_normalisieren(v, moo_none());
        CHECK(mm.tag == MOO_TENSOR, "normalisieren minmax laeuft");
        CHECK(NAHE(T(mm)->data[0], 0.0) && NAHE(T(mm)->data[1], 0.25) &&
              NAHE(T(mm)->data[3], 1.0), "minmax: (t-2)/8 handgerechnet");
        MooValue art = str_("standard");
        MooValue st = moo_ds_normalisieren(v, art);
        moo_release(art);
        double mu = 0, q = 0;
        for (int i = 0; i < 4; i++) mu += T(st)->data[i];
        mu /= 4.0;
        for (int i = 0; i < 4; i++) {
            double d = T(st)->data[i] - mu;
            q += d * d;
        }
        CHECK(fabs(mu) < 1e-6 && NAHE(sqrt(q / 4.0), 1.0),
              "standard: Mittel 0, Streuung 1");
        moo_release(mm); moo_release(st); moo_release(v);
    }
    {
        /* konstante Daten: kein Div-durch-0 */
        int32_t sh[1] = { 3 };
        MooTensor* t = moo_tensor_raw(1, sh);
        t->data[0] = t->data[1] = t->data[2] = 5.0f;
        MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
        MooValue mm = moo_ds_normalisieren(v, moo_none());
        CHECK(mm.tag == MOO_TENSOR && NAHE(T(mm)->data[0], 0.0),
              "minmax: konstante Daten -> 0, kein Crash");
        moo_release(mm); moo_release(v);
    }

    /* ===== 6. text_tokenizer (UTF-8-Codepoints, P014-G1) ===== */
    {
        /* "Bär,ä€": 1-Byte (B, r, Komma), 2-Byte (ä zweimal), 3-Byte (€)
         * = 6 Codepoints in 10 Bytes. Vokab sortiert nach Codepoint:
         * ','(0x2C) 'B'(0x42) 'r'(0x72) 'ä'(0xE4) '€'(0x20AC) => 5. */
        MooValue txt = moo_string_new_len("B\xC3\xA4r,\xC3\xA4\xE2\x82\xAC", 10);
        MooValue tok = moo_ds_tokenizer(txt);
        CHECK(!moo_error_flag && tok.tag == MOO_DICT,
              "tokenizer: gibt Dict zurueck");
        MooValue ids = dget_(tok, "ids");
        MooValue vok = dget_(tok, "vokab");
        MooValue i2z = dget_(tok, "id_zu_zeichen");
        MooValue z2i = dget_(tok, "zeichen_zu_id");
        CHECK(ids.tag == MOO_TENSOR && T(ids)->ndim == 1 && T(ids)->size == 6,
              "tokenizer: 6 CODEPOINTS, nicht 9 Bytes");
        CHECK(vok.tag == MOO_NUMBER && MV_NUM(vok) == 5.0,
              "tokenizer: Vokab 5 (ae nur einmal gezaehlt)");
        float erwartet[6] = { 1, 3, 2, 0, 3, 4 };   /* B ä r , ä € */
        bool idord = true;
        for (int k = 0; k < 6; k++)
            if (T(ids)->data[k] != erwartet[k]) idord = false;
        CHECK(idord, "tokenizer: Ids nach Codepoint sortiert, handgeprueft");
        /* Roundtrip: ids -> id_zu_zeichen -> Bytes == Original, bit-genau */
        const char* orig = MV_STR(txt)->chars;
        int32_t off = 0;
        bool rt = true;
        for (int64_t k = 0; k < T(ids)->size && rt; k++) {
            MooValue z = moo_list_get(i2z, moo_number((double)T(ids)->data[k]));
            if (z.tag != MOO_STRING) { rt = false; break; }
            int32_t zl = MV_STR(z)->length;
            if (off + zl > MV_STR(txt)->length ||
                memcmp(orig + off, MV_STR(z)->chars, (size_t)zl) != 0) rt = false;
            off += zl;
            moo_release(z);
        }
        CHECK(rt && off == MV_STR(txt)->length,
              "tokenizer: Umlaut-Roundtrip bit-genau");
        /* zeichen_zu_id <-> id_zu_zeichen konsistent (fuer 'ä') */
        MooValue ae = moo_string_new_len("\xC3\xA4", 2);
        MooValue idv = moo_dict_get(z2i, ae);   /* dict_get konsumiert den Key */
        CHECK(idv.tag == MOO_NUMBER, "tokenizer: zeichen_zu_id kennt ae");
        MooValue back = moo_list_get(i2z, idv);
        CHECK(back.tag == MOO_STRING && MV_STR(back)->length == 2 &&
              memcmp(MV_STR(back)->chars, "\xC3\xA4", 2) == 0,
              "tokenizer: id_zu_zeichen[zeichen_zu_id[ae]] == ae");
        moo_release(back);
        moo_release(ids); moo_release(vok); moo_release(i2z); moo_release(z2i);
        moo_release(tok); moo_release(txt);
    }
    {
        /* Fehlerpfade: alle werfen erklaerend, keiner crasht */
        MooValue kaputt = moo_string_new_len("a\xC3", 2);   /* abgeschnitten */
        MooValue r1 = moo_ds_tokenizer(kaputt);
        CHECK(moo_error_flag && r1.tag == MOO_NONE,
              "tokenizer: abgeschnittene UTF-8-Sequenz wirft");
        fehler_reset(); moo_release(kaputt);
        MooValue ovl = moo_string_new_len("\xC0\x80", 2);   /* Overlong U+0000 */
        MooValue r2 = moo_ds_tokenizer(ovl);
        CHECK(moo_error_flag && r2.tag == MOO_NONE,
              "tokenizer: Overlong-Form wirft");
        fehler_reset(); moo_release(ovl);
        MooValue leer = str_("");
        MooValue r3 = moo_ds_tokenizer(leer);
        CHECK(moo_error_flag && r3.tag == MOO_NONE,
              "tokenizer: leerer Text wirft");
        fehler_reset(); moo_release(leer);
        MooValue r4 = moo_ds_tokenizer(moo_number(5));
        CHECK(moo_error_flag && r4.tag == MOO_NONE,
              "tokenizer: Zahl statt Text wirft");
        fehler_reset();
    }

    remove("/tmp/moo_ds_test-images-idx3-ubyte");
    remove("/tmp/moo_ds_test-labels-idx1-ubyte");

    printf("test_dataset_asan: alle %d Checks bestanden\n", checks);
    return 0;
}
