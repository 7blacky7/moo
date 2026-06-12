/**
 * moo_video.c - Isolierter MP4/H.264-Encoder-Kern via ffmpeg-Kindprozess
 *   (Plan-009 P009-V0, TEIL 1). Reine C/POSIX, KEINE moo-Abhaengigkeit.
 *
 * Sicherheits-/Invarianten-Design (plan-009-mp4-video-dispatch-koordination):
 *   - fork + pipe + execvp mit argv. KEIN popen, KEIN sprintf-Shellstring,
 *     keine Shell -> keine Injection ueber den Zielpfad.
 *   - Parent schliesst sofort das Read-Ende der Pipe; Child dup2(read, STDIN),
 *     schliesst beide originalen Pipe-FDs und _exit(127) bei exec-Fehler.
 *   - SIGPIPE wird ignoriert (signal SIG_IGN); ein write() auf eine kaputte
 *     Pipe liefert dann EPIPE statt den Prozess zu killen -> wir propagieren
 *     den Fehler als MOO_VIDEO_ERR_IO.
 *   - Frame-bounded: add_frame schreibt exakt w*h*4 Bytes, kein RAM-Sammeln.
 *   - close() schliesst stdin, waitpid(child) (kein Zombie), meldet nonzero
 *     ffmpeg-Exit. Doppel-close idempotent.
 *
 * ub-arithmetik-policy: Die einzige Groessenrechnung ist die Byte-Anzahl pro
 * Frame. w/h sind beim Open auf 1..65535 begrenzt, daher passt (size_t)w*h*4
 * sicher in size_t (max ~1.7e10 < SIZE_MAX). Multiplikation explizit in size_t.
 */
#include "moo_video.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

/* Privater Writer-Zustand. */
struct MooVideoWriter {
    pid_t   child;       /* ffmpeg-PID, oder -1 wenn schon eingesammelt */
    int     wpipe_fd;    /* Schreib-Ende der Pipe zu ffmpeg-stdin, oder -1 */
    int     width;       /* beim Open fixiert */
    int     height;
    int     fps;
    size_t  frame_count; /* erfolgreich gepipte Frames */
    int     err;         /* gespeicherter Fehler (MooVideoStatus), 0 = ok */
};

/* Schreibt buf vollstaendig (Schleife gegen partielle writes). Liefert true bei
 * Erfolg, false bei Fehler (errno gesetzt, inkl. EPIPE bei totem ffmpeg). */
static bool write_all(int fd, const uint8_t* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue; /* Signal -> erneut versuchen */
            return false;
        }
        if (n == 0) return false; /* sollte bei Pipe nicht passieren */
        off += (size_t)n;
    }
    return true;
}

MooVideoWriter* moo_video_open(const char* path, int w, int h, int fps) {
    if (!path || w <= 0 || h <= 0 || fps <= 0) return NULL;
    if (w > 65535 || h > 65535) return NULL;        /* Bound fuer size_t-Mul */
    if ((w & 1) != 0 || (h & 1) != 0) return NULL;  /* yuv420p braucht gerade */

    /* SIGPIPE ignorieren, damit ein toter ffmpeg uns nicht per Signal killt.
     * Prozessweit, idempotent (mehrfach SIG_IGN setzen ist harmlos). */
    signal(SIGPIPE, SIG_IGN);

    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;

    /* argv fuer ffmpeg vorbereiten. Zahlen lokal in stack-Puffer formatieren
     * (KEIN Shell-String, das sind reine argv-Elemente). */
    char s_arg[64];
    char r_arg[32];
    snprintf(s_arg, sizeof(s_arg), "%dx%d", w, h);
    snprintf(r_arg, sizeof(r_arg), "%d", fps);

    /* path wird NICHT interpretiert/gequotet - es ist ein eigenes argv-Element. */
    char* const argv[] = {
        "ffmpeg", "-y",
        "-f", "rawvideo",
        "-pix_fmt", "rgba",
        "-s", s_arg,
        "-r", r_arg,
        "-i", "-",
        "-an",
        "-c:v", "libx264",
        "-preset", "veryfast",
        "-crf", "20",
        "-pix_fmt", "yuv420p",
        (char*)path,
        NULL
    };

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    if (pid == 0) {
        /* === Child === */
        /* stdin = Read-Ende der Pipe. */
        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            _exit(127);
        }
        /* Originale Pipe-FDs schliessen (das dup bleibt als STDIN erhalten). */
        close(pipefd[0]);
        close(pipefd[1]);
        execvp("ffmpeg", argv);
        /* Nur erreichbar, wenn exec fehlschlug (ffmpeg fehlt o.ae.). */
        _exit(127);
    }

    /* === Parent === */
    close(pipefd[0]); /* Read-Ende gehoert dem Child. */

    MooVideoWriter* v = (MooVideoWriter*)malloc(sizeof(MooVideoWriter));
    if (!v) {
        /* Handle-Alloc fehlgeschlagen: Pipe schliessen + Child einsammeln. */
        close(pipefd[1]);
        int st;
        waitpid(pid, &st, 0);
        return NULL;
    }
    memset(v, 0, sizeof(MooVideoWriter)); /* moo_alloc/malloc nullt nicht */
    v->child       = pid;
    v->wpipe_fd    = pipefd[1];
    v->width       = w;
    v->height      = h;
    v->fps         = fps;
    v->frame_count = 0;
    v->err         = MOO_VIDEO_OK;
    return v;
}

int moo_video_add_frame(MooVideoWriter* v, const uint8_t* rgba, int w, int h) {
    if (!v || !rgba) return MOO_VIDEO_ERR_ARG;
    if (v->err != MOO_VIDEO_OK) return v->err;   /* verdorben: gespeicherten Fehler */
    if (w != v->width || h != v->height) {
        v->err = MOO_VIDEO_ERR_DIM;
        return MOO_VIDEO_ERR_DIM;
    }
    if (v->wpipe_fd < 0) {
        v->err = MOO_VIDEO_ERR_IO;
        return MOO_VIDEO_ERR_IO;
    }
    /* Bytes pro Frame: w,h in [1,65535] (Open) -> passt sicher in size_t. */
    size_t nbytes = (size_t)w * (size_t)h * 4u;
    if (!write_all(v->wpipe_fd, rgba, nbytes)) {
        /* ffmpeg vorzeitig tot (EPIPE) o.ae. -> Writer verderben. */
        v->err = MOO_VIDEO_ERR_IO;
        return MOO_VIDEO_ERR_IO;
    }
    v->frame_count++;
    return MOO_VIDEO_OK;
}

int moo_video_close(MooVideoWriter* v) {
    if (!v) return MOO_VIDEO_OK; /* idempotent / No-Op */

    int result = (v->err == MOO_VIDEO_ERR_IO) ? MOO_VIDEO_ERR_IO : MOO_VIDEO_OK;

    /* stdin schliessen -> ffmpeg sieht EOF, finalisiert die Datei und beendet. */
    if (v->wpipe_fd >= 0) {
        close(v->wpipe_fd);
        v->wpipe_fd = -1;
    }

    /* Kindprozess einsammeln (kein Zombie). */
    if (v->child > 0) {
        int st = 0;
        pid_t r;
        do {
            r = waitpid(v->child, &st, 0);
        } while (r < 0 && errno == EINTR);
        v->child = -1;
        if (r > 0) {
            /* nonzero ffmpeg-Exit sichtbar machen (ueberschreibt OK, nicht IO). */
            bool ok_exit = WIFEXITED(st) && WEXITSTATUS(st) == 0;
            if (!ok_exit && result == MOO_VIDEO_OK) {
                result = MOO_VIDEO_ERR_FFMPEG;
            }
        }
    }

    free(v);
    return result;
}

size_t moo_video_frame_count(const MooVideoWriter* v) {
    return v ? v->frame_count : 0;
}
