# ui_moo — moo-eigene Widgets (Schicht 3)

`ui_moo` ist das plattformübergreifend identische Design-/Widget-System von moo:
Widgets, Theme, Fokus, Hit-Testing und Eventlogik sind **vollständig in moo**
implementiert (`stdlib/ui_moo.moo`). Es setzt **keine nativen Controls** voraus —
der Host liefert nur Fenster/Zeichenfläche/Eingaben. `ui_moo` ist optional:
die nativen UI-APIs (`ui_*`, Schicht 1/2) bleiben der direkte Weg zu
GTK/Win32/Cocoa und werden davon nicht berührt.

```moo
importiere ui
importiere ui_moo

setze k auf uim_wurzel(fenster, 10, 10, 580, 400)
uim_hinzu(k, uim_knopf("Speichern", 20, 20, 130, 32, auf_speichern))
```

Demo: `beispiele/ui_moo_demo.moo` (Desktop-Schaufenster mit Theme-Umschalter),
`beispiele/ui_moo_spiel_overlay.moo` (Pausemenü als In-Game-Overlay).

## Backend-Vertrag

`ui_moo` ist in **Kern** und **Backends** getrennt. Der Kern
(`_uim_kern_maus/maus_los/bewegung/taste/rad/zeichne` + Widget-Tree, Theme,
Fokus) kennt keine Plattform. Ein Backend muss genau zwei Dinge liefern:

1. **Zeichenziel** mit diesen Primitiven (über `_uim_zf_*` angebunden):
   Rechteck (gefüllt/Umriss), Kreis (gefüllt), Linie (mit Breite),
   Text + Textbreite, optional Clip-Rechteck (Scissor) und abgerundete
   Rechtecke. Fehlt ein optionales Primitiv, degradiert die Optik
   dokumentiert (siehe Frame-Backend).
2. **Ereignis-Einspeisung** in den Kern: Maus-Press/-Release/-Bewegung
   (Leinwand-lokale Pixel), Tastenname + gedrückt + Modifier-Bitmaske
   (1=Shift, 2=Strg, 4=Alt), Rad-Delta (+1 = nach unten).

Vorhandene Backends:

| Backend    | Zeichenziel                          | Events                          |
|------------|--------------------------------------|---------------------------------|
| `leinwand` | `ui_leinwand` + `ui_zeichne_*` (GTK/Win32/Cocoa-Host) | automatisch über `ui_leinwand_on_*` |
| `frame`    | 2D-Spiel-API (`zeichne_rechteck/kreis/linie`, 3x5-Pixelfont) | Polling: `uim_frame_maus/taste/rad` |

Ein künftiges Moo-OS-/Grafikbackend ist Backend Nr. 3 im selben Vertrag —
der Kern bleibt unverändert.

## Öffentliche API

### Kontext

| Funktion | Beschreibung |
|---|---|
| `uim_wurzel(fenster, x, y, b, h)` | Desktop: legt Leinwand an, bindet alle Events, liefert Kontext |
| `uim_frame_wurzel(b, h)` | Game: Kontext fürs Frame-Backend (Overlay, Hintergrund aus) |
| `uim_hinzu(ziel, widget)` | Widget an Kontext-Wurzel ODER Container anhängen |
| `uim_theme_setze(kontext, theme)` | Theme wechseln + neu zeichnen |
| `uim_theme_dunkel()` / `uim_theme_hell()` | eingebaute Themes |
| `uim_neuzeichnen(kontext)` | Repaint anfordern (Leinwand-Backend) |
| `uim_finde(kontext, id)` | Widget per String-ID (`w["id"]`) |

### Widgets

| Funktion | Verhalten |
|---|---|
| `uim_knopf(text, x, y, b, h, on_klick)` | Klick-auf-Release, Hover/Druck/Disabled, Fokusring, `on_klick(w)` |
| `uim_label(text, x, y, b, h)` | Textzeile |
| `uim_checkbox(text, x, y, b, h, initial, on_wechsel)` | Klick/space/Return toggelt, `on_wechsel(w, wert)` |
| `uim_slider(x, y, b, h, min, max, start, on_wechsel)` | Press setzt Wert, Drag zieht, Pfeiltasten 1/20-Schritt |
| `uim_fortschritt(x, y, b, h)` + `uim_fortschritt_setze(k, w, wert01)` | passiv, geklemmt 0..1 |
| `uim_slider_setze(kontext, w, wert)` | programmatisch (geklemmt) |
| `uim_panel(titel, x, y, b, h)` | Container, Kind-Koordinaten relativ |
| `uim_scroll(x, y, b, h, inhalt_hoehe)` | Clip + proportionale Scrollbar, Rad scrollt |
| `uim_liste(x, y, b, h, zeilen, on_auswahl)` | virtuelles Rendering, Klick/Up/Down, `on_auswahl(w, index)` |

### Frame-Backend (In-Game-UI)

Pro Spiel-Tick:

```moo
uim_frame_maus(hud, maus_x(win), maus_y(win), maus_gedrückt(win))
uim_frame_zeichne(hud, win)
```

`uim_frame_maus` erkennt Press-/Release-Flanken selbst;
`uim_frame_taste(k, taste, gedrückt, mod)` und `uim_frame_rad(k, x, y, delta)`
speisen Tastatur/Rad ein.

## Theme

Theme = Dict; die Widget-Zeichner enthalten **keinen** hartkodierten Wert.
Schlüssel: `hintergrund, flaeche, flaeche_hover, flaeche_druck, rand, akzent,
text, text_gedimmt` (je `[r,g,b,a]` 0..255) sowie `radius, schrift, abstand`.
Eigene Themes: Dict mit denselben Schlüsseln an `uim_theme_setze`.

## Widget-Dict

Widgets sind offene Dicts: `typ, uid, id, x, y, b, h, sichtbar, aktiv,
fokussierbar, text, hover, druck, kinder, on_klick, on_wechsel, wert, min,
max` (+ typ-spezifisch `zeilen, auswahl, scroll_y, inhalt_hoehe`).
Eigene Widget-Typen: Dict mit eigenem `typ` bauen, Zeichnen/Verhalten über
die `_uim_zf_*`-Primitive — kein C-Rebuild nötig.

## Einschränkungen (V1, dokumentiert)

- Tab-Fokus läuft nur über Top-Level-Widgets (Klick-Fokus überall).
- Scrollbar per Rad, nicht draggbar; kein horizontales Scrollen.
- Frame-Backend: kein Scissor (Listen-Overdraw ≤ 1 Zeile durch virtuelles
  Rendering), Rounded-Rects eckig, Outline-Kreise entfallen, Text nur
  3x5-Pixelfont (A–Z, 0–9, `.:-?!`).
- Kein Custom-Textfeld: Texteingabe (IME/Selektion) bleibt bewusst nativ
  (`ui_eingabe`), ebenso Dateidialoge, Menüleiste, Tray.

## Tests

`beispiele/tests/ui_moo_kern_test.moo` (Dispatch/Fokus),
`ui_moo_widgets_test.moo` (Checkbox/Slider/Klemmung),
`ui_moo_container_test.moo` (Panel/Liste/Scroll-Transformation),
`ui_moo_visuell_test.moo` (Snapshot-Frames),
`ui_moo_frame_test.moo` (Pixel-Regression via `test_frame_diff/region`).
