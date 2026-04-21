/*
 * moo_tray_linux.c — Linux-Backend fuer die OS-neutrale Tray-API.
 *
 * Basis: libappindicator3 + GTK3 + libdbusmenu-glib.
 * Header: moo_tray.h (OS-neutral, plan-002-moo-ui-cross-platform).
 *
 * Ownership-Pattern (unveraendert aus alter moo_tray.c):
 *   Jedes Menue-Item und jeder Timer-Tick besitzt ein Heap-allokiertes
 *   MooValue (cb_copy). GClosureNotify / GDestroyNotify feuert, wenn die
 *   Signal-Verbindung bzw. die GSource zerstoert wird — dort geben wir
 *   die moo-Referenz frei und freen das Heap-Objekt. Damit kann
 *   menu_clear einfach das gesamte alte Menue zerstoeren, alle Items
 *   feuern ihre Notify-Callbacks, und nichts leakt.
 *
 * Handle-Layout:
 *   Tray      — AppIndicator*    (tag=MOO_NUMBER, ptr in data)
 *   Submenu   — GtkMenu*         (tag=MOO_NUMBER, ptr in data)
 *   Menu-Item — GtkWidget*       (tag=MOO_NUMBER, ptr in data)
 *
 * Die Unterscheidung Tray/Submenu im Kontext von "menu_add", "separator_add"
 * etc. wird vom ABI durch separate Funktionen (moo_tray_menu_add vs
 * moo_tray_menu_add_to) geleistet — intern geht beides in dieselbe
 * interne Helper-Funktion mit einem GtkMenu*.
 */

#include "moo_runtime.h"
#include "moo_tray.h"

#include <libappindicator/app-indicator.h>
#include <gtk/gtk.h>
#include <stdlib.h>

static int gtk_initialized = 0;

static inline void ensure_gtk(void) {
    if (!gtk_initialized) {
        gtk_init(NULL, NULL);
        gtk_initialized = 1;
    }
}

/* ---------------------------------------------------------------------------
 * Callback-Ownership
 * ------------------------------------------------------------------------- */

/* GClosureNotify: feuert beim Zerstoeren der Signal-Closure eines Items. */
static void cb_box_destroy(gpointer data, GClosure* closure) {
    (void)closure;
    MooValue* cb = (MooValue*)data;
    if (!cb) return;
    moo_release(*cb);
    free(cb);
}

static void on_menu_item_activated(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    MooValue* cb = (MooValue*)user_data;
    if (cb && cb->tag == MOO_FUNC) {
        moo_func_call_0(*cb);
    }
}

static void on_check_item_toggled(GtkCheckMenuItem* item, gpointer user_data) {
    (void)item;
    MooValue* cb = (MooValue*)user_data;
    if (cb && cb->tag == MOO_FUNC) {
        /* Der User ruft selbst moo_tray_check_wert(item) im Callback ab,
         * falls er den aktuellen Zustand braucht. call_0 ist konsistent
         * mit moo_tray_menu_add. */
        moo_func_call_0(*cb);
    }
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static MooValue ptr_to_moo(void* p) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, p);
    return v;
}

static GtkMenu* tray_main_menu(MooValue tray) {
    AppIndicator* ind = (AppIndicator*)moo_val_as_ptr(tray);
    if (!ind) return NULL;
    return app_indicator_get_menu(ind);
}

/* Heap-Kopie eines MooValue-Callbacks (retain-t die Referenz). */
static MooValue* cb_box_new(MooValue callback) {
    MooValue* cb_copy = (MooValue*)malloc(sizeof(MooValue));
    *cb_copy = callback;
    moo_retain(callback);
    return cb_copy;
}

/* Haengt ein Label-Item an ein beliebiges GtkMenu. */
static GtkWidget* menu_append_label_item(GtkMenu* menu, const char* label,
                                         MooValue callback) {
    GtkWidget* item = gtk_menu_item_new_with_label(label);
    MooValue* cb_copy = cb_box_new(callback);
    g_signal_connect_data(item, "activate",
                          G_CALLBACK(on_menu_item_activated),
                          cb_copy, cb_box_destroy, 0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    return item;
}

/* Haengt einen Separator an ein beliebiges GtkMenu. */
static GtkWidget* menu_append_separator(GtkMenu* menu) {
    GtkWidget* sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
    gtk_widget_show(sep);
    return sep;
}

/* Haengt ein Check-Item an ein beliebiges GtkMenu. */
static GtkWidget* menu_append_check_item(GtkMenu* menu, const char* label,
                                         gboolean initial, MooValue callback) {
    GtkWidget* item = gtk_check_menu_item_new_with_label(label);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), initial);
    MooValue* cb_copy = cb_box_new(callback);
    g_signal_connect_data(item, "toggled",
                          G_CALLBACK(on_check_item_toggled),
                          cb_copy, cb_box_destroy, 0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    return item;
}

/* ===========================================================================
 * Tray-Icon
 * ========================================================================= */

MooValue moo_tray_create(MooValue titel, MooValue icon_name) {
    ensure_gtk();
    const char* t = (titel.tag == MOO_STRING) ? MV_STR(titel)->chars : "Tray";
    const char* i = (icon_name.tag == MOO_STRING) ? MV_STR(icon_name)->chars
                                                  : "application-x-executable";

    AppIndicator* ind = app_indicator_new(t, i, APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(ind, APP_INDICATOR_STATUS_ACTIVE);

    GtkWidget* menu = gtk_menu_new();
    /* Der AppIndicator nimmt keine volle Referenz auf das Menue. Wir sinken
     * die floating-Referenz selbst, damit der Widget-Tree stabil bleibt, bis
     * wir ihn via menu_clear explizit zerstoeren. */
    g_object_ref_sink(menu);
    app_indicator_set_menu(ind, GTK_MENU(menu));

    return ptr_to_moo(ind);
}

MooValue moo_tray_titel_setze(MooValue tray, MooValue titel) {
    AppIndicator* ind = (AppIndicator*)moo_val_as_ptr(tray);
    if (!ind) return moo_bool(0);
    const char* t = (titel.tag == MOO_STRING) ? MV_STR(titel)->chars : "";
    app_indicator_set_title(ind, t);
    return moo_bool(1);
}

MooValue moo_tray_icon_setze(MooValue tray, MooValue icon_name) {
    AppIndicator* ind = (AppIndicator*)moo_val_as_ptr(tray);
    if (!ind) return moo_bool(0);
    const char* i = (icon_name.tag == MOO_STRING) ? MV_STR(icon_name)->chars
                                                  : "application-x-executable";
    app_indicator_set_icon_full(ind, i, i);
    return moo_bool(1);
}

MooValue moo_tray_aktiv(MooValue tray, MooValue aktiv) {
    AppIndicator* ind = (AppIndicator*)moo_val_as_ptr(tray);
    if (!ind) return moo_bool(0);
    int an = (aktiv.tag == MOO_BOOL) ? (int)MV_BOOL(aktiv) : 1;
    app_indicator_set_status(ind, an ? APP_INDICATOR_STATUS_ACTIVE
                                     : APP_INDICATOR_STATUS_PASSIVE);
    return moo_bool(1);
}

/* ===========================================================================
 * Menu (flach) — Top-Level
 * ========================================================================= */

MooValue moo_tray_menu_add(MooValue tray, MooValue label, MooValue callback) {
    GtkMenu* menu = tray_main_menu(tray);
    if (!menu) return moo_bool(0);
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    GtkWidget* item = menu_append_label_item(menu, l, callback);
    return ptr_to_moo(item);
}

MooValue moo_tray_separator_add(MooValue tray) {
    GtkMenu* menu = tray_main_menu(tray);
    if (!menu) return moo_bool(0);
    GtkWidget* sep = menu_append_separator(menu);
    return ptr_to_moo(sep);
}

MooValue moo_tray_menu_clear(MooValue tray) {
    AppIndicator* ind = (AppIndicator*)moo_val_as_ptr(tray);
    if (!ind) return moo_bool(0);

    GtkMenu* alt = app_indicator_get_menu(ind);

    GtkWidget* neu = gtk_menu_new();
    g_object_ref_sink(neu);
    app_indicator_set_menu(ind, GTK_MENU(neu));
    gtk_widget_show(neu);

    /* Alte Menue-Items explizit zerstoeren. Das triggert cb_box_destroy pro
     * Item, die moo-Callback-Referenzen werden released und cb_copy-Speicher
     * freigegeben. Erst dann geben wir auch das alte Menue-Widget selbst frei.
     * gtk_container_foreach rekursiert auch in Submenues: wenn ein Item ein
     * Submenu ansetzt, wird das Submenu beim destroy des Parent-Items via
     * gtk_widget_destroy ebenfalls freigegeben — GtkMenuItem ref-t sein
     * Submenu, und destroy cleart diese Ref. */
    if (alt) {
        GtkWidget* alt_w = GTK_WIDGET(alt);
        gtk_container_foreach(GTK_CONTAINER(alt_w),
                              (GtkCallback)gtk_widget_destroy, NULL);
        /* g_object_unref ruft intern dispose → destroy, gibt das Widget frei.
         * Ein zusaetzliches gtk_widget_destroy vor dem unref wuerde den
         * Refcount auf 0 senken → use-after-free beim unref. */
        g_object_unref(alt_w);
    }
    return moo_bool(1);
}

/* ===========================================================================
 * Submenues
 * ========================================================================= */

MooValue moo_tray_submenu_add(MooValue tray, MooValue label) {
    GtkMenu* menu = tray_main_menu(tray);
    if (!menu) return moo_bool(0);
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";

    GtkWidget* item = gtk_menu_item_new_with_label(l);
    GtkWidget* submenu = gtk_menu_new();
    /* gtk_menu_item_set_submenu nimmt eine Ref auf das Submenu und sinkt die
     * floating-Ref. Wir muessen nichts extra tun — wenn das Parent-Item
     * zerstoert wird, wird auch das Submenu zerstoert, alle Child-Items
     * feuern ihre cb_box_destroy. */
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    gtk_widget_show(submenu);

    return ptr_to_moo(submenu);
}

MooValue moo_tray_menu_add_to(MooValue submenu, MooValue label, MooValue callback) {
    GtkMenu* menu = (GtkMenu*)moo_val_as_ptr(submenu);
    if (!menu) return moo_bool(0);
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    GtkWidget* item = menu_append_label_item(menu, l, callback);
    return ptr_to_moo(item);
}

MooValue moo_tray_separator_add_to(MooValue submenu) {
    GtkMenu* menu = (GtkMenu*)moo_val_as_ptr(submenu);
    if (!menu) return moo_bool(0);
    GtkWidget* sep = menu_append_separator(menu);
    return ptr_to_moo(sep);
}

/* ===========================================================================
 * Check-Items
 * ========================================================================= */

MooValue moo_tray_check_add(MooValue tray, MooValue label, MooValue initial,
                            MooValue callback) {
    GtkMenu* menu = tray_main_menu(tray);
    if (!menu) return moo_bool(0);
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    gboolean init = (initial.tag == MOO_BOOL) ? (gboolean)MV_BOOL(initial) : FALSE;
    GtkWidget* item = menu_append_check_item(menu, l, init, callback);
    return ptr_to_moo(item);
}

MooValue moo_tray_check_add_to(MooValue submenu, MooValue label,
                               MooValue initial, MooValue callback) {
    GtkMenu* menu = (GtkMenu*)moo_val_as_ptr(submenu);
    if (!menu) return moo_bool(0);
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    gboolean init = (initial.tag == MOO_BOOL) ? (gboolean)MV_BOOL(initial) : FALSE;
    GtkWidget* item = menu_append_check_item(menu, l, init, callback);
    return ptr_to_moo(item);
}

MooValue moo_tray_check_wert(MooValue check_item) {
    GtkWidget* w = (GtkWidget*)moo_val_as_ptr(check_item);
    if (!w || !GTK_IS_CHECK_MENU_ITEM(w)) return moo_bool(0);
    gboolean active = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w));
    return moo_bool(active ? 1 : 0);
}

MooValue moo_tray_check_set(MooValue check_item, MooValue wert) {
    GtkWidget* w = (GtkWidget*)moo_val_as_ptr(check_item);
    if (!w || !GTK_IS_CHECK_MENU_ITEM(w)) return moo_bool(0);
    gboolean an = (wert.tag == MOO_BOOL) ? (gboolean)MV_BOOL(wert) : FALSE;
    /* set_active feuert "toggled" nur, wenn sich der Zustand aendert. Das
     * ist das gewuenschte Verhalten — programmatische Setzer triggern den
     * User-Callback mit. */
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), an);
    return moo_bool(1);
}

/* ===========================================================================
 * Item-Manipulation
 * ========================================================================= */

MooValue moo_tray_item_aktiv(MooValue item, MooValue aktiv) {
    GtkWidget* w = (GtkWidget*)moo_val_as_ptr(item);
    if (!w || !GTK_IS_WIDGET(w)) return moo_bool(0);
    gboolean sensitive = (aktiv.tag == MOO_BOOL) ? (gboolean)MV_BOOL(aktiv) : TRUE;
    gtk_widget_set_sensitive(w, sensitive);
    return moo_bool(1);
}

MooValue moo_tray_item_label_setze(MooValue item, MooValue label) {
    GtkWidget* w = (GtkWidget*)moo_val_as_ptr(item);
    if (!w || !GTK_IS_MENU_ITEM(w)) return moo_bool(0);
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "";
    gtk_menu_item_set_label(GTK_MENU_ITEM(w), l);
    return moo_bool(1);
}

/* ===========================================================================
 * Timer
 * ========================================================================= */

static gboolean timer_tick_wrap(gpointer user_data) {
    MooValue* cb = (MooValue*)user_data;
    if (cb && cb->tag == MOO_FUNC) {
        moo_func_call_0(*cb);
    }
    return G_SOURCE_CONTINUE;
}

static void timer_destroy_wrap(gpointer user_data) {
    MooValue* cb = (MooValue*)user_data;
    if (!cb) return;
    moo_release(*cb);
    free(cb);
}

MooValue moo_tray_timer_add(MooValue interval_ms, MooValue callback) {
    ensure_gtk();
    int ms = (interval_ms.tag == MOO_NUMBER) ? (int)MV_NUM(interval_ms) : 1000;
    MooValue* cb_copy = cb_box_new(callback);
    /* g_timeout_add_full mit Destroy-Notify: wenn der Timer jemals entfernt
     * wird (Prozess-Ende oder g_source_remove), wird timer_destroy_wrap die
     * moo-Referenz sauber freigeben. */
    guint id = g_timeout_add_full(G_PRIORITY_DEFAULT, ms,
                                  timer_tick_wrap, cb_copy, timer_destroy_wrap);
    return moo_number((double)id);
}

MooValue moo_tray_timer_remove(MooValue timer_id) {
    if (timer_id.tag != MOO_NUMBER) return moo_bool(0);
    guint id = (guint)MV_NUM(timer_id);
    if (id == 0) return moo_bool(0);
    /* g_source_remove loest die destroy_notify aus → timer_destroy_wrap
     * released den moo-Callback. */
    gboolean ok = g_source_remove(id);
    return moo_bool(ok ? 1 : 0);
}

/* ===========================================================================
 * Event-Loop (Legacy)
 * ========================================================================= */

MooValue moo_tray_run(void) {
    ensure_gtk();
    gtk_main();
    return moo_none();
}
