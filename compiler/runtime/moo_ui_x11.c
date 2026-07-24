/*
 * moo_ui_x11.c — Natives X11/XCB-Fenster-Backend (NATIVE-UI-2)
 * ============================================================
 * Dritter Erfueller des MooUiHostOps-Vertrags (nach GTK-Bruecke und
 * Wayland). Direkt auf der X11-Protokoll-Ebene via libxcb — KEIN
 * Toolkit. Laeuft auf echtem Xorg und via Xwayland (kwin).
 *
 * Bausteine:
 *  - ARGB32-Visual (depth 32, TrueColor) + eigene Colormap +
 *    border_pixel=0 (klassische BadMatch-Falle bei 32-bit Windows)
 *  - Rahmenlos via _MOTIF_WM_HINTS decorations=0 (WM-verwaltet, damit
 *    _NET_WM_MOVERESIZE/Minimieren/Maximieren normal funktionieren —
 *    bewusst KEIN override-redirect)
 *  - Present: MIT-SHM ZPixmap, RGBA straight -> premultipliziertes
 *    ARGB32 (LE), identische Konvertierung wie das Wayland-Backend.
 *    v1 Single-Buffer + get_input_focus-Roundtrip als Sync nach put.
 *  - CSD-Hooks: _NET_WM_MOVERESIZE ClientMessage (kwin respektiert es
 *    auch unter Xwayland) mit letzter Button-Press-Root-Position.
 *  - Input: xcb-Events -> Vertrags-Callback cb(art, a, b, c);
 *    Tasten als X11-Keysym via xcb-keysyms (Spalte per Shift-State).
 *  - Kein Input-Serial-Modell: CAP_INPUT_SERIAL nicht gesetzt,
 *    letztes_input_serial liefert 0 (X nutzt Timestamps intern).
 *  - v1-Einschraenkungen: ein Fenster, cursor_setze ist ehrlicher
 *    No-op (eigener Pixel-Cursor als Folge), buffer_scale=1.
 */

#include "moo_ui_host.h"
#include "moo_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/shm.h>
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/xcb_keysyms.h>

/* ------------------------------------------------------------------ */

typedef struct X11Fenster {
    xcb_window_t win;
    int breite, hoehe;
    int konfiguriert;
    /* SHM */
    int shm_id;
    uint8_t* shm_daten;
    size_t shm_groesse;
    xcb_shm_seg_t shm_seg;
    xcb_gcontext_t gc;
    /* Input */
    MooValue input_cb;
    int hat_cb;
    int16_t press_root_x, press_root_y;
    uint32_t press_zeit;
} X11Fenster;

static struct {
    xcb_connection_t* conn;
    xcb_screen_t* screen;
    xcb_visualid_t visual32;
    uint8_t tiefe;
    xcb_colormap_t colormap;
    xcb_key_symbols_t* keysyms;
    int hat_shm;
    X11Fenster* fenster;
    int laufend;
    int init;
    /* Atome */
    xcb_atom_t wm_protocols, wm_delete, motif_hints, net_moveresize;
    xcb_atom_t wm_change_state, net_state, net_max_h, net_max_v;
} X;

static void xlog(const char* was) {
    const char* d = getenv("MOO_UI_DEBUG");
    if (d && d[0] == '1') fprintf(stderr, "[moo_ui_x11] %s\n", was);
}

static inline MooValue wrap_fenster(X11Fenster* f) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, f);
    return v;
}
static inline X11Fenster* unwrap_fenster(MooValue v) {
    if (v.tag != MOO_NUMBER) return NULL;
    return (X11Fenster*)moo_val_as_ptr(v);
}
static inline const char* xstr_or(MooValue v, const char* fb) {
    return (v.tag == MOO_STRING) ? MV_STR(v)->chars : fb;
}
static inline int xnum_or(MooValue v, int fb) {
    return (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : fb;
}

static void event_an_moo(X11Fenster* f, const char* art,
                         double a, double b, double c) {
    if (!f || !f->hat_cb || f->input_cb.tag != MOO_FUNC) return;
    MooValue cb = f->input_cb;
    moo_retain(cb);
    MooValue rv = moo_func_call_4(cb, moo_string_new(art),
                                  moo_number(a), moo_number(b), moo_number(c));
    moo_release(rv);
    moo_release(cb);
}

/* ------------------------------------------------------------------ */

static xcb_atom_t atom_holen(const char* name) {
    xcb_intern_atom_cookie_t ck =
        xcb_intern_atom(X.conn, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(X.conn, ck, NULL);
    if (!r) return XCB_ATOM_NONE;
    xcb_atom_t a = r->atom;
    free(r);
    return a;
}

static int visual32_suchen(void) {
    xcb_depth_iterator_t di =
        xcb_screen_allowed_depths_iterator(X.screen);
    for (; di.rem; xcb_depth_next(&di)) {
        if (di.data->depth != 32) continue;
        xcb_visualtype_iterator_t vi = xcb_depth_visuals_iterator(di.data);
        for (; vi.rem; xcb_visualtype_next(&vi)) {
            if (vi.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
                X.visual32 = vi.data->visual_id;
                X.tiefe = 32;
                return 1;
            }
        }
    }
    return 0;
}

static int ensure_x11(void) {
    if (X.init) return X.conn != NULL;
    X.init = 1;
    X.conn = xcb_connect(NULL, NULL);
    if (!X.conn || xcb_connection_has_error(X.conn)) {
        X.conn = NULL;
        xlog("kein X-Display erreichbar");
        return 0;
    }
    X.screen = xcb_setup_roots_iterator(xcb_get_setup(X.conn)).data;
    if (!X.screen || !visual32_suchen()) {
        xlog("kein ARGB32-TrueColor-Visual");
        xcb_disconnect(X.conn);
        X.conn = NULL;
        return 0;
    }
    X.colormap = xcb_generate_id(X.conn);
    xcb_create_colormap(X.conn, XCB_COLORMAP_ALLOC_NONE, X.colormap,
                        X.screen->root, X.visual32);
    X.keysyms = xcb_key_symbols_alloc(X.conn);
    const xcb_query_extension_reply_t* shm =
        xcb_get_extension_data(X.conn, &xcb_shm_id);
    X.hat_shm = shm && shm->present;
    if (!X.hat_shm) xlog("MIT-SHM fehlt — Present nicht moeglich (v1)");
    X.wm_protocols = atom_holen("WM_PROTOCOLS");
    X.wm_delete = atom_holen("WM_DELETE_WINDOW");
    X.motif_hints = atom_holen("_MOTIF_WM_HINTS");
    X.net_moveresize = atom_holen("_NET_WM_MOVERESIZE");
    X.wm_change_state = atom_holen("WM_CHANGE_STATE");
    X.net_state = atom_holen("_NET_WM_STATE");
    X.net_max_h = atom_holen("_NET_WM_STATE_MAXIMIZED_HORZ");
    X.net_max_v = atom_holen("_NET_WM_STATE_MAXIMIZED_VERT");
    return 1;
}

/* ------------------------------------------------------------------ */

static void shm_freigeben(X11Fenster* f) {
    if (f->shm_daten) {
        xcb_shm_detach(X.conn, f->shm_seg);
        shmdt(f->shm_daten);
        f->shm_daten = NULL;
    }
    if (f->shm_id >= 0) {
        shmctl(f->shm_id, IPC_RMID, NULL);
        f->shm_id = -1;
    }
}

static int shm_anlegen(X11Fenster* f, int b, int h) {
    if (!X.hat_shm) return 0;
    shm_freigeben(f);
    size_t groesse = (size_t)b * 4u * (size_t)h;
    f->shm_id = shmget(IPC_PRIVATE, groesse, IPC_CREAT | 0600);
    if (f->shm_id < 0) return 0;
    f->shm_daten = (uint8_t*)shmat(f->shm_id, NULL, 0);
    if (f->shm_daten == (void*)-1) {
        f->shm_daten = NULL;
        shmctl(f->shm_id, IPC_RMID, NULL);
        f->shm_id = -1;
        return 0;
    }
    f->shm_groesse = groesse;
    f->shm_seg = xcb_generate_id(X.conn);
    xcb_shm_attach(X.conn, f->shm_seg, (uint32_t)f->shm_id, 0);
    f->breite = b;
    f->hoehe = h;
    return 1;
}

/* ------------------------------------------------------------------ */

static MooValue x11_host_fenster(MooValue titel, MooValue breite,
                                 MooValue hoehe, MooValue flags,
                                 MooValue parent) {
    (void)flags; (void)parent; /* rahmenlos ist der einzige Modus (CSD) */
    if (!ensure_x11()) return moo_bool(0);
    if (X.fenster) return moo_bool(0); /* v1: ein Fenster */
    X11Fenster* f = (X11Fenster*)calloc(1, sizeof(X11Fenster));
    if (!f) return moo_bool(0);
    f->shm_id = -1;
    int b = xnum_or(breite, 640);
    int h = xnum_or(hoehe, 480);
    f->win = xcb_generate_id(X.conn);
    uint32_t werte[4];
    werte[0] = 0; /* back_pixel   — 32-bit: Pflicht gegen BadMatch */
    werte[1] = 0; /* border_pixel — dito */
    werte[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
               XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
               XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEY_PRESS |
               XCB_EVENT_MASK_KEY_RELEASE;
    werte[3] = X.colormap;
    xcb_create_window(X.conn, 32, f->win, X.screen->root, 0, 0,
                      (uint16_t)b, (uint16_t)h, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, X.visual32,
                      XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL |
                      XCB_CW_EVENT_MASK | XCB_CW_COLORMAP, werte);
    const char* t = xstr_or(titel, "moo");
    xcb_change_property(X.conn, XCB_PROP_MODE_REPLACE, f->win,
                        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                        (uint32_t)strlen(t), t);
    xcb_change_property(X.conn, XCB_PROP_MODE_REPLACE, f->win,
                        X.wm_protocols, XCB_ATOM_ATOM, 32, 1, &X.wm_delete);
    /* Rahmenlos: _MOTIF_WM_HINTS, decorations=0. */
    uint32_t motif[5] = { 2u /* MWM_HINTS_DECORATIONS */, 0, 0, 0, 0 };
    xcb_change_property(X.conn, XCB_PROP_MODE_REPLACE, f->win,
                        X.motif_hints, X.motif_hints, 32, 5, motif);
    f->gc = xcb_generate_id(X.conn);
    xcb_create_gc(X.conn, f->gc, f->win, 0, NULL);
    if (!shm_anlegen(f, b, h)) {
        xcb_destroy_window(X.conn, f->win);
        xcb_flush(X.conn);
        free(f);
        return moo_bool(0);
    }
    X.fenster = f;
    xlog("fenster angelegt");
    return wrap_fenster(f);
}

static MooValue x11_host_schliessen(MooValue fenster) {
    X11Fenster* f = unwrap_fenster(fenster);
    if (!f || f != X.fenster) return moo_bool(0);
    if (f->hat_cb) { moo_release(f->input_cb); f->hat_cb = 0; }
    shm_freigeben(f);
    xcb_free_gc(X.conn, f->gc);
    xcb_destroy_window(X.conn, f->win);
    xcb_flush(X.conn);
    X.fenster = NULL;
    X.laufend = 0;
    free(f);
    return moo_bool(1);
}

static MooValue x11_host_zeige(MooValue fenster) {
    X11Fenster* f = unwrap_fenster(fenster);
    if (!f) return moo_bool(0);
    xcb_map_window(X.conn, f->win);
    xcb_flush(X.conn);
    f->konfiguriert = 1;
    return moo_bool(1);
}

static MooValue x11_host_praesentiere(MooValue fenster, MooValue frame) {
    X11Fenster* f = unwrap_fenster(fenster);
    if (!f || !f->shm_daten || frame.tag != MOO_FRAME) return moo_bool(0);
    MooFrame* fr = MV_FRAME(frame);
    if (!fr || !fr->pixels) return moo_bool(0);
    int b = f->breite, h = f->hoehe;
    int kb = fr->width < b ? fr->width : b;
    int kh = fr->height < h ? fr->height : h;
    size_t stride = (size_t)b * 4u;
    memset(f->shm_daten, 0, stride * (size_t)h);
    /* RGBA straight (top-left) -> premultipliziertes ARGB32 (LE),
     * identische Konvertierung wie Wayland-/GTK-Present. */
    for (int row = 0; row < kh; ++row) {
        const uint8_t* src = fr->pixels + (size_t)row * (size_t)fr->stride;
        uint32_t* dst = (uint32_t*)(f->shm_daten + (size_t)row * stride);
        for (int col = 0; col < kb; ++col) {
            uint32_t a = src[3];
            uint32_t r = (src[0] * a + 127u) / 255u;
            uint32_t g = (src[1] * a + 127u) / 255u;
            uint32_t bl = (src[2] * a + 127u) / 255u;
            dst[col] = (a << 24) | (r << 16) | (g << 8) | bl;
            src += 4;
        }
    }
    xcb_shm_put_image(X.conn, f->win, f->gc,
                      (uint16_t)b, (uint16_t)h, 0, 0,
                      (uint16_t)b, (uint16_t)h, 0, 0,
                      32, XCB_IMAGE_FORMAT_Z_PIXMAP, 0, f->shm_seg, 0);
    /* Roundtrip-Sync: X kopiert serverseitig; danach ist das SHM-Segment
     * wieder frei beschreibbar (v1 Single-Buffer-Vertrag). */
    xcb_get_input_focus_reply_t* sync =
        xcb_get_input_focus_reply(X.conn, xcb_get_input_focus(X.conn), NULL);
    free(sync);
    xcb_flush(X.conn);
    return moo_bool(1);
}

static MooValue x11_host_zeichne_frame_stub(MooValue z, MooValue x, MooValue y,
                                            MooValue b, MooValue h, MooValue fr) {
    (void)z; (void)x; (void)y; (void)b; (void)h; (void)fr;
    return moo_bool(0); /* kein Zeichner-Modell; praesentiere() nutzen */
}

static MooValue x11_host_input_cb(MooValue fenster, MooValue cb) {
    X11Fenster* f = unwrap_fenster(fenster);
    if (!f) return moo_bool(0);
    if (f->hat_cb) { moo_release(f->input_cb); f->hat_cb = 0; }
    if (cb.tag == MOO_FUNC) {
        f->input_cb = cb;
        moo_retain(cb);
        f->hat_cb = 1;
    }
    return moo_bool(1);
}

/* ------------------------------------------------------------------ */

static void x11_event(xcb_generic_event_t* ev) {
    X11Fenster* f = X.fenster;
    if (!f) return;
    switch (ev->response_type & 0x7f) {
    case XCB_BUTTON_PRESS: {
        xcb_button_press_event_t* e = (xcb_button_press_event_t*)ev;
        if (e->detail >= 1 && e->detail <= 3) {
            f->press_root_x = e->root_x;
            f->press_root_y = e->root_y;
            f->press_zeit = e->time;
            event_an_moo(f, "maus_runter", e->event_x, e->event_y, e->detail);
        } else if (e->detail == 4 || e->detail == 5) {
            /* Rad: v1 nicht im Vertrag — bewusst still. */
        }
        break;
    }
    case XCB_BUTTON_RELEASE: {
        xcb_button_release_event_t* e = (xcb_button_release_event_t*)ev;
        if (e->detail >= 1 && e->detail <= 3)
            event_an_moo(f, "maus_hoch", e->event_x, e->event_y, e->detail);
        break;
    }
    case XCB_MOTION_NOTIFY: {
        xcb_motion_notify_event_t* e = (xcb_motion_notify_event_t*)ev;
        event_an_moo(f, "bewegung", e->event_x, e->event_y, 0);
        break;
    }
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE: {
        xcb_key_press_event_t* e = (xcb_key_press_event_t*)ev;
        int col = (e->state & XCB_MOD_MASK_SHIFT) ? 1 : 0;
        xcb_keysym_t ks = X.keysyms
            ? xcb_key_symbols_get_keysym(X.keysyms, e->detail, col)
            : 0;
        event_an_moo(f,
                     (ev->response_type & 0x7f) == XCB_KEY_PRESS
                         ? "taste_runter" : "taste_hoch",
                     (double)ks, 0, 0);
        break;
    }
    case XCB_CONFIGURE_NOTIFY: {
        xcb_configure_notify_event_t* e = (xcb_configure_notify_event_t*)ev;
        if (e->width > 0 && e->height > 0 &&
            (e->width != f->breite || e->height != f->hoehe)) {
            if (shm_anlegen(f, e->width, e->height))
                event_an_moo(f, "resize", e->width, e->height, 0);
        }
        break;
    }
    case XCB_EXPOSE: {
        xcb_expose_event_t* e = (xcb_expose_event_t*)ev;
        if (e->count == 0)
            event_an_moo(f, "resize", f->breite, f->hoehe, 0);
        break;
    }
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t* e = (xcb_client_message_event_t*)ev;
        if (e->type == X.wm_protocols &&
            e->data.data32[0] == X.wm_delete)
            event_an_moo(f, "schliessen", 0, 0, 0);
        break;
    }
    default:
        break;
    }
}

static MooValue x11_host_laufen(void) {
    if (!X.conn || !X.fenster) return moo_bool(0);
    X.laufend = 1;
    struct pollfd pfd;
    pfd.fd = xcb_get_file_descriptor(X.conn);
    pfd.events = POLLIN;
    while (X.laufend && X.fenster && !xcb_connection_has_error(X.conn)) {
        xcb_generic_event_t* ev;
        while ((ev = xcb_poll_for_event(X.conn)) != NULL) {
            x11_event(ev);
            free(ev);
            if (!X.laufend || !X.fenster) break;
        }
        if (!X.laufend || !X.fenster) break;
        xcb_flush(X.conn);
        poll(&pfd, 1, 50);
    }
    return moo_none();
}

static MooValue x11_host_pump(void) {
    if (!X.conn) return moo_bool(0);
    xcb_generic_event_t* ev;
    while ((ev = xcb_poll_for_event(X.conn)) != NULL) {
        x11_event(ev);
        free(ev);
    }
    xcb_flush(X.conn);
    return moo_none();
}

static MooValue x11_host_beenden(void) {
    X.laufend = 0;
    return moo_none();
}

/* ------------------------------------------------------------------ */

static void moveresize_senden(X11Fenster* f, uint32_t richtung) {
    /* Spec: Pointer-Grab des Clients vorher loesen. */
    xcb_ungrab_pointer(X.conn, XCB_CURRENT_TIME);
    xcb_client_message_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = f->win;
    ev.type = X.net_moveresize;
    ev.data.data32[0] = (uint32_t)f->press_root_x;
    ev.data.data32[1] = (uint32_t)f->press_root_y;
    ev.data.data32[2] = richtung;
    ev.data.data32[3] = 1; /* Button 1 */
    ev.data.data32[4] = 1; /* Quelle: normale Anwendung */
    xcb_send_event(X.conn, 0, X.screen->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   (const char*)&ev);
    xcb_flush(X.conn);
}

static MooValue x11_host_drag_start(MooValue fenster) {
    X11Fenster* f = unwrap_fenster(fenster);
    if (!f) return moo_bool(0);
    moveresize_senden(f, 8 /* _NET_WM_MOVERESIZE_MOVE */);
    return moo_bool(1);
}

static MooValue x11_host_resize_start(MooValue fenster, MooValue kante) {
    X11Fenster* f = unwrap_fenster(fenster);
    if (!f) return moo_bool(0);
    const char* k = xstr_or(kante, "");
    uint32_t richtung;
    if      (strcmp(k, "nw") == 0) richtung = 0;
    else if (strcmp(k, "n")  == 0) richtung = 1;
    else if (strcmp(k, "no") == 0) richtung = 2;
    else if (strcmp(k, "o")  == 0) richtung = 3;
    else if (strcmp(k, "so") == 0) richtung = 4;
    else if (strcmp(k, "s")  == 0) richtung = 5;
    else if (strcmp(k, "sw") == 0) richtung = 6;
    else if (strcmp(k, "w")  == 0) richtung = 7;
    else return moo_bool(0);
    moveresize_senden(f, richtung);
    return moo_bool(1);
}

static MooValue x11_host_minimiere(MooValue fenster) {
    X11Fenster* f = unwrap_fenster(fenster);
    if (!f) return moo_bool(0);
    xcb_client_message_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = f->win;
    ev.type = X.wm_change_state;
    ev.data.data32[0] = 3; /* IconicState */
    xcb_send_event(X.conn, 0, X.screen->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   (const char*)&ev);
    xcb_flush(X.conn);
    return moo_bool(1);
}

static MooValue x11_host_maximiere_umschalten(MooValue fenster) {
    X11Fenster* f = unwrap_fenster(fenster);
    if (!f) return moo_bool(0);
    xcb_client_message_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = f->win;
    ev.type = X.net_state;
    ev.data.data32[0] = 2; /* _NET_WM_STATE_TOGGLE */
    ev.data.data32[1] = X.net_max_h;
    ev.data.data32[2] = X.net_max_v;
    ev.data.data32[3] = 1;
    xcb_send_event(X.conn, 0, X.screen->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   (const char*)&ev);
    xcb_flush(X.conn);
    return moo_bool(1);
}

static MooValue x11_host_transparenz(MooValue fenster, MooValue an) {
    (void)fenster; (void)an;
    return moo_bool(1); /* ARGB32-Visual: per-pixel Alpha ist immer aktiv */
}

static MooValue x11_host_cursor_setze(MooValue fenster, MooValue name) {
    (void)fenster; (void)name;
    /* v1: ehrlicher No-op — eigener Pixel-Cursor als Folgepunkt
     * (gleiche Glass-Layer-Quelle wie Wayland). */
    return moo_bool(1);
}

static uint32_t x11_host_letztes_serial(MooValue fenster) {
    (void)fenster;
    return 0; /* kein Serial-Modell — CAP_INPUT_SERIAL nicht gesetzt */
}

/* ------------------------------------------------------------------ */

static const MooUiHostOps g_x11_host_ops = {
    .name = "x11",
    .capabilities = MOO_UI_HOST_CAP_TRANSPARENZ |
                    MOO_UI_HOST_CAP_CSD_MOVE |
                    MOO_UI_HOST_CAP_MINIMIEREN |
                    MOO_UI_HOST_CAP_MAXIMIEREN |
                    MOO_UI_HOST_CAP_POLL_DISPATCH |
                    MOO_UI_HOST_CAP_DIRECT_PRESENT,
    .fenster = x11_host_fenster,
    .fenster_schliessen = x11_host_schliessen,
    .zeige = x11_host_zeige,
    .zeichne_frame = x11_host_zeichne_frame_stub,
    .laufen = x11_host_laufen,
    .pump = x11_host_pump,
    .beenden = x11_host_beenden,
    .drag_start = x11_host_drag_start,
    .resize_start = x11_host_resize_start,
    .minimiere = x11_host_minimiere,
    .maximiere_umschalten = x11_host_maximiere_umschalten,
    .transparenz = x11_host_transparenz,
    .cursor_setze = x11_host_cursor_setze,
    .letztes_input_serial = x11_host_letztes_serial,
    .praesentiere = x11_host_praesentiere,
    .input_callback_setze = x11_host_input_cb,
};

void moo_ui_x11_link_anker(void) {
    /* Erzwingt das Einlinken dieser Uebersetzungseinheit. */
}

__attribute__((constructor))
static void x11_host_selbstregistrierung(void) {
    moo_ui_host_registriere(&g_x11_host_ops);
}
