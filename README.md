# moo

**Deutsch** · [English](README.en.md)

**Eine Sprache vom Skript bis zum Kernel — auf Deutsch oder Englisch, dynamisch bequem und nativ schnell.**

moo wird mit LLVM zu nativem Code kompiliert, die Laufzeit ist in C geschrieben. Dieselbe Sprache trägt Skripte, KI-Training auf der GPU, 3D-Spiele, eigene Oberflächen, Netzwerkprotokolle in reinem moo und einen UEFI-Loader mit eigenem Netzwerktreiber, der auf echter Hardware gebootet wurde.

> **Top-Highlight: moo auf echter Hardware.** Eine UEFI-Anwendung in moo bootet vom USB-Stick auf einem Mini-PC mit Intel N100 und zeigt die von moo gezeichnete MoOS-Oberfläche. Ein eigener I226-V-Netzwerktreiber in moo lief dort und meldete auf Port 0 den Link mit 100 Mbit. *Auf echter Hardware nachgewiesen, Stand 01.11.2026; mit dem neuesten Stand wurde der Hardwarelauf noch nicht wiederholt.*

Reifegrade in dieser README: **getestet** (automatische Tests oder Gates), **funktioniert**, **teilweise**, **in Arbeit**. Die Details mit Schwächen stehen in [docs/STAND.md](docs/STAND.md).

## 1. Zwei Ebenen, Deutsch und Englisch

Deutsch und Englisch sind gleichwertig; die Schlüsselwörter stehen in `spec/tokens.yaml` und werden in der CI geprüft. Normaler Code ist dynamisch und bequem. Mit `@nativ` und Typen wird eine Funktion zu Maschinencode ohne Boxing. Reifegrad: Sprache getestet, native Ebene funktioniert.

```moo
funktion fibonacci(n):
    wenn n <= 1:
        gib_zurück n
    gib_zurück fibonacci(n - 1) + fibonacci(n - 2)

zeige fibonacci(20)
```

Dasselbe auf Englisch (beide Schreibweisen lassen sich in einer Datei mischen):

```moo
func fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

show fibonacci(20)
```

Beide Programme geben `6765` aus. Die native Ebene:

```moo
@nativ
funktion quadratsumme(n: i64) -> i64:
    setze s: i64 auf 0
    für i in 0..n:
        setze s auf s + i * i
    gib_zurück s

zeige quadratsumme(1000)
```

Ausgabe: `332833500`. In einer Einzelmessung (Schleife mit 10^8 Durchläufen) brauchte `@nativ` 0,033–0,039 s, C mit `-O2` 0,071–0,073 s, dynamischer Code 4,9 s.

## 2. KI-Training, auch auf der GPU

Tensoren mit Autograd (Gradienten-Prüfung über die gesamte Operationsliste), Netzschichten bis zum Transformer, Optimierer, Fine-Tuning mit SFT, Distillation und DPO sowie BPE- und Silben-Tokenizer. Training läuft optional über Vulkan-Compute mit 28 Shadern; ein Strikt-Modus und `gpu_statistik()` zeigen, ob wirklich die GPU gerechnet hat. Reifegrad: Autograd getestet, Rest funktioniert (eine GPU belegt).

Aus `beispiele/ki_xor.moos` (komplettes Training in 14 Zeilen):

```moo
setze netz auf [schicht_dicht(2, 8, "tanh", 7), schicht_dicht(8, 1, "sigmoid", 8)]
setze opt auf optimierer_adam(parameter(netz), 0.05)
solange i < 400:
    setze fehler auf mse(vorwaerts(netz, x), ziel)
    fehler.rueckwaerts()
    opt.schritt()
```

Anleitung: [docs/KI_MIT_MOO.md](docs/KI_MIT_MOO.md).

## 3. 3D, Voxel und Sichtprüfung ohne Monitor

Eine `raum_*`-API für 3D auf OpenGL 2.1, OpenGL 3.3 und Vulkan mit Licht, Schatten, Nebel, Bloom, Himmel und Tageszeit; eine Voxel-Engine für ganze Welten. Spiele lassen sich ohne Bildschirm prüfen: Eine Test-API simuliert Eingaben, liest Pixel zurück und speichert Bilder; ein eigener GIF-Encoder ist eingebaut. Reifegrad: 3D und Voxel funktionieren auf allen drei Backends (OpenGL 2.1, OpenGL 3.3, Vulkan), Test-API getestet.

Aus `beispiele/voxel_sandbox_selftest.moos`:

```moo
setze welt auf voxel_welt_neu(1337)
für gcx in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
    für gcy in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
        setze chunks_neu auf chunks_neu + voxel_generieren(welt, gcx, gcy)
setze win auf fenster_unified("voxel selftest", WIN_B, WIN_H)
raum_perspektive(win, 70.0, 0.1, 300.0)
setze info auf test_fenster_info(win)
```

Ausgeführt wird der Selftest laut Dateikopf headless: `xvfb-run -a -s "-screen 0 1280x800x24" env MOO_3D_BACKEND=gl33 moo-compiler run <datei>`.

## 4. Eigene Oberflächen und UI-Tests

Selbst gezeichnete Widgets ohne GTK auf X11, Wayland und Windows, mit Layout, Menüs, Zwischenablage, Glass-Theme und Compositor. UI-Tests nehmen einen Screenshot samt JSON-Beschreibung (Snapshot mit Sidecar) auf und klicken Widgets über ihre ID. Reifegrad: Linux und Windows funktionieren, UI-Tests getestet; macOS ist blind nachgezogen und nicht geprüft.

Aus `beispiele/ui_login_demo.moos`:

```moo
importiere ui
ui_baue(g, "Login", 340, 260, [
    [setup_login_form, ["Benutzername:", "Passwort:", 100, on_login, on_cancel]],
    [setup_status_bar, ["Bereit", "v1.0"]],
])
ui_zeige(g["hFenster"])
ui_laufen()
```

Aus `beispiele/ui_snapshot_demo.moos`:

```moo
ui_widget_id_setze(g["btnSpeichern"], "speichern")
funktion do_snapshot():
    setze ok auf ui_test_snapshot_mit_sidecar(g["hFenster"], "beispiele/snapshots/demo1")
```

## 5. Netz und Datenbank — auch in reinem moo

TCP, UDP, HTTP-Server und -Client sowie TLS 1.3 sind eingebaut (getestet); SQLite, JSON, Regex, Dateien, Threads und Kanäle ebenso. Darüber liegen Protokolle, die komplett in moo geschrieben sind: ein DNS-Resolver, ein WebSocket-Client und ein PostgreSQL-Client einschließlich SCRAM-Anmeldung. Reifegrad: Eingebautes getestet, Protokolle in moo funktionieren.

Aus `beispiele/dns_selftest.moos` (Selftest ohne Netz):

```moo
importiere dns
setze a_resp auf parse_response(kopf(1, TYP_A, [1, 2, 3, 4]))
pruefe("A-Wert", a_resp["antworten"][0]["rdata"] == "1.2.3.4")
```

Aus `beispiele/websocket_client_selftest.moos`:

```moo
importiere websocket
setze ws auf ws_verbinde(url, 10000)
ws.sende_text("hallo websocket")
setze text_event auf ws.empfange()
pruefe("Text-Echo", text_event["daten"] == "hallo websocket")
```

Aus `beispiele/domain/db/bench/pg_test.moos` (Postgres-Protokoll in moo):

```moo
funktion int32_be(n):
    setze b0 auf boden(n / (256 * 256 * 256)) % 256
    setze b1 auf boden(n / (256 * 256)) % 256
    setze b2 auf boden(n / 256) % 256
    setze b3 auf n % 256
    gib_zurück [b0, b1, b2, b3]
```

Aus `beispiele/domain/web/http_api.moos` (gekürzt):

```moo
setze server auf web_server(3000)
solange wahr:
    setze req auf server.web_annehmen()
    setze pfad auf req["pfad"]
    # ...
    wenn pfad == "/api/status":
        web_json(req, {"sprache": "moo", "todos": länge(todos)})
        weiter
```

## 6. Kamera und Mikrofon

Kamera- und Audio-Capture: unter Windows über Media Foundation und WASAPI (auf Hardware geprüft), unter Linux über V4L2 und ALSA (mit Mock-Tests). Kameraframes lassen sich in Tensoren umwandeln; PNG- und WAV-Dateien schreibt reines moo; `beispiele/ki_audio.moos` macht aus Audio ein Spektrogramm für ein Netz. Aufnahme sowie WAV und Spektrogramm sind da, die Wiedergabe über Lautsprecher folgt. Reifegrad: funktioniert.

Aus `beispiele/ki_kamera_live.moos`:

```moo
setze kamera auf kamera_oeffnen()
setze frame auf kamera_frame(kamera, 1000)
test_frame_save_bmp(frame, "/tmp/ki_kamera_live.bmp")
kamera_schliessen(kamera)
```

## 7. Kernel, UEFI und eigener Netzwerktreiber

moo erzeugt Bare-Metal-Programme: einen x86_64-Kernel im Multiboot2-Format (getestet unter QEMU), UEFI-Anwendungen als PE32+ und AArch64 auf QEMU-virt. Der Loader und der Netzwerktreiber sind in moo geschrieben; auf echter Hardware nachgewiesen (Intel N100, Stand 01.11.2026) sind UEFI-Boot mit moo-Oberfläche und der I226-V-Treiber mit Link. Der Kernel-Unterbau besteht noch überwiegend aus C; MoOS ist **in Arbeit** (7 von 16 Planschritten). Scheduler, Prozesse, Dateisystem und PCI-Verwaltung fehlen noch.

Aus `beispiele/uefi/hallo_uefi.moos`:

```moo
aus "./netz_treiber" importiere nt_pci_init, nt_ports_oeffnen, nt_anzahl, nt_port, nt_link, nt_udp_log, nt_log, nt_status_text, nt_dma, nt_dez

funktion netz_log(ports, puf, text):
    setze n auf nt_anzahl(ports)
    wenn n == 0:
        kern_netz_sende(text)
        gib_zurück nichts
    nt_log(ports, puf, text)
```

Aus `beispiele/kernel/hallo_kern.moos` (Build: `compile --no-stdlib --target x86_64-bare --kernel`):

```moo
setze adresse auf kern_alloc(64)
kern_seriell_hex(adresse)
kern_frei(adresse)
kern_idt_init()
kern_pic_remap(0x20, 0x28)
kern_timer_init(100)
```

## 8. C-Bibliotheken anbinden

`extern "C"`, Rückrufe, Strukturen by value, opake Griffe und C++ über Adapter; der Bindungsgenerator `moo-compiler bindgen` erzeugt aus C-Headern einen Entwurf, `layout --c-header` prüft Strukturlayouts. Reifegrad: funktioniert; Zeiger und `unsicher` nur teilweise.

Aus `beispiele/nativ/10_rueckruf.moos` — eine moo-Funktion als Vergleichsrückruf für `qsort` aus libc:

```moo
extern "C" funktion qsort(basis: ptr[Zahl], anzahl: u64, groesse: u64, vergleich: ptr[fn(ptr[Zahl], ptr[Zahl]) -> i32]) -> void

@nativ
@rückruf
funktion vergleiche(a: ptr[Zahl], b: ptr[Zahl]) -> i32:
    unsicher:
        wenn a.w < b.w:
            gib_zurück -1
        # ...

# in lauf_sortiere:
        qsort(zeiger(u64(puffer_zeiger(p))), länge(p), 4, vergleiche)
```

## 9. Testkultur

Sanitizer-Läufe (Speicher- und Undefined-Behavior-Prüfung) auf drei Betriebssystemen, ein Gesamtlauf `run_all`, eine Konformanz-Suite (Stufen G0–G7), Gradienten-Prüfung für die KI-Operationen und zehn CI-Workflows; die GitHub-CI läuft auf Linux, Windows und macOS grün. Beispiele schreiben ihre Tests selbst in moo, etwa so (aus `beispiele/websocket_client_selftest.moos`):

```moo
funktion pruefe(name, bedingung):
    wenn nicht bedingung:
        wirf "WEBSOCKET FAIL " + name
    zeige "PASS " + name
```

Reifegrad: getestet.

## Für KI-Agenten (Vision, in Arbeit)

moo soll sich gut von KI-Agenten programmieren lassen: einheitliche Diagnosen mit Fehlercode, JSON-Ausgabe (`--diagnose=json`) und `moo pruefe` sind in Review. Eine begrenzte Prüfung vor der Ausführung gibt es bereits (`moo-compiler pruefe --typpruefung`, siehe unten); die vollständige Typprüfung mit Datei:Zeile und Vorschlag folgt. Das ist noch nicht fertig; belegt sind heute Compilefehler mit Zeile und Spalte sowie die begrenzten Hinweise des folgenden Absatzes.

Bereits belegt (Commits 35d1f77, 3e579b5, b5cb6ee): Der Compiler prüft die Argumentzahl von Builtins (Aritätstabelle für 1256 Builtins) und meldet falsche Aufrufe als Diagnose M-C003/M-C004 statt mit einem Compiler-Absturz; ein Auffangnetz meldet unerwartete interne Fehler als M-C999. `moo-compiler pruefe --typpruefung` gibt begrenzte Hinweise: M-F001 bei einem Aufruf einer Funktion, die per `wirf` einen Fehler werfen kann, und M-F002 bei bestimmten Builtins (u. a. Netz, Datenbank, Laden), die bei äußerem Fehlschlag werfen, jeweils ohne `versuche:`. Erfasst werden nicht alle unbehandelten Aufrufe. Ein Fehler vor `weiter`/`stopp` springt zum innersten lokalen `fange`. Die Runtime bricht die Ausgabe nach einem Formatierungsfehler ab, und ein Schreiben auf eine vom Peer geschlossene Netzverbindung ist ein moo-Fehler statt eines Prozessendes per SIGPIPE (RC 141). Für ausgewählte Beispiele (die P4-Silben-Adapter, `ki_verteilt`, `stackvm`) ist gemessen, dass Fehler mit Meldung und RC 1 enden; Umfang und Messungen stehen in [docs/STAND.md](docs/STAND.md).

## Ausblick

Geplant, noch nicht begonnen: ein A/B-Vergleich von GPU-Trainingsläufen, adaptive Sparse-Berechnung im KI-Stack und adaptive Suche mit schnellem Methoden-Dispatch im Sprachkern.

## Schnellstart

```bash
cd compiler && cargo build --release
./target/release/moo-compiler run ../beispiele/fibonacci.moos
./target/release/moo-compiler compile ../beispiele/nativ/03_casts.moos -o casts
```

Voraussetzungen: [DEPENDENCIES.md](DEPENDENCIES.md). Alle Ziele (x86_64, x86, ARM, AArch64, RISC-V, MIPS, PowerPC, WebAssembly, Windows und Freestanding-Varianten): `moo-compiler targets`.

## Wie es funktioniert

Der Compiler (Rust, `compiler/src`) übersetzt moo über LLVM 18 (Crate `inkwell`) in nativen Code und bindet die C-Runtime (`compiler/runtime`) ein. Dynamischer Code läuft über die Runtime; `@nativ`-Funktionen werden direkt mit Maschinentypen übersetzt.

```
compiler/   Compiler (Rust), C-Runtime, Tests
stdlib/     Stdlib-Module in moo: ui, ui_moo, websocket, dns, welt, vektor …
beispiele/  lauffähige .moos-Programme
docs/       Benutzer-Doku
spec/       Spezifikation (u. a. tokens.yaml)
src/moo/    Python-Werkzeuge (LSP, Formatter, Transpiler), nicht der Hauptpfad
editors/    VSCode-Extension (klein)
website/    Landingpage und Playground (serverseitig)
```

Mehr: [ARCHITECTURE.md](ARCHITECTURE.md), [spec/runtime_layers.md](spec/runtime_layers.md).

## Stand

Alpha, ein aktives Ein-Personen-Projekt, entwickelt mit KI-Agenten. Dateiendung `.moos`. Planliste 15.11.2026: Hauptroadmap 423 von 504 Schritten, native Ebene 74 von 89, KI-Trainingsmethoden 17 von 20, Silben-Tokenizer 14 von 17, MoOS 7 von 16. Reifegrade, Schwächen und offene Punkte: **[docs/STAND.md](docs/STAND.md)**.

Stand 15.11.2026, noch nicht veröffentlicht: Der Spaltenindex `m[i]` auf `mat4`/`dmat4` liefert eine Spalte als `vec4`/`dvec4`, `v[i]` auf `vecN`/`dvecN` eine Komponente, lesend und als Zuweisungsziel; ein konstanter Index außerhalb des Bereichs ist der Compilefehler M-M001, ein Laufzeitindex außerhalb löst einen Trap aus (in hosted-Programmen fangbar). In `stdlib/vektor.moos` rechnet die Trigonometrie über die eingebauten `sin`/`cos`/`tan` ohne libm, dazu kommen neue `dmat4_*`-Konstruktoren. Außerdem enden Fehlerpfade in den P4-Beispielen, in `ki_verteilt` und in `stackvm` mit Meldung und RC 1; die P5-Beispiele und die stdlib-Module `websocket`, `dns` und `ki_bild_easy` sind bei Fehlerpfaden überarbeitet (bewusst belassene Stellen tragen einen `FEHLERPFAD`-Kommentar). Belege: `skripte/p2/grafik/test_mat4_nativ.sh` (Differenztest über 20000 Runden ohne Abweichung) und `compiler/runtime/tests/test_p2_native_math.c` unter ASan und UBSan ohne Fehler.

## Dokumentation

- Sprache lernen: [docs/lernen.md](docs/lernen.md)
- KI mit moo: [docs/KI_MIT_MOO.md](docs/KI_MIT_MOO.md) · English: [docs/AI_WITH_MOO.md](docs/AI_WITH_MOO.md)
- 3D: [docs/raum_3d.md](docs/raum_3d.md); Oberflächen: [docs/ui_moo.md](docs/ui_moo.md); TLS unter Windows: [docs/windows_tls.md](docs/windows_tls.md)
- Architektur: [ARCHITECTURE.md](ARCHITECTURE.md), Abhängigkeiten: [DEPENDENCIES.md](DEPENDENCIES.md)
- Referenz aller Builtins: [moolang-docs](https://7blacky7.github.io/moolang-docs/)
- Native Ebene: [Sprachreferenz-Entwurf](dokumentation/p2/sprachreferenz-entwurf-de.md), [Migrationsleitfaden](dokumentation/p2/migrationsleitfaden-de.md)

## Lizenz

Wird vor v1.0 festgelegt. Das Repository enthält derzeit keine LICENSE-Datei.
