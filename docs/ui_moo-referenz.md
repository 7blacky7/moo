# ui_moo — Referenz (Schicht 3: moo-eigene gezeichnete Widgets)

Stand: 2026-07-11, UIMOO-1 bis -7. Modul: `stdlib/ui_moo.moos`. Design-Memo: Synapse-Memory `plan-uimoo-widget-toolkit`.

ui_moo ist ein Widget-Toolkit, das **vollständig in moo implementiert** ist: Widget-Baum, Layout, Theme, Fokus, Hit-Testing und Eventlogik leben in `stdlib/ui_moo.moos`. Die Plattform (GTK/Win32/Cocoa) liefert nur eine austauschbare **Host-Brücke**: Fenster + Leinwand + Eingabe-Events + Zeichenprimitive. ui_moo setzt **keine** nativen Controls (GtkButton, Win32-BUTTON, NSButton …) voraus.

ui_moo ist ein **optionaler zweiter UI-Weg**. Der native Weg (`importiere ui`, Schicht 1/2) bleibt vollwertig erhalten — für Texteingabe (IME), Dateidialoge, Menüleiste und Tray ist er weiterhin der richtige Werkzeugkasten (bewusster Hybrid-Schnitt).

## Nutzung (Desktop)

```moolang
importiere ui
importiere ui_moo

funktion auf_klick(w):
    gib_zurück wahr

setze fenster auf ui_fenster("Demo", 640, 480, 0, nichts)
setze k auf uim_wurzel(fenster, 10, 10, 620, 460)
uim_hinzu(k, uim_knopf("Klick mich", 20, 20, 140, 32, auf_klick))
ui_zeige_nebenbei(fenster)
ui_laufen()
```

Vollständiges Schaufenster: `beispiele/ui_moo_demo.moos`.

## Backend-Vertrag

Der Kern (`_uim_kern_*`) ist backend-neutral. Ein Backend muss liefern:

| Richtung | Vertrag |
|---|---|
| Zeichnen | Rechteck (gefüllt/Umriss), abgerundetes Rechteck, Kreis, Linie (Breite), Text + Textbreiten-Metrik, optional Clip-Rechteck (Stack) |
| Ereignisse | Maus-Press/-Release/-Bewegung, Rad-Delta, Taste (Name, gedrückt, Modifier-Bitmaske 1=Shift/2=Strg/4=Alt), Fokus |
| Farben | RGBA 0..255 (Theme-Dict), backend-intern konvertierbar |

Vorhandene Backends:

1. **`leinwand`** (Desktop): `ui_leinwand` + `ui_zeichne_*` + `ui_leinwand_on_*` (GTK implementiert; Win32/Cocoa blind portiert, Runner-Verifikation offen — siehe unten).
2. **`frame`** (In-Game): 2D-Spiel-API (`zeichne_rechteck/kreis/linie`), Farben als `#RRGGBB`, Text über eingebauten 3x5-Pixelfont, Events per Polling. Einschränkungen: kein Clip/Scissor (virtuelles Listen-Rendering begrenzt Overdraw auf ≤ 1 Zeile), Rounded-Rects eckig, Umriss-Kreise entfallen.
3. **Zukunft**: ein Moo-OS-/eigenes Grafikbackend erfüllt denselben Vertrag als Backend Nr. 3 — ohne Änderung am Widget-Code.

## Nutzung (In-Game-Overlay, Frame-Backend)

```moolang
importiere ui_moo

setze hud auf uim_frame_wurzel(900, 700)
uim_hinzu(hud, uim_knopf("WEITER", 350, 300, 200, 40, auf_weiter))

# im Spiel-Loop, jeden Tick:
uim_frame_maus(hud, maus_x, maus_y, maus_gedrueckt)   # Flanken-Erkennung intern
uim_frame_zeichne(hud, win)                            # Overlay (Hintergrund aus)
```

Referenz-Testfall mit Pixel-Regression: `beispiele/tests/ui_moo_frame_test.moos`.

## Öffentliche API

### Kontext

| Funktion | Beschreibung |
|---|---|
| `uim_wurzel(fenster, x, y, b, h)` | Legt Leinwand an, bindet alle Events, liefert Kontext (Backend `leinwand`) |
| `uim_frame_wurzel(b, h)` | Kontext fürs Frame-Backend (kein Fenster, Overlay-Modus) |
| `uim_hinzu(ziel, widget)` | Widget an Kontext-Wurzel ODER Container anhängen; liefert das Widget |
| `uim_theme_setze(kontext, theme)` | Theme wechseln + neu zeichnen |
| `uim_theme_dunkel()` / `uim_theme_hell()` | Theme-Dicts |
| `uim_neuzeichnen(kontext)` | Repaint anfordern (Frame-Backend: no-op) |
| `uim_finde(kontext, id)` | Top-Level-Widget per String-ID (`w["id"]` setzen) |
| `uim_frame_zeichne(kontext, win)` | Frame-Backend: Baum pro Tick zeichnen |
| `uim_frame_maus(kontext, x, y, gedrueckt)` | Maus-Polling (erzeugt Press/Release/Bewegung) |
| `uim_frame_taste(kontext, taste, gedrueckt, mod)` | Tasten-Ereignis einspeisen |
| `uim_frame_rad(kontext, x, y, delta)` | Rad-Ereignis einspeisen |

### Widgets

| Konstruktor | Verhalten |
|---|---|
| `uim_knopf(text, x, y, b, h, on_klick)` | Hover/Druck/Disabled, Fokusring, Klick-auf-Release, space/Return |
| `uim_label(text, x, y, b, h)` | `uim_label_ausrichtung(w, "links"\|"mitte"\|"rechts")` |
| `uim_checkbox(text, x, y, b, h, initial, on_wechsel)` | Toggle per Klick/space/Return; `on_wechsel(w, wert)` |
| `uim_slider(x, y, b, h, min, max, start, on_wechsel)` | Press setzt Wert, Drag, Pfeiltasten (1/20-Schritt); `on_wechsel(w, wert)` |
| `uim_fortschritt(x, y, b, h)` | passiv; `uim_fortschritt_setze(kontext, w, wert01)` |
| `uim_slider_setze(kontext, w, wert)` | programmatisch mit Klemmung |
| `uim_panel(titel, x, y, b, h)` | Container, Kind-Koordinaten relativ; `uim_hinzu(panel, w)` |
| `uim_scroll(x, y, b, h, inhalt_hoehe)` | Clip + proportionale Scrollbar + Mausrad |
| `uim_liste(x, y, b, h, zeilen, on_auswahl)` | virtuelles Rendering, Auswahl per Klick/Up/Down, Rad; `on_auswahl(w, index)` |

### Widget-Dict (direkt manipulierbar, danach `uim_neuzeichnen`)

`typ, uid, id, x, y, b, h, sichtbar, aktiv, fokussierbar, text, hover, druck, kinder, wert, min, max, on_klick, on_wechsel, on_auswahl, ausrichtung, scroll_y, zeilen, auswahl, inhalt_hoehe`

### Theme-Dict

`hintergrund, flaeche, flaeche_hover, flaeche_druck, rand, akzent, text, text_gedimmt` (je `[r,g,b,a]` 0..255) + `radius, schrift, abstand`. Die Widget-Zeichner enthalten **keinen** Wert außerhalb des Themes.

## Tests

| Test | Deckt ab |
|---|---|
| `beispiele/tests/ui_moo_kern_test.moos` | Klick-Dispatch, Fokus, Theme-Wechsel, `uim_finde` |
| `beispiele/tests/ui_moo_widgets_test.moos` | Checkbox-Toggle, Slider-Klick-Positionierung, Klemmung |
| `beispiele/tests/ui_moo_container_test.moos` | Panel-Transformation, Listen-Scroll-Zuordnung, Scroll-Treffbarkeit |
| `beispiele/tests/ui_moo_visuell_test.moos` | 4 Zustands-Snapshots (JSON+PNG) unter `beispiele/snapshots/ui_moo/` |
| `beispiele/tests/ui_moo_frame_test.moos` | Frame-Backend inkl. echter Pixel-Regression (`test_frame_diff/region`) |

## Bekannte V1-Einschränkungen

- Tab-Fokus läuft nur über Top-Level-Widgets (Klick-Fokus funktioniert überall).
- Scrollbar ist nicht draggbar (Mausrad/Tasten), kein horizontales Scrollen.
- Kein Custom-Textfeld — Texteingabe bewusst nativ (`ui_eingabe`, IME/Selektion/Clipboard).
- Win32/Cocoa-Leinwand-Primitive (UIMOO-1) sind blind portiert; Runner-Verifikation hängt zusätzlich hinter den bekannten Win32-Suite-Bugs (Kanal-Befund #13099: table/tray-Hang, PrintWindow-Staleness).
- Frame-Backend: siehe Backend-Vertrag (kein Clip, eckige Rounded-Rects, Pixelfont).

## Sprach-Gotchas beim Arbeiten an ui_moo

Siehe Synapse-Memory `moo-gotcha-keywords-und-stdlib-muster`: `neu`, `an`, `aus`, `sl` sind Schlüsselwörter; Print ist das Statement `zeige`; Modul-Zustand gehört in Dicts (globale Skalare sind aus Funktionen nicht neu zuweisbar); Dict-gespeicherte Callbacks erst in eine lokale Variable holen, dann aufrufen.
