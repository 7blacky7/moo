# KIP-G4c Phase 2 — Patch-Entwurf Schritt 1: matmul-Residenz-Routing

**Status:** Entwurf (nicht angewendet — bereitet nur die exakte Änderung vor, damit
Phase 2 sofort nach FREI-Ping von kip-ops starten kann). Baut auf dem kanonischen
`docs/kip/G4c-production-wiring-plan.md` auf (Resident-Buffer-Lebenszyklus §-Skizze:
`res_ensure(t) = buf_belegen+upload wenn !MOO_V_DEV; op_res; o->valid=MOO_V_DEV`).

## Ziel

`moo_tensor_matmul` (`moo_tensor_ops.c:211-238`) nutzt aktuell den non-residenten
GPU2-Hook (`moo_ki_gpu_matmul`, Upload/Download pro Call). Schritt 1 ersetzt das
durch einen residenten Pfad über `moo_ki_gpu_matmul_res` + die bereits vorhandenen
`MooTensor`-Felder `gpu_buf`/`valid`(`MOO_V_DEV`), OHNE das bestehende
non-resident-Verhalten fuer Nicht-Trainings-Aufrufe zu brechen (Kompatibilität:
CPU-Default unveraendert, GPU2-Hook bleibt Fallback wenn Residenz nicht greift).

## 1. Neuer Helfer (Vorschlag: `moo_ki_gpu.c`, mein Revier — kein Shared-File-Lock nötig)

```c
/* res_ensure: stellt sicher dass t->gpu_buf einen gueltigen (uploadeten) GPU-
 * Buffer hat. Rueckgabe false = Residenz nicht moeglich (kein Vulkan, OOM, ...)
 * -> Aufrufer faellt auf CPU zurueck (oder wirft unter STRIKT, siehe §3). */
bool moo_ki_gpu_res_ensure(MooTensor* t) {
    if (!t) return false;
    if (t->valid & MOO_V_DEV) return true;           /* schon resident+aktuell */
    if (!t->gpu_buf) {
        t->gpu_buf = moo_ki_gpu_buf_belegen((int64_t)t->size * sizeof(float));
        if (!t->gpu_buf) return false;
    }
    if (!(t->valid & MOO_V_DATA)) return false;       /* nichts Gueltiges zum Hochladen */
    if (!moo_ki_gpu_upload(t->gpu_buf, t->data, (int64_t)t->size * sizeof(float)))
        return false;
    t->valid |= MOO_V_DEV;                            /* additiv: data bleibt gueltig */
    return true;
}
```

Deklaration in `moo_ki_gpu_api.h` (mein Revier, kein Lock nötig) statt
`moo_runtime.h` — vermeidet unnötigen Touch der Sperrliste, `moo_tensor_ops.c`
inkludiert bereits `moo_runtime.h`, welches wiederum Zugriff auf die
`moo_ki_gpu_*`-Deklarationen hat (Präzedenzfall: `moo_ki_gpu_matmul` wird heute
schon aus `moo_tensor_ops.c` aufgerufen, Deklaration kommt über `moo_runtime.h:347-349`
— Schritt 1 ergänzt dort NUR die neue `res_ensure`/`matmul`-Dispatch-Deklaration,
das ist der einzige zwingende `moo_runtime.h`-Touch).

## 2. Änderung in `moo_tensor_ops.c::moo_tensor_matmul` (Zeile ~226-229)

Heute:
```c
    if (!moo_ki_gpu_matmul(a->data, b->data, out->data,
                           a->shape[0], a->shape[1], b->shape[1]))
        matmul_ikj(a->data, b->data, out->data, a->shape[0], a->shape[1], b->shape[1]);
```

Neu (additiv, Reihenfolge: resident zuerst, non-resident-Hook als Fallback,
CPU-Loop als letzter Fallback — KEIN bestehendes Verhalten geändert, nur vorne
ein Pfad ergänzt):
```c
    bool done = false;
    if (moo_ki_gpu_res_ensure(a) && moo_ki_gpu_res_ensure(b)) {
        done = moo_ki_gpu_matmul_res(a->gpu_buf, b->gpu_buf, /* siehe §2a */ o_buf,
                                      a->shape[0], a->shape[1], b->shape[1]);
        if (done) out->valid = MOO_V_DEV;  /* nur DEV gueltig, data noch nicht synced */
    }
    if (!done)
        done = moo_ki_gpu_matmul(a->data, b->data, out->data,
                                 a->shape[0], a->shape[1], b->shape[1]);
    if (!done)
        matmul_ikj(a->data, b->data, out->data, a->shape[0], a->shape[1], b->shape[1]);
```

### 2a. Offene Detailfrage fuer Umsetzung (KEINE Blockade, nur Implementierungsdetail)

`out` (frisch via `moo_tensor_raw`) hat noch keinen `gpu_buf`. Vor dem `matmul_res`-
Call muss `out->gpu_buf` per `moo_ki_gpu_buf_belegen` angelegt werden (analog
`res_ensure`, aber OHNE Upload — reiner Alloc, da der Buffer beschrieben wird).
Kleiner Helfer `moo_ki_gpu_res_alloc(MooTensor* t)` dafuer vorgesehen (nur Alloc,
kein Upload/Valid-Check) — ergänzt `res_ensure` in §1.

### 2b. Lazy-Download-on-Read (fuer CPU-Konsumenten von `out`)

Wenn spaeter `out->data` gelesen wird (z.B. `moo_tensor_f32_sichern`/print/Checkpoint)
und `out->valid == MOO_V_DEV` (data NICHT gueltig), muss ein Download passieren.
Das ist bereits im D0/G1-Vertrag als `valid`-Bitmaske vorgesehen — Schritt 1 nutzt
den bestehenden Read-Pfad NICHT um (kein neuer Download-Hook), sondern beschränkt
sich auf Ketten, in denen `out` sofort wieder GPU-seitig weiterverarbeitet wird
(nächster Layer/Op). Ein genereller "Download-on-CPU-Read"-Hook (Interzeption an
jeder Stelle, die `t->data` liest) ist NICHT Teil von Schritt 1 — das wäre ein
breiterer Touch über viele Lesestellen und gehört in einen späteren, eigenen
Teilschritt (siehe kanonischer Plan §"Resident-Buffer-Lebenszyklus"). Bis dahin:
Schritt 1 ist NUR sicher innerhalb einer durchgehenden GPU-residenten Op-Kette
(wie im G4/G4b-PoC), nicht für beliebig gemischte CPU/GPU-Aufrufreihenfolgen.

## 3. STRIKT-Guard (Schritt 1 minimal, volle Semantik folgt mit §5 des Hauptplans)

```c
    if (!done && moo_ki_gpu_strikt_aktiv())   /* neuer Accessor, moo_ki_gpu.c */
        moo_throw(moo_error("GPU-STRIKT: matmul konnte nicht resident dispatcht "
                             "werden (kein Vulkan/OOM/Upload-Fehler)"));
```//
Platzierung: NACH dem `matmul_res`-Versuch, VOR dem non-resident-Hook-Fallback
— unter STRIKT darf weder der non-resident GPU2-Hook noch `matmul_ikj` greifen.

## 4. Warum matmul zuerst (Reihenfolge-Begründung)

- Höchster FLOP-Anteil in jedem Transformer-Block (QKV/Out-Proj/FFN) → größter
  Nutzen zuerst sichtbar.
- Einziger Op mit HEUTE bereits vorhandenem (wenn auch non-residentem) GPU-Pfad
  → kleinster Diff, Verhaltensänderung isoliert nachvollziehbar.
- `matmul_bw_res` existiert bereits (Komposition transpose+matmul) → Backward-Wiring
  ist Schritt 1b (direkt danach), keine neue Kernel-Arbeit nötig.

## 5. Test-Skizze (neue Datei, kein Shared-File-Touch)

`compiler/runtime/tests/test_g4c_matmul_res_wiring.c`: ruft `moo_tensor_matmul`
(nicht `moo_ki_gpu_matmul_res` direkt!) über die öffentliche Runtime-API mit
zwei Tensoren, prüft (a) Ergebnis == CPU-Referenz (Toleranz), (b) Telemetrie
zeigt `submits==1` bei GPU-Pfad, (c) ohne `MOO_KI_GPU=1` bit-identisches
CPU-Verhalten (Regressionscheck gegen bestehenden `matmul_ikj`-Pfad).

---
Dieser Entwurf wird NICHT angewendet bevor kip-ops FREI fuer die vier gesperrten
Dateien meldet. Nach FREI: BELEGT posten, `files(plan)` mit den Ops aus §1/§2
vorbereiten, Review gegen diesen Entwurf, dann `files(commit)`.
