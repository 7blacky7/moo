
# KIP-G4e-PREFLIGHT — Echte gpu_grad-Residenz: Valid-Masken- und Fan-out-Vertrag

Owner: **kip-kern** · Task `5b32627c-3065-451f-8a6c-6e120627363a` (in_progress)
Stand HEAD bei Erstellung: nach KIP-G4c Punkte 1-5 vollständig (matmul-Fwd/Bwd, ew-Fwd, SGD/Adam-Optimizer,
STRIKT, gpu_statistik(), bw_mul/bw_div-Kontribution) — Commits bis `bcac1cf`/`16691ff`.
Nur Analyse/Design — keine Änderungen an moo_nn.c/moo_autograd.c/moo_tensor_ops.c/moo_runtime.h.

Vorgänger: `docs/kip/G4c-preflight-ownership-vertrag.md` (be01ac8, Invarianten I1-I7). Dieses Doc erweitert
I1-I3 um den in G4c bewusst vertagten Fall: **echte pass-weite gpu_grad-Residenz statt "GPU rechnet,
CPU akkumuliert immer"**.

---

## 1. Ausgangslage — verifiziert gegen den aktuellen Code (code_intel, `moo_autograd.c`)

**`grad_materialisieren(t)` (Zeile 57-68) existiert bereits als CPU-seitiger Lese-/Schreib-Trichter**
(eingeführt in G4c Schritt 0) und ist — wichtiger Fund — **schon heute technisch auf GPU-Zusammenspiel
vorbereitet**, auch wenn dieser Zweig aktuell nie greift:

```c
static void grad_materialisieren(MooTensor* t) {
    if (!t) return;
    if (!t->grad) t->grad = calloc(t->size, sizeof(float));
    if ((t->grad_valid & MOO_V_DEV) && !(t->grad_valid & MOO_V_DATA)) {
        moo_ki_gpu_download(t->gpu_grad, t->grad, ...);   // GPU->CPU falls GPU autoritativ war
    }
    t->grad_valid = MOO_V_DATA;   // CPU jetzt SOLE autoritativ, MOO_V_DEV wird geloescht
}
```

Der Kommentar im Code selbst sagt bereits: "kommt erst in einer späteren G4c-Phase-2-Stufe" — **das ist
genau diese Aufgabe.** Aktuell setzt NIRGENDS im Code `grad_valid |= MOO_V_DEV` oder schreibt in
`t->gpu_grad` persistent — der Zweig ist toter Code, aber architektonisch korrekt vorbereitet.

**`gpu_grad` (Struct-Feld seit G3c) wird heute NUR transient/lokal genutzt**, nie am Tensor persistiert:
- `bw_matmul` (Zeile 592-682): lokale Scratch-Buffer `dabuf`/`dbbuf` (NICHT `a->gpu_grad`/`b->gpu_grad`),
  Download sofort nach dem Kernel-Call, danach CPU-`zg[i]+=`. `a->gpu_grad`/`b->gpu_grad` bleiben NULL.
- `bw_mul`/`bw_div` (`gpu_ew_kontribution`, Zeile 397-412): identisches Muster — Ergebnis landet in
  einem Host-`tmp`-Array, CPU-Akkumulation via `accum_bcast`/`accum_bcast_mul` danach.
- **`moo_ki_gpu_grad_accum_res(void* acc, void* g, int64_t n)` existiert bereits** (`moo_ki_gpu.c`,
  genutzt in `test_ki_gpu_optim.c`/`ki_gpu_g4_lm.c`) — reines In-Place-`acc += g` auf zwei residenten
  Handles. Das ist der fehlende Baustein für echte Residenz, nur noch nicht an `->gpu_grad` angebunden.

**Fazit Bestandsaufnahme:** Die Bausteine existieren (Struct-Feld, Downloadzweig im Trichter,
Accumulate-Kernel) — es fehlt der SYMMETRISCHE Gegenpart zu `grad_materialisieren` für die GPU-Seite.

---

## 2. Kern-Design: symmetrischer Schreib-Trichter (Spiegel des bestehenden Daten-Vertrags)

Für `data` existiert bereits genau dieses Muster: `moo_tensor_f32_sichern` (CPU autoritativ machen) UND
`moo_tensor_nach_gpu` (GPU autoritativ machen), beide idempotent, beide respektieren `valid`. Für `grad`
gibt es bisher nur die CPU-Hälfte (`grad_materialisieren`). Die fehlende GPU-Hälfte, symmetrisch entworfen:

```c
// NEU (Vorschlag, kip-gpu-Revier moo_autograd.c): GPU-Seite von grad autoritativ machen.
// Symmetrisch zu grad_materialisieren — Spiegelbild von moo_tensor_nach_gpu fuer grad statt data.
static bool grad_materialisieren_gpu(MooTensor* t) {
    if (!t) return false;
    if (!t->gpu_grad) {
        t->gpu_grad = moo_ki_gpu_buf_belegen((int64_t)t->size * sizeof(float));
        if (!t->gpu_grad) return false;              // kein GPU verfuegbar -> Aufrufer faellt auf CPU zurueck
    }
    if ((t->grad_valid & MOO_V_DATA) && !(t->grad_valid & MOO_V_DEV)) {
        // CPU war zuletzt autoritativ (frueherer CPU-Beitrag ODER frisch genullt) -> EINMALIG hochladen.
        grad_sicherstellen(t);   // stellt sicher ->grad existiert+genullt, falls noch nie geschrieben
        if (!moo_ki_gpu_upload(t->gpu_grad, t->grad, (int64_t)t->size * sizeof(float))) return false;
    }
    // War grad_valid schon MOO_V_DEV (GPU bereits autoritativ vom letzten Knoten) -> NO-OP, kein Transfer.
    t->grad_valid = MOO_V_DEV;   // GPU jetzt SOLE autoritativ, MOO_V_DATA wird geloescht (Spiegel v. oben)
    return true;
}
```

**Warum das I3 (Fan-out) korrekt löst, OHNE einen globalen Umbau zu brauchen** (Korrektur meiner eigenen,
zu vorsichtigen Einschätzung im G4c-PREFLIGHT-Doc §6 Punkt 2 — dort hatte ich einen "koordinierten Umbau
über alle bw_* gleichzeitig" für nötig gehalten; das stimmt nicht, siehe unten):

Die Tape-Traversierung ist **strikt sequenziell** (`moo_tensor_rueckwaerts` iteriert das Tape rückwärts,
ein `bw()`-Aufruf nach dem anderen, kein Multithreading, `in_backward`-Flag verhindert Re-Entranz — bestätigt
durch kip-ops' B2-Analyse, Msg 12867). Solange **jeder** `bw_*`, der in `t`s Grad schreibt, VOR seinem
eigenen Akkumulationsschritt den zu SEINER Zielseite passenden Trichter aufruft
(`grad_materialisieren`/`grad_sicherstellen` für CPU-Beitrag, `grad_materialisieren_gpu` für GPU-Beitrag),
gilt:

- Knoten A (CPU) schreibt zuerst → `grad_valid=MOO_V_DATA`.
- Knoten B (GPU) will als nächstes schreiben → `grad_materialisieren_gpu` sieht `MOO_V_DATA` gesetzt,
  lädt EINMALIG hoch (inkl. A's Beitrag), akkumuliert dann via `grad_accum_res`, setzt `MOO_V_DEV`.
- Knoten C (GPU, selbe Zielseite) schreibt direkt danach → `grad_materialisieren_gpu` sieht bereits
  `MOO_V_DEV` → **NO-OP, kein Transfer** (das ist der Performance-Gewinn ggü. dem heutigen Download-pro-Op).
- Knoten D (CPU) will danach lesen/schreiben → `grad_materialisieren` lädt EINMALIG herunter (inkl. B+C),
  akkumuliert CPU-seitig.

Kein Beitrag geht verloren, weil **jeder Seitenwechsel genau EINEN synchronisierenden Transfer auslöst,
gleichgültig wie oft vorher auf derselben Seite akkumuliert wurde.** Das ist strukturell identisch zum
bereits produktiv laufenden `data`/`valid`-Vertrag — **kein neues Konzept, sondern dieselbe Bitmasken-Logik
für eine zweite Repräsentation.** Introduktion ist damit **pro Op-Typ inkrementell möglich** (z.B. erst
`bw_mul` auf echte Residenz umstellen, `bw_matmul` bleibt vorerst beim heutigen Download-Muster) — SOLANGE
jeder umgestellte `bw_*` konsequent durch `grad_materialisieren_gpu` vor seinem Akkumulationsschritt geht.
**Meine ursprüngliche Warnung "erst großer Parallel-Umbau aller bw_* nötig" war zu konservativ — der
bestehende Trichter-Mechanismus macht Inkrementalität sicher, sofern die Symmetrie (CPU-Trichter UND
GPU-Trichter, nie direkter Buffer-Zugriff) eingehalten wird.**

---

## 3. Lebenszyklus `gpu_grad`

- **Allokation:** lazy, beim ersten GPU-seitigen Schreibversuch via `grad_materialisieren_gpu` (siehe oben).
  Kein Vor-Allozieren nötig.
- **Freigabe:** bereits vollständig durch bestehenden Code abgedeckt — `moo_tensor_free` gibt `gpu_grad`
  schon heute an den Pool zurück (`if (t->gpu_grad) moo_ki_gpu_buf_freigeben(t->gpu_grad);`, seit G3c/G1).
  **Kein neuer Free-Pfad nötig.**
- **Über Trainings-Iterationen hinweg:** Leaf-Parameter (`p`) sind NIE Tape-`output` (nur `inputs`) und
  werden daher von `moo_ag_reset()` nicht releast — ihr `gpu_grad`-Handle würde also über mehrere
  Iterationen persistieren, WENN er nicht am Ende jeder Iteration korrekt zurückgesetzt wird (siehe I11
  unten). Zwischenaktivierungen (Tape-`output`s) werden bei `moo_ag_reset()` releast wie bisher — ihr
  `gpu_grad` (falls in der Zwischenzeit allokiert) geht über die normale `moo_tensor_free`-Pool-Rückgabe
  korrekt zurück, kein Sonderfall.

---

## 4. Neue Invarianten (Fortsetzung von I1-I7 aus dem G4c-PREFLIGHT-Doc)

- **I8 (Symmetrischer Schreib-Trichter):** Jeder `bw_*`, der eine GPU-residente Kontribution in `t->grad`
  akkumulieren will, MUSS `grad_materialisieren_gpu(t)` aufrufen, BEVOR er `moo_ki_gpu_grad_accum_res`
  aufruft. Direkter Zugriff auf `t->gpu_grad` ohne diesen Trichter ist verboten (Spiegel von I2).
- **I9 (Unconditional Set, kein `|=`):** Sowohl `grad_materialisieren` als auch `grad_materialisieren_gpu`
  setzen `grad_valid` UNBEDINGT auf genau eine Seite (`= MOO_V_DATA` bzw. `= MOO_V_DEV`), nie `|=`. Nach
  jedem Trichter-Aufruf ist exakt eine Seite autoritativ — die andere gilt als potenziell stale, bis der
  nächste Trichter-Aufruf sie erneut synchronisiert.
- **I10 (Kein Skip des Trichters bei Same-Side):** Der Trichter-Aufruf ist auch bei vermuteter
  Gleich-Seitigkeit PFLICHT (er ist idempotent/no-op wenn nichts zu tun ist) — kein `bw_*` darf den
  Trichter-Call "optimieren" und direkt akkumulieren, weil das die Erkennung eines Seitenwechsels umgeht.
- **I11 (Optimizer-Reset symmetrisch, Erweiterung von I6):** `moo_nn_opt_schritt` nullt heute `p->grad`
  (CPU, memset) und setzt `p->valid=MOO_V_DATA` für **data** nach jedem Schritt. Sobald `grad_materialisieren_gpu`
  existiert, MUSS der Optimizer-Schritt nach dem Nullen zusätzlich `p->grad_valid = MOO_V_DATA` setzen
  (mirror der bestehenden Zeile) — sonst bleibt `grad_valid` von der letzten Iteration inkonsistent
  (könnte noch `MOO_V_DEV` von einem alten, jetzt bedeutungslosen `gpu_grad`-Inhalt zeigen). Der alte
  `gpu_grad`-Buffer muss NICHT explizit genullt werden — der nächste `grad_materialisieren_gpu`-Aufruf der
  Folge-Iteration lädt automatisch die frische Null von `p->grad` hoch, sobald `grad_valid=MOO_V_DATA`
  erkannt wird (siehe §2, Seitenwechsel-Logik).
- **I12 (Fehlerpfad symmetrisch zu I1):** Schlägt `moo_ki_gpu_buf_belegen`/`moo_ki_gpu_upload`/
  `moo_ki_gpu_grad_accum_res` in `grad_materialisieren_gpu` fehl, MUSS der Aufrufer (der `bw_*`) auf den
  bestehenden CPU-Pfad zurückfallen (identisch zum heutigen `done`-Flag-Muster in `bw_mul`/`bw_matmul`) —
  **kein Teil-Update, kein stiller Datenverlust.** Unter STRIKT: harter `moo_throw` statt Fallback, exakt
  wie die bestehenden `_res`-Guards.
- **I13 (Kein Aliasing, Fortsetzung I aus G1 §2):** `gpu_grad` ist wie `gpu_buf` strikt Tensor-eigen, kein
  Teilen zwischen zwei `MooTensor`-Instanzen. Gilt unverändert.

---

## 5. Konkrete Schritte für kip-gpu (empfohlene Reihenfolge, jede Stufe einzeln gate-verifizierbar)

1. **`grad_materialisieren_gpu` einführen** (moo_autograd.c, neben `grad_materialisieren`) + Unit-Test:
   Symmetrie-Test (Upload nur bei Seitenwechsel, No-Op bei Gleichseitigkeit, Download in
   `grad_materialisieren` funktioniert weiterhin nach einem GPU-Schreibvorgang).
2. **EIN Op-Typ auf echte Residenz umstellen** — Vorschlag `bw_mul` (schon GPU-beschleunigt, kleinster
   Umbau): statt `gpu_ew_kontribution` → Host-`tmp` → `accum_bcast`, jetzt `gpu_ew_kontribution_res`
   (Ergebnis bleibt resident im GPU-Buffer) → `grad_materialisieren_gpu(ziel)` →
   `moo_ki_gpu_grad_accum_res(ziel->gpu_grad, delta_buf, n)`. **`bw_matmul` und alle anderen bw_* bleiben
   unverändert** beim heutigen Download-Muster — das ist explizit erlaubt (I8-I10 machen das sicher).
3. **Gate-Test (kip-ops' Vorschlag aus Msg 12867, hier bestätigt als Pflicht-Kriterium):** Tensor `x` als
   Fan-out-Ziel in ZWEI Ops unterschiedlicher Residenz-Klasse (z.B. `y = matmul(x,W) + mul(x,c)`, x fließt
   in matmul UND mul), Grad(x) unter STRIKT vs. CPU-Referenz vergleichen — deckt GENAU den Seitenwechsel-Fall
   ab, den I8/I9 lösen sollen. Erweiterung von `test_autograd_asan.c` Test 3 oder neuer
   `test_autograd_gpu_fanout.c` (kip-ops-Revier).
4. **Erst NACH grünem Gate für Schritt 2+3:** weitere Op-Typen (bw_matmul, bw_gather, bw_div-db) optional
   auf echte Residenz umstellen — jede Umstellung einzeln durch dasselbe Fan-out-Cross-Residenz-Gate prüfen.
5. **Optimizer-Anpassung (I11):** `moo_nn_opt_schritt` um `p->grad_valid = MOO_V_DATA` nach dem
   Grad-Reset ergänzen — kleine, isolierte Änderung, eigenes Mini-Gate (Bit-Identität CPU-Default,
   Regressionscheck bestehender SGD/Adam-Gates).

---

## 6. No-Go-Liste

- **KEIN** direkter Zugriff auf `t->gpu_grad` außerhalb von `grad_materialisieren_gpu` (I8) — auch nicht
  "nur für einen schnellen Sonderfall".
- **KEIN** `|=` auf `grad_valid` — immer unbedingtes Setzen einer einzigen autoritativen Seite (I9).
- **KEIN** Umstellen mehrerer `bw_*`-Funktionen auf echte Residenz "in einem Rutsch" ohne den
  Fan-out-Cross-Residenz-Gate-Test dazwischen — jede neue Op-Klasse braucht ihren eigenen Beweis, dass sie
  mit ALLEN anderen bereits umgestellten UND allen noch-CPU-only Op-Klassen fan-out-sicher zusammenspielt.
- **KEIN** Verzicht auf den Optimizer-`grad_valid`-Reset (I11) — sonst startet die nächste Iteration mit
  einer stale `MOO_V_DEV`-Markierung auf einem inhaltlich bedeutungslosen alten `gpu_grad`.
- **KEIN** asynchroner/gepipelinter Grad-Accum-Dispatch — gilt weiterhin die I5-Invariante aus dem
  G4c-PREFLIGHT-Doc (synchroner Dispatch, Fence-Wait-vor-Return, Slot-Reuse-Sicherheit).
- **KEIN** MooTensor-Struct-Edit — `gpu_grad`/`grad_valid` existieren bereits vollständig (KIP-STRUCT).

---

## 7a. Addendum I14 (kip-gpu, nach Implementierung Schritt 1-3, Commit af5dbf3,
Channel-Bestaetigung kip-kern Msg 12920) — Buffer-Pool liefert KEINE Zero-Garantie

Beim Bau von `grad_materialisieren_gpu` wurde verifiziert (`moo_ki_gpu.c`, `buf_holen`/
`buf_anlegen`): der Vulkan-Buffer-Pool zerot wiederverwendete UND frisch angelegte
Buffer NICHT (kein `memset` bei Slot-Reuse). Das urspruengliche Pseudocode-Muster in
§2 ("nur hochladen wenn `grad_valid & MOO_V_DATA` gesetzt ist") ging implizit von
calloc-aehnlicher Zero-Semantik aus — bei `grad_valid==0` (frischer Tensor, noch nie
geschrieben) haette das den Upload uebersprungen und `moo_ki_gpu_grad_accum_res`
haette auf UNDEFINIERTEM VRAM-Inhalt akkumuliert (stiller Bug, kein Crash).

- **I14 (Keine Zero-Garantie des Buffer-Pools):** `grad_materialisieren_gpu` MUSS
  IMMER ueber `grad_sicherstellen(t)` hochladen, AUSSER `t->grad_valid & MOO_V_DEV`
  ist bereits gesetzt (GPU schon autoritativ, No-Op). Ein bedingtes Skip bei
  `grad_valid==0` (nach dem Muster "nur bei MOO_V_DATA hochladen") ist VERBOTEN —
  `grad_sicherstellen` liefert in JEDEM anderen Fall einen korrekten Puffer (echten
  CPU-Beitrag ODER frische calloc-Null), das deckt sowohl den MOO_V_DATA-Fall als
  auch den "noch nie geschrieben"-Fall (`grad_valid==0`) ab. Gilt fuer JEDEN
  weiteren Op-Typ, der in Doc §5 Punkt 4 auf echte Residenz umgestellt wird
  (bw_matmul/bw_gather/bw_div-db) — dieselbe Falle wuerde dort identisch zuschlagen.
- Verifiziert durch `test_g4e_fanout_gpu.c` (Fan-out-Cross-Residenz-Gate, echte
  4070-Ti-Hardware, 9/9 Checks gruen, exakter Gradientwert gegen Hand-Referenz).

## 7. Offene Frage an kip-ops (B2-Vertrag)

Test-Erweiterung aus §5 Punkt 3 (Fan-out-Cross-Residenz-Gradcheck) fällt laut kip-ops' eigenem Angebot
(Msg 12867) in dessen Revier (`test_autograd_asan.c`/neuer `test_autograd_gpu_fanout.c`). Empfehlung:
dieser Test entsteht VOR oder GLEICHZEITIG mit kip-gpus Schritt 2 (erste echte Residenz-Umstellung), nicht
danach — er ist das Korrektheits-Gate für den ganzen Ansatz, nicht nur eine nachträgliche Bestätigung.
