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

    /* Vertikaler Container: (optional) MenuBar oben, GtkFixed unten.
     * Menueleiste wird spaeter ueber moo_ui_menueleiste in "moo-vbox" eingehaengt. */
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(win), vbox);
    GtkWidget* fx = gtk_fixed_new();
    gtk_box_pack_start(GTK_BOX(vbox), fx, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(win), "moo-vbox", vbox);
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

/* ================================================================== *
 * Schritt 2: Checkbox, Radio, Eingabe, Textbereich
 * ================================================================== */

static void on_toggle_trampoline(GtkToggleButton* tb, gpointer user_data) {
    (void)tb;
    MooValue* cb = (MooValue*)user_data;
    if (cb && cb->tag == MOO_FUNC) moo_func_call_0(*cb);
}

MooValue moo_ui_checkbox(MooValue parent, MooValue text,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue initial, MooValue callback) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* cb = gtk_check_button_new_with_label(str_or(text, ""));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb), bool_or(initial, 0));
    place_child(fx, cb, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    if (callback.tag == MOO_FUNC) {
        MooValue* box = cb_box_new(callback);
        g_signal_connect_data(cb, "toggled",
                              G_CALLBACK(on_toggle_trampoline),
                              box, cb_box_destroy, 0);
    }
    gtk_widget_show(cb);
    return wrap_widget(cb);
}

MooValue moo_ui_checkbox_wert(MooValue checkbox) {
    GtkWidget* c = unwrap_widget(checkbox);
    if (!c || !GTK_IS_TOGGLE_BUTTON(c)) return moo_bool(0);
    return moo_bool(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c)));
}

MooValue moo_ui_checkbox_setze(MooValue checkbox, MooValue wert) {
    GtkWidget* c = unwrap_widget(checkbox);
    if (!c || !GTK_IS_TOGGLE_BUTTON(c)) return moo_bool(0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), bool_or(wert, 0));
    return moo_bool(1);
}

/* Radio-Gruppen: per Top-Level-Fenster speichern wir den ersten Radio-Button
 * einer Gruppe unter dem Key "moo-radio-grp:<name>". Folge-Radios benutzen
 * gtk_radio_button_new_with_label_from_widget. */
MooValue moo_ui_radio(MooValue parent, MooValue gruppe, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h,
                      MooValue callback) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* top = gtk_widget_get_toplevel(fx);

    const char* grp = str_or(gruppe, "default");
    char key[128];
    g_snprintf(key, sizeof(key), "moo-radio-grp:%s", grp);

    GtkWidget* first = (GtkWidget*)g_object_get_data(G_OBJECT(top), key);
    GtkWidget* rb;
    if (first && GTK_IS_RADIO_BUTTON(first)) {
        rb = gtk_radio_button_new_with_label_from_widget(
                 GTK_RADIO_BUTTON(first), str_or(text, ""));
    } else {
        rb = gtk_radio_button_new_with_label(NULL, str_or(text, ""));
        g_object_set_data(G_OBJECT(top), key, rb);
    }
    place_child(fx, rb, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    if (callback.tag == MOO_FUNC) {
        MooValue* box = cb_box_new(callback);
        g_signal_connect_data(rb, "toggled",
                              G_CALLBACK(on_toggle_trampoline),
                              box, cb_box_destroy, 0);
    }
    gtk_widget_show(rb);
    return wrap_widget(rb);
}

MooValue moo_ui_radio_wert(MooValue radio) {
    GtkWidget* r = unwrap_widget(radio);
    if (!r || !GTK_IS_TOGGLE_BUTTON(r)) return moo_bool(0);
    return moo_bool(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r)));
}

static void on_entry_changed_trampoline(GtkEditable* e, gpointer user_data) {
    (void)e;
    MooValue* cb = (MooValue*)user_data;
    if (cb && cb->tag == MOO_FUNC) moo_func_call_0(*cb);
}

MooValue moo_ui_eingabe(MooValue parent,
                        MooValue x, MooValue y, MooValue b, MooValue h,
                        MooValue platzhalter, MooValue passwort) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* en = gtk_entry_new();
    if (platzhalter.tag == MOO_STRING) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(en), MV_STR(platzhalter)->chars);
    }
    if (bool_or(passwort, 0)) gtk_entry_set_visibility(GTK_ENTRY(en), FALSE);
    place_child(fx, en, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    gtk_widget_show(en);
    return wrap_widget(en);
}

MooValue moo_ui_eingabe_text(MooValue eingabe) {
    GtkWidget* e = unwrap_widget(eingabe);
    if (!e || !GTK_IS_ENTRY(e)) return moo_string_new("");
    return moo_string_new(gtk_entry_get_text(GTK_ENTRY(e)));
}

MooValue moo_ui_eingabe_setze(MooValue eingabe, MooValue text) {
    GtkWidget* e = unwrap_widget(eingabe);
    if (!e || !GTK_IS_ENTRY(e)) return moo_bool(0);
    gtk_entry_set_text(GTK_ENTRY(e), str_or(text, ""));
    return moo_bool(1);
}

MooValue moo_ui_eingabe_on_change(MooValue eingabe, MooValue callback) {
    GtkWidget* e = unwrap_widget(eingabe);
    if (!e || !GTK_IS_ENTRY(e)) return moo_bool(0);
    MooValue* box = cb_box_new(callback);
    g_signal_connect_data(e, "changed",
                          G_CALLBACK(on_entry_changed_trampoline),
                          box, cb_box_destroy, 0);
    return moo_bool(1);
}

/* Textbereich: Wir liefern als Handle die aeussere GtkScrolledWindow und
 * legen "moo-tv" = GtkTextView ab. So funktionieren generische Widget-Ops
 * (sichtbar/position/groesse) auf dem Scroller, Text-Getter/Setter greifen
 * auf den TextView. */
MooValue moo_ui_textbereich(MooValue parent,
                            MooValue x, MooValue y, MooValue b, MooValue h) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkWidget* tv = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    g_object_set_data(G_OBJECT(sw), "moo-tv", tv);
    place_child(fx, sw, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    gtk_widget_show_all(sw);
    return wrap_widget(sw);
}

static GtkTextView* tb_get_view(MooValue tb) {
    GtkWidget* sw = unwrap_widget(tb);
    if (!sw) return NULL;
    GtkWidget* tv = (GtkWidget*)g_object_get_data(G_OBJECT(sw), "moo-tv");
    return (tv && GTK_IS_TEXT_VIEW(tv)) ? GTK_TEXT_VIEW(tv) : NULL;
}

MooValue moo_ui_textbereich_text(MooValue tb) {
    GtkTextView* v = tb_get_view(tb);
    if (!v) return moo_string_new("");
    GtkTextBuffer* buf = gtk_text_view_get_buffer(v);
    GtkTextIter s, e;
    gtk_text_buffer_get_start_iter(buf, &s);
    gtk_text_buffer_get_end_iter(buf, &e);
    gchar* txt = gtk_text_buffer_get_text(buf, &s, &e, FALSE);
    MooValue rv = moo_string_new(txt ? txt : "");
    g_free(txt);
    return rv;
}

MooValue moo_ui_textbereich_setze(MooValue tb, MooValue text) {
    GtkTextView* v = tb_get_view(tb);
    if (!v) return moo_bool(0);
    GtkTextBuffer* buf = gtk_text_view_get_buffer(v);
    const char* s = str_or(text, "");
    gtk_text_buffer_set_text(buf, s, -1);
    return moo_bool(1);
}

MooValue moo_ui_textbereich_anhaengen(MooValue tb, MooValue text) {
    GtkTextView* v = tb_get_view(tb);
    if (!v) return moo_bool(0);
    GtkTextBuffer* buf = gtk_text_view_get_buffer(v);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    const char* s = str_or(text, "");
    gtk_text_buffer_insert(buf, &end, s, -1);
    return moo_bool(1);
}

/* ================================================================== *
 * Schritt 3: Dropdown, Liste, Slider, Fortschritt, Bild, Leinwand
 * ================================================================== */

static void on_combo_changed_trampoline(GtkComboBox* cb, gpointer user_data) {
    (void)cb;
    MooValue* cv = (MooValue*)user_data;
    if (cv && cv->tag == MOO_FUNC) moo_func_call_0(*cv);
}

MooValue moo_ui_dropdown(MooValue parent, MooValue optionen,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue callback) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* dd = gtk_combo_box_text_new();
    if (optionen.tag == MOO_LIST) {
        int32_t n = moo_list_iter_len(optionen);
        for (int32_t i = 0; i < n; i++) {
            MooValue item = moo_list_iter_get(optionen, i);
            if (item.tag == MOO_STRING) {
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dd),
                                               MV_STR(item)->chars);
            }
        }
    }
    place_child(fx, dd, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    if (callback.tag == MOO_FUNC) {
        MooValue* box = cb_box_new(callback);
        g_signal_connect_data(dd, "changed",
                              G_CALLBACK(on_combo_changed_trampoline),
                              box, cb_box_destroy, 0);
    }
    gtk_widget_show(dd);
    return wrap_widget(dd);
}

MooValue moo_ui_dropdown_auswahl(MooValue dd) {
    GtkWidget* c = unwrap_widget(dd);
    if (!c || !GTK_IS_COMBO_BOX(c)) return moo_number(-1);
    return moo_number((double)gtk_combo_box_get_active(GTK_COMBO_BOX(c)));
}

MooValue moo_ui_dropdown_auswahl_setze(MooValue dd, MooValue index) {
    GtkWidget* c = unwrap_widget(dd);
    if (!c || !GTK_IS_COMBO_BOX(c)) return moo_bool(0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), num_or(index, -1));
    return moo_bool(1);
}

MooValue moo_ui_dropdown_text(MooValue dd) {
    GtkWidget* c = unwrap_widget(dd);
    if (!c || !GTK_IS_COMBO_BOX_TEXT(c)) return moo_string_new("");
    gchar* txt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(c));
    MooValue rv = moo_string_new(txt ? txt : "");
    g_free(txt);
    return rv;
}

/* Liste: GtkTreeView + GtkListStore (alle Spalten als G_TYPE_STRING).
 * Handle = GtkScrolledWindow. "moo-tv"=GtkTreeView, "moo-store"=GtkListStore,
 * "moo-ncols"=int gespeichert via g_object_set_data. */
MooValue moo_ui_liste(MooValue parent, MooValue spalten,
                      MooValue x, MooValue y, MooValue b, MooValue h) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);

    int32_t ncols = 1;
    if (spalten.tag == MOO_LIST) ncols = moo_list_iter_len(spalten);
    if (ncols < 1) ncols = 1;

    GType* types = (GType*)g_malloc0(sizeof(GType) * ncols);
    for (int32_t i = 0; i < ncols; i++) types[i] = G_TYPE_STRING;
    GtkListStore* store = gtk_list_store_newv(ncols, types);
    g_free(types);

    GtkWidget* tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);  /* TreeView haelt eigene Ref */

    for (int32_t i = 0; i < ncols; i++) {
        const char* title = "";
        if (spalten.tag == MOO_LIST) {
            MooValue c = moo_list_iter_get(spalten, i);
            if (c.tag == MOO_STRING) title = MV_STR(c)->chars;
        }
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            title, r, "text", i, NULL);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);
    }

    GtkWidget* sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), tv);

    g_object_set_data(G_OBJECT(sw), "moo-tv", tv);
    g_object_set_data(G_OBJECT(sw), "moo-store", store);
    g_object_set_data(G_OBJECT(sw), "moo-ncols", GINT_TO_POINTER(ncols));

    place_child(fx, sw, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    gtk_widget_show_all(sw);
    return wrap_widget(sw);
}

static GtkListStore* liste_store(MooValue liste, int* out_ncols) {
    GtkWidget* sw = unwrap_widget(liste);
    if (!sw) return NULL;
    GtkListStore* s = (GtkListStore*)g_object_get_data(G_OBJECT(sw), "moo-store");
    if (out_ncols) {
        *out_ncols = GPOINTER_TO_INT(
            g_object_get_data(G_OBJECT(sw), "moo-ncols"));
    }
    return s;
}

MooValue moo_ui_liste_zeile_hinzu(MooValue liste, MooValue zeile) {
    int ncols = 0;
    GtkListStore* st = liste_store(liste, &ncols);
    if (!st) return moo_bool(0);
    GtkTreeIter it;
    gtk_list_store_append(st, &it);
    int32_t n = (zeile.tag == MOO_LIST) ? moo_list_iter_len(zeile) : 0;
    for (int i = 0; i < ncols; i++) {
        const char* s = "";
        if (i < n) {
            MooValue c = moo_list_iter_get(zeile, i);
            if (c.tag == MOO_STRING) s = MV_STR(c)->chars;
        }
        gtk_list_store_set(st, &it, i, s, -1);
    }
    return moo_bool(1);
}

MooValue moo_ui_liste_auswahl(MooValue liste) {
    GtkWidget* sw = unwrap_widget(liste);
    if (!sw) return moo_number(-1);
    GtkWidget* tv = (GtkWidget*)g_object_get_data(G_OBJECT(sw), "moo-tv");
    if (!tv || !GTK_IS_TREE_VIEW(tv)) return moo_number(-1);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
    GtkTreeModel* model = NULL;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it)) return moo_number(-1);
    GtkTreePath* path = gtk_tree_model_get_path(model, &it);
    gint* idx = gtk_tree_path_get_indices(path);
    double i = (idx) ? (double)idx[0] : -1.0;
    gtk_tree_path_free(path);
    return moo_number(i);
}

MooValue moo_ui_liste_zeile(MooValue liste, MooValue index) {
    int ncols = 0;
    GtkListStore* st = liste_store(liste, &ncols);
    if (!st) return moo_list_new(0);
    int idx = num_or(index, -1);
    if (idx < 0) return moo_list_new(0);
    GtkTreeIter it;
    GtkTreePath* p = gtk_tree_path_new_from_indices(idx, -1);
    gboolean ok = gtk_tree_model_get_iter(GTK_TREE_MODEL(st), &it, p);
    gtk_tree_path_free(p);
    if (!ok) return moo_list_new(0);
    MooValue out = moo_list_new(ncols);
    for (int i = 0; i < ncols; i++) {
        gchar* v = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(st), &it, i, &v, -1);
        moo_list_append(out, moo_string_new(v ? v : ""));
        g_free(v);
    }
    return out;
}

MooValue moo_ui_liste_leeren(MooValue liste) {
    int ncols = 0;
    GtkListStore* st = liste_store(liste, &ncols);
    if (!st) return moo_bool(0);
    gtk_list_store_clear(st);
    return moo_bool(1);
}

static void on_tree_selection_trampoline(GtkTreeSelection* sel, gpointer ud) {
    (void)sel;
    MooValue* cb = (MooValue*)ud;
    if (cb && cb->tag == MOO_FUNC) moo_func_call_0(*cb);
}

MooValue moo_ui_liste_on_auswahl(MooValue liste, MooValue callback) {
    GtkWidget* sw = unwrap_widget(liste);
    if (!sw) return moo_bool(0);
    GtkWidget* tv = (GtkWidget*)g_object_get_data(G_OBJECT(sw), "moo-tv");
    if (!tv || !GTK_IS_TREE_VIEW(tv)) return moo_bool(0);
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
    MooValue* box = cb_box_new(callback);
    g_signal_connect_data(sel, "changed",
                          G_CALLBACK(on_tree_selection_trampoline),
                          box, cb_box_destroy, 0);
    return moo_bool(1);
}

static void on_slider_changed_trampoline(GtkRange* r, gpointer ud) {
    (void)r;
    MooValue* cb = (MooValue*)ud;
    if (cb && cb->tag == MOO_FUNC) moo_func_call_0(*cb);
}

MooValue moo_ui_slider(MooValue parent, MooValue min, MooValue max, MooValue start,
                       MooValue x, MooValue y, MooValue b, MooValue h,
                       MooValue callback) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    double mn = (min.tag == MOO_NUMBER) ? MV_NUM(min) : 0.0;
    double mx = (max.tag == MOO_NUMBER) ? MV_NUM(max) : 100.0;
    double st = (start.tag == MOO_NUMBER) ? MV_NUM(start) : mn;
    GtkWidget* sl = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, mn, mx, 1.0);
    gtk_range_set_value(GTK_RANGE(sl), st);
    place_child(fx, sl, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    if (callback.tag == MOO_FUNC) {
        MooValue* box = cb_box_new(callback);
        g_signal_connect_data(sl, "value-changed",
                              G_CALLBACK(on_slider_changed_trampoline),
                              box, cb_box_destroy, 0);
    }
    gtk_widget_show(sl);
    return wrap_widget(sl);
}

MooValue moo_ui_slider_wert(MooValue slider) {
    GtkWidget* s = unwrap_widget(slider);
    if (!s || !GTK_IS_RANGE(s)) return moo_number(0.0);
    return moo_number(gtk_range_get_value(GTK_RANGE(s)));
}

MooValue moo_ui_slider_setze(MooValue slider, MooValue wert) {
    GtkWidget* s = unwrap_widget(slider);
    if (!s || !GTK_IS_RANGE(s)) return moo_bool(0);
    gtk_range_set_value(GTK_RANGE(s),
                        (wert.tag == MOO_NUMBER) ? MV_NUM(wert) : 0.0);
    return moo_bool(1);
}

MooValue moo_ui_fortschritt(MooValue parent,
                            MooValue x, MooValue y, MooValue b, MooValue h) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* pb = gtk_progress_bar_new();
    place_child(fx, pb, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    gtk_widget_show(pb);
    return wrap_widget(pb);
}

MooValue moo_ui_fortschritt_setze(MooValue bar, MooValue wert) {
    GtkWidget* p = unwrap_widget(bar);
    if (!p || !GTK_IS_PROGRESS_BAR(p)) return moo_bool(0);
    double v = (wert.tag == MOO_NUMBER) ? MV_NUM(wert) : 0.0;
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(p), v);
    return moo_bool(1);
}

MooValue moo_ui_bild(MooValue parent, MooValue pfad,
                     MooValue x, MooValue y, MooValue b, MooValue h) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    const char* p = str_or(pfad, "");
    GtkWidget* img = (*p) ? gtk_image_new_from_file(p) : gtk_image_new();
    place_child(fx, img, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    gtk_widget_show(img);
    return wrap_widget(img);
}

MooValue moo_ui_bild_setze(MooValue bild, MooValue pfad) {
    GtkWidget* i = unwrap_widget(bild);
    if (!i || !GTK_IS_IMAGE(i)) return moo_bool(0);
    gtk_image_set_from_file(GTK_IMAGE(i), str_or(pfad, ""));
    return moo_bool(1);
}

/* ------------------------------------------------------------------ *
 * Leinwand / Zeichner (Phase 5)
 *
 * Der zeichner wrapped einen cairo_t* + aktuelle Farbe + valid-Flag.
 * Er wird auf dem Stack im draw-Handler angelegt, an den moo-Callback
 * als MooValue uebergeben (tag=MOO_NUMBER, data=&zeichner), und nach
 * Callback-Return durch `valid=0` entwertet. Spaetere Primitiv-Aufrufe
 * greifen dann ins Leere (no-op, liefern falsch).
 * ------------------------------------------------------------------ */

typedef struct {
    cairo_t* cr;            /* Cairo-Kontext (nur waehrend draw gueltig) */
    double   r, g, b, a;    /* aktuelle Farbe 0..1 */
    int      valid;         /* 1 = cr nutzbar, 0 = entwertet */
    int      widget_w, widget_h;
} MooZeichnerGtk;

static inline MooValue wrap_zeichner(MooZeichnerGtk* z) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, z);
    return v;
}

static inline MooZeichnerGtk* unwrap_zeichner(MooValue v) {
    if (v.tag != MOO_NUMBER) return NULL;
    MooZeichnerGtk* z = (MooZeichnerGtk*)moo_val_as_ptr(v);
    if (!z || !z->valid || !z->cr) return NULL;
    return z;
}

static gboolean on_draw_trampoline(GtkWidget* w, cairo_t* cr, gpointer ud) {
    MooValue* cb = (MooValue*)ud;
    if (cb && cb->tag == MOO_FUNC) {
        MooZeichnerGtk z;
        z.cr = cr;
        z.r = 0.0; z.g = 0.0; z.b = 0.0; z.a = 1.0;
        z.valid = 1;
        z.widget_w = gtk_widget_get_allocated_width(w);
        z.widget_h = gtk_widget_get_allocated_height(w);
        MooValue handle   = wrap_widget(w);
        MooValue zeichner = wrap_zeichner(&z);
        MooValue rv = moo_func_call_2(*cb, handle, zeichner);
        moo_release(rv);
        z.valid = 0;
        z.cr = NULL;
    }
    return FALSE;
}

MooValue moo_ui_zeichne_farbe(MooValue zeichner,
                              MooValue r, MooValue g, MooValue b, MooValue a) {
    MooZeichnerGtk* z = unwrap_zeichner(zeichner);
    if (!z) return moo_bool(0);
    int ri = num_or(r, 0), gi = num_or(g, 0), bi = num_or(b, 0), ai = num_or(a, 255);
    if (ri < 0) ri = 0; if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; if (bi > 255) bi = 255;
    if (ai < 0) ai = 0; if (ai > 255) ai = 255;
    z->r = ri / 255.0; z->g = gi / 255.0; z->b = bi / 255.0; z->a = ai / 255.0;
    return moo_bool(1);
}

static inline void apply_color(MooZeichnerGtk* z) {
    cairo_set_source_rgba(z->cr, z->r, z->g, z->b, z->a);
}

MooValue moo_ui_zeichne_linie(MooValue zeichner,
                              MooValue x1, MooValue y1,
                              MooValue x2, MooValue y2,
                              MooValue breite) {
    MooZeichnerGtk* z = unwrap_zeichner(zeichner);
    if (!z) return moo_bool(0);
    double bw = (breite.tag == MOO_NUMBER) ? MV_NUM(breite) : 1.0;
    if (bw < 0.1) bw = 0.1;
    apply_color(z);
    cairo_set_line_width(z->cr, bw);
    cairo_move_to(z->cr, num_or(x1, 0) + 0.5, num_or(y1, 0) + 0.5);
    cairo_line_to(z->cr, num_or(x2, 0) + 0.5, num_or(y2, 0) + 0.5);
    cairo_stroke(z->cr);
    return moo_bool(1);
}

MooValue moo_ui_zeichne_rechteck(MooValue zeichner,
                                 MooValue x, MooValue y,
                                 MooValue b, MooValue h,
                                 MooValue gefuellt) {
    MooZeichnerGtk* z = unwrap_zeichner(zeichner);
    if (!z) return moo_bool(0);
    apply_color(z);
    cairo_rectangle(z->cr, num_or(x, 0), num_or(y, 0),
                    num_or(b, 0), num_or(h, 0));
    if (bool_or(gefuellt, 1)) {
        cairo_fill(z->cr);
    } else {
        cairo_set_line_width(z->cr, 1.0);
        cairo_stroke(z->cr);
    }
    return moo_bool(1);
}

MooValue moo_ui_zeichne_kreis(MooValue zeichner,
                              MooValue cx, MooValue cy,
                              MooValue radius, MooValue gefuellt) {
    MooZeichnerGtk* z = unwrap_zeichner(zeichner);
    if (!z) return moo_bool(0);
    double rad = (radius.tag == MOO_NUMBER) ? MV_NUM(radius) : 1.0;
    if (rad < 0) rad = 0;
    apply_color(z);
    cairo_new_sub_path(z->cr);
    cairo_arc(z->cr, num_or(cx, 0), num_or(cy, 0), rad,
              0.0, 2.0 * 3.14159265358979323846);
    if (bool_or(gefuellt, 1)) {
        cairo_fill(z->cr);
    } else {
        cairo_set_line_width(z->cr, 1.0);
        cairo_stroke(z->cr);
    }
    return moo_bool(1);
}

MooValue moo_ui_zeichne_text(MooValue zeichner,
                             MooValue x, MooValue y,
                             MooValue text, MooValue schriftgroesse) {
    MooZeichnerGtk* z = unwrap_zeichner(zeichner);
    if (!z) return moo_bool(0);
    const char* s = str_or(text, "");
    double sz = (schriftgroesse.tag == MOO_NUMBER) ? MV_NUM(schriftgroesse) : 12.0;
    if (sz < 1.0) sz = 1.0;
    apply_color(z);
    cairo_select_font_face(z->cr, "Sans",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(z->cr, sz);
    cairo_font_extents_t fe;
    cairo_font_extents(z->cr, &fe);
    /* (x,y) ist Top-Left → baseline = y + Ascent. */
    cairo_move_to(z->cr, num_or(x, 0), num_or(y, 0) + fe.ascent);
    cairo_show_text(z->cr, s);
    return moo_bool(1);
}

MooValue moo_ui_zeichne_bild(MooValue zeichner,
                             MooValue x, MooValue y,
                             MooValue b, MooValue h,
                             MooValue pfad) {
    MooZeichnerGtk* z = unwrap_zeichner(zeichner);
    if (!z) return moo_bool(0);
    const char* p = str_or(pfad, "");
    if (!*p) return moo_bool(0);
    cairo_surface_t* surf = cairo_image_surface_create_from_png(p);
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        if (surf) cairo_surface_destroy(surf);
        return moo_bool(0);
    }
    int iw = cairo_image_surface_get_width(surf);
    int ih = cairo_image_surface_get_height(surf);
    int tw = num_or(b, iw);
    int th = num_or(h, ih);
    cairo_save(z->cr);
    if (iw > 0 && ih > 0 && (tw != iw || th != ih)) {
        cairo_translate(z->cr, num_or(x, 0), num_or(y, 0));
        cairo_scale(z->cr, (double)tw / (double)iw, (double)th / (double)ih);
        cairo_set_source_surface(z->cr, surf, 0, 0);
    } else {
        cairo_set_source_surface(z->cr, surf, num_or(x, 0), num_or(y, 0));
    }
    cairo_paint(z->cr);
    cairo_restore(z->cr);
    cairo_surface_destroy(surf);
    return moo_bool(1);
}

MooValue moo_ui_leinwand(MooValue parent,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue on_zeichne) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* da = gtk_drawing_area_new();
    place_child(fx, da, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    if (on_zeichne.tag == MOO_FUNC) {
        MooValue* box = cb_box_new(on_zeichne);
        g_signal_connect_data(da, "draw",
                              G_CALLBACK(on_draw_trampoline),
                              box, cb_box_destroy, 0);
    }
    gtk_widget_show(da);
    return wrap_widget(da);
}

MooValue moo_ui_leinwand_anfordern(MooValue leinwand) {
    GtkWidget* w = unwrap_widget(leinwand);
    if (!w) return moo_bool(0);
    gtk_widget_queue_draw(w);
    return moo_bool(1);
}

/* ================================================================== *
 * Schritt 4: Rahmen, Trenner, Tabs, Scroll
 * ================================================================== */

MooValue moo_ui_rahmen(MooValue parent, MooValue titel,
                       MooValue x, MooValue y, MooValue b, MooValue h) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* fr = gtk_frame_new(str_or(titel, NULL));
    GtkWidget* inner = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(fr), inner);
    g_object_set_data(G_OBJECT(fr), "moo-fixed", inner);
    place_child(fx, fr, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    gtk_widget_show_all(fr);
    return wrap_widget(fr);
}

MooValue moo_ui_trenner(MooValue parent,
                        MooValue x, MooValue y, MooValue b, MooValue h) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    int bb = num_or(b, 1), hh = num_or(h, 1);
    /* Breiter als hoch → horizontal, sonst vertikal. */
    GtkOrientation o = (bb >= hh) ? GTK_ORIENTATION_HORIZONTAL
                                  : GTK_ORIENTATION_VERTICAL;
    GtkWidget* s = gtk_separator_new(o);
    place_child(fx, s, num_or(x, 0), num_or(y, 0), bb, hh);
    gtk_widget_show(s);
    return wrap_widget(s);
}

MooValue moo_ui_tabs(MooValue parent,
                     MooValue x, MooValue y, MooValue b, MooValue h) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* nb = gtk_notebook_new();
    place_child(fx, nb, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    gtk_widget_show(nb);
    return wrap_widget(nb);
}

/* tab_hinzu: legt eine neue Seite als GtkFixed an und gibt diese zurueck,
 * damit sie direkt als `parent` fuer Kind-Widgets benutzt werden kann. */
MooValue moo_ui_tab_hinzu(MooValue tabs, MooValue titel) {
    GtkWidget* nb = unwrap_widget(tabs);
    if (!nb || !GTK_IS_NOTEBOOK(nb)) return moo_bool(0);
    GtkWidget* page = gtk_fixed_new();
    GtkWidget* lbl = gtk_label_new(str_or(titel, ""));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), page, lbl);
    gtk_widget_show_all(page);
    /* Eine tab-page IST ein Fixed → keine "moo-fixed"-Indirektion noetig,
     * aber fuer Konsistenz setzen wir sie dennoch. */
    g_object_set_data(G_OBJECT(page), "moo-fixed", page);
    return wrap_widget(page);
}

MooValue moo_ui_tabs_auswahl(MooValue tabs) {
    GtkWidget* nb = unwrap_widget(tabs);
    if (!nb || !GTK_IS_NOTEBOOK(nb)) return moo_number(-1);
    return moo_number((double)gtk_notebook_get_current_page(GTK_NOTEBOOK(nb)));
}

MooValue moo_ui_tabs_auswahl_setze(MooValue tabs, MooValue index) {
    GtkWidget* nb = unwrap_widget(tabs);
    if (!nb || !GTK_IS_NOTEBOOK(nb)) return moo_bool(0);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), num_or(index, 0));
    return moo_bool(1);
}

MooValue moo_ui_scroll(MooValue parent,
                       MooValue x, MooValue y, MooValue b, MooValue h) {
    GtkWidget* fx = resolve_container(parent);
    if (!fx) return moo_bool(0);
    GtkWidget* sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkWidget* inner = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(sw), inner);
    g_object_set_data(G_OBJECT(sw), "moo-fixed", inner);
    place_child(fx, sw, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
    gtk_widget_show_all(sw);
    return wrap_widget(sw);
}

/* ================================================================== *
 * Schritt 5: Allgemeine Widget-Ops + Timer
 * ================================================================== */

MooValue moo_ui_sichtbar(MooValue widget, MooValue sichtbar) {
    GtkWidget* w = unwrap_widget(widget);
    if (!w) return moo_bool(0);
    if (bool_or(sichtbar, 1)) gtk_widget_show(w);
    else gtk_widget_hide(w);
    return moo_bool(1);
}

MooValue moo_ui_aktiv(MooValue widget, MooValue aktiv) {
    GtkWidget* w = unwrap_widget(widget);
    if (!w) return moo_bool(0);
    gtk_widget_set_sensitive(w, bool_or(aktiv, 1));
    return moo_bool(1);
}

MooValue moo_ui_position_setze(MooValue widget, MooValue x, MooValue y) {
    GtkWidget* w = unwrap_widget(widget);
    if (!w) return moo_bool(0);
    if (GTK_IS_WINDOW(w)) {
        gtk_window_move(GTK_WINDOW(w), num_or(x, 0), num_or(y, 0));
        return moo_bool(1);
    }
    GtkWidget* par = gtk_widget_get_parent(w);
    if (par && GTK_IS_FIXED(par)) {
        gtk_fixed_move(GTK_FIXED(par), w, num_or(x, 0), num_or(y, 0));
        return moo_bool(1);
    }
    return moo_bool(0);
}

MooValue moo_ui_groesse_setze(MooValue widget, MooValue b, MooValue h) {
    GtkWidget* w = unwrap_widget(widget);
    if (!w) return moo_bool(0);
    if (GTK_IS_WINDOW(w)) {
        gtk_window_resize(GTK_WINDOW(w), num_or(b, 100), num_or(h, 100));
    } else {
        gtk_widget_set_size_request(w, num_or(b, -1), num_or(h, -1));
    }
    return moo_bool(1);
}

MooValue moo_ui_farbe_setze(MooValue widget, MooValue hex) {
    GtkWidget* w = unwrap_widget(widget);
    if (!w) return moo_bool(0);
    const char* c = str_or(hex, "#000000");
    GtkCssProvider* prov = gtk_css_provider_new();
    char css[128];
    g_snprintf(css, sizeof(css), "* { color: %s; }", c);
    gtk_css_provider_load_from_data(prov, css, -1, NULL);
    GtkStyleContext* ctx = gtk_widget_get_style_context(w);
    gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(prov),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(prov);
    return moo_bool(1);
}

MooValue moo_ui_schrift_setze(MooValue widget, MooValue groesse, MooValue fett) {
    GtkWidget* w = unwrap_widget(widget);
    if (!w) return moo_bool(0);
    int sz = num_or(groesse, 10);
    gboolean bold = bool_or(fett, 0);
    GtkCssProvider* prov = gtk_css_provider_new();
    char css[160];
    g_snprintf(css, sizeof(css),
               "* { font-size: %dpt; font-weight: %s; }",
               sz, bold ? "bold" : "normal");
    gtk_css_provider_load_from_data(prov, css, -1, NULL);
    GtkStyleContext* ctx = gtk_widget_get_style_context(w);
    gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(prov),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(prov);
    return moo_bool(1);
}

MooValue moo_ui_tooltip_setze(MooValue widget, MooValue text) {
    GtkWidget* w = unwrap_widget(widget);
    if (!w) return moo_bool(0);
    gtk_widget_set_tooltip_text(w, str_or(text, NULL));
    return moo_bool(1);
}

MooValue moo_ui_zerstoere(MooValue widget) {
    GtkWidget* w = unwrap_widget(widget);
    if (!w) return moo_bool(0);
    gtk_widget_destroy(w);
    return moo_bool(1);
}

/* Timer analog moo_tray.c: g_timeout_add_full mit Destroy-Notify. */
static gboolean ui_timer_tick(gpointer ud) {
    MooValue* cb = (MooValue*)ud;
    if (cb && cb->tag == MOO_FUNC) moo_func_call_0(*cb);
    return G_SOURCE_CONTINUE;
}

static void ui_timer_destroy(gpointer ud) {
    MooValue* cb = (MooValue*)ud;
    if (!cb) return;
    moo_release(*cb);
    free(cb);
}

MooValue moo_ui_timer_hinzu(MooValue ms, MooValue callback) {
    ensure_gtk();
    int ival = num_or(ms, 1000);
    MooValue* box = cb_box_new(callback);
    guint id = g_timeout_add_full(G_PRIORITY_DEFAULT, ival,
                                  ui_timer_tick, box, ui_timer_destroy);
    return moo_number((double)id);
}

MooValue moo_ui_timer_entfernen(MooValue timer_id) {
    guint id = (guint)num_or(timer_id, 0);
    if (id == 0) return moo_bool(0);
    return moo_bool(g_source_remove(id));
}

/* ================================================================== *
 * Schritt 6: Dialoge + Menueleiste
 * ================================================================== */

static GtkWindow* resolve_dialog_parent(MooValue parent) {
    GtkWidget* w = unwrap_widget(parent);
    if (!w) return NULL;
    if (GTK_IS_WINDOW(w)) return GTK_WINDOW(w);
    GtkWidget* top = gtk_widget_get_toplevel(w);
    return (top && GTK_IS_WINDOW(top)) ? GTK_WINDOW(top) : NULL;
}

static void run_message_dialog(GtkWindow* par, GtkMessageType type,
                               const char* title, const char* text) {
    GtkWidget* d = gtk_message_dialog_new(par,
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          type, GTK_BUTTONS_OK, "%s", text);
    if (title) gtk_window_set_title(GTK_WINDOW(d), title);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

MooValue moo_ui_info(MooValue parent, MooValue titel, MooValue text) {
    ensure_gtk();
    run_message_dialog(resolve_dialog_parent(parent), GTK_MESSAGE_INFO,
                       str_or(titel, "Info"), str_or(text, ""));
    return moo_none();
}

MooValue moo_ui_warnung(MooValue parent, MooValue titel, MooValue text) {
    ensure_gtk();
    run_message_dialog(resolve_dialog_parent(parent), GTK_MESSAGE_WARNING,
                       str_or(titel, "Warnung"), str_or(text, ""));
    return moo_none();
}

MooValue moo_ui_fehler(MooValue parent, MooValue titel, MooValue text) {
    ensure_gtk();
    run_message_dialog(resolve_dialog_parent(parent), GTK_MESSAGE_ERROR,
                       str_or(titel, "Fehler"), str_or(text, ""));
    return moo_none();
}

MooValue moo_ui_frage(MooValue parent, MooValue titel, MooValue text) {
    ensure_gtk();
    GtkWidget* d = gtk_message_dialog_new(resolve_dialog_parent(parent),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_YES_NO, "%s",
                                          str_or(text, ""));
    gtk_window_set_title(GTK_WINDOW(d), str_or(titel, "Frage"));
    gint rv = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return moo_bool(rv == GTK_RESPONSE_YES);
}

MooValue moo_ui_eingabe_dialog(MooValue parent, MooValue titel,
                               MooValue prompt, MooValue vorgabe) {
    ensure_gtk();
    GtkWidget* d = gtk_dialog_new_with_buttons(str_or(titel, "Eingabe"),
        resolve_dialog_parent(parent),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        "_OK", GTK_RESPONSE_OK, NULL);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 8);
    GtkWidget* lbl = gtk_label_new(str_or(prompt, ""));
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    GtkWidget* en = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(en), str_or(vorgabe, ""));
    gtk_entry_set_activates_default(GTK_ENTRY(en), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_box_pack_start(GTK_BOX(content), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), en, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    gint resp = gtk_dialog_run(GTK_DIALOG(d));
    MooValue rv;
    if (resp == GTK_RESPONSE_OK) {
        rv = moo_string_new(gtk_entry_get_text(GTK_ENTRY(en)));
    } else {
        rv = moo_none();
    }
    gtk_widget_destroy(d);
    return rv;
}

/* filter: MooList mit Paaren [[name, pattern], ...] oder MOO_NONE. */
static void apply_file_filters(GtkFileChooser* fc, MooValue filter) {
    if (filter.tag != MOO_LIST) return;
    int32_t n = moo_list_iter_len(filter);
    for (int32_t i = 0; i < n; i++) {
        MooValue pair = moo_list_iter_get(filter, i);
        if (pair.tag != MOO_LIST) continue;
        if (moo_list_iter_len(pair) < 2) continue;
        MooValue nm = moo_list_iter_get(pair, 0);
        MooValue pt = moo_list_iter_get(pair, 1);
        if (nm.tag != MOO_STRING || pt.tag != MOO_STRING) continue;
        GtkFileFilter* f = gtk_file_filter_new();
        gtk_file_filter_set_name(f, MV_STR(nm)->chars);
        /* pattern kann mehrere ;-getrennte Globs enthalten */
        const char* src = MV_STR(pt)->chars;
        char* copy = g_strdup(src);
        char* tok = strtok(copy, ";");
        while (tok) {
            gtk_file_filter_add_pattern(f, tok);
            tok = strtok(NULL, ";");
        }
        g_free(copy);
        gtk_file_chooser_add_filter(fc, f);
    }
}

static MooValue file_chooser_run(MooValue parent, MooValue titel,
                                 MooValue filter, GtkFileChooserAction action,
                                 const char* accept_label) {
    ensure_gtk();
    GtkWidget* d = gtk_file_chooser_dialog_new(str_or(titel, ""),
        resolve_dialog_parent(parent), action,
        "_Abbrechen", GTK_RESPONSE_CANCEL,
        accept_label, GTK_RESPONSE_ACCEPT, NULL);
    apply_file_filters(GTK_FILE_CHOOSER(d), filter);
    MooValue rv = moo_none();
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        if (path) {
            rv = moo_string_new(path);
            g_free(path);
        }
    }
    gtk_widget_destroy(d);
    return rv;
}

MooValue moo_ui_datei_oeffnen(MooValue parent, MooValue titel, MooValue filter) {
    return file_chooser_run(parent, titel, filter,
                            GTK_FILE_CHOOSER_ACTION_OPEN, "_Oeffnen");
}

MooValue moo_ui_datei_speichern(MooValue parent, MooValue titel, MooValue filter) {
    return file_chooser_run(parent, titel, filter,
                            GTK_FILE_CHOOSER_ACTION_SAVE, "_Speichern");
}

MooValue moo_ui_ordner_waehlen(MooValue parent, MooValue titel) {
    return file_chooser_run(parent, titel, moo_none(),
                            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Waehlen");
}

/* --- Menueleiste ---
 * Menueleiste wird als erstes Kind in die vbox des Fensters gepackt. */
MooValue moo_ui_menueleiste(MooValue fenster) {
    GtkWidget* win = unwrap_widget(fenster);
    if (!win || !GTK_IS_WINDOW(win)) return moo_bool(0);
    GtkWidget* vbox = (GtkWidget*)g_object_get_data(G_OBJECT(win), "moo-vbox");
    if (!vbox) return moo_bool(0);
    GtkWidget* mb = gtk_menu_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), mb, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(vbox), mb, 0);
    g_object_set_data(G_OBJECT(win), "moo-menubar", mb);
    gtk_widget_show(mb);
    return wrap_widget(mb);
}

/* Liefert den zugehoerigen GtkMenu (Dropdown) eines Menue-Items. */
MooValue moo_ui_menue(MooValue leiste, MooValue titel) {
    GtkWidget* mb = unwrap_widget(leiste);
    if (!mb || !GTK_IS_MENU_SHELL(mb)) return moo_bool(0);
    GtkWidget* item = gtk_menu_item_new_with_label(str_or(titel, ""));
    GtkWidget* sub = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(mb), item);
    gtk_widget_show(item);
    return wrap_widget(sub);
}

static void on_menu_item_trampoline(GtkMenuItem* it, gpointer ud) {
    (void)it;
    MooValue* cb = (MooValue*)ud;
    if (cb && cb->tag == MOO_FUNC) moo_func_call_0(*cb);
}

MooValue moo_ui_menue_eintrag(MooValue menue, MooValue titel, MooValue callback) {
    GtkWidget* m = unwrap_widget(menue);
    if (!m || !GTK_IS_MENU_SHELL(m)) return moo_bool(0);
    GtkWidget* item = gtk_menu_item_new_with_label(str_or(titel, ""));
    if (callback.tag == MOO_FUNC) {
        MooValue* box = cb_box_new(callback);
        g_signal_connect_data(item, "activate",
                              G_CALLBACK(on_menu_item_trampoline),
                              box, cb_box_destroy, 0);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(m), item);
    gtk_widget_show(item);
    return wrap_widget(item);
}

MooValue moo_ui_menue_trenner(MooValue menue) {
    GtkWidget* m = unwrap_widget(menue);
    if (!m || !GTK_IS_MENU_SHELL(m)) return moo_bool(0);
    GtkWidget* sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(m), sep);
    gtk_widget_show(sep);
    return wrap_widget(sep);
}

/* Untermenue: fuegt in `menue` einen Eintrag mit Submenu ein; gibt den
 * Submenu-GtkMenu zurueck (damit Caller weitere Eintraege anfuegen kann). */
MooValue moo_ui_menue_untermenue(MooValue menue, MooValue titel) {
    GtkWidget* m = unwrap_widget(menue);
    if (!m || !GTK_IS_MENU_SHELL(m)) return moo_bool(0);
    GtkWidget* item = gtk_menu_item_new_with_label(str_or(titel, ""));
    GtkWidget* sub = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), item);
    gtk_widget_show(item);
    return wrap_widget(sub);
}

