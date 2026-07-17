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
