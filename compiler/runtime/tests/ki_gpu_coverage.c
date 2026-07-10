/**
 * ki_gpu_coverage.c — KIP-G3d Coverage-Harness (G0 §3, das G4-Start-Gate).
 *
 * Fuehrt einen residenten M-A-artigen Trainingsschritt (Embedding-Gather ->
 * RMSNorm -> lineare Head-Projektion -> fused Cross-Entropy; vollstaendiger
 * Backward ce_bw -> matmul_bw -> norm_bw -> scatter_add; Gradient-Akkumulation
 * grad_accum; Adam-Schritt) UEBER MEHRERE SCHRITTE komplett GPU-resident aus
 * und prueft maschinell die sechs G0-§3-Erfolgskriterien:
 *   (1) Prozess-Exit 0.
 *   (2) >= 2 vollstaendige Schritte Forward+Backward+Optimizer (Steady-State).
 *   (3) Loss + Parameter nach jedem Schritt finit (isfinite).
 *   (4) cpu_fallbacks == 0.
 *   (5) Erwartete Op-Namen tatsaechlich ausgefuehrt (Positivliste unten ->
 *       exakte Submit-Zahl je Schritt beweist, dass JEDER erwartete Op lief).
 *   (6) uploads/downloads nur im Randbereich: nach dem Setup werden im
 *       Trainings-Loop 0 Uploads und nur die inhaerenten CE-Loss-Downloads
 *       gezaehlt (Residenz-Beweis).
 *
 * Positivliste (M-A-Op-Set) je Schritt: gather, norm(rms), matmul, ce_fwd,
 * ce_bw, matmul_bw (=2 transpose + 2 matmul), norm_bw, scatter_add,
 * unary(muls, Grad-Nullung) x2, grad_accum x2, opt_adam x2
 *   = 1+1+1+1 +1+4+1+1 +2+2+2 = 17 Compute-Submits/Schritt.
 *
 * Ohne echte Vulkan-Hardware SKIP (Exit 0 — ein SKIP ist KEIN Beweis, das
 * Skript ist nur auf der inventarisierten GPU aussagekraeftig). Ohne ASan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "moo_ki_gpu_api.h"

#define B 8            /* Batch (Tokens) */
#define D 6            /* Modell-Dimension */
#define V 10           /* Vokabular / Klassen */
#define XN (B * D)     /* 48 */
#define LN (B * V)     /* 80 */
#define PN (D * V)     /* 60  = Whead [D,V] = Emb [V,D] */
#define STEPS 2
#define SUBMITS_PRO_SCHRITT 17

static int fehler = 0;
#define CHECK(bed, msg) do { \
    if (bed) { printf("  OK   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fehler++; } \
} while (0)

static bool alle_finit(const float* x, int n) {
    for (int i = 0; i < n; i++) if (!isfinite(x[i])) return false;
    return true;
}

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) {
        printf("SKIP: keine GPU-Residenz (kein Vulkan/keine GPU) — Coverage nicht bewiesen\n");
        return 0;
    }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G3d Coverage: residenter M-A-Trainingsschritt (G0 §3) ==\n");

    /* --- Parameter / Zustand (persistent, resident) --- */
    void* hEmb  = moo_ki_gpu_buf_belegen((int64_t)PN * 4);   /* [V,D] */
    void* hWh   = moo_ki_gpu_buf_belegen((int64_t)PN * 4);   /* [D,V] */
    void* hIdx  = moo_ki_gpu_buf_belegen((int64_t)B * 4);
    void* hTgt  = moo_ki_gpu_buf_belegen((int64_t)LN * 4);
    void* hMvW  = moo_ki_gpu_buf_belegen((int64_t)PN * 2 * 4); /* Adam m|v Whead */
    void* hMvE  = moo_ki_gpu_buf_belegen((int64_t)PN * 2 * 4); /* Adam m|v Emb */
    void* hGWh  = moo_ki_gpu_buf_belegen((int64_t)PN * 4);   /* Grad-Akku Whead */
    void* hGE   = moo_ki_gpu_buf_belegen((int64_t)PN * 4);   /* Grad-Akku Emb */
    /* --- Aktivierungen / Zwischengradienten (resident, je Schritt neu geschrieben) --- */
    void* hX    = moo_ki_gpu_buf_belegen((int64_t)XN * 4);   /* gather-out [B,D] */
    void* hXn   = moo_ki_gpu_buf_belegen((int64_t)XN * 4);   /* rmsnorm [B,D] */
    void* hLog  = moo_ki_gpu_buf_belegen((int64_t)LN * 4);   /* logits [B,V] */
    void* hDlog = moo_ki_gpu_buf_belegen((int64_t)LN * 4);   /* dlogits [B,V] */
    void* hDxn  = moo_ki_gpu_buf_belegen((int64_t)XN * 4);   /* dxn [B,D] */
    void* hDx   = moo_ki_gpu_buf_belegen((int64_t)XN * 4);   /* dx [B,D] */
    void* hDWh  = moo_ki_gpu_buf_belegen((int64_t)PN * 4);   /* dWhead [D,V] */
    void* hDE   = moo_ki_gpu_buf_belegen((int64_t)PN * 4);   /* dEmb [V,D] */
    CHECK(hEmb && hWh && hIdx && hTgt && hMvW && hMvE && hGWh && hGE &&
          hX && hXn && hLog && hDlog && hDxn && hDx && hDWh && hDE,
          "16 residente Buffers belegt");

    /* --- Initialwerte (Host) + Upload (Randbereich, VOR dem Loop) --- */
    float emb[PN], wh[PN], idx[B], tgt[LN], nullm[PN * 2], nullg[PN];
    for (int i = 0; i < PN; i++) emb[i] = 0.15f * sinf(0.11f * (float)i) - 0.05f;
    for (int i = 0; i < PN; i++) wh[i]  = 0.12f * cosf(0.09f * (float)i) + 0.03f;
    int idxv[B] = { 0, 3, 7, 2, 9, 5, 1, 4 };
    for (int i = 0; i < B; i++) idx[i] = (float)idxv[i];
    memset(tgt, 0, sizeof tgt);
    for (int i = 0; i < B; i++) tgt[i * V + (i % V)] = 1.0f;   /* one-hot */
    memset(nullm, 0, sizeof nullm);
    memset(nullg, 0, sizeof nullg);

    bool up = moo_ki_gpu_upload(hEmb, emb, (int64_t)PN * 4)
           && moo_ki_gpu_upload(hWh,  wh,  (int64_t)PN * 4)
           && moo_ki_gpu_upload(hIdx, idx, (int64_t)B * 4)
           && moo_ki_gpu_upload(hTgt, tgt, (int64_t)LN * 4)
           && moo_ki_gpu_upload(hMvW, nullm, (int64_t)PN * 2 * 4)
           && moo_ki_gpu_upload(hMvE, nullm, (int64_t)PN * 2 * 4)
           && moo_ki_gpu_upload(hGWh, nullg, (int64_t)PN * 4)
           && moo_ki_gpu_upload(hGE,  nullg, (int64_t)PN * 4);
    CHECK(up, "Setup-Upload (Parameter/Zustand) — Randbereich");

    const float lr = 0.02f, b1 = 0.9f, b2 = 0.999f, eps_a = 1e-8f, nrm_eps = 1e-5f;
    const float scale = 1.0f / (float)B;

    /* Telemetrie NACH dem Setup nullen -> misst ausschliesslich den Loop. */
    moo_ki_gpu_telemetrie_reset();

    bool loop_ok = true, loss_finit = true;
    for (int schritt = 1; schritt <= STEPS; schritt++) {
        double loss = 0.0;
        bool ok =
            /* Forward */
            moo_ki_gpu_gather_res(hEmb, hIdx, hX, B, D, V) &&
            moo_ki_gpu_norm_res(MOO_KI_NORM_RMS, hX, hXn, B, D, nrm_eps) &&
            moo_ki_gpu_matmul_res(hXn, hWh, hLog, B, D, V) &&
            moo_ki_gpu_ce_fwd_res(hLog, hTgt, B, V, &loss) &&
            /* Backward */
            moo_ki_gpu_ce_bw_res(hLog, hTgt, hDlog, B, V, scale) &&
            moo_ki_gpu_matmul_bw_res(hXn, hWh, hDlog, hDxn, hDWh, B, D, V) &&
            moo_ki_gpu_norm_bw_res(MOO_KI_NORM_RMS, hX, hDxn, hDx, B, D, nrm_eps) &&
            moo_ki_gpu_scatter_add_res(hDx, hIdx, hDE, B, D, V) &&
            /* Grad-Akkumulation (Nullung via muls*0 + += grad_accum) */
            moo_ki_gpu_unary_res(MOO_KI_U_MULS, hGWh, hGWh, PN, 0.0f) &&
            moo_ki_gpu_grad_accum_res(hGWh, hDWh, PN) &&
            moo_ki_gpu_unary_res(MOO_KI_U_MULS, hGE, hGE, PN, 0.0f) &&
            moo_ki_gpu_grad_accum_res(hGE, hDE, PN) &&
            /* Optimizer-Schritt (Adam) */
            moo_ki_gpu_opt_adam_res(hWh, hGWh, hMvW, PN, lr, b1, b2, eps_a, 0.0f, 0, schritt) &&
            moo_ki_gpu_opt_adam_res(hEmb, hGE, hMvE, PN, lr, b1, b2, eps_a, 0.0f, 0, schritt);
        if (!ok) loop_ok = false;
        if (!isfinite(loss)) loss_finit = false;
        printf("  .. Schritt %d: loss=%.6f%s\n", schritt, loss,
               isfinite(loss) ? "" : "  [NICHT FINIT]");
    }
    CHECK(loop_ok, "2 vollstaendige F+B+Optimizer-Schritte ausgefuehrt (Kriterium 2)");
    CHECK(loss_finit, "Loss nach jedem Schritt finit (Kriterium 3a)");

    /* Telemetrie-Auswertung des Loops (Kriterien 4/5/6). */
    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    CHECK(tel.cpu_fallbacks == 0, "cpu_fallbacks == 0 (Kriterium 4)");
    CHECK(tel.submits == (uint64_t)SUBMITS_PRO_SCHRITT * STEPS,
          "Submit-Zahl == erwartete Op-Positivliste je Schritt (Kriterium 5)");
    CHECK(tel.uploads == 0, "0 Uploads im Trainings-Loop — Residenz (Kriterium 6a)");
    CHECK(tel.downloads == (uint64_t)STEPS,
          "Downloads == nur CE-Loss je Schritt (Randbereich, Kriterium 6b)");

    /* Parameter nach dem Loop materialisieren + Finitheit pruefen (Kriterium 3b). */
    float outw[PN], oute[PN];
    bool dl = moo_ki_gpu_download(hWh, outw, (int64_t)PN * 4)
           && moo_ki_gpu_download(hEmb, oute, (int64_t)PN * 4);
    CHECK(dl && alle_finit(outw, PN) && alle_finit(oute, PN),
          "Parameter (Whead, Emb) nach Training finit (Kriterium 3b)");

    moo_ki_gpu_buf_freigeben(hEmb); moo_ki_gpu_buf_freigeben(hWh);
    moo_ki_gpu_buf_freigeben(hIdx); moo_ki_gpu_buf_freigeben(hTgt);
    moo_ki_gpu_buf_freigeben(hMvW); moo_ki_gpu_buf_freigeben(hMvE);
    moo_ki_gpu_buf_freigeben(hGWh); moo_ki_gpu_buf_freigeben(hGE);
    moo_ki_gpu_buf_freigeben(hX);   moo_ki_gpu_buf_freigeben(hXn);
    moo_ki_gpu_buf_freigeben(hLog); moo_ki_gpu_buf_freigeben(hDlog);
    moo_ki_gpu_buf_freigeben(hDxn); moo_ki_gpu_buf_freigeben(hDx);
    moo_ki_gpu_buf_freigeben(hDWh); moo_ki_gpu_buf_freigeben(hDE);

    printf("\n%s — G3d-Coverage %s (fehler=%d)\n",
           fehler == 0 ? "ALLE PASS" : "FEHLER",
           fehler == 0 ? "GRUEN (Kriterien 1-6 erfuellt, G4 entsperrt)" : "ROT",
           fehler);
    return fehler == 0 ? 0 : 1;   /* Kriterium 1: Exit 0 nur bei allen gruen */
}
