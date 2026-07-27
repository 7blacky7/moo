# ui_moo RGBA-Surface

P016-O1 ergänzt ein fensterloses RGBA8888-Backend für denselben Widget- und Eingabekern, den auch die Desktop- und Game-Adapter verwenden. Es öffnet kein Fenster und benötigt weder GTK, SDL, Win32 noch Cocoa.

## Module

| Modul | Aufgabe | Host-Abhängigkeit |
|---|---|---|
| `ui_moo_kern` | Themes, Widgetbaum, Zeichnung über Backendvertrag, normalisierte Eingabe, Mock | keine |
| `ui_moo_host` | bestehende Leinwand- und Game-/Frame-Adapter | `ui` und Hostprimitive |
| `ui_moo_surface` | mutable RGBA-Offscreen-Surface | nur Surface-Builtins |
| `ui_moo` | kompatibles Umbrella mit allen drei Schichten | enthält Hostadapter |

Für reine Headless-Arbeit und den gehosteten Moo-OS-Referenzpfad direkt `ui_moo_surface` importieren. Dadurch wird `ui` nicht importiert.

## Schnellstart

```moo
importiere ui_moo_surface

setze k auf uim_surface_wurzel(160, 120)
uim_hinzu(k, uim_label("A", 4, 4, 20, 12))
uim_hinzu(k, uim_knopf("OK", 20, 20, 64, 24, nichts))

uim_surface_zeichne(k)
zeige uim_surface_hash(k)

setze pixel auf uim_surface_pixel(k, 6, 4)
setze frame auf uim_surface_snapshot(k)
test_frame_save_bmp(frame, "/tmp/ui_moo_surface.bmp")
```

Der Snapshot ist eine tiefe `MOO_FRAME`-Momentaufnahme. BMP wird absichtlich über die bestehende Frame-API geschrieben; es gibt kein zweites Surface-BMP-Builtin.

## Bare-Kernel-Grenze

`moo_surface_core.c` ist freestanding, hat keine undefinierten Symbole und wird von der Bare-Kernel-Pipeline mitgebaut. Die öffentlichen `surface_*`-MooValue-Wrapper setzen derzeit jedoch die gehostete Runtime mit Heapwerten, Dicts, Strings und `MOO_FRAME` voraus. Sie sind deshalb noch nicht unter `--kernel` linkbar. Buffer-Ownership, Bare-MooValue-Adapter und Presenter/Compositor folgen in P016-O3; O1 liefert dafür den deterministischen Pixelkern und das gehostete Referenzbackend.

## Öffentliche Surface-Builtins

Der Compiler exponiert ausschließlich diesen kanonischen Satz:

- `surface_new(b, h)`
- `surface_clear(surface, r, g, b, a)`
- `surface_clip_push(surface, x, y, b, h)`
- `surface_clip_pop(surface)`
- `surface_rect(surface, x, y, b, h, r, g, b, a)`
- `surface_roundrect(surface, x, y, b, h, radius, r, g, b, a)`
- `surface_circle(surface, cx, cy, radius, r, g, b, a)`
- `surface_line(surface, x0, y0, x1, y1, r, g, b, a)`
- `surface_read_pixel(surface, x, y)`
- `surface_text(surface, x, y, skala, text, r, g, b, a)`
- `surface_text_breite(text, skala)`
- `surface_text_stil(surface, x, y, skala, text, stil, r, g, b, a)`
- `surface_text_breite_stil(text, skala, stil)`
- `surface_text_glyphen_status(text, stil)`
- `surface_hash(surface)`
- `surface_snapshot_to_frame(surface)`

Mutatoren liefern Bool. Ungültige Konstruktorparameter liefern `nichts`; ungültige Pixelkoordinaten liefern `nichts`. Handles werden nur geliehen und von den Builtins nicht freigegeben.

Die stilfähigen Text-Builtins verwenden diese festen IDs:

| ID | Schnitt |
|---:|---|
| `0` | normal |
| `1` | fett |
| `2` | kursiv |
| `3` | fett + kursiv |
| `4` | monospace |

`surface_text_stil(...)` liefert einen Wahrheitswert und bei jedem ungültigen Argument `falsch`. `surface_text_breite_stil(...)` liefert die Breite als Zahl; `surface_text_glyphen_status(...)` liefert `{vollstaendig, ungueltig_utf8, fehlende_glyphen}`. Messen und Status liefern bei ungültigen Argumenten `nichts` — ausdrücklich nicht `0`.

Rendern, Messen und Status verwenden denselben UTF-8-Dekoder. Ungültige Bytefolgen ergeben je längster gültiger Teilfolge genau ein Ersatzzeichen gemäß Unicode-Tabelle 3-7. Fehlende Glyphen werden ebenfalls als `?` gerendert und mit dessen Vorschub gemessen.

Wichtig für Metriktests: Der kursive Schnitt hat dieselben Vorschübe wie normal. Ein Test nur mit kursivem Text kann daher eine stilblinde Breitenmessung nicht erkennen; dafür fett oder monospace verwenden.

## ui_moo-Adapter

- `uim_surface_wurzel(b, h)`
- `uim_surface_zeichne(k)`
- `uim_surface_handle(k)`
- `uim_surface_leeren(k, r, g, b, a)`
- `uim_surface_hash(k)`
- `uim_surface_pixel(k, x, y)`
- `uim_surface_snapshot(k)`
- `uim_surface_maus/bewegung/taste/rad/fokus(...)`

Die Surface-Dimension ist nach der Erstellung fest. Für eine andere Größe wird eine neue Wurzel erzeugt.

## Raster- und Textvertrag

- RGBA8888, top-left, straight alpha.
- Rechtecke sind half-open.
- Clips werden verschachtelt und jeweils mit dem aktuellen Clip geschnitten.
- Schlägt ein Clip-Push oder -Pop fehl, liefert der Widgetrenderer `falsch`; ein fehlgeschlagener Push zeichnet keine Kinder und führt kein Pop aus. Der Surface-Kontext bleibt dabei `ungueltig` (fail-closed).
- Farben werden per ganzzahligem source-over komponiert.
- Gefüllte abgerundete Rechtecke bleiben über `surface_roundrect` und den Surface-Adapter verfügbar. Ungefüllte Varianten degradieren derzeit zu einem eckigen Umriss; deshalb meldet das Backend die Capability `rechteck_rund = falsch`.
- Text wird deterministisch aus fünf eingebetteten, antialiasierten DejaVu-Sans-Schnitten in der C-Surface gerastert; es gibt keinen Systemfont- oder Laufzeit-Fallback.
- Der Surface-Adapter reicht Zeichnung und Breitenmessung mit derselben Schnitt-ID an die nativen `surface_text_*`-Builtins weiter.
- Der Hash ist lowercase FNV-1a-64 über Breite/ Höhe als little-endian 32 Bit und anschließend die RGBA-Bytes in top-left-Zeilenreihenfolge.

Kanonischer Hashvektor: 2x1 mit `[255,0,0,255]` und `[0,255,0,0]` ergibt `e2337428034aea61`.

## Tests

```bash
mise run test-ui-moo-surface
```

Die Task kompiliert und startet ausschließlich:

- `ui_moo_surface_primitives_test.moos`: Pixel, Alpha, Clip, Primitive, Linienbreite ohne Doppelblend, Capability, Hash und Snapshot.
- `ui_moo_surface_golden_test.moos`: echter Widgetbaum, Schlüsselpixel, fester Golden-/State-Hash, normalisierter Input, 500 Repaints und fail-closed Clipstack-Überlauf.

Die Task selbst öffnet kein Fenster. Für die vollständige Isolation wird sie zusätzlich in Bubblewrap mit getrennten Namespaces, maskiertem `/run` und ohne Display-/Wayland-/DBus-/Audio-Umgebungsvariablen ausgeführt. Die Desktop-Tasks `test-ui` und `test-ui-moo-demo` gehören nicht zum O1-Gate.
