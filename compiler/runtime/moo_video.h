/**
 * moo_video.h - Isolierter MP4/H.264-Encoder-Kern via ffmpeg-Kindprozess
 *   (Plan-009 P009-V0).
 *
 * TEIL 1 von P009-V0: reiner C-Kern OHNE jede MOO-Abhaengigkeit.
 *   - KEINE MooValue, KEIN moo_runtime.h, KEINE MOO_FRAME-Abhaengigkeit.
 *   - KEINE Backend-/3D-Abhaengigkeit -> baut immer (Core).
 * Diese Signaturen sind der VERTRAG, auf dem TEIL 2 (test_video_*-Builtins +
 * MOO_FRAME-Verdrahtung, P009-V1) aufsetzt.
 *
 * Strategie (verbindlich, plan-009-mp4-video-dispatch-koordination):
 *   - ffmpeg wird als Kindprozess via pipe + fork + execvp gestartet. KEIN
 *     popen, KEIN sprintf zu einem Shell-String -> keine Shell, keine
 *     Injection. Der Zielpfad geht als eigenes argv-Element an ffmpeg.
 *   - RGBA rawvideo wird Frame fuer Frame in ffmpeg-stdin gepiped (frame-
 *     bounded): pro add_frame exakt w*h*4 Bytes, NIE eine Frame-Sequenz im RAM.
 *   - ffmpeg konvertiert nach H.264/yuv420p in eine .mp4-Datei:
 *       ffmpeg -y -f rawvideo -pix_fmt rgba -s WxH -r FPS -i -
 *              -an -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p OUT
 *   - yuv420p verlangt GERADE Breite/Hoehe -> ungerade Dims = MOO_VIDEO_ERR_DIM
 *     (kein scale/pad im PoC).
 *
 * Lebenszyklus / Zombie-Schutz:
 *   - moo_video_open() forkt ffmpeg und gibt einen MooVideoWriter* zurueck.
 *   - moo_video_close() schliesst stdin (-> ffmpeg flush/exit), waitpid(child)
 *     und meldet einen nonzero ffmpeg-Exit als MOO_VIDEO_ERR_FFMPEG. Doppel-
 *     close ist idempotent (NULL -> No-Op).
 *   - SIGPIPE wird beim Open prozessweit ignoriert; ein Schreibfehler auf die
 *     Pipe (ffmpeg vorzeitig tot) wird als MOO_VIDEO_ERR_IO propagiert statt
 *     den Prozess per Signal zu killen.
 *
 * Build-Status: standalone via gcc/clang, POSIX (fork/execvp/pipe/waitpid).
 * Windows ist explizit out-of-scope-PoC (siehe Plan-009).
 */
#ifndef MOO_VIDEO_H
#define MOO_VIDEO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaker Writer-Handle. Lebensdauer/Felder sind privat (siehe moo_video.c). */
typedef struct MooVideoWriter MooVideoWriter;

/* Rueckgabe-Codes der Video-API. 0 == Erfolg, negativ == Fehler. */
typedef enum {
    MOO_VIDEO_OK         =  0,
    MOO_VIDEO_ERR_ARG    = -1,  /* ungueltiges Argument (NULL, w/h<=0, fps<=0) */
    MOO_VIDEO_ERR_DIM    = -2,  /* Frame-Dim passt nicht / ungerade fuer yuv420p */
    MOO_VIDEO_ERR_IO     = -3,  /* Pipe-Schreibfehler (ffmpeg vorzeitig tot o.ae.) */
    MOO_VIDEO_ERR_SPAWN  = -4,  /* fork/pipe/execvp-Setup fehlgeschlagen */
    MOO_VIDEO_ERR_FFMPEG = -5   /* ffmpeg mit nonzero Status beendet */
} MooVideoStatus;

/*
 * moo_video_open - Startet ffmpeg als Kindprozess und oeffnet die Schreib-Pipe.
 *
 *   path : Zielpfad der .mp4. Geht als eigenes argv-Element an ffmpeg (keine
 *          Shell, kein Quoting noetig). Vorhandene Datei wird via -y ueberschrieben.
 *   w, h : Frame-Breite/-Hoehe in Pixeln, > 0. MUESSEN GERADE sein (yuv420p);
 *          ungerade -> NULL (Fehler, weil yuv420p sonst spaeter scheitert).
 *   fps  : Bilder pro Sekunde, > 0.
 *
 *   Rueckgabe: gueltiger MooVideoWriter* bei Erfolg, NULL bei Fehler (ungueltige
 *   Argumente, ungerade Dims, fork/pipe/exec-Setup fehlgeschlagen).
 *
 *   Eigentum: Der Aufrufer MUSS den Handle mit genau einem moo_video_close()
 *   freigeben - auch wenn add_frame fehlschlaegt (sonst Zombie + Leak).
 */
MooVideoWriter* moo_video_open(const char* path, int w, int h, int fps);

/*
 * moo_video_add_frame - Schreibt ein RGBA-Frame (w*h*4 Bytes, top-left origin)
 *   exakt in ffmpeg-stdin. Frame-bounded: keine Kopie, kein Sammeln.
 *
 *   v    : Writer aus moo_video_open (!= NULL).
 *   rgba : Pixelpuffer, w*h*4 Bytes, RGBA, top-left origin.
 *   w, h : MUESSEN den Werten aus moo_video_open entsprechen, sonst
 *          MOO_VIDEO_ERR_DIM.
 *
 *   Rueckgabe: MOO_VIDEO_OK oder negativer MooVideoStatus. Nach einem Pipe-
 *   Fehler ist der Writer "verdorben"; weitere add_frame-Aufrufe geben sofort
 *   den gespeicherten Fehler zurueck. close() bleibt zwingend zum Freigeben.
 */
int moo_video_add_frame(MooVideoWriter* v, const uint8_t* rgba, int w, int h);

/*
 * moo_video_close - Schliesst stdin (ffmpeg finalisiert die Datei), wartet per
 *   waitpid auf den Kindprozess (kein Zombie) und gibt alle Ressourcen frei.
 *   Der Handle ist danach ungueltig.
 *
 *   v : Writer (NULL ist erlaubt und ein No-Op, Rueckgabe MOO_VIDEO_OK).
 *
 *   Rueckgabe: MOO_VIDEO_OK; MOO_VIDEO_ERR_FFMPEG bei nonzero ffmpeg-Exit;
 *   MOO_VIDEO_ERR_IO falls bereits ein Schreibfehler anstand.
 */
int moo_video_close(MooVideoWriter* v);

/*
 * moo_video_frame_count - Wie viele Frames bisher erfolgreich gepiped wurden.
 *   Diagnose/Tests. NULL -> 0.
 */
size_t moo_video_frame_count(const MooVideoWriter* v);

#ifdef __cplusplus
}
#endif

#endif /* MOO_VIDEO_H */
