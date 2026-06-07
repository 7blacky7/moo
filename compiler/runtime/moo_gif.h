/**
 * moo_gif.h - Isolierter GIF89a + LZW Animations-Encoder-Kern (Plan-008 P008-A3B).
 *
 * TEIL 1 von P008-A3B: reiner C-Encoder-Kern OHNE jede MOO-Abhaengigkeit.
 *   - KEINE MooValue, KEIN moo_runtime.h, KEINE MOO_FRAME-Abhaengigkeit.
 *   - KEIN build.rs / cargo / Bindings (das ist TEIL 2, nach P008-A3A).
 * Diese Signaturen sind der VERTRAG, auf dem TEIL 2 (test_gif_*-Builtins +
 * MOO_FRAME-Verdrahtung) aufsetzt.
 *
 * Eigenschaften:
 *   - Streaming direkt in eine FILE* (frame-bounded): es wird NIE die komplette
 *     Frame-Sequenz im RAM gehalten. Pro add_frame nur ein Index-Puffer (w*h)
 *     + LZW-Arbeitsstrukturen. Damit unkritisch bei Ultrawide (~20 MB RGBA/Frame).
 *   - Deterministische Fixpalette (6x6x6 Websafe-Wuerfel = 216 + 40 Graustufen
 *     = 256 Eintraege). Reproduzierbarkeit > Qualitaet. Median-Cut ist A3c.
 *   - Quantisierung RGBA->Index per Nearest-Color (deterministisch).
 *   - LZW-Encoder: variable Code-Breite, Clear-/EOI-Codes, Bit-Packing
 *     AUSSCHLIESSLICH UNSIGNED (uint32-Akkumulator) gemaess ub-arithmetik-policy.
 *   - Eingabe-Konvention: RGBA, 4 Byte/Pixel, top-left origin (Standard aus Plan).
 *   - NetscapeLoop-Extension fuer Endlosschleife.
 *
 * Build-Status: standalone via gcc/clang. Verifiziert unter ASan (detect_leaks=1)
 * und UBSan (-fsanitize=undefined -fno-sanitize-recover=undefined) — siehe
 * compiler/runtime/tests/test_gif_core_asan.c.
 */
#ifndef MOO_GIF_H
#define MOO_GIF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaker Writer-Handle. Lebensdauer/Felder sind privat (siehe moo_gif.c). */
typedef struct MooGifWriter MooGifWriter;

/* Rueckgabe-Codes der GIF-API. 0 == Erfolg, negativ == Fehler. */
typedef enum {
    MOO_GIF_OK            =  0,
    MOO_GIF_ERR_ARG       = -1,  /* ungueltiges Argument (NULL, w/h <= 0, ...) */
    MOO_GIF_ERR_IO        = -2,  /* Datei oeffnen/schreiben/schliessen schlug fehl */
    MOO_GIF_ERR_NOMEM     = -3,  /* Speicheranforderung fehlgeschlagen */
    MOO_GIF_ERR_DIM       = -4   /* Frame-Dimension passt nicht zum geoeffneten GIF */
} MooGifStatus;

/*
 * moo_gif_open - Oeffnet eine GIF89a-Datei zum Schreiben und emittiert Header,
 *   Logical Screen Descriptor, Global Color Table und die NetscapeLoop-Extension.
 *
 *   path : Zielpfad. Wird mit "wb" geoeffnet (vorhandene Datei wird ueberschrieben).
 *   w, h : Frame-Breite/-Hoehe in Pixeln, > 0 und <= 65535.
 *   fps  : Bilder pro Sekunde, > 0. Wird intern in GIF-Delay (1/100 s) umgerechnet;
 *          minimal 1 (= 1/100 s). 0 oder negativ -> MOO_GIF_ERR_ARG.
 *
 *   Rueckgabe: gueltiger MooGifWriter* bei Erfolg, NULL bei Fehler (ungueltige
 *   Argumente, Datei nicht oeffenbar, Speichermangel).
 *
 *   Eigentum: Der Aufrufer MUSS den Handle mit genau einem moo_gif_close()
 *   freigeben — auch wenn add_frame fehlschlaegt.
 */
MooGifWriter* moo_gif_open(const char* path, int w, int h, int fps);

/*
 * moo_gif_add_frame - Quantisiert ein RGBA-Frame, LZW-komprimiert es und
 *   streamt Graphic Control Extension + Image Descriptor + Bilddaten direkt in
 *   die Datei. Es wird KEINE Frame-Kopie im Writer aufbewahrt.
 *
 *   g    : Writer aus moo_gif_open (!= NULL).
 *   rgba : Pixelpuffer, w*h*4 Bytes, RGBA, top-left origin. Alpha wird ignoriert
 *          (kein Transparenz-Index in dieser Minimalversion).
 *   w, h : MUESSEN den Werten aus moo_gif_open entsprechen, sonst MOO_GIF_ERR_DIM.
 *
 *   Rueckgabe: MOO_GIF_OK oder negativer MooGifStatus. Nach einem IO-Fehler
 *   ist der Writer "verdorben"; weitere add_frame-Aufrufe geben sofort den
 *   gespeicherten Fehler zurueck. close() bleibt zwingend zum Freigeben.
 */
int moo_gif_add_frame(MooGifWriter* g, const uint8_t* rgba, int w, int h);

/*
 * moo_gif_close - Schreibt den GIF-Trailer (0x3B), schliesst die Datei und gibt
 *   alle Ressourcen frei. Der Handle ist danach ungueltig.
 *
 *   g : Writer (NULL ist erlaubt und ein No-Op, Rueckgabe MOO_GIF_OK).
 *
 *   Rueckgabe: MOO_GIF_OK, oder MOO_GIF_ERR_IO falls Trailer/fclose fehlschlug
 *   bzw. ein frueherer Schreibfehler anstand.
 */
int moo_gif_close(MooGifWriter* g);

/*
 * moo_gif_frame_count - Wie viele Frames bisher erfolgreich geschrieben wurden.
 *   Diagnose/Tests. NULL -> 0.
 */
size_t moo_gif_frame_count(const MooGifWriter* g);

#ifdef __cplusplus
}
#endif

#endif /* MOO_GIF_H */
