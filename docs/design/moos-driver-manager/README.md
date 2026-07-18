# MoOS Driver Manager — Design-Vorschau (Vorlage, NICHT final)

Google-Stitch-Mockup, vom User geteilt (2026-07-18) als Vorschau, wie Treiber in
MoOS aussehen/funktionieren koennten. **Nicht final** — als Vorlage/Referenz.

## Dateien
- `DESIGN.md` — Design-System (Glassmorphism/Aero-Revival, Farbtokens, Typografie
  Inter + Geist-mono, Spacing/Radius/Elevation). Direkt nutzbar als OS-weite
  Designsprache fuer ui_moo / P016.
- `screen.png` — Render des Driver-Manager-Screens.
- `code.html` — HTML-Mockup.

## Kernkonzept "Multi-Driver Intelligence"
Fuer dasselbe Geraet stehen mehrere Treiber nebeneinander — **Windows | MoOS-Native
| Linux** — mit Live-Benchmark (Latenz/Durchsatz/Stabilitaet/Power), Zuweisung pro
Scope (Dienste/Apps/Programme/Benutzer) und automatischer Best-Kombi-Wahl
("Intelligent Config"). Treiber-Ebene desselben Dual-/Multi-Path-Prinzips wie
ui_native/ui_moo und die Krypto/TLS-Backends.

Einordnung, Technik-Realitaet (Fremd-Treiber-Kompat) und Design-Tokens im Detail:
Synapse-Memory `moos-driver-manager-vorschau-multi-driver-intelligence`.
