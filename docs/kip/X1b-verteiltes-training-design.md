# KIP-X1b — Verteiltes Training über 2 Linux-Hosts (Design, EVAL + PoC)

Status: ENTWURF für GPT-Gegenreview (Pflicht-Gate vor Implementierung).
Task: faba3a00-4fa5-4172-bc70-1ffa37ec7fc7. Vorgänger-Befund KIP-X1: kein lokales Single-Node-Multi-GPU — bleibt gültig.
Ziel: Data-Parallel-Training über Netz: 4070 Ti (lokal, CachyOS) + RTX 2070 (Unraid-Server).

## 1. EVAL-Befunde (gemessen 2026-07-16)

### 1.1 Interconnect
- Latenz lokal ↔ Unraid (192.168.50.10 ↔ 192.168.50.65): **0,2 ms avg** (ping, 0% loss) — unkritisch.
- Durchsatz (iperf3, TCP, 8 s): **94 Mbit/s ≈ 11,3 MB/s netto**.
- Ursache: lokale NIC (enp59s0, 2,5-GbE-fähig, advertised bis 2500baseT/Full) handelt nur **100 Mb/s Full** aus; Unraid-Seite hat 1000 Mb/s. Verdacht: Kabel (nur 2 Adernpaare nutzbar) oder Switch-Port. **HARDWARE-FOLGEPUNKT für den User — kein Software-Fix.** Nach Fix: ~117 MB/s realistisch.
- Konsequenz Sync-Budget beim Demonstrator-Modell (~120k Parameter, f32 = 480 KB): Voll-Austausch pro Step ≈ 2 × 480 KB ≈ **85 ms bei 94 Mbit/s** bzw. ≈ 8 ms bei 1 GbE. Für den PoC tragbar; für M-A (10M Parameter, 40 MB) bei 100 Mbit **untragbar** (≈ 7 s/Step) → 1-GbE-Fix ist Voraussetzung für alles jenseits des PoC; bf16-Kompression und Local-SGD sind dokumentierte Eskalationsstufen.

### 1.2 RTX 2070 auf dem Unraid-Host
- GPU frei am Host sichtbar (nvidia-smi, Treiber 570.86.16, keine Compute-Prozesse); Win10-VM lief dabei OHNE GPU-Passthrough.
- Docker: nvidia-Runtime + nvidia-container-toolkit vorhanden. Devices (/dev/nvidia*) und Treiber-Userland (inkl. libGLX_nvidia, glvkspirv, glsi, gpucomp) werden in Container gemountet, ldconfig-Cache korrekt.
- **Befund Vulkan im Container: NEGATIV (Stand heute).** `vk_icdNegotiateLoaderICDInterfaceVersion` des Unraid-Plugin-Treibers liefert `VK_ERROR_INITIALIZATION_FAILED (-3)` — ohne jeden Device-/X11-Zugriff (strace-verifiziert), sowohl mit Ubuntu-24.04- als auch 25.04-Loader. dlopen + alle NEEDED-Deps sauber. Gleicher ctypes-Test direkt auf dem Host nicht möglich (kein python3 auf Unraid). Ursache noch offen (Verdacht: Plugin-Build der Userland-Libs). NICHT weiter am Host-Treiber drehen (User-Vorgabe: keine Treiber-Änderungen am Unraid).
- **Alternative bereits vorhanden:** LinuxMint-VM auf dem Unraid hat PCI-Passthrough konfiguriert (managed hostdev). Weg C in §5.

### 1.3 Vorhandene Moo-Bausteine (Code-Inventur)
- Netz (compiler/runtime/moo_net.c, Moo-Builtins): `tcp_server(port)`, `tcp_verbinden(host, port)` (moo_tcp_connect), `sock.annehmen()`, `sock.lesen_bytes(n)` / `sock.schreibe_bytes(liste)`, `sock.timeout_setzen(ms)`, `bytes_zu_text`/`text_zu_bytes`. Byte-Level-Protokolle in Moo bewiesen (beispiele/mqtt_broker.moos: MQTT 3.1.1 komplett).
- Training auf Moo-Ebene bewiesen (beispiele/ki_sprachmodell.moos, Plan-014 G1): `schicht_*`-Bau, `parameter(alle)`, `kreuzentropie`, `verlust.rueckwaerts()`, `gradienten_kappen(params, 1.0)`, `optimierer_sgd(params, rate, momentum)`, `opt.schritt()`, deterministische Seeds, Loss-Checkpoint-Gate.
- Gradienten: `t.gradient()` (lesen, Download aus gpu_grad vorhanden), `t.gradient_loeschen()`. **LÜCKE: kein `gradient_setzen` auf Moo-Ebene** → Runtime-Erweiterung nötig (§4).
- GPU-resident (C-Ebene, KIP-Grundstock): kompletter LM-Trainings-Step resident bewiesen (G4/G4b), Adam resident, Telemetrie. Für PoC-Phase B/C relevant, Phase A ist CPU-only.

## 2. Entscheid Gradient-Sync-Strategie

**ENTSCHEID: synchrones Gradient-Averaging pro Step, direkter TCP-Austausch, Leader/Follower.**

Begründung:
- Bei N=2 Nodes degeneriert Ring-All-Reduce zum paarweisen Austausch — ein separater Parameter-Server wäre reiner Overhead. Der "naive" synchrone Austausch IST bei 2 Nodes die kanonische Form.
- Synchron (nicht async) wegen des Task-Gates: nur synchrones Averaging ist deterministisch gegen eine lokale Referenz beweisbar.
- Rollen: Leader (bindet Port, orchestriert Steps, hält die Referenz-Seeds) / Follower (verbindet). Beide rechnen Forward+Backward auf disjunkten Batches, tauschen Gradienten, wenden identisch gemittelte Gradienten mit identischem Optimizer-State an → Parameter bleiben auf beiden Nodes bit-synchron (Drift-Check per Prüfsumme, §3.4).
- Heterogene GPUs (12 GB vs 8 GB, ungleiche Rechenleistung): im synchronen Betrieb bestimmt der langsamere Node den Takt. PoC akzeptiert das ehrlich (Phase A/B: gleicher Batch-Split). Ungleicher statischer Batch-Split (z. B. 60/40 mit Loss-Gewichtung nach Token-Anteil) ist als dokumentierte Folgeoption spezifiziert, NICHT im PoC-Gate.
- Eskalationsstufen bei Bandbreiten-Not (nur dokumentiert, nicht PoC): (a) bf16-Gradienten-Serialisierung (halbiert Volumen; Toleranz-Vertrag nach E2b-Muster nötig), (b) Local-SGD / Parameter-Averaging alle K Steps (bricht das Bit-Identitäts-Gate, braucht eigenes Konvergenz-Gate).

## 3. PoC-Design (Phase A: CPU, localhost — DANN erst Cross-Host)

### 3.1 Programm
Neues Beispiel `beispiele/ki_verteilt.moos` (abgeleitet aus ki_sprachmodell.moos, Modell/Seeds/LR-Plan identisch). Aufruf:
- Leader: `MOO_DIST_ROLLE=leader MOO_DIST_PORT=5299 ./ki_verteilt`
- Follower: `MOO_DIST_ROLLE=follower MOO_DIST_HOST=127.0.0.1 MOO_DIST_PORT=5299 ./ki_verteilt`
- Referenz: `MOO_DIST_ROLLE=lokal ./ki_verteilt` (rechnet beide Batches selbst — exakt die Zwei-Block-Konstruktion des bestehenden Beispiels, aber mit getrennten Backwards, §3.3).

### 3.2 Protokoll MOODIST01 (über tcp_server/tcp_verbinden, Byte-Listen)
- HELLO: Magic "MOODIST01", Protokoll-Version, Modell-Fingerprint (Param-Anzahl gesamt + pro Tensor Shape-Liste), Seed-Set, Schrittzahl. Mismatch → harter Abbruch.
- Pro Step: GRAD-Frame je Richtung = Step-Nr (u32) + Param-Gesamtzahl (u32) + f32-Payload (little-endian, Parameter-Reihenfolge = parameter(alle)-Reihenfolge) + CRC32. Step-Nr-Mismatch/CRC-Fehler → Abbruch (kein Retry im PoC).
- Serialisierung: f32↔4-Byte-IEEE754 in Moo (Moo-Zahlen sind double, f32 verlustfrei einbettbar). Falls Profiling zeigt, dass die Moo-Schleife dominiert: optionales Builtin `tensor_zu_bytes`/`tensor_aus_bytes` (§4b) — erst nach Messung, nicht präventiv.
- Alle 100 Steps: SYNC-Frame mit Parameter-Prüfsumme (FNV-1a über f32-Bits aller Parameter) → beweist Bit-Synchronität der Replikate im Lauf.

### 3.3 Gate (deterministisch, bit-ehrlich)
Mathematische Identität wird KONSTRUIERT, nicht erhofft:
- Verteilt: Node A: Backward auf Block 1 → g1; Node B: Backward auf Block 2 → g2; Austausch; beide: g_avg = (g1 + g2) * 0.5 elementweise in fester Reihenfolge (erst eigener, dann fremder Gradient — nach Param-Index aufsteigend).
- Lokale Referenz: SELBER Prozess rechnet v1.rueckwaerts() → g1 sichern, gradient_loeschen, v2.rueckwaerts() → g2, dann identisches g_avg = (g1 + g2) * 0.5. (BEWUSST NICHT (v1+v2)*0.5 über den Tape — andere f32-Akkumulationsreihenfolge, wäre nur toleranz-vergleichbar.)
- GATE 1: Loss-Folge Leader == Follower == lokale Referenz **bit-identisch** über ≥ 200 Steps (f32-Bits vergleichen, nicht Dezimaldruck).
- GATE 2: Parameter-Prüfsummen Leader == Follower an jedem SYNC-Frame.
- GATE 3 (Ehrlichkeits-Gate): Lauf mit absichtlich verschiedenem Seed auf dem Follower MUSS an HELLO bzw. GATE 2 scheitern (Negativ-Test).
- GATE 4: Wallclock-Protokoll: Step-Zeit lokal vs. verteilt inkl. Sync-Overhead (ehrliche Zahlen; bei 100 Mbit wird verteilt LANGSAMER sein als lokal — der PoC beweist Korrektheit, nicht Speedup; das steht so im Log).

### 3.4 Determinismus-Voraussetzungen
- Feste Seeds (bestehendes Muster), keine Dropout-Schicht im PoC-Modell, CPU-f32-Ops single-threaded deterministisch (bestehende Runtime), identisches Binary auf beiden Nodes (Hash im HELLO-Log), gleiche Korpus-Datei (Hash im HELLO).

## 4. Nötige Runtime-Erweiterungen (einzige Code-Änderungen außerhalb des Beispiels)
(a) **`t.gradient_setzen(quelle)`** (moo_autograd.c + codegen): schreibt f32-Werte (aus Tensor oder Liste) in t->grad. Vertrag: analog gradient_loeschen; grad_valid-Maske gemäß G0 §1 (Host-Write → V_DATA autoritativ, DEV-Bit löschen); Fehler bei Shape-Mismatch; Negativ-Tests (kein Tape-Node nötig? — Parameter haben mit_gradient, nur solche erlaubt).
(b) OPTIONAL nach Messung: `tensor_zu_bytes(t)` / `tensor_aus_bytes(bytes, form)` (f32 LE raw) für schnelle Serialisierung.
Beides mit ASan-Tests nach bestehendem Muster (run_sanitize), keine Änderungen an Tape-Kern oder GPU-Pfaden in Phase A.

## 5. Phasenplan
- **Phase A (dieses Design, Gate §3.3): CPU, 2 Prozesse auf localhost.** Kein Netz-Risiko, kein GPU-Risiko — beweist Protokoll + Mathematik. Läuft komplett auf dem lokalen Rechner.
- **Phase B: Cross-Host CPU** (lokal ↔ Unraid-Docker mit Moo-Binary; Unraid-Container braucht NUR CPU) — beweist echtes Netz (94 Mbit/s), gleiche Gates. Moo+Docker-Image: schlankes Debian/Ubuntu-Base + Moo-Binary + Korpus, via docker run auf Unraid (kein Treiber-Eingriff).
- **Phase C: GPU-Beteiligung.** Lokal 4070 Ti resident (KIP-Grundstock-Pfad); Unraid-2070 über den zu klärenden Vulkan-Weg: bevorzugt LinuxMint-VM mit vorhandenem PCI-Passthrough (voller Distro-Vulkan-Stack, kein Unraid-Treiber-Eingriff), Container-ICD-Problem bleibt dokumentierter Befund. GPU-Läufe bekommen Toleranz-Verträge nach E2b-Muster (f32-GPU vs f32-CPU nicht bit-identisch) — eigenes Gate-Design VOR Phase C als Addendum.
- Voraussetzung für sinnvolle Skalierung über den PoC hinaus: 1-GbE-Link-Fix (Hardware, User).

## 6. Offene Fragen an GPT (Gegenreview)
(1) Gate-Konstruktion §3.3 (getrennte Backwards als lokale Referenz für Bit-Identität) — akzeptiert?
(2) gradient_setzen-Vertrag §4a ausreichend eng (nur mit_gradient-Parameter, grad_valid-Handling)?
(3) Phasenreihenfolge A→B→C ok, oder GPU früher erzwingen?
(4) CRC32+FNV-1a als Integritäts-/Synchronitätsnachweis im PoC ausreichend (kein TLS, LAN-only)?
(5) Ist das Wallclock-Ehrlichkeits-Gate (GATE 4, verteilt darf langsamer sein) als PoC-Abschluss akzeptabel?

## 7. GPT-Gegenreview auf Codebasis (2026-07-17)

**Review-Status: Designentscheidung grundsätzlich akzeptiert, Implementierungsfreigabe noch nicht.**

Die Gegenprüfung erfolgte gegen die aktuelle Codebasis im Projekt `moo`, insbesondere gegen:
- `compiler/runtime/moo_net.c` und die Netzwerk-Bindings,
- `compiler/runtime/moo_autograd.c`,
- `compiler/runtime/moo_nn.c`,
- `compiler/src/runtime_bindings.rs` und `compiler/src/codegen.rs`,
- vorhandene Moo-Netzwerk- und Trainingsbeispiele.

### 7.1 Bestätigte Grundlagen

- Die benötigten TCP-Grundfunktionen sind vorhanden: Server, Connect, Accept, Byte-Lesen/-Schreiben und Timeouts.
- Binäre Protokolle auf Moo-Ebene sind durch vorhandene Beispiele praktisch belegt.
- `gradient()` materialisiert auch GPU-residente Gradienten korrekt auf der CPU-Seite.
- `gradient_loeschen()` setzt die CPU-Seite anschließend als autoritativ (`grad_valid = MOO_V_DATA`).
- Ein öffentlicher Schreibpfad für Gradienten fehlt tatsächlich. `gradient_setzen` ist daher eine echte notwendige Runtime-Erweiterung.
- Der CPU-SGD-Pfad arbeitet elementweise in `float`; bei identischem Gradient, identischem Parameterstand und identischem Optimizer-State sind bitidentische Folgezustände auf demselben Host realistisch.

### 7.2 BLOCKER 1 — GATE 1 ist in der aktuellen Form logisch falsch

Die Forderung

> Loss-Folge Leader == Follower == lokale Referenz bit-identisch

kann nicht gelten, wenn Leader Block 1 und Follower Block 2 verarbeiten. Vor dem Gradientenaustausch entstehen unterschiedliche lokale Losswerte.

**Korrektur:**
- Leader-Loss muss bitidentisch zum lokalen Referenz-Loss für Block 1 sein.
- Follower-Loss muss bitidentisch zum lokalen Referenz-Loss für Block 2 sein.
- Optional wird ein globaler Referenz-Loss definiert als `(loss_block1 + loss_block2) * 0.5`.
- Nach Gradient-Averaging und Optimizer-Schritt müssen Parameter und Optimizer-State identisch sein.

### 7.3 BLOCKER 2 — TCP-Framing ist noch nicht vollständig spezifiziert

TCP garantiert nicht, dass `lesen_bytes(n)` die komplette angeforderte Nutzlast in einem Aufruf liefert. Das Protokoll braucht zwingend:

- einen festen Frame-Header,
- ein explizites Payload-Längenfeld,
- eine `lies_genau(n)`-Schleife,
- ein maximales Frame-Größenlimit,
- eine feste Kommunikationsreihenfolge zur Deadlock-Vermeidung.

Empfohlene Reihenfolge bei zwei Nodes:
- Leader sendet zuerst und liest danach.
- Follower liest zuerst und sendet danach.

Der Frame-Header sollte mindestens Magic, Version, Frame-Typ, Step, Payload-Länge, Param-Anzahl und CRC32 enthalten.

### 7.4 BLOCKER 3 — Gradient-Clipping muss nach dem Averaging erfolgen

Falls `gradienten_kappen(params, 1.0)` verwendet wird, muss die Reihenfolge sein:

1. lokaler Forward/Backward,
2. Gradientenaustausch,
3. elementweises globales Averaging,
4. Clipping des gemittelten Gradienten,
5. Optimizer-Schritt.

Lokales Clipping vor dem Austausch ist mathematisch nicht identisch zum Clipping des global gemittelten Gradienten und würde das Referenz-Gate verfälschen.

### 7.5 BLOCKER 4 — Modell-Fingerprint muss Parameteridentität absichern

Param-Gesamtzahl und Shape-Liste reichen nicht aus. Zwei Parameter gleicher Form könnten vertauscht werden.

Der HELLO-Fingerprint muss pro Parameter mindestens enthalten:
- stabilen Index,
- stabilen Namen/Pfad, soweit verfügbar,
- Datentyp,
- Dimensionen und Elementanzahl,
- Hash der Initialdaten.

Zusätzlich sollten Binary-, Korpus-, Build-/Runtime- und Seed-Fingerprints übertragen werden.

### 7.6 Präzisierter Vertrag für `gradient_setzen`

Empfohlener Vertrag für `t.gradient_setzen(quelle)`:

- Ziel und Tensorquelle müssen Tensoren sein; alternativ ist eine flache Zahlenliste erlaubt.
- Ziel muss `requires_grad == true` besitzen. Eine sichere Prüfung auf „echter Modellparameter“ existiert derzeit nicht; daher nicht enger formulieren.
- Exakte Elementanzahl muss übereinstimmen; bei Tensorquelle zusätzlich Shape-Gleichheit prüfen.
- Der Host-Gradientenpuffer wird bei Bedarf angelegt.
- Werte werden als f32 in `t->grad` geschrieben.
- Danach gilt ausschließlich `t->grad_valid = MOO_V_DATA`; ein vorhandener GPU-Gradient ist stale.
- Es wird kein Tape-Knoten erzeugt.
- Selbstzuweisung muss definiert funktionieren.
- Shape-/Typfehler brechen hart ab und dürfen keinen Teilzustand hinterlassen.

### 7.7 Zusätzliche Gates

Der SYNC-Nachweis sollte nicht nur Parameter, sondern auch Optimizer-State umfassen:
- Momentum beziehungsweise Adam `m`/`v`,
- Optimizer-Step `t`,
- Lernrate,
- Step-Nummer.

Für den ersten PoC ist SGD ohne Momentum die kleinste beweisbare Variante. Momentum kann danach als eigenes Gate ergänzt werden.

### 7.8 Bitidentität localhost versus Cross-Host

- **Phase A localhost:** Bitidentität ist ein angemessenes Pflicht-Gate.
- **Phase B Cross-Host:** Bitidentität zuerst verlangen, aber CPU-/Compiler-/FMA-Unterschiede als mögliche Ursache prüfen. Falls ausschließlich hardwarebedingte Unterschiede auftreten, ist vor einer Lockerung ein eigener ULP-/Absolut-Toleranzvertrag zu dokumentieren.

### 7.9 Antworten auf die offenen Fragen

1. **Getrennte Backwards:** akzeptiert; das gemeinsame Loss-Gate muss jedoch wie in §7.2 korrigiert werden.
2. **`gradient_setzen`:** grundsätzlich akzeptiert, mit dem präzisierten Vertrag aus §7.6.
3. **Phasenfolge A → B → C:** akzeptiert; GPU nicht früher erzwingen, damit Protokoll-, Autograd- und GPU-Fehler getrennt bleiben.
4. **CRC32 + FNV-1a:** für den LAN-PoC ausreichend; finaler SHA-256-Abschlussnachweis empfohlen. Kein Authentizitätsschutz.
5. **Wallclock-Gate:** akzeptiert. Der PoC beweist Korrektheit und misst Overhead, nicht zwingend Speedup.

### 7.10 Freigabebedingung

Phase A kann implementiert werden, sobald mindestens diese Punkte in das Design übernommen sind:

1. korrigiertes Loss-/Referenz-Gate,
2. vollständiges TCP-Framing mit `lies_genau`, Größenlimit und Rollenreihenfolge,
3. Gradient-Clipping erst nach globalem Averaging,
4. stärkerer Modell-/Parameter-Fingerprint,
5. Optimizer-State im Synchronitätsnachweis.


## 8. Design-Update: Übernahme der Review-Auflagen (2026-07-17, verbindlich für Phase A)

Dieser Abschnitt übernimmt alle 5 Freigabebedingungen aus §7.10 und ÜBERSCHREIBT die
betroffenen Stellen in §3/§4. Bei Widerspruch gilt §8.

### 8.1 GATE 1 korrigiert (ersetzt §3.3 GATE 1)
- GATE 1a: Leader-Loss-Folge (Block-1-Batches) == lokale Referenz-Loss-Folge für Block 1, **bit-identisch** über ≥ 200 Steps (f32-Bits).
- GATE 1b: Follower-Loss-Folge (Block-2-Batches) == lokale Referenz-Loss-Folge für Block 2, bit-identisch.
- GATE 1c (protokolliert, informativ): globaler Referenz-Loss `(loss_b1 + loss_b2) * 0.5` je Step im Log.
- Identität der REPLIKATE wird über GATE 2 (Parameter + Optimizer-State nach jedem Averaging-Schritt, §8.5) bewiesen — nicht über Loss-Gleichheit zwischen Leader und Follower (die rechnen verschiedene Blöcke; Loss-Gleichheit dort wäre logisch falsch, Review §7.2).

### 8.2 MOODIST01 v2 — vollständiges TCP-Framing (ersetzt GRAD-Format in §3.2)
- **Frame-Header (24 Bytes, little-endian):** MAGIC u32 ("MD01"), VERSION u16, TYP u16 (1=HELLO, 2=GRAD, 3=SYNC, 4=BYE), STEP u32, PARAM_ANZAHL u32, PAYLOAD_LAENGE u32, CRC32 u32 (über Header ohne CRC-Feld + Payload).
- **`lies_genau(sock, n)`:** Schleife über `sock.lesen_bytes(rest)` bis exakt n Bytes gelesen (TCP liefert Teilstücke!); Timeout/EOF → harter Abbruch.
- **MAX_FRAME:** 64 MB hart; PAYLOAD_LAENGE darüber → Abbruch vor jeder Allokation.
- **Rollen-Reihenfolge (Deadlock-frei):** Leader sendet zuerst und liest danach; Follower liest zuerst und sendet danach. Gilt für jeden Step und jeden Frame-Typ.
- **MOOS-OS-KONTEXT (User-Entscheid 2026-07-17):** Moo soll mittelfristig Richtung eigenes OS wachsen — Netzwerkfunktionen/Protokolle werden ohnehin ausgebaut. Der Framing-Layer (Header, lies_genau, CRC, Größenlimit) wird deshalb als EIGENSTÄNDIGES, wiederverwendbares Moo-Modul geschnitten (`bibliothek/netz_frame.moos` o. ä.); MOODIST01 ist nur der erste Nutzer. Kein Ad-hoc-Framing im Trainingsbeispiel.

### 8.3 Gradient-Clipping erst nach globalem Averaging (Review §7.4)
Verbindliche Reihenfolge pro Step: (1) lokaler Forward/Backward → (2) Gradientenaustausch → (3) elementweises Averaging in fester Reihenfolge → (4) `gradient_setzen(g_avg)` auf die Parameter → (5) `gradienten_kappen(params, 1.0)` auf den GEMITTELTEN Gradient → (6) `opt.schritt()`. Lokale Referenz identisch. Lokales Clipping vor dem Austausch ist VERBOTEN (verfälscht das Referenz-Gate).

### 8.4 Modell-Fingerprint v2 (ersetzt HELLO-Fingerprint in §3.2)
HELLO enthält pro Parameter: stabilen Index (parameter(alle)-Reihenfolge), Namen/Pfad soweit verfügbar, Datentyp, Dimensionsliste, Elementanzahl, FNV-1a-Hash der Initialdaten. Global zusätzlich: Binary-SHA256, Korpus-SHA256, Build-/Runtime-Kennung, komplettes Seed-Set, Schrittzahl. JEDER Mismatch → harter Abbruch (kein Teil-Match).

### 8.5 SYNC v2 — Optimizer-State im Synchronitätsnachweis (Review §7.7)
SYNC-Frame (alle 100 Steps) prüft per FNV-1a: Parameter-Bits UND Optimizer-State (Momentum-Puffer soweit vorhanden, Step-Nummer, Lernrate). **PoC-Erststufe: SGD OHNE Momentum** (kleinste beweisbare Variante); Momentum danach als eigenes Folge-Gate, Adam erst mit Phase C.

### 8.6 `gradient_setzen`-Vertrag v2 (übernimmt Review §7.6 wörtlich als Spezifikation)
Ziel muss Tensor mit `requires_grad == true` sein; Quelle Tensor (Shape-Gleichheit) oder flache Zahlenliste (Elementanzahl exakt); Host-Grad-Puffer wird bei Bedarf angelegt; Werte als f32 nach `t->grad`; danach `grad_valid = MOO_V_DATA` (GPU-Gradient stale); kein Tape-Knoten; Selbstzuweisung definiert; Shape-/Typfehler brechen hart ab ohne Teilzustand. ASan-Positiv- und Negativ-Tests Pflicht.

### 8.7 Bitidentität Cross-Host (Review §7.8)
Phase B fordert zunächst Bit-Identität. Treten Abweichungen auf, sind CPU-/Compiler-/FMA-Unterschiede als Ursache nachzuweisen; erst DANN darf ein dokumentierter ULP-/Absolut-Toleranzvertrag (E2b-Muster) die Lockerung definieren — nie stillschweigend.

### 8.8 Status
Alle 5 Freigabebedingungen aus §7.10 sind hiermit ins Design übernommen. **Phase A (CPU, localhost) ist implementierungsbereit.** Erste Implementierungsschritte: (1) `netz_frame.moos`-Modul + Selftest, (2) `gradient_setzen` (Runtime + ASan-Tests), (3) `ki_verteilt.moos` mit den korrigierten Gates.
