# moo City Builder

Ein 3D-Top-Down Aufbau-Spiel, geschrieben in **moo**, gerendert ueber das
moo `raum_*`-API (Vulkan-Backend, OpenGL-Backend als Fallback).

## Kompilieren & Starten

```bash
# Mit Vulkan-Backend (empfohlen):
MOO_3D_BACKEND=vulkan moo-compiler run beispiele/city_builder/city_builder.moo

# Standard-Backend (OpenGL):
moo-compiler run beispiele/city_builder/city_builder.moo

# Als Binary kompilieren:
moo-compiler compile beispiele/city_builder/city_builder.moo \
    -o beispiele/city_builder/city_builder
./beispiele/city_builder/city_builder
```

## Steuerung

| Taste                 | Wirkung                              |
|-----------------------|--------------------------------------|
| **Pfeiltasten**       | Cursor ueber das Grid bewegen        |
| **1**                 | Haus bauen (3 Holz, 1 Stein, 2 Gold) |
| **2**                 | Fabrik bauen (2 Holz, 5 Stein, 5 Gold)|
| **3**                 | Park bauen (2 Holz, 0 Stein, 1 Gold) |
| **4**                 | Strasse bauen (0 Holz, 2 Stein, 1 Gold) |
| **5**                 | Wasser/See platzieren (kostenlos)    |
| **0**                 | Kachel abreissen                     |
| **W / A / S / D**     | Kamera schwenken                     |
| **Q / E**             | Heran-/Wegzoomen                     |
| **R**                 | Stadt zuruecksetzen                  |
| **Escape**            | Beenden                              |

## Spielziel

- Lass deine Bevoelkerung wachsen (Haeuser bauen).
- Halte deine Ressourcen-Bilanz im Plus (Fabriken liefern Gold, Parks
  liefern Holz, Fabriken liefern Stein).
- Plane mit Strassen und Wasserflaechen ein schoenes Stadtbild.

## Ressourcen

| Resource     | Quelle                                    |
|--------------|-------------------------------------------|
| **Holz**     | +1 pro Park / Tick                        |
| **Stein**    | +1 pro Fabrik / Tick                      |
| **Gold**     | +1 pro Haus, +2 pro Fabrik / Tick         |
| **Bevoelkerung** | 4 pro Haus + 1 pro Park              |

Ein Tick erfolgt etwa jede Sekunde (60 Frames a 16 ms).

## Dateistruktur

| Datei                | Inhalt                                              |
|----------------------|-----------------------------------------------------|
| `city_builder.moo`   | Hauptspiel: Render-Loop, Input, HUD, Game-Logik     |
| `grid.moo`           | Modul-Skizze: NxN-Grid, get/set, Adjazenz, Bilanz   |
| `buildings.moo`      | Modul-Skizze: Gebaeude-Typen, Farben, Kosten        |
| `README.md`          | Diese Datei                                         |

`grid.moo` und `buildings.moo` sind eigenstaendige Referenz-Module —
die Funktionen sind im Hauptspiel zusaetzlich inline, damit das Spiel
auch ohne lokales Modul-System kompilierbar bleibt.

## Architektur

- **Kamera**: `camera_topdown(win, cx, cz, zoom)` setzt das Auge hoch
  ueber `(cx, cz)` mit leichter Z-Verschiebung. Das ergibt eine
  geneigte Top-Down-Perspektive (sichtbare Gebaeude-Hoehen) statt
  einer streng orthografischen Draufsicht.
- **Grid**: Flache Liste der Laenge `N*N`. Index = `y * N + x`.
- **Rendering**: Pro Frame zeichnet `grid_render` 256 Boden-Tiles
  und gestapelte Wuerfel pro Gebaeude. Cursor pulsiert via Sinus.
- **HUD**: Vier farbige Saeulen seitlich des Spielfelds zeigen Holz,
  Stein, Gold und Bevoelkerung an. Konsolen-Output liefert genaue Zahlen.
- **Backend**: Nutzt `raum_*`-API (Datei `compiler/runtime/moo_3d.c`).
  Mit `MOO_3D_BACKEND=vulkan` rendert moo ueber den Vulkan-Backend.
