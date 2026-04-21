#ifndef MOO_UI_H
#define MOO_UI_H

/*
 * moo_ui.h — Oeffentliche, OS-neutrale UI-API fuer moo.
 * =====================================================
 *
 * Plan-Referenz: Memory `plan-002-moo-ui-cross-platform` (Schicht 1).
 * Status: Phase 1 = Linux-Backend (GTK3) aktiv; Windows (Win32) und macOS
 *         (Cocoa) folgen in spaeteren Phasen. Signaturen sind dieselben,
 *         nur die `.c/.m`-Datei hinter den Funktionen wechselt.
 *
 * Alle Funktionen liefern/nehmen ausschliesslich `MooValue`. Widget-Handles
 * sind `MooValue` mit `tag == MOO_NUMBER` und einem gepackten Native-Pointer
 * in `data` (Pattern identisch zum bisherigen moo_gui/moo_tray).
 *
 * Ownership-Regel fuer Callbacks (MOO_FUNC):
 *   Jede Funktion, die einen callback entgegen nimmt, ruft intern
 *   moo_retain() und gibt die Referenz erst beim Widget-Destroy wieder
 *   frei (GClosureNotify / DestroyNotify / WM_DESTROY / dealloc).
 *   → moo-Aufrufer muessen NICHTS freigeben.
 *
 * Parent-Kontext:
 *   Jedes Widget-Create verlangt `parent` (ein Fenster-Handle). Es gibt
 *   keinen impliziten "aktuellen Fenster"-Stack. Dialoge akzeptieren
 *   `parent` ODER `MOO_NONE` (= toplevel/unparented).
 *
 * Event-Loop:
 *   `moo_ui_laufen()` ist die EINZIGE Hauptschleife. Sie ersetzt sowohl
 *   das alte `moo_gui_zeige` als auch `moo_tray_run`. Fenster werden mit
 *   `moo_ui_zeige` sichtbar gemacht (non-blocking), der Event-Loop
 *   wird danach explizit mit `moo_ui_laufen()` gestartet. Tray-Icons und
 *   Fenster teilen sich dieselbe Loop — kein separates `tray_run` mehr.
 */

#include "moo_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Fenster-Flags (Bitfield fuer moo_ui_fenster)
 *
 * Window-Flags-Matrix (aus GUIBuilder v2.2):
 *   RESIZABLE | MAXIMIZED | Verhalten
 *   0         | 0         | Standard-Dialog (Login, Settings)
 *   0         | 1         | Maximiert-Start (Splash)
 *   1         | 0         | Drag-Resize (Tool-Windows, Panels)
 *   1         | 1         | Vollflexibel (Haupt-Anwendung)
 *
 * Wird als MooValue-Integer uebergeben (via moo_number()).
 * ========================================================================= */
#define MOO_UI_FLAG_NONE        0u
#define MOO_UI_FLAG_RESIZABLE   (1u << 0)
#define MOO_UI_FLAG_MAXIMIZED   (1u << 1)
#define MOO_UI_FLAG_FULLSCREEN  (1u << 2)
#define MOO_UI_FLAG_MODAL       (1u << 3)
#define MOO_UI_FLAG_NO_DECOR    (1u << 4)
#define MOO_UI_FLAG_ALWAYS_TOP  (1u << 5)

/* =========================================================================
 * Initialisierung & globaler Event-Loop
 * ========================================================================= */

/* Initialisiert das UI-Subsystem idempotent. Wird intern von allen
 * Widget-Create-Funktionen automatisch aufgerufen; expliziter Aufruf ist
 * optional (z.B. wenn man sehr frueh Dialog-Return-Werte braucht). */
MooValue moo_ui_init(void);

/* Startet den blocking Haupt-Event-Loop. Kehrt zurueck sobald
 *   (a) das letzte Top-Level-Fenster geschlossen wurde UND kein Tray
 *       aktiv ist, oder
 *   (b) moo_ui_beenden() aus einem Callback heraus gerufen wurde.
 * Ersetzt moo_gui_zeige() und moo_tray_run(). */
MooValue moo_ui_laufen(void);

/* Faehrt den Event-Loop herunter. Kann aus jedem Callback gerufen werden. */
MooValue moo_ui_beenden(void);

/* Pumpt anstehende Events einmal ohne zu blockieren. Fuer Game-Loops,
 * Render-Ticks oder Test-Szenarien. */
MooValue moo_ui_pump(void);

/* Schaltet Debug-Logging (aus GUIBuilder __showguilog): zeigt welche
 * Setup-Funktionen/Widgets mit welchen Returns gebaut wurden. */
MooValue moo_ui_debug(MooValue an);

/* =========================================================================
 * Fenster & Top-Level
 *
 * parent: MOO_NONE = Top-Level ohne Parent. Widget-Handle eines anderen
 *         Fensters = Kind-Fenster (wird transient zum Parent).
 * flags:  Bitfield aus MOO_UI_FLAG_* oben (als MooValue-Number).
 * ========================================================================= */

MooValue moo_ui_fenster(MooValue titel, MooValue breite, MooValue hoehe,
                        MooValue flags, MooValue parent);

MooValue moo_ui_fenster_titel_setze(MooValue fenster, MooValue titel);
MooValue moo_ui_fenster_icon_setze(MooValue fenster, MooValue pfad);
MooValue moo_ui_fenster_groesse_setze(MooValue fenster, MooValue b, MooValue h);
MooValue moo_ui_fenster_position_setze(MooValue fenster, MooValue x, MooValue y);
MooValue moo_ui_fenster_schliessen(MooValue fenster);

/* Zeigt Fenster non-blocking (kein impliziter Event-Loop-Start).
 * Ruft Event-Loop-Start ueber moo_ui_laufen(). */
MooValue moo_ui_zeige(MooValue fenster);

/* Zeigt Fenster non-blocking (Alias, explizit fuer Tray-parallel-Nutzung). */
MooValue moo_ui_zeige_nebenbei(MooValue fenster);

/* Registriert close-Handler (callback wird vor dem Schliessen gerufen;
 * liefert callback wahr zurueck → schliessen; falsch → abbrechen). */
MooValue moo_ui_fenster_on_close(MooValue fenster, MooValue callback);

/* =========================================================================
 * Basis-Widgets
 *
 * Koordinaten: Pixel, Origin links-oben, bezogen auf parent-content-area.
 * Rueckgabe: Widget-Handle (MooValue).
 * ========================================================================= */

MooValue moo_ui_label(MooValue parent, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h);
MooValue moo_ui_label_setze(MooValue label, MooValue text);
MooValue moo_ui_label_text(MooValue label);

MooValue moo_ui_knopf(MooValue parent, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h,
                      MooValue callback);

MooValue moo_ui_checkbox(MooValue parent, MooValue text,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue initial, MooValue callback);
MooValue moo_ui_checkbox_wert(MooValue checkbox);
MooValue moo_ui_checkbox_setze(MooValue checkbox, MooValue wert);

MooValue moo_ui_radio(MooValue parent, MooValue gruppe, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h,
                      MooValue callback);
MooValue moo_ui_radio_wert(MooValue radio);

/* `passwort`: MooValue-Bool; wahr → masked-Input. */
MooValue moo_ui_eingabe(MooValue parent,
                        MooValue x, MooValue y, MooValue b, MooValue h,
                        MooValue platzhalter, MooValue passwort);
MooValue moo_ui_eingabe_text(MooValue eingabe);
MooValue moo_ui_eingabe_setze(MooValue eingabe, MooValue text);
MooValue moo_ui_eingabe_on_change(MooValue eingabe, MooValue callback);

MooValue moo_ui_textbereich(MooValue parent,
                            MooValue x, MooValue y, MooValue b, MooValue h);
MooValue moo_ui_textbereich_text(MooValue tb);
MooValue moo_ui_textbereich_setze(MooValue tb, MooValue text);
MooValue moo_ui_textbereich_anhaengen(MooValue tb, MooValue text);

/* =========================================================================
 * Listen & Auswahl
 * ========================================================================= */

/* optionen: MooList mit Strings. */
MooValue moo_ui_dropdown(MooValue parent, MooValue optionen,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue callback);
MooValue moo_ui_dropdown_auswahl(MooValue dd);        /* liefert index oder -1 */
MooValue moo_ui_dropdown_auswahl_setze(MooValue dd, MooValue index);
MooValue moo_ui_dropdown_text(MooValue dd);

/* spalten: MooList mit Spalten-Titeln (Strings). */
MooValue moo_ui_liste(MooValue parent, MooValue spalten,
                      MooValue x, MooValue y, MooValue b, MooValue h);
/* zeile: MooList mit Zellen-Werten (Strings). */
MooValue moo_ui_liste_zeile_hinzu(MooValue liste, MooValue zeile);
MooValue moo_ui_liste_auswahl(MooValue liste);        /* liefert index oder -1 */
MooValue moo_ui_liste_zeile(MooValue liste, MooValue index); /* liefert MooList */
MooValue moo_ui_liste_leeren(MooValue liste);
MooValue moo_ui_liste_on_auswahl(MooValue liste, MooValue callback);

/* =========================================================================
 * Werte-Controls
 * ========================================================================= */

MooValue moo_ui_slider(MooValue parent, MooValue min, MooValue max, MooValue start,
                       MooValue x, MooValue y, MooValue b, MooValue h,
                       MooValue callback);
MooValue moo_ui_slider_wert(MooValue slider);
MooValue moo_ui_slider_setze(MooValue slider, MooValue wert);

MooValue moo_ui_fortschritt(MooValue parent,
                            MooValue x, MooValue y, MooValue b, MooValue h);
/* wert: 0.0 .. 1.0 */
MooValue moo_ui_fortschritt_setze(MooValue bar, MooValue wert);

/* =========================================================================
 * Bild & Grafik
 * ========================================================================= */

MooValue moo_ui_bild(MooValue parent, MooValue pfad,
                     MooValue x, MooValue y, MooValue b, MooValue h);
MooValue moo_ui_bild_setze(MooValue bild, MooValue pfad);

/* Custom-Draw-Surface.
 *
 * callback: Funktion(leinwand, zeichner) — wird pro Repaint einmal
 * gerufen. `leinwand` ist das Widget-Handle, `zeichner` ein opaker
 * Handle, der NUR innerhalb dieses Callbacks gueltig ist (danach invalid).
 *
 * Backend-Mapping:
 *   Linux (GTK3) : GtkDrawingArea + cairo_t*       (draw-Signal)
 *   Windows      : STATIC+SS_OWNERDRAW + HDC       (WM_DRAWITEM)
 *   macOS        : NSView-Subklasse + CGContextRef (drawRect:)
 *
 * Phase 6 Vision: moo_ui_leinwand_gpu(...) mit identischer Callback-
 * Signatur, aber Vulkan/GL/Metal-Backend. User-Code bleibt gleich. */
MooValue moo_ui_leinwand(MooValue parent,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue on_zeichne);
MooValue moo_ui_leinwand_anfordern(MooValue leinwand); /* Repaint-Request */

/* =========================================================================
 * Zeichner-Primitive (nur im on_zeichne-Callback gueltig)
 *
 * `zeichner` ist der 2. Parameter des on_zeichne-Callbacks. Ausserhalb
 * des Callbacks sind die Funktionen no-op und liefern falsch. Alle
 * Koordinaten in Pixeln, Farbkanaele r/g/b/a in 0..255.
 * ========================================================================= */

MooValue moo_ui_zeichne_farbe(MooValue zeichner,
                              MooValue r, MooValue g, MooValue b, MooValue a);

MooValue moo_ui_zeichne_linie(MooValue zeichner,
                              MooValue x1, MooValue y1,
                              MooValue x2, MooValue y2,
                              MooValue breite);

/* gefuellt: MooBool. wahr → Fill, falsch → Outline. */
MooValue moo_ui_zeichne_rechteck(MooValue zeichner,
                                 MooValue x, MooValue y,
                                 MooValue b, MooValue h,
                                 MooValue gefuellt);

MooValue moo_ui_zeichne_kreis(MooValue zeichner,
                              MooValue cx, MooValue cy,
                              MooValue radius, MooValue gefuellt);

MooValue moo_ui_zeichne_text(MooValue zeichner,
                             MooValue x, MooValue y,
                             MooValue text, MooValue schriftgroesse);

MooValue moo_ui_zeichne_bild(MooValue zeichner,
                             MooValue x, MooValue y,
                             MooValue b, MooValue h,
                             MooValue pfad);

/* =========================================================================
 * Layout-Hilfen
 * ========================================================================= */

MooValue moo_ui_rahmen(MooValue parent, MooValue titel,
                       MooValue x, MooValue y, MooValue b, MooValue h);

MooValue moo_ui_trenner(MooValue parent,
                        MooValue x, MooValue y, MooValue b, MooValue h);

MooValue moo_ui_tabs(MooValue parent,
                     MooValue x, MooValue y, MooValue b, MooValue h);
/* Liefert einen Tab-Container-Handle, der als `parent` fuer Kind-Widgets
 * dient. */
MooValue moo_ui_tab_hinzu(MooValue tabs, MooValue titel);
MooValue moo_ui_tabs_auswahl(MooValue tabs);
MooValue moo_ui_tabs_auswahl_setze(MooValue tabs, MooValue index);

MooValue moo_ui_scroll(MooValue parent,
                       MooValue x, MooValue y, MooValue b, MooValue h);
/* Scroll-Inhalts-Container als `parent` fuer Kinder. */

/* =========================================================================
 * Allgemeine Widget-Operationen (wirken auf JEDES Widget-Handle)
 * ========================================================================= */

MooValue moo_ui_sichtbar(MooValue widget, MooValue sichtbar);
MooValue moo_ui_aktiv(MooValue widget, MooValue aktiv);
MooValue moo_ui_position_setze(MooValue widget, MooValue x, MooValue y);
MooValue moo_ui_groesse_setze(MooValue widget, MooValue b, MooValue h);
MooValue moo_ui_farbe_setze(MooValue widget, MooValue hex);   /* "#rrggbb" */
MooValue moo_ui_schrift_setze(MooValue widget, MooValue groesse, MooValue fett);
MooValue moo_ui_tooltip_setze(MooValue widget, MooValue text);
MooValue moo_ui_zerstoere(MooValue widget);

/* =========================================================================
 * Timer (an den globalen Event-Loop gebunden)
 * ========================================================================= */

/* Liefert Timer-ID (MooNumber). */
MooValue moo_ui_timer_hinzu(MooValue ms, MooValue callback);
MooValue moo_ui_timer_entfernen(MooValue timer_id);

/* =========================================================================
 * Dialoge
 *
 * `parent` darf MOO_NONE sein (unparented Dialog). Returns:
 *   info/warnung/fehler → MOO_NONE
 *   frage               → MOO_BOOL (ja = wahr)
 *   eingabe_dialog      → MOO_STRING oder MOO_NONE bei Abbruch
 *   datei/ordner        → MOO_STRING oder MOO_NONE bei Abbruch
 * ========================================================================= */

MooValue moo_ui_info(MooValue parent, MooValue titel, MooValue text);
MooValue moo_ui_warnung(MooValue parent, MooValue titel, MooValue text);
MooValue moo_ui_fehler(MooValue parent, MooValue titel, MooValue text);
MooValue moo_ui_frage(MooValue parent, MooValue titel, MooValue text);

MooValue moo_ui_eingabe_dialog(MooValue parent, MooValue titel,
                               MooValue prompt, MooValue vorgabe);

/* filter: MooList mit Paaren [["Bilder","*.png;*.jpg"], ["Alle","*"]]
 *         oder MOO_NONE fuer keinen Filter. */
MooValue moo_ui_datei_oeffnen(MooValue parent, MooValue titel, MooValue filter);
MooValue moo_ui_datei_speichern(MooValue parent, MooValue titel, MooValue filter);
MooValue moo_ui_ordner_waehlen(MooValue parent, MooValue titel);

/* =========================================================================
 * Menueleiste
 * ========================================================================= */

MooValue moo_ui_menueleiste(MooValue fenster);
MooValue moo_ui_menue(MooValue leiste, MooValue titel);
MooValue moo_ui_menue_eintrag(MooValue menue, MooValue titel, MooValue callback);
MooValue moo_ui_menue_trenner(MooValue menue);
MooValue moo_ui_menue_untermenue(MooValue menue, MooValue titel);

/* =========================================================================
 * Kompatibilitaets-Shims (Deprecation-Stufe 1)
 *
 * Die alten moo_gui_* / tray-run-Funktionen in moo_gui.c bleiben temporaer
 * als Wrappers um moo_ui_* bestehen, werden in Phase 2 entfernt.
 * Details: Thought ef83dc4a, Abschnitt "Alte moo_gui.c / moo_tray.c".
 * ========================================================================= */

#ifdef __cplusplus
}
#endif

#endif /* MOO_UI_H */
