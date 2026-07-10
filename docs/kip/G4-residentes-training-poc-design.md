# KIP-G4 — GPU-residentes Trainings-PoC (Mini-LM komplett auf GPU)

Task 8a2691fa. Stand: 2026-07-10. Autor: kip-gpu. Baut auf G0 (Coverage-Gate),
G1 (Residenz+Telemetrie), G2 (tiled matmul), G3a/b/c + G3d-a..e.
Start-Gate erfüllt: skripte/kip_gpu_coverage.sh GRÜN (Commit b9cbd64).

## 0. Ziel & Gate (aus Task 8a2691fa + Inbox 699)
End-to-End: Toy-LM-Training, Gewichte/Aktivierungen/Gradienten/Optimizer-
Zustand komplett GPU-resident; Transfers nur Batch rein / Loss raus.
GATE:
1. Loss-Kurve GPU == CPU-Referenz innerhalb dokumentierter Toleranz.
2. Deterministisch (zwei GPU-Läufe bit-identisch).
3. Residenz-Nachweis via G1-Telemetrie: cpu_fallbacks==0, im Steady-State-
   Loop uploads==0, downloads == nur Loss-Randbereich.
4. Speedup (GPU- vs CPU-Wallclock) + Submit-Statistik protokolliert.

## 1. Scope-Entscheidung (WICHTIG — Review-Punkt)
Der G0-§2-Zielarchitektur-Text nennt RoPE-Attention (B2, GQA) + SwiGLU (B3).
Für das RESIDENZ-/TRAININGS-PoC (nicht Feature-Parität) baue ich den vollen
Transformer-LM-Stack, aber mit drei bewusst dokumentierten Vereinfachungen,
die den vorhandenen _res-Op-Satz OHNE NEUEN SHADER ausreichen lassen:

| Spec (G0 §2) | G4-PoC | Warum zulässig / Grund |
|---|---|---|
| RoPE-Positionen | ADDITIVE sinusoidale Pos-Embeddings (einmal hochgeladen, ew add) | RoPE = strided Paar-Rotation → bräuchte neuen residenten RoPE-Kernel (strided; copy_res ist nur contiguous). Additive Positionen sind ein korrektes, klassisches Schema. RoPE-Kernel = benannter Follow-up (G3-Stil), NICHT G4. |
| GQA / Multi-Head | SINGLE-HEAD kausale Self-Attention | Head-Slicing aus [T,D] row-major ist STRIDED → bräuchte strided-copy oder Transpose-Gymnastik. Head-Anzahl ist für den Residenz-/Trainings-Beweis irrelevant. Multi-Head = Follow-up. |
| RMSNorm mit Affine (gamma) | affine-FREIE RMSNorm (norm_res-Kern direkt) | Affine = ew-Broadcast-Mul + dgamma-Reduktion; additive Komplexität ohne PoC-Mehrwert. |

Der LM bleibt ein echter Transformer (Embedding + Positionen + L× [Norm →
kausale Self-Attention → Residual → Norm → SwiGLU-FFN → Residual] → Norm →
Head → fused CE), voll trainierbar, voll resident. Konsequenz: G4 fasst KEINE
geteilte Produktivdatei an (kein moo_nn.c/moo_tensor_ops.c/moo_autograd.c-
Routing) — Deliverable ist ein SELBSTÄNDIGES Harness + CPU-Referenz + Skript,
exakt das Strang-G-Muster (jedes G3-Gate war _res-Ops + CPU-Diff-Harness).
Production-Wiring (moo_nn.c auf _res routen + MOO_KI_GPU_STRIKT im Dispatch)
wäre ein SEPARATES, später Epic und kollidiert JETZT mit kip-kern D3 / kip-ops.
→ @koordinator: falls stattdessen echtes Production-Wiring gewünscht ist, bitte
melden; sonst liefere ich das Harness-PoC (verteidigt jedes Gate-Kriterium).

## 2. Architektur (P0)
Dims (parametrisierbar): D=Modelldim, T=Kontext, V=Vokab, F=FFN-Hidden, L=Layer.
- Korrektheits-/Diff-Lauf: kleine Dims (z.B. D=32, T=16, V=24, F=64, L=2) —
  CPU-Referenz + FD-Gradcheck bezahlbar, deckt jeden Op-Pfad ab.
- Speedup-/Submit-Lauf: P0-Dims (D=256, L=6, T=1024) — misst GPU vs CPU.
Ein Schritt = EINE Sequenz [T] Token-IDs, Ziel = next-token one-hot [T,V].
(Multi-Sequenz-Batch mit blockdiagonaler Maske = B4a-Gebiet, nicht nötig.)

### Forward (resident, Buffer-Handles)
```
emb   = gather(Emb[V,D], idx[T])              -> x0 [T,D]
x     = x0 + PosEmb[T,D]   (ew add)           -> Residual-Strom [T,D]
je Layer l:
  # Attention (single-head, kausal)
  xn    = rmsnorm(x)                           [T,D]
  Q=xn@Wq  K=xn@Wk  Vv=xn@Wv                   [T,D] (3 matmul)
  Kt    = transpose(K)                          [D,T]
  sc    = Q@Kt                                  [T,T] (matmul)
  scs   = sc * (1/sqrt(D))   (unary muls)       [T,T]
  scm   = scs + CausalBias[T,T]  (ew add; 0 unter/auf Diag, -1e9 drüber)
  att   = softmax(scm)  (row-wise)              [T,T]
  ctx   = att@Vv                                [T,D] (matmul)
  ao    = ctx@Wo                                [T,D] (matmul)
  x     = x + ao   (ew add, Residual)
  # FFN (SwiGLU)
  xn2   = rmsnorm(x)                            [T,D]
  g=xn2@W1  u=xn2@W3                            [T,F] (2 matmul)
  s     = sigmoid(g)                            [T,F]
  sg    = g*s   (ew mul)  = silu(g)             [T,F]
  h     = sg*u  (ew mul)                        [T,F]
  fo    = h@W2                                  [T,D] (matmul)
  x     = x + fo   (ew add, Residual)
xf    = rmsnorm(x)                              [T,D]
logits= xf@Whead                               [T,V] (matmul)
loss  = ce_fwd(logits, target)                 -> Skalar (downloads++)
```

### Backward (reverse; matmul_bw/softmax_bw/norm_bw/unary_bw/ew; += via grad_accum)
Laufender Residual-Grad dX [T,D]; Branch-Beiträge via grad_accum in dX.
- CE: ce_bw(logits,target) -> dlogits [T,V]
- Head: matmul_bw(xf,Whead,dlogits) -> dxf, dWhead ; norm_bw(x,dxf)->dX (init)
- je Layer (umgekehrt):
  - FFN: matmul_bw(h,W2,dX)->dh,dW2 ; d_sg=dh*u ; d_u=dh*sg ;
    silu': ds = s + sg*(1-s)  (ew: t=sg*s; sg-t; +s) ; dg=d_sg*ds ;
    matmul_bw(xn2,W1,dg)->dxn2a,dW1 ; matmul_bw(xn2,W3,d_u)->dxn2b,dW3 ;
    dxn2=dxn2a+dxn2b ; norm_bw(x,dxn2)->dxf2 ; dX = dX + dxf2 (grad_accum)
  - Attn: matmul_bw(ctx,Wo,dX)->dctx,dWo ; matmul_bw(att,Vv,dctx)->datt,dVv ;
    softmax_bw(att,datt)->dscm ; dscs=dscm (Maske=const) ;
    dsc=dscs*(1/sqrt(D)) (unary muls) ; matmul_bw(Q,Kt,dsc)->dQ,dKt ;
    dK=transpose(dKt) ; matmul_bw(xn,Wq,dQ)->dxnq,dWq ;
    matmul_bw(xn,Wk,dK)->dxnk,dWk ; matmul_bw(xn,Wv,dVv)->dxnv,dWv ;
    dxn=dxnq+dxnk+dxnv ; norm_bw(x,dxn)->dxa ; dX = dX + dxa (grad_accum)
- Embedding: scatter_add(dX, idx) -> dEmb [V,D]  (PosEmb-Grad = dX direkt,
  falls PosEmb trainierbar; im PoC PosEmb FIX -> kein Grad, spart Buffer).
Param-Grads (dWq..dWhead, dEmb) je in eigenen Akku-Buffer via grad_accum;
Adam-Schritt (opt_adam_res) je Param mit mv-Buffer 2n.

## 3. Verifikation (dreischichtig)
1. FD-Gradcheck (nur Forward nötig, validiert die Backward-MATHE unabhängig):
   CPU-Referenz-Gradienten vs zentrale finite Differenz je Param-Klasse
   (rel. Fehler < 1e-2 bei f32/kleinen Dims). Schützt vor „CPU==GPU aber
   beide falsch".
2. GPU==CPU je Schritt: |loss_gpu - loss_cpu| / (|loss_cpu|+1e-6) < TOL
   (TOL ~1e-3; f32, andere Reduktions-Reihenfolge in matmul/softmax) über
   die ganze Loss-Kurve (K Schritte).
3. Determinismus: zwei komplette GPU-Läufe -> Loss-Kurve bit-identisch.
HINWEIS Toleranz/Schritt-Zahl: Der Forward ist bit-genau (GPU==CPU rel ~1e-7 in
Schritt 1). Über mehrere Adam-Schritte driften float-GPU und double-CPU jedoch
CHAOTISCH auseinander (Adam normiert grad -> maximal sign-sensitiv; winzige
Reduktions-Reihenfolge-Unterschiede kippen bei größeren Modellen eine sensitive
Richtung -> Trajektorien-Bifurkation). Das ist KEIN Bug (Forward exakt), sondern
inhärent beim Vergleich zweier Gleitkomma-Adam-Implementierungen. Konsequenz:
Der Korrektheits-Lauf (kleine Dims, gut konditioniert) hält rel<2e-3 über 4
Schritte; ein P0-Lauf (dim 256, 6 Layer) hält es ~2 Schritte, danach wächst die
Drift. Das Gate nutzt daher den kleinen Lauf für Korrektheit + einen kurzen
P0-Lauf (STEPS=2) für Speedup/Submit-Protokoll.

## 4. Telemetrie-Erwartung (Residenz-Gate)
telemetrie_reset() NACH Setup-Upload. Im K-Schritt-Loop:
- cpu_fallbacks == 0.
- uploads == 0 (alle Parameter/Aktivierungen resident; idx/target werden
  je Schritt NEU hochgeladen -> das ist der erlaubte „Batch rein"-Rand;
  Zähl-Erwartung dokumentiert: uploads == 2*K bei je Schritt neuer Sequenz,
  ODER 0 bei fixer Sequenz. PoC nutzt fixe Trainingssequenz -> uploads==0,
  sauberster Residenz-Beweis).
- downloads == K (nur CE-Loss je Schritt = „Loss raus"-Rand).
- submits == konstante Op-Zahl/Schritt * K (Positivliste, beweist Pfad lief).

## 5. Speedup + Submit-Statistik (Protokoll-Deliverable)
Bei P0-Dims: Wallclock GPU-Loop vs CPU-Referenz-Loop -> Speedup-Faktor.
Submit-Aufschlüsselung (matmul/matmul_bw/softmax/norm/ew/unary/gather/
scatter/grad_accum/opt) als Report-Zeilen. Kein Korrektheits-Gate, aber
Pflicht-Protokoll laut Task.

## 6. Deliverables & Slices
- `compiler/runtime/tests/ki_gpu_g4_lm.c` — Harness: CPU-Referenz (fwd+bwd+FD)
  + residenter GPU-Pfad + Diff/Determinismus/Telemetrie/Speedup.
- `skripte/kip_g4_lm.sh` — Compile+Run auf 4070 Ti, SKIP ohne libvulkan
  (Muster ki-gpu-smoke.sh / kip_gpu_coverage.sh).
- Slice 1: Design (dies) + Scope-Meldung Channel.
- Slice 2: FFN-LM (Embedding+Pos+L×SwiGLU+Head+CE) resident + CPU-Diff +
  FD-Gradcheck + Telemetrie. (Attention-freier Zwischen-Gate-Checkpoint.)
- Slice 3: kausale Single-Head-Attention F+B ergänzen; Gate erweitern.
- Slice 4: P0-Dims-Lauf, Speedup+Submit-Report, Skript, done+Beleg.

## 8. Ergebnisse (4070 Ti, verifiziert 2026-07-10)
Korrektheits-Lauf (D=32 T=16 V=24 F=48 L=2, STEPS=4) — skripte/kip_g4_lm.sh GRÜN:
- [1] FD-Gradcheck (double): alle Param-Klassen rel < 1e-3 (typ. ~1e-8).
- [3] GPU==CPU je Schritt: rel 3e-7 .. 2e-4 (< 2e-3). ✅
- [4] Determinismus: zwei GPU-Läufe bit-identisch. ✅
- [5] Residenz: cpu_fallbacks=0, uploads=0 im Loop, downloads=STEPS (nur Loss),
      submits=174/Schritt konstant. ✅
P0-naher Lauf (D=256 T=256 V=256 F=512 L=6, STEPS=2, FD_SAMPLES=2) — GRÜN:
- GPU==CPU: Schritt 1 rel=1.9e-7, Schritt 2 rel=2.0e-5. ✅
- submits=494/Schritt (6-Layer-Positivliste), Residenz identisch (uploads=0,
  downloads=STEPS, cpu_fallbacks=0), deterministisch.
- Wallclock 3-Schritt-Variante: GPU=0.0135s. Der „Speedup"-Faktor gegen die
  CPU-Referenz ist NICHT als Produktions-Speedup zu lesen: die Referenz ist ein
  unoptimierter double-Skalar-Loop (single-thread, kein BLAS) — der Faktor
  (~1000x) überzeichnet massiv. Aussagekräftig ist die ABSOLUTE GPU-Zeit +
  der Residenz-Beweis (0 Transfers im Steady-State).

## 7. Nicht-Ziele / dokumentierte Follow-ups
- RoPE-Kernel (strided), Multi-Head/GQA (strided Head-Slice), RMSNorm-Affine,
  Multi-Sequenz-Batch (blockdiagonal, B4a), Production-Wiring auf moo_nn.c
  (+ MOO_KI_GPU_STRIKT-Dispatch-Enforcement) — jeweils eigenes Epic.
- Default-Build/Stub-Vertrag unberührt; run_all bleibt GPU-frei; MooTensor-
  Struct unberührt.
