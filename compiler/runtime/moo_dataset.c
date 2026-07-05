/**
 * moo_dataset.c — Daten-Pipeline (Plan-014 E1): MNIST-IDX, CSV, PGM/PPM,
 * Mischen, Normalisieren. Plan-014 G1: Zeichen-Tokenizer (UTF-8-Codepoints).
 * ============================================================================
 * ENTSCHEIDE (E1):
 *   * Bild-Eingabe: EIGENER minimaler PGM/PPM-Reader (P2/P5 grau, P3/P6 RGB).
 *     SDL_image ist 3D-Feature-gated und stb_image-Vendoring waere fuer den
 *     Zweck Overkill — das raytracer-Beispiel schreibt bereits PPM, damit ist
 *     das Format im Projekt etabliert. PNG/JPG = Backlog.
 *   * MNIST-IDX in pure C (kein Dep): u32-BE-Header byteweise gelesen
 *     (UB-sicher, kein Aliasing-Cast). Download + Checksummen macht das
 *     separate skripte/mnist_download.sh — der Loader liest die ENTPACKTEN
 *     Dateien.
 *   * mischen/normalisieren arbeiten OHNE Autograd-Tape (rohe Daten-
 *     Vorverarbeitung, kein grad noetig) und sind seed-deterministisch
 *     (splitmix64, Muster moo_noise/moo_nn).
 * Tensor-Konvention: Args borrowed, Rueckgaben +1. UB-Policy: Groessen
 * int64_t/size_t, Byte-Reads unsigned.
 * ============================================================================
 */
#include "moo_runtime.h"
#include <math.h>

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

static uint64_t dsm64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* u32 big-endian byteweise (kein Cast-Aliasing). false bei EOF. */
static bool lese_u32be(FILE* f, uint32_t* out) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    return true;
}

/* ============================================================
 * MNIST (IDX-Format)
 * ============================================================ */

/* mnist_laden(prefix): liest <prefix>-images-idx3-ubyte und
 * <prefix>-labels-idx1-ubyte. Rueckgabe Dict {bilder: [n,784] (0..1),
 * labels: [n]}. Beispiel: mnist_laden("daten/mnist/train"). */
MooValue moo_ds_mnist(MooValue prefix) {
    if (prefix.tag != MOO_STRING) {
        moo_throw(moo_error("mnist_laden: erwarte einen Pfad-Prefix als Text, "
                            "z.B. mnist_laden(\"daten/mnist/train\")"));
        return moo_none();
    }
    char pfad_img[512], pfad_lbl[512];
    snprintf(pfad_img, sizeof(pfad_img), "%s-images-idx3-ubyte",
             MV_STR(prefix)->chars);
    snprintf(pfad_lbl, sizeof(pfad_lbl), "%s-labels-idx1-ubyte",
             MV_STR(prefix)->chars);

    FILE* fi = fopen(pfad_img, "rb");
    if (!fi) {
        char msg[600];
        snprintf(msg, sizeof(msg), "mnist_laden: kann \"%s\" nicht oeffnen — "
                 "erst skripte/mnist_download.sh laufen lassen?", pfad_img);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    uint32_t magic = 0, n = 0, rows = 0, cols = 0;
    bool ok = lese_u32be(fi, &magic) && lese_u32be(fi, &n) &&
              lese_u32be(fi, &rows) && lese_u32be(fi, &cols);
    if (!ok || magic != 2051 || rows == 0 || cols == 0 || n == 0 ||
        rows > 4096 || cols > 4096) {
        fclose(fi);
        moo_throw(moo_error("mnist_laden: die Bilddatei ist kein IDX3-Format "
                            "(noch gepackt? -> erst entpacken)"));
        return moo_none();
    }
    FILE* fl = fopen(pfad_lbl, "rb");
    if (!fl) {
        fclose(fi);
        char msg[600];
        snprintf(msg, sizeof(msg), "mnist_laden: kann \"%s\" nicht oeffnen "
                 "(Bilder da, Labels fehlen)", pfad_lbl);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    uint32_t lmagic = 0, ln = 0;
    ok = lese_u32be(fl, &lmagic) && lese_u32be(fl, &ln);
    if (!ok || lmagic != 2049 || ln != n) {
        fclose(fi); fclose(fl);
        moo_throw(moo_error("mnist_laden: Labeldatei passt nicht zu den "
                            "Bildern (Anzahl verschieden oder kein IDX1)"));
        return moo_none();
    }

    /* rows/cols oben auf <=4096 geprueft -> Produkt passt sicher in int64
     * (max 16.7M), kein checked_mul noetig. n-Grenze gegen Unfug. */
    if (n > 10000000u) {
        fclose(fi); fclose(fl);
        moo_throw(moo_error("mnist_laden: Bildgroesse unplausibel"));
        return moo_none();
    }
    int64_t sp = (int64_t)rows * (int64_t)cols;
    int64_t gesamt = (int64_t)n * sp;

    int32_t bshape[2] = { (int32_t)n, (int32_t)sp };
    MooTensor* bt = moo_tensor_raw(2, bshape);
    int32_t lshape[1] = { (int32_t)n };
    MooTensor* lt = moo_tensor_raw(1, lshape);
    unsigned char* zeile = (unsigned char*)malloc((size_t)sp);
    if (!bt || !lt || !zeile) {
        if (bt) { MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, bt); moo_release(v); }
        if (lt) { MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, lt); moo_release(v); }
        free(zeile); fclose(fi); fclose(fl);
        moo_throw(moo_error("mnist_laden: nicht genug Speicher fuer den "
                            "Datensatz"));
        return moo_none();
    }
    ok = true;
    for (int64_t r = 0; r < (int64_t)n && ok; r++) {
        ok = fread(zeile, 1, (size_t)sp, fi) == (size_t)sp;
        if (!ok) break;
        float* ziel = bt->data + r * sp;
        for (int64_t i = 0; i < sp; i++)
            ziel[i] = (float)zeile[i] / 255.0f;   /* 0..1 normalisiert */
    }
    if (ok) {
        for (int64_t r = 0; r < (int64_t)n && ok; r++) {
            int c = fgetc(fl);
            ok = (c != EOF);
            if (ok) lt->data[r] = (float)(unsigned char)c;
        }
    }
    free(zeile); fclose(fi); fclose(fl);
    (void)gesamt;

    MooValue bv; bv.tag = MOO_TENSOR; moo_val_set_ptr(&bv, bt);
    MooValue lv; lv.tag = MOO_TENSOR; moo_val_set_ptr(&lv, lt);
    if (!ok) {
        moo_release(bv); moo_release(lv);
        moo_throw(moo_error("mnist_laden: Datei zu kurz — Download "
                            "abgebrochen? Nochmal skripte/mnist_download.sh"));
        return moo_none();
    }
    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("bilder"), bv);
    moo_dict_set(d, moo_string_new("labels"), lv);
    moo_dict_set(d, moo_string_new("breite"), moo_number((double)cols));
    moo_dict_set(d, moo_string_new("hoehe"), moo_number((double)rows));
    return d;
}

/* ============================================================
 * CSV
 * ============================================================ */

/* datensatz_csv(pfad): Zahlen-CSV -> Tensor [zeilen, spalten].
 * Trennzeichen , oder ; — eine nicht-numerische ERSTE Zeile wird als
 * Kopfzeile uebersprungen. */
MooValue moo_ds_csv(MooValue pfad) {
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_error("datensatz_csv: erwarte einen Dateinamen als Text"));
        return moo_none();
    }
    FILE* f = fopen(MV_STR(pfad)->chars, "r");
    if (!f) {
        char msg[600];
        snprintf(msg, sizeof(msg), "datensatz_csv: kann \"%s\" nicht oeffnen",
                 MV_STR(pfad)->chars);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    float* werte = NULL;
    size_t cap = 0, len = 0;
    int64_t spalten = -1, zeilen = 0;
    char* line = NULL;
    size_t lcap = 0;
    ssize_t ll;
    int64_t zeilen_nr = 0;
    bool fehler = false;
    char fmsg[240] = {0};

    while ((ll = getline(&line, &lcap, f)) != -1 && !fehler) {
        zeilen_nr++;
        /* Zeile in Zahlen zerlegen */
        int64_t sp = 0;
        char* p = line;
        bool zeile_ok = true;
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0' || *p == '\n' || *p == '\r') break;
            char* ende = NULL;
            double v = strtod(p, &ende);
            if (ende == p) { zeile_ok = false; break; }
            if (len == cap) {
                size_t nc = cap ? cap * 2 : 1024;
                float* nw = (float*)realloc(werte, nc * sizeof(float));
                if (!nw) {
                    snprintf(fmsg, sizeof(fmsg),
                             "datensatz_csv: nicht genug Speicher");
                    fehler = true; zeile_ok = false; break;
                }
                werte = nw; cap = nc;
            }
            werte[len++] = (float)v;
            sp++;
            p = ende;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == ',' || *p == ';') p++;
        }
        if (fehler) break;
        if (!zeile_ok) {
            if (zeilen_nr == 1 && zeilen == 0) {
                continue;   /* Kopfzeile — ueberspringen, nichts uebernommen */
            }
            snprintf(fmsg, sizeof(fmsg), "datensatz_csv: Zeile %lld enthaelt "
                     "etwas, das keine Zahl ist", (long long)zeilen_nr);
            fehler = true;
            break;
        }
        if (sp == 0) continue;   /* Leerzeile */
        if (spalten < 0) spalten = sp;
        else if (sp != spalten) {
            snprintf(fmsg, sizeof(fmsg), "datensatz_csv: Zeile %lld hat %lld "
                     "Werte, davor waren es %lld — alle Zeilen muessen gleich "
                     "lang sein", (long long)zeilen_nr, (long long)sp,
                     (long long)spalten);
            fehler = true;
            break;
        }
        zeilen++;
    }
    free(line);
    fclose(f);
    if (!fehler && (zeilen == 0 || spalten <= 0)) {
        snprintf(fmsg, sizeof(fmsg), "datensatz_csv: keine Datenzeilen "
                 "gefunden");
        fehler = true;
    }
    if (fehler) {
        free(werte);
        moo_throw(moo_error(fmsg));
        return moo_none();
    }
    int32_t shape[2] = { (int32_t)zeilen, (int32_t)spalten };
    MooTensor* t = moo_tensor_raw(2, shape);
    if (!t) {
        free(werte);
        moo_throw(moo_error("datensatz_csv: nicht genug Speicher"));
        return moo_none();
    }
    memcpy(t->data, werte, (size_t)zeilen * (size_t)spalten * sizeof(float));
    free(werte);
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

/* ============================================================
 * PGM/PPM (P2/P5 grau, P3/P6 RGB)
 * ============================================================ */

/* Naechstes Zahl-Token (Whitespace + #-Kommentare ueberspringen). */
static bool ppm_zahl(FILE* f, long* out) {
    int c;
    for (;;) {
        c = fgetc(f);
        if (c == EOF) return false;
        if (c == '#') {   /* Kommentar bis Zeilenende */
            while (c != EOF && c != '\n') c = fgetc(f);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        break;
    }
    long v = 0;
    bool ziffer = false;
    while (c != EOF && c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        ziffer = true;
        if (v > 1000000000L) return false;
        c = fgetc(f);
    }
    if (!ziffer) return false;
    *out = v;
    return true;
}

/* bild_laden(pfad): PGM -> [h,w], PPM -> [h,w,3]; Werte 0..1. */
MooValue moo_ds_bild(MooValue pfad) {
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_error("bild_laden: erwarte einen Dateinamen als Text"));
        return moo_none();
    }
    FILE* f = fopen(MV_STR(pfad)->chars, "rb");
    if (!f) {
        char msg[600];
        snprintf(msg, sizeof(msg), "bild_laden: kann \"%s\" nicht oeffnen",
                 MV_STR(pfad)->chars);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    int m0 = fgetc(f), m1 = fgetc(f);
    int variante = 0;   /* 2/3 = ASCII, 5/6 = binaer */
    if (m0 == 'P' && (m1 == '2' || m1 == '3' || m1 == '5' || m1 == '6'))
        variante = m1 - '0';
    if (!variante) {
        fclose(f);
        moo_throw(moo_error("bild_laden: kann nur PGM (P2/P5) und PPM "
                            "(P3/P6) lesen — PNG/JPG bitte vorher umwandeln"));
        return moo_none();
    }
    bool farbe = (variante == 3 || variante == 6);
    bool binaer = (variante == 5 || variante == 6);
    long w = 0, h = 0, maxv = 0;
    if (!ppm_zahl(f, &w) || !ppm_zahl(f, &h) || !ppm_zahl(f, &maxv) ||
        w <= 0 || h <= 0 || w > 32768 || h > 32768 || maxv <= 0 || maxv > 65535) {
        fclose(f);
        moo_throw(moo_error("bild_laden: kaputter PGM/PPM-Kopf"));
        return moo_none();
    }
    int32_t kanaele = farbe ? 3 : 1;
    int64_t n = (int64_t)w * (int64_t)h * kanaele;
    MooTensor* t;
    if (farbe) {
        int32_t shape[3] = { (int32_t)h, (int32_t)w, 3 };
        t = moo_tensor_raw(3, shape);
    } else {
        int32_t shape[2] = { (int32_t)h, (int32_t)w };
        t = moo_tensor_raw(2, shape);
    }
    if (!t) {
        fclose(f);
        moo_throw(moo_error("bild_laden: nicht genug Speicher"));
        return moo_none();
    }
    float skala = 1.0f / (float)maxv;
    bool ok = true;
    if (binaer) {
        /* nach maxv genau EIN Whitespace-Byte, dann raw */
        int bpp = (maxv > 255) ? 2 : 1;
        size_t bytes = (size_t)n * (size_t)bpp;
        unsigned char* buf = (unsigned char*)malloc(bytes);
        ok = buf && fread(buf, 1, bytes, f) == bytes;
        if (ok) {
            for (int64_t i = 0; i < n; i++) {
                unsigned v = (bpp == 2)
                    ? ((unsigned)buf[i * 2] << 8) | buf[i * 2 + 1]
                    : buf[i];
                t->data[i] = (float)v * skala;
            }
        }
        free(buf);
    } else {
        for (int64_t i = 0; i < n && ok; i++) {
            long v;
            ok = ppm_zahl(f, &v) && v >= 0 && v <= maxv;
            if (ok) t->data[i] = (float)v * skala;
        }
    }
    fclose(f);
    MooValue tv; tv.tag = MOO_TENSOR; moo_val_set_ptr(&tv, t);
    if (!ok) {
        moo_release(tv);
        moo_throw(moo_error("bild_laden: Bilddaten unvollstaendig"));
        return moo_none();
    }
    return tv;
}

/* ============================================================
 * Mischen + Normalisieren
 * ============================================================ */

/* mischen(x, y, seed?): GLEICHE Zeilen-Permutation auf beide Tensoren.
 * Rueckgabe Liste [x_gemischt, y_gemischt]. seed-deterministisch. */
MooValue moo_ds_mischen(MooValue x, MooValue y, MooValue seed) {
    if (x.tag != MOO_TENSOR || y.tag != MOO_TENSOR) {
        moo_throw(moo_error("mischen: erwarte zwei Tensoren (Daten, Ziele)"));
        return moo_none();
    }
    MooTensor* xt = T(x);
    MooTensor* yt = T(y);
    int64_t n = xt->shape[0];
    if (yt->shape[0] != n) {
        moo_throw(moo_error("mischen: beide Tensoren muessen gleich viele "
                            "Zeilen haben"));
        return moo_none();
    }
    if (seed.tag != MOO_NUMBER && seed.tag != MOO_NONE) {
        moo_throw(moo_error("mischen: der Seed muss eine Zahl sein"));
        return moo_none();
    }
    int64_t xsp = (xt->ndim >= 2) ? xt->size / n : 1;
    int64_t ysp = (yt->ndim >= 2) ? yt->size / n : 1;

    int32_t* perm = (int32_t*)malloc((size_t)n * sizeof(int32_t));
    MooTensor* x2 = moo_tensor_raw(xt->ndim, xt->shape);
    MooTensor* y2 = moo_tensor_raw(yt->ndim, yt->shape);
    if (!perm || !x2 || !y2) {
        free(perm);
        if (x2) { MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, x2); moo_release(v); }
        if (y2) { MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, y2); moo_release(v); }
        moo_throw(moo_error("mischen: nicht genug Speicher"));
        return moo_none();
    }
    for (int64_t i = 0; i < n; i++) perm[i] = (int32_t)i;
    uint64_t s = (seed.tag == MOO_NUMBER) ? (uint64_t)(int64_t)MV_NUM(seed) : 42ULL;
    uint64_t rng = s * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL;
    for (int64_t i = n - 1; i > 0; i--) {
        int64_t j = (int64_t)(dsm64(&rng) % (uint64_t)(i + 1));
        int32_t tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
    }
    for (int64_t r = 0; r < n; r++) {
        memcpy(x2->data + r * xsp, xt->data + (int64_t)perm[r] * xsp,
               (size_t)xsp * sizeof(float));
        memcpy(y2->data + r * ysp, yt->data + (int64_t)perm[r] * ysp,
               (size_t)ysp * sizeof(float));
    }
    free(perm);
    MooValue l = moo_list_new(2);
    MooValue xv; xv.tag = MOO_TENSOR; moo_val_set_ptr(&xv, x2);
    MooValue yv; yv.tag = MOO_TENSOR; moo_val_set_ptr(&yv, y2);
    moo_list_append(l, xv);
    moo_list_append(l, yv);
    return l;
}

/* normalisieren(t, art?): "minmax" (Standard, -> 0..1) oder "standard"
 * (Mittel 0, Streuung 1). Ueber ALLE Werte (Daten-Vorverarbeitung),
 * ohne Autograd-Tape. */
MooValue moo_ds_normalisieren(MooValue t, MooValue art) {
    if (t.tag != MOO_TENSOR) {
        moo_throw(moo_error("normalisieren: erwarte einen Tensor"));
        return moo_none();
    }
    const char* a = "minmax";
    if (art.tag == MOO_STRING) a = MV_STR(art)->chars;
    else if (art.tag != MOO_NONE) {
        moo_throw(moo_error("normalisieren: die Art muss ein Text sein "
                            "(\"minmax\" oder \"standard\")"));
        return moo_none();
    }
    bool std_ = (strcmp(a, "standard") == 0);
    if (!std_ && strcmp(a, "minmax") != 0) {
        moo_throw(moo_error("normalisieren: kenne nur \"minmax\" und "
                            "\"standard\""));
        return moo_none();
    }
    MooTensor* src = T(t);
    MooTensor* out = moo_tensor_raw(src->ndim, src->shape);
    if (!out) {
        moo_throw(moo_error("normalisieren: nicht genug Speicher"));
        return moo_none();
    }
    if (std_) {
        double summe = 0.0;
        for (int64_t i = 0; i < src->size; i++) summe += (double)src->data[i];
        double mittel = summe / (double)src->size;
        double q = 0.0;
        for (int64_t i = 0; i < src->size; i++) {
            double d = (double)src->data[i] - mittel;
            q += d * d;
        }
        double streuung = sqrt(q / (double)src->size);
        if (streuung < 1e-12) streuung = 1.0;   /* konstante Daten */
        for (int64_t i = 0; i < src->size; i++)
            out->data[i] = (float)(((double)src->data[i] - mittel) / streuung);
    } else {
        float mn = src->data[0], mx = src->data[0];
        for (int64_t i = 1; i < src->size; i++) {
            if (src->data[i] < mn) mn = src->data[i];
            if (src->data[i] > mx) mx = src->data[i];
        }
        float spanne = mx - mn;
        if (spanne < 1e-12f) spanne = 1.0f;
        for (int64_t i = 0; i < src->size; i++)
            out->data[i] = (src->data[i] - mn) / spanne;
    }
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, out);
    return v;
}

/* ============================================================
 * Zeichen-Tokenizer (Plan-014 G1)
 * ============================================================ */

/* Laenge + Wert eines UTF-8-Codepoints ab s[i] (blen Gesamtbytes).
 * Rueckgabe Byte-Laenge, 0 = ungueltig. Validiert Continuation-Bytes,
 * Overlong-Formen, Surrogate und das Unicode-Maximum — OHNE diese Checks
 * waere der Roundtrip encode(decode(x)) == x fuer kaputte Eingaben still
 * falsch (Overlong dekodiert zu einem Codepoint, der kuerzer re-encodet). */
static int utf8_decode_cp(const unsigned char* s, int32_t i, int32_t blen,
                          uint32_t* out) {
    unsigned char b = s[i];
    if (b < 0x80) { *out = b; return 1; }
    int len; uint32_t cp, min;
    if ((b & 0xE0) == 0xC0)      { len = 2; cp = b & 0x1Fu; min = 0x80u;    }
    else if ((b & 0xF0) == 0xE0) { len = 3; cp = b & 0x0Fu; min = 0x800u;   }
    else if ((b & 0xF8) == 0xF0) { len = 4; cp = b & 0x07u; min = 0x10000u; }
    else return 0;   /* Continuation-Byte oder 0xF8+ als Leadbyte */
    if (i > blen - len) return 0;   /* abgeschnittene Sequenz */
    for (int k = 1; k < len; k++) {
        if ((s[i + k] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (uint32_t)(s[i + k] & 0x3Fu);
    }
    if (cp < min) return 0;                        /* Overlong */
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;  /* Surrogat */
    if (cp > 0x10FFFFu) return 0;
    *out = cp;
    return len;
}

/* Codepoint -> UTF-8-Bytes (out mind. 4 Bytes). Rueckgabe Byte-Laenge.
 * Nur mit von utf8_decode_cp validierten Codepoints aufrufen. */
static int utf8_encode_cp(uint32_t cp, char out[4]) {
    if (cp < 0x80u) { out[0] = (char)cp; return 1; }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

static int tok_cmp_u32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}

/* text_tokenizer(text): Zeichen-Level-Tokenizer auf UTF-8-CODEPOINTS
 * (nicht Bytes — "ä" ist EIN Zeichen, nicht zwei). Rueckgabe Dict:
 *   zeichen_zu_id: Dict   Zeichen -> Id
 *   id_zu_zeichen: Liste  Id -> Zeichen
 *   ids:           Tensor [n] der Zeichen-Ids des Texts
 *   vokab:         Anzahl verschiedener Zeichen
 * Ids sind nach Codepoint sortiert vergeben — deterministisch, unabhaengig
 * von der Reihenfolge im Text. Ungueltiges UTF-8 und Null-Zeichen werfen
 * erklaerend (kein stilles Ueberspringen). */
MooValue moo_ds_tokenizer(MooValue text) {
    if (text.tag != MOO_STRING) {
        moo_throw(moo_error("text_tokenizer: erwarte einen Text"));
        return moo_none();
    }
    const unsigned char* s = (const unsigned char*)MV_STR(text)->chars;
    int32_t blen = MV_STR(text)->length;
    if (blen <= 0) {
        moo_throw(moo_error("text_tokenizer: der Text ist leer"));
        return moo_none();
    }
    /* Pass 1: Codepoints dekodieren (worst case 1 Codepoint pro Byte) */
    uint32_t* cps = (uint32_t*)malloc((size_t)blen * sizeof(uint32_t));
    if (!cps) {
        moo_throw(moo_error("text_tokenizer: nicht genug Speicher"));
        return moo_none();
    }
    int32_t n = 0, i = 0;
    while (i < blen) {
        uint32_t cp;
        int l = utf8_decode_cp(s, i, blen, &cp);
        if (l == 0) {
            free(cps);
            char msg[160];
            snprintf(msg, sizeof(msg), "text_tokenizer: ungueltiges UTF-8 bei "
                     "Byte %d — ist die Datei wirklich UTF-8-Text?", (int)i);
            moo_throw(moo_error(msg));
            return moo_none();
        }
        if (cp == 0) {
            free(cps);
            moo_throw(moo_error("text_tokenizer: der Text enthaelt ein "
                                "Null-Zeichen — das ist keine Textdatei"));
            return moo_none();
        }
        cps[n++] = cp;
        i += l;
    }
    /* Vokabular: sortierte, eindeutige Codepoints => deterministische Ids */
    uint32_t* uniq = (uint32_t*)malloc((size_t)n * sizeof(uint32_t));
    if (!uniq) {
        free(cps);
        moo_throw(moo_error("text_tokenizer: nicht genug Speicher"));
        return moo_none();
    }
    memcpy(uniq, cps, (size_t)n * sizeof(uint32_t));
    qsort(uniq, (size_t)n, sizeof(uint32_t), tok_cmp_u32);
    int32_t vokab = 0;
    for (int32_t k = 0; k < n; k++)
        if (k == 0 || uniq[k] != uniq[k - 1]) uniq[vokab++] = uniq[k];
    /* ids-Tensor: lower_bound-Binaersuche im sortierten Vokabular */
    int32_t shape[1] = { n };
    MooTensor* t = moo_tensor_raw(1, shape);
    if (!t) {
        free(cps); free(uniq);
        moo_throw(moo_error("text_tokenizer: nicht genug Speicher"));
        return moo_none();
    }
    for (int32_t k = 0; k < n; k++) {
        int32_t lo = 0, hi = vokab;
        while (lo < hi) {
            int32_t mid = lo + (hi - lo) / 2;
            if (uniq[mid] < cps[k]) lo = mid + 1; else hi = mid;
        }
        t->data[k] = (float)lo;   /* cps[k] ist garantiert im Vokabular */
    }
    /* Nachschlage-Strukturen. Transfer-Semantik: dict_set/list_append
     * uebernehmen die +1-Refs der frisch gebauten Werte. */
    MooValue z2i = moo_dict_new();
    MooValue i2z = moo_list_new(vokab);
    for (int32_t k = 0; k < vokab; k++) {
        char buf[4];
        int l = utf8_encode_cp(uniq[k], buf);
        moo_list_append(i2z, moo_string_new_len(buf, l));
        moo_dict_set(z2i, moo_string_new_len(buf, l), moo_number((double)k));
    }
    free(cps); free(uniq);
    MooValue tv; tv.tag = MOO_TENSOR; moo_val_set_ptr(&tv, t);
    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("zeichen_zu_id"), z2i);
    moo_dict_set(d, moo_string_new("id_zu_zeichen"), i2z);
    moo_dict_set(d, moo_string_new("ids"), tv);
    moo_dict_set(d, moo_string_new("vokab"), moo_number((double)vokab));
    return d;
}

