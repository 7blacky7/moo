# Followup-Tickets

Nicht-dringende Aufgaben, die im Rahmen der Konsolidierungs-Welle (Prio 1–3,
2026-04-14) bewusst **aus Scope gehalten** wurden, weil sie Neu-Features sind.
Kein Deployment-Blocker.

## P2c-followup: Vision-API-Wrapper für Welt-Engine

**Herkunft**: Prio2c (Welt-API entdupen), thought `b4e4f5d7` (Vision 2026-04-13).

**Fehlend als öffentliche API** (weder als `stdlib/welt.moo`-Wrapper noch als
`__welt_*`-Builtin noch als `moo_world_*`-Runtime-Funktion vorhanden):

- `welt_wolken(w, aktiv)` — Wolken-Layer an/aus
- `welt_wasser(w, hoehe)` — Meeresspiegel-Variante mit Wasser-Shader
- `welt_licht(w, stil)` — Preset-Licht (`"abendsonne"`, `"mittag"`, `"nacht"`)
- `welt_starten(w)` — komplette Game-Loop (WASD + Maus + Schleife)

**Grund für Vertagung**: Benötigt **neue C-Runtime-Implementierungen** in
`compiler/runtime/moo_world.c` inklusive Shader-/Asset-Arbeit. Das ist
Feature-Entwicklung, keine Konsolidierung — widerspricht der User-Auflage
"Neue Features erst nach Konsolidierung".

**Voraussetzungen zur Umsetzung**:
1. Shader-Erweiterung für Wolken (Volumetric oder Skybox-Layer).
2. Water-Shader (Wellen, Reflexion) im aktiven 3D-Backend.
3. Preset-Tabelle für `welt_licht`-Stile (Sonne-Position + Farbe + Nebel).
4. Generischer Game-Loop-Baustein in C (`moo_world_run`) oder moo-Ebene.

**Priorität**: niedrig — nur beginnen, nachdem P2b/P3b/P3c gelandet sind und
die Runtime sich stabilisiert hat.
