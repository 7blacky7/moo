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

/* Registriert Resize-Handler. Callback-Signatur: on_resize(b, h) — neue
 * Client-Pixelbreite/-hoehe. Feuert auch beim erstmaligen Layout (initial
 * size-allocate). Re-Bind disconnectet das vorherige Binding still.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : "size-allocate" auf dem Top-Level-Fenster.
 *   Windows      : WM_SIZE (Subclass-Proc); LOWORD/HIWORD(lParam).
 *   macOS        : NSWindowDidResizeNotification.
 */
MooValue moo_ui_fenster_on_resize(MooValue fenster, MooValue callback);

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

/* Feuert wenn der User in einer Eingabe Enter/Return drueckt. Callback
 * ohne Argumente — der aktuelle Text liest sich via moo_ui_eingabe_text.
 * Re-Bind disconnectet das vorherige Binding still.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : "activate"-Signal auf GtkEntry (feuert bei Return).
 *   Windows      : WM_KEYDOWN/VK_RETURN auf der EDIT-Subclass.
 *   macOS        : NSTextField doCommandBySelector:@selector(insertNewline:).
 */
MooValue moo_ui_eingabe_on_enter(MooValue eingabe, MooValue callback);

MooValue moo_ui_textbereich(MooValue parent,
                            MooValue x, MooValue y, MooValue b, MooValue h);
MooValue moo_ui_textbereich_text(MooValue tb);
MooValue moo_ui_textbereich_setze(MooValue tb, MooValue text);
MooValue moo_ui_textbereich_anhaengen(MooValue tb, MooValue text);

/* Tasten-Hook fuer Textbereich. Callback-Signatur:
 *   on_key(key, ctrl, shift, alt)
 *     key:   MOO_STRING — Symbol-Name (z.B. "Tab", "Return", "a", "Escape").
 *     ctrl/shift/alt: MOO_BOOL — Modifier-Status zum Zeitpunkt der Taste.
 *
 * Rueckgabewert wahr → Default-Handling unterdruecken (Taste konsumiert),
 * falsch → an Backend weiterreichen. Damit kann ui-Code z.B. Tab abfangen
 * und stattdessen einen Tabstop einfuegen. Re-Bind disconnectet still.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : "key-press-event" auf der inneren GtkTextView;
 *                  gdk_keyval_name fuer den Symbol-Namen.
 *   Windows      : WM_KEYDOWN auf der EDIT-Multiline-Subclass; VK_*
 *                  -> Symbolname via Lookup-Tabelle.
 *   macOS        : NSTextView keyDown:; [event characters]/keyCode.
 */
MooValue moo_ui_textbereich_on_key(MooValue tb, MooValue callback);

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

/* Mausrad-Hook fuer Liste. Callback-Signatur:
 *   on_scroll(delta_y)
 *     delta_y: MOO_NUMBER — positiv = nach unten, negativ = nach oben.
 *              Bei Smooth-Scroll der tatsaechliche Delta-Wert; sonst +/- 1.0.
 * Rueckgabe ignoriert (das native Scroll-Verhalten der Liste bleibt aktiv).
 * Re-Bind disconnectet still.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : "scroll-event" auf der GtkTreeView mit GDK_SCROLL_MASK
 *                  + GDK_SMOOTH_SCROLL_MASK; ev->direction / ev->delta_y.
 *   Windows      : WM_MOUSEWHEEL auf der ListView-Subclass; delta via
 *                  GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA.
 *   macOS        : NSScrollView scrollWheel:; [event scrollingDeltaY].
 */
MooValue moo_ui_liste_on_scroll(MooValue liste, MooValue callback);

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
 * Widget-Introspection (Plan-004 P1)
 *
 * Erlaubt Agenten und User-Code, UI-Baeume programmatisch auszulesen und
 * einzelne Widgets per String-ID aufzufinden. Gehoert zur normalen UI-API
 * (nicht test-only): IDs sind auch fuer Styling, State-Binding by-id und
 * Debug-Logs nuetzlich. Die Screenshot-/Automations-APIs (Klick/Text)
 * folgen als separates ui_test-Modul in spaeteren Phasen.
 *
 * ID-Semantik: Pro Fenster sollen IDs eindeutig sein; die Backends
 * erzwingen das NICHT, aber moo_ui_widget_suche liefert dann den zuerst
 * gesetzten Treffer. IDs ueberleben Property-Aenderungen, aber nicht
 * moo_ui_zerstoere.
 * ========================================================================= */

/* Setzt eine String-ID auf ein beliebiges Widget. Die ID ist ein freies
 * String-Label, das spaeter fuer moo_ui_widget_suche und die Felder
 * `id` in moo_ui_widget_info/moo_ui_widget_baum genutzt wird.
 *
 * id_string: MOO_STRING (leerer String loescht die ID). MOO_NONE loescht
 *            die ID ebenfalls.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : g_object_set_data_full(G_OBJECT(w), "moo-id",
 *                  g_strdup(id), g_free) — lebt bis Widget-Destroy.
 *   Windows      : SetPropW(hwnd, L"moo-id", wcsdup(id)); Cleanup via
 *                  RemovePropW in der Destroy-Subclass.
 *   macOS        : objc_setAssociatedObject(view, &kMooIdKey,
 *                  [NSString stringWithUTF8String:id],
 *                  OBJC_ASSOCIATION_COPY_NONATOMIC).
 *
 * Liefert MOO_BOOL wahr bei Erfolg, falsch wenn widget kein gueltiges
 * Widget-Handle ist. */
MooValue moo_ui_widget_id_setze(MooValue widget, MooValue id_string);

/* Liest die zuvor gesetzte String-ID eines Widgets aus.
 *
 * Rueckgabe: MOO_STRING mit der ID, oder MOO_NONE wenn keine ID gesetzt
 * ist. Eine leere ID wird als MOO_NONE gemeldet.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : g_object_get_data(G_OBJECT(w), "moo-id") → String.
 *   Windows      : GetPropW(hwnd, L"moo-id") → wchar_t*, UTF-16→UTF-8.
 *   macOS        : objc_getAssociatedObject(view, &kMooIdKey).
 */
MooValue moo_ui_widget_id_hole(MooValue widget);

/* Liefert einen strukturierten Snapshot der wichtigsten Widget-Eigenschaften.
 *
 * Rueckgabe: MOO_DICT mit den Schluesseln:
 *   "typ"      → MOO_STRING (z.B. "knopf","label","eingabe","liste",
 *                "leinwand","fenster","checkbox","radio","slider",
 *                "fortschritt","dropdown","rahmen","trenner","tabs",
 *                "scroll","bild","textbereich","menueleiste","menue","timer").
 *   "x","y"    → MOO_INTEGER, Pixel, parent-content-area-lokal.
 *                Fuer Fenster: Bildschirm-Koordinaten.
 *   "b","h"    → MOO_INTEGER, Pixel-Breite/Hoehe.
 *   "sichtbar" → MOO_BOOL.
 *   "aktiv"    → MOO_BOOL (entspricht nicht-disabled).
 *   "id"       → MOO_STRING oder MOO_NONE (siehe moo_ui_widget_id_hole).
 *   "text"     → MOO_STRING oder MOO_NONE. Fuer Label/Knopf/Checkbox/
 *                Eingabe/Textbereich/Fenster-Titel: der aktuelle Text.
 *                Sonst MOO_NONE.
 *   "name"     → MOO_STRING oder MOO_NONE. Backend-spezifischer Widget-
 *                Name (GTK gtk_widget_get_name, Win32 Class+Ctrl-ID,
 *                Cocoa NSAccessibilityIdentifier). Fuer Agent-Heuristiken
 *                wenn keine id gesetzt ist.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : GtkAllocation + gtk_widget_get_visible +
 *                  gtk_widget_is_sensitive + Typ-Cast-Kette fuer text.
 *   Windows      : GetWindowRect+ScreenToClient, IsWindowVisible,
 *                  IsWindowEnabled, GetClassNameW + GetWindowTextW.
 *   macOS        : NSView frame (mit Flip), isHidden, isEnabled,
 *                  class description + accessibilityLabel.
 *
 * Liefert MOO_NONE wenn widget ungueltig ist. */
MooValue moo_ui_widget_info(MooValue widget);

/* Liefert rekursiv den Widget-Baum eines Fensters als flache MooList.
 *
 * Jeder Eintrag ist ein Dict wie in moo_ui_widget_info, zusaetzlich:
 *   "tiefe"  → MOO_INTEGER, 0 = Fenster selbst, 1 = direkte Kinder, usw.
 *   "eltern" → MOO_STRING oder MOO_NONE, die ID des unmittelbaren
 *              Container-Widgets (falls gesetzt) — erleichtert Diffs
 *              ohne separaten Tree-Walk.
 *
 * Reihenfolge: pre-order (Eltern vor Kindern), Geschwister in
 * Einfuegereihenfolge (GTK: g_list_children; Win32: EnumChildWindows
 * Z-Order; Cocoa: subviews-Array).
 *
 * Backend-Mapping:
 *   Linux (GTK3) : rekursiv gtk_container_foreach(GTK_CONTAINER(w), ...).
 *   Windows      : EnumChildWindows(hwnd, cb, &list) rekursiv je HWND.
 *   macOS        : rekursiv ueber [view subviews].
 *
 * Liefert MOO_NONE wenn `fenster` kein gueltiges Fenster-Handle ist.
 * Leere Container liefern eine MooList mit nur dem Fenster-Eintrag. */
MooValue moo_ui_widget_baum(MooValue fenster);

/* Sucht im Widget-Baum eines Fensters das erste Widget mit passender
 * String-ID.
 *
 * id_string: MOO_STRING. Leere oder MOO_NONE-IDs liefern MOO_NONE.
 *
 * Suche ist pre-order, string-exakt (case-sensitive). Mehrfach-Treffer
 * (sollten durch Aufrufer vermieden werden) liefern den ersten in
 * Baum-Reihenfolge.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : Tree-Walk wie moo_ui_widget_baum, Vergleich via
 *                  g_object_get_data(w,"moo-id") + g_strcmp0.
 *   Windows      : EnumChildWindows + GetPropW(hwnd,L"moo-id"); UTF-16
 *                  Vergleich.
 *   macOS        : recursiveSubviews + objc_getAssociatedObject + isEqual:.
 *
 * Liefert das Widget-Handle (MOO_NUMBER) oder MOO_NONE wenn nichts
 * gefunden wurde oder `fenster` ungueltig ist. */
MooValue moo_ui_widget_suche(MooValue fenster, MooValue id_string);

/* =========================================================================
 * Test-/Debug-API: Snapshot (Plan-004 P2)
 *
 * Nimmt einen PNG-Screenshot von einem Fenster oder einem einzelnen Widget
 * auf und schreibt ihn als Datei. Diese API gehoert bewusst zur Test-/
 * Debug-Schicht (Namensraum `ui_test_*`), nicht zur normalen App-API —
 * Endnutzer-Anwendungen sollen sich nicht selbst fotografieren muessen.
 * Hauptzweck: Agenten-/KI-Review und visuelle Regressionstests zusammen
 * mit dem Sidecar-JSON aus der P1-Introspection.
 *
 * Format: ausschliesslich PNG. Die Runtime liefert nur das PNG; die
 * zugehoerige JSON-Sidecar-Erzeugung (aus `ui_widget_baum_json`) laeuft
 * ueber die stdlib (siehe P1-Review und ui-test-stdlib-p2).
 *
 * Sichtbarkeit: Fenster bzw. Widget MUSS sichtbar und realisiert sein
 * (nach `moo_ui_zeige` plus mindestens ein `moo_ui_pump`-Tick, damit
 * das Backend das Fenster tatsaechlich gerendert hat). Nicht sichtbare
 * oder zerstoerte Widgets → falsch.
 *
 * Hintergrund/Alpha: Das Backend entscheidet, ob der Fenster-Hintergrund
 * (Decorations, Desktop) mit-aufgenommen wird oder ein transparenter
 * Kanal entsteht. Garantiert ist nur: das sichtbare Widget-Rechteck
 * erscheint pixelgenau im PNG.
 *   Linux (GTK3) : Client-Area ohne WM-Decorations, opak;
 *                  transparenter Hintergrund wenn die Oberflaeche alpha hat.
 *   Windows      : PW_RENDERFULLCONTENT liefert Client-Area opak;
 *                  GDI+/WIC schreibt 32-bit RGBA-PNG (Alpha = opak).
 *   macOS        : cacheDisplay in Rect liefert die View inkl. eigener
 *                  Transparenz; Fenster-Chrome wird nicht mit aufgenommen.
 *
 * Fehlerpfad (Rueckgabe MOO_BOOLEAN falsch): ungueltiges Handle, nicht
 * sichtbar, nicht realisiert, pfad nicht schreibbar, PNG-Encode-Fehler.
 * ========================================================================= */

/* Schreibt einen PNG-Screenshot eines kompletten Fensters nach `pfad`.
 *
 * fenster: Widget-Handle eines Top-Level-Fensters (wie von `moo_ui_fenster`).
 * pfad:    MOO_STRING mit absolutem oder relativem Dateipfad. Endung
 *          sollte `.png` sein; der Encoder schreibt unabhaengig davon
 *          immer PNG. Existiert die Datei, wird sie ueberschrieben.
 *
 * Aufnahmebereich: Fenster-Client-Area (siehe Hintergrund/Alpha oben).
 * Die Koordinaten im PNG entsprechen den Widget-Koordinaten aus
 * `moo_ui_widget_info` — damit ist die Sidecar-Bounding-Box-Auswertung
 * direkt auf das PNG anwendbar.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gdk_pixbuf_get_from_window(gdk_window, 0,0, b,h) +
 *                  gdk_pixbuf_save(pixbuf, pfad, "png", &err, NULL).
 *   Windows      : CreateCompatibleDC + CreateCompatibleBitmap auf HWND-
 *                  Client-Size; PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT);
 *                  WIC (IWICImagingFactory → PNG-Encoder) oder GDI+
 *                  (Gdiplus::Bitmap::Save mit image/png CLSID); alternativ
 *                  libpng falls kein WIC verfuegbar.
 *   macOS        : NSBitmapImageRep* rep =
 *                    [contentView bitmapImageRepForCachingDisplayInRect:bounds];
 *                  [contentView cacheDisplayInRect:bounds toBitmapImageRep:rep];
 *                  NSData* png =
 *                    [rep representationUsingType:NSBitmapImageFileTypePNG
 *                                      properties:@{}];
 *                  [png writeToFile:pfad atomically:YES].
 *
 * Liefert MOO_BOOLEAN: wahr bei Erfolg, falsch bei Fehler. */
MooValue moo_ui_test_snapshot(MooValue fenster, MooValue pfad);

/* Schreibt einen PNG-Screenshot eines einzelnen Widgets (inkl. seiner
 * Kinder, falls Container) nach `pfad`.
 *
 * widget: beliebiges Widget-Handle. Darf ein Fenster sein (dann Verhalten
 *         wie moo_ui_test_snapshot), ein Container (Rahmen, Tab, Scroll,
 *         Leinwand-Parent) oder ein Leaf-Widget.
 * pfad:   MOO_STRING wie bei moo_ui_test_snapshot.
 *
 * Aufnahmebereich: das Widget-Rechteck (b x h aus `moo_ui_widget_info`),
 * gerendert mit dem in diesem Moment sichtbaren Zustand. Ueberlappende
 * fremde Fenster sind Backend-abhaengig — das Ziel ist die logische
 * Widget-Darstellung, nicht der Screen-Compositor-Output.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_widget_get_window(w) + gtk_widget_get_allocation(w,&a)
 *                  → gdk_pixbuf_get_from_window(win, a.x,a.y, a.width,a.height)
 *                  + gdk_pixbuf_save(... "png" ...). Fuer off-screen-
 *                  bezogene Widgets (z.B. Leinwand) alternativ ueber
 *                  gtk_widget_draw(w, cairo_create(surface)) + PNG-Export.
 *   Windows      : HWND ermitteln, GetClientRect; CreateCompatibleDC +
 *                  CreateCompatibleBitmap auf diese Groesse;
 *                  PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT | PW_CLIENTONLY);
 *                  WIC/GDI+ PNG-Encode wie oben. Fuer Custom-Draw
 *                  (MooCanvas/STATIC) genuegt PrintWindow, weil der
 *                  Subclass-Proc WM_PRINTCLIENT weiterreicht.
 *   macOS        : NSView* v = widget_view; NSRect r = [v bounds];
 *                  NSBitmapImageRep* rep =
 *                    [v bitmapImageRepForCachingDisplayInRect:r];
 *                  [v cacheDisplayInRect:r toBitmapImageRep:rep];
 *                  [[rep representationUsingType:NSBitmapImageFileTypePNG
 *                                     properties:@{}]
 *                     writeToFile:pfad atomically:YES].
 *
 * Liefert MOO_BOOLEAN: wahr bei Erfolg, falsch bei Fehler. */
MooValue moo_ui_test_snapshot_widget(MooValue widget, MooValue pfad);

/* =========================================================================
 * Test-/Debug-API: Automation (Plan-004 P3)
 *
 * Programmatische Bedienung von UI-Elementen fuer Agenten-Tests und visuelle
 * Regressionstests. Zweck: nach einem Snapshot (P2) einen Zustand gezielt
 * aendern, naechsten Snapshot machen, Differenz bewerten. Diese API gehoert
 * bewusst zur Test-/Debug-Schicht (Namensraum `ui_test_*`), nicht zur
 * normalen App-API — Endnutzer-Anwendungen sollen sich nicht selbst
 * bedienen.
 *
 * Callback-Semantik (WICHTIG, gilt fuer alle Automation-Funktionen, die
 * einen Callback ausloesen):
 *   SYNCHRON. Der registrierte Callback wird im selben Stack noch vor
 *   Rueckkehr der test_*-Funktion aufgerufen. Damit ist nach Rueckkehr
 *   garantiert, dass alle unmittelbaren Zustaendsaenderungen geschehen
 *   sind (Variable gesetzt, Fenster-Titel aktualisiert, ...).
 *   Folge-Effekte, die das Backend ueber die Event-Queue verteilt
 *   (Relayout nach Groessenaenderung, deferred Redraw, Timer-Fires),
 *   sind NACH Rueckkehr noch nicht abgearbeitet — dafuer
 *   `moo_ui_test_pump` oder `moo_ui_test_warte` nutzen, bevor ein
 *   Snapshot aufgenommen wird.
 *
 * Sichtbarkeit/Aktivitaet: Alle Automation-Funktionen verlangen, dass
 * Fenster und Widget sichtbar und aktiv sind. Auf unsichtbaren oder
 * deaktivierten Widgets liefern sie `falsch` ohne Seiteneffekt.
 *
 * Fehlerpfad (Rueckgabe MOO_BOOLEAN falsch): ungueltiges Handle, nicht
 * sichtbar, nicht aktiv, Widget-Typ unterstuetzt die Operation nicht,
 * Koordinaten ausserhalb, Shortcut-Sequenz nicht parsebar.
 * ========================================================================= */

/* Programmatischer Klick auf ein klickbares Widget (Knopf, Checkbox,
 * Radio, Menue-Eintrag). Feuert den beim Widget registrierten Callback
 * SYNCHRON (on_click bei Knopf, toggle-Callback bei Checkbox/Radio,
 * activate bei Menue-Eintrag).
 *
 * widget: Widget-Handle eines klickbaren Widgets. Muss sichtbar und
 *         aktiv sein; Widget-Typ muss Klick unterstuetzen. Nicht-klickbare
 *         Widgets (Label, Rahmen, Leinwand ohne on_maus, ...) liefern falsch.
 *
 * Bei Checkbox/Radio wechselt zusaetzlich der sichtbare Zustand
 * (analog zum Benutzerklick), und der Callback erhaelt den neuen Wert.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_widget_activate(w). Aktiviert Button/Checkbox/
 *                  MenuItem genauso wie ein echter Klick und loest das
 *                  clicked/toggled/activate-Signal synchron aus.
 *   Windows      : SendMessageW(hwnd, BM_CLICK, 0, 0) fuer Buttons/
 *                  Checkboxes/Radios (synchroner Dispatch, feuert
 *                  BN_CLICKED an den Parent). Menue-Eintraege: via
 *                  WM_COMMAND mit der Control-ID. SendMessage (nicht
 *                  PostMessage), damit der Callback synchron feuert.
 *   macOS        : [control performClick:nil] auf NSButton/NSMenuItem;
 *                  triggert target/action synchron im Main-Thread.
 *
 * Liefert MOO_BOOLEAN: wahr bei Erfolg, falsch bei Fehler. */
MooValue moo_ui_test_klick(MooValue widget);

/* Simulierter Klick an Fenster-Content-lokalen Koordinaten. Sucht via
 * Hit-Test das Widget an (x,y) und delegiert an moo_ui_test_klick.
 * Nuetzlich fuer Leinwand-Klicks (loest on_maus-Callback aus, wenn
 * registriert) oder wenn auf dem Ziel-Widget keine ID gesetzt ist.
 *
 * fenster: Fenster-Handle (Top-Level).
 * x, y:    MOO_INTEGER, Pixel, relativ zur Fenster-Content-Area
 *          (origin links-oben), konsistent mit Widget-Koordinaten
 *          aus `moo_ui_widget_info`.
 *
 * Koordinaten ausserhalb der Content-Area oder auf einem nicht-klickbaren
 * Widget → falsch. Bei Leinwand mit on_maus wird stattdessen der
 * on_maus-Callback SYNCHRON mit (leinwand, lokal_x, lokal_y, taste=1)
 * aufgerufen; Koordinaten werden Leinwand-lokal umgerechnet.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : gtk_widget_get_window(fenster) + gdk_window_get_device_position
 *                  Heuristik via rekursivem gtk_widget_get_allocation-Walk;
 *                  gefundenes Ziel → gtk_widget_activate (Buttons) oder
 *                  synthetisches "button-press-event" (Leinwand).
 *   Windows      : ChildWindowFromPointEx(hwnd, {x,y}, CWP_SKIPINVISIBLE|
 *                  CWP_SKIPDISABLED); danach BM_CLICK oder
 *                  WM_LBUTTONDOWN/WM_LBUTTONUP an Ziel-HWND mit
 *                  client-lokalen Koordinaten.
 *   macOS        : [contentView hitTest:NSPoint] → NSView; bei NSControl
 *                  [view performClick:nil], bei Custom-View mouseDown:
 *                  mit synthetischem NSEvent.
 *
 * Klick-Taste: immer links (1). Fuer mittlere/rechte Taste spaeter
 * eine eigene Variante, falls benoetigt.
 *
 * Liefert MOO_BOOLEAN: wahr bei Erfolg, falsch bei Fehler. */
MooValue moo_ui_test_klick_xy(MooValue fenster, MooValue x, MooValue y);

/* Setzt den Text-Inhalt eines Text-Eingabe-Widgets programmatisch und
 * feuert den on_change-Callback SYNCHRON (falls gebunden).
 *
 * widget: Eingabe, Textbereich, oder Dropdown (bei Dropdown: sucht den
 *         Eintrag mit exakt passendem Text und setzt die Auswahl; nicht
 *         gefundener Text → falsch, keine Aenderung).
 * text:   MOO_STRING. Leerer String leert das Feld. MOO_NONE → falsch.
 *
 * Ersetzt den gesamten bisherigen Inhalt (kein Append). Der on_change-
 * Callback erhaelt den neuen Text. Beim Textbereich wird genau ein
 * on_change-Event gefeuert, nicht pro Zeichen.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : Eingabe: gtk_entry_set_text(entry, utf8);
 *                  g_signal_emit_by_name(entry, "changed") ist implizit,
 *                  weil set_text das "changed"-Signal nativ feuert.
 *                  Textbereich: gtk_text_buffer_set_text(buf, utf8, -1);
 *                  Buffer emittiert "changed" synchron.
 *                  Dropdown: gtk_combo_box_set_active(cb, index) nach
 *                  Text-Suche in der Model-Liste.
 *   Windows      : Eingabe/Textbereich: SetWindowTextW(hwnd, wtext);
 *                  danach SendMessageW(parent, WM_COMMAND,
 *                  MAKEWPARAM(id, EN_CHANGE), (LPARAM)hwnd) explizit
 *                  posten, weil SetWindowText EN_CHANGE auf Controls
 *                  ohne Parent-Notify-Flag schluckt.
 *                  Dropdown: CB_FINDSTRINGEXACT + CB_SETCURSEL;
 *                  CBN_SELCHANGE via WM_COMMAND.
 *   macOS        : Eingabe: [field setStringValue:ns]; NSTextField sendet
 *                  nicht automatisch controlTextDidChange: bei setValue —
 *                  daher explizit: [[NSNotificationCenter defaultCenter]
 *                  postNotificationName:NSControlTextDidChangeNotification
 *                  object:field].
 *                  Textbereich: [[textView textStorage]
 *                  replaceCharactersInRange:all withString:ns] +
 *                  textDidChange:.
 *                  Dropdown: [popup selectItemWithTitle:ns] + action senden.
 *
 * Liefert MOO_BOOLEAN: wahr bei Erfolg, falsch bei Fehler. */
MooValue moo_ui_test_text_setze(MooValue widget, MooValue text);

/* Simuliert einen Keyboard-Shortcut und feuert den via
 * `moo_ui_shortcut_bind` registrierten Callback SYNCHRON.
 *
 * fenster:  Fenster-Handle, in dessen Shortcut-Tabelle gesucht wird.
 * sequenz:  MOO_STRING im gleichen Format wie `moo_ui_shortcut_bind`
 *           (z.B. "Ctrl+S", "Strg+Z", "F11"). Case-insensitive,
 *           "+"-getrennt, gleiche Modifier-/Key-Token-Tabelle.
 *
 * Liefert falsch wenn:
 *   - sequenz nicht parsebar,
 *   - keine aktive Bindung fuer diese Sequenz im Fenster existiert,
 *   - Fenster nicht sichtbar.
 *
 * Die Implementierung nutzt den bestehenden Shortcut-Parser aus
 * `moo_ui_shortcut_bind` (gleiche Token-Tabelle, gleiche Normalisierung),
 * sucht den registrierten Callback im Fenster-Accel-Table und ruft
 * ihn direkt auf — es werden KEINE synthetischen OS-Keyboard-Events
 * an den Event-Queue geschickt. Damit ist die Ausloesung deterministisch
 * und unabhaengig vom aktuellen Fokus-Widget.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : interne Shortcut-Map (sequenz → callback) lookup +
 *                  moo_call(callback). Kein gtk_main_do_event, weil das
 *                  Key-Events ueber Fokus-Pfad dispatcht und Child-
 *                  Widgets die Keys abfangen koennten.
 *   Windows      : interne ACCEL-Map-Lookup + synchroner Callback-Call.
 *                  Kein PostMessage(WM_HOTKEY/WM_KEYDOWN).
 *   macOS        : interne Map-Lookup + synchroner Callback-Call.
 *                  Kein NSEvent-Injection in die Event-Queue.
 *
 * Liefert MOO_BOOLEAN: wahr bei Erfolg, falsch bei Fehler. */
MooValue moo_ui_test_shortcut(MooValue fenster, MooValue sequenz);

/* Blockiert fuer `millisekunden` und pumpt waehrenddessen den UI-Event-
 * Loop weiter, damit Redraws, Timer und deferred Signale abgearbeitet
 * werden. KEIN `sleep` und KEIN busy-wait ohne Pump — Tests, die nach
 * einem Zustandswechsel einen Snapshot machen wollen, sollen hier auf
 * das Relayout warten koennen.
 *
 * millisekunden: MOO_INTEGER >= 0. Werte <= 0 liefern wahr und pumpen
 *                einmal (entspricht `moo_ui_test_pump`).
 *
 * Die Funktion kehrt frueher zurueck, falls `moo_ui_beenden` waehrend
 * der Wartezeit ausgeloest wird.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : Schleife bis (now - start) >= ms:
 *                    while (gtk_events_pending()) gtk_main_iteration();
 *                    g_usleep(1000); /\* 1 ms, keine CPU-Starre *\/
 *   Windows      : Schleife bis GetTickCount64-Delta >= ms:
 *                    while (PeekMessageW(&msg, NULL, 0,0, PM_REMOVE)) {
 *                      TranslateMessage(&msg); DispatchMessageW(&msg);
 *                    }
 *                    Sleep(1);
 *   macOS        : [[NSRunLoop currentRunLoop]
 *                     runUntilDate:[NSDate dateWithTimeIntervalSinceNow:
 *                                   ms/1000.0]];
 *                  (bzw. Schleife mit kurzen Intervallen, damit
 *                  moo_ui_beenden-Abbruch greift.)
 *
 * Liefert MOO_BOOLEAN: wahr bei planmaessigem Ende, falsch bei Fehler
 * (negativer Nichtsinn-Wert als MOO_STRING etc.). */
MooValue moo_ui_test_warte(MooValue millisekunden);

/* Verarbeitet alle aktuell in der UI-Event-Queue wartenden Events
 * genau einmal und kehrt sofort zurueck. Anders als `moo_ui_test_warte`
 * wird NICHT zusaetzlich gewartet — nur der aktuelle Queue-Inhalt wird
 * abgearbeitet.
 *
 * Typischer Einsatz: direkt nach `moo_ui_zeige` einmal pumpen, damit
 * das Fenster realisiert ist und anschliessend ein Snapshot aufgenommen
 * werden kann. Fuer Relayout nach Groessenaenderung reicht ein Pump
 * i.d.R. nicht — dann `moo_ui_test_warte(16)` oder mehr nutzen, damit
 * der Backend-eigene Frame-Tick durchlaeuft.
 *
 * Backend-Mapping:
 *   Linux (GTK3) : while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
 *                  FALSE = nicht blockieren, wenn Queue leer wird.
 *   Windows      : while (PeekMessageW(&msg, NULL, 0,0, PM_REMOVE)) {
 *                    TranslateMessage(&msg); DispatchMessageW(&msg);
 *                  }
 *   macOS        : NSEvent* e;
 *                  while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny
 *                               untilDate:[NSDate distantPast]
 *                               inMode:NSDefaultRunLoopMode dequeue:YES]))
 *                  {
 *                    [NSApp sendEvent:e];
 *                  }
 *
 * Hinweis: `moo_ui_test_pump` ist semantisch aequivalent zum bestehenden
 * `moo_ui_pump`, wird aber unter dem `ui_test_*`-Namensraum zweitexponiert,
 * damit Test-Skripte konsistent bleiben und `ui_test`-Modul eigenstaendig
 * nutzbar ist, ohne die normale UI-API zu importieren.
 *
 * Liefert MOO_BOOLEAN: wahr wenn mindestens ein Event verarbeitet wurde
 * oder die Queue leer war (also normaler Verlauf), falsch nur bei
 * Backend-internen Fehlern (z.B. UI-Subsystem nicht initialisiert). */
MooValue moo_ui_test_pump(void);

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
