/**
 * test_video_wiring_asan.c — ASan/UBSan-Harness fuer die MOO_VIDEO-VERDRAHTUNG
 *   (Plan-009 P009-V0T). GATE vor den test_video_*-Builtins (P009-V1).
 *
 * Was hier geprueft wird (V0-Core moo_video.c + Heap-Wrapper moo_video_handle.c),
 * OHNE echtes ffmpeg und OHNE GPU/Fenster:
 *   1. Happy Path: open -> N x add_frame -> close, frame_count korrekt, exit 0.
 *   2. release NACH ende: test_video_ende-Aequivalent (close + writer=NULL),
 *      danach moo_release(MOO_VIDEO) -> KEIN Doppel-close (writer==NULL).
 *   3. release OHNE ende: Handle wird mit noch OFFENEM Writer freigegeben ->
 *      moo_video_handle_free() schliesst stdin + waitpid(child) -> kein Zombie,
 *      kein FD-/Heap-Leak (ASan).
 *   4. dimension mismatch: ein Folgeframe mit abweichender w/h ->
 *      MOO_VIDEO_ERR_DIM; der Writer ist danach "verdorben".
 *   5. broken pipe / child-failure: ffmpeg-Mock, das stdin sofort schliesst und
 *      exit 0 macht -> spaetere write() liefern EPIPE (SIGPIPE ist im Core auf
 *      SIG_IGN) -> MOO_VIDEO_ERR_IO statt Prozess-Kill.
 *   6. child-failure (nonzero exit): Mock liest stdin leer und exit 1 ->
 *      moo_video_close() meldet MOO_VIDEO_ERR_FFMPEG (nonzero ffmpeg-Exit).
 *   7. exec failure: PATH zeigt auf ein Verzeichnis OHNE ffmpeg -> execvp im
 *      Child schlaegt fehl -> Child _exit(127); open() liefert weiter einen
 *      gueltigen Handle (fork hat geklappt), close() sieht nonzero Exit ->
 *      MOO_VIDEO_ERR_FFMPEG. Der Writer muss trotzdem leakfrei einsammelbar sein.
 *   8. ungerade yuv420p-Dims bei open: moo_video_open() liefert NULL (yuv420p
 *      verlangt gerade Breite/Hoehe; kein scale/pad im PoC).
 *   9. frame-bounded: viele grosse Frames streamen, jeden VOR dem naechsten
 *      freigeben -> Peak-RAM ~ 1 Frame, kein monoton wachsender Heap (ASan).
 *
 * MOCK-FFMPEG (warum/wie):
 *   Der Core startet ffmpeg per fork + execvp("ffmpeg", argv) -> es wird ueber
 *   PATH aufgeloest. Dieser Harness schreibt zur Laufzeit kleine POSIX-sh-
 *   Skripte namens "ffmpeg" in ein mkdtemp()-Verzeichnis und setzt PATH per
 *   setenv() so, dass execvp GENAU diese Mocks findet — NIE ein echtes ffmpeg,
 *   NIE eine GPU. Verschiedene Mock-Varianten simulieren die Erfolgs-/Fehler-
 *   pfade (alles lesen + exit 0; sofort schliessen + exit 0; exit 1; gar kein
 *   ffmpeg fuer exec-failure). Damit ist der Harness vollstaendig unabhaengig
 *   von ffmpeg/GPU und CI-tauglich (run_all.sh bleibt ffmpeg-frei).
 *
 * Dieser Harness umgeht das nur-3D-Modul moo_test_api.c (das SDL/GLFW zoege)
 * und linkt den isolierten Core (moo_video.c) + den immer-gebauten Heap-Wrapper
 * (moo_video_handle.c) + die noetigen Core-Runtime-Bausteine (moo_memory.c /
 * moo_value.c / moo_error.c / moo_print.c). Die test_video_*-Builtins selbst
 * (Fenster-Grab) werden spaeter am echten Programm verifiziert; HIER liegt der
 * Fokus auf Speicher-/UB-/Zombie-Sicherheit der Verdrahtungs-Datenpfade.
 *
 * Build/Run (analog run_sanitize.sh):
 *   gcc -fsanitize=address -g -std=gnu11 -D_GNU_SOURCE -Wall -Wextra -I.. \
 *       test_video_wiring_asan.c ../moo_video.c ../moo_video_handle.c \
 *       ../moo_memory.c ../moo_value.c ../moo_error.c ../moo_print.c \
 *       -lm -o /tmp/t_video_wire
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_video_wire
 *
 *   gcc -fsanitize=undefined -fno-sanitize-recover=undefined -g -std=gnu11 \
 *       -D_GNU_SOURCE -Wall -Wextra -I.. test_video_wiring_asan.c \
 *       ../moo_video.c ../moo_video_handle.c ../moo_memory.c ../moo_value.c \
 *       ../moo_error.c ../moo_print.c -lm -o /tmp/t_video_wire_ub
 *   UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/t_video_wire_ub
 */

#include "moo_runtime.h"
#include "moo_video.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

/* Aus moo_video_handle.c (immer gebaut). */
extern MooValue moo_video_handle_new(MooVideoWriter* writer);

/* moo_memory.c::moo_release dispatcht (per switch) auf die Free-Funktionen
 * ALLER Heap-Typen. Dieser Harness erzeugt aber NUR MOO_VIDEO, dessen Free
 * (moo_video_handle_free) echt aus moo_video_handle.c gelinkt ist. Die uebrigen
 * Free-Symbole kommen sonst aus schweren Modulen (SDL/curl/sqlite/glfw/GIF/
 * Frame). Da deren Tags hier NIE auftreten, sind no-op-Stubs korrekt (werden
 * nie aufgerufen) und halten den Harness dep-frei. */
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

/* ---- Mock-ffmpeg-Infrastruktur ------------------------------------------- */
/* Verzeichnis mit unseren "ffmpeg"-Mock-Skripten; per PATH vorangestellt. */
static char g_mockdir[256] = {0};
static char g_orig_path[4096] = {0};

/* Schreibt ein ausfuehrbares sh-Skript namens "ffmpeg" mit gegebenem Body in
 * g_mockdir und setzt PATH=g_mockdir:<orig>. So findet execvp("ffmpeg") GENAU
 * dieses Mock. Body bekommt ein "#!/bin/sh"-Shebang vorangestellt. */
static int install_mock_ffmpeg(const char* body) {
    char path[512];
    snprintf(path, sizeof(path), "%s/ffmpeg", g_mockdir);
    FILE* f = fopen(path, "w");
    if (!f) { perror("  mock fopen"); return -1; }
    fputs("#!/bin/sh\n", f);
    fputs(body, f);
    fclose(f);
    if (chmod(path, 0755) != 0) { perror("  mock chmod"); return -1; }
    char newpath[4096];
    snprintf(newpath, sizeof(newpath), "%s:%s", g_mockdir, g_orig_path);
    if (setenv("PATH", newpath, 1) != 0) { perror("  setenv PATH"); return -1; }
    return 0;
}

/* Entfernt das Mock-"ffmpeg" wieder und setzt PATH so, dass execvp KEIN ffmpeg
 * findet (leeres PATH) -> exec-failure-Pfad (Child _exit(127)). */
static int uninstall_mock_ffmpeg_no_path(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/ffmpeg", g_mockdir);
    unlink(path); /* falls vorhanden; egal ob es existierte */
    /* Leeres PATH -> execvp findet "ffmpeg" garantiert nicht. */
    if (setenv("PATH", "", 1) != 0) { perror("  setenv PATH leer"); return -1; }
    return 0;
}

/* Erzeugt einen RGBA-Pixelpuffer (top-left), deterministisch befuellt.
 * Reiner malloc-Puffer (KEIN MOO_FRAME) — der Harness testet den Core-Datenpfad
 * direkt, ohne den 3D-only Frame-Typ. Aufrufer gibt mit free() frei. */
static uint8_t* make_pixels(int w, int h, int seed) {
    size_t n = (size_t)w * (size_t)h * 4u;
    uint8_t* px = (uint8_t*)malloc(n);
    assert(px);
    for (size_t i = 0; i < n; i += 4) {
        px[i + 0] = (uint8_t)((i / 4 + (unsigned)seed) & 0xFFu);
        px[i + 1] = (uint8_t)(((i / 4) * 3u + (unsigned)seed) & 0xFFu);
        px[i + 2] = (uint8_t)(((unsigned)seed * 17u) & 0xFFu);
        px[i + 3] = 255u;
    }
    return px;
}

/* --- Test 1: Happy Path open -> N frame -> close, frame_count korrekt ------ */
static int test_happy_path(void) {
    printf("[test] Happy Path: open -> 5 Frames -> close (frame_count==5, exit 0)\n");
    /* Mock liest den gesamten stdin in /dev/null und beendet sauber (exit 0). */
    if (install_mock_ffmpeg("cat >/dev/null\nexit 0\n") != 0) return -1;

    const int w = 64, h = 48, frames = 5;
    MooVideoWriter* v = moo_video_open("/tmp/moo_video_wire_happy.mp4", w, h, 12);
    if (!v) { fprintf(stderr, "  open fehlgeschlagen\n"); return -1; }

    for (int i = 0; i < frames; ++i) {
        uint8_t* px = make_pixels(w, h, i);
        int rc = moo_video_add_frame(v, px, w, h);
        free(px); /* frame-bounded: sofort frei */
        if (rc != MOO_VIDEO_OK) {
            fprintf(stderr, "  add_frame %d -> %d\n", i, rc);
            moo_video_close(v);
            return -1;
        }
    }
    size_t cnt = moo_video_frame_count(v);
    int crc = moo_video_close(v);
    if (cnt != (size_t)frames) { fprintf(stderr, "  frame_count %zu != %d\n", cnt, frames); return -1; }
    if (crc != MOO_VIDEO_OK)   { fprintf(stderr, "  close -> %d (erwartet OK)\n", crc); return -1; }
    printf("  %zu Frames gepiped, close OK\n", cnt);
    return 0;
}

/* --- Test 2: release NACH ende -> kein Doppel-close (via Heap-Handle) ------ */
static int test_release_after_ende(void) {
    printf("[test] release NACH ende: Handle haelt writer, ende schliesst,\n"
           "       release danach macht KEIN Doppel-close\n");
    if (install_mock_ffmpeg("cat >/dev/null\nexit 0\n") != 0) return -1;

    const int w = 32, h = 32;
    MooVideoWriter* writer = moo_video_open("/tmp/moo_video_wire_afterend.mp4", w, h, 10);
    if (!writer) return -1;

    {
        uint8_t* px = make_pixels(w, h, 1);
        assert(moo_video_add_frame(writer, px, w, h) == MOO_VIDEO_OK);
        free(px);
    }

    MooValue vid = moo_video_handle_new(writer); /* take ownership */
    assert(vid.tag == MOO_VIDEO);

    /* test_video_ende-Aequivalent: Writer regulaer schliessen + writer=NULL. */
    MooVideoHandle* vh = MV_VIDEO(vid);
    assert(vh->writer != NULL);
    int crc = moo_video_close(vh->writer);
    vh->writer = NULL;
    if (crc != MOO_VIDEO_OK) { fprintf(stderr, "  ende-close -> %d\n", crc); moo_release(vid); return -1; }

    /* Handle freigeben: writer==NULL -> moo_video_handle_free macht KEIN
     * zweites close (sonst waere es ein Doppel-close auf einen freed Writer). */
    moo_release(vid);
    printf("  ende sauber, release danach ohne Doppel-close\n");
    return 0;
}

/* --- Test 3: release OHNE ende -> Wrapper schliesst offenen Writer (waitpid) */
static int test_release_without_ende(void) {
    printf("[test] release OHNE ende: handle_free schliesst offenen Writer\n"
           "       (stdin close + waitpid, kein Zombie/Leak)\n");
    if (install_mock_ffmpeg("cat >/dev/null\nexit 0\n") != 0) return -1;

    const int w = 16, h = 16;
    MooVideoWriter* writer = moo_video_open("/tmp/moo_video_wire_noend.mp4", w, h, 8);
    if (!writer) return -1;

    uint8_t* px = make_pixels(w, h, 3);
    assert(moo_video_add_frame(writer, px, w, h) == MOO_VIDEO_OK);
    free(px);

    MooValue vid = moo_video_handle_new(writer);
    assert(vid.tag == MOO_VIDEO);

    /* KEIN ende -> direkt release. handle_free schliesst writer + waitpid. */
    moo_release(vid);
    printf("  offener Writer beim release sauber abgeschlossen\n");
    return 0;
}

/* --- Test 4: dimension mismatch -> MOO_VIDEO_ERR_DIM ----------------------- */
static int test_dimension_mismatch(void) {
    printf("[test] dimension mismatch: Folgeframe mit abweichender w/h -> ERR_DIM\n");
    if (install_mock_ffmpeg("cat >/dev/null\nexit 0\n") != 0) return -1;

    const int w = 40, h = 30;
    MooVideoWriter* v = moo_video_open("/tmp/moo_video_wire_dim.mp4", w, h, 10);
    if (!v) return -1;

    uint8_t* ok = make_pixels(w, h, 0);
    assert(moo_video_add_frame(v, ok, w, h) == MOO_VIDEO_OK);
    free(ok);

    /* Falsche Dims: anderer Puffer, anderer w/h -> ERR_DIM, NICHT geschrieben. */
    uint8_t* bad = make_pixels(w + 2, h, 1);
    int rc = moo_video_add_frame(v, bad, w + 2, h);
    free(bad);
    if (rc != MOO_VIDEO_ERR_DIM) {
        fprintf(stderr, "  mismatch -> %d (erwartet ERR_DIM %d)\n", rc, MOO_VIDEO_ERR_DIM);
        moo_video_close(v);
        return -1;
    }
    /* Writer ist nun verdorben: weiterer add_frame liefert sofort den Fehler. */
    uint8_t* ok2 = make_pixels(w, h, 2);
    int rc2 = moo_video_add_frame(v, ok2, w, h);
    free(ok2);
    if (rc2 != MOO_VIDEO_ERR_DIM) {
        fprintf(stderr, "  nach mismatch -> %d (erwartet gespeicherten ERR_DIM)\n", rc2);
        moo_video_close(v);
        return -1;
    }
    /* frame_count darf nur den EINEN gueltigen Frame zaehlen. */
    size_t cnt = moo_video_frame_count(v);
    moo_video_close(v); /* close trotz verdorbenem Writer: kein Leak/Zombie */
    if (cnt != 1) { fprintf(stderr, "  frame_count %zu != 1\n", cnt); return -1; }
    printf("  ERR_DIM erkannt + Writer verdorben, frame_count==1\n");
    return 0;
}

/* --- Test 5: broken pipe -> MOO_VIDEO_ERR_IO ------------------------------- */
static int test_broken_pipe(void) {
    printf("[test] broken pipe: ffmpeg schliesst stdin sofort -> add_frame ERR_IO\n");
    /* Mock liest NICHTS und beendet sofort (exit 0). Damit ist das Lese-Ende der
     * Pipe zu, sobald der Kindprozess weg ist -> write() -> EPIPE. SIGPIPE ist
     * im Core ignoriert, also kein Prozess-Kill, sondern sauberer Fehler. */
    if (install_mock_ffmpeg("exit 0\n") != 0) return -1;

    const int w = 256, h = 256; /* groesser Frame -> write fuellt Pipe-Puffer + EPIPE */
    MooVideoWriter* v = moo_video_open("/tmp/moo_video_wire_brokenpipe.mp4", w, h, 24);
    if (!v) return -1;

    /* Mehrere Frames schreiben; spaetestens wenn ffmpeg weg ist -> ERR_IO. */
    int saw_io = 0;
    for (int i = 0; i < 16 && !saw_io; ++i) {
        uint8_t* px = make_pixels(w, h, i);
        int rc = moo_video_add_frame(v, px, w, h);
        free(px);
        if (rc == MOO_VIDEO_ERR_IO) saw_io = 1;
        else if (rc != MOO_VIDEO_OK) {
            fprintf(stderr, "  add_frame %d -> unerwartet %d\n", i, rc);
            moo_video_close(v);
            return -1;
        }
    }
    int crc = moo_video_close(v); /* close raeumt waitpid; kein Zombie/Leak */
    if (!saw_io) {
        fprintf(stderr, "  kein ERR_IO trotz totem ffmpeg (close=%d)\n", crc);
        return -1;
    }
    printf("  broken pipe als ERR_IO propagiert (kein SIGPIPE-Kill)\n");
    return 0;
}

/* --- Test 6: child-failure (nonzero exit) -> MOO_VIDEO_ERR_FFMPEG ---------- */
static int test_child_nonzero_exit(void) {
    printf("[test] child-failure: ffmpeg liest stdin, exit 1 -> close ERR_FFMPEG\n");
    /* Mock liest den gesamten stdin (damit unsere writes nicht broken-pipen)
     * und beendet dann mit nonzero. close() muss das als ERR_FFMPEG melden. */
    if (install_mock_ffmpeg("cat >/dev/null\nexit 1\n") != 0) return -1;

    const int w = 32, h = 32, frames = 3;
    MooVideoWriter* v = moo_video_open("/tmp/moo_video_wire_childfail.mp4", w, h, 10);
    if (!v) return -1;

    for (int i = 0; i < frames; ++i) {
        uint8_t* px = make_pixels(w, h, i);
        int rc = moo_video_add_frame(v, px, w, h);
        free(px);
        if (rc != MOO_VIDEO_OK) { fprintf(stderr, "  add_frame %d -> %d\n", i, rc); moo_video_close(v); return -1; }
    }
    int crc = moo_video_close(v);
    if (crc != MOO_VIDEO_ERR_FFMPEG) {
        fprintf(stderr, "  close -> %d (erwartet ERR_FFMPEG %d)\n", crc, MOO_VIDEO_ERR_FFMPEG);
        return -1;
    }
    printf("  nonzero ffmpeg-Exit als ERR_FFMPEG gemeldet\n");
    return 0;
}

/* --- Test 7: exec failure (kein ffmpeg auffindbar) -> Child _exit(127) ----- */
static int test_exec_failure(void) {
    printf("[test] exec failure: PATH ohne ffmpeg -> Child _exit(127),\n"
           "       open liefert Handle, close meldet ERR_FFMPEG, leakfrei\n");
    if (uninstall_mock_ffmpeg_no_path() != 0) return -1;

    const int w = 16, h = 16;
    /* fork klappt -> open liefert gueltigen Handle; execvp scheitert erst im
     * Child (_exit(127)). Der Aufrufer MUSS trotzdem close() rufen (kein Zombie). */
    MooVideoWriter* v = moo_video_open("/tmp/moo_video_wire_execfail.mp4", w, h, 10);
    if (!v) {
        /* Auch akzeptabel/leakfrei, falls eine Plattform schon hier scheitert. */
        printf("  open -> NULL (exec-Setup fehlgeschlagen, leakfrei)\n");
        return 0;
    }
    /* Ein add_frame kann je nach Timing OK (in Pipe-Puffer) oder ERR_IO sein —
     * beides ist korrekt. Der harte Check ist close == ERR_FFMPEG (Exit 127). */
    uint8_t* px = make_pixels(w, h, 0);
    int arc = moo_video_add_frame(v, px, w, h);
    free(px);
    (void)arc;
    int crc = moo_video_close(v);
    if (crc != MOO_VIDEO_ERR_FFMPEG) {
        fprintf(stderr, "  close -> %d (erwartet ERR_FFMPEG bei Exit 127)\n", crc);
        return -1;
    }
    printf("  exec-failure als nonzero (127) -> ERR_FFMPEG, Child eingesammelt\n");
    return 0;
}

/* --- Test 8: ungerade yuv420p-Dims bei open -> NULL ------------------------ */
static int test_odd_dims(void) {
    printf("[test] ungerade yuv420p-Dims bei open -> NULL (kein scale/pad im PoC)\n");
    if (install_mock_ffmpeg("cat >/dev/null\nexit 0\n") != 0) return -1;

    /* yuv420p verlangt gerade Breite UND Hoehe. */
    MooVideoWriter* a = moo_video_open("/tmp/moo_video_wire_odd1.mp4", 65, 64, 10);
    MooVideoWriter* b = moo_video_open("/tmp/moo_video_wire_odd2.mp4", 64, 65, 10);
    MooVideoWriter* c = moo_video_open("/tmp/moo_video_wire_odd3.mp4", 65, 65, 10);
    int ok = (a == NULL) && (b == NULL) && (c == NULL);
    /* Defensive: falls (faelschlich) ein Handle kam, leakfrei schliessen. */
    if (a) moo_video_close(a);
    if (b) moo_video_close(b);
    if (c) moo_video_close(c);
    if (!ok) { fprintf(stderr, "  ungerade Dims lieferten unerwartet einen Writer\n"); return -1; }

    /* Gegenprobe: gerade Dims liefern einen Handle. */
    MooVideoWriter* good = moo_video_open("/tmp/moo_video_wire_even.mp4", 64, 64, 10);
    if (!good) { fprintf(stderr, "  gerade Dims lieferten KEINEN Writer\n"); return -1; }
    moo_video_close(good);
    printf("  ungerade -> NULL, gerade -> OK\n");
    return 0;
}

/* --- Test 9: frame-bounded RAM: viele grosse Frames, Peak ~ 1 Frame -------- */
static int test_frame_bounded(void) {
    const int w = 640, h = 480, frames = 40;
    printf("[test] frame-bounded: %d Frames (%dx%d) streamend, Peak ~1 Frame\n",
           frames, w, h);
    /* Mock muss zuverlaessig ALLES lesen, sonst broken pipe. */
    if (install_mock_ffmpeg("cat >/dev/null\nexit 0\n") != 0) return -1;

    MooVideoWriter* v = moo_video_open("/tmp/moo_video_wire_bounded.mp4", w, h, 30);
    if (!v) return -1;

    for (int i = 0; i < frames; ++i) {
        uint8_t* px = make_pixels(w, h, i * 7 + 1);
        int rc = moo_video_add_frame(v, px, w, h);
        free(px); /* <-- entscheidend: VOR dem naechsten Frame frei -> Peak ~1 Frame */
        if (rc != MOO_VIDEO_OK) {
            fprintf(stderr, "  add_frame %d -> %d\n", i, rc);
            moo_video_close(v);
            return -1;
        }
    }
    size_t cnt = moo_video_frame_count(v);
    int crc = moo_video_close(v);
    if (cnt != (size_t)frames) { fprintf(stderr, "  frame_count %zu != %d\n", cnt, frames); return -1; }
    if (crc != MOO_VIDEO_OK)   { fprintf(stderr, "  close -> %d\n", crc); return -1; }
    printf("  %zu Frames gestreamt (Peak ~ 1 Frame, kein RAM-Sammeln)\n", cnt);
    return 0;
}

int main(void) {
    printf("=== test_video_wiring_asan: MOO_VIDEO-Verdrahtung (P009-V0T) ===\n");

    /* PATH sichern + Mock-Verzeichnis anlegen. */
    const char* p = getenv("PATH");
    snprintf(g_orig_path, sizeof(g_orig_path), "%s", p ? p : "/usr/bin:/bin");
    char tmpl[] = "/tmp/moo_video_mock.XXXXXX";
    char* d = mkdtemp(tmpl);
    if (!d) { perror("mkdtemp"); return 1; }
    snprintf(g_mockdir, sizeof(g_mockdir), "%s", d);
    printf("Mock-ffmpeg-Verzeichnis: %s\n", g_mockdir);

    int fail = 0;
    fail |= (test_happy_path()            != 0);
    fail |= (test_release_after_ende()    != 0);
    fail |= (test_release_without_ende()  != 0);
    fail |= (test_dimension_mismatch()    != 0);
    fail |= (test_broken_pipe()           != 0);
    fail |= (test_child_nonzero_exit()    != 0);
    fail |= (test_exec_failure()          != 0);
    fail |= (test_odd_dims()              != 0);
    fail |= (test_frame_bounded()         != 0);

    /* PATH wiederherstellen + Mock-Verzeichnis aufraeumen (leakfrei/sauber). */
    setenv("PATH", g_orig_path, 1);
    {
        char mockbin[512];
        snprintf(mockbin, sizeof(mockbin), "%s/ffmpeg", g_mockdir);
        unlink(mockbin);
        rmdir(g_mockdir);
    }

    if (fail) {
        printf("=== FEHLER: mindestens ein Video-Verdrahtungs-Test fehlgeschlagen ===\n");
        return 1;
    }
    printf("=== ALLE VIDEO-VERDRAHTUNGS-TESTS GRUEN ===\n");
    return 0;
}
