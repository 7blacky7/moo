/**
 * moo_video.c - Isolierter MP4/H.264-Encoder-Kern via ffmpeg-Kindprozess
 *   (Plan-009 P009-V0, TEIL 1). Reine C, KEINE moo-Abhaengigkeit.
 *
 * Sicherheits-/Invarianten-Design (plan-009-mp4-video-dispatch-koordination):
 *   - POSIX: fork + pipe + execvp mit argv. KEIN popen, KEIN sprintf-Shell-
 *     string, keine Shell -> keine Injection ueber den Zielpfad.
 *   - Windows (P013): CreatePipe + CreateProcess — ebenfalls KEINE Shell.
 *     Der Zielpfad wird nach den Windows-CRT-Argv-Regeln gequotet (inkl.
 *     Backslash-vor-Quote-Verdopplung), bleibt also ein reines Argument.
 *     Das Pipe-Write-Ende wird explizit NICHT vererbt, sonst hielte ffmpeg
 *     es selbst offen und saehe beim Parent-Close nie EOF.
 *   - Parent schliesst sofort das Read-Ende der Pipe; Child bekommt es als
 *     STDIN (POSIX: dup2 + _exit(127) bei exec-Fehler; Windows: hStdInput).
 *   - SIGPIPE wird unter POSIX ignoriert (signal SIG_IGN); ein write() auf
 *     eine kaputte Pipe liefert dann EPIPE statt den Prozess zu killen -> wir
 *     propagieren den Fehler als MOO_VIDEO_ERR_IO. Unter Windows existiert
 *     SIGPIPE nicht; WriteFile liefert bei kaputter Pipe FALSE -> selber Pfad.
 *   - Frame-bounded: add_frame schreibt exakt w*h*4 Bytes, kein RAM-Sammeln.
 *   - close() schliesst stdin, sammelt das Kind ein (waitpid bzw.
 *     WaitForSingleObject+GetExitCodeProcess, kein Zombie), meldet nonzero
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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef HANDLE moo_vid_proc_t;  /* Prozess-Handle, NULL = eingesammelt */
typedef HANDLE moo_vid_pipe_t;  /* Pipe-Write-Handle */
#define MOO_VID_PIPE_BAD(h)  ((h) == INVALID_HANDLE_VALUE)
#define MOO_VID_PIPE_NONE    INVALID_HANDLE_VALUE
#else
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
typedef pid_t moo_vid_proc_t;   /* ffmpeg-PID, oder -1 wenn schon eingesammelt */
typedef int   moo_vid_pipe_t;   /* Schreib-Ende der Pipe zu ffmpeg-stdin */
#define MOO_VID_PIPE_BAD(fd) ((fd) < 0)
#define MOO_VID_PIPE_NONE    (-1)
#endif

/* Privater Writer-Zustand. */
struct MooVideoWriter {
    moo_vid_proc_t child;
    moo_vid_pipe_t wpipe_fd;
    int     width;       /* beim Open fixiert */
    int     height;
    int     fps;
    size_t  frame_count; /* erfolgreich gepipte Frames */
    int     err;         /* gespeicherter Fehler (MooVideoStatus), 0 = ok */
};

#ifdef _WIN32

/* Schreibt buf vollstaendig (Schleife gegen partielle writes). Liefert true
 * bei Erfolg, false bei Fehler (inkl. ERROR_BROKEN_PIPE bei totem ffmpeg). */
static bool write_all(moo_vid_pipe_t h, const uint8_t* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t rest = len - off;
        DWORD chunk = (rest > 0x40000000u) ? 0x40000000u : (DWORD)rest;
        DWORD n = 0;
        if (!WriteFile(h, buf + off, chunk, &n, NULL)) return false;
        if (n == 0) return false;
        off += (size_t)n;
    }
    return true;
}

/* Haengt arg nach den Windows-CRT-Argv-Regeln gequotet an buf an
 * (umschliessende Quotes, innere Quotes als \", Backslash-Folgen vor
 * Quote/Argument-Ende verdoppelt). Liefert false bei Puffer-Ueberlauf. */
static bool win_append_quoted_arg(char* buf, size_t cap, size_t* len, const char* arg) {
    size_t i = *len;
#define MOO_VID_PUT(c) do { if (i + 1 >= cap) return false; buf[i++] = (c); } while (0)
    if (i > 0) MOO_VID_PUT(' ');
    MOO_VID_PUT('"');
    size_t bs = 0;
    for (const char* p = arg; *p; p++) {
        if (*p == '\\') { bs++; continue; }
        if (*p == '"') {
            for (size_t k = 0; k < 2 * bs + 1; k++) MOO_VID_PUT('\\');
            bs = 0;
            MOO_VID_PUT('"');
            continue;
        }
        for (size_t k = 0; k < bs; k++) MOO_VID_PUT('\\');
        bs = 0;
        MOO_VID_PUT(*p);
    }
    for (size_t k = 0; k < 2 * bs; k++) MOO_VID_PUT('\\');
    MOO_VID_PUT('"');
#undef MOO_VID_PUT
    buf[i] = '\0';
    *len = i;
    return true;
}

/* Startet ffmpeg ohne Shell: Pipe-Read-Ende als hStdInput, Write-Ende beim
 * Parent (nicht vererbt). Liefert Prozess- und Pipe-Handle. */
static bool win_spawn_ffmpeg(const char* path, const char* s_arg, const char* r_arg,
                             moo_vid_proc_t* out_child, moo_vid_pipe_t* out_wpipe) {
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    /* Write-Ende NICHT vererben (EOF-Invariante, siehe Kopf-Kommentar). */
    SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);

    char cmd[4096];
    size_t len = 0;
    cmd[0] = '\0';
    const char* fixed[] = {
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
        NULL
    };
    for (int k = 0; fixed[k]; k++) {
        if (!win_append_quoted_arg(cmd, sizeof(cmd), &len, fixed[k])) goto fail;
    }
    if (!win_append_quoted_arg(cmd, sizeof(cmd), &len, path)) goto fail;

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = rd;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        goto fail;
    }
    CloseHandle(pi.hThread);
    CloseHandle(rd); /* Read-Ende gehoert dem Child. */
    *out_child = pi.hProcess;
    *out_wpipe = wr;
    return true;

fail:
    CloseHandle(rd);
    CloseHandle(wr);
    return false;
}

#else /* POSIX */

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

#endif /* _WIN32 */

MooVideoWriter* moo_video_open(const char* path, int w, int h, int fps) {
    if (!path || w <= 0 || h <= 0 || fps <= 0) return NULL;
    if (w > 65535 || h > 65535) return NULL;        /* Bound fuer size_t-Mul */
    if ((w & 1) != 0 || (h & 1) != 0) return NULL;  /* yuv420p braucht gerade */

    /* argv fuer ffmpeg vorbereiten. Zahlen lokal in stack-Puffer formatieren
     * (KEIN Shell-String, das sind reine argv-Elemente). */
    char s_arg[64];
    char r_arg[32];
    snprintf(s_arg, sizeof(s_arg), "%dx%d", w, h);
    snprintf(r_arg, sizeof(r_arg), "%d", fps);

    moo_vid_proc_t child;
    moo_vid_pipe_t wpipe;

#ifdef _WIN32
    if (!win_spawn_ffmpeg(path, s_arg, r_arg, &child, &wpipe)) return NULL;
#else
    /* SIGPIPE ignorieren, damit ein toter ffmpeg uns nicht per Signal killt.
     * Prozessweit, idempotent (mehrfach SIG_IGN setzen ist harmlos). */
    signal(SIGPIPE, SIG_IGN);

    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;

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
    child = pid;
    wpipe = pipefd[1];
#endif

    MooVideoWriter* v = (MooVideoWriter*)malloc(sizeof(MooVideoWriter));
    if (!v) {
        /* Handle-Alloc fehlgeschlagen: Pipe schliessen + Child einsammeln. */
#ifdef _WIN32
        CloseHandle(wpipe);
        WaitForSingleObject(child, INFINITE);
        CloseHandle(child);
#else
        close(wpipe);
        int st;
        waitpid(child, &st, 0);
#endif
        return NULL;
    }
    memset(v, 0, sizeof(MooVideoWriter)); /* moo_alloc/malloc nullt nicht */
    v->child       = child;
    v->wpipe_fd    = wpipe;
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
    if (MOO_VID_PIPE_BAD(v->wpipe_fd)) {
        v->err = MOO_VIDEO_ERR_IO;
        return MOO_VIDEO_ERR_IO;
    }
    /* Bytes pro Frame: w,h in [1,65535] (Open) -> passt sicher in size_t. */
    size_t nbytes = (size_t)w * (size_t)h * 4u;
    if (!write_all(v->wpipe_fd, rgba, nbytes)) {
        /* ffmpeg vorzeitig tot (EPIPE/broken pipe) o.ae. -> Writer verderben. */
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
    if (!MOO_VID_PIPE_BAD(v->wpipe_fd)) {
#ifdef _WIN32
        CloseHandle(v->wpipe_fd);
#else
        close(v->wpipe_fd);
#endif
        v->wpipe_fd = MOO_VID_PIPE_NONE;
    }

    /* Kindprozess einsammeln (kein Zombie). */
#ifdef _WIN32
    if (v->child) {
        WaitForSingleObject(v->child, INFINITE);
        DWORD code = 1;
        BOOL got = GetExitCodeProcess(v->child, &code);
        CloseHandle(v->child);
        v->child = NULL;
        /* nonzero ffmpeg-Exit sichtbar machen (ueberschreibt OK, nicht IO). */
        if ((!got || code != 0) && result == MOO_VIDEO_OK) {
            result = MOO_VIDEO_ERR_FFMPEG;
        }
    }
#else
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
#endif

    free(v);
    return result;
}

size_t moo_video_frame_count(const MooVideoWriter* v) {
    return v ? v->frame_count : 0;
}
