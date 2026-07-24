/*
 * moo_ui_host.c — Backend-Registry fuer den Host-Vertrag (NATIVE-UI-4).
 * Featureneutral, wird IMMER gebaut (keine Toolkit-Abhaengigkeit).
 * Ohne einkompiliertes Backend bleibt moo_ui_host_aktiv() NULL.
 */

#include "moo_ui_host.h"
#include <stddef.h>

static const MooUiHostOps* g_aktives_backend = NULL;

const MooUiHostOps* moo_ui_host_aktiv(void) {
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
    return 1;
}

int moo_ui_host_registriere(const MooUiHostOps* ops) {
    if (!moo_ui_host_vollstaendig(ops)) return 0;
    if (g_aktives_backend) return 0; /* erste Registrierung gewinnt */
    g_aktives_backend = ops;
    return 1;
}

MooValue moo_ui_host_backend_name(void) {
    if (!g_aktives_backend) return moo_string_new("keins");
    return moo_string_new(g_aktives_backend->name);
}
