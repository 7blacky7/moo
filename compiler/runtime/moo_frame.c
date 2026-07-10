/**
 * moo_frame.c — Opaker Pixel-Frame-Heap-Typ (Plan-008 A3A).
 *
 * MOO_FRAME ist ein refcounteter Heap-Typ fuer Framebuffer-Pixeldaten. Pixel
 * werden NIEMALS als moo-Liste gehalten (MooValue=16B; ein Ultrawide-Frame
 * 3440x1440x4 ~= 20 MB — das wuerde als Liste ~320 MB belegen und das GC-/
 * Refcount-System ueberlasten). Stattdessen ein einziger malloc'ter uint8_t*-
 * Block.
 *
 * STANDARDISIERTES FORMAT (backend-uebergreifend identisch):
 *   - format MOO_FRAME_FMT_RGBA8 (=0): 4 Bytes/Pixel R,G,B,A
 *   - top-left origin (Pixel (0,0) ist oben links)
 *   - stride = width*4 (dicht gepackt)
 * Der Y-Flip und ggf. BGRA->RGBA-Swizzle wird IM Backend-Grab erledigt
 * (moo_test_api.c / die grab_rgba-Vtable), sodass dieser File rein
 * format-agnostisch mit RGBA8-top-left arbeitet.
 *
 * Dieser File ist IMMER Teil des Builds (auch Non-3D), weil moo_memory.c den
 * Release-Dispatch (moo_frame_free) braucht. Die backend-spezifische
 * test_frame_grab-Implementierung lebt dagegen in moo_test_api.c (nur 3D-Build).
 *
 * Refcount-Konvention: refcount ist das ERSTE Struct-Feld (siehe MooFrame in
 * moo_runtime.h). moo_frame_new_take() startet bei refcount=1, moo_release()
 * dekrementiert und ruft bei 0 moo_frame_free().
 */

#include "moo_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Forward-Decls (in moo_value.c / moo_dict.c / moo_string.c definiert) ---- */
extern MooValue moo_number(double n);
extern MooValue moo_bool(bool b);
extern MooValue moo_string_new(const char* s);
extern MooValue moo_none(void);
extern void     moo_throw(MooValue v);
extern MooValue moo_dict_new(void);
extern void     moo_dict_set(MooValue dict, MooValue key, MooValue value);

/* ============================================================
 * Konstruktor + Destruktor
 * ============================================================ */

/* Uebernimmt den uebergebenen RGBA8-top-left-Buffer (take ownership). Der
 * Buffer MUSS width*height*4 Bytes gross und via malloc allokiert sein. Bei
 * pixels==NULL oder ungueltigen Dims wird der Buffer freigegeben und NONE
 * zurueckgegeben (kein halbinitialisierter Frame). */
MooValue moo_frame_new_take(int width, int height, uint8_t* rgba_pixels_top_left) {
    if (!rgba_pixels_top_left || width <= 0 || height <= 0) {
        free(rgba_pixels_top_left);
        return moo_none();
    }
    MooFrame* f = (MooFrame*)malloc(sizeof(MooFrame));
    if (!f) {
        free(rgba_pixels_top_left);
        return moo_none();
    }
    f->refcount = 1;
    f->width    = width;
    f->height   = height;
    f->format   = MOO_FRAME_FMT_RGBA8;
    f->stride   = width * 4;
    f->pixels   = rgba_pixels_top_left;

    MooValue v;
    v.tag = MOO_FRAME;
    moo_val_set_ptr(&v, f);
    return v;
}

void moo_frame_free(void* ptr) {
    if (!ptr) return;
    MooFrame* f = (MooFrame*)ptr;
    if (f->pixels) free(f->pixels);
    f->pixels = NULL;
    free(f);
}

/* ============================================================
 * Pixel-Lesen  ->  Dict { rot, gruen, blau, alpha }  (0..255)
 * ============================================================
 * NUR fuer ein direktes MOO_FRAME. x/y sind ganzzahlige Pixelkoordinaten,
 * top-left origin. Out-of-bounds / Nicht-Frame -> throw. Der Fenster-Pfad
 * (kurz grabben) liegt in moo_test_api.c (moo_test_pixel), da er die nur im
 * 3D-Build verfuegbare Grab-Infrastruktur braucht.
 */
MooValue moo_frame_pixel_dict(const MooFrame* f, int x, int y) {
    MooValue d = moo_dict_new();
    size_t off = (size_t)y * (size_t)f->stride + (size_t)x * 4;
    uint8_t r = f->pixels[off + 0];
    uint8_t g = f->pixels[off + 1];
    uint8_t b = f->pixels[off + 2];
    uint8_t a = f->pixels[off + 3];
    moo_dict_set(d, moo_string_new("rot"),   moo_number((double)r));
    moo_dict_set(d, moo_string_new("gruen"), moo_number((double)g));
    moo_dict_set(d, moo_string_new("blau"),  moo_number((double)b));
    moo_dict_set(d, moo_string_new("alpha"), moo_number((double)a));
    return d;
}

MooValue moo_frame_read_pixel(MooValue frame, MooValue x, MooValue y) {
    if (frame.tag != MOO_FRAME) {
        moo_throw(moo_string_new("test_pixel: kein gueltiger Frame"));
        return moo_none();
    }
    if (x.tag != MOO_NUMBER || y.tag != MOO_NUMBER) {
        moo_throw(moo_string_new("test_pixel: x und y muessen Zahlen sein"));
        return moo_none();
    }
    MooFrame* f = MV_FRAME(frame);
    if (!f || !f->pixels) {
        moo_throw(moo_string_new("test_pixel: ungueltiger Frame"));
        return moo_none();
    }
    int ix = (int)MV_NUM(x);
    int iy = (int)MV_NUM(y);
    if (ix < 0 || iy < 0 || ix >= f->width || iy >= f->height) {
        moo_throw(moo_string_new("test_pixel: Koordinate ausserhalb des Frames"));
        return moo_none();
    }
    return moo_frame_pixel_dict(f, ix, iy);
}

/* ============================================================
 * Frame -> BMP (24bpp, bottom-up). SDL-unabhaengig, damit dieser File auch
 * im Non-3D-Build linkbar bleibt. Eingang ist RGBA8 top-left.
 * ============================================================ */
static void put_le32(unsigned char* p, uint32_t v) {
    /* Little-Endian-Bytes; unsigned -> definiertes Wrap/Shift (ub-policy). */
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

MooValue moo_test_frame_save_bmp(MooValue frame, MooValue pfad) {
    if (frame.tag != MOO_FRAME) {
        moo_throw(moo_string_new("test_frame_save_bmp: erstes Argument ist kein Frame"));
        return moo_bool(false);
    }
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_string_new("test_frame_save_bmp: Pfad muss ein String sein"));
        return moo_bool(false);
    }
    MooFrame* f = MV_FRAME(frame);
    if (!f || !f->pixels || f->width <= 0 || f->height <= 0) {
        moo_throw(moo_string_new("test_frame_save_bmp: ungueltiger Frame"));
        return moo_bool(false);
    }

    int w = f->width;
    int h = f->height;
    /* Zeilen-Padding auf 4-Byte-Grenze (BMP-Pflicht). unsigned-Arithmetik. */
    uint32_t row_bytes  = (uint32_t)w * 3u;
    uint32_t row_pad    = (4u - (row_bytes % 4u)) % 4u;
    uint32_t row_padded = row_bytes + row_pad;
    uint32_t data_size  = row_padded * (uint32_t)h;
    uint32_t file_size  = 54u + data_size;

    FILE* fp = fopen(MV_STR(pfad)->chars, "wb");
    if (!fp) {
        moo_throw(moo_string_new("test_frame_save_bmp: Datei nicht schreibbar"));
        return moo_bool(false);
    }

    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    put_le32(hdr + 2, file_size);
    put_le32(hdr + 10, 54u);          /* pixel-data offset */
    put_le32(hdr + 14, 40u);          /* DIB header size */
    put_le32(hdr + 18, (uint32_t)w);
    put_le32(hdr + 22, (uint32_t)h);  /* positiv = bottom-up */
    hdr[26] = 1;                       /* planes */
    hdr[28] = 24;                      /* bpp */
    put_le32(hdr + 34, data_size);
    fwrite(hdr, 1, 54, fp);

    unsigned char* row = (unsigned char*)malloc(row_padded);
    if (!row) {
        fclose(fp);
        moo_throw(moo_string_new("test_frame_save_bmp: Speicher fehlgeschlagen"));
        return moo_bool(false);
    }
    memset(row, 0, row_padded);

    /* Frame ist top-down RGBA; BMP ist bottom-up BGR -> Reihen umkehren. */
    for (int y = h - 1; y >= 0; y--) {
        const uint8_t* sp = f->pixels + (size_t)y * (size_t)f->stride;
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = sp[x * 4 + 2]; /* B */
            row[x * 3 + 1] = sp[x * 4 + 1]; /* G */
            row[x * 3 + 2] = sp[x * 4 + 0]; /* R */
        }
        fwrite(row, 1, row_padded, fp);
    }

    free(row);
    fclose(fp);
    return moo_bool(true);
}

/* ============================================================
 * Frame-Diff + Region-Durchschnitt (Task 6bb03790 Teil B).
 * Reine RGBA8-Puffer-Mathematik — SDL-frei, damit dieser File im
 * Non-3D-Build linkbar bleibt. Frames liefert test_frame_grab.
 * ============================================================ */

/* test_frame_diff(frame_a, frame_b) -> Dict { maxdiff, geaenderte_pixel,
 * prozent }. maxdiff = groesste Einzelkanal-Differenz (0..255, RGBA);
 * geaenderte_pixel = Pixel mit mind. 1 abweichendem Kanal; prozent = Anteil
 * geaenderter Pixel (0..100). Visuelles Regressions-Primitiv fuer Selftests. */
MooValue moo_test_frame_diff(MooValue a, MooValue b) {
    if (a.tag != MOO_FRAME || b.tag != MOO_FRAME) {
        moo_throw(moo_string_new("test_frame_diff: beide Argumente muessen Frames sein (test_frame_grab)"));
        return moo_none();
    }
    const MooFrame* fa = MV_FRAME(a);
    const MooFrame* fb = MV_FRAME(b);
    if (!fa || !fa->pixels || !fb || !fb->pixels) {
        moo_throw(moo_string_new("test_frame_diff: ungueltiger Frame"));
        return moo_none();
    }
    if (fa->width != fb->width || fa->height != fb->height) {
        moo_throw(moo_string_new("test_frame_diff: Frames sind unterschiedlich gross"));
        return moo_none();
    }
    size_t n = (size_t)fa->width * (size_t)fa->height;
    uint64_t geaendert = 0;
    int maxdiff = 0;
    const uint8_t* pa = fa->pixels;
    const uint8_t* pb = fb->pixels;
    for (size_t i = 0; i < n; i++) {
        int pixel_diff = 0;
        for (int c = 0; c < 4; c++) {
            int d = (int)pa[i * 4 + (size_t)c] - (int)pb[i * 4 + (size_t)c];
            if (d < 0) d = -d;
            if (d > maxdiff) maxdiff = d;
            if (d != 0) pixel_diff = 1;
        }
        geaendert += (uint64_t)pixel_diff;
    }
    MooValue dict = moo_dict_new();
    moo_dict_set(dict, moo_string_new("maxdiff"), moo_number((double)maxdiff));
    moo_dict_set(dict, moo_string_new("geaenderte_pixel"), moo_number((double)geaendert));
    moo_dict_set(dict, moo_string_new("prozent"), moo_number(100.0 * (double)geaendert / (double)n));
    return dict;
}

/* test_frame_region(frame, x, y, b, h) -> Dict { rot, gruen, blau, alpha }:
 * Durchschnittsfarbe der Region (kaufmaennisch gerundet). Robuster als
 * fragile Einzelpixel-Asserts ("HUD-Bereich ist rot"). Bounds werden in
 * double geprueft, BEVOR nach int gecastet wird (kein Cast-UB bei riesigen
 * Werten). */
MooValue moo_test_frame_region(MooValue frame, MooValue x, MooValue y, MooValue b, MooValue h) {
    if (frame.tag != MOO_FRAME) {
        moo_throw(moo_string_new("test_frame_region: erstes Argument muss ein Frame sein (test_frame_grab)"));
        return moo_none();
    }
    const MooFrame* f = MV_FRAME(frame);
    if (!f || !f->pixels) {
        moo_throw(moo_string_new("test_frame_region: ungueltiger Frame"));
        return moo_none();
    }
    if (x.tag != MOO_NUMBER || y.tag != MOO_NUMBER || b.tag != MOO_NUMBER || h.tag != MOO_NUMBER) {
        moo_throw(moo_string_new("test_frame_region: x, y, b, h muessen Zahlen sein"));
        return moo_none();
    }
    double dx = MV_NUM(x), dy = MV_NUM(y), db = MV_NUM(b), dh = MV_NUM(h);
    if (db < 1.0 || dh < 1.0) {
        moo_throw(moo_string_new("test_frame_region: b und h muessen >= 1 sein"));
        return moo_none();
    }
    if (dx < 0.0 || dy < 0.0 || dx + db > (double)f->width || dy + dh > (double)f->height) {
        moo_throw(moo_string_new("test_frame_region: Region ausserhalb des Frames"));
        return moo_none();
    }
    int ix = (int)dx, iy = (int)dy, ib = (int)db, ih = (int)dh;
    uint64_t sum[4] = {0, 0, 0, 0};
    for (int yy = iy; yy < iy + ih; yy++) {
        const uint8_t* row = f->pixels + (size_t)yy * (size_t)f->stride + (size_t)ix * 4;
        for (int xx = 0; xx < ib; xx++) {
            for (int c = 0; c < 4; c++) {
                sum[c] += (uint64_t)row[(size_t)xx * 4 + (size_t)c];
            }
        }
    }
    uint64_t cnt = (uint64_t)ib * (uint64_t)ih;
    MooValue dict = moo_dict_new();
    moo_dict_set(dict, moo_string_new("rot"),   moo_number((double)((sum[0] + cnt / 2) / cnt)));
    moo_dict_set(dict, moo_string_new("gruen"), moo_number((double)((sum[1] + cnt / 2) / cnt)));
    moo_dict_set(dict, moo_string_new("blau"),  moo_number((double)((sum[2] + cnt / 2) / cnt)));
    moo_dict_set(dict, moo_string_new("alpha"), moo_number((double)((sum[3] + cnt / 2) / cnt)));
    return dict;
}
