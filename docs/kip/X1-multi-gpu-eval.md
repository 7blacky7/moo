# KIP-X1 — EVAL: Multi-GPU-Strategie für moo (Live-Hardware-Inventur + Backend-Entscheid)

**Autor:** kip-kern · **Task:** a2e20711 · **Stand:** 2026-07-10 · **Status:** Design/EVAL (kein Produktivcode)
**Depends:** KIP-G4 (residentes GPU-Training-PoC), KIP-P0 (Zielprofil). **Bezug:** D0-DType-Vertrag, E2b Cross-Device-Checkpoint (493b15c).

---

## 0. TL;DR — Empfehlung

> **NO-GO für Multi-GPU-Training (Data-Parallel / All-Reduce), egal ob Vulkan-eigenes All-Reduce oder CUDA/NCCL-Brücke.**
> **GO für den bewussten Verzicht:** (a) **Gradient Accumulation** auf der einzigen 4070 Ti als Skalierungs-Mechanismus für die effektive Batchgröße (null Kommunikation, deterministisch, autograd-nativ), und (b) **RTX 2070 als asynchroner Eval/Inferenz-Worker**, gespeist über den bereits gate-verifizierten **E2b-Cross-Device-Checkpoint-Pfad** — Datei als Transportmedium, kein Live-All-Reduce.

Begründung in einem Satz: Die zwei GPUs stehen in **zwei verschiedenen Hosts** (nur LAN-gekoppelt), sind **heterogen** (4070 Ti ≈ 5× schneller als 2070), und moos GPU-Backend ist **Vulkan-only ohne jede CUDA/NCCL-Anbindung** — die Kommunikationskosten über LAN übersteigen die Rechenzeit pro Schritt um das 3–500-fache, während die langsamere Karte synchrones DP zusätzlich ausbremst. Multi-GPU wäre hier **langsamer als die 4070 Ti allein**.

Revisit-Bedingung siehe §8.

---

## 1. Live-Hardware-Inventur (zur Laufzeit erhoben, nichts hardcoded)

Erhoben am 2026-07-10 auf Host `cachyos-x64` via `nvidia-smi`, `vulkaninfo`, `lspci`, `ldconfig`, `nproc`. Rohbefehle unten reproduzierbar.

### 1.1 Node A — lokale Workstation (`cachyos-x64`)
| Merkmal | Wert (live) | Quelle |
|---|---|---|
| GPU | 1× **NVIDIA GeForce RTX 4070 Ti** (AD104) | `nvidia-smi`, `lspci 3d:00.0` |
| VRAM | 12282 MiB (**12 GB**) | `nvidia-smi --query-gpu=memory.total` |
| PCIe-Slot (LnkCap) | Gen4 (16 GT/s), **Width x16 fähig** | `lspci -vv … LnkCap` |
| PCIe-Link real (LnkSta) | 16 GT/s, **Width x8 (downgraded)** | `lspci -vv … LnkSta` |
| PCIe gen/width idle | Gen2 x8 (Powersaving, taktet unter Last auf Gen4) | `nvidia-smi pcie.link.*` |
| Vulkan-Devices | **1** (nur die 4070 Ti) | `vulkaninfo --summary` |
| Vulkan-Treiber | `DRIVER_ID_NVIDIA_PROPRIETARY`, ICD nur `nvidia_icd.json` | `vulkaninfo`, `/usr/share/vulkan/icd.d/` |
| CPU | 32 Threads | `nproc` |
| NCCL | **nicht installiert** ("kein libnccl") | `ldconfig -p \| grep nccl` |
| CUDA-Runtime | `libcudart.so.13` vorhanden (`/opt/cuda`) — **von moo NICHT genutzt** | `ldconfig -p \| grep libcudart` |

> **Wichtiger Live-Befund:** Der 4070-Ti-Slot läuft real auf **PCIe Gen4 x8** (LnkSta „Width x8 downgraded", obwohl x16-fähig). Das halbiert die nominelle Host↔Device-Bandbreite gegenüber x16. Relevant für jeden Staging-Pfad (auch E2b-Download).

### 1.2 Node B — Unraid-Server (remote)
| Merkmal | Wert | Anmerkung |
|---|---|---|
| GPU | 1× **NVIDIA GeForce RTX 2070** (TU106, 8 GB) | laut Betreiber-Angabe; **nicht lokal an dieser Maschine** |
| Kopplung | **nur über LAN/Ethernet** | separater Host, kein gemeinsamer PCIe-Bus |
| Bandbreite Node A↔B | **UNBEKANNT — vor jedem 2-Node-Einsatz mit `iperf3` messen** | siehe §9, NICHT hardcoden |

### 1.3 moo-GPU-Backend-Realität
- moos GPU-Pfad ist **ausschließlich Vulkan Compute**: 24 `.comp`-Shader unter `compiler/runtime/shader_ki/`, Dispatch über `moo_ki_gpu.c` / `moo_ki_gpu_api.h`.
- **Keine** CUDA-/NCCL-Referenzen im Runtime-C (`grep -rilE 'nccl|cudaMemcpy|<cuda_runtime' compiler/runtime/*.{c,h}` → leer).
- Konsequenz: Jede NCCL-Route bräuchte zusätzlich eine **Vulkan↔CUDA-External-Memory-Brücke** (VK_KHR_external_memory_fd Export → CUDA Import), plus NCCL-Installation, plus CUDA-Build von moo. Das ist ein zweiter GPU-Stack neben dem existierenden — massiver Bau-Aufwand.

---

## 2. Interconnect-Bandbreiten (Zahlen)

Theoretisch / real-erreichbar (TCP bzw. PCIe-Overhead):

| Link | Theoretisch | Real (~) |
|---|---|---|
| PCIe Gen4 x16 | 32 GB/s | ~26–28 GB/s |
| **PCIe Gen4 x8 (unser Link)** | ~15.75 GB/s | **~13 GB/s** |
| 1 GbE | 0.125 GB/s | ~0.112 GB/s |
| 2.5 GbE | 0.31 GB/s | ~0.29 GB/s |
| 10 GbE | 1.25 GB/s | ~1.1 GB/s |

**Kernpunkt:** Selbst 10 GbE ist ~**12×** langsamer als der (schon x8-gedrosselte) PCIe-Link und ~**24×** langsamer als PCIe Gen4 x16. Consumer-NVIDIA (40er/20er) hat **kein NVLink** — GPU↔GPU liefe selbst im selben Gehäuse nur P2P über PCIe.

---

## 3. Kommunikationskosten-Rechnung (Data-Parallel All-Reduce)

**Modell:** Ring-All-Reduce transferiert pro Rank ≈ `2·(N−1)/N · S` Bytes (S = Gradientengröße). Für **N=2**: jeder Rank sendet und empfängt ≈ `S` Bytes pro Schritt. Zeit ≈ `S / BW` (Full-Duplex).

**Modellgrößen (moo Decoder-LM, SwiGLU-FFN, grobe Schätzung ~12·d² Params/Layer + vocab·d Embedding):**

| Profil | Params | Grad f32 (S) | Grad bf16 |
|---|---|---|---|
| Tiny (dim256 / 6L / vocab8k, ≈ G4-PoC) | ~6.8 M | **27 MB** | 14 MB |
| Small (dim512 / 12L / vocab16k) | ~46 M | **184 MB** | 92 MB |
| Medium (dim768 / 12L / vocab32k, ~GPT-2-S) | ~124 M | **496 MB** | 248 MB |

**All-Reduce-Zeit pro Schritt (N=2, Gradient f32):**

| Profil | 1 GbE | 2.5 GbE | 10 GbE | (hypoth. PCIe Gen4 x8, gleiches Gehäuse) |
|---|---|---|---|---|
| Tiny 27 MB | 0.24 s | 0.093 s | **0.025 s** | 0.002 s |
| Small 184 MB | 1.64 s | 0.63 s | **0.167 s** | 0.014 s |
| Medium 496 MB | 4.4 s | 1.71 s | **0.45 s** | 0.038 s |

**Vergleich mit Rechenzeit:** G4-PoC (dim256/6L, GPU-resident, 4070 Ti) = **~0.0094 s/Schritt** (Msg 12778). Selbst im günstigsten realistischen LAN-Fall (10 GbE, Tiny) ist die Kommunikation **~2.8×** der Rechenzeit; bei Small/Medium über 1 GbE liegt sie **175–470×** darüber. → **Kommunikations-gebunden, katastrophal.**

bf16-Gradienten (D1/D2-Infra) halbieren S, ändern die Größenordnung aber nicht.

---

## 4. Heterogenitäts-Analyse (4070 Ti vs. 2070)

| GPU | FP32 | Speicher-BW | VRAM |
|---|---|---|---|
| RTX 4070 Ti (AD104) | ~40 TFLOPS | ~504 GB/s | 12 GB |
| RTX 2070 (TU106) | ~7.5 TFLOPS | ~448 GB/s | 8 GB |

- **Synchrones DP mit gleichen Shards** → Takt der **langsamsten** Karte. Beide laufen 2070-Tempo → Aggregat ≈ 2×7.5 = **15 TFLOPS < 40 TFLOPS** der 4070 Ti allein. **Verlust, sogar bei kostenloser Kommunikation.**
- **Imbalanciertes Sharding** (2070 bekommt weniger Arbeit ∝ Speed) → theoretisches Aggregat ≈ 47.5 TFLOPS (+19 % über 4070 Ti allein) — **nur bei Null-Kommunikationskosten**. Über LAN frisst der Sync-Overhead (§3) diese 19 % ein Vielfaches wieder auf. Netto weiterhin langsamer.
- VRAM-Asymmetrie (8 vs 12 GB): das globale Modell/Batch-Budget wird von den **8 GB der 2070** limitiert.

---

## 5. Option-Vergleich (die drei geforderten Backends)

### Option 1 — Vulkan-eigenes All-Reduce
- **Aufwand:** sehr hoch. moo hat **keine Netzwerkschicht**; Vulkan hat keinen Cross-Node-Transport. Man müsste Device→Host-Staging (über den x8-Link), einen eigenen TCP/RDMA-Ring/Tree-Reduce und Fence-Synchronisation von Grund auf bauen.
- **Nutzen:** negativ auf dieser Hardware (§3/§4).
- **Verdikt:** ❌ NO-GO.

### Option 2 — CUDA/NCCL-Brücke
- **Aufwand:** extrem hoch. NCCL nicht installiert; moo ist **Vulkan, nicht CUDA**. Erfordert (a) NCCL-Install, (b) VK↔CUDA-External-Memory-Interop für die Gradientenpuffer, (c) CUDA-Build von moo. NCCL ist für NVLink/PCIe/InfiniBand-Cluster ausgelegt, nicht für 1-GbE-Hobby-LAN.
- **Nutzen:** weiterhin LAN-limitiert; zweiter GPU-Stack als Dauerlast.
- **Verdikt:** ❌ NO-GO.

### Option 3 — Bewusst KEIN Multi-GPU (empfohlen)
- **3a Gradient Accumulation** auf der 4070 Ti: große effektive Batch durch Akkumulation über K Micro-Batches vor dem Optimizer-Schritt. **Null Interconnect-Verkehr, voll deterministisch**, nutzt das existierende autograd-Tape + B4b Activation-Checkpointing für Peak-RAM. Der Standard-Skalierungshebel für Einzel-GPU.
- **3b 2070 als Eval/Inferenz-Worker:** Training auf 4070 Ti → **E2b-Checkpoint** (493b15c: Device→Host→Device, f32-Format, bit-genau) schreiben → auf 2070 (bzw. deren Host) laden für Eval/Generierung. Vollständig **entkoppelt vom Trainings-Device**, kein All-Reduce, keine PCIe/LAN-Kosten im Trainings-Loop. kip-daten bestätigt den Pfad (Msg 12792).
- **Verdikt:** ✅ **GO.**

---

## 6. Empfehlung (Go/No-Go)

1. **NO-GO** für jede Form von synchronem Multi-GPU-Data-Parallel-Training (Option 1 & 2) auf der aktuellen Hardware.
2. **GO** für **Gradient Accumulation** als Batch-Skalierung auf der 4070 Ti (nutzt vorhandene Autograd-/Checkpointing-Infra, keine neue Netzwerk-/Interop-Schicht).
3. **GO** für **2070 als asynchronen Eval/Inferenz-Knoten** über den E2b-Checkpoint-Pfad (Datei als Transport). Vor Produktiveinsatz LAN mit `iperf3` messen; selbst 1 GbE reicht, weil ein Checkpoint einmalig (nicht pro Schritt) übertragen wird.

Damit bleibt der Kern-Runtime unangetastet (kein MooTensor-Struct-Umbau, keine Multi-Device-Ownership-Komplexität im Refcount-Minenfeld).

---

## 7. Warum das die richtige Grenze ist (Kern-Perspektive)

Multi-Device-Training würde den **MooTensor-Ownership-/Refcount-Vertrag** um eine Device-Sync-Dimension erweitern (welcher Rank hält die kanonische Kopie, wann ist `valid`/`grad_valid` über Geräte konsistent, Fence-vs-Refcount-Race). Das ist genau die Minenzone, die D1 mit dem Valid-Masken-Vertrag gerade **eindämmen** sollte. Ein Cross-Device-**Checkpoint** (E2b) serialisiert an einer sauberen Optimizer-Schritt-Grenze und umgeht diese Komplexität vollständig — ein einziger konsistenter Snapshot statt fortlaufender Sync-Invarianten. Die Empfehlung ist also nicht nur Performance-, sondern auch **Korrektheits-getrieben**.

---

## 8. Revisit-Bedingungen (wann Multi-GPU doch lohnt)

Multi-GPU-Training neu bewerten, sobald **eine** dieser Bedingungen eintritt:
- Zwei **homogene** GPUs im **selben Gehäuse** mit **PCIe Gen4 x16** (oder NVLink) — dann ist P2P-All-Reduce im 13–28 GB/s-Bereich, Kommunikation < Rechenzeit für Small+.
- Ein **≥25 GbE / InfiniBand**-Fabric zwischen den Knoten (senkt LAN-Kosten um ~20–200×).
- Modelle, deren **Rechenzeit/Schritt** so wächst (dim ≥ 1024, viele Layer), dass die Kommunikation relativ klein wird (Compute-gebundenes Regime) — dann ggf. Gradient-Compression + Overlap prüfen.

---

## 9. Optionales deterministisches 2-Device-Gate-Konzept

Da kein Live-All-Reduce gebaut wird, ist das sinnvolle 2-Device-Gate ein **Checkpoint-Transfer-Determinismus-Gate** (baut direkt auf E2b auf):

**Gate „X1-2dev-eval-determinismus":**
1. Trainiere K Schritte GPU-resident auf **Device A** (4070 Ti), Adam, fixed seed.
2. Fence + E2b-Save (p*/m/v/t → Host → f32-Checkpoint).
3. Lade Checkpoint auf **Device B** (2070; wenn 2070 nicht lokal erreichbar: **CPU als Stellvertreter-Device** — reiner Load+Forward ist reduktionsfrei und daher bit-exakt).
4. **Assert:** Forward/Eval-Logits Device-B == Device-A-Referenz **bit-identisch** (reiner Load+Forward, kein Reduktions-Toleranzthema) bzw. Weitertraining innerhalb dokumentierter Toleranz (falls reduktionslastig).
5. **Negativ-Kontrolle:** Ein absichtlich verfälschter Checkpoint-Wert macht das Gate rot (beweist, dass das Gate scharf ist).

> Dieses Gate ist heute schon zu ~90 % durch E2b (21 Checks, Cross-Device bit-genau) abgedeckt; X1 fügt nur den expliziten **Eval-auf-Zweitkarte**-Fall + CPU-Stellvertreter hinzu. Falls jemals ein All-Reduce gebaut wird: separates Gate „2-Rank-Ring-Reduce eines fixen Gradientenvektors == serielle Summe innerhalb f32-Toleranz, feste Reduktionsreihenfolge für Bit-Identität".

---

## 10. Offene Messpunkte (vor 2-Node-Deployment, NICHT hardcoden)

- `iperf3` Node A ↔ Unraid-Node: reale LAN-Bandbreite + Latenz (bestimmt, ob 3b-Checkpoint-Transfer Sekunden oder Minuten dauert).
- Ob der 4070-Ti-Slot dauerhaft x8 bleibt (BIOS/Board-Lane-Sharing prüfen) — betrifft E2b-Download-Zeit und jeden Host-Staging-Pfad.
- 2070-Treiber/Vulkan-Version auf dem Unraid-Host (für die Eval-Worker-Kompatibilität mit dem f32-Checkpoint-Format).

---

## Anhang A — Reproduzierbare Inventur-Befehle
```
nvidia-smi --query-gpu=index,name,memory.total,pcie.link.gen.current,pcie.link.gen.max,pcie.link.width.current,pcie.link.width.max --format=csv
lspci -s 3d:00.0 -vv | grep -iE 'LnkCap:|LnkSta:'
vulkaninfo --summary | grep -iE 'deviceName|driverID'
ls /usr/share/vulkan/icd.d/
ldconfig -p | grep -iE 'nccl|libcudart'
nproc
grep -rilE 'nccl|cudaMemcpy|<cuda_runtime' compiler/runtime/*.c compiler/runtime/*.h   # -> leer (Vulkan-only)
ls compiler/runtime/shader_ki/*.comp | wc -l                                           # -> 24
# vor 2-Node: iperf3 -c <unraid-host>   (LAN messen, nicht annehmen)
```

## Anhang B — Rohdaten (live, 2026-07-10)
```
0, NVIDIA GeForce RTX 4070 Ti, 12282 MiB, 2, 4, 8, 16
LnkCap: Port #0, Speed 16GT/s, Width x16, ASPM L1, Exit Latency L1 <4us
LnkSta: Speed 16GT/s, Width x8 (downgraded)
vulkaninfo: GPU0 deviceName = NVIDIA GeForce RTX 4070 Ti; driverID = DRIVER_ID_NVIDIA_PROPRIETARY
ICD: nvidia_icd.json     VK-DEV-COUNT: 1
NCCL: kein libnccl       CUDA-RT: libcudart.so.13 (/opt/cuda, von moo ungenutzt)
CPU threads: 32          shader_ki/*.comp: 24
```
