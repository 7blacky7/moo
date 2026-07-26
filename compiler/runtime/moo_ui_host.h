#ifndef MOO_UI_HOST_H
#define MOO_UI_HOST_H

/*
 * moo_ui_host.h — Host-Backend-Vertrag (NATIVE-UI-4)
 * ===================================================
 *
 * Schlanker austauschbarer Vertrag zwischen dem portablen Glass-/CSD-Layer
 * (stdlib/ui_moo_*, MOO_SURFACE) und dem OS-Host. Ein Backend liefert genau:
 * Fenster-Lebenszyklus, Frame-Present, Event-Dispatch und die CSD-Hooks.
 * Widgets/Chrome zeichnet der Glass-Layer selbst — Backends bleiben duenn.
 *
 * DISPATCH-VERTRAG (moOS-Pflicht): Jedes Backend MUSS beides anbieten:
 *   laufen()  — blockierender Callback-Loop (Desktop-Modell)
 *   pump()    — nicht-blockierender Poll-Schritt (moOS hat kein Event-FD)
 * Genau EIN Event-Loop-Owner pro Prozess (Callback-Refcount-Vertraege).
 *
 * INPUT-SERIAL (Wayland-Vorgriff): xdg_toplevel.move/resize verlangen das
 * letzte Pointer-Button-Serial. Backends ohne Serial-Modell (GTK/X11/Win32)
 * liefern 0 und setzen MOO_UI_HOST_CAP_INPUT_SERIAL nicht.
 *
 * Registrierung: Das einkompilierte Plattform-Backend registriert sich beim
 * Laden selbst; die erste vollstaendige Registrierung gewinnt. Backends
 * danach: Wayland (UI-1), X11 (UI-2), Win32 (UI-7), Cocoa (UI-8),
 * moOS-Framebuffer (UI-6).
 */

#include "moo_runtime.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capability-Bitmaske — capability-ehrlich: nur setzen was real geht. */
#define MOO_UI_HOST_CAP_TRANSPARENZ   (1u << 0) /* RGBA/per-pixel-Alpha */
#define MOO_UI_HOST_CAP_CSD_MOVE      (1u << 1) /* drag_start/resize_start echt */
#define MOO_UI_HOST_CAP_MINIMIEREN    (1u << 2)
#define MOO_UI_HOST_CAP_MAXIMIEREN    (1u << 3)
#define MOO_UI_HOST_CAP_INPUT_SERIAL  (1u << 4) /* letztes_input_serial liefert echte Serials */
#define MOO_UI_HOST_CAP_POLL_DISPATCH (1u << 5) /* pump ohne laufen nutzbar */
#define MOO_UI_HOST_CAP_DIRECT_PRESENT (1u << 6) /* praesentiere() echt (nicht Leinwand-Weg) */

typedef struct MooUiHostOps {
    const char* name;          /* "gtk" | "wayland" | "x11" | ... */
    uint32_t    capabilities;  /* MOO_UI_HOST_CAP_* */

    /* Fenster-Lebenszyklus */
    MooValue (*fenster)(MooValue titel, MooValue breite, MooValue hoehe,
                        MooValue flags, MooValue parent);
    MooValue (*fenster_schliessen)(MooValue fenster);
    MooValue (*zeige)(MooValue fenster);

    /* Present: fertiges MOO_FRAME auf den Fenster-Zeichner blitten */
    MooValue (*zeichne_frame)(MooValue zeichner, MooValue x, MooValue y,
                              MooValue b, MooValue h, MooValue frame);

    /* Dispatch (beide Pflicht, siehe Vertrag oben) */
    MooValue (*laufen)(void);
    MooValue (*pump)(void);
    MooValue (*beenden)(void);

    /* CSD-Hooks (UI-GLASS-CSD): der Glass-Layer macht Hit-Testing selbst
     * und ruft hier nur die WM-Operation dahinter. */
    MooValue (*drag_start)(MooValue fenster);
    MooValue (*resize_start)(MooValue fenster, MooValue kante);
    MooValue (*minimiere)(MooValue fenster);
    MooValue (*maximiere_umschalten)(MooValue fenster);
    MooValue (*transparenz)(MooValue fenster, MooValue einschalten);
    MooValue (*cursor_setze)(MooValue fenster, MooValue name);

    /* Letztes Pointer-Button-Serial des Fensters; 0 = kein Serial-Modell. */
    uint32_t (*letztes_input_serial)(MooValue fenster);

    /* Direkter Present-Pfad (CAP_DIRECT_PRESENT): fertiges MOO_FRAME
     * unmittelbar auf das Fenster praesentieren — der Weg fuer Backends
     * ohne Zeichner-/Leinwand-Modell (Wayland/X11/Framebuffer). Backends
     * ohne diesen Pfad (GTK) liefern einen ehrlichen falsch-Stub. */
    MooValue (*praesentiere)(MooValue fenster, MooValue frame);

    /* Input-/Frame-Registrierung: cb(art, a, b, c) mit art als Moo-String
     * in {"maus_runter","maus_hoch","bewegung","taste_runter","taste_hoch",
     * "resize","neuzeichnen","tick","schliessen"}. maus: a=x, b=y,
     * c=taste(1/2/3); taste: a=keysym; resize: a=breite, b=hoehe.
     * Jeder laufen-/pump-Dispatch-Batch liefert genau einen "tick". */
    MooValue (*input_callback_setze)(MooValue fenster, MooValue cb);
} MooUiHostOps;

/* Aktives Backend oder NULL, wenn keines registriert ist. */
const MooUiHostOps* moo_ui_host_aktiv(void);

/* Registriert ein Backend. Liefert 1 bei Erfolg. Ablehnungsgruende:
 * NULL, unvollstaendige Tabelle, oder es ist bereits eines registriert
 * (erste vollstaendige Registrierung gewinnt — ein Loop-Owner). */
int moo_ui_host_registriere(const MooUiHostOps* ops);

/* Vertrags-Selbstpruefung: 1 wenn name und ALLE Funktionspointer gesetzt. */
int moo_ui_host_vollstaendig(const MooUiHostOps* ops);

/* Moo-Builtin ui_host_backend(): Name des aktiven Backends als Moo-String,
 * "keins" wenn kein Backend registriert ist. Erste Vertragsnutzung aus Moo
 * heraus (NATIVE-UI-5 Beweis-Harness prueft den Namen im Prozess-Output). */
MooValue moo_ui_host_backend_name(void);

/* Backend-agnostische Dispatcher (Moo-Builtins host_*): routen auf das
 * aktive Backend; ohne Backend liefern sie falsch/nichts. */
MooValue moo_ui_host_fenster(MooValue titel, MooValue breite, MooValue hoehe, MooValue flags);
MooValue moo_ui_host_zeige(MooValue fenster);
MooValue moo_ui_host_schliessen(MooValue fenster);
MooValue moo_ui_host_praesentiere(MooValue fenster, MooValue frame);
MooValue moo_ui_host_input_callback(MooValue fenster, MooValue cb);
MooValue moo_ui_host_laufen(void);
MooValue moo_ui_host_pump(void);
MooValue moo_ui_host_beenden(void);
MooValue moo_ui_host_drag_start(MooValue fenster);
MooValue moo_ui_host_resize_start(MooValue fenster, MooValue kante);
MooValue moo_ui_host_minimiere(MooValue fenster);
MooValue moo_ui_host_maximiere_umschalten(MooValue fenster);
MooValue moo_ui_host_cursor_setze(MooValue fenster, MooValue name);

#ifdef __cplusplus
}
#endif

#endif /* MOO_UI_HOST_H */
