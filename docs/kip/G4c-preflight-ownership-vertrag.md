
# KIP-G4c-PREFLIGHT — Ownership-/Kohärenz-Vertrag für Production-Wiring

Owner: **kip-kern** · Task `9d808cf8-53c0-4a77-9c35-d5d3410114ec` (in_progress)
Stand HEAD bei Erstellung: `f6b20d9` (nach kip-gpu Phase 1, G4c-Lücke dokumentiert)
Nur Analyse — keine Änderungen an moo_nn.c / moo_autograd.c / moo_tensor_ops.c / moo_runtime.h.

Referenzen: `docs/kip/G4c-production-wiring-plan.md` (kip-gpu, kanonisch), `docs/kip/G1-device-modell-design.md`,
`docs/kip/G0-gpu-vertrag-inventur.md`, `moo_runtime.h:126-171` (MooTensor-Struct, KIP-STRUCT f2cbebc7).

---

## 1. Ergebnis vorweg

**Kein MooTensor-Struct-Redesign nötig.** Bestätige kip-gpus Befund (Msg 12811): `dtype/valid/grad_valid/
device/store/gpu_buf/gpu_grad` existieren bereits (KIP-STRUCT f2cbebc7), sind vollständig für das G4c-Ziel
ausgelegt. Die Struct-Frage aus dem ursprünglichen Auftrag ist damit **erledigt, ohne Codeänderung**.

**Der eigentliche Preflight-Befund liegt woanders:** Das bestehende valid-Masken-System schützt heute nur
die **Lese-Trichter in `moo_tensor_ops.c`/`moo_tensor.c`** (`expect_t`/`expect_tensor` rufen
`moo_tensor_f32_sichern` vor jedem `->data`-Zugriff). **`moo_autograd.c` (alle `bw_*`-Funktionen) und
`moo_nn_opt_schritt` lesen/schreiben `->data` und `->grad` komplett UNGESICHERT direkt** — kein
Sichern-Aufruf, keine valid-Prüfung. Das ist im reinen CPU-Modell folgenlos (data ist immer autoritativ),
wird aber zur **Kern-Gefahrenzone**, sobald kip-gpu Resident-Buffer in den Trainingsschritt einwebt: ein
Op, der nur noch `MOO_V_DEV` setzt (Ergebnis bleibt auf der GPU), macht jeden direkten `->data[i]`-Zugriff
in Backward/Optimizer zu einem **Stale-Read auf veraltetem/nie geschriebenem Host-Speicher** — silent
wrong numbers, kein Crash, kein ASan-Treffer.

---

## 2. Bestandsaufnahme: DType-/Valid-Vertrag (bestätigt korrekt, unverändert gültig)

- `valid` ist EINE geteilte Bitmaske über `data`/`store`/`gpu_buf` (`MOO_V_DATA|MOO_V_STORE|MOO_V_DEV`).
  Invariante `valid != 0` gilt bereits im ganzen Code (`moo_tensor_setzen`, `moo_nn_opt_schritt` setzen
  nach jedem Host-Write explizit `valid = MOO_V_DATA` — korrekte Mutations-Invalidierung, D0 §4.2).
- `grad_valid` ist eine **eigene** Maske (`MOO_V_DATA|MOO_V_DEV`, kein STORE — Grad hat kein bf16).
  `grad_valid == 0` ist erlaubt (kein Grad-Beitrag). Diese Maske wird **im Code heute nirgends gesetzt
  oder gelesen** — `grad_valid` existiert nur als Struct-Feld, das gesamte Backward arbeitet, als wäre
  `grad` immer `MOO_V_DATA`. Das ist der Haken: G4c muss `grad_valid` erst **einführen**, nicht nur nutzen.
- `moo_tensor_f32_sichern`/`_host_sichern`/`_nach_gpu`/`_nach_cpu` sind bereits korrekt implementiert und
  idempotent (`moo_tensor.c:261-333`). Sie sind die **einzigen** sicheren Materialisierungspunkte — jeder
  neue Resident-Pfad MUSS über sie laufen, niemals `t->gpu_buf`/`t->data` direkt mischen.

## 3. Host↔Device-Kohärenz — konkrete Lücke

Zwei Klassen von Zugriffen existieren nebeneinander:

| Zugriffsklasse | Ort | Sicherung heute |
|---|---|---|
| Forward-Ops (Registry) | `moo_tensor_ops.c` via `expect_t`/`expect_tensor` | ✅ `moo_tensor_f32_sichern` vor jedem Read |
| Backward (`bw_*`) | `moo_autograd.c` | ❌ direkter `->data`/`->grad`-Zugriff, KEIN Trichter |
| Optimizer (`opt_schritt`) | `moo_nn.c:2294` | ❌ direkter `p->data`/`p->grad`-Zugriff, KEIN Trichter |
| Grad-Akkumulation (Fan-out `zg[i]+=`) | `moo_autograd.c` (`grad_sicherstellen`, `accum_bcast`) | ❌ kein Trichter, `grad` wird als IMMER-f32-autoritativ angenommen |

**Konkretes Fehlerbild, wenn kip-gpu ungeschützt verdrahtet:** Sobald `bw_matmul` o.ä. GPU-resident läuft
und nur `gpu_grad` beschreibt (`grad_valid = MOO_V_DEV`, `t->grad` bleibt NULL/stale), aber `moo_nn_opt_schritt`
weiterhin `p->grad[j]` direkt liest (kein Sichern-Aufruf) → Optimizer sieht NULL-Pointer-Crash oder (falls
`grad` aus einer früheren Iteration noch belegt ist) **stille, falsche alte Gradienten**. Gleiches gilt für
jede `bw_*`-Funktion, die ein Input `->data` liest, während der Forward-Wert nur noch auf der GPU aktuell ist.

## 4. Refcount/Ownership — Bestandsaufnahme (kein Änderungsbedarf, nur bestätigen)

- `gpu_buf`/`gpu_grad` sind **Tensor-owned**, Pool-verwaltet, Rückgabe ausschließlich in `moo_tensor_free`
  (`moo_tensor.c:211-221`). Kein Alias/Sharing zwischen Tensoren (G1 §2 Verbot) — bereits eingehalten,
  nirgends im Code wird ein `gpu_buf`-Pointer kopiert statt neu belegt.
- Slot-Reuse-Sicherheit (die in G1 §2 als offene Frage markierte "Minenzone"): **gelöst**. Synchroner
  Dispatch garantiert GPU-idle vor jedem Op-Return (`dispatch_sync` + Fence-Wait), `buf_zurueck` gibt den
  Slot erst NACH dem Fence-Wait frei (`moo_ki_gpu.c:956-961`, Kommentar bestätigt "kein separater
  In-flight-Fence-Wait nötig"). Kein Generation-Counter nötig, solange der Dispatch synchron bleibt —
  **das ist selbst eine Invariante, die G4c NICHT brechen darf** (siehe I5 unten).
- Autograd-Tape (`MooAgNode`) hält `inputs[2]`+`output` als **retained** `MooValue` (`moo_ag_record`).
  Das bedeutet: solange ein Tensor auf dem Tape referenziert ist, lebt sein `gpu_buf` (Refcount > 0) —
  Resident-Buffer über den ganzen Fwd+Bwd-Schritt ist damit refcount-seitig bereits sicher, **kein
  Extra-Pinning nötig**. `moo_ag_reset()` released alle Tape-Nodes nach `opt_schritt` — das ist der
  natürliche Punkt, an dem Zwischen-Aktivierungen (nicht mehr referenziert) ihren `gpu_buf` an den Pool
  zurückgeben. Für G4c-Residenz heißt das: Zwischen-Buffer werden JEDE Iteration neu belegt (kein
  Cross-Iteration-Pooling der Aktivierungen) — Params/Grad-Akku-Tensoren dagegen bleiben (kein Tape-Release)
  über Iterationen resident, wenn kip-gpu ihren `gpu_buf` nicht in `opt_schritt` freigibt.

## 5. Grad-Validität — Spezialfall Fan-out/Optimizer (der eigentliche Risikoschwerpunkt)

Backward akkumuliert fan-out per `zg[i] += g[i]` über MEHRERE `bw_*`-Aufrufe in denselben `grad`-Buffer
(z.B. ein Parameter, der in zwei Layern verwendet wird). Das ist eine **Read-Modify-Write-Sequenz über
mehrere Op-Aufrufe hinweg**, nicht ein einzelner op-lokaler Schreibzugriff wie bei `data`. Für GPU-Residenz
folgt daraus eine härtere Regel als beim normalen valid-Bit-Vertrag:

> **Ein Gradient-Buffer darf innerhalb EINES Backward-Durchlaufs nur auf EINER Seite akkumuliert werden
> (entweder durchgehend CPU `zg[i]+=` ODER durchgehend GPU `grad_accum_res`). Kein Wechsel zwischen den
> Beiträgen desselben Tensors, sonst geht ein Fan-out-Beitrag verloren (der jeweils andere Buffer sieht
> ihn nie).**

Grund: `grad_accum_res` akkumuliert in `gpu_grad`, CPU-`+=` in `grad` — es gibt (heute) keinen
Merge-Schritt zwischen beiden pro Zwischenschritt, nur `moo_tensor_host_sichern`/`_nach_gpu` als
EINMALIGE Materialisierung. Ein Parameter mit gemischten Fan-out-Quellen (ein Layer CPU-Op, ein anderer
GPU-Op, weil z.B. ein Shape nicht `_res`-routbar ist) MUSS vor dem jeweils anderen Beitrag konsolidiert
werden — das ist exakt das STRIKT-Modell aus dem Wiring-Plan (kein stiller Fallback), verschärft um: auch
im AN-Modus (opportunistisch) muss ein Fallback für eine spezifische Op-Instanz VOR dem nächsten
Akkumulations-Beitrag desselben Ziel-Tensors materialisieren (`host_sichern` bzw. `nach_gpu`), nicht erst
am Ende des ganzen Backward-Durchlaufs.

## 6. Minimaler sicherer Wiring-Plan (Ergänzung zu kip-gpus §2.5-Reihenfolge)

Die von kip-gpu vorgeschlagene Reihenfolge (matmul → Optimizer → norm/softmax/ew/gather → Backward →
STRIKT) ist grundsätzlich richtig, hat aber eine **Ordering-Lücke**: Optimizer-Routing (Stufe 2) kommt
VOR Backward-Residenz (Stufe 4). Das bedeutet, `opt_*_res` würde in den Zwischenstufen einen resident
erwarteten Grad-Buffer vorfinden, der noch komplett CPU-geschrieben ist (§2.4 verlangt "g muss initial
0-uploaded sein" — das setzt bereits einen Upload-Schritt voraus, der ohne Trichter-Disziplin leicht
vergessen wird). Empfehlung, den Plan um einen Trichter-Schritt VOR jeder Stufe zu ergänzen:

1. **Trichter zuerst (Voraussetzung für ALLES weitere, kleinste Fläche, kein Kernumbau):**
   In `moo_autograd.c` VOR jedem `t->data`/`o->grad`-Lesezugriff in `bw_*` einen Aufruf
   `moo_tensor_f32_sichern(t)` einfügen (analog zum bereits existierenden `expect_t`-Muster in
   `moo_tensor_ops.c`); in `moo_nn_opt_schritt` vor der Update-Schleife für `p`/`g` denselben Trichter.
   Diese Änderung ist im reinen F32/CPU-Skelett ein **No-op** (data ist immer `MOO_V_DATA`) — sie
   verändert also keinen Basisgate-Wert, schließt aber die Lücke aus §3 bevor Resident-Ops überhaupt
   verdrahtet werden. **Das ist der Schlüssel-Schritt, den kip-gpu vor Stufe 1 (matmul-Routing) braucht,
   sonst wirkt jede weitere Stufe auf einem ungesicherten Fundament.**
2. **`grad_valid` aktiv einführen** parallel zum Trichter: jede Stelle, die `t->grad` NEU beschreibt
   (Backward-Erstschreiber je Tensor, nicht die Fan-out-`+=`-Folgeschreiber) setzt `grad_valid = MOO_V_DATA`
   analog zu `data`. GPU-Backward setzt stattdessen `grad_valid = MOO_V_DEV`. Optimizer-Trichter aus
   Schritt 1 sichert dann konsequent über `grad_valid`, nicht implizit über "grad ist immer da".
3. **matmul-Routing** (wie kip-gpu Stufe 1) — jetzt sicher, weil Downstream-Konsumenten (Backward,
   Optimizer) über den Trichter geschützt sind.
4. **Optimizer-Routing** (kip-gpu Stufe 2) — NUR für Parameter, deren `grad_valid & MOO_V_DEV` bereits
   gesetzt ist (aus Stufe 5/GPU-Backward); für alle anderen bleibt der CPU-Pfad automatisch aktiv (der
   Trichter aus Schritt 1 sichert das transparent, kein Extra-Fall nötig).
5. **norm/softmax/ew/gather, dann Backward resident** wie kip-gpus Stufen 3-4.
6. **STRIKT-Enforcement + Telemetrie** wie kip-gpus Stufe 5 — jetzt zusätzlich mit der Fan-out-Regel aus
   §5 als Gate-Kriterium (Negativ-Kontrolle: absichtlich gemischter CPU/GPU-Fan-out auf denselben
   Ziel-Tensor MUSS unter STRIKT hart fehlschlagen, nicht still einen Beitrag verlieren).

## 7. Konkrete Invarianten (für kip-gpus Gate-Harness, §4 im Wiring-Plan)

- **I1 (Datenlese-Trichter):** Jeder Lesezugriff auf `t->data` außerhalb `moo_tensor_ops.c`s bestehenden
  Trichtern (also insbesondere `moo_autograd.c`, `moo_nn.c` Optimizer) MUSS `moo_tensor_f32_sichern(t)`
  vorausgehen. Kein direkter `->data[i]`-Zugriff ohne vorherige Sicherung.
- **I2 (Gradlese-Trichter):** Analog für `t->grad` — neuer Trichter nötig (existiert heute nicht), muss
  `grad_valid` respektieren, sobald GPU-Backward `grad_valid=MOO_V_DEV` setzen kann.
- **I3 (Single-Writer pro Fan-out-Ziel):** Ein Grad-Akkumulationsziel wird pro Backward-Durchlauf
  durchgehend auf EINER Seite (CPU ODER GPU) akkumuliert; jeder Seitenwechsel erzwingt vorherige
  Materialisierung (`host_sichern`/`nach_gpu`) auf den Zielort des nächsten Beitrags.
- **I4 (valid != 0 / grad_valid optional 0):** Bestehende Invariante bleibt unverändert gültig, keine
  Ausnahme durch G4c-Wiring.
- **I5 (Synchroner Dispatch):** Resident-Ops bleiben synchron mit Fence-Wait-vor-Return (wie heute).
  Ein Wechsel zu asynchronem Submit/Multi-Buffering würde die Slot-Reuse-Sicherheit aus §4 brechen und
  bräuchte zwingend die in G1 §2 vorgemerkte Generation-Erweiterung — **außerhalb G4c-Scope, explizit
  No-Go (siehe unten)**.
- **I6 (Optimizer-Grad-Nullung):** `moo_nn_opt_schritt`s bestehendes Verhalten (`p->valid=MOO_V_DATA`
  nach dem Schritt, `grad` memset auf 0) muss um `grad_valid=MOO_V_DATA` (bzw. bei resident-Nullung
  `MOO_V_DEV`) ergänzt werden — sonst bleibt die neu eingeführte Maske nach dem ersten Schritt inkonsistent.
- **I7 (E2/E2b-Kompatibilität, von kip-gpu bereits gefordert, hier bestätigt):** Adam `m`/`v` und der
  Dropout-`zaehler` müssen nach jedem Schritt CPU-lesbar/downloadbar bleiben — mit I1/I2-Trichtern
  automatisch erfüllt, solange `moo_nn_ckpt_speichern` (E2b) ebenfalls über `moo_tensor_host_sichern`
  läuft (bereits der Fall laut kip-daten E2b-Bericht, Msg 12785).

## 8. No-Go-Liste (für kip-gpu, Phase 2)

- **KEIN** direkter `->data`/`->grad`-Zugriff in neuem Resident-Code ohne vorherigen Sichern-Aufruf
  (I1/I2) — auch nicht "nur lesend, ist doch eh meist f32".
- **KEIN** Mischen von CPU-`+=` und GPU-`grad_accum_res` auf denselben Fan-out-Ziel-Tensor innerhalb
  eines Backward-Durchlaufs ohne Zwischen-Materialisierung (I3).
- **KEIN** asynchroner/gepipelinter GPU-Dispatch in G4c — Slot-Reuse-Sicherheit hängt am synchronen
  Fence-Wait-vor-Return-Modell (I5). Async wäre ein eigener KIP mit Generation-Counter-Erweiterung.
- **KEIN** Aliasing/Sharing eines `gpu_buf`/`gpu_grad`-Handles zwischen zwei MooTensor-Instanzen (G1 §2,
  weiterhin gültig — reshape/transpose auf GPU-Tensor bleibt neuer Buffer oder CPU-Roundtrip).
- **KEIN** MooTensor-Struct-Edit — Felder reichen (bestätigt §1). Falls im Verlauf doch ein neues Feld
  nötig scheint, zuerst mit kip-kern klären (Struct-Hoheit).
- **KEIN** stiller Fallback unter `MOO_KI_GPU_STRIKT=1` — gilt jetzt auch explizit für den
  Fan-out-Seitenwechsel aus I3, nicht nur für "Op nicht routbar".
- **KEIN** Entfernen/Umgehen des bestehenden Mutations-Invalidierungs-Musters
  (`t->valid = MOO_V_DATA` nach jedem Host-Write, z.B. `moo_tensor_setzen`, Optimizer) — GPU-Pfade
  ergänzen `MOO_V_DEV`/`grad_valid`, ersetzen aber nie diese bestehende Zeile.

## 9. Offene Fragen an kip-gpu / kip-ops

- Wer implementiert Schritt 6.1 (Trichter in `moo_autograd.c`/`moo_nn.c`)? Das sind Shared-File-Edits in
  genau den Dateien, die B4b heute hält — Timing mit kip-ops FREI-Ping abstimmen. Empfehlung: Trichter
  VOR dem ersten `_res`-Routing-Commit einbauen (kleine, isolierte No-op-Änderung, eigenes Mini-Gate:
  Basiswerte bit-identisch, da No-op auf F32/CPU).
- `grad_valid`-Einführung (I2/I6) berührt denselben Dateikreis — sinnvoll im selben Commit wie der
  Trichter, um nicht zweimal durch Konvention-v1-BELEGT zu müssen.
