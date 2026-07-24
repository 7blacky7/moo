/*
 * moo_ui_wayland.c — Natives Wayland-Fenster-Backend (NATIVE-UI-1).
 * ====================================================================
 * OS-Display-Interface DIREKT: libwayland-client + xdg-shell/xdg-decoration
 * (Glue eingecheckt unter runtime/wayland/, via wayland-scanner generiert).
 * KEIN Toolkit. Der Glass-/CSD-Layer (stdlib/ui_moo_csd.moos) zeichnet das
 * komplette Fenster selbst; dieses Backend liefert nur: Surface, Present,
 * Input, WM-Operationen.
 *
 * V1-EINSCHRAENKUNGEN (dokumentiert, Task 998e06d1):
 *   - EIN Fenster pro Prozess (CSD-Beweis-Demo; Multi-Window spaeter)
 *   - buffer_scale = 1 (HiDPI bewusst ausgeklammert)
 *   - Clipboard/IME/A11y: NATIVE-UI-9 (nicht still gefaked)
 *   - frame-Callback wird registriert (Throttle-Signal), Present ist
 *     event-getrieben — kein Busy-Redraw-Loop im Client.
 */

#define _GNU_SOURCE
#include "moo_ui_host.h"

#include <wayland-client.h>
#include "wayland/xdg-shell-client-protocol.h"
#include "wayland/xdg-decoration-client-protocol.h"
#include <xkbcommon/xkbcommon.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define WL_BTN_LEFT   272 /* linux/input-event-codes.h, hier fixiert */
#define WL_BTN_RIGHT  273
#define WL_BTN_MIDDLE 274

/* ------------------------------------------------------------------ *
 * Zustand
 * ------------------------------------------------------------------ */

typedef struct WlFenster {
    struct wl_surface* surface;
    struct xdg_surface* xsurface;
    struct xdg_toplevel* toplevel;
    struct zxdg_toplevel_decoration_v1* deco;
    int breite, hoehe;
    int konfiguriert;
    int maximiert;
    /* NATIVE-UI-1c: WM-Groesse aus toplevel_configure wird erst NACH dem
     * ack_configure uebernommen (xdg-Muster: ack, dann Commit mit neuem
     * Buffer) — sonst wertet der Compositor den frischen Frame gegen den
     * alten Zustand und zeigt beim Unmaximize-Drag kurz halben Inhalt. */
    int pending_b, pending_h;
    int soll_schliessen;
    /* SHM-Doublebuffer */
    int shm_fd;
    void* shm_daten;
    size_t pool_bytes;
    struct wl_shm_pool* pool;
    struct wl_buffer* buffer[2];
    int buffer_frei[2];
    /* Input-Callback (Moo-Funktion, retained) */
    MooValue input_cb;
    int hat_cb;
    /* frame-Callback-Throttle-Signal */
    int frame_ausstehend;
} WlFenster;

static struct {
    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_compositor* compositor;
    struct wl_shm* shm;
    struct wl_seat* seat;
    struct wl_pointer* pointer;
    struct wl_keyboard* keyboard;
    struct xdg_wm_base* wm_base;
    struct zxdg_decoration_manager_v1* deco_mgr;
    struct xkb_context* xkb;
    struct xkb_keymap* keymap;
    struct xkb_state* xkb_state;
    uint32_t letztes_serial;   /* letztes Pointer-Button-Serial */
    uint32_t enter_serial;
    double px, py;             /* surface-lokale Pointer-Position */
    int laufend;
    WlFenster* fenster;        /* v1: ein Fenster */
    /* eigener Pixel-Cursor */
    struct wl_surface* cursor_surface;
    struct wl_buffer* cursor_buffer;
    int cursor_fd;
    void* cursor_daten;
} W;

static void wlog(const char* was) {
    const char* dbg = getenv("MOO_WAYLAND_DEBUG");
    if (dbg && dbg[0] == '1') fprintf(stderr, "[moo_wayland] %s\n", was);
}

/* ------------------------------------------------------------------ *
 * Moo-Hilfen (Konventionen wie moo_ui_gtk.c)
 * ------------------------------------------------------------------ */

static inline MooValue wrap_fenster(WlFenster* f) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, f);
    return v;
}
static inline WlFenster* unwrap_fenster(MooValue v) {
    if (v.tag != MOO_NUMBER) return NULL;
    return (WlFenster*)moo_val_as_ptr(v);
}
static inline const char* wstr_or(MooValue v, const char* fb) {
    return (v.tag == MOO_STRING) ? MV_STR(v)->chars : fb;
}
static inline int wnum_or(MooValue v, int fb) {
    return (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : fb;
}

static void event_an_moo(WlFenster* f, const char* art,
                         double a, double b, double c) {
    if (!f || !f->hat_cb || f->input_cb.tag != MOO_FUNC) return;
    MooValue cb = f->input_cb;
    moo_retain(cb);
    MooValue rv = moo_func_call_4(cb, moo_string_new(art),
                                  moo_number(a), moo_number(b), moo_number(c));
    moo_release(rv);
    moo_release(cb);
}

/* ------------------------------------------------------------------ *
 * SHM-Puffer (Doublebuffer, release-getrieben)
 * ------------------------------------------------------------------ */

static void buffer_release(void* data, struct wl_buffer* b) {
    WlFenster* f = (WlFenster*)data;
    if (!f) return;
    for (int i = 0; i < 2; i++)
        if (f->buffer[i] == b) f->buffer_frei[i] = 1;
}
static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void puffer_zerstoeren(WlFenster* f) {
    for (int i = 0; i < 2; i++) {
        if (f->buffer[i]) { wl_buffer_destroy(f->buffer[i]); f->buffer[i] = NULL; }
        f->buffer_frei[i] = 1;
    }
    if (f->pool) { wl_shm_pool_destroy(f->pool); f->pool = NULL; }
    if (f->shm_daten && f->shm_daten != MAP_FAILED) {
        munmap(f->shm_daten, f->pool_bytes);
        f->shm_daten = NULL;
    }
}

static int puffer_anlegen(WlFenster* f, int b, int h) {
    if (b < 1 || h < 1) return 0;
    puffer_zerstoeren(f);
    int stride = b * 4;
    size_t einer = (size_t)stride * (size_t)h;
    f->pool_bytes = einer * 2;
    if (f->shm_fd < 0) {
        f->shm_fd = memfd_create("moo-wayland-shm", 0);
        if (f->shm_fd < 0) return 0;
    }
    if (ftruncate(f->shm_fd, (off_t)f->pool_bytes) != 0) return 0;
    f->shm_daten = mmap(NULL, f->pool_bytes, PROT_READ | PROT_WRITE,
                        MAP_SHARED, f->shm_fd, 0);
    if (f->shm_daten == MAP_FAILED) { f->shm_daten = NULL; return 0; }
    f->pool = wl_shm_create_pool(W.shm, f->shm_fd, (int32_t)f->pool_bytes);
    if (!f->pool) return 0;
    for (int i = 0; i < 2; i++) {
        f->buffer[i] = wl_shm_pool_create_buffer(
            f->pool, (int32_t)(einer * (size_t)i), b, h, stride,
            WL_SHM_FORMAT_ARGB8888);
        if (!f->buffer[i]) return 0;
        wl_buffer_add_listener(f->buffer[i], &buffer_listener, f);
        f->buffer_frei[i] = 1;
    }
    f->breite = b;
    f->hoehe = h;
    return 1;
}

/* ------------------------------------------------------------------ *
 * xdg-shell: configure / close
 * ------------------------------------------------------------------ */

static void xdg_surface_configure(void* data, struct xdg_surface* s,
                                  uint32_t serial) {
    WlFenster* f = (WlFenster*)data;
    xdg_surface_ack_configure(s, serial);
    if (f) {
        f->konfiguriert = 1;
        /* Erst nach dem ack: Puffer neu + Moo rendern/committen. */
        if (f->pending_b > 0 && f->pending_h > 0 &&
            (f->pending_b != f->breite || f->pending_h != f->hoehe)) {
            if (puffer_anlegen(f, f->pending_b, f->pending_h)) {
                event_an_moo(f, "resize", (double)f->pending_b,
                             (double)f->pending_h, 0.0);
            }
        }
        f->pending_b = 0;
        f->pending_h = 0;
    }
}
static const struct xdg_surface_listener xdg_surface_lst = {
    xdg_surface_configure
};

static void toplevel_configure(void* data, struct xdg_toplevel* t,
                               int32_t b, int32_t h, struct wl_array* states) {
    (void)t;
    WlFenster* f = (WlFenster*)data;
    if (!f) return;
    int max = 0;
    uint32_t* st;
    wl_array_for_each(st, states) {
        if (*st == XDG_TOPLEVEL_STATE_MAXIMIZED) max = 1;
    }
    f->maximiert = max;
    /* PFLICHT-Punkt 1 (Live-Reflow): WM-Groesse nur VORMERKEN — Puffer und
     * Re-Render laufen im xdg_surface_configure NACH dem ack (s. oben). */
    if (b > 0 && h > 0 && (b != f->breite || h != f->hoehe)) {
        f->pending_b = b;
        f->pending_h = h;
    }
}
static void toplevel_close(void* data, struct xdg_toplevel* t) {
    (void)t;
    WlFenster* f = (WlFenster*)data;
    if (!f) return;
    event_an_moo(f, "schliessen", 0, 0, 0);
    f->soll_schliessen = 1;
    W.laufend = 0;
}
static void toplevel_bounds(void* d, struct xdg_toplevel* t,
                            int32_t b, int32_t h) { (void)d; (void)t; (void)b; (void)h; }
static void toplevel_caps(void* d, struct xdg_toplevel* t,
                          struct wl_array* c) { (void)d; (void)t; (void)c; }
static const struct xdg_toplevel_listener toplevel_lst = {
    toplevel_configure, toplevel_close, toplevel_bounds, toplevel_caps
};

static void wm_base_ping(void* d, struct xdg_wm_base* wb, uint32_t serial) {
    (void)d;
    xdg_wm_base_pong(wb, serial);
}
static const struct xdg_wm_base_listener wm_base_lst = { wm_base_ping };

/* ------------------------------------------------------------------ *
 * Eigener Pixel-Cursor (Punkt 6): prozeduraler Pfeil, eigene SHM-Surface
 * ------------------------------------------------------------------ */

static void cursor_anlegen(void) {
    if (W.cursor_surface || !W.compositor || !W.shm) return;
    const int cb = 12, ch = 19;
    int stride = cb * 4;
    size_t bytes = (size_t)stride * ch;
    W.cursor_fd = memfd_create("moo-wayland-cursor", 0);
    if (W.cursor_fd < 0) return;
    if (ftruncate(W.cursor_fd, (off_t)bytes) != 0) return;
    W.cursor_daten = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
                          W.cursor_fd, 0);
    if (W.cursor_daten == MAP_FAILED) { W.cursor_daten = NULL; return; }
    /* Klassischer Pfeil: pro Zeile [start,ende) weiss, 1px schwarze Kante. */
    memset(W.cursor_daten, 0, bytes);
    for (int y = 0; y < ch; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)W.cursor_daten + (size_t)y * stride);
        int breite_zeile = y < 12 ? y + 1 : (y < 15 ? 8 - (y - 12) : 4);
        if (breite_zeile > cb) breite_zeile = cb;
        for (int x = 0; x < breite_zeile; x++) {
            int rand = (x == 0 || x == breite_zeile - 1 || y == 0 || y == ch - 1);
            row[x] = rand ? 0xFF000000u : 0xFFFFFFFFu;
        }
    }
    struct wl_shm_pool* cp = wl_shm_create_pool(W.shm, W.cursor_fd, (int32_t)bytes);
    if (!cp) return;
    W.cursor_buffer = wl_shm_pool_create_buffer(cp, 0, cb, ch, stride,
                                                WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(cp);
    if (!W.cursor_buffer) return;
    W.cursor_surface = wl_compositor_create_surface(W.compositor);
    if (!W.cursor_surface) return;
    wl_surface_attach(W.cursor_surface, W.cursor_buffer, 0, 0);
    wl_surface_commit(W.cursor_surface);
}

/* ------------------------------------------------------------------ *
 * Input: Pointer + Keyboard (Punkte 2 und 4)
 * ------------------------------------------------------------------ */

static void ptr_enter(void* d, struct wl_pointer* p, uint32_t serial,
                      struct wl_surface* s, wl_fixed_t sx, wl_fixed_t sy) {
    (void)d; (void)s;
    W.enter_serial = serial;
    W.px = wl_fixed_to_double(sx);
    W.py = wl_fixed_to_double(sy);
    cursor_anlegen();
    if (W.cursor_surface)
        wl_pointer_set_cursor(p, serial, W.cursor_surface, 0, 0);
}
static void ptr_leave(void* d, struct wl_pointer* p, uint32_t serial,
                      struct wl_surface* s) {
    (void)d; (void)p; (void)serial; (void)s;
}
static void ptr_motion(void* d, struct wl_pointer* p, uint32_t t,
                       wl_fixed_t sx, wl_fixed_t sy) {
    (void)d; (void)p; (void)t;
    W.px = wl_fixed_to_double(sx);
    W.py = wl_fixed_to_double(sy);
    event_an_moo(W.fenster, "bewegung", W.px, W.py, 0);
}
static void ptr_button(void* d, struct wl_pointer* p, uint32_t serial,
                       uint32_t t, uint32_t button, uint32_t state) {
    (void)d; (void)p; (void)t;
    /* PFLICHT-Punkt 2: Serial fuer xdg_toplevel_move/resize tracken. */
    W.letztes_serial = serial;
    double taste = button == WL_BTN_LEFT ? 1 : button == WL_BTN_RIGHT ? 3 :
                   button == WL_BTN_MIDDLE ? 2 : 0;
    event_an_moo(W.fenster,
                 state == WL_POINTER_BUTTON_STATE_PRESSED ? "maus_runter"
                                                          : "maus_hoch",
                 W.px, W.py, taste);
}
static void ptr_axis(void* d, struct wl_pointer* p, uint32_t t,
                     uint32_t axis, wl_fixed_t v) {
    (void)d; (void)p; (void)t; (void)axis; (void)v;
}
static void ptr_frame(void* d, struct wl_pointer* p) { (void)d; (void)p; }
static void ptr_axis_source(void* d, struct wl_pointer* p, uint32_t s) {
    (void)d; (void)p; (void)s;
}
static void ptr_axis_stop(void* d, struct wl_pointer* p, uint32_t t,
                          uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void ptr_axis_discrete(void* d, struct wl_pointer* p, uint32_t a,
                              int32_t disc) { (void)d; (void)p; (void)a; (void)disc; }
/* libwayland verlangt ALLE Slots der gebundenen Version non-NULL —
 * NULL-Listener = harter Client-Abbruch beim ersten Event (opcode 5 =
 * wl_pointer.frame, kommt nach jedem Pointer-Burst). */
static const struct wl_pointer_listener pointer_lst = {
    ptr_enter, ptr_leave, ptr_motion, ptr_button, ptr_axis,
    ptr_frame, ptr_axis_source, ptr_axis_stop, ptr_axis_discrete
};

static void kb_keymap(void* d, struct wl_keyboard* k, uint32_t format,
                      int32_t fd, uint32_t size) {
    (void)d; (void)k;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    char* map = (char*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { close(fd); return; }
    if (!W.xkb) W.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (W.keymap) xkb_keymap_unref(W.keymap);
    W.keymap = xkb_keymap_new_from_string(W.xkb, map,
                                          XKB_KEYMAP_FORMAT_TEXT_V1,
                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);
    if (W.xkb_state) xkb_state_unref(W.xkb_state);
    W.xkb_state = W.keymap ? xkb_state_new(W.keymap) : NULL;
}
static void kb_enter(void* d, struct wl_keyboard* k, uint32_t serial,
                     struct wl_surface* s, struct wl_array* keys) {
    (void)d; (void)k; (void)serial; (void)s; (void)keys;
}
static void kb_leave(void* d, struct wl_keyboard* k, uint32_t serial,
                     struct wl_surface* s) {
    (void)d; (void)k; (void)serial; (void)s;
}
static void kb_key(void* d, struct wl_keyboard* k, uint32_t serial,
                   uint32_t t, uint32_t key, uint32_t state) {
    (void)d; (void)k; (void)serial; (void)t;
    uint32_t keysym = 0;
    if (W.xkb_state)
        keysym = xkb_state_key_get_one_sym(W.xkb_state, key + 8);
    event_an_moo(W.fenster,
                 state == WL_KEYBOARD_KEY_STATE_PRESSED ? "taste_runter"
                                                        : "taste_hoch",
                 (double)keysym, 0, 0);
}
static void kb_mods(void* d, struct wl_keyboard* k, uint32_t serial,
                    uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) {
    (void)d; (void)k; (void)serial;
    if (W.xkb_state)
        xkb_state_update_mask(W.xkb_state, dep, lat, lock, 0, 0, grp);
}
static void kb_repeat(void* d, struct wl_keyboard* k, int32_t rate,
                      int32_t delay) { (void)d; (void)k; (void)rate; (void)delay; }
static const struct wl_keyboard_listener keyboard_lst = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_mods, kb_repeat
};

static void seat_caps(void* d, struct wl_seat* s, uint32_t caps) {
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !W.pointer) {
        W.pointer = wl_seat_get_pointer(s);
        wl_pointer_add_listener(W.pointer, &pointer_lst, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !W.keyboard) {
        W.keyboard = wl_seat_get_keyboard(s);
        wl_keyboard_add_listener(W.keyboard, &keyboard_lst, NULL);
    }
}
static void seat_name(void* d, struct wl_seat* s, const char* n) {
    (void)d; (void)s; (void)n;
}
static const struct wl_seat_listener seat_lst = { seat_caps, seat_name };

/* ------------------------------------------------------------------ *
 * Registry
 * ------------------------------------------------------------------ */

static void reg_global(void* d, struct wl_registry* r, uint32_t name,
                       const char* iface, uint32_t version) {
    (void)d; (void)version;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        W.compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        W.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, wl_seat_interface.name) == 0) {
        W.seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
        wl_seat_add_listener(W.seat, &seat_lst, NULL);
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        W.wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(W.wm_base, &wm_base_lst, NULL);
    } else if (strcmp(iface, zxdg_decoration_manager_v1_interface.name) == 0) {
        W.deco_mgr = wl_registry_bind(r, name,
                                      &zxdg_decoration_manager_v1_interface, 1);
    }
}
static void reg_remove(void* d, struct wl_registry* r, uint32_t name) {
    (void)d; (void)r; (void)name;
}
static const struct wl_registry_listener reg_lst = { reg_global, reg_remove };

static int ensure_wayland(void) {
    if (W.display) return 1;
    W.display = wl_display_connect(NULL);
    if (!W.display) {
        fprintf(stderr, "[moo_wayland] kein WAYLAND_DISPLAY erreichbar\n");
        return 0;
    }
    W.registry = wl_display_get_registry(W.display);
    wl_registry_add_listener(W.registry, &reg_lst, NULL);
    wl_display_roundtrip(W.display);
    if (!W.compositor || !W.shm || !W.wm_base) {
        fprintf(stderr, "[moo_wayland] Compositor/SHM/xdg_wm_base fehlt\n");
        return 0;
    }
    W.laufend = 1;
    wlog("verbunden");
    return 1;
}

/* ------------------------------------------------------------------ *
 * MooUiHostOps-Implementierung
 * ------------------------------------------------------------------ */

static MooValue wl_host_fenster(MooValue titel, MooValue breite, MooValue hoehe,
                                MooValue flags, MooValue parent) {
    (void)flags; (void)parent; /* rahmenlos ist der einzige Modus (CSD) */
    if (!ensure_wayland()) return moo_bool(0);
    if (W.fenster) return moo_bool(0); /* v1: ein Fenster */
    WlFenster* f = (WlFenster*)calloc(1, sizeof(WlFenster));
    if (!f) return moo_bool(0);
    f->shm_fd = -1;
    f->buffer_frei[0] = f->buffer_frei[1] = 1;
    f->surface = wl_compositor_create_surface(W.compositor);
    f->xsurface = xdg_wm_base_get_xdg_surface(W.wm_base, f->surface);
    xdg_surface_add_listener(f->xsurface, &xdg_surface_lst, f);
    f->toplevel = xdg_surface_get_toplevel(f->xsurface);
    xdg_toplevel_add_listener(f->toplevel, &toplevel_lst, f);
    xdg_toplevel_set_title(f->toplevel, wstr_or(titel, "moo"));
    xdg_toplevel_set_app_id(f->toplevel, "org.moosynapse.moo");
    /* PFLICHT-Punkt 3: client-side Dekoration explizit anfordern. */
    if (W.deco_mgr) {
        f->deco = zxdg_decoration_manager_v1_get_toplevel_decoration(
            W.deco_mgr, f->toplevel);
        zxdg_toplevel_decoration_v1_set_mode(
            f->deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    }
    int b = wnum_or(breite, 640);
    int h = wnum_or(hoehe, 480);
    if (!puffer_anlegen(f, b, h)) { free(f); return moo_bool(0); }
    wl_surface_commit(f->surface); /* xdg: erst configure abwarten */
    W.fenster = f;
    wlog("fenster angelegt");
    return wrap_fenster(f);
}

static MooValue wl_host_schliessen(MooValue fenster) {
    WlFenster* f = unwrap_fenster(fenster);
    if (!f || f != W.fenster) return moo_bool(0);
    if (f->hat_cb) { moo_release(f->input_cb); f->hat_cb = 0; }
    puffer_zerstoeren(f);
    if (f->shm_fd >= 0) close(f->shm_fd);
    if (f->deco) zxdg_toplevel_decoration_v1_destroy(f->deco);
    if (f->toplevel) xdg_toplevel_destroy(f->toplevel);
    if (f->xsurface) xdg_surface_destroy(f->xsurface);
    if (f->surface) wl_surface_destroy(f->surface);
    if (W.display) wl_display_flush(W.display);
    W.fenster = NULL;
    W.laufend = 0;
    free(f);
    return moo_bool(1);
}

static MooValue wl_host_zeige(MooValue fenster) {
    WlFenster* f = unwrap_fenster(fenster);
    if (!f || !W.display) return moo_bool(0);
    /* Auf erstes configure warten (xdg-Vertrag: kein attach davor). */
    int runden = 0;
    while (!f->konfiguriert && runden < 64) {
        if (wl_display_dispatch(W.display) == -1) return moo_bool(0);
        runden++;
    }
    return moo_bool(f->konfiguriert ? 1 : 0);
}

static void frame_done(void* data, struct wl_callback* cb, uint32_t t) {
    (void)t;
    WlFenster* f = (WlFenster*)data;
    if (f) f->frame_ausstehend = 0;
    wl_callback_destroy(cb);
}
static const struct wl_callback_listener frame_lst = { frame_done };

static MooValue wl_host_praesentiere(MooValue fenster, MooValue frame) {
    WlFenster* f = unwrap_fenster(fenster);
    if (!f || !f->konfiguriert || frame.tag != MOO_FRAME) return moo_bool(0);
    MooFrame* fr = MV_FRAME(frame);
    if (!fr || !fr->pixels) return moo_bool(0);
    int idx = f->buffer_frei[0] ? 0 : (f->buffer_frei[1] ? 1 : -1);
    if (idx < 0) {
        wl_display_roundtrip(W.display); /* auf release warten */
        idx = f->buffer_frei[0] ? 0 : (f->buffer_frei[1] ? 1 : 0);
    }
    int b = f->breite, h = f->hoehe;
    int kb = fr->width < b ? fr->width : b;
    int kh = fr->height < h ? fr->height : h;
    size_t stride = (size_t)b * 4;
    uint8_t* ziel = (uint8_t*)f->shm_daten + (size_t)idx * stride * (size_t)h;
    memset(ziel, 0, stride * (size_t)h);
    /* RGBA straight (top-left) -> premultipliziertes ARGB32 (LE),
     * identische Konvertierung wie der GTK-Present-Pfad. */
    for (int row = 0; row < kh; ++row) {
        const uint8_t* src = fr->pixels + (size_t)row * (size_t)fr->stride;
        uint32_t* dst = (uint32_t*)(ziel + (size_t)row * stride);
        for (int col = 0; col < kb; ++col) {
            uint32_t a = src[3];
            uint32_t r = (src[0] * a + 127u) / 255u;
            uint32_t g = (src[1] * a + 127u) / 255u;
            uint32_t bl = (src[2] * a + 127u) / 255u;
            dst[col] = (a << 24) | (r << 16) | (g << 8) | bl;
            src += 4;
        }
    }
    f->buffer_frei[idx] = 0;
    wl_surface_attach(f->surface, f->buffer[idx], 0, 0);
    wl_surface_damage_buffer(f->surface, 0, 0, b, h);
    if (!f->frame_ausstehend) {
        struct wl_callback* cb = wl_surface_frame(f->surface);
        wl_callback_add_listener(cb, &frame_lst, f);
        f->frame_ausstehend = 1;
    }
    wl_surface_commit(f->surface);
    wl_display_flush(W.display);
    return moo_bool(1);
}

static MooValue wl_host_zeichne_frame_stub(MooValue z, MooValue x, MooValue y,
                                           MooValue b, MooValue h, MooValue fr) {
    (void)z; (void)x; (void)y; (void)b; (void)h; (void)fr;
    return moo_bool(0); /* kein Zeichner-Modell; praesentiere() nutzen */
}

static MooValue wl_host_input_cb(MooValue fenster, MooValue cb) {
    WlFenster* f = unwrap_fenster(fenster);
    if (!f || cb.tag != MOO_FUNC) return moo_bool(0);
    if (f->hat_cb) moo_release(f->input_cb);
    f->input_cb = cb;
    moo_retain(cb);
    f->hat_cb = 1;
    return moo_bool(1);
}

static MooValue wl_host_laufen(void) {
    if (!W.display) return moo_bool(0);
    while (W.laufend && W.fenster && !W.fenster->soll_schliessen) {
        if (wl_display_dispatch(W.display) == -1) break;
    }
    return moo_none();
}

static MooValue wl_host_pump(void) {
    if (!W.display) return moo_bool(0);
    while (wl_display_prepare_read(W.display) != 0)
        wl_display_dispatch_pending(W.display);
    wl_display_flush(W.display);
    struct pollfd p = { wl_display_get_fd(W.display), POLLIN, 0 };
    if (poll(&p, 1, 0) > 0) wl_display_read_events(W.display);
    else wl_display_cancel_read(W.display);
    wl_display_dispatch_pending(W.display);
    return moo_none();
}

static MooValue wl_host_beenden(void) {
    W.laufend = 0;
    return moo_none();
}

static MooValue wl_host_drag_start(MooValue fenster) {
    WlFenster* f = unwrap_fenster(fenster);
    if (!f || !W.seat || !W.letztes_serial) return moo_bool(0);
    xdg_toplevel_move(f->toplevel, W.seat, W.letztes_serial);
    return moo_bool(1);
}

static MooValue wl_host_resize_start(MooValue fenster, MooValue kante) {
    WlFenster* f = unwrap_fenster(fenster);
    if (!f || !W.seat || !W.letztes_serial) return moo_bool(0);
    const char* k = wstr_or(kante, "");
    uint32_t edge = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
    if      (strcmp(k, "n")  == 0) edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    else if (strcmp(k, "no") == 0) edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    else if (strcmp(k, "o")  == 0) edge = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    else if (strcmp(k, "so") == 0) edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    else if (strcmp(k, "s")  == 0) edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    else if (strcmp(k, "sw") == 0) edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    else if (strcmp(k, "w")  == 0) edge = XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    else if (strcmp(k, "nw") == 0) edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    if (edge == XDG_TOPLEVEL_RESIZE_EDGE_NONE) return moo_bool(0);
    xdg_toplevel_resize(f->toplevel, W.seat, W.letztes_serial, edge);
    return moo_bool(1);
}

static MooValue wl_host_minimiere(MooValue fenster) {
    WlFenster* f = unwrap_fenster(fenster);
    if (!f) return moo_bool(0);
    xdg_toplevel_set_minimized(f->toplevel);
    return moo_bool(1);
}

static MooValue wl_host_maximiere_umschalten(MooValue fenster) {
    WlFenster* f = unwrap_fenster(fenster);
    if (!f) return moo_bool(0);
    if (f->maximiert) xdg_toplevel_unset_maximized(f->toplevel);
    else              xdg_toplevel_set_maximized(f->toplevel);
    return moo_bool(1);
}

static MooValue wl_host_transparenz(MooValue fenster, MooValue an) {
    (void)fenster; (void)an;
    return moo_bool(1); /* ARGB8888 per-pixel-Alpha ist immer aktiv */
}

static MooValue wl_host_cursor_setze(MooValue fenster, MooValue name) {
    (void)fenster; (void)name;
    /* v1: ein eigener Pixel-Pfeil (Punkt 6), Formen-Mapping spaeter. */
    if (W.pointer && W.cursor_surface)
        wl_pointer_set_cursor(W.pointer, W.enter_serial, W.cursor_surface, 0, 0);
    return moo_bool(1);
}

static uint32_t wl_host_letztes_serial(MooValue fenster) {
    (void)fenster;
    return W.letztes_serial;
}

static const MooUiHostOps g_wayland_host_ops = {
    .name = "wayland",
    .capabilities = MOO_UI_HOST_CAP_TRANSPARENZ | MOO_UI_HOST_CAP_CSD_MOVE |
                    MOO_UI_HOST_CAP_MINIMIEREN | MOO_UI_HOST_CAP_MAXIMIEREN |
                    MOO_UI_HOST_CAP_INPUT_SERIAL | MOO_UI_HOST_CAP_POLL_DISPATCH |
                    MOO_UI_HOST_CAP_DIRECT_PRESENT,
    .fenster = wl_host_fenster,
    .fenster_schliessen = wl_host_schliessen,
    .zeige = wl_host_zeige,
    .zeichne_frame = wl_host_zeichne_frame_stub,
    .laufen = wl_host_laufen,
    .pump = wl_host_pump,
    .beenden = wl_host_beenden,
    .drag_start = wl_host_drag_start,
    .resize_start = wl_host_resize_start,
    .minimiere = wl_host_minimiere,
    .maximiere_umschalten = wl_host_maximiere_umschalten,
    .transparenz = wl_host_transparenz,
    .cursor_setze = wl_host_cursor_setze,
    .letztes_input_serial = wl_host_letztes_serial,
    .praesentiere = wl_host_praesentiere,
    .input_callback_setze = wl_host_input_cb,
};

/* Link-Anker: erzwingt, dass der Linker moo_ui_wayland.o aus dem
 * Runtime-Archiv zieht (sonst laeuft der Konstruktor nie — Archive
 * linken nur referenzierte Objekte). Wird von moo_ui_host.c gerufen
 * (MOO_HAS_WAYLAND_UI). Idempotent: Registry lehnt Doppel ab. */
void moo_ui_wayland_link_anker(void) {
    (void)moo_ui_host_registriere(&g_wayland_host_ops);
}

__attribute__((constructor))
static void wayland_host_selbstregistrierung(void) {
    (void)moo_ui_host_registriere(&g_wayland_host_ops);
}
