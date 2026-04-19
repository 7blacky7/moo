#include "moo_runtime.h"
#include <gtk/gtk.h>
#include <stdlib.h>

static int gui_gtk_init = 0;
static void ensure_gtk(void) { if (!gui_gtk_init) { gtk_init(NULL, NULL); gui_gtk_init = 1; } }

MooValue moo_gui_fenster(MooValue titel, MooValue breite, MooValue hoehe) {
    ensure_gtk();
    const char* t = (titel.tag == MOO_STRING) ? MV_STR(titel)->chars : "moo";
    int w = (breite.tag == MOO_NUMBER) ? (int)MV_NUM(breite) : 400;
    int h = (hoehe.tag == MOO_NUMBER) ? (int)MV_NUM(hoehe) : 300;
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), t);
    gtk_window_set_default_size(GTK_WINDOW(win), w, h);
    gtk_window_set_icon_from_file(GTK_WINDOW(win), "/tmp/moo-icon.png", NULL);
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(win), 12);
    gtk_container_add(GTK_CONTAINER(win), vbox);
    g_object_set_data(G_OBJECT(win), "moo-vbox", vbox);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    MooValue v; v.tag = MOO_NUMBER; moo_val_set_ptr(&v, win); return v;
}

MooValue moo_gui_label(MooValue fenster, MooValue text) {
    GtkWidget* win = (GtkWidget*)moo_val_as_ptr(fenster);
    GtkWidget* vbox = (GtkWidget*)g_object_get_data(G_OBJECT(win), "moo-vbox");
    const char* s = (text.tag == MOO_STRING) ? MV_STR(text)->chars : "";
    GtkWidget* lbl = gtk_label_new(s);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
    MooValue v; v.tag = MOO_NUMBER; moo_val_set_ptr(&v, lbl); return v;
}

static void on_button_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    MooValue* cb = (MooValue*)user_data;
    if (cb && cb->tag == MOO_FUNC) {
        moo_func_call_0(*cb);
    }
}

MooValue moo_gui_button(MooValue fenster, MooValue label, MooValue callback) {
    GtkWidget* win = (GtkWidget*)moo_val_as_ptr(fenster);
    GtkWidget* vbox = (GtkWidget*)g_object_get_data(G_OBJECT(win), "moo-vbox");
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "OK";
    GtkWidget* btn = gtk_button_new_with_label(l);
    MooValue* cb = (MooValue*)malloc(sizeof(MooValue));
    *cb = callback;
    moo_retain(callback);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_button_clicked), cb);
    gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
    MooValue v; v.tag = MOO_NUMBER; moo_val_set_ptr(&v, btn); return v;
}

MooValue moo_gui_label_setze(MooValue label, MooValue text) {
    GtkWidget* lbl = (GtkWidget*)moo_val_as_ptr(label);
    const char* s = (text.tag == MOO_STRING) ? MV_STR(text)->chars : "";
    gtk_label_set_text(GTK_LABEL(lbl), s);
    return moo_bool(1);
}

MooValue moo_gui_icon_setze(MooValue fenster, MooValue icon_name) {
    GtkWidget* win = (GtkWidget*)moo_val_as_ptr(fenster);
    const char* n = (icon_name.tag == MOO_STRING) ? MV_STR(icon_name)->chars : "";
    gtk_window_set_icon_from_file(GTK_WINDOW(win), n, NULL);
    return moo_bool(1);
}

MooValue moo_gui_zeige(MooValue fenster) {
    GtkWidget* win = (GtkWidget*)moo_val_as_ptr(fenster);
    gtk_widget_show_all(win);
    gtk_main();
    return moo_none();
}
