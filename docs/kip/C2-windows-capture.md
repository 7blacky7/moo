# C2-WIN: Kamera- und Mikrofon-Capture unter Windows

## Stand

Die Windows-Implementierung verbindet MOO mit Media Foundation (Kamera) und
WASAPI (Mikrofon). Der portable Zustandsautomat ist unter Sanitizern geprüft;
die Windows-Systemschicht wird mit MinGW-w64 gegen die echten SDK-Header und
-Bibliotheken mit `-Werror` gebaut und als PE ausgeführt.

Echte Kamera-/Mikrofon-Hardware unter Windows ist noch nicht lokal verfügbar.
Dieser Stand darf deshalb nicht als Hardware-verifiziert bezeichnet werden.

## Architektur

- `moo_capture_windows.c`: plattformneutraler Zustandsautomat, Fristen,
  Queue-/Spill-Verhalten, Konvertierung und Recovery.
- `moo_capture_windows_system.c`: COM/Media-Foundation- und WASAPI-Grenze.
- `moo_capture_windows_internal.h`: injizierbare Systemoperationen für
  deterministische Fehler- und Timingtests.
- Media Foundation liefert RGB32/BGRA asynchron. MOO übernimmt jeweils das
  neueste vollständige Bild und konvertiert nach RGBA.
- WASAPI läuft shared, event-driven und liefert Float-Samples. Stereo wird
  deterministisch auf Mono gemischt; Paketreste bleiben im Spill-Puffer.
- Geräteverlust ist begrenzt auf drei Recovery-Versuche. Danach entsteht ein
  normaler MOO-Fehler statt einer Endlosschleife.

## Gates

```bash
mise run test-compiler
bash compiler/runtime/tests/run_sanitize.sh
bash skripte/capture_windows_cross_gate.sh
MOO_REQUIRE_WINE=1 bash skripte/capture_windows_cross_gate.sh
```

Das normale Cross-Gate verlangt erfolgreichen MinGW-`-Werror`-Build und Link.
Wine ist optional, weil Host-Präfixe bei `MFStartup` hängen können. Mit
`MOO_REQUIRE_WINE=1` wird der PE-Startup-/Shutdown-Smoke zum harten Gate; das
ist für den isolierten `wine-qa`-Workspace vorgesehen.

Der Vollmodus von `test_capture_windows_wine.c` (ohne `--init-only`) prüft
Enumerierung und WASAPI-Open. Er gehört in echte Windows-CI und anschließend
auf einen Rechner mit Kamera und Mikrofon. Erst dieses Hardware-Gate kann
C2-WIN vollständig freigeben.
