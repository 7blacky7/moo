# KIP-D0 — DType-Design + Direct-Access-Audit

Stand: 2026-07-10. Task b3817972. Status: Entwurf, GPT-Review pending.
Gate vor jedem Kern-Edit an MooTensor (KIP-D1).

## 1. Direct-Access-Inventur (2026-07-10, Job 719dcc34)

Ist-Struct (moo_runtime.h:120-129): `float* data` (owned), `float* grad`
(NULL oder owned), beides fest f32.

Indizierte Direktzugriffe `->data[` / `->grad[` in der Runtime (ohne Tests):

| Datei | ->data[ | ->grad[ |
|---|---|---|
| moo_tensor.c | 8 | 0 |
| moo_tensor_ops.c | 17 | 0 |
| moo_autograd.c | 10 | 23 |
| moo_nn.c | 18 | 4 |
| moo_nn_easy.c | 10 | 0 |
| moo_dataset.c | 11 | 0 |
| moo_ki_gpu.c | 0 (nimmt float*-Args) | 0 |
| **Summe Runtime** | **74** | **27** |

Tests mit Direktzugriffen: test_tensor_asan, test_tensor_ops_asan,
test_autograd_asan, test_gradcheck, test_nn_asan, test_dataset_asan.
VERDACHTSFALL: moo_http.c matcht `->data[` — bei D1 prüfen, ob das ein
anderes Struct ist (vermutlich HTTP-Body, dann irrelevant).
UNVOLLSTÄNDIGKEIT DES GREP: nicht-indizierte Nutzungen (`t->data` als
Pointer an memcpy/GPU/fwrite) sind NICHT gezählt — D1 auditiert die
zusätzlich (Kommando: `grep -rn '\->data\b' *.c` minus Indexform).
Re-Check maschinell: `grep -c '\->data\['` pro Datei muss nach D1
UNVERÄNDERT sein (siehe Entscheid unten — das ist das harte Gate).

## 2. Design-Entscheid: Dual-Buffer statt void*-Umbau

**Verworfen (Option A):** `void* data` + dtype + Accessor-Makros überall.
Grund: alle 74+27 Zugriffsstellen + Tests müssten angefasst werden —
maximales Regressionsrisiko im Refcount-/ASan-Minenfeld, null Nutzen für
den Compute-Pfad (der laut D1-Vertrag ohnehin f32 bleibt).

**Gewählt (Option B): Dual-Buffer.** MooTensor wird erweitert, nicht umgebaut:

```c
typedef struct MooTensor {
    /* ... bestehende Felder unverändert ... */
    float*   data;    // f32-COMPUTE-Buffer, NULLABLE wenn andere Repr. valid
    float*   grad;    // bleibt IMMER f32 (Vertrag aus 7b5eb6e2 Pkt. 2d)
    bool     requires_grad;
    uint8_t  dtype;   // MOO_DT_F32=0 (Default), MOO_DT_BF16=1
    uint8_t  valid;   // Bitmaske: MOO_V_DATA|MOO_V_STORE|MOO_V_DEV (mit G1 geteilt)
    void*    store;   // Storage-Buffer in dtype, NULL bei reinem f32
} MooTensor;
```

VEREINHEITLICHTER VALID-VERTRAG (GPT 49573fb4, gemeinsam mit G1): Ein Tensor
hat bis zu DREI Daten-Repräsentationen — f32-`data`, bf16-`store`,
GPU-`gpu_buf` (G1). `valid` ist EINE Bitmaske über alle drei:
- Lesender Zugriff auf eine Repräsentation setzt deren Bit via zentraler
  Sicherungs-Funktion (`moo_tensor_f32_sichern` / `_store_sichern` /
  `_host_sichern` aus G1) — Konvertierung von der jüngsten autoritativen
  Repräsentation.
- SCHREIBENDER Zugriff (in-place: tensor_setzen, Optimizer-Schritt, jede
  Mutation) macht GENAU die geschriebene Repräsentation autoritativ und
  LÖSCHT die anderen Bits. Damit ist der Stale-store-Fall (Optimizer
  schreibt data, store veraltet) per Vertrag ausgeschlossen.
- Invariante: mindestens ein Bit gesetzt. dtype==F32 ⇒ STORE-Bit nie gesetzt.

EINTRITTSPUNKT-VERTRAG (korrigiert nach GPT 49573fb4 — aufgelöster
Widerspruch "zentrale Funktion vs Ops unangetastet"): NICHT die 74
Zugriffsstellen rufen f32_sichern — sondern die ÖFFENTLICHEN EINTRITTSPUNKTE
davor. Konkret: expect_t() in moo_tensor_ops.c (der EINE Trichter, durch den
jeder Registry-Op seine Tensor-Args zieht) + die öffentlichen Nicht-Registry-
Eintritte (nn-vorwaerts-Pfade, dataset, zu_liste/zeige, speichern). D1
inventarisiert diese Eintrittspunkte MASCHINELL (code_intel functions/
references auf expect_t + exportierte moo_tensor_*/moo_nn_*-Signaturen mit
Tensor-Args) und weist je Eintritt die Sicherung nach. Die 74 INTERNEN
Stellen hinter den Eintritten bleiben unverändert — dort ist data nach
Eintritts-Sicherung garantiert gültig.
- `als_dtype("bf16")`: konvertiert data→store (round-to-nearest-even),
  gibt data frei. `als_dtype("f32")`: umgekehrt.
- Speicherersparnis wirkt bei: Gewichten/Checkpoints in Ruhe, D2-
  Aktivierungs-Storage (nach Op-Ausführung f32→bf16 downcast + data frei;
  backward rematerialisiert), D3-I/O, später GPU-Transfers.
- Neue Felder ANS ENDE des Structs (refcount MUSS erstes Feld bleiben,
  Kommentar moo_runtime.h:121).

## 3. Betroffene Funktionen (D1-Arbeitsliste)
Nur die Lebenszyklus-/Grenzflächen, NICHT die Ops:
moo_tensor_raw/neu (Init dtype=F32, store=NULL), tensor_free (store
freigeben), Kopier-/Clone-Pfade, zu_liste/zeige (materialisieren via
zentraler Funktion), .mook speichern/laden (dtype-Header, D3), GPU-Upload
(liest data; bf16-GPU-Transfer = spätere Option). Byte-Allokation:
`(size_t)size * elem_size(dtype)` mit int64-Overflow-Check VOR dem Cast
(size ist bereits int64_t; Guard gegen size*4 bzw. size*2 > SIZE_MAX/2).

## 4. Audit-Plan für D1 (Gate-Checkliste, korrigiert 49573fb4)
1. EINTRITTSPUNKT-AUDIT (maschinell): Liste aller öffentlichen Funktionen
   mit Tensor-Args (code_intel) × Nachweis der Sicherungs-Zeile — das ist
   das primäre Gate. grep-Counts der 74 internen Stellen bleiben zusätzlich
   unverändert (Sekundärbeweis, allein NICHT hinreichend).
2. MUTATIONS-AUDIT: Liste aller in-place-Schreiber (tensor_setzen,
   Optimizer-Schritt, dataset-Füller) × Nachweis der Invalidierungs-Zeile.
3. BF16-EINGABEN ÜBER ALLE ÖFFENTLICHEN OPS: Testmatrix führt jeden der 26
   Registry-Ops + kreuzentropie/mse/vorwaerts mit bf16-Input aus, Ergebnis ==
   f32-Referenz innerhalb bf16-Toleranz. Plus Stale-Store-Regressionstest:
   als_dtype(bf16) → f32-Mutation → als_dtype(bf16) → Werte aktuell.
4. Roundtrip-Gate f32→bf16→f32 (Toleranz 2^-8 relativ), NaN/Inf/Subnormal.
5. Neuer ASan-Harness test_tensor_dtype_asan.c in run_sanitize.sh
   (Alloc/Free/Konvertier-Zyklen, 1M-Leak-Gate, Valid-Masken-Unit-Tests).
6. Basisgates (run_all, MNIST bit-identisch, LM-Kurve) unverändert.

## 5. Exit-Gate dieses Dokuments
GPT-Erstreview 49573fb4: Richtung bestätigt, Korrektur eingearbeitet
(Valid-Maske, Invalidierungsvertrag, Eintrittspunkt-Audit, bf16-Op-Matrix).
Kurzes GPT-GEGENREVIEW der Korrektur pending; danach D1-Go.
