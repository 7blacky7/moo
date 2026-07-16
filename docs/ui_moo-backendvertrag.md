# ui_moo-Backendvertrag v1

Status: P016-A1. Dieses Dokument ist der kanonische Backendvertrag. Die kollidierten Dateien `docs/ui_moo.md` und `docs/ui_moo-referenz.md` bleiben bis P016-D1 historische UIMOO-7-Artefakte.

## Zwei gleichwertige UI-Wege

1. Die native API `ui_*` bleibt direkt aus Moo nutzbar und führt über die Runtime zu GTK, Win32 oder Cocoa.
2. `ui_moo` ist optional. Widget-Tree, Zustand, Theme, Hit-Testing, Fokus und Event-Dispatch liegen in `stdlib/ui_moo.moos`.

Ein `ui_moo`-Backend ist kein natives Widget-Toolkit. Es ist ein Dict aus Zeichenoperationen und Fähigkeiten.

## Hybrid-Grenze: native Plattformdienste

Diese Dienste bleiben Eigentum des Host-Adapters; ui_moo implementiert diese Plattformdienste nicht selbst:

- Dialoge: `ui_info`, `ui_warnung`, `ui_fehler`, `ui_frage`, `ui_eingabe_dialog`, `ui_datei_oeffnen` und `ui_datei_speichern`.
- Native Texteingabe und IME: `ui_eingabe`; Komposition, Kandidatenfenster und Plattformintegration bleiben beim Host.
- Menüleiste und Menüs: `ui_menueleiste`, `ui_menue` und `ui_menue_eintrag`.
- System-Tray: `tray_create` und `tray_menu_add`.

## Abhängigkeitsgraph

```text
Moo-Anwendung ── ui_* ── Compiler/Runtime ── GTK | Win32 | Cocoa
      │
      └── ui_moo (Tree/Theme/Fokus/Dispatch in Moo)
              │
              └── Backend-Dict
                    ├── Leinwand-Adapter ── ui_zeichne_* ── Host-Runtime
                    ├── Frame-Adapter ── vorhandene Spiel-/SDL-Brücke
                    └── Mock/Moo-OS ── Command- oder Pixelbuffer, kein Toolkit
```

## Backend-Dict

`uim_backend_neu(name, faehigkeiten, operationen)` erzeugt Vertrag Version 1 oder `nichts`, falls eine Pflichtoperation, ein Operationswert oder ein Pflichtfeld des Capability-Schemas fehlt. Moo v1 besitzt keine allgemeine Callable-Reflection; die konkrete Signatur wird deshalb zusätzlich durch den Mock-Conformance-Test belegt.

Jede Operation hat dieselbe ABI:

```text
operation(kontext, args_liste) -> wert
```

Pflichtoperationen:

| Operation | Argumente nach Kontext | Ergebnis |
|---|---|---|
| `anfordern` | `[]` | Neuzeichnung angefordert |
| `farbe` | `[zeichner, rgba]` | Farbe übernommen |
| `rechteck` | `[zeichner,x,y,b,h,gefüllt]` | gezeichnet |
| `rechteck_rund` | `[zeichner,x,y,b,h,radius,gefüllt]` | gezeichnet |
| `kreis` | `[zeichner,cx,cy,radius,gefüllt]` | gezeichnet |
| `linie` | `[zeichner,x1,y1,x2,y2,breite]` | gezeichnet |
| `text` | `[zeichner,x,y,text,größe]` | gezeichnet |
| `text_breite` | `[zeichner,text,größe]` | Breite als Zahl |
| `clip_setze` | `[zeichner,x,y,b,h]` | Clip gesetzt |
| `clip_loesche` | `[zeichner]` | Clip entfernt |

Fähigkeiten sind explizit: `alpha`, `clip`, `rechteck_rund` und `kreis_rand` sind zwingend Bool-Werte; `text_metrik` ist ein nichtleerer semantischer Name des Metrikmodells. Ein Backend darf fehlende Fähigkeiten nicht als plattformidentisch ausgeben.

## Normalisierte Eingabe

Backends speisen plattformneutrale Ereignisse ein:

- `uim_backend_maus_taste(k,x,y,taste,gedrueckt)`: logische Pixel relativ zur Wurzel; Taste `1` ist primär, andere Tasten werden in v1 verworfen.
- `uim_backend_maus(k,x,y,gedrueckt)`: Kurzform für Primärtaste.
- `uim_backend_bewegung(k,x,y)`: logische Pixel relativ zur Wurzel.
- `uim_backend_taste(k,taste,gedrueckt,mod)`: kanonische Keynamen der nativen UI-API. `mod` ist die Bitmaske `Shift=1`, `Control=2`, `Alt=4`; nicht gesetzte oder derzeit nicht unterstützte Modifier tragen `0` bei.
- `uim_backend_rad(k,x,y,delta)`: positives Delta verschiebt den Viewport nach unten (Inhalt nach oben), negatives Delta entsprechend nach oben.
- `uim_backend_fokus(k,hat_fokus)`: explizite Aktivierung/Deaktivierung aller Eingaben.

Desktop-Leinwand bindet ausschließlich diese Funktionen an die nativen Callbacks. Frame-, Mock- und spätere Moo-OS-Backends rufen sie direkt auf. Bei `hat_fokus == falsch` werden Maus, Bewegung, Tastatur und Rad verworfen; ein Blur löst außerdem Druck- und Hoverzustände am Kontext und an den betroffenen Widgets. Erst ein explizites Fokus-Ereignis aktiviert Eingaben wieder.

## Mitgelieferte Adapter

- `uim_backend_leinwand()`: Desktop-Host über `ui_leinwand`, vollständige v1-Fähigkeiten.
- `uim_backend_frame()`: vorhandene SDL-/Spielbrücke. Degradierungen werden im Fähigkeits-Dict offengelegt.
- `uim_backend_mock()`: reines Moo-Command-Buffer-Backend ohne Fenster, GTK oder SDL.

`uim_mock_wurzel`, `uim_mock_zeichne` und `uim_mock_befehle` bilden die Referenz für ein späteres Moo-OS-/Framebuffer-Backend. Ein echtes Backend kann die Commands direkt in einen Pixelbuffer, eine GPU-Queue oder einen Kernel-Framebuffer übersetzen.

## Plattformisolation der Tests

Der Backendvertrag ist plattformneutral; der Testharness ist es nicht automatisch:

- Linux/GTK: privater Xvfb, `GDK_BACKEND=x11`, `SDL_VIDEODRIVER=x11`, kein geerbtes `WAYLAND_DISPLAY`.
- Windows: eigener Prozessbaum/Job Object und isolierte CI-Sitzung (P016-P1).
- macOS: isolierter macOS-Runner; kein Xvfb (P016-P1).
- Moo-OS: Mock-/Framebuffer-Test ohne Desktop-Toolkit.

Unbekannte Umgebungen starten keine UI-Tests (fail closed).
