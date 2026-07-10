# KIP-G1 — Design: Tensor-Device-Modell (GPU-residente Buffers)

Stand: 2026-07-10. Task 368ebf50, Design-Teil (PoC folgt nach GPT-Review).
Voraussetzung für KIP-G0. Antwortet auf 7b5eb6e2 Pkt. 10.

## 1. Zustandsmodell pro Tensor
MooTensor wird erweitert (Felder ans Ende, refcount bleibt erstes Feld;
koordiniert mit D0-Dual-Buffer — beide Erweiterungen in EINEM Struct-Edit):

```c
    uint8_t  device;       // MOO_DEV_CPU=0, MOO_DEV_GPU=1 — nur BEVORZUGTER Compute-Ort
    uint8_t  valid;        // GETEILTE Bitmaske mit D0: MOO_V_DATA|MOO_V_STORE|MOO_V_DEV
    uint8_t  grad_valid;   // EIGENE Maske: MOO_V_DATA|MOO_V_DEV (Grad hat kein bf16-store)
    void*    gpu_buf;      // opaque Handle (MooKiGpuBuf*), Pool-verwaltet
    void*    gpu_grad;     // dito für Gradient (G3c), NULL bis dahin
```

KORRIGIERT (GPT 49573fb4): host_valid/dev_valid als getrennte bools sind bei
DREI Repräsentationen (f32-data, bf16-store, gpu_buf) unzureichend — D0 und
G1 teilen EINE `valid`-Bitmaske (Vertrag in D0 §2): Schreibzugriff macht
genau die schreibende Repräsentation autoritativ und löscht die anderen Bits.
GRADIENT-Validität ist eine EIGENE Maske `grad_valid` — Daten- und
Gradientenzustand teilen NIE dieselben Flags (ein GPU-Backward darf
dev-Gradient setzen, ohne die Daten-Maske zu berühren).

Invarianten: valid != 0 immer; grad_valid == 0 erlaubt (kein Grad); device
beschreibt den BEVORZUGTEN Compute-Ort, nie die einzige Kopie.

## 2. Ownership: GPU-Buffer vs Refcount (die Minenzone)
- gpu_buf gehört dem TENSOR (owned), wird aus dem bestehenden GPU3-B-Pool
  bezogen und in moo_tensor-free an den Pool ZURÜCKGEGEBEN (nicht vkDestroy —
  Pool-Rückgabe ist die bestehende Semantik).
- KEINE geteilten GPU-Buffers zwischen Tensoren in G1 (Views/Aliasing auf GPU
  explizit verboten; reshape/transpose auf GPU-Tensor = neuer Buffer oder
  CPU-Roundtrip — Entscheid pro Op in G0-Inventur).
- Free-Reihenfolge: erst Fence-Wait falls Buffer in-flight (Submit-Tracking
  aus GPU3-C wiederverwenden), dann Pool-Rückgabe, dann Host-Frees.
  SLOT-REUSE-SICHERHEIT (GPT 49573fb4): die Buffer→letzter-Submit-Map braucht
  eine GENERATION pro Pool-Slot (Handle = Slot-Index + Generation) ODER
  garantiertes Entfernen des Map-Eintrags VOR der Pool-Rückgabe — sonst kann
  ein wiederverwendeter Slot auf den Fence eines fremden Tensors warten
  (oder schlimmer: nicht warten). Entscheid im PoC: Eintrag-Entfernen zuerst
  (einfacher), Generation falls Messung Contention zeigt.
  ASan-Harness: Alloc/Free-Zyklen mit erzwungenen in-flight-Frees + Slot-
  Reuse-Regressionstest.

## 3. Transfer-API (explizit, moo-sichtbar)
- `t.nach_gpu()` / `t.nach_cpu()` (DE/EN-Aliase): synchron, idempotent
  (valid-Flags prüfen, no-op wenn schon dort). Upload nutzt GPU3-A-Staging.
- Implizite Syncs (Materialisierung + DATA-Bit setzen) lösen GENAU aus: zeige/print,
  zu_liste, speichern/.mook, Vergleichs-/Skalar-Extraktion (verlust.zu_liste-
  Muster). Jede dieser Stellen ruft EINE zentrale Funktion
  `moo_tensor_host_sichern(t)` — ein Grep-barer Indirektionspunkt.

## 4. Dispatch-Regeln (kein stilles Verhalten)
- Beide Operanden GPU + Op GPU-fähig → GPU, Ergebnis GPU-resident
  (valid=DEV, grad_valid unberührt). KEIN Download zwischen Ops.
- Gemischte Devices: HARTER, erklärender Fehler ("Operand a liegt auf der
  GPU, b auf der CPU — nutze nach_gpu()/nach_cpu()"). Keine stille Promotion
  in G1; Promotion-Heuristik = spätere Option nach Messdaten.
- GPU-Tensor + Op NICHT GPU-fähig: außerhalb Hotpath transparenter Fallback
  (Download, CPU-Op, Ergebnis CPU) MIT Telemetrie-Zähler; im TRAININGS-
  HOTPATH (ENV MOO_KI_GPU_STRIKT=1, von G4 gesetzt) stattdessen harter
  Fehler. Damit ist G4-Residenz beweisbar statt behauptet (Pkt. 10e).

## 5. Telemetrie (G4-Beweismittel)
Zähler im GPU-Kontext: uploads, downloads, submits, cpu_fallbacks, je Op-Name.
Abfrage `gpu_statistik()` → Dict. G4-Gate: cpu_fallbacks==0 und
uploads/downloads ~konstant im Steady-State (nur Batch rein / Loss raus).

## 6. Fence-/Sync-Lebensdauer
Ein Submit-Kontext pro Op-Kette (GPU3-C-Caching bleibt); Fences leben im
Kontext, nicht am Tensor. moo_tensor_host_sichern wartet auf den letzten
Submit, der den Buffer schreibt (Buffer→letzter-Submit-Map, GPU3-C-Muster).

## 7. PoC-Schnitt (nach GPT-GO)
GPT-Erstreview 49573fb4: Korrektur eingearbeitet (geteilte Valid-Maske mit
D0, eigene grad_valid-Maske, Slot-Reuse-Generation). Kurzes GPT-GEGENREVIEW
pending; danach PoC: nur die 7 bestehenden GPU-Ops auf residente Buffers,
Gate laut Task (Differentialtest ohne Zwischen-Transfers, Submit-Zähler-
Beweis, Default-Build ohne Vulkan unverändert).
