/*
 * moo_ui_gtk.c — Linux-Backend fuer moo_ui.h via GTK3.
 * ====================================================
 *
 * Implementiert die OS-neutrale UI-API aus moo_ui.h mit GTK3. Layout: jedes
 * Fenster hat einen GtkFixed als Content-Container (erlaubt absolute
 * x/y/w/h-Platzierung wie in AutoIt/GUIBuilder).
 *
 * Callback-Ownership (analog altem moo_tray.c):
 *   Jeder Callback wird als Heap-MooValue* ("cb_box") gehalten.
 *   g_signal_connect_data(..., cb, cb_box_destroy, 0) sorgt dafuer, dass
 *   cb_box_destroy beim Widget-Destroy feuert, die moo-Referenz released
 *   und die Box freigibt. KEIN LEAK, KEIN DANGLING-POINTER.
 *
 * Widget-Handles sind MooValue { tag=MOO_NUMBER, data=GtkWidget* }. Der
 * "container", an den Kinder angehaengt werden, wird je nach Parent-Typ
 * aus g_object_get_data(parent, "moo-fixed") geholt; Widgets ohne "moo-fixed"
 * (direkt ein GtkFixed, ein Tab-Content etc.) werden direkt als Container
 * benutzt.
 *
 * Plan-Referenz: Memory `plan-002-moo-ui-cross-platform`.
 */

#include "moo_runtime.h"
#include "moo_ui.h"
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * GTK-Init (idempotent) + globale Event-Loop-Bookkeeping
 * ------------------------------------------------------------------ */

static int g_gtk_init = 0;
static int g_open_windows = 0;      /* Top-Level-Fenster, die noch existieren */
static int g_ui_debug_on = 0;

static inline void ensure_gtk(void) {
    if (!g_gtk_init) {
        gtk_init(NULL, NULL);
        g_gtk_init = 1;
    }
}

static void ui_log(const char* fmt, const char* arg) {
    if (!g_ui_debug_on) return;
    if (arg) fprintf(stderr, "[moo_ui] %s: %s\n", fmt, arg);
    else     fprintf(stderr, "[moo_ui] %s\n", fmt);
}

/* ------------------------------------------------------------------ *
 * Callback-Ownership (Pattern aus moo_tray.c)
 * ------------------------------------------------------------------ */

static void cb_box_destroy(gpointer data, GClosure* closure) {
    (void)closure;
    MooValue* cb = (MooValue*)data;
    if (!cb) return;
    moo_release(*cb);
    free(cb);
}

static MooValue* cb_box_new(MooValue callback) {
    MooValue* box = (MooValue*)malloc(sizeof(MooValue));
    *box = callback;
    moo_retain(callback);
    return box;
}

static void on_click_trampoline(GtkButton* btn, gpointer user_data) {
    (void)btn;
    MooValue* cb = (MooValue*)user_data;
    if (cb && cb->tag == MOO_FUNC) moo_func_call_0(*cb);
}

/* ------------------------------------------------------------------ *
 * Hilfen: MooValue ↔ GtkWidget, Parent-Container-Aufloesung
 * ------------------------------------------------------------------ */

static inline MooValue wrap_widget(GtkWidget* w) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, w);
    return v;
}

static inline GtkWidget* unwrap_widget(MooValue v) {
    if (v.tag != MOO_NUMBER) return NULL;
    return (GtkWidget*)moo_val_as_ptr(v);
}

/* Liefert den GtkFixed-Container, in den ein Kind-Widget mit (x,y,w,h)
 * eingefuegt werden soll. Ein Fenster speichert seinen Fixed in
 * "moo-fixed"; ein Tab-Content oder Scroll-Content IST schon ein Fixed.
 * Ein Rahmen (GtkFrame) hat ebenfalls "moo-fixed" gesetzt. */
static GtkWidget* resolve_container(MooValue parent) {
    GtkWidget* p = unwrap_widget(parent);
    if (!p) return NULL;
    GtkWidget* fx = (GtkWidget*)g_object_get_data(G_OBJECT(p), "moo-fixed");
    if (fx) return fx;
    return p;
}

static inline const char* str_or(MooValue v, const char* fallback) {
    return (v.tag == MOO_STRING) ? MV_STR(v)->chars : fallback;
}

static inline int num_or(MooValue v, int fallback) {
    return (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : fallback;
}

static inline gboolean bool_or(MooValue v, gboolean fallback) {
    if (v.tag == MOO_BOOL) return (gboolean)MV_BOOL(v);
    return fallback;
}

static void place_child(GtkWidget* fixed, GtkWidget* child,
                        int x, int y, int b, int h) {
    gtk_fixed_put(GTK_FIXED(fixed), child, x, y);
    if (b > 0 && h > 0) gtk_widget_set_size_request(child, b, h);
}

/* ------------------------------------------------------------------ *
 * Initialisierung & Event-Loop
 * ------------------------------------------------------------------ */

MooValue moo_ui_init(void) {
    ensure_gtk();
    return moo_bool(1);
}

MooValue moo_ui_laufen(void) {
    ensure_gtk();
    ui_log("ui_laufen: enter main loop", NULL);
    gtk_main();
    ui_log("ui_laufen: exit main loop", NULL);
    return moo_none();
}

MooValue moo_ui_beenden(void) {
    if (gtk_main_level() > 0) gtk_main_quit();
    return moo_none();
}

MooValue moo_ui_pump(void) {
    ensure_gtk();
    while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
    return moo_none();
}

MooValue moo_ui_debug(MooValue an) {
    g_ui_debug_on = bool_or(an, 0) ? 1 : 0;
    return moo_bool(g_ui_debug_on);
}

/* ------------------------------------------------------------------ *
 * Fenster & Top-Level
 * ------------------------------------------------------------------ */

static void on_window_destroy(GtkWidget* w, gpointer user_data) {
    (void)w; (void)user_data;
    if (g_open_windows > 0) g_open_windows--;
    /* Wenn letztes Top-Level-Fenster weg UND kein Tray aktiv → Loop beenden.
     * Tray-Koppelung folgt ueber ui-tray; aktuell: bei 0 Fenstern quitten. */
    if (g_open_windows == 0 && gtk_main_level() > 0) {
        gtk_main_quit();
    }
}

MooValue moo_ui_fenster(MooValue titel, MooValue breite, MooValue hoehe,
                        MooValue flags, MooValue parent) {
    ensure_gtk();
    const char* t = str_or(titel, "moo");
    int w = num_or(breite, 640);
    int h = num_or(hoehe, 480);
    unsigned f = (unsigned)num_or(flags, 0);

    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), t);
    gtk_window_set_default_size(GTK_WINDOW(win), w, h);

    gtk_window_set_resizable(GTK_WINDOW(win),
        (f & MOO_UI_FLAG_RESIZABLE) ? TRUE : FALSE);

    if (f & MOO_UI_FLAG_MAXIMIZED) gtk_window_maximize(GTK_WINDOW(win));
    if (f & MOO_UI_FLAG_FULLSCREEN) gtk_window_fullscreen(GTK_WINDOW(win));
    if (f & MOO_UI_FLAG_MODAL)     gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    if (f & MOO_UI_FLAG_NO_DECOR)  gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    if (f & MOO_UI_FLAG_ALWAYS_TOP) gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);

    GtkWidget* parent_win = unwrap_widget(parent);
    if (parent_win && GTK_IS_WINDOW(parent_win)) {
        gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(parent_win));
    }

    /* GtkFixed als Content: erlaubt x/y/w/h-Placement. */
    GtkWidget* fx = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(win), fx);
    g_object_set_data(G_OBJECT(win), "moo-fixed", fx);

    g_open_windows++;
    g_signal_connect(win, "destroy", G_CALLBACK(on_window_destroy), NULL);

    ui_log("ui_fenster", t);
    return wrap_widget(win);
}

MooValue moo_ui_fenster_titel_setze(MooValue fenster, MooValue titel) {
    GtkWidget* w = unwrap_widget(fenster);
    if (!w || !GTK_IS_WINDOW(w)) return moo_bool(0);
    gtk_window_set_title(GTK_WINDOW(w), str_or(titel, ""));
    return moo_bool(1);
}

MooValue moo_ui_fenster_icon_setze(MooValue fenster, MooValue pfad) {
    GtkWidget* w = unwrap_widget(fenster);
    if (!w || !GTK_IS_WINDOW(w)) return moo_bool(0);
    gtk_window_set_icon_from_file(GTK_WINDOW(w), str_or(pfad, ""), NULL);
    return moo_bool(1);
}

MooValue moo_ui_fenster_groesse_setze(MooValue fenster, MooValue b, MooValue h) {
    GtkWidget* w = unwrap_widget(fenster);
    if (!w || !GTK_IS_WINDOW(w)) return moo_bool(0);
    gtk_window_resize(GTK_WINDOW(w), num_or(b, 100), num_or(h, 100));
    return moo_bool(1);
}

MooValue moo_ui_fenster_position_setze(MooValue fenster, MooValue x, MooValue y) {
    GtkWidget* w = unwrap_widget(fenster);
    if (!w || !GTK_IS_WINDOW(w)) return moo_bool(0);
    gtk_window_move(GTK_WINDOW(w), num_or(x, 0), num_or(y, 0));
    return moo_bool(1);
}

MooValue moo_ui_fenster_schliessen(MooValue fenster) {
    GtkWidget* w = unwrap_widget(fenster);
    if (!w) return moo_bool(0);
    gtk_widget_destroy(w);
    return moo_bool(1);
}

MooValue moo_ui_zeige(MooValue fenster) {
    GtkWidget* w = unwrap_widget(fenster);
    if (!w) return moo_bool(0);
    gtk_widget_show_all(w);
    return moo_bool(1);
}

MooValue moo_ui_zeige_nebenbei(MooValue fenster) {
    return moo_ui_zeige(fenster);
}

/* on_close: Callback(fenster) → gibt wahr/falsch zurueck. wahr → schliessen.
 * GTK "delete-event" handler erwartet gboolean (TRUE = block close). */
static gboolean on_delete_event_trampoline(GtkWidget* w, GdkEvent* ev,
                                           gpointer user_data) {
    (void)w; (void)ev;
    MooValue* cb = (MooValue*)user_data;
    if (!cb || cb->tag != MOO_FUNC) return FALSE;  /* allow close */
    MooValue rv = moo_func_call_0(*cb);
    gboolean allow = moo_is_truthy(rv);
    moo_release(rv);
    return allow ? FALSE : TRUE;  /* FALSE = proceed to destroy */
}

MooValue moo_ui_fenster_on_close(MooValue fenster, MooValue callback) {
    GtkWidget* w = unwrap_widget(fenster);
    if (!w || !GTK_IS_WINDOW(w)) return moo_bool(0);
    MooValue* cb = cb_box_new(callback);
    g_signal_connect_data(w, "delete-event",
                          G_CALLBACK(on_delete_event_trampoline),
                          cb, cb_box_destroy, 0);
    return moo_bool(1);
}

/* ------------------------------------------------------------------ *
 * Label + Knopf (Schritt 1: Smoke-Test-Basis)
 * ------------------------------------------------------------------ */

MooValue moo_ui_label(MooValue parent, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* lbl = gtk_label_new(str_or(text, ""));
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    place_child(fx, lbl, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    gtk_widget_show(lbl);
    return wrap_widget(lbl);
}

MooValue moo_ui_label_setze(MooValue label, MooValue text) {
    GtkWidget* l = unwrap_widget(label);
    if (!l || !GTK_IS_LABEL(l)) return moo_bool(0);
    gtk_label_set_text(GTK_LABEL(l), str_or(text, ""));
    return moo_bool(1);
}

MooValue moo_ui_label_text(MooValue label) {
    GtkWidget* l = unwrap_widget(label);
    if (!l || !GTK_IS_LABEL(l)) return moo_string_new("");
    const char* s = gtk_label_get_text(GTK_LABEL(l));
    return moo_string_new(s ? s : "");
}

MooValue moo_ui_knopf(MooValue parent, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h,
                      MooValue callback) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* btn = gtk_button_new_with_label(str_or(text, "OK"));
    place_child(fx, btn, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));

    MooValue* cb = cb_box_new(callback);
    g_signal_connect_data(btn, "clicked",
                          G_CALLBACK(on_click_trampoline),
                          cb, cb_box_destroy, 0);
    gtk_widget_show(btn);
    return wrap_widget(btn);
}
