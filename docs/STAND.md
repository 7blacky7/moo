# Stand des Projekts (15.11.2026)

Quellen: Bestandsaufnahme vom 15.11.2026 (acht Stack-Analysen über den Code-Index, Pläne und Notizen; nur lesend, nichts ausgeführt) mit Korrektur zum Hardwarebeleg, Planliste vom 15.11.2026. Reifegrade: **G** funktioniert und getestet, **F** funktioniert, **T** teilweise, **P** geplant.

moo ist ein aktives Ein-Personen-Projekt, das mit KI-Agenten entwickelt wird.

## Fortschritt je Arbeitsstrang

| Strang | Erledigt |
|---|---|
| Hauptroadmap (Sprache, Compiler, Runtime, Stdlib, UI) | 423 von 504 |
| Native Ebene `@nativ` (aktiv) | 74 von 89 |
| GPU-Training A/B | 0 von 6 |
| Silben-Tokenizer | 14 von 17 |
| Trainingsmethoden | 17 von 20 |
| MoOS-Kern in moo | 7 von 16 |
| Adaptive Sparse-Berechnung / MoE-Routing | 0 von 26 |
| Sprachkern: adaptive Suche, Methoden-Dispatch | 0 von 16 |

## Seit dem letzten Inventar (15.11.2026)

Quellen: Commit-Texte 35d1f77, 3e579b5, b5cb6ee und Messläufe im Projekt. Das Fortschrittsraster oben ist dabei nicht neu erhoben.

| Inhalt | Stand |
|---|---|
| Falsche Argumentzahl bei Builtins: Diagnosen M-C003/M-C004 statt Compiler-Panic; Panic-Auffangnetz M-C999; Tiefenlimit M-S004 (stackabhängig); Aritätstabelle für 1256 Builtins per `build.rs`; versteckter Befehl `builtins` | veröffentlicht (35d1f77) |
| Fehler vor `weiter`/`stopp` springt zum innersten lokalen `fange`; Fehlerpfade in Beispielen; CI-Bericht `typpruefung-bericht` | veröffentlicht (35d1f77) |
| `moo_to_string`/`moo_print` brechen nach einem Konvertierungsfehler ohne Folgeausgabe ab, Zwischenwerte werden einmal freigegeben (`sanitizer.sh` grün); `db-ci` kopiert `postgres_client.moos` vollständig | veröffentlicht (3e579b5) |
| Kein SIGPIPE-Abbruch: `MSG_NOSIGNAL`/`SO_NOSIGPIPE`, TLS über eigenen BIO; Schreiben auf eine geschlossene Verbindung ist ein moo-Fehler statt RC 141; Follower in `ki_verteilt` bricht bei HELLO kontrolliert ab; P4-Ladefehler RC 1; Legacy-IR-Baseline neu | veröffentlicht (b5cb6ee) |
| Spaltenindex `m[i]` auf `mat4`/`dmat4` → `vec4`/`dvec4`, `v[i]` auf `vecN`/`dvecN` → `f32`/`f64` (lesend und Zuweisungsziel); konstanter Index außerhalb/negativ → M-M001, Laufzeitindex außerhalb → Trap (hosted fangbar), auch bei festen Arrays `[T; N]` mit negativem Literal; Zwischenwerte wie `vec4(..)[i]` in skalaren `@nativ`-Funktionen; `stdlib/vektor.moos`: Trigonometrie über eingebaute `sin`/`cos`/`tan` (ohne libm, auch Bare/UEFI), neue `dmat4_*`-Konstruktoren | noch nicht veröffentlicht |
| `wirf`-Prüfungen in den P4-Adaptern, `p4_silben_demo`, `tests/p4_*`, `ki_verteilt` und `stackvm` enden mit Meldung und RC 1 | noch nicht veröffentlicht |
| Fehlerpfade der P5-Beispiele und der Module `websocket`, `dns`, `ki_bild_easy` (Abschnitte unten) | noch nicht veröffentlicht |

Messungen zur Vektor-/Matrix-Indizierung: `skripte/p2/grafik/test_mat4_nativ.sh` RC 0 (Differenztest über 20000 Runden ohne Abweichung, IR-Gate, Negativfälle); `bare.sh` RC 0 (QEMU); `compiler/runtime/tests/test_p2_native_math.c` unter ASan und UBSan mit 4 603 813 Fällen ohne Fehler (die 1-ULP-Grenze der Trigonometrie gilt nur für diese getesteten Fälle, nicht als allgemeine Garantie); `compiler/tests/p2/vecmat_spaltenindex.moos` mit dem Projekt-Compiler: RC 0, Ausgabe identisch mit `.expected` (15.11.2026).

**Fehlerausgänge und M-F001/M-F002:** Die Bereinigung wurde für die in den Abschnitten unten genannten Bereiche durchgeführt und dort gemessen. Eine vollständige Abdeckung aller Beispiele ist nicht belegt (siehe „Noch offen“). M-F001/M-F002 sind begrenzte Hinweise des Compilers, keine vollständige Prüfung aller unbehandelten Aufrufe.

## Reifegrade und Schwächen

| Bereich | Reifegrad | Schwächen |
|---|---|---|
| Sprache und Compiler | DE/EN-Schlüsselwörter aus `spec/tokens.yaml` (CI-Prüfung) G; `versuche`/`wirf`/`aufräumen` und Refcount G (ASan/LSan); Pattern Matching, Lambdas/Closures, Comprehensions, Module F; `@nativ`, `struktur`, `puffer`, `vec`/`mat` F; `extern "C"` + `@rückruf` F; `bindgen` F; Zeiger/`unsicher` T | native Aufrufe in Methoden und Lambdas gesperrt; `codegen.rs` hat über 13.000 Zeilen; Diagnosen (`diagnose.rs`) im Review; f-Strings und `@bibliothek` nicht belegt (zu prüfen) |
| KI | Tensor/Autograd mit Gradcheck über die ganze Registry G; Netze bis Transformer (RoPE, GQA, MoE-Schicht) F; SGD/Adam/AdamW F; SFT/DPO/Distillation F; BPE- und Silben-Tokenizer F; Dataloader, Checkpoints F; Vulkan-Compute (28 Shader, bit-identischer CPU-Fallback, Strikt-Modus, `gpu_statistik`) F, eine GPU belegt | GRPO ohne Implementierung (im Plan als erledigt geführt, Widerspruch); nur f32; adaptive Sparse-Berechnung und Methoden-Dispatch geplant |
| Grafik und 3D | SDL2-2D; eine `raum_*`-API für GL21/GL33/Vulkan; Licht, Schatten (PCF), Nebel, Bloom, Himmel/Tageszeit, Texturen G/F; Voxel-Engine F (GL33 und Vulkan); Test-API mit Input-Simulation und Pixel-Assert G; GIF-Encoder G; MP4 über ffmpeg T | Vulkan-Chunk-Draws laufen absichtlich inline in `vk_swap` (`moo_3d_vulkan_mem.c`); der visuelle Vulkan-Voxel-Test und der Vulkan-Screenshot sind erledigt; HDR/PBR/Wasser T; keine Physik-Engine; GL21 ohne Screenshot |
| Oberflächen | selbst gezeichnete Widgets ohne GTK auf X11/Wayland/Win32 F; Glass-Theme und Compositor (ASan) F; Layout, Menüs, Clipboard; UI-Tests (Snapshot + JSON-Sidecar, Klick per ID) G | macOS blind nachgezogen; keine nativen Dateidialoge; GTK-Altpfad läuft parallel |
| Netz und Daten | TCP/UDP, HTTP Server/Client G; TLS 1.3 G (net-crypto-ci); DNS, WebSocket-Client, PostgreSQL-Client (SCRAM) in reinem moo F; SQLite F; SHA-256/HMAC/PBKDF2/CSPRNG G (NIST); JSON G; Regex F; Dateien; Threads/Channels F | keine Zeit-/Datums-API; AES/ChaCha/X25519 als Builtins nicht belegt (im Plan als erledigt geführt, Widerspruch); kein WebSocket-Server; kein async |
| Multimedia | Kamera/Mikro Windows (Media Foundation, WASAPI) G auf Hardware; Linux V4L2/ALSA mit Mock-Tests G; Frame↔Tensor F; MP4 ohne Shell; PNG-/WAV-Writer in reinem moo | Wiedergabe über Lautsprecher folgt (Aufnahme, WAV und Spektrogramm sind da); Bilder laden geht über `sprite_laden` (PNG, SDL2_image) und den Bildlader der Datensätze (`moo_dataset.c`); `ki_capture_loopback_gate` evtl. defekt ("rgb"-Modus) |
| System und Bare-Metal | x86_64-Multiboot2-Kernel G (QEMU-Gate); UEFI-App PE32+ G (QEMU) und auf echter Hardware; BIOS-Loader Stage 1/2 F; ARM64 auf qemu-virt F; Port-I/O/MSR/MMIO/IDT/PIC/PIT F; `@unterbrechung`; `--emit elf\|flat\|sector`; Windows-EXE per Cross F; WASM T | Kernel-Unterbau überwiegend C (7 von 16 Planschritten); kein Scheduler, keine Prozesse, kein VFS, kein PCI-Management; Doku zum Netzboot widersprüchlich |
| Werkzeuge | Sanitizer-Gates auf 3 Betriebssystemen G; `run_all` G; Konformanz G0–G7 G; 10 CI-Workflows, GitHub-CI grün auf Linux, Windows und macOS | Python-Toolchain (LSP, Formatter, Transpiler, docgen) ohne Tests, vermutlich Legacy; VSCode-Extension klein (33 Zeilen); Playground serverseitig, kein WASM im Browser; Windows-Installer geplant; Pakete lösen nur Pfade auf |

## Hardwarebeleg

- **UEFI-Boot auf echter Hardware:** Die moo-UEFI-App bootet vom USB-Stick auf dem Testrechner (Glovary N100, AMI UEFI) und zeigt die von moo gezeichnete MoOS-Oberfläche.
- **Eigener I226-V-Netzwerktreiber in moo:** lief auf dem N100, Port 0 meldet Link mit 100 Mbit. Die Netzwerk-Konsole über die I226-V funktionierte (damals noch mit iPXE-Treiber).
- **Stand 01.11.2026.** Ein erneuter Hardwarelauf mit dem neuesten Stand steht aus.

## Zu prüfende Widersprüche

GRPO (Plan sagt erledigt, Code fehlt), AES/ChaCha/X25519 als Builtins, f-Strings und `@bibliothek`, `ki_capture_loopback_gate` ("rgb"), Netzboot. Diese Punkte stehen deshalb nicht oder nur als „in Arbeit“ in der README. Die frühere README übertrieb außerdem: macOS-UI sowie LSP und Playground als fertig.

## P2: Native Ebene `@nativ`

Featuretabelle aus `dokumentation/p2/sprachreferenz-entwurf-de.md` (Stand 14.11.2026). Die Beispiele wurden auf Linux einzeln ausgeführt; Geltung für Windows und Bare setzt eigene Nachweise voraus.

| Bereich | Stand | Beleg |
|---|---|---|
| `@nativ`-Funktionen, Vorwärtsaufrufe | implementiert | `beispiele/nativ/01_native_funktionen.moos` |
| Ganzzahlsemantik (Division, Rest, Shifts, Wrap) | implementiert | `beispiele/nativ/02_ganzzahl_semantik.moos` |
| Casts `als`, `als_wrap`, `als_saettigend` | implementiert | `beispiele/nativ/03_casts.moos` |
| Typisierte Puffer | implementiert (Grundform) | `beispiele/nativ/04_puffer.moos` |
| Strukturen, `@gepackt`, feste Arrays | Deklaration implementiert | `beispiele/nativ/05_struktur_layout.moos` |
| `extern "C"`, `@bibliothek(ziel=...)`, `unsicher` | implementiert für skalare Signaturen und Pointer | Tests in `compiler/tests/p2` |
| Strukturen by value und C-`bool` in FFI | implementiert | `beispiele/nativ/11_struktur_by_value.moos` |
| Feste Arrays und bloße Funktionstypen by value in FFI | abgelehnt (nicht freigegeben) | Test `ffi_aggregat` |
| C-Rückrufe `@rückruf` | implementiert | `beispiele/nativ/10_rueckruf.moos` |
| Opake Griffe, C++ über extern-C-Adapter | implementiert | `beispiele/nativ/12_opaker_griff.moos` |
| Zeigerarithmetik, Umdeutung `zeiger(p)` | implementiert | `beispiele/nativ/07_zeiger.moos` |
| Leihgabe `@liest` | implementiert | `beispiele/nativ/08_leihgabe_liest.moos` |
| Native Aufrufe aus Methoden und Lambdas | implementiert laut Tabelle; die Bestandsaufnahme meldet sie als gesperrt (zu prüfen) | `beispiele/nativ/09_aufruf_aus_methoden.moos` |
| Strukturen in dynamischem Code | implementiert | `beispiele/nativ/13_struktur_dynamisch.moos` |
| `vec2/3/4`, `mat4`, `betrag`/`magnitude` | implementiert | `beispiele/nativ/06_vec_mat.moos` |
| `@nativ`-Funktion als Wert (Variable, `map`/`filter`, Callback, Thread-Start, Import) | implementiert in hosted-Programmen, Grenzen siehe unten | `compiler/tests/p2/native_wert_positiv.moos` |

Die 13 Beispiele unter `beispiele/nativ/` laufen alle (13 von 13).

### Native Funktionswerte (seit Commit 5386eaf)

Belege: die Fixtures `compiler/tests/p2/native_wert_*`. Der direkte Aufruf einer `@nativ`-Funktion bleibt unverändert.

**Was geht (hosted):**

- Als Variable: `setze f auf quadrat`, danach `f(3)`; als Argument einer Moo-Funktion (`anwenden(quadrat, 4)`); in `map` und `filter` (`[1, 2, 3].map(quadrat)`, `[5, 20, 11].filter(ist_gross)`). Getestet mit den Parametertypen `i64`, `f64`, `i32`, `bool` und mit mehreren Parametern (`native_wert_positiv`).
- Importiert aus einem Modul: Direktaufruf und Funktionswert (`native_wert_import`).
- Als Thread-Start: `starte(quadrat, 6)` ruft die Funktion mit genau einem Argument; `t.warten()` liefert 36 (`native_wert_thread`).
- Prüfung beim Aufruf über den Wert, vor dem nativen Aufruf und mit `versuche:` fangbar: falsche Argumentzahl („Funktion 'quadrat' erwartet 1 Argument, bekommen 2“), falscher Typ, nicht exakter Wert (`1.5` für `i64`) und Bereichsüberlauf (`300` für `i8`). Danach läuft das Programm normal weiter (`native_wert_fehler`). Dieselbe Argumentzahl-Prüfung gilt beim Thread-Start: ein zweistelliger nativer Wert mit einem Argument ist ein fangbarer Fehler (`native_wert_thread`).
- Speicher: Wird der `@nativ`-Name direkt an `map`/`filter` übergeben (`xs.map(plus1)`), wird der Funktionswert je Element erzeugt und nach dem Aufruf freigegeben; dazu läuft laut Fixture-Kommentar eine Leckprobe unter LSan im Gate (`native_wert_leck`, nur dieser Fall). **Bekannte Einschränkung (wird behoben):** Wird der Funktionswert zuvor an eine Variable gebunden (`setze f auf plus`, dann `xs.map(f)`), leckt er unter LSan.

**Was nicht geht (Compilefehler):**

- **M-N001:** Die Funktion ist als Wert nicht verwendbar, wenn ihre Parameter oder ihr Ergebnis keine Zahlen oder Wahrheitswerte sind. Der Fehlerkatalog nennt Strukturen, Zeiger und Puffer, außerdem Systemkörper (`unsicher:`) und C-Exporte. Mit Fixture belegt sind ein Strukturparameter (`native_wert_struktur`) und ein `unsicher:`-Körper, der nur einen Wert zurückgibt (`native_wert_system`). **Ausnahme:** Die skalare hosted FFI-Einzelform ist als Wert erlaubt, also eine `@nativ`-Hülle mit skalaren Parametern und Ergebnis, deren `unsicher:`-Körper nur eine `extern "C"`-Funktion aufruft (`ffi_defer_reach_funktionswert`: `ffi_sinus(x: f32) -> f32` ruft `sinf`, der Wert wird über eine Variable aufgerufen). Weitere `unsicher:`-Körper sind damit nicht freigegeben. Ausweg sonst: die Funktion direkt aufrufen oder eine Moo-Funktion als Hülle benutzen.
- **M-N002:** Im Bare-/UEFI-Profil sind native Funktionswerte nicht verfügbar (`native_wert_bare`); der Fehlerkatalog nennt auch `wasm`. Dort ist nur der direkte Aufruf möglich, weil Funktionswerte und Fehlerkanal die hosted Laufzeit brauchen.
- **M-N003:** Eine mit `extern "C"` deklarierte Funktion ist kein moo-Wert (`native_wert_extern`, Beispiel `sinf`). Der Weg ist eine `@nativ`-Hülle um den C-Aufruf in der skalaren FFI-Einzelform (siehe Ausnahme bei M-N001); C-Funktionszeiger für C-Bibliotheken entstehen über `@rückruf`.

Die Texte der Diagnosen: `moo-compiler erklaere M-N001` (Katalog in `compiler/src/diagnose.rs`).

### Einzelmessung Schleife

Schleife mit 10^8 Durchläufen, Host cachyos-x64, je 3 Läufe, Ergebnis in allen Varianten 3088707712:

| Variante | Laufzeit |
|---|---|
| C, `cc -O2` | 0,071–0,073 s |
| moo `@nativ` | 0,033–0,039 s (ca. 1,9× schneller als das C-Programm) |
| moo dynamisch | 4,931 s (1 Lauf) |

Einzelmessung eines einzelnen Programms, keine allgemeine Aussage über Geschwindigkeit. Das C-Vergleichsprogramm liegt nicht im Repository.

### Typprüfung und Rückmeldung für Agenten (in Arbeit)

Ziel: Typfehler vor der Ausführung mit Datei:Zeile und Vorschlag, JSON-Diagnosen, unbehandelter Fehler stürzt nicht ab (`MOO_STRENG=1` bricht ab), Typangaben bleiben freiwillig. Einheitliche Diagnosen mit Fehlercode, `--diagnose=json` und `moo pruefe` sind in Review. Bereits vorhanden: Aritätsdiagnosen M-C003/M-C004 für Builtins, das Panic-Auffangnetz M-C999 und `pruefe --typpruefung` mit den begrenzten Hinweisen M-F001/M-F002 (Abschnitt unten); die vollständige Typprüfung folgt. Belegt ist außerdem: Compilefehler mit Zeile und Spalte.

## Was für die README selbst geprüft wurde

- **Ausgeführt (15.11.2026):** Fibonacci in DE und EN (6765), eine `@nativ`-Quadratsumme in DE und EN (332833500), `beispiele/fibonacci.moos`, die 13 Beispiele unter `beispiele/nativ/`.
- **Nur gelesen, nicht ausgeführt:** alle weiteren Schnipsel der README (KI, 3D, UI, DNS, WebSocket, PostgreSQL, Webserver, Kamera, UEFI, Kernel). Der Kernel `beispiele/kernel/hallo_kern.moos` baut zu einem ELF, wurde dabei aber nicht gebootet.
- **Reifegrade** stammen aus der Bestandsaufnahme (nur lesend erhoben), nicht aus eigenen Läufen.
- **GPU im KI-Beispiel:** per `MOO_KI_GPU_ERZWINGEN=1` (docs/KI_MIT_MOO.md, Abschnitt 4.5); ob die GPU genutzt wurde, zeigt `gpu_statistik()`. `ki_sprachmodell_gpu.moos` ist ausdrücklich ein Lern-Demonstrator, kein Chatbot.
- Trainingskorpora, eigene Testdaten und Modell-Checkpoints sind absichtlich nicht im Repository.

## Englische Namen und Lücken (Stand 15.11.2026)

`README.en.md` nutzt englische Schlüsselwörter (`spec/tokens.yaml`), die englischen Aliase der Builtins (in `compiler/src/codegen.rs` als "deutsch" | "englisch" hinterlegt) und inzwischen englische Namen für die stdlib-Module `ui`, `ui_test`, `websocket` und `dns`.

### Erledigt (englische stdlib-Namen)

- **Weiterleitungen** in denselben Modulen (es gibt keinen Alias-Mechanismus im Modulsystem): `ui_build`, `ui_debug_build`, `ui_widget_set_id`, `ui_test_snapshot_with_sidecar`, `ui_test_snapshot_series`, `ui_test_click_id`, `ui_test_set_text_id`, `ui_test_action`, `ui_test_sequence`, `ws_connect`, `ws_connect_with_headers`, `dns_resolver`, `dns_resolve`, `dns_type_name`. WebSocket-Methoden: `send_text`, `send_bytes`, `receive`, `close`; DNS-Methode: `resolve`.
- **Zusätzliche englische Dict-Schlüssel:** UI-Container `hWindow`, `width`, `height`; WebSocket-Ereignisse `type`, `data`, `reason`; DNS-Antworten `answers`, je Eintrag `type` und `class`. Die deutschen Namen und Schlüssel bleiben unverändert.
- **Namensregel:** Alle neuen Top-Level-Namen tragen den Modulpräfix (`dns_`, `ws_`, `ui_`). `importiere` inlint alle Moduldefinitionen, und bei gleichem Namen gewinnt die erste Funktion; ein generisches `type_name` hatte deshalb eine gleichnamige Benutzerfunktion still überschrieben. `skripte/stdlib_en/collision.moos` sichert das ab. Gegen alle Funktionen im Index (inklusive `beispiele/`, `compiler/tests`, `skripte/`) wurde geprüft: Keine weitere Funktion trägt einen der neuen Namen.
- `setup_login_form` und `setup_status_bar` waren schon englisch.

### Noch offen

- **Builtins ohne englischen Alias in `codegen.rs`:** `web_json`, `gpu_statistik`, `kern_idt_init`, `kern_pic_remap`, `kern_timer_init`, `kern_alloc` (nur Alias `kern_reserviere`), `interrupts_an` (nur das Mnemonic `sti`). Der Präfix `kern_` ist deutsch; `mse` und `test_frame_save_bmp` sind bereits englisch. Das ist eine Compiler-Änderung und nicht Teil dieser Arbeit.
- **Weitere stdlib-Module:** `ui_komponenten`, `ui_layout`, `ui_aktionen`, `ui_bind`, `ui_liste`, `ui_leinwand`, `ui_introspect`; die Ergebnis-Dicts von `ui_test_aktion`; die DNS-Konstanten (`TYP_A` usw.).
- **Runtime und Treiber:** der Dict-Schlüssel `pfad` der Webanfrage (`web_accept`) und das Bare-Metal-Modul `beispiele/uefi/netz_treiber.moos` (`nt_anzahl`, `nt_log` u. a.).
- **Länge:** Im dynamischen Code heißt sie `len`, nicht `length`; `length` hat sich nur in nativen Funktionen bewährt (im dynamischen Code meldete `pruefe` „Funktion 'length' nicht gefunden“).

### Geprüft am 15.11.2026

- **`moo-compiler pruefe` (Repo-Root als Arbeitsverzeichnis, wegen `stdlib/`):** KI-Training, Voxel, Webserver, Kamera, Kernel (`--no-stdlib --target x86_64-bare`) und die vier stdlib-Schnipsel der README (UI-Login, UI-Snapshot, DNS, WebSocket) bestehen ohne Fehler.
- **Ausgeführt:** `@native`-Quadratsumme (332833500), `int32_be(258)` (`[0, 0, 1, 2]`), die `check`-Funktion, das DNS-Schnipsel (`PASS A value`) und die englische Fassung des `qsort`-Rückrufs mit der erwarteten Ausgabe (0, -2147483648, -3, -3, 5, 2147483647). Fixtures: `bash skripte/stdlib_en/test.sh` (7 von 7) und `bash skripte/stdlib_en/websocket_e2e.sh` (PASS).
- **Nicht geprüft:** der UEFI-Schnipsel (`from "./netz_treiber" import …`), weil er das Modul im selben Ordner braucht; die UI-Schnipsel wurden nur mit `pruefe` geprüft, nicht mit einem Fenster ausgeführt (die englische UI wurde separat mit Xvfb geprüft). Die geprüften Programme sind um Hilfsfunktionen (`check`, `build_packet`, `x`, `target`) ergänzt; die README zeigt Auszüge daraus.

## M-F001/M-F002-Bereinigung der Beispiele (Stand 15.11.2026)

`moo-compiler pruefe --typpruefung` meldet **M-F001** (Aufruf einer werfenden Funktion ohne `versuche:`) und **M-F002** (Builtin, das bei äußerem Fehlschlag wirft: Netz, Datenbank, Laden). Gezählt wird mit `bash skripte/p2/fehler_hinweis/f001_scan.sh <pfade>` (fail-closed: fehlender Pfad, defekter Compiler oder ungültige Diagnose-JSON ergeben Exit 2; Selbsttest `f001_scan_selbsttest.sh`).

**Regel:** Normale Beispiele bekommen bei Fehlern eine Meldung und einen sauberen Rückweg (Rückgabe oder normales Programmende mit `beende(0)`), kein `beende(1)`. Nur Selftests und Gates, deren Rückgabecode etwas aussagt, enden mit `beende(1)`. Im Erfolgsfall ändert sich nichts.

**Bewusst belassene Stellen (Variante b)** tragen direkt über der Aufrufstelle genau einen Kommentar im Format `# FEHLERPFAD(M-F001|M-F002): bewusst ungeschützt – <Grund> – ändern: <was zu tun wäre>`. Alle bewusst belassenen Stellen: `code_intel search FEHLERPFAD(` (Baumansicht: `code_intel tree` mit `show_comments`). Umgestellt sind bisher die Netz-Selftests (`netz_foundation_selftest`, `netz_frame_selftest`, `netz_large_write_selftest`, `dns_selftest`) und in web/db `mini_sql` und `redis_client`; die übrigen Bereiche folgen. Der Kommentar nennt den gemessenen Verlauf des Fehlers, nicht eine Annahme.

**Erledigt (Variante a):**
- **web:** `websocket_server` (`lies_frame` in `versuche:`, `tcp_server`; im Testlauf `beende(1)`), `http_api`, `http_api_cors` (`web_server`), `proxy` (`tcp_verbinde`, `tcp_server`). Scan `beispiele/domain/web`: 0 Hinweise.
- **db:** `adressbuch` (Hilfsfunktionen `sicher_ausfuehren`/`sicher_abfrage`, `db_verbinde` geschützt), `blog_engine`, `blog_engine_v2` (Start: Datenbank, Tabellen, Demo-User, `web_server`), `chat_server_prepared` (Start, `history_holen`, `web_server`), `mysql_client` (`tcp_verbinde`), die vier `PgClient.verbinde` in `postgres_client`, `bench/bench`, `bench/bench_scram`, `bench/pg_test` (Verbindungsfehler -> `letzte_fehler`, `ready` bleibt falsch), `redis_client` (Hauptprogramm).
- **Befund adressbuch:** `db_verbinde("adressbuch.db")` scheiterte schon vorher, weil das Schema `sqlite:///<absoluter Pfad>` nötig ist. Jetzt `sqlite:///tmp/adressbuch.db`; Hinzufügen, Auflisten, Suchen und Ende laufen durch.
- **Host/Port per Umgebung** (Standard unverändert): `MOO_PG_HOST`, `MOO_PG_PORT`, `MOO_PG_SCRAM_PW` in `scram_e2e_smoke` und `postgres_client_smoke`; zusätzlich `MOO_PG_USER`, `MOO_PG_DB` in `pg_test`; `MOO_MYSQL_HOST`/`_PORT`/`_USER`/`_PASS`/`_DB` in `mysql_client`; `MOO_REDIS_HOST`/`_PORT` in `redis_client`.
- **Keine Zugangsdaten im Code** (Review-Befund): Passwörter kommen ausschließlich aus der Umgebung und haben keinen Standardwert mehr. `MOO_PG_SCRAM_PW` (`scram_e2e_smoke`, `postgres_client_smoke`: ohne Wert Meldung und `beende(1)`), `MOO_MYSQL_PASS` (`mysql_client`: Meldung und normales Ende), `MOO_BLOG_ADMIN_PW` (`blog_engine`, `blog_engine_v2`: ohne Wert wird der Admin-User nicht angelegt, der Blog läuft weiter).
- **`PgClient.verbinde` bei erneutem Aufruf** (vier Varianten: `postgres_client`, `bench/bench`, `bench/bench_scram`, `bench/pg_test`): setzt Socket, `ready`, `letzte_fehler`, Server-Version und PID zurück (und SCRAM-Zustand bzw. Prepared-Caches, wo vorhanden). Ein fehlgeschlagener zweiter Versuch lässt kein `ready=wahr` mehr stehen.

**Fehlerfortpflanzung (gemessen, Stand 15.11.2026):** Ein unbehandeltes `wirf` oder ein Fehler eines Builtins in einer Funktion ohne eigenes `versuche:` gibt nur einen Hinweis auf stderr aus; die Funktion läuft mit `nichts` weiter, das Programm endet mit RC 0. Das `versuche:`/`fange` eines Aufrufers erreicht der Fehler nicht zuverlässig (die Meldung kann leer sein, und eine Route, die direkt in der Server-Schleife läuft, liefert trotzdem 200 statt 500).

**Korrektur web/db:** Meine frühere Angabe „Routen werfen bewusst, die Server-Schleife antwortet mit 500“ war falsch. Gemessen mit fehlender Tabelle antwortete `blog_engine` mit HTTP 200 und leerem Inhalt. Beide Blogs rufen die Datenbank jetzt über Hilfsfunktionen mit eigenem `versuche:` auf (`sql_abfrage_sicher`, `sql_ausfuehren_sicher`) und antworten bei SQL-Fehler mit 500; geprüft mit fehlender Tabelle (500, Server läuft weiter) und im Erfolgsfall (Login, Post anlegen, Liste). `redis_client` hing bei geschlossenem Socket endlos (`RespBuf.zeile` ruft `fuellen` in `solange wahr` auf); `zeile` und `bytes` verlassen die Schleife jetzt über `versuche:`.

**Server und Netz-Beispiele (Stand 15.11.2026, gemessen):** `chat/chat_server` (DB-Start, `web_server`, Speichern und History über Hilfsfunktionen mit eigenem `versuche:`; SQL-Fehler in den Routen geben 500), `mini_redis/mini_redis` (`tcp_server`), `mini_redis/test_client` (`tcp_verbinde`), `mqtt_broker` (`tcp_server`) und `playground/*` (`web_server`, vier Dateien) melden Fehler beim Start mit Meldung und normalem Ende. `http_native_selftest` meldet bei belegtem Port `FAIL` mit RC 1; vorher hing der Client am fremden Listener (Timeout, RC 124).

**KI-Beispiele und Selftests (Stand 15.11.2026, gemessen):** Die Lader `mnist_laden` (`ki_mnist`, `eval_m6b_mnist`), `safetensors_laden`, `wav_lesen`, `ki_laden` (`ki_kinderleicht`) laufen in `versuche:` und enden mit Meldung und Hinweis normal; `ki_moe` meldet `CHECK-D FAIL` mit RC 1. In den vier `ki_multimodal_*`-Selftests meldet ein nicht ladbarer Checkpoint `SELFTEST_RESULT: FAIL` mit RC 1, in `ki_sprachmodell_gpu` ein nicht ladbarer Resume-Checkpoint Meldung und normales Ende. `websocket_client_selftest` endete bei einem fehlgeschlagenen Test trotzdem mit `WEBSOCKET-CLIENT PASS`, weil das `wirf` in `pruefe` den Lauf nicht abbrach; jetzt `FAIL` mit RC 1. `ki_verteilt`: GATE 3 des Runners (`ki_verteilt_run.sh`) schlug fehl, weil das `wirf` bei HELLO-Mismatch das Training nicht stoppte; `hello_pruefen`, `sync_pruefen` und `schritt_verteilt` liefern jetzt einen Rückgabewert, Leader und Follower brechen mit Meldung ab, der Runner meldet alle Korrektheits-Gates grün. `hello_pruefen` unterscheidet Mismatch (0), Transportfehler (-1) und ok (1); bei Transportfehler sendet der Follower nichts mehr (vorher schrieb er auf die tote Verbindung und endete bei TCP-Reset mit RC 141). `stdlib/netz_frame`: `nf_lies_genau` und `nf_frame_empfangen` kehren nach einem Fehler mit `nichts` zurück, damit nach einem EOF kein Folgefehler „falsche MAGIC“ entsteht (Erfolgspfad unverändert, `netz_frame_selftest` PASS). **Kein SIGPIPE-Abbruch (b5cb6ee, erledigt):** Schreibt die Runtime auf eine vom Peer zurückgesetzte Verbindung, ist das jetzt ein moo-Fehler statt eines Prozessendes per SIGPIPE (RC 141). Gemessen: Follower-Reset gegen einen Test-Peer, der die Verbindung zurücksetzt, RC 1, eine Meldung, kein SIGPIPE; `ki_verteilt_run.sh` RC 0 mit GATE 1a/1b/2/3 PASS. `stackvm`: Bei Syntaxfehler oder unbekannter Funktion lief die VM mit unvollständigem Bytecode endlos; `run_programm` fängt den Compilerfehler jetzt, meldet ihn und beendet mit RC 1 (vorher „Test übersprungen“ mit RC 0).

**P4-Adapter und -Tests (Stand 15.11.2026, gemessen):** Die Adapter unter `beispiele/p4_silben/` (und `p4_silben_demo`) werden vom Python-Orchestrator mit Umgebungsvariablen gestartet, der den Exitcode prüft (`run_training.py`). Ein Ladefehler (`tokenizer_laden`, `checkpoint_laden`) erzeugte bisher nur einen Hinweis, das Programm lief mit `nichts` weiter und endete mit RC 0 (Probe `logits`). Alle 24 Ladestellen in den Adaptern und die 7 in `beispiele/tests/p4_*` laufen jetzt in `versuche:`, geben eine Meldung aus und enden mit RC 1; Probe: fehlender Tokenizer oder Checkpoint gibt RC 1, `mise run test-p4-moos-api` (`P4 MOOS API OK`) und `test-p4-demo` (`P4 DEMO OK`) bleiben grün. **Fehlerausgang (erledigt für P4):** Die 125 `wirf`-Prüfungen in den P4-Adaptern, `p4_silben_demo` und `tests/p4_*` (fehlende Umgebungsvariablen, ungültige Parameter, fehlgeschlagene Testaussagen) brachen den Lauf nicht ab (RC 0, Folgefehler bis zum Ende). Jede Stelle gibt jetzt die Meldung aus und endet mit `beende(1)`; Probe `logits`, `init_model`, `train_window` ohne Umgebung: eine Meldung, RC 1. Die gleiche Umstellung für `ki_verteilt` (Leader und Follower brechen bei HELLO-, Schritt- oder SYNC-Fehler mit RC 1 ab). Offen: die `wirf`-Prüfungen in `p5` und in anderen Beispielen mit Orchestrator-Exitcode.

**P5 (Stand 15.11.2026, gemessen):** Die Adapter und Tests unter `beispiele/p5/` laufen unter dem Python-Orchestrator mit Exitcode-Prüfung und hatten dasselbe Muster wie P4. 23 Dateien: alle `checkpoint_laden`-Aufrufe und die Daten-Aufrufe `korrektur_fenster`, `korrektur_fenster_aufteilen`, `praferenz_paar`, `trainiere_mit_val` laufen in `versuche:`, geben eine Meldung aus und enden mit RC 1 (Tests mit `FAIL`-Meldung); `optimierer_resume_test` ruft `pruefe` in `versuche:`. Erfolgspfad unverändert: `mise run` für `test-p5-sft`, `test-p5-train-api`, `test-p5-optimierer-resume`, `test-p5-optimierer-sft`, `test-p5-train-orchestrierung`, `test-p5-distill-dpo-resume` und `test-p5-pilot` sind PASS. Drei Bibliotheksstellen sind bewusst belassen (FEHLERPFAD-Kommentar): `sft_adapter` (`sft_fenster`), `praeferenz_adapter` (`korrektur_fenster_aufteilen`), `grimm_langtext_sampling` (`ziehe_topk`).

**stdlib (Stand 15.11.2026, gemessen):** Die Module `websocket` (25 Aufrufe), `dns` (10), `netz3d` (8), `netz_frame` (2) und `ki_bild_easy` (1) werfen Fehler per Vertrag an den Aufrufer (bewusst belassen, je Aufruf ein `FEHLERPFAD(`-Kommentar). Gemessen: mit `versuche:` beim Aufrufer wird gefangen (die Meldung kann der letzte Folgefehler sein); ohne nur Hinweis und die Funktion läuft weiter (RC 0). Ein Hänger bei sofort schließendem Peer tritt bei `websocket` nicht auf (anders als vor der Korrektur in `netz_frame`). Der Scan meldet alle 33 Stellen mit Kommentar direkt davor. `stdlib/world.moos` besteht `pruefe` nicht (M-S003, unverändert, nicht Teil dieser Arbeit).

**Bewusst belassen (Variante b), je Aufruf ein `FEHLERPFAD(`-Kommentar:**
- `stackvm.moos` (16 rekursive Aufrufe von `parse_one`, `compile_expr`, `compile_fun`; der Fehler erreicht das `versuche:` in `run_programm`) und `stdlib/netz3d.moos` (8 Aufrufe von `netz3d_fehler`; der Vertrag ist „fangbar“, `netz3d_gitter` fängt und beendet das Fenster sauber).
- `fluxmail/fluxmail_smtp.moos` (`tls_verbinde`; der einzige Aufrufer `smtp_senden` fängt in `versuche:`, gemessen) und `fluxmail/fluxmail_imap.moos` (`tls_verbinde`; `imap_verbinden` hat keinen Aufrufer im Projekt).
- `db/mini_sql.moos`: 32 Stellen (`erwarte_kw`, `erwarte_typ`, `parse_insert`, `Parser.parse`, `Engine.run`); Parser und Engine melden Syntaxfehler mit `wirf`.
- `db/redis_client.moos`: 10 Stellen (9 × `redis_befehl` in den `redis_*`-Wrappern, 1 × `tcp_verbinde` in `redis_verbinde`; das Testprogramm fängt den Verbindungsfehler und endet normal).

**Geprüft** gegen die lokale Test-PostgreSQL (`127.0.0.1:5434`, nicht 5432/5433): `scram_e2e_smoke` 10 ok / 0 fail, `postgres_client_smoke` 10 ok / 0 fail, `pg_test` 31 von 31. Redis 7 (`127.0.0.1:6380`): `redis_client` 10 OK / 0 FAIL. MySQL 8.0 (`127.0.0.1:3307`, Benutzer `moo`, DB `moo_test`, `MOO_MYSQL_USER=moo MOO_MYSQL_PASS=moo_lokal MOO_MYSQL_DB=moo_test`): `mysql_client` läuft bis „Fertig“ (Test 6 ist ein gewollter Fehlertest). Ohne Datenbankserver melden beide den Verbindungsfehler und enden normal. SQLite: `chat_server_prepared` (Senden und Abfragen), `blog_engine` und `blog_engine_v2` (Startseite HTTP 200), `adressbuch` (Ende-zu-Ende).

**Noch offen / Stand des Scans (15.11.2026):** Der Scan (`f001_scan.sh beispiele stdlib`) meldet weiterhin Hinweise, darunter in `stdlib/dns`, `stdlib/websocket`, `stdlib/netz_frame` und `stdlib/ki_bild_easy`. Bei den stdlib-Modulen sind das laut der Messung oben die bewusst belassenen Stellen mit `FEHLERPFAD(`-Kommentar (Fehler werden per Vertrag an den Aufrufer geworfen). Für die übrigen Bereiche liegt keine neue Gesamtzählung vor; die frühere Liste offener Bereiche ist durch die Bereinigungen oben überholt. Nicht abbrechende `wirf`-Prüfungen gibt es weiterhin in `beispiele/p5/` (z. B. `grimm_langtext_sampling_20261030.moos`) und in weiteren Beispielen mit Orchestrator-Exitcode.

## Root-Inhalt

Dauerhaft im Root: `README.md`, `README.en.md`, `ARCHITECTURE.md`, `DEPENDENCIES.md`, `pyproject.toml`, `.gitignore`, `.github/`. Es gibt derzeit keine `LICENSE`-Datei. Alles andere sind Code- oder Doku-Verzeichnisse. Bitte keine losen `test_*.moos` im Root anlegen (siehe `scratch/README.md`).
