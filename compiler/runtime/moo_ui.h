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

/* Setzt die Pixel-Breite einer Spalte.
 *
 * spalte_index: 0-basierter Spalten-Index (MOO_INTEGER).
 * breite:       Pixel-Breite (MOO_INTEGER, > 0). Wird als feste Breite
 *               gesetzt; automatische Spalten-Anpassung ist danach inaktiv
 *               fuer diese Spalte.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_tree_view_column_set_fixed_width(col, breite) +
 *                  gtk_tree_view_column_set_sizing(GTK_TREE_VIEW_COLUMN_FIXED)
 *   Windows      : ListView_SetColumnWidth(hwnd, spalte_index, breite)
 *   macOS        : [[tv tableColumnWithIdentifier:idx] setWidth:breite]
 *
 * Liefert MOO_BOOL wahr bei Erfolg, falsch wenn Index ausser Bereich. */
MooValue moo_ui_liste_spalte_breite(MooValue liste, MooValue spalte_index,
                                    MooValue breite);

/* Aktiviert oder deaktiviert das Klick-Sortieren fuer eine Spalte.
 *
 * spalte_index: 0-basierter Spalten-Index (MOO_INTEGER).
 * aktiv:        MOO_BOOL; wahr = Spalten-Header klickbar, loest Sort aus.
 *               falsch = kein Klick-Sort fuer diese Spalte.
 *
 * Hinweis: Das Sortieren selbst bleibt modell-seitig (Backend sortiert
 * alphabetisch nach dem Spalten-String); fuer eigene Sortierfunktionen
 * moo_ui_liste_sortiere nutzen.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_tree_view_column_set_clickable(col, aktiv) +
 *                  gtk_tree_view_column_set_sort_column_id(col, spalte_index)
 *   Windows      : ListView_SetExtendedListViewStyle += LVS_EX_HEADERDRAGDROP;
 *                  WM_NOTIFY / LVN_COLUMNCLICK aktivieren/deaktivieren.
 *   macOS        : [tableColumn setSortDescriptorPrototype:...] setzen/loeschen.
 *
 * Liefert MOO_BOOL wahr bei Erfolg, falsch wenn Index ausser Bereich. */
MooValue moo_ui_liste_sortierbar(MooValue liste, MooValue spalte_index,
                                 MooValue aktiv);

/* Sortiert alle Zeilen der Liste nach einer Spalte.
 *
 * spalte_index:  0-basierter Spalten-Index (MOO_INTEGER).
 * aufsteigend:   MOO_BOOL; wahr = A→Z / 0→9, falsch = Z→A / 9→0.
 *
 * Die Sortierung ist string-lexikographisch. Numerische Werte muessen
 * vom Aufrufer als zero-padded String vorformatiert werden falls echte
 * numerische Sortierung gewuenscht wird.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_tree_sortable_set_sort_column_id(model, spalte_index,
 *                  aufsteigend ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING)
 *   Windows      : ListView_SortItems(hwnd, CompareFunc, spalte_index);
 *                  Richtung per LPARAM in CompareFunc.
 *   macOS        : [tv setSortDescriptors:@[desc]]; tableView:sortDescriptorsDidChange:
 *                  loest reloadData aus.
 *
 * Liefert MOO_BOOL wahr bei Erfolg, falsch wenn Index ausser Bereich. */
MooValue moo_ui_liste_sortiere(MooValue liste, MooValue spalte_index,
                               MooValue aufsteigend);

/* Ersetzt alle Zellen einer vorhandenen Zeile.
 *
 * zeile_index:  0-basierter Zeilen-Index (MOO_INTEGER).
 * werte_liste:  MooList mit neuen Zellen-Werten (Strings). Laenge muss
 *               genau der Spalten-Anzahl entsprechen. Bei kuerzerer oder
 *               laengerer Liste liefert die Funktion falsch und nimmt keine
 *               Teil-Aenderung vor.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_list_store_set(store, &iter, col0, str0, col1, str1, ..., -1)
 *                  nach gtk_tree_model_iter_nth_child(model, &iter, NULL, zeile_index).
 *   Windows      : Schleife ueber ListView_SetItemText(hwnd, zeile_index, col, str).
 *   macOS        : [ds replaceRow:zeile_index withValues:werte_liste]; [tv reloadData].
 *
 * Liefert MOO_BOOL wahr bei Erfolg, falsch wenn Index ausser Bereich. */
MooValue moo_ui_liste_zeile_setze(MooValue liste, MooValue zeile_index,
                                  MooValue werte_liste);

/* Setzt den Inhalt einer einzelnen Zelle.
 *
 * zeile_index:   0-basierter Zeilen-Index (MOO_INTEGER).
 * spalte_index:  0-basierter Spalten-Index (MOO_INTEGER).
 * wert:          Neuer Zellen-Inhalt (MOO_STRING).
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_list_store_set(store, &iter, spalte_index, str, -1)
 *                  nach gtk_tree_model_iter_nth_child fuer zeile_index.
 *   Windows      : ListView_SetItemText(hwnd, zeile_index, spalte_index, str).
 *   macOS        : [ds setValue:wert row:zeile_index column:spalte_index];
 *                  [tv reloadDataForRowIndexes:... columnIndexes:...].
 *
 * Liefert MOO_BOOL wahr bei Erfolg, falsch wenn Indizes ausser Bereich. */
MooValue moo_ui_liste_zelle_setze(MooValue liste, MooValue zeile_index,
                                  MooValue spalte_index, MooValue wert);

/* Entfernt eine Zeile aus der Liste.
 *
 * zeile_index:  0-basierter Index der zu loeschenden Zeile (MOO_INTEGER).
 *               Alle nachfolgenden Zeilen ruecken um eins hoch; gespeicherte
 *               Indizes werden damit ungueltig.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_list_store_remove(store, &iter) nach iter via
 *                  gtk_tree_model_iter_nth_child.
 *   Windows      : ListView_DeleteItem(hwnd, zeile_index).
 *   macOS        : [ds removeRowAtIndex:zeile_index];
 *                  [tv removeRowsAtIndexes:... withAnimation:NSTableViewAnimationEffectNone].
 *
 * Liefert MOO_BOOL wahr bei Erfolg, falsch wenn Index ausser Bereich. */
MooValue moo_ui_liste_entferne(MooValue liste, MooValue zeile_index);

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

/* -------------------------------------------------------------------------
 * Leinwand-Maus-Events (Welle 2 / Plan 003 P5, Phase 1)
 *
 * Registriert Callbacks fuer Maus-Interaktion auf einer Leinwand. Nur
 * EIN Callback pro Event-Typ pro Leinwand — erneuter Aufruf ersetzt
 * das vorherige Binding (alter Callback wird released).
 *
 * Koordinaten sind Leinwand-lokal (links-oben = 0,0), Pixel-Einheit,
 * konsistent mit den Zeichner-Primitiven.
 *
 * Tasten-Events (on_taste) sind bewusst NICHT Teil dieser Welle —
 * Keyboard-Fokus/Tab-Order ist backend-spezifisch und kommt separat.
 * ------------------------------------------------------------------------- */

/* Registriert einen Maus-Klick-Callback.
 *
 * Callback-Signatur:
 *   on_maus(leinwand, x, y, taste)
 *     leinwand: Widget-Handle (MOO_NUMBER)
 *     x, y:     MOO_INTEGER, Leinwand-lokale Pixel-Koordinaten
 *     taste:    MOO_INTEGER, 1=links, 2=mitte, 3=rechts
 *
 * Feuert auf MouseDown (Press), NICHT auf Release — konsistent mit
 * dem Knopf-Aktivierungs-Pattern. Doppelklick wird (fuer diese Welle)
 * als zwei einzelne Klicks gesendet; dediziertes on_dblklick folgt
 * spaeter falls gewuenscht.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_widget_add_events(GDK_BUTTON_PRESS_MASK) +
 *                  "button-press-event"-Signal; event->button 1/2/3
 *                  direkt uebernehmen, event->x/y sind cairo-lokal.
 *   Windows      : Subclass der STATIC-Leinwand; WM_LBUTTONDOWN (1),
 *                  WM_MBUTTONDOWN (2), WM_RBUTTONDOWN (3).
 *                  x/y aus LOWORD/HIWORD(lParam), bereits client-lokal.
 *   macOS        : NSView-Subklasse ueberschreibt mouseDown: / rightMouseDown:
 *                  / otherMouseDown:; [event locationInWindow] ->
 *                  [view convertPoint:fromView:nil]; Y an flipped-view
 *                  anpassen (bereits flipped=YES → top-left-origin).
 *
 * Ownership: callback wird retain-t, beim Leinwand-Destroy oder
 * erneutem on_maus-Aufruf released.
 */
MooValue moo_ui_leinwand_on_maus(MooValue leinwand, MooValue callback);

/* Registriert einen Maus-Bewegungs-Callback.
 *
 * Callback-Signatur:
 *   on_bewegung(leinwand, x, y)
 *     leinwand: Widget-Handle
 *     x, y:     MOO_INTEGER, Leinwand-lokale Pixel
 *
 * Feuert bei JEDER Mausbewegung ueber der Leinwand, unabhaengig davon
 * ob eine Taste gedrueckt ist. Backends koennen Events koaleszieren
 * (es wird kein 1:1-Event pro Pixel garantiert). Ausserhalb der
 * Leinwand (nach Leave) feuert der Callback nicht mehr.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_widget_add_events(GDK_POINTER_MOTION_MASK) +
 *                  "motion-notify-event"; event->x/y.
 *   Windows      : WM_MOUSEMOVE auf der subclassed STATIC; optional
 *                  TrackMouseEvent fuer konsistentes Leave-Verhalten.
 *   macOS        : NSView mouseMoved: — benoetigt
 *                  addTrackingArea:NSTrackingMouseMoved|NSTrackingActiveInKeyWindow;
 *                  Koord-Konversion wie on_maus.
 *
 * Ownership: wie on_maus.
 */
MooValue moo_ui_leinwand_on_bewegung(MooValue leinwand, MooValue callback);

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

/* Text-Metrik: Liefert die Pixelbreite, die `text` in der angegebenen
 * Schriftgroesse beim Zeichnen belegen wuerde. Hauptzweck: zentrierte
 * und rechtsbuendige Layouts, Spalten-Ausrichtung auf der Leinwand.
 *
 * Gueltigkeit: NUR innerhalb des on_zeichne-Callbacks (benoetigt den
 * aktiven Backend-Graphics-Context). Ausserhalb → liefert MOO_INTEGER 0.
 *
 * Rueckgabe: MOO_INTEGER (Pixelbreite, gerundet auf ganze Pixel;
 * Backend-interne Subpixel-Metrik wird gekappt). Hoehe wird nicht
 * zurueckgegeben — Leinwand-Text hat in dieser Welle nur eine
 * horizontale Metrik (Hoehe kommt mit Phase-6-Font-API).
 *
 * Backend-Mapping:
 *   Linux (GTK3) : cairo_text_extents(cr, text, &ext); ext.x_advance.
 *                  Font-Size via cairo_set_font_size(cr, groesse)
 *                  VOR der Messung (konsistent mit ui_zeichne_text).
 *   Windows      : SelectObject(hdc, CreateFontW(-groesse, ...));
 *                  GetTextExtentPoint32W(hdc, wtext, len, &sz); sz.cx.
 *   macOS        : [NSString sizeWithAttributes:@{NSFontAttributeName:
 *                  [NSFont systemFontOfSize:groesse]}].width;
 *                  ceil() auf Integer runden.
 */
MooValue moo_ui_zeichne_text_breite(MooValue zeichner, MooValue text,
                                    MooValue groesse);

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
 * Keyboard-Shortcuts (Accelerator-Bindings)
 *
 * Bindet eine Tasten-Sequenz an einen Callback im Kontext eines Fensters.
 * Solange das Fenster Fokus hat (oder ein Kind-Widget darin), feuert
 * die Sequenz den Callback — voellig unabhaengig davon ob ein Menue-
 * Eintrag existiert. Ideal fuer globale App-Hotkeys.
 *
 * sequenz-Format (MooString, case-insensitive, "+"-getrennt):
 *   "Ctrl+S", "Shift+F1", "Ctrl+Alt+Del", "F11", "Strg+Z"
 *
 * Modifier-Tokens (synonym):
 *   Ctrl  | Control | Strg
 *   Shift | Umschalt
 *   Alt
 *   Super | Meta | Win | Cmd
 *
 * Key-Tokens: Buchstaben A..Z, Ziffern 0..9, F1..F24, Esc/Escape,
 *   Tab, Enter/Return, Space/Leertaste, Up/Down/Left/Right/Hoch/
 *   Runter/Links/Rechts, Home/Pos1, End/Ende, PageUp/BildHoch,
 *   PageDown/BildRunter, Delete/Entf, Backspace/Rueckschritt,
 *   Insert/Einfg, Plus, Minus, Comma/Komma, Period/Punkt, Slash, ";".
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_accel_group_connect + gtk_accelerator_parse
 *   Windows      : RegisterHotKey pro Fenster ODER TranslateAcceleratorW
 *                  mit ACCEL-Table (Backend entscheidet; moo-Aufrufer
 *                  sieht keinen Unterschied).
 *   macOS        : NSEvent addLocalMonitorForEventsMatchingMask:
 *                  (NSEventMaskKeyDown) gefiltert auf sequenz.
 *
 * Return moo_ui_shortcut_bind:
 *   wahr  → Binding aktiv (ersetzt ein evtl. vorheriges mit derselben
 *           Sequenz im selben Fenster still).
 *   falsch → sequenz nicht parsebar.
 *
 * Ownership: callback wird retain-t und beim Fenster-Destroy (oder
 * beim Re-Bind derselben Sequenz) released.
 * ========================================================================= */

MooValue moo_ui_shortcut_bind(MooValue fenster, MooValue sequenz,
                              MooValue callback);

/* Entfernt das Binding fuer `sequenz`. Liefert wahr wenn ein aktives
 * Binding entfernt wurde, falsch wenn keines existierte. */
MooValue moo_ui_shortcut_loese(MooValue fenster, MooValue sequenz);

#ifdef __cplusplus
}
#endif

#endif /* MOO_UI_H */
