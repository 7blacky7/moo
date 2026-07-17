/**
 * ki_gpu_v3c.c — KI-MULTI-V3c Teil 2: GPU-residentes multimodales Trainings-PoC.
 *
 * Muster: tests/ki_gpu_g4_lm.c (KIP-G4). Baut auf denselben residenten
 * G3/G4-Bausteinen (moo_ki_gpu_api.h) auf und beweist den V3c-Kern:
 *
 *   Praefix-Injection (CONCAT) + Projektions-Gradient + MASKIERTE Cross-Entropy
 *   GPU-RESIDENT ueber mehrere SGD-Schritte, gegen eine double-CPU-Referenz.
 *
 * Der residente Trainingsschritt:
 *   e_inj [1,ENC]  (konstanter "Encoder-Output", frozen, EINMAL vor dem Loop
 *                   hochgeladen — Encoder bleibt draussen)
 *     -> Projektion dicht ENC->D  (trainierbar, reine Gewichtsmatrix P)
 *     -> CONCAT als Praefix vor T Text-Token-Embeddings (Gather, trainierbar)
 *        => Sequenz S = 1 + T  (Zeile 0 = Praefix, Zeilen 1..T = Text)
 *     -> additive Positions-Embeddings (konstant)
 *     -> 1 Transformer-Block  [RMSNorm -> kausale Single-Head-Attention ->
 *        Residual -> RMSNorm -> SwiGLU-FFN -> Residual]
 *     -> Final-RMSNorm -> Head
 *     -> MASKIERTE CE (Maske [0] + [1]*T: nur die T Text-Zeilen zaehlen,
 *        die Praefix-Zeile 0 hat Gewicht 0 in Loss UND Gradient)
 *     -> Backward -> SGD-Schritt.
 *
 * MASKIERTE CE ueber residente Ops: Head liefert logits [S,V]; die T Text-
 * Zeilen werden per moo_ki_gpu_copy_res (src_off) in einen [T,V]-Puffer
 * extrahiert, ce_fwd/ce_bw laufen NUR darueber (Mittel 1/T). Der Rueckwaerts-
 * Gradient wird per copy_res in die Zeilen 1..T von dlog[S,V] zurueckgestreut;
 * Zeile 0 wird aus einem vor dem Loop hochgeladenen Null-Zeilen-Puffer genullt.
 * -> Praefix-Zeile bekommt exakt Null Gradient (= Maske [0]).
 *
 * CONCAT ueber residente Ops: moo_ki_gpu_copy_res(e_proj -> cat[0]) und
 * copy_res(temb -> cat[1..T]); Backward = copy_res-Split (src_off) in
 * de_proj [1,D] und dtemb [T,D]. de_proj -> Projektions-Backward (gP,
 * trainierbar; de_inj wird verworfen — Encoder frozen); dtemb -> scatter_add
 * in die Text-Embeddings.
 *
 * ==== DOKUMENTIERTE ABWEICHUNG (ehrlich, Auflage des Koordinators) ====
 * Der Transformer-Block nutzt den in KIP-G4 BIT-GENAU verifizierten Baustein
 * (RMSNorm ohne Affine + Single-Head-Attention + SwiGLU-FFN), NICHT die
 * LayerNorm/GELU-Variante. Beide waeren resident (MOO_KI_NORM_LAYER,
 * MOO_KI_U_GELU existieren), aber der G4-Block ist der gegen die double-
 * Referenz abgesicherte Pfad — der V3c-Beweis sitzt DRUMHERUM (Praefix-Concat,
 * maskierte CE, Projektions-Gradient) und wird durch die Wahl des inneren
 * Blocks nicht beruehrt. Zusaetzliche PoC-Vereinfachungen wie in G4:
 * Single-Head statt GQA, additive Positionen statt RoPE, affine-freie RMSNorm.
 * Die Projektion ist eine reine Gewichtsmatrix (kein Bias) — der Gradient-
 * Beweis gilt dem Gewicht P.
 *
 * Dreischichtige Verifikation (Muster G4):
 *   [1] FD-Gradcheck der CPU-Referenz (double) — Backward-Mathe unabhaengig,
 *       inkl. der Projektion P und der Text-Embeddings.
 *   [3] GPU-Loss (float) == CPU-Referenz-Loss (double) je Schritt (rel < 2e-3).
 *   [4] Determinismus: zwei GPU-Laeufe bit-identisch.
 *   [5] Residenz-Telemetrie: cpu_fallbacks==0, uploads==0 im Loop (e_inj +
 *       Konstanten + Parameter VOR dem Loop hochgeladen), downloads==STEPS
 *       (nur der CE-Loss verlaesst die GPU), submits konstant je Schritt.
 *
 * Ohne libvulkan/GPU -> SKIP (Exit 0, KEIN Beweis). Ohne ASan (NVIDIA-Treiber-
 * Leak-Noise), wie die uebrigen GPU-Harnesses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "moo_ki_gpu_api.h"

#ifndef V3C_ENC
#define V3C_ENC 32          /* Encoder-Output-Dim (e_inj [1,ENC]) */
#endif
#ifndef V3C_D
#define V3C_D 48            /* Modell-Dim nach Projektion */
#endif
#ifndef V3C_T
#define V3C_T 8             /* Text-Token */
#endif
#ifndef V3C_V
#define V3C_V 24            /* Vokabular */
#endif
#ifndef V3C_F
#define V3C_F 64            /* SwiGLU-FFN-Breite */
#endif
#ifndef V3C_STEPS
#define V3C_STEPS 4         /* >= 3 Trainingsschritte */
#endif
#ifndef V3C_FD_SAMPLES
#define V3C_FD_SAMPLES 6
#endif
#define MAX2(a, b) ((a) > (b) ? (a) : (b))

enum { ENC = V3C_ENC, D = V3C_D, T = V3C_T, V = V3C_V, F = V3C_F, STEPS = V3C_STEPS };
enum { S = 1 + T };         /* Sequenzlaenge: Praefix + Text */
enum {
    ED = ENC * D, DD = D * D, DF = D * F, FD = F * D, VD = V * D, DV = D * V,
    SD = S * D, SS = S * S, SF = S * F, SV = S * V, TD = T * D, TV = T * V, DS = D * S
};
static const double EPS = 1e-5, LR = 0.01, MU = 0.9;
static double SCALE;        /* 1/sqrt(D) */

static int fehler = 0;
#define CHECK(bed, msg) do { \
    if (bed) { printf("  OK   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fehler++; } \
} while (0)

/* ===================== Bump-Arena (double) ===================== */
static double* g_arena = NULL;
static size_t g_apos = 0, g_acap = 0;
static double* aa(int n) {
    if (g_apos + (size_t)n > g_acap) { fprintf(stderr, "arena voll\n"); exit(2); }
    double* p = g_arena + g_apos; g_apos += (size_t)n;
    for (int i = 0; i < n; i++) p[i] = 0.0;
    return p;
}

/* ============= CPU-Referenz-Ops (double) — spiegeln die _res-Kernel ========= */
static void mm(const double* A, const double* B, double* C, int m, int k, int n) {
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++) {
            double s = 0.0;
            for (int p = 0; p < k; p++) s += A[i * k + p] * B[p * n + j];
            C[i * n + j] = s;
        }
}
/* C=A@B: dA=G@B^T [m,k], dB=A^T@G [k,n]. */
static void mm_bw(const double* A, const double* B, const double* G,
                  double* dA, double* dB, int m, int k, int n) {
    for (int i = 0; i < m; i++)
        for (int p = 0; p < k; p++) {
            double s = 0.0;
            for (int j = 0; j < n; j++) s += G[i * n + j] * B[p * n + j];
            dA[i * k + p] = s;
        }
    for (int p = 0; p < k; p++)
        for (int j = 0; j < n; j++) {
            double s = 0.0;
            for (int i = 0; i < m; i++) s += A[i * k + p] * G[i * n + j];
            dB[p * n + j] = s;
        }
}
static void trn(const double* A, double* O, int r, int c) {
    for (int i = 0; i < r; i++) for (int j = 0; j < c; j++) O[j * r + i] = A[i * c + j];
}
static void rms(const double* x, double* y, int r, int c, double eps) {
    for (int i = 0; i < r; i++) {
        double ms = 0.0;
        for (int j = 0; j < c; j++) ms += x[i * c + j] * x[i * c + j];
        double inv = 1.0 / sqrt(ms / (double)c + eps);
        for (int j = 0; j < c; j++) y[i * c + j] = x[i * c + j] * inv;
    }
}
static void rms_bw(const double* x, const double* g, double* dx, int r, int c, double eps) {
    for (int i = 0; i < r; i++) {
        double ms = 0.0;
        for (int j = 0; j < c; j++) ms += x[i * c + j] * x[i * c + j];
        double s = sqrt(ms / (double)c + eps);
        double mgn = 0.0;
        for (int j = 0; j < c; j++) mgn += g[i * c + j] * (x[i * c + j] / s);
        mgn /= (double)c;
        for (int j = 0; j < c; j++) dx[i * c + j] = (g[i * c + j] - (x[i * c + j] / s) * mgn) / s;
    }
}
static void smax(const double* a, double* o, int r, int c) {
    for (int i = 0; i < r; i++) {
        double mx = a[i * c];
        for (int j = 1; j < c; j++) if (a[i * c + j] > mx) mx = a[i * c + j];
        double sum = 0.0;
        for (int j = 0; j < c; j++) { o[i * c + j] = exp(a[i * c + j] - mx); sum += o[i * c + j]; }
        for (int j = 0; j < c; j++) o[i * c + j] /= sum;
    }
}
static void smax_bw(const double* y, const double* g, double* gin, int r, int c) {
    for (int i = 0; i < r; i++) {
        double dot = 0.0;
        for (int j = 0; j < c; j++) dot += g[i * c + j] * y[i * c + j];
        for (int j = 0; j < c; j++) gin[i * c + j] = y[i * c + j] * (g[i * c + j] - dot);
    }
}
static void sig(const double* a, double* o, int n) { for (int i = 0; i < n; i++) o[i] = 1.0 / (1.0 + exp(-a[i])); }
static void emul(const double* a, const double* b, double* o, int n) { for (int i = 0; i < n; i++) o[i] = a[i] * b[i]; }
static void eadd(const double* a, const double* b, double* o, int n) { for (int i = 0; i < n; i++) o[i] = a[i] + b[i]; }
static void esub(const double* a, const double* b, double* o, int n) { for (int i = 0; i < n; i++) o[i] = a[i] - b[i]; }
static void muls(const double* a, double* o, int n, double s) { for (int i = 0; i < n; i++) o[i] = a[i] * s; }
static void gath(const double* w, const double* idx, double* o, int rows, int dim, int vocab) {
    for (int i = 0; i < rows; i++) {
        int v = (int)idx[i]; if (v < 0) v = 0; if (v >= vocab) v = vocab - 1;
        for (int d = 0; d < dim; d++) o[i * dim + d] = w[v * dim + d];
    }
}
static void scat(const double* g, const double* idx, double* gw, int rows, int dim, int vocab) {
    for (int i = 0; i < vocab * dim; i++) gw[i] = 0.0;
    for (int i = 0; i < rows; i++) {
        int v = (int)idx[i]; if (v < 0) v = 0; if (v >= vocab) v = vocab - 1;
        for (int d = 0; d < dim; d++) gw[v * dim + d] += g[i * dim + d];
    }
}
/* Element-Kopie o[dst_off+i] = a[src_off+i] — Spiegel von moo_ki_gpu_copy_res. */
static void cpy(const double* a, double* o, int n, int src_off, int dst_off) {
    for (int i = 0; i < n; i++) o[dst_off + i] = a[src_off + i];
}
/* CE nur ueber [rows,cols] (Mittel 1/rows) — maskiert auf die Text-Zeilen. */
static void ce_f(const double* logits, const double* tgt, int r, int c, double* loss) {
    double sum = 0.0;
    for (int i = 0; i < r; i++) {
        double mx = logits[i * c];
        for (int j = 1; j < c; j++) if (logits[i * c + j] > mx) mx = logits[i * c + j];
        double se = 0.0;
        for (int j = 0; j < c; j++) se += exp(logits[i * c + j] - mx);
        double lse = mx + log(se);
        for (int j = 0; j < c; j++) sum += -tgt[i * c + j] * (logits[i * c + j] - lse);
    }
    *loss = sum / (double)r;
}
static void ce_b(const double* logits, const double* tgt, double* grad, int r, int c, double scale) {
    for (int i = 0; i < r; i++) {
        double mx = logits[i * c];
        for (int j = 1; j < c; j++) if (logits[i * c + j] > mx) mx = logits[i * c + j];
        double se = 0.0;
        for (int j = 0; j < c; j++) se += exp(logits[i * c + j] - mx);
        for (int j = 0; j < c; j++) {
            double p = exp(logits[i * c + j] - mx) / se;
            grad[i * c + j] = (p - tgt[i * c + j]) * scale;
        }
    }
}
/* SGD-mit-Momentum (Spiegel von moo_ki_gpu_opt_sgd_res): m=mu*m+g; p-=lr*m. */
static void sgd(double* p, double* m, const double* g, int n, double lr, double mu) {
    for (int i = 0; i < n; i++) { m[i] = mu * m[i] + g[i]; p[i] -= lr * m[i]; }
}

/* ===================== Parameter-Registry (CPU) ===================== */
#define NP 10
typedef struct { double* p; double* g; double* m; int n; const char* name; } CpuParam;
static CpuParam CP[NP];
static double* g_snap[NP];   /* Init-Snapshot: CPU-Training mutiert CP[].p, GPU startet von der Init */
static int np = 0;
static void reg(double* p, double* g, int n, const char* name) {
    CP[np].p = p; CP[np].g = g; CP[np].m = aa(n);
    CP[np].n = n; CP[np].name = name; np++;
}

/* Parameter (trainierbar) */
static double *cP, *cEmb, *cWq, *cWk, *cWv, *cWo, *cW1, *cW3, *cW2, *cWhead;
static double *gP, *gEmb, *gWq, *gWk, *gWv, *gWo, *gW1, *gW3, *gW2, *gWhead;
/* Konstanten (frozen / nicht trainierbar) */
static double *cEInj, *cPos, *cBias, *cIdx, *cTgt;
/* Aktivierungen (persistent, in place ueberschrieben) */
static double *cEProj, *cTemb, *cCat, *cStream0, *cXn, *cQ, *cK, *cVv, *cAtt,
              *cCtx, *cMid, *cXn2, *cG, *cU, *cS, *cSg, *cH, *cStream1, *cXf, *cLogits;

static void cpu_init_params(void) {
    /* Projektion ENC->D — der Star: deterministische Init. */
    cP = aa(ED); for (int i = 0; i < ED; i++) cP[i] = 0.22 * sin(0.041 * i + 0.7);
    cEmb = aa(VD); for (int i = 0; i < VD; i++) cEmb[i] = 0.30 * sin(0.07 * i + 0.3);
    cWq = aa(DD); cWk = aa(DD); cWv = aa(DD); cWo = aa(DD);
    for (int i = 0; i < DD; i++) {
        cWq[i] = 0.35 * sin(0.031 * i + 0.1);
        cWk[i] = 0.35 * cos(0.029 * i + 0.2);
        cWv[i] = 0.30 * sin(0.027 * i + 0.3);
        cWo[i] = 0.30 * cos(0.033 * i + 0.4);
    }
    cW1 = aa(DF); cW3 = aa(DF); cW2 = aa(FD);
    for (int i = 0; i < DF; i++) {
        cW1[i] = 0.30 * sin(0.017 * i + 0.5);
        cW3[i] = 0.30 * cos(0.019 * i + 0.6);
    }
    for (int i = 0; i < FD; i++) cW2[i] = 0.25 * sin(0.021 * i + 0.7);
    cWhead = aa(DV); for (int i = 0; i < DV; i++) cWhead[i] = 0.25 * cos(0.05 * i + 0.1);

    gP = aa(ED); reg(cP, gP, ED, "P");
    gEmb = aa(VD); reg(cEmb, gEmb, VD, "Emb");
    gWq = aa(DD); reg(cWq, gWq, DD, "Wq");
    gWk = aa(DD); reg(cWk, gWk, DD, "Wk");
    gWv = aa(DD); reg(cWv, gWv, DD, "Wv");
    gWo = aa(DD); reg(cWo, gWo, DD, "Wo");
    gW1 = aa(DF); reg(cW1, gW1, DF, "W1");
    gW3 = aa(DF); reg(cW3, gW3, DF, "W3");
    gW2 = aa(FD); reg(cW2, gW2, FD, "W2");
    gWhead = aa(DV); reg(cWhead, gWhead, DV, "Whead");

    /* Konstanter Encoder-Output (frozen). */
    cEInj = aa(ENC); for (int i = 0; i < ENC; i++) cEInj[i] = 0.5 * sin(0.13 * i + 0.9);
    /* Additive Positionen fuer S Zeilen (konstant). */
    cPos = aa(SD);
    for (int t = 0; t < S; t++)
        for (int d = 0; d < D; d++)
            cPos[t * D + d] = 0.05 * ((d % 2 == 0) ? sin(t / pow(10000.0, (double)d / D))
                                                   : cos(t / pow(10000.0, (double)(d - 1) / D)));
    /* Text-Token-Indizes + One-Hot-Targets (T Text-Positionen). */
    cIdx = aa(T); for (int i = 0; i < T; i++) cIdx[i] = (double)((i * 7 + 3) % V);
    cTgt = aa(TV); for (int i = 0; i < T; i++) cTgt[i * V + ((i * 5 + 1) % V)] = 1.0;
    /* Kausale Maske ueber die volle Sequenz S (Praefix Zeile 0 sichtbar fuer alle). */
    cBias = aa(SS);
    for (int t = 0; t < S; t++)
        for (int j = 0; j < S; j++) cBias[t * S + j] = (j <= t) ? 0.0 : -1e9;

    /* Persistente Aktivierungen. */
    cEProj = aa(D); cTemb = aa(TD); cCat = aa(SD); cStream0 = aa(SD);
    cXn = aa(SD); cQ = aa(SD); cK = aa(SD); cVv = aa(SD); cAtt = aa(SS);
    cCtx = aa(SD); cMid = aa(SD); cXn2 = aa(SD);
    cG = aa(SF); cU = aa(SF); cS = aa(SF); cSg = aa(SF); cH = aa(SF);
    cStream1 = aa(SD); cXf = aa(SD); cLogits = aa(SV);
}

static double cpu_forward(void) {
    double* tmpDS = aa(DS); double* tmpSS = aa(SS); double* tmpSD = aa(SD);
    /* Projektion + Praefix-Concat. */
    mm(cEInj, cP, cEProj, 1, ENC, D);
    gath(cEmb, cIdx, cTemb, T, D, V);
    cpy(cEProj, cCat, D, 0, 0);          /* Zeile 0 = Praefix */
    cpy(cTemb, cCat, TD, 0, D);          /* Zeilen 1..T = Text */
    eadd(cCat, cPos, cStream0, SD);
    /* Transformer-Block (S Zeilen). */
    rms(cStream0, cXn, S, D, EPS);
    mm(cXn, cWq, cQ, S, D, D);
    mm(cXn, cWk, cK, S, D, D);
    mm(cXn, cWv, cVv, S, D, D);
    trn(cK, tmpDS, S, D);
    mm(cQ, tmpDS, tmpSS, S, D, S);
    muls(tmpSS, tmpSS, SS, SCALE);
    eadd(tmpSS, cBias, tmpSS, SS);
    smax(tmpSS, cAtt, S, S);
    mm(cAtt, cVv, cCtx, S, S, D);
    mm(cCtx, cWo, tmpSD, S, D, D);
    eadd(cStream0, tmpSD, cMid, SD);
    rms(cMid, cXn2, S, D, EPS);
    mm(cXn2, cW1, cG, S, D, F);
    mm(cXn2, cW3, cU, S, D, F);
    sig(cG, cS, SF);
    emul(cG, cS, cSg, SF);
    emul(cSg, cU, cH, SF);
    mm(cH, cW2, tmpSD, S, F, D);
    eadd(cMid, tmpSD, cStream1, SD);
    /* Final-Norm + Head. */
    rms(cStream1, cXf, S, D, EPS);
    mm(cXf, cWhead, cLogits, S, D, V);
    /* Maskierte CE: nur die T Text-Zeilen (logits Zeilen 1..T). */
    double loss; ce_f(&cLogits[V], cTgt, T, V, &loss);
    return loss;
}

static void cpu_backward(void) {
    double* dlog = aa(SV);
    double* dX = aa(SD); double* dXf = aa(SD);
    double* dH = aa(SF); double* dsg = aa(SF); double* du = aa(SF); double* dg = aa(SF); double* tf = aa(SF);
    double* dxn2a = aa(SD); double* dxn2b = aa(SD); double* dxb = aa(SD);
    double* dCtx = aa(SD); double* dAtt = aa(SS); double* dScm = aa(SS);
    double* dQ = aa(SD); double* dKt = aa(DS); double* dK = aa(SD); double* dVv = aa(SD);
    double* dxnq = aa(SD); double* dxnk = aa(SD); double* dxnv = aa(SD); double* Kt = aa(DS);
    double* dEInj = aa(ENC);

    /* Maskierte CE-Backward: Zeile 0 = 0, Zeilen 1..T = pur (Mittel 1/T). */
    for (int i = 0; i < SV; i++) dlog[i] = 0.0;
    ce_b(&cLogits[V], cTgt, &dlog[V], T, V, 1.0 / (double)T);
    mm_bw(cXf, cWhead, dlog, dXf, gWhead, S, D, V);
    rms_bw(cStream1, dXf, dX, S, D, EPS);
    /* FFN-Backward. */
    mm_bw(cH, cW2, dX, dH, gW2, S, F, D);
    emul(dH, cU, dsg, SF);
    emul(dH, cSg, du, SF);
    emul(cSg, cS, tf, SF);
    esub(cSg, tf, tf, SF);
    eadd(cS, tf, tf, SF);
    emul(dsg, tf, dg, SF);
    mm_bw(cXn2, cW1, dg, dxn2a, gW1, S, D, F);
    mm_bw(cXn2, cW3, du, dxn2b, gW3, S, D, F);
    eadd(dxn2a, dxn2b, dxn2a, SD);
    rms_bw(cMid, dxn2a, dxb, S, D, EPS);
    eadd(dX, dxb, dX, SD);
    /* Attention-Backward. */
    mm_bw(cCtx, cWo, dX, dCtx, gWo, S, D, D);
    mm_bw(cAtt, cVv, dCtx, dAtt, dVv, S, S, D);
    smax_bw(cAtt, dAtt, dScm, S, S);
    muls(dScm, dScm, SS, SCALE);
    trn(cK, Kt, S, D);
    mm_bw(cQ, Kt, dScm, dQ, dKt, S, D, S);
    trn(dKt, dK, D, S);
    mm_bw(cXn, cWq, dQ, dxnq, gWq, S, D, D);
    mm_bw(cXn, cWk, dK, dxnk, gWk, S, D, D);
    mm_bw(cXn, cWv, dVv, dxnv, gWv, S, D, D);
    eadd(dxnq, dxnk, dxnq, SD);
    eadd(dxnq, dxnv, dxnq, SD);
    rms_bw(cStream0, dxnq, dxb, S, D, EPS);
    eadd(dX, dxb, dX, SD);
    /* Concat-Split: Zeile 0 -> Projektion, Zeilen 1..T -> Text-Embeddings. */
    mm_bw(cEInj, cP, &dX[0], dEInj, gP, 1, ENC, D);   /* gP trainierbar; dEInj verworfen */
    scat(&dX[D], cIdx, gEmb, T, D, V);
}

static void cpu_sgd(void) {
    for (int i = 0; i < np; i++)
        sgd(CP[i].p, CP[i].m, CP[i].g, CP[i].n, LR, MU);
}

/* ===================== FD-Gradcheck (double Forward) ===================== */
static bool fd_check_param(int pi, int samples) {
    CpuParam* q = &CP[pi];
    double h = 1e-4;
    double max_rel = 0.0;
    for (int s = 0; s < samples; s++) {
        int idx = (int)((s * 2654435761u) % (unsigned)q->n);
        double orig = q->p[idx];
        size_t sv = g_apos;
        q->p[idx] = orig + h; double lp = cpu_forward();
        q->p[idx] = orig - h; double lm = cpu_forward();
        q->p[idx] = orig;
        g_apos = sv;
        double num = (lp - lm) / (2.0 * h);
        double ana = q->g[idx];
        double rel = fabs(num - ana) / (fabs(num) + fabs(ana) + 1e-9);
        if (rel > max_rel) max_rel = rel;
        if (getenv("V3C_FD_DEBUG")) printf("      [%s idx=%d] num=%+.6e ana=%+.6e rel=%.3e\n", q->name, idx, num, ana, rel);
    }
    printf("    FD %-6s max_rel=%.3e (%d samples)\n", q->name, max_rel, samples);
    return max_rel < 1e-3;
}

#ifndef V3C_CPU_ONLY
/* ===================== GPU-residenter Pfad (float) ===================== */
#define MAXH 256
static void* g_handles[MAXH]; static int g_nh = 0;
static void* gb(int64_t floats) {
    void* h = moo_ki_gpu_buf_belegen(floats * 4);
    if (h && g_nh < MAXH) g_handles[g_nh++] = h;
    return h;
}
static void gb_free_all(void) { for (int i = 0; i < g_nh; i++) moo_ki_gpu_buf_freigeben(g_handles[i]); g_nh = 0; }

/* double->float Upload-Konversion (Params/Eingaben liegen als double vor). */
static float* g_upf = NULL;
static bool up(void* h, const double* src, int n) {
    for (int i = 0; i < n; i++) g_upf[i] = (float)src[i];
    return moo_ki_gpu_upload(h, g_upf, (int64_t)n * 4);
}

/* Konstanten */
static void *hEInj, *hPos, *hBias, *hIdx, *hTgtT, *hZeroRow;
/* Parameter / Grads / Momentum */
static void *hP, *hEmb, *hWq, *hWk, *hWv, *hWo, *hW1, *hW3, *hW2, *hWhead;
static void *hgP, *hgEmb, *hgWq, *hgWk, *hgWv, *hgWo, *hgW1, *hgW3, *hgW2, *hgWhead;
static void *hmP, *hmEmb, *hmWq, *hmWk, *hmWv, *hmWo, *hmW1, *hmW3, *hmW2, *hmWhead;
/* Aktivierungen */
static void *hEProj, *hTemb, *hCat, *hStream0, *hXn, *hQ, *hK, *hVv, *hAtt, *hCtx,
            *hMid, *hXn2, *hG, *hU, *hS, *hSg, *hH, *hStream1, *hXf, *hLogits, *hLogitsT, *hGradT, *hDlog;
/* Scratch / Backward-Temps */
static void *sDS, *sSS, *sSD, *sKt;
static void *dX, *dXf, *dH, *dsg, *ddu, *dg2, *dxn2a, *dxn2b, *dxb, *dCtx, *dAtt, *dScm,
            *dQ, *dKt, *dK, *dVv2, *dxnq, *dxnk, *dxnv, *dEProj, *dTemb, *dEInj;

typedef struct { void* p; void* g; void* m; int n; } GpuParam;
static GpuParam GP[NP]; static int ngp = 0;
static void greg(void* p, void* g, void* m, int n) { GP[ngp].p = p; GP[ngp].g = g; GP[ngp].m = m; GP[ngp].n = n; ngp++; }

static bool gpu_alloc(void) {
    hEInj = gb(ENC); hPos = gb(SD); hBias = gb(SS); hIdx = gb(T); hTgtT = gb(TV); hZeroRow = gb(V);
    hP = gb(ED); hEmb = gb(VD); hWq = gb(DD); hWk = gb(DD); hWv = gb(DD); hWo = gb(DD);
    hW1 = gb(DF); hW3 = gb(DF); hW2 = gb(FD); hWhead = gb(DV);
    hgP = gb(ED); hgEmb = gb(VD); hgWq = gb(DD); hgWk = gb(DD); hgWv = gb(DD); hgWo = gb(DD);
    hgW1 = gb(DF); hgW3 = gb(DF); hgW2 = gb(FD); hgWhead = gb(DV);
    hmP = gb(ED); hmEmb = gb(VD); hmWq = gb(DD); hmWk = gb(DD); hmWv = gb(DD); hmWo = gb(DD);
    hmW1 = gb(DF); hmW3 = gb(DF); hmW2 = gb(FD); hmWhead = gb(DV);
    hEProj = gb(D); hTemb = gb(TD); hCat = gb(SD); hStream0 = gb(SD); hXn = gb(SD); hQ = gb(SD);
    hK = gb(SD); hVv = gb(SD); hAtt = gb(SS); hCtx = gb(SD); hMid = gb(SD); hXn2 = gb(SD);
    hG = gb(SF); hU = gb(SF); hS = gb(SF); hSg = gb(SF); hH = gb(SF);
    hStream1 = gb(SD); hXf = gb(SD); hLogits = gb(SV); hLogitsT = gb(TV); hGradT = gb(TV); hDlog = gb(SV);
    sDS = gb(DS); sSS = gb(SS); sSD = gb(SD); sKt = gb(DS);
    dX = gb(SD); dXf = gb(SD); dH = gb(SF); dsg = gb(SF); ddu = gb(SF); dg2 = gb(SF);
    dxn2a = gb(SD); dxn2b = gb(SD); dxb = gb(SD); dCtx = gb(SD); dAtt = gb(SS); dScm = gb(SS);
    dQ = gb(SD); dKt = gb(DS); dK = gb(SD); dVv2 = gb(SD); dxnq = gb(SD); dxnk = gb(SD);
    dxnv = gb(SD); dEProj = gb(D); dTemb = gb(TD); dEInj = gb(ENC);

    greg(hP, hgP, hmP, ED); greg(hEmb, hgEmb, hmEmb, VD);
    greg(hWq, hgWq, hmWq, DD); greg(hWk, hgWk, hmWk, DD); greg(hWv, hgWv, hmWv, DD);
    greg(hWo, hgWo, hmWo, DD); greg(hW1, hgW1, hmW1, DF); greg(hW3, hgW3, hmW3, DF);
    greg(hW2, hgW2, hmW2, FD); greg(hWhead, hgWhead, hmWhead, DV);

    for (int i = 0; i < g_nh; i++) if (!g_handles[i]) return false;
    return true;
}

/* Konstanten EINMAL hochladen (e_inj frozen, Positionen/Maske/Targets/Null-Zeile). */
static bool gpu_upload_consts(void) {
    double* z = aa(V);   /* Null-Zeile (aa nullt) */
    return up(hEInj, cEInj, ENC) && up(hPos, cPos, SD) && up(hBias, cBias, SS)
        && up(hIdx, cIdx, T) && up(hTgtT, cTgt, TV) && up(hZeroRow, z, V);
}

/* Params (auf Init) + Momentum-Zustand (=0) VOR dem Loop hochladen. */
static bool gpu_upload_params(void) {
    int zmax = MAX2(MAX2(MAX2(DD, DF), FD), MAX2(VD, ED));
    double* z = aa(zmax);   /* Nullen fuer Momentum */
    bool ok = true;
    for (int i = 0; i < ngp && ok; i++)
        ok = up(GP[i].p, CP[i].p, GP[i].n) && up(GP[i].m, z, GP[i].n);
    return ok;
}

static double maxdiff(void* h, const double* ref, int n) {
    float* t = (float*)malloc((size_t)n * 4);
    moo_ki_gpu_download(h, t, (int64_t)n * 4);
    double m = 0.0;
    for (int i = 0; i < n; i++) { double d = fabs((double)t[i] - ref[i]); if (d > m) m = d; }
    free(t);
    return m;
}
#define G(e) do { if (!(e)) return false; } while (0)
static bool gpu_forward(double* loss) {
    /* Projektion + Praefix-Concat. */
    G(moo_ki_gpu_matmul_res(hEInj, hP, hEProj, 1, ENC, D));
    G(moo_ki_gpu_gather_res(hEmb, hIdx, hTemb, T, D, V));
    G(moo_ki_gpu_copy_res(hEProj, hCat, D, 0, 0));
    G(moo_ki_gpu_copy_res(hTemb, hCat, TD, 0, D));
    G(moo_ki_gpu_ew_res(0, hCat, hPos, hStream0, SD));
    /* Transformer-Block. */
    G(moo_ki_gpu_norm_res(MOO_KI_NORM_RMS, hStream0, hXn, S, D, (float)EPS));
    G(moo_ki_gpu_matmul_res(hXn, hWq, hQ, S, D, D));
    G(moo_ki_gpu_matmul_res(hXn, hWk, hK, S, D, D));
    G(moo_ki_gpu_matmul_res(hXn, hWv, hVv, S, D, D));
    G(moo_ki_gpu_transpose_res(hK, sDS, S, D));
    G(moo_ki_gpu_matmul_res(hQ, sDS, sSS, S, D, S));
    G(moo_ki_gpu_unary_res(MOO_KI_U_MULS, sSS, sSS, SS, (float)SCALE));
    G(moo_ki_gpu_ew_res(0, sSS, hBias, sSS, SS));
    G(moo_ki_gpu_softmax_res(MOO_KI_SM_SOFTMAX, sSS, hAtt, S, S));
    G(moo_ki_gpu_matmul_res(hAtt, hVv, hCtx, S, S, D));
    G(moo_ki_gpu_matmul_res(hCtx, hWo, sSD, S, D, D));
    G(moo_ki_gpu_ew_res(0, hStream0, sSD, hMid, SD));
    G(moo_ki_gpu_norm_res(MOO_KI_NORM_RMS, hMid, hXn2, S, D, (float)EPS));
    G(moo_ki_gpu_matmul_res(hXn2, hW1, hG, S, D, F));
    G(moo_ki_gpu_matmul_res(hXn2, hW3, hU, S, D, F));
    G(moo_ki_gpu_unary_res(MOO_KI_U_SIGMOID, hG, hS, SF, 0.0f));
    G(moo_ki_gpu_ew_res(2, hG, hS, hSg, SF));
    G(moo_ki_gpu_ew_res(2, hSg, hU, hH, SF));
    G(moo_ki_gpu_matmul_res(hH, hW2, sSD, S, F, D));
    G(moo_ki_gpu_ew_res(0, hMid, sSD, hStream1, SD));
    /* Final-Norm + Head. */
    G(moo_ki_gpu_norm_res(MOO_KI_NORM_RMS, hStream1, hXf, S, D, (float)EPS));
    G(moo_ki_gpu_matmul_res(hXf, hWhead, hLogits, S, D, V));
    /* Maskierte CE: Text-Zeilen [1..T] extrahieren, CE nur darueber. */
    G(moo_ki_gpu_copy_res(hLogits, hLogitsT, TV, V, 0));
    G(moo_ki_gpu_ce_fwd_res(hLogitsT, hTgtT, T, V, loss));
    return true;
}

static bool gpu_backward(void) {
    G(moo_ki_gpu_ce_bw_res(hLogitsT, hTgtT, hGradT, T, V, 1.0f / (float)T));
    /* dlog[S,V]: Zeile 0 = 0 (Null-Zeile), Zeilen 1..T = Text-Gradient. */
    G(moo_ki_gpu_copy_res(hZeroRow, hDlog, V, 0, 0));
    G(moo_ki_gpu_copy_res(hGradT, hDlog, TV, 0, V));
    G(moo_ki_gpu_matmul_bw_res(hXf, hWhead, hDlog, dXf, hgWhead, S, D, V));
    G(moo_ki_gpu_norm_bw_res(MOO_KI_NORM_RMS, hStream1, dXf, dX, S, D, (float)EPS));
    /* FFN-Backward. */
    G(moo_ki_gpu_matmul_bw_res(hH, hW2, dX, dH, hgW2, S, F, D));
    G(moo_ki_gpu_ew_res(2, dH, hU, dsg, SF));
    G(moo_ki_gpu_ew_res(2, dH, hSg, ddu, SF));
    G(moo_ki_gpu_ew_res(2, hSg, hS, dg2, SF));
    G(moo_ki_gpu_ew_res(1, hSg, dg2, dg2, SF));
    G(moo_ki_gpu_ew_res(0, hS, dg2, dg2, SF));
    G(moo_ki_gpu_ew_res(2, dsg, dg2, dg2, SF));
    G(moo_ki_gpu_matmul_bw_res(hXn2, hW1, dg2, dxn2a, hgW1, S, D, F));
    G(moo_ki_gpu_matmul_bw_res(hXn2, hW3, ddu, dxn2b, hgW3, S, D, F));
    G(moo_ki_gpu_grad_accum_res(dxn2a, dxn2b, SD));
    G(moo_ki_gpu_norm_bw_res(MOO_KI_NORM_RMS, hMid, dxn2a, dxb, S, D, (float)EPS));
    G(moo_ki_gpu_grad_accum_res(dX, dxb, SD));
    /* Attention-Backward. */
    G(moo_ki_gpu_matmul_bw_res(hCtx, hWo, dX, dCtx, hgWo, S, D, D));
    G(moo_ki_gpu_matmul_bw_res(hAtt, hVv, dCtx, dAtt, dVv2, S, S, D));
    G(moo_ki_gpu_softmax_bw_res(MOO_KI_SM_SOFTMAX, hAtt, dAtt, dScm, S, S));
    G(moo_ki_gpu_unary_res(MOO_KI_U_MULS, dScm, dScm, SS, (float)SCALE));
    G(moo_ki_gpu_transpose_res(hK, sKt, S, D));
    G(moo_ki_gpu_matmul_bw_res(hQ, sKt, dScm, dQ, dKt, S, D, S));
    G(moo_ki_gpu_transpose_res(dKt, dK, D, S));
    G(moo_ki_gpu_matmul_bw_res(hXn, hWq, dQ, dxnq, hgWq, S, D, D));
    G(moo_ki_gpu_matmul_bw_res(hXn, hWk, dK, dxnk, hgWk, S, D, D));
    G(moo_ki_gpu_matmul_bw_res(hXn, hWv, dVv2, dxnv, hgWv, S, D, D));
    G(moo_ki_gpu_grad_accum_res(dxnq, dxnk, SD));
    G(moo_ki_gpu_grad_accum_res(dxnq, dxnv, SD));
    G(moo_ki_gpu_norm_bw_res(MOO_KI_NORM_RMS, hStream0, dxnq, dxb, S, D, (float)EPS));
    G(moo_ki_gpu_grad_accum_res(dX, dxb, SD));
    /* Concat-Split: Zeile 0 -> Projektion, Zeilen 1..T -> Text-Embeddings. */
    G(moo_ki_gpu_copy_res(dX, dEProj, D, 0, 0));
    G(moo_ki_gpu_copy_res(dX, dTemb, TD, D, 0));
    G(moo_ki_gpu_matmul_bw_res(hEInj, hP, dEProj, dEInj, hgP, 1, ENC, D));  /* gP; dEInj verworfen */
    G(moo_ki_gpu_scatter_add_res(dTemb, hIdx, hgEmb, T, D, V));
    return true;
}

static bool gpu_sgd(void) {
    for (int i = 0; i < ngp; i++)
        G(moo_ki_gpu_opt_sgd_res(GP[i].p, GP[i].m, GP[i].g, GP[i].n, (float)LR, (float)MU));
    return true;
}
#undef G
#endif /* !V3C_CPU_ONLY */

int main(void) {
    SCALE = 1.0 / sqrt((double)D);
    g_acap = (size_t)4 * 1024 * 1024;   /* 4M doubles (32 MB) — Dims bleiben winzig */
    g_arena = (double*)malloc(g_acap * sizeof(double));
    if (!g_arena) { fprintf(stderr, "arena malloc fail\n"); return 2; }

    printf("== KI-MULTI-V3c: residentes multimodales Training-PoC ==\n");
    printf("   Praefix-Injection(concat) + Projektion(%d->%d) + maskierte CE\n", ENC, D);
    printf("   Dims: ENC=%d D=%d T=%d S=%d V=%d F=%d STEPS=%d\n", ENC, D, T, S, V, F, STEPS);

    cpu_init_params();

    printf("\n[1] FD-Gradcheck der CPU-Referenz (double):\n");
    size_t apos_save = g_apos;
    (void)cpu_forward();
    cpu_backward();
    bool fd_ok = true;
    fd_ok &= fd_check_param(0, V3C_FD_SAMPLES);   /* P — Projektion (der Kernbeweis) */
    fd_ok &= fd_check_param(1, V3C_FD_SAMPLES);   /* Emb */
    fd_ok &= fd_check_param(2, V3C_FD_SAMPLES);   /* Wq */
    fd_ok &= fd_check_param(5, V3C_FD_SAMPLES);   /* Wo */
    fd_ok &= fd_check_param(6, V3C_FD_SAMPLES);   /* W1 */
    fd_ok &= fd_check_param(8, V3C_FD_SAMPLES);   /* W2 */
    fd_ok &= fd_check_param(9, V3C_FD_SAMPLES);   /* Whead */
    CHECK(fd_ok, "FD-Gradcheck: analytische == numerische Grads (rel < 1e-3), inkl. Projektion P");
    g_apos = apos_save;

    /* Init-Snapshot: die CPU-Trainingsschleife mutiert die Param-Arrays; der
     * GPU-Lauf muss aber von DENSELBEN Init-Params starten. */
    for (int i = 0; i < np; i++) { g_snap[i] = aa(CP[i].n); memcpy(g_snap[i], CP[i].p, (size_t)CP[i].n * sizeof(double)); }

    printf("\n[2] CPU-Referenz-Trainingskurve (SGD, mu=%.2f):\n", MU);
    double cpu_loss[STEPS];
    clock_t c0 = clock();
    for (int t = 1; t <= STEPS; t++) {
        size_t sv = g_apos;
        cpu_loss[t - 1] = cpu_forward();
        cpu_backward();
        cpu_sgd();
        g_apos = sv;
        printf("   CPU Schritt %d: loss=%.6f\n", t, cpu_loss[t - 1]);
    }
    double cpu_sec = (double)(clock() - c0) / CLOCKS_PER_SEC;

#ifdef V3C_CPU_ONLY
    printf("\nV3C_CPU_ONLY: nur CPU-Mathe validiert (kein GPU-Pfad).\n");
    printf("%s (fehler=%d)\n", fehler == 0 ? "CPU-MATHE OK" : "CPU-MATHE FEHLER", fehler);
    free(g_arena);
    return fehler == 0 ? 0 : 1;
#else
    void* probe0 = moo_ki_gpu_buf_belegen(64);
    if (!probe0) {
        printf("\nSKIP: keine GPU-Residenz (kein Vulkan/keine GPU) — V3c nicht bewiesen\n");
        free(g_arena);
        return 0;
    }
    moo_ki_gpu_buf_freigeben(probe0);

    { int zmax = MAX2(MAX2(MAX2(MAX2(DD, DF), FD), MAX2(VD, ED)), SS);
      g_upf = (float*)malloc((size_t)zmax * sizeof(float));
      if (!g_upf) { fprintf(stderr, "upf malloc fail\n"); return 2; } }

    CHECK(gpu_alloc(), "GPU-Buffers belegt");

    /* Params auf die Init zuruecksetzen (CPU-Training [2] hat sie mutiert). */
    for (int i = 0; i < np; i++) memcpy(CP[i].p, g_snap[i], (size_t)CP[i].n * sizeof(double));

    /* Konstanten EINMAL hochladen (e_inj frozen, vor beiden Laeufen). */
    CHECK(gpu_upload_consts(), "Konstanten-Upload (e_inj frozen, EINMAL vor dem Loop)");

    if (getenv("V3C_DIFF")) {
        gpu_upload_params();
        double gl = 0.0; gpu_forward(&gl);
        double cl = cpu_forward();
        printf("[DIFF] loss   gpu=%.6f cpu=%.6f\n", gl, cl);
        printf("[DIFF] eproj  =%.3e\n", maxdiff(hEProj, cEProj, D));
        printf("[DIFF] cat    =%.3e\n", maxdiff(hCat, cCat, SD));
        printf("[DIFF] xn     =%.3e\n", maxdiff(hXn, cXn, SD));
        printf("[DIFF] att    =%.3e\n", maxdiff(hAtt, cAtt, SS));
        printf("[DIFF] mid    =%.3e\n", maxdiff(hMid, cMid, SD));
        printf("[DIFF] stream1=%.3e\n", maxdiff(hStream1, cStream1, SD));
        printf("[DIFF] logits =%.3e\n", maxdiff(hLogits, cLogits, SV));
    }

    double gpu_loss[2][STEPS];
    double gpu_sec = 0.0;
    uint64_t submits_step1 = 0;
    MooKiGpuTelemetrie tel_loop; memset(&tel_loop, 0, sizeof tel_loop);

    for (int run = 0; run < 2; run++) {
        CHECK(gpu_upload_params(), run == 0 ? "Setup-Upload Params+Momentum Lauf A" : "Setup-Upload Lauf B");
        moo_ki_gpu_telemetrie_reset();
        clock_t g0 = clock();
        bool ok = true;
        for (int t = 1; t <= STEPS; t++) {
            double loss = 0.0;
            MooKiGpuTelemetrie a; moo_ki_gpu_telemetrie(&a);
            ok = ok && gpu_forward(&loss) && gpu_backward() && gpu_sgd();
            MooKiGpuTelemetrie b; moo_ki_gpu_telemetrie(&b);
            gpu_loss[run][t - 1] = loss;
            if (run == 0 && t == 1) submits_step1 = b.submits - a.submits;
            if (run == 0) printf("   GPU Schritt %d: loss=%.6f (submits=%llu)\n",
                                 t, loss, (unsigned long long)(b.submits - a.submits));
        }
        gpu_sec = (double)(clock() - g0) / CLOCKS_PER_SEC;
        CHECK(ok, run == 0 ? "GPU-Trainingsloop Lauf A ausgefuehrt" : "GPU-Trainingsloop Lauf B ausgefuehrt");
        if (run == 0) moo_ki_gpu_telemetrie(&tel_loop);
    }

    printf("\n[3] GPU vs CPU Loss-Kurve:\n");
    bool kurve_ok = true;
    for (int t = 0; t < STEPS; t++) {
        double rel = fabs(gpu_loss[0][t] - cpu_loss[t]) / (fabs(cpu_loss[t]) + 1e-6);
        printf("   Schritt %d: gpu=%.6f cpu=%.6f rel=%.3e\n", t + 1, gpu_loss[0][t], cpu_loss[t], rel);
        if (rel > 2e-3) kurve_ok = false;
    }
    CHECK(kurve_ok, "GPU-Loss == CPU-Referenz je Schritt (rel < 2e-3)");

    bool determ = true;
    for (int t = 0; t < STEPS; t++) if (gpu_loss[0][t] != gpu_loss[1][t]) determ = false;
    CHECK(determ, "Determinismus: zwei GPU-Laeufe bit-identisch");

    printf("\n[5] Residenz-Telemetrie (Loop, nach Setup):\n");
    printf("   submits=%llu uploads=%llu downloads=%llu cpu_fallbacks=%llu\n",
           (unsigned long long)tel_loop.submits, (unsigned long long)tel_loop.uploads,
           (unsigned long long)tel_loop.downloads, (unsigned long long)tel_loop.cpu_fallbacks);
    CHECK(tel_loop.cpu_fallbacks == 0, "cpu_fallbacks == 0 (kein stiller CPU-Fallback)");
    CHECK(tel_loop.uploads == 0, "uploads == 0 im Loop (e_inj/Params/Aktivierungen resident)");
    CHECK(tel_loop.downloads == (uint64_t)STEPS, "downloads == STEPS (nur CE-Loss raus)");
    CHECK(submits_step1 > 0 && tel_loop.submits == submits_step1 * (uint64_t)STEPS,
          "submits == konst. Op-Positivliste je Schritt (Pfad lief, deterministisch)");

    float* outw = (float*)malloc((size_t)ED * sizeof(float));
    bool dl = moo_ki_gpu_download(hP, outw, (int64_t)ED * 4);
    bool fin = dl; for (int i = 0; i < ED && fin; i++) if (!isfinite(outw[i])) fin = false;
    CHECK(fin, "Projektion P nach Training finit (Download-Randbereich)");
    free(outw);

    printf("\n[6] Protokoll:\n");
    printf("   submits/Schritt = %llu   (matmul/matmul_bw/softmax/norm/ew/unary/gather/scatter/copy/grad_accum/sgd)\n",
           (unsigned long long)submits_step1);
    printf("   submits total   = %llu ueber %d Schritte\n", (unsigned long long)tel_loop.submits, STEPS);
    printf("   Wallclock: CPU(double)=%.4fs  GPU(float)=%.4fs\n", cpu_sec, gpu_sec);

    gb_free_all();
    free(g_upf);
    free(g_arena);
    printf("\n%s — KI-MULTI-V3c %s (fehler=%d)\n",
           fehler == 0 ? "ALLE PASS" : "FEHLER",
           fehler == 0 ? "GRUEN (residente Praefix-Injection + maskierte CE + Projektions-Gradient == CPU, deterministisch)" : "ROT",
           fehler);
    return fehler == 0 ? 0 : 1;
#endif
}
