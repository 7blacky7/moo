/**
 * moo_gif_handle.c — moo-Heap-Wrapper fuer den isolierten GIF-Encoder-Kern
 *   (Plan-008 P008-A3B, Verdrahtungs-Teil).
 *
 * Trennung der Verantwortung:
 *   - moo_gif.c    : reiner GIF89a+LZW-Encoder, KEINE moo-Abhaengigkeit (Teil 1).
 *   - moo_gif_handle.c (DIESE Datei): nur der refcountete moo-Heap-Typ MOO_GIF,
 *     der einen MooGifWriter* haelt. Hat moo-Abhaengigkeit (moo_runtime.h),
 *     aber KEINE Backend-/Fenster-Abhaengigkeit -> IMMER Teil des Builds.
 *   - moo_test_api.c (nur 3D-Build): die test_gif_*-Builtins, die Fenster
 *     grabben bzw. ein MOO_FRAME nehmen und hierhin/an moo_gif.c weiterreichen.
 *
 * Warum eine eigene immer-gebaute Datei? moo_memory.c (Core) dispatcht in
 * moo_release() den MOO_GIF-Fall auf moo_gif_handle_free(). Dieses Symbol MUSS
 * daher in jedem Build aufloesbar sein — exakt dasselbe Muster wie
 * moo_frame_free()/moo_frame.c. moo_gif.c selbst ist pure C ohne Backend-Deps
 * und wird darum ebenfalls in den Core-Build aufgenommen (build.rs).
 *
 * Lebenszyklus / Refcount-Konvention:
 *   - moo_gif_handle_new(writer) startet bei refcount=1 (MooGifHandle.refcount
 *     ist das ERSTE Feld, siehe moo_runtime.h).
 *   - test_gif_ende() schliesst den Writer regulaer (Trailer+close) und setzt
 *     handle->writer = NULL, sodass das spaetere moo_release() nicht doppelt
 *     schliesst.
 *   - Wird der Handle freigegeben (refcount 0) BEVOR test_gif_ende lief (z.B.
 *     Variable verlaesst den Scope nach einem Fehler), schliesst
 *     moo_gif_handle_free() den noch offenen Writer sauber ab — kein Leak, kein
 *     korruptes/halb-geschriebenes GIF (es bekommt zumindest einen Trailer).
 */

#include "moo_runtime.h"
#include "moo_gif.h"

#include <stdlib.h>

/* Erzeugt einen MOO_GIF-Handle, der den (bereits geoeffneten) Writer uebernimmt.
 * Bei writer==NULL oder Speichermangel wird der Writer geschlossen (kein Leak)
 * und NONE zurueckgegeben. take ownership des Writers. */
MooValue moo_gif_handle_new(MooGifWriter* writer) {
    if (!writer) {
        return moo_none();
    }
    MooGifHandle* h = (MooGifHandle*)malloc(sizeof(MooGifHandle));
    if (!h) {
        moo_gif_close(writer); /* Trailer + fclose, kein Leak */
        return moo_none();
    }
    h->refcount = 1;
    h->writer   = writer;

    MooValue v;
    v.tag = MOO_GIF;
    moo_val_set_ptr(&v, h);
    return v;
}

/* Refcount-0-Destruktor (aus moo_release/MOO_GIF). Schliesst einen ggf. noch
 * offenen Writer sicher ab und gibt den Handle frei. */
void moo_gif_handle_free(void* ptr) {
    if (!ptr) return;
    MooGifHandle* h = (MooGifHandle*)ptr;
    if (h->writer) {
        moo_gif_close(h->writer); /* idempotent gegen test_gif_ende via NULL-Setzen */
        h->writer = NULL;
    }
    free(h);
}
