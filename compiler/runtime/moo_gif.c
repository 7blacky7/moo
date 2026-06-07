/**
 * moo_gif.c - Isolierter GIF89a + LZW Animations-Encoder-Kern (Plan-008 P008-A3B Teil 1).
 *
 * Siehe moo_gif.h fuer den oeffentlichen Vertrag. Diese Datei hat KEINE
 * MOO-Abhaengigkeit (kein moo_runtime.h, keine MooValue) — sie ist ein in sich
 * geschlossener C-Encoder, der via moo_gif.h von Teil 2 (MOO_FRAME-Verdrahtung +
 * test_gif_*-Builtins) angebunden wird.
 *
 * UB-Policy (Plan-007 ub-arithmetik-policy): Das LZW-Bit-Packing arbeitet
 * AUSSCHLIESSLICH unsigned (uint32-Akkumulator, unsigned Shifts/Masken). Es gibt
 * keinen signed Shift und keine signed Overflow-Arithmetik im heissen Pfad.
 * Verifiziert unter UBSan (-fsanitize=undefined -fno-sanitize-recover=undefined)
 * und ASan (detect_leaks=1).
 *
 * Frame-bounded: Pro Frame wird nur ein Index-Puffer (w*h Bytes) gehalten, der
 * direkt nach dem LZW-Schreiben freigegeben wird. Die komprimierten Bytes werden
 * in 255-Byte-Subbloecken in die FILE* gestreamt. Es liegt NIE eine komplette
 * RGBA- oder Frame-Sequenz im Writer.
 */

#include "moo_gif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================== */
/*  Globale Fixpalette: 6x6x6 Websafe-Wuerfel (216) + 40 Graustufen = 256.   */
/*  Deterministisch -> reproduzierbare GIFs. Median-Cut ist A3c (optional).  */
/* ======================================================================== */

#define MOO_GIF_PALETTE_SIZE 256

/* Die 6 Pegel der Websafe-Achse: 0, 51, 102, 153, 204, 255. */
static const uint8_t MOO_GIF_LEVELS[6] = { 0u, 51u, 102u, 153u, 204u, 255u };

/* Baut die 256-Eintrag-Palette (je 3 Bytes R,G,B). Indizes 0..215 = Wuerfel,
 * 216..255 = 40 Graustufen (feiner als die Wuerfel-Diagonale). */
static void moo_gif_build_palette(uint8_t pal[MOO_GIF_PALETTE_SIZE * 3]) {
    int idx = 0;
    for (int r = 0; r < 6; ++r) {
        for (int g = 0; g < 6; ++g) {
            for (int b = 0; b < 6; ++b) {
                pal[idx * 3 + 0] = MOO_GIF_LEVELS[r];
                pal[idx * 3 + 1] = MOO_GIF_LEVELS[g];
                pal[idx * 3 + 2] = MOO_GIF_LEVELS[b];
                ++idx;
            }
        }
    }
    /* idx == 216. 40 Graustufen ueber 0..255 verteilt (gleichmaessig). */
    for (int i = 0; i < 40; ++i) {
        /* 0..39 -> 0..255 ; unsigned, kein Overflow (max 39*255=9945). */
        uint8_t v = (uint8_t)((uint32_t)i * 255u / 39u);
        pal[idx * 3 + 0] = v;
        pal[idx * 3 + 1] = v;
        pal[idx * 3 + 2] = v;
        ++idx;
    }
    /* idx == 256. */
}

/* Nearest-Color-Quantisierung eines RGB-Tripels gegen die Fixpalette.
 * Deterministisch (kleinster Index bei Gleichstand gewinnt). Distanz im
 * quadrierten RGB-Raum; alle Zwischenwerte unsigned (max 3*255^2=195075,
 * passt locker in uint32). */
static uint8_t moo_gif_nearest(const uint8_t pal[MOO_GIF_PALETTE_SIZE * 3],
                               uint8_t r, uint8_t g, uint8_t b) {
    uint32_t best_dist = 0xFFFFFFFFu;
    uint8_t  best_idx  = 0;
    for (int i = 0; i < MOO_GIF_PALETTE_SIZE; ++i) {
        int32_t dr = (int32_t)r - (int32_t)pal[i * 3 + 0];
        int32_t dg = (int32_t)g - (int32_t)pal[i * 3 + 1];
        int32_t db = (int32_t)b - (int32_t)pal[i * 3 + 2];
        /* dr,dg,db in [-255,255] -> Quadrate <= 65025, Summe <= 195075. */
        uint32_t dist = (uint32_t)(dr * dr) + (uint32_t)(dg * dg) + (uint32_t)(db * db);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx  = (uint8_t)i;
            if (dist == 0u) break; /* exakter Treffer */
        }
    }
    return best_idx;
}

/* ======================================================================== */
/*  Bit-Writer + GIF-Subblock-Streaming (255-Byte-Chunks direkt in FILE*).   */
/*  ALLES UNSIGNED (ub-arithmetik-policy).                                    */
/* ======================================================================== */

typedef struct {
    FILE*    fp;
    uint32_t acc;        /* Bit-Akkumulator (unsigned!) */
    uint32_t nbits;      /* gueltige Bits im Akkumulator */
    uint8_t  block[255]; /* aktueller GIF-Datensubblock */
    uint32_t block_len;  /* gefuellte Bytes in block[] */
    int      io_err;     /* 1 sobald ein fwrite fehlschlug */
} MooGifBitWriter;

static void moo_gif_bw_init(MooGifBitWriter* bw, FILE* fp) {
    bw->fp = fp;
    bw->acc = 0u;
    bw->nbits = 0u;
    bw->block_len = 0u;
    bw->io_err = 0;
}

/* Schreibt den aktuellen Subblock (Laengen-Praefix + Daten) und leert ihn. */
static void moo_gif_bw_flush_block(MooGifBitWriter* bw) {
    if (bw->block_len == 0u) return;
    uint8_t len = (uint8_t)bw->block_len; /* block_len <= 255 garantiert */
    if (fputc((int)len, bw->fp) == EOF) { bw->io_err = 1; return; }
    if (fwrite(bw->block, 1u, bw->block_len, bw->fp) != bw->block_len) {
        bw->io_err = 1;
        return;
    }
    bw->block_len = 0u;
}

/* Haengt ein fertiges Byte an den Subblock; flusht bei 255. */
static void moo_gif_bw_emit_byte(MooGifBitWriter* bw, uint8_t byte) {
    bw->block[bw->block_len++] = byte;
    if (bw->block_len == 255u) {
        moo_gif_bw_flush_block(bw);
    }
}

/* Schreibt einen LZW-Code mit 'code_size' Bits (LSB-first, GIF-Konvention).
 * acc/nbits/code sind alle unsigned -> kein UB beim Shiften. */
static void moo_gif_bw_put_code(MooGifBitWriter* bw, uint32_t code, uint32_t code_size) {
    /* Neue Bits oben in den Akkumulator einfuegen (LSB-first Strom). */
    bw->acc |= (code & ((1u << code_size) - 1u)) << bw->nbits;
    bw->nbits += code_size;
    while (bw->nbits >= 8u) {
        moo_gif_bw_emit_byte(bw, (uint8_t)(bw->acc & 0xFFu));
        bw->acc >>= 8u;
        bw->nbits -= 8u;
    }
}

/* Restbits (< 8) als letztes Byte herausschieben und Subblock-Stream beenden. */
static void moo_gif_bw_finish(MooGifBitWriter* bw) {
    if (bw->nbits > 0u) {
        moo_gif_bw_emit_byte(bw, (uint8_t)(bw->acc & 0xFFu));
        bw->acc = 0u;
        bw->nbits = 0u;
    }
    moo_gif_bw_flush_block(bw);
    /* Block-Terminator (Laenge 0). */
    if (fputc(0x00, bw->fp) == EOF) bw->io_err = 1;
}

/* ======================================================================== */
/*  LZW-Encoder (GIF-Variante mit String-Tabelle, Hash-Lookup).              */
/* ======================================================================== */

/*
 * GIF-LZW: min_code_size = 8 (256-Farb-Palette). Clear-Code = 256, EOI = 257,
 * erste freie Code = 258. Code-Breite startet bei 9 Bit, waechst bis 12 Bit
 * (max 4096 Eintraege). Bei Erreichen von 4096 -> Clear-Code + Reset.
 *
 * String-Tabelle: ein Eintrag = (prefix_code, append_byte). Lookup ueber eine
 * offene Hash-Tabelle (linear probing). Alle Indizes/Schluessel unsigned.
 */

#define MOO_GIF_MIN_CODE_SIZE 8u
#define MOO_GIF_CLEAR_CODE    256u
#define MOO_GIF_EOI_CODE      257u
#define MOO_GIF_FIRST_CODE    258u
#define MOO_GIF_MAX_CODE      4096u    /* exklusive Obergrenze (12 Bit) */
#define MOO_GIF_HASH_SIZE     8192u    /* > 2*4096, Power-of-two fuer & */

/* Schluessel kodiert (prefix << 8) | byte. prefix < 4096, byte < 256 ->
 * passt in 20 Bit, also bequem in uint32. -1-Aequivalent = 0xFFFFFFFF. */
typedef struct {
    uint32_t* key;   /* HASH_SIZE Eintraege; 0xFFFFFFFF == leer */
    uint32_t* code;  /* zugeordneter LZW-Code */
} MooGifLzwTable;

static int moo_gif_lzw_table_alloc(MooGifLzwTable* t) {
    t->key  = (uint32_t*)malloc(sizeof(uint32_t) * MOO_GIF_HASH_SIZE);
    t->code = (uint32_t*)malloc(sizeof(uint32_t) * MOO_GIF_HASH_SIZE);
    if (!t->key || !t->code) {
        free(t->key);
        free(t->code);
        t->key = NULL;
        t->code = NULL;
        return -1;
    }
    return 0;
}

static void moo_gif_lzw_table_free(MooGifLzwTable* t) {
    free(t->key);
    free(t->code);
    t->key = NULL;
    t->code = NULL;
}

static void moo_gif_lzw_table_reset(MooGifLzwTable* t) {
    /* 0xFF-Fuellung -> alle key == 0xFFFFFFFF (leer). */
    memset(t->key, 0xFF, sizeof(uint32_t) * MOO_GIF_HASH_SIZE);
}

/* Sucht (prefix,byte). Gibt Code zurueck oder 0xFFFFFFFF wenn nicht vorhanden.
 * Bei Nicht-Treffer wird *slot_out auf den freien Slot gesetzt (zum Einfuegen).
 * Hash unsigned, Multiplikation absichtlich wrappend in uint32. */
static uint32_t moo_gif_lzw_lookup(const MooGifLzwTable* t, uint32_t composite,
                                   uint32_t* slot_out) {
    /* Streuung: Knuth-Multiplikativ-Hash, bewusst unsigned-wrappend. */
    uint32_t h = (composite * 2654435761u) & (MOO_GIF_HASH_SIZE - 1u);
    for (;;) {
        uint32_t k = t->key[h];
        if (k == 0xFFFFFFFFu) {        /* leerer Slot -> nicht gefunden */
            *slot_out = h;
            return 0xFFFFFFFFu;
        }
        if (k == composite) {          /* Treffer */
            *slot_out = h;
            return t->code[h];
        }
        h = (h + 1u) & (MOO_GIF_HASH_SIZE - 1u); /* linear probing, wrap unsigned */
    }
}

/* Komprimiert 'indices' (count Palette-Indizes je 1 Byte) als GIF-LZW-Stream.
 * Schreibt min_code_size-Byte + LZW-Subbloecke + Terminator in bw->fp.
 * Rueckgabe 0 ok, -1 NOMEM, -2 IO. */
static int moo_gif_lzw_encode(MooGifBitWriter* bw,
                              const uint8_t* indices, size_t count) {
    MooGifLzwTable tbl;
    if (moo_gif_lzw_table_alloc(&tbl) != 0) return -1;

    /* min_code_size voranstellen (Image-Data-Header). */
    if (fputc((int)MOO_GIF_MIN_CODE_SIZE, bw->fp) == EOF) {
        moo_gif_lzw_table_free(&tbl);
        return -2;
    }

    uint32_t code_size = MOO_GIF_MIN_CODE_SIZE + 1u; /* 9 Bit Startbreite */
    uint32_t next_code = MOO_GIF_FIRST_CODE;         /* 258 */

    moo_gif_lzw_table_reset(&tbl);
    moo_gif_bw_put_code(bw, MOO_GIF_CLEAR_CODE, code_size);

    if (count == 0u) {
        /* Leeres Bild: nur EOI. */
        moo_gif_bw_put_code(bw, MOO_GIF_EOI_CODE, code_size);
        moo_gif_bw_finish(bw);
        moo_gif_lzw_table_free(&tbl);
        return bw->io_err ? -2 : 0;
    }

    /* prefix = erstes Symbol. */
    uint32_t prefix = (uint32_t)indices[0];

    for (size_t i = 1; i < count; ++i) {
        uint32_t cur = (uint32_t)indices[i];
        uint32_t composite = (prefix << 8) | cur; /* unsigned, prefix<4096 */
        uint32_t slot = 0u;
        uint32_t found = moo_gif_lzw_lookup(&tbl, composite, &slot);

        if (found != 0xFFFFFFFFu) {
            /* (prefix,cur) bekannt -> verlaengern. */
            prefix = found;
        } else {
            /* prefix ausgeben, neuen Eintrag anlegen. */
            moo_gif_bw_put_code(bw, prefix, code_size);

            if (next_code < MOO_GIF_MAX_CODE) {
                tbl.key[slot]  = composite;
                tbl.code[slot] = next_code;
                ++next_code;
                /* GIF-"Early-Change": Der Decoder baut seine String-Tabelle um
                 * EINEN Code verzoegert auf (er fuegt einen Eintrag erst beim
                 * Empfang des FOLGENDEN Codes hinzu). Damit Encoder und Decoder
                 * dieselbe Lesebreite verwenden, erhoeht der Encoder die Breite
                 * eine Stufe SPAETER als naives LZW: erst wenn next_code die
                 * aktuelle Breite ueberschreitet (next_code > 2^code_size), nicht
                 * schon bei Gleichheit. So bleibt der nachhinkende Decoder
                 * synchron. (giflib-Konvention.) (1u<<code_size) unsigned -> kein UB. */
                if (next_code > (1u << code_size) && code_size < 12u) {
                    ++code_size;
                }
            } else {
                /* Tabelle voll -> Clear + Reset. */
                moo_gif_bw_put_code(bw, MOO_GIF_CLEAR_CODE, code_size);
                moo_gif_lzw_table_reset(&tbl);
                code_size = MOO_GIF_MIN_CODE_SIZE + 1u;
                next_code = MOO_GIF_FIRST_CODE;
            }
            prefix = cur;
        }

        if (bw->io_err) {
            moo_gif_lzw_table_free(&tbl);
            return -2;
        }
    }

    /* Letztes prefix + EOI. */
    moo_gif_bw_put_code(bw, prefix, code_size);
    moo_gif_bw_put_code(bw, MOO_GIF_EOI_CODE, code_size);
    moo_gif_bw_finish(bw);

    moo_gif_lzw_table_free(&tbl);
    return bw->io_err ? -2 : 0;
}

/* ======================================================================== */
/*  Writer-Struktur + Header-/Frame-/Trailer-Emission.                       */
/* ======================================================================== */

struct MooGifWriter {
    FILE*    fp;
    int      width;
    int      height;
    uint16_t delay_cs;   /* Frame-Delay in 1/100 s */
    size_t   frame_count;
    int      status;     /* gespeicherter MooGifStatus; != OK -> verdorben */
    uint8_t  palette[MOO_GIF_PALETTE_SIZE * 3];
};

/* Little-Endian uint16 schreiben. */
static int moo_gif_put_u16(FILE* fp, uint16_t v) {
    if (fputc((int)(v & 0xFFu), fp) == EOF) return -1;
    if (fputc((int)((v >> 8) & 0xFFu), fp) == EOF) return -1;
    return 0;
}

MooGifWriter* moo_gif_open(const char* path, int w, int h, int fps) {
    if (!path || w <= 0 || h <= 0 || w > 65535 || h > 65535 || fps <= 0) {
        return NULL;
    }

    MooGifWriter* g = (MooGifWriter*)calloc(1, sizeof(MooGifWriter));
    if (!g) return NULL;

    g->fp = fopen(path, "wb");
    if (!g->fp) {
        free(g);
        return NULL;
    }
    g->width  = w;
    g->height = h;
    g->frame_count = 0;
    g->status = MOO_GIF_OK;

    /* fps -> Delay in Hundertstelsekunden. Mindestens 1 (= 1/100 s).
     * 100/fps; alle Operanden positiv -> kein UB. */
    {
        int d = 100 / fps;
        if (d < 1) d = 1;
        if (d > 65535) d = 65535;
        g->delay_cs = (uint16_t)d;
    }

    moo_gif_build_palette(g->palette);

    /* --- Header "GIF89a" --- */
    if (fwrite("GIF89a", 1, 6, g->fp) != 6) goto io_fail;

    /* --- Logical Screen Descriptor --- */
    if (moo_gif_put_u16(g->fp, (uint16_t)w) != 0) goto io_fail;
    if (moo_gif_put_u16(g->fp, (uint16_t)h) != 0) goto io_fail;
    /* Packed: GCT vorhanden(0x80) | Farbaufloesung 7<<4(0x70) |
     * GCT-Groesse 7 (=> 2^(7+1)=256). = 0xF7. */
    if (fputc(0xF7, g->fp) == EOF) goto io_fail;
    if (fputc(0x00, g->fp) == EOF) goto io_fail; /* Background-Color-Index */
    if (fputc(0x00, g->fp) == EOF) goto io_fail; /* Pixel-Aspect-Ratio */

    /* --- Global Color Table (256 * 3 Byte) --- */
    if (fwrite(g->palette, 1, MOO_GIF_PALETTE_SIZE * 3, g->fp)
        != (size_t)(MOO_GIF_PALETTE_SIZE * 3)) goto io_fail;

    /* --- NetscapeLoop-Extension (Endlosschleife) --- */
    if (fputc(0x21, g->fp) == EOF) goto io_fail; /* Extension Introducer */
    if (fputc(0xFF, g->fp) == EOF) goto io_fail; /* Application Extension */
    if (fputc(0x0B, g->fp) == EOF) goto io_fail; /* Block-Size 11 */
    if (fwrite("NETSCAPE2.0", 1, 11, g->fp) != 11) goto io_fail;
    if (fputc(0x03, g->fp) == EOF) goto io_fail; /* Sub-Block-Size 3 */
    if (fputc(0x01, g->fp) == EOF) goto io_fail; /* Sub-Block-ID */
    if (moo_gif_put_u16(g->fp, 0u) != 0) goto io_fail; /* Loop-Count 0 = unendlich */
    if (fputc(0x00, g->fp) == EOF) goto io_fail; /* Block-Terminator */

    return g;

io_fail:
    fclose(g->fp);
    free(g);
    return NULL;
}

int moo_gif_add_frame(MooGifWriter* g, const uint8_t* rgba, int w, int h) {
    if (!g) return MOO_GIF_ERR_ARG;
    if (g->status != MOO_GIF_OK) return g->status; /* bereits verdorben */
    if (!rgba) return MOO_GIF_ERR_ARG;
    if (w != g->width || h != g->height) return MOO_GIF_ERR_DIM;

    size_t npix = (size_t)w * (size_t)h;

    /* Index-Puffer (ein Byte/Pixel) — einzige Frame-RAM-Allokation, sofort
     * nach dem LZW-Schreiben freigegeben. Frame-bounded. */
    uint8_t* indices = (uint8_t*)malloc(npix > 0 ? npix : 1u);
    if (!indices) {
        g->status = MOO_GIF_ERR_NOMEM;
        return MOO_GIF_ERR_NOMEM;
    }

    /* Quantisierung RGBA->Index (top-left origin, Alpha ignoriert). */
    for (size_t p = 0; p < npix; ++p) {
        const uint8_t* px = rgba + p * 4u;
        indices[p] = moo_gif_nearest(g->palette, px[0], px[1], px[2]);
    }

    /* --- Graphic Control Extension (Delay) --- */
    if (fputc(0x21, g->fp) == EOF) goto io_fail; /* Extension Introducer */
    if (fputc(0xF9, g->fp) == EOF) goto io_fail; /* Graphic Control Label */
    if (fputc(0x04, g->fp) == EOF) goto io_fail; /* Block-Size 4 */
    if (fputc(0x00, g->fp) == EOF) goto io_fail; /* Packed: kein Transparenz, Disposal 0 */
    if (moo_gif_put_u16(g->fp, g->delay_cs) != 0) goto io_fail; /* Delay (1/100s) */
    if (fputc(0x00, g->fp) == EOF) goto io_fail; /* Transparent-Color-Index */
    if (fputc(0x00, g->fp) == EOF) goto io_fail; /* Block-Terminator */

    /* --- Image Descriptor --- */
    if (fputc(0x2C, g->fp) == EOF) goto io_fail; /* Image Separator */
    if (moo_gif_put_u16(g->fp, 0u) != 0) goto io_fail;          /* Left */
    if (moo_gif_put_u16(g->fp, 0u) != 0) goto io_fail;          /* Top */
    if (moo_gif_put_u16(g->fp, (uint16_t)w) != 0) goto io_fail; /* Breite */
    if (moo_gif_put_u16(g->fp, (uint16_t)h) != 0) goto io_fail; /* Hoehe */
    if (fputc(0x00, g->fp) == EOF) goto io_fail; /* Packed: keine lokale Farbtabelle */

    /* --- LZW-komprimierte Bilddaten --- */
    {
        MooGifBitWriter bw;
        moo_gif_bw_init(&bw, g->fp);
        int rc = moo_gif_lzw_encode(&bw, indices, npix);
        if (rc != 0) {
            free(indices);
            g->status = MOO_GIF_ERR_IO;
            return MOO_GIF_ERR_IO;
        }
    }

    free(indices);
    g->frame_count++;
    return MOO_GIF_OK;

io_fail:
    free(indices);
    g->status = MOO_GIF_ERR_IO;
    return MOO_GIF_ERR_IO;
}

int moo_gif_close(MooGifWriter* g) {
    if (!g) return MOO_GIF_OK; /* NULL = No-Op */

    int result = MOO_GIF_OK;
    if (g->status != MOO_GIF_OK) {
        result = MOO_GIF_ERR_IO; /* frueherer Fehler propagiert */
    }

    /* Trailer 0x3B — auch bei verdorbenem Writer versuchen, sauber zu schliessen. */
    if (g->fp) {
        if (fputc(0x3B, g->fp) == EOF) result = MOO_GIF_ERR_IO;
        if (fclose(g->fp) != 0) result = MOO_GIF_ERR_IO;
        g->fp = NULL;
    }

    free(g);
    return result;
}

size_t moo_gif_frame_count(const MooGifWriter* g) {
    return g ? g->frame_count : 0u;
}
