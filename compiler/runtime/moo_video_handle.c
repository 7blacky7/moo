/**
 * moo_video_handle.c - moo-Heap-Wrapper fuer den isolierten ffmpeg-Video-Kern
 *   (Plan-009 P009-V0, Verdrahtungs-Teil).
 *
 * Trennung der Verantwortung (analog moo_gif.c / moo_gif_handle.c):
 *   - moo_video.c          : reiner ffmpeg-Pipe-Kern, KEINE moo-Abhaengigkeit.
 *   - moo_video_handle.c (DIESE Datei): nur der refcountete moo-Heap-Typ
 *     MOO_VIDEO, der einen MooVideoWriter* haelt. Hat moo-Abhaengigkeit
 *     (moo_runtime.h), aber KEINE Backend-/Fenster-Abhaengigkeit -> IMMER
 *     Teil des Builds.
 *   - moo_test_api.c (nur 3D-Build, P009-V1): die test_video_*-Builtins, die
 *     ein Fenster grabben bzw. ein MOO_FRAME nehmen und hierhin/an moo_video.c
 *     weiterreichen.
 *
 * Warum eine eigene immer-gebaute Datei? moo_memory.c (Core) dispatcht in
 * moo_release() den MOO_VIDEO-Fall auf moo_video_handle_free(). Dieses Symbol
 * MUSS daher in jedem Build aufloesbar sein - exakt dasselbe Muster wie
 * moo_gif_handle_free()/moo_gif_handle.c.
 *
 * Lebenszyklus / Refcount-Konvention:
 *   - moo_video_handle_new(writer) startet bei refcount=1 (MooVideoHandle.refcount
 *     ist das ERSTE Feld, siehe moo_runtime.h).
 *   - test_video_ende() (P009-V1) schliesst den Writer regulaer (stdin close +
 *     waitpid) und setzt handle->writer = NULL, sodass das spaetere
 *     moo_release() nicht doppelt schliesst.
 *   - Wird der Handle freigegeben (refcount 0) BEVOR test_video_ende lief (z.B.
 *     Variable verlaesst den Scope nach einem Fehler), schliesst
 *     moo_video_handle_free() den noch offenen Writer sauber ab - kein Leak,
 *     kein Zombie (close ruft intern waitpid).
 */

#include "moo_runtime.h"
#include "moo_video.h"

#include <stdlib.h>

/* Erzeugt einen MOO_VIDEO-Handle, der den (bereits geoeffneten) Writer
 * uebernimmt. Bei writer==NULL oder Speichermangel wird der Writer geschlossen
 * (kein Leak/Zombie) und NONE zurueckgegeben. take ownership des Writers. */
MooValue moo_video_handle_new(MooVideoWriter* writer) {
    if (!writer) {
        return moo_none();
    }
    MooVideoHandle* h = (MooVideoHandle*)malloc(sizeof(MooVideoHandle));
    if (!h) {
        moo_video_close(writer); /* stdin close + waitpid, kein Leak/Zombie */
        return moo_none();
    }
    h->refcount = 1;
    h->writer   = writer;

    MooValue v;
    v.tag = MOO_VIDEO;
    moo_val_set_ptr(&v, h);
    return v;
}

/* Refcount-0-Destruktor (aus moo_release/MOO_VIDEO). Schliesst einen ggf. noch
 * offenen Writer sicher ab (stdin close + waitpid) und gibt den Handle frei. */
void moo_video_handle_free(void* ptr) {
    if (!ptr) return;
    MooVideoHandle* h = (MooVideoHandle*)ptr;
    if (h->writer) {
        moo_video_close(h->writer); /* idempotent gegen test_video_ende via NULL */
        h->writer = NULL;
    }
    free(h);
}
