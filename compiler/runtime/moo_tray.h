#ifndef MOO_TRAY_H
#define MOO_TRAY_H

/*
 * moo_tray.h — Oeffentliche, OS-neutrale Tray-API fuer moo.
 * ========================================================
 *
 * Plan-Referenz: Memory `plan-002-moo-ui-cross-platform` — Abschnitt
 * "Tray-Roadmap".
 *
 * Backends:
 *   Linux   — libappindicator3 / libdbusmenu-glib (moo_tray_linux.c)
 *   Windows — Shell_NotifyIcon      (moo_tray_win32.c, Phase 3)
 *   macOS   — NSStatusItem          (moo_tray_cocoa.m, Phase 4)
 *
 * Event-Loop:
 *   Tray-Icons teilen sich den globalen Event-Loop mit Fenstern
 *   (siehe moo_ui_laufen()). Das alte `moo_tray_run()` wird deprecated
 *   und ist nur noch Alias auf moo_ui_laufen() — darf bestehende moo-
 *   Programme nicht brechen, aber Neu-Code sollte moo_ui_laufen()
 *   benutzen.
 *
 * Handles:
 *   Tray, Menu, Menu-Item und Check-Item sind alle MooValue-Handles mit
 *   tag=MOO_NUMBER und gepacktem Native-Pointer in data.
 *
 * Ownership:
 *   Callback-MooValues (MOO_FUNC) werden intern retain-t und via
 *   GClosureNotify/DestroyNotify/WM_DESTROY/dealloc released — siehe
 *   Kommentar in moo_tray_linux.c (cb_box_destroy).
 */

#include "moo_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Tray-Icon: Erstellen, Titel/Icon aendern
 * ========================================================================= */

MooValue moo_tray_create(MooValue titel, MooValue icon_name);
MooValue moo_tray_titel_setze(MooValue tray, MooValue titel);
MooValue moo_tray_icon_setze(MooValue tray, MooValue icon_name);
MooValue moo_tray_aktiv(MooValue tray, MooValue aktiv);

/* =========================================================================
 * Menue (flach)
 * ========================================================================= */

/* Haengt Item an das Haupt-Menue des Trays. */
MooValue moo_tray_menu_add(MooValue tray, MooValue label, MooValue callback);

/* Haengt Trenner an das Haupt-Menue. */
MooValue moo_tray_separator_add(MooValue tray);

/* Loescht alle Items (und released alle Callback-Refs) und
 * re-initialisiert das Menue. */
MooValue moo_tray_menu_clear(MooValue tray);

/* =========================================================================
 * Submenues (NEU, aus Thought a7a84a60)
 * ========================================================================= */

/* Liefert Handle auf ein Submenu, das man mit moo_tray_menu_add_to befuellt. */
MooValue moo_tray_submenu_add(MooValue tray, MooValue label);

/* Haengt Item an ein Submenu (statt an das Haupt-Menue). */
MooValue moo_tray_menu_add_to(MooValue submenu, MooValue label, MooValue callback);

/* Haengt Trenner an ein Submenu. */
MooValue moo_tray_separator_add_to(MooValue submenu);

/* =========================================================================
 * Check-Items (NEU)
 * ========================================================================= */

/* Gecheckte Menu-Items (mit Haekchen). Liefert Item-Handle. */
MooValue moo_tray_check_add(MooValue tray, MooValue label, MooValue initial,
                            MooValue callback);
MooValue moo_tray_check_add_to(MooValue submenu, MooValue label,
                               MooValue initial, MooValue callback);
MooValue moo_tray_check_wert(MooValue check_item);
MooValue moo_tray_check_set(MooValue check_item, MooValue wert);

/* =========================================================================
 * Item-Manipulation
 * ========================================================================= */

MooValue moo_tray_item_aktiv(MooValue item, MooValue aktiv);
MooValue moo_tray_item_label_setze(MooValue item, MooValue label);

/* =========================================================================
 * Timer (wie bisher) — nutzt den globalen Event-Loop
 * ========================================================================= */

MooValue moo_tray_timer_add(MooValue interval_ms, MooValue callback);
MooValue moo_tray_timer_remove(MooValue timer_id);

/* =========================================================================
 * Event-Loop (Legacy-Wrapper)
 *
 * DEPRECATED: Benutze stattdessen moo_ui_laufen(). moo_tray_run bleibt
 * nur als Alias fuer Abwaerts-Kompatibilitaet bis Phase 2 bestehen.
 * ========================================================================= */

MooValue moo_tray_run(void);

#ifdef __cplusplus
}
#endif

#endif /* MOO_TRAY_H */
