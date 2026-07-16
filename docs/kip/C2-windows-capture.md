# C2-WIN: Kamera- und Mikrofon-Capture unter Windows

## Stand

Die Windows-Implementierung verbindet MOO mit Media Foundation (Kamera) und
WASAPI (Mikrofon). Der portable Zustandsautomat ist unter Sanitizern geprüft;
die Windows-Systemschicht wird mit MinGW-w64 gegen die echten SDK-Header und
-Bibliotheken mit `-Werror` gebaut und als PE ausgeführt.

Echte Kamera-/Mikrofon-Hardware unter Windows ist noch nicht lokal verfügbar.
Dieser Stand darf deshalb nicht als Hardware-verifiziert bezeichnet werden.

## Architektur

- `moo_capture_pull.c`: plattformneutraler Zustandsautomat, Fristen,
  Queue-/Spill-Verhalten, Konvertierung und Recovery.
- `moo_capture_windows_system.c`: COM/Media-Foundation- und WASAPI-Grenze.
- `moo_capture_pull_internal.h`: injizierbare Systemoperationen für
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

## Natives Hardware-Gate (bestanden 2026-07-16)

Auf der Win10-VM (19044, `pro@192.168.50.246`) mit durchgereichter Logitech
BRIO 4K (UVC-Kamera + Mikrofon) nativ ausgeführt — MinGW-Cross-Build, SCP,
SHA256-Abgleich, Lauf ohne Wine:

- `c2win-smoke.exe`: `startup=OK`, `camera_enumerate total=1`,
  `wasapi_open rate=48000 channels=1`.
- `test_capture_windows_native_stream.c` (`c2win-stream.exe`): 3 Open/Close-
  Zyklen mit je 30 Kameraframes (640x480@30, nonzero_ratio 1.00) und je
  48000 WASAPI-Samples (avg_abs 0.003-0.006, echtes Mikrofonsignal),
  `recoveries=0`, `RESULT=PASS`.

Das Gate fand einen realen Bug, den Wine nie zeigte: WASAPI flaggt das erste
Paket nach `Start`/Recovery als `DATA_DISCONTINUITY`. Alt-Verhalten verwarf
das Paket als `MOO_PULL_RECOVERABLE` und `moo_capture_pull.c` behandelte
`RECOVERABLE` aus `microphone_next` gar nicht (Handle → BROKEN): `mikro_lesen`
schlug auf echter Hardware beim ersten Lesen immer fehl. Fix: erstes Paket
wird ausgeliefert (`first_packet`-Flag, auch nach Recovery); Mid-Stream-
Discontinuity bleibt `RECOVERABLE`, und der Pull-Core beherrscht den
Recovery-Pfad jetzt auch für `microphone_next`.

Bewusst offen: Application-Verifier-/TSan-artiges Race-Gate unter Last
(AppVerifier ist auf der VM nicht installiert) und Hotplug/Disconnect-Smoke.
