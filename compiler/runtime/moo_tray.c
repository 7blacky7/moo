#include "moo_runtime.h"
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

// Callback-Ownership: jedes Menue-Item und jeder Timer-Tick besitzt ein
// Heap-allokiertes MooValue (cb_copy). GClosureNotify feuert, wenn die
// Signal-Verbindung bzw. die GSource zerstoert wird — dort geben wir die
// moo-Referenz frei und freen das Heap-Objekt. Damit kann menu_clear
// einfach das gesamte alte Menue zerstoeren, alle Items feuern ihre
// Notify-Callbacks, und nichts leakt.
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

MooValue moo_tray_create(MooValue titel, MooValue icon_name) {
    ensure_gtk();
    const char* t = (titel.tag == MOO_STRING) ? MV_STR(titel)->chars : "Tray";
    const char* i = (icon_name.tag == MOO_STRING) ? MV_STR(icon_name)->chars
                                                  : "application-x-executable";

    AppIndicator* ind = app_indicator_new(t, i, APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(ind, APP_INDICATOR_STATUS_ACTIVE);

    GtkWidget* menu = gtk_menu_new();
    // Der AppIndicator nimmt keine volle Referenz auf das Menue. Wir sinken
    // die floating-Referenz selbst, damit der Widget-Tree stabil bleibt, bis
    // wir ihn via menu_clear explizit zerstoeren.
    g_object_ref_sink(menu);
    app_indicator_set_menu(ind, GTK_MENU(menu));

    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, ind);
    return v;
}

MooValue moo_tray_menu_add(MooValue tray, MooValue label, MooValue callback) {
    AppIndicator* ind = (AppIndicator*)moo_val_as_ptr(tray);
    if (!ind) return moo_bool(0);
    GtkMenu* menu = app_indicator_get_menu(ind);
    if (!menu) return moo_bool(0);

    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    GtkWidget* item = gtk_menu_item_new_with_label(l);

    MooValue* cb_copy = (MooValue*)malloc(sizeof(MooValue));
    *cb_copy = callback;
    moo_retain(callback);

    // connect_data bindet cb_box_destroy an die Lebenszeit der Closure.
    // Wenn das Item zerstoert wird (z.B. durch menu_clear), feuert cb_box_destroy,
    // ruft moo_release und free(cb_copy) — kein Leak, keine Dangling-Pointer.
    g_signal_connect_data(item, "activate",
                          G_CALLBACK(on_menu_item_activated),
                          cb_copy, cb_box_destroy, 0);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    return moo_bool(1);
}

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
    MooValue* cb_copy = (MooValue*)malloc(sizeof(MooValue));
    *cb_copy = callback;
    moo_retain(callback);
    // g_timeout_add_full mit Destroy-Notify: wenn der Timer jemals entfernt wird
    // (Prozess-Ende oder g_source_remove), wird timer_destroy_wrap die moo-Referenz
    // sauber freigeben.
    guint id = g_timeout_add_full(G_PRIORITY_DEFAULT, ms,
                                  timer_tick_wrap, cb_copy, timer_destroy_wrap);
    return moo_number((double)id);
}

MooValue moo_tray_menu_clear(MooValue tray) {
    AppIndicator* ind = (AppIndicator*)moo_val_as_ptr(tray);
    if (!ind) return moo_bool(0);

    GtkMenu* alt = app_indicator_get_menu(ind);

    GtkWidget* neu = gtk_menu_new();
    g_object_ref_sink(neu);
    app_indicator_set_menu(ind, GTK_MENU(neu));
    gtk_widget_show(neu);

    // Alte Menue-Items explizit zerstoeren. Das triggert cb_box_destroy pro
    // Item, die moo-Callback-Referenzen werden released und cb_copy-Speicher
    // freigegeben. Erst dann geben wir auch das alte Menue-Widget selbst frei.
    if (alt) {
        GtkWidget* alt_w = GTK_WIDGET(alt);
        gtk_container_foreach(GTK_CONTAINER(alt_w),
                              (GtkCallback)gtk_widget_destroy, NULL);
        // g_object_unref ruft intern dispose → destroy, gibt das Widget frei.
        // Ein zusaetzliches gtk_widget_destroy vor dem unref wuerde den
        // Refcount auf 0 senken → use-after-free beim unref.
        g_object_unref(alt_w);
    }
    return moo_bool(1);
}

MooValue moo_tray_run(void) {
    ensure_gtk();
    gtk_main();
    return moo_none();
}
