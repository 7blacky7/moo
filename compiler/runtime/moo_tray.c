#include "moo_runtime.h"
#include <libappindicator/app-indicator.h>
#include <gtk/gtk.h>
#include <stdlib.h>

static int gtk_initialized = 0;

static void on_menu_item_activated(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    MooValue* cb = (MooValue*)user_data;
    if (cb && cb->tag == MOO_FUNC) {
        moo_func_call_0(*cb);
    }
}

MooValue moo_tray_create(MooValue titel, MooValue icon_name) {
    if (!gtk_initialized) {
        gtk_init(NULL, NULL);
        gtk_initialized = 1;
    }
    const char* t = (titel.tag == MOO_STRING) ? MV_STR(titel)->chars : "Tray";
    const char* i = (icon_name.tag == MOO_STRING) ? MV_STR(icon_name)->chars
                                                  : "application-x-executable";

    AppIndicator* ind = app_indicator_new(t, i, APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(ind, APP_INDICATOR_STATUS_ACTIVE);

    GtkWidget* menu = gtk_menu_new();
    app_indicator_set_menu(ind, GTK_MENU(menu));

    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, ind);
    return v;
}

MooValue moo_tray_menu_add(MooValue tray, MooValue label, MooValue callback) {
    AppIndicator* ind = (AppIndicator*)moo_val_as_ptr(tray);
    if (!ind) return moo_bool(0);
    GtkMenu* old = app_indicator_get_menu(ind);
    GtkWidget* menu = (GtkWidget*)old;
    if (!menu) { menu = gtk_menu_new(); }
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    GtkWidget* item = gtk_menu_item_new_with_label(l);

    MooValue* cb_copy = (MooValue*)malloc(sizeof(MooValue));
    *cb_copy = callback;
    moo_retain(callback);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_activated), cb_copy);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show_all(menu);
    app_indicator_set_menu(ind, GTK_MENU(menu));
    return moo_bool(1);
}

static int on_timer_tick(gpointer user_data) {
    MooValue* cb = (MooValue*)user_data;
    if (cb && cb->tag == MOO_FUNC) {
        moo_func_call_0(*cb);
    }
    return G_SOURCE_CONTINUE;
}

MooValue moo_tray_timer_add(MooValue interval_ms, MooValue callback) {
    if (!gtk_initialized) { gtk_init(NULL, NULL); gtk_initialized = 1; }
    int ms = (interval_ms.tag == MOO_NUMBER) ? (int)MV_NUM(interval_ms) : 1000;
    MooValue* cb_copy = (MooValue*)malloc(sizeof(MooValue));
    *cb_copy = callback;
    moo_retain(callback);
    guint id = g_timeout_add(ms, on_timer_tick, cb_copy);
    return moo_number((double)id);
}

MooValue moo_tray_menu_clear(MooValue tray) {
    AppIndicator* ind = (AppIndicator*)moo_val_as_ptr(tray);
    if (!ind) return moo_bool(0);
    GtkMenu* menu = app_indicator_get_menu(ind);
    if (!menu) return moo_bool(1);
    gtk_container_foreach(GTK_CONTAINER(menu), (GtkCallback)gtk_widget_destroy, NULL);
    gtk_widget_show_all(GTK_WIDGET(menu));
    return moo_bool(1);
}

MooValue moo_tray_run(void) {
    if (!gtk_initialized) { gtk_init(NULL, NULL); gtk_initialized = 1; }
    gtk_main();
    return moo_none();
}
