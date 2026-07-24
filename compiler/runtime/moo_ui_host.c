/*
 * moo_ui_host.c — Backend-Registry fuer den Host-Vertrag (NATIVE-UI-4).
 * Featureneutral, wird IMMER gebaut (keine Toolkit-Abhaengigkeit).
 * Ohne einkompiliertes Backend bleibt moo_ui_host_aktiv() NULL.
 */

#include "moo_ui_host.h"
#include <stddef.h>

static const MooUiHostOps* g_aktives_backend = NULL;

/* Link-Anker fuer Backends im Static-Archiv: der Linker zieht nur
 * referenzierte Objekte — ohne diesen Aufruf laeuft die
 * Selbstregistrierung eines nie direkt referenzierten Backends nicht. */
#ifdef MOO_HAS_WAYLAND_UI
extern void moo_ui_wayland_link_anker(void);
#endif
#ifdef MOO_HAS_X11_UI
extern void moo_ui_x11_link_anker(void);
#endif
static void backends_ankern(void) {
#ifdef MOO_HAS_WAYLAND_UI
    moo_ui_wayland_link_anker();
#endif
#ifdef MOO_HAS_X11_UI
    moo_ui_x11_link_anker();
#endif
}

const MooUiHostOps* moo_ui_host_aktiv(void) {
    backends_ankern();
    return g_aktives_backend;
}

int moo_ui_host_vollstaendig(const MooUiHostOps* ops) {
    if (!ops || !ops->name) return 0;
    if (!ops->fenster || !ops->fenster_schliessen || !ops->zeige) return 0;
    if (!ops->zeichne_frame) return 0;
    if (!ops->laufen || !ops->pump || !ops->beenden) return 0;
    if (!ops->drag_start || !ops->resize_start) return 0;
    if (!ops->minimiere || !ops->maximiere_umschalten) return 0;
    if (!ops->transparenz || !ops->cursor_setze) return 0;
    if (!ops->letztes_input_serial) return 0;
    if (!ops->praesentiere || !ops->input_callback_setze) return 0;
    return 1;
}

int moo_ui_host_registriere(const MooUiHostOps* ops) {
    if (!moo_ui_host_vollstaendig(ops)) return 0;
    if (g_aktives_backend) return 0; /* erste Registrierung gewinnt */
    g_aktives_backend = ops;
    return 1;
}

/* Ab hier: Moo-Builtin-Traeger — brauchen die Kern-Runtime (moo_bool,
 * moo_none, moo_string_new). Das toolkitfreie Contract-Gate
 * (test-ui-host-contract) blendet sie mit -DMOO_UI_HOST_OHNE_DISPATCHER
 * aus: es prueft Registry+Vollstaendigkeit; die Dispatcher beweist der
 * echte Backend-Lauf (native-ui-beweis.sh). */
#ifndef MOO_UI_HOST_OHNE_DISPATCHER

MooValue moo_ui_host_backend_name(void) {
    const MooUiHostOps* ops = moo_ui_host_aktiv();
    if (!ops) return moo_string_new("keins");
    return moo_string_new(ops->name);
}

/* ------------------------------------------------------------------ *
 * Backend-agnostische Dispatcher (Builtin-Traeger host_*)
 * ------------------------------------------------------------------ */

#define HOST_ODER(fallback) \
    const MooUiHostOps* ops = moo_ui_host_aktiv(); \
    if (!ops) return (fallback)

MooValue moo_ui_host_fenster(MooValue titel, MooValue breite, MooValue hoehe, MooValue flags) {
    HOST_ODER(moo_bool(0));
    return ops->fenster(titel, breite, hoehe, flags, moo_none());
}
MooValue moo_ui_host_zeige(MooValue fenster) {
    HOST_ODER(moo_bool(0));
    return ops->zeige(fenster);
}
MooValue moo_ui_host_schliessen(MooValue fenster) {
    HOST_ODER(moo_bool(0));
    return ops->fenster_schliessen(fenster);
}
MooValue moo_ui_host_praesentiere(MooValue fenster, MooValue frame) {
    HOST_ODER(moo_bool(0));
    return ops->praesentiere(fenster, frame);
}
MooValue moo_ui_host_input_callback(MooValue fenster, MooValue cb) {
    HOST_ODER(moo_bool(0));
    return ops->input_callback_setze(fenster, cb);
}
MooValue moo_ui_host_laufen(void) {
    HOST_ODER(moo_bool(0));
    return ops->laufen();
}
MooValue moo_ui_host_pump(void) {
    HOST_ODER(moo_bool(0));
    return ops->pump();
}
MooValue moo_ui_host_beenden(void) {
    HOST_ODER(moo_bool(0));
    return ops->beenden();
}
MooValue moo_ui_host_drag_start(MooValue fenster) {
    HOST_ODER(moo_bool(0));
    return ops->drag_start(fenster);
}
MooValue moo_ui_host_resize_start(MooValue fenster, MooValue kante) {
    HOST_ODER(moo_bool(0));
    return ops->resize_start(fenster, kante);
}
MooValue moo_ui_host_minimiere(MooValue fenster) {
    HOST_ODER(moo_bool(0));
    return ops->minimiere(fenster);
}
MooValue moo_ui_host_maximiere_umschalten(MooValue fenster) {
    HOST_ODER(moo_bool(0));
    return ops->maximiere_umschalten(fenster);
}
MooValue moo_ui_host_cursor_setze(MooValue fenster, MooValue name) {
    HOST_ODER(moo_bool(0));
    return ops->cursor_setze(fenster, name);
}

#endif /* MOO_UI_HOST_OHNE_DISPATCHER */
