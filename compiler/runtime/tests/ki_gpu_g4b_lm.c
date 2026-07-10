/**
 * ki_gpu_g4b_lm.c — KIP-G4b: GPU-residentes Trainings-PoC mit ECHTER Multi-Head/
 * GQA-Attention + RoPE (loest zwei der drei G4-PoC-Vereinfachungen auf; g4 bleibt
 * als Single-Head/additive-Pos-Regressions-Anker unberuehrt).
 *
 * Architektur (M-A-nah): Embedding-Gather -> L Layer [RMSNorm -> kausale
 * Grouped-Query-Attention mit RoPE auf Q,K -> Residual -> RMSNorm -> SwiGLU-FFN
 * -> Residual] -> RMSNorm -> Head -> fused Cross-Entropy. Komplett GPU-RESIDENT
 * (Adam). Transfers nur: Setup rein, Loss raus.
 *
 * Aufgeloeste Vereinfachungen ggü. G4:
 *   - Single-Head  -> H Query-Koepfe, HKV KV-Koepfe (GQA, rep=H/HKV) via strided
 *     head_slice; SCALE = 1/sqrt(head_dim).
 *   - additive Pos -> interleaved RoPE auf Q und K (rope_res), Position = Zeile.
 * Weiter PoC: affine-freie RMSNorm (norm_res-Kern direkt), basis=10000 fix.
 *
 * GQA-Backward: die rep Query-Koepfe einer KV-Gruppe akkumulieren dK/dV im
 * KOPF-Raum (erster Kopf der Gruppe schreibt via copy, weitere via grad_accum),
 * dann pro Gruppe genau EIN insert in den vollen dK/dV-Puffer -> head_slice-
 * insert bleibt reiner disjunkter Write.
 *
 * Verifikation wie G4: (1) FD-Gradcheck der double-CPU-Referenz; (2) GPU-Loss
 * (float) == CPU-Referenz (double) je Schritt; (3) Determinismus (2 Laeufe bit-
 * identisch); (4) Residenz-Telemetrie (cpu_fallbacks=0, uploads=0 im Loop,
 * downloads=STEPS). Ohne Vulkan/GPU: SKIP (Exit 0).
 *
 * Dims via -DG4B_* ueberschreibbar. Constraints: D%H==0, head_dim gerade,
 * H%HKV==0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "moo_ki_gpu_api.h"

#ifndef G4B_D
#define G4B_D 32
#endif
#ifndef G4B_T
#define G4B_T 16
#endif
#ifndef G4B_V
#define G4B_V 24
#endif
#ifndef G4B_F
#define G4B_F 48
#endif
#ifndef G4B_L
#define G4B_L 2
#endif
#ifndef G4B_H
#define G4B_H 4
#endif
#ifndef G4B_HKV
#define G4B_HKV 2
#endif
#ifndef G4B_STEPS
#define G4B_STEPS 4
#endif
#ifndef G4B_FD_SAMPLES
#define G4B_FD_SAMPLES 6
#endif

enum {
    D = G4B_D, T = G4B_T, V = G4B_V, F = G4B_F, L = G4B_L,
    H = G4B_H, HKV = G4B_HKV, STEPS = G4B_STEPS,
    DH = D / H, DKV = HKV * (D / H), REP = H / HKV
};
_Static_assert(D % H == 0, "D muss durch H teilbar sein");
_Static_assert((D / H) % 2 == 0, "head_dim muss gerade sein (RoPE)");
_Static_assert(H % HKV == 0, "H muss durch HKV teilbar sein (GQA)");

enum {
    VD = V * D, DV = D * V, TD = T * D, TT = T * T, TF = T * F, TV = T * V,
    DD = D * D, DKD = D * DKV, DF = D * F, FD = F * D, TDKV = T * DKV, TDH = T * DH
};

static const double EPS = 1e-5, LR = 0.01, B1 = 0.9, B2 = 0.999, EPSA = 1e-8;
static double SCALE;   /* 1/sqrt(head_dim), in main gesetzt */
static int fehler = 0;
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#define CHECK(bed, msg) do { if (bed) { printf("  OK   %s\n", msg); } \
    else { printf("  FEHLER %s\n", msg); fehler++; } } while (0)

/* ===================== Arena (double) ===================== */
static double* g_arena = NULL; static size_t g_apos = 0, g_acap = 0;
static double* aa(int n) {
    if (g_apos + (size_t)n > g_acap) { fprintf(stderr, "arena OOM\n"); exit(2); }
    double* p = g_arena + g_apos; g_apos += (size_t)n;
    for (int i = 0; i < n; i++) p[i] = 0.0;
    return p;
}

/* ===================== CPU-Mathe (double, Referenz) ===================== */
static void mm(const double* A, const double* B, double* C, int m, int k, int n) {
    for (int i = 0; i < m; i++) for (int j = 0; j < n; j++) {
        double s = 0.0; for (int p = 0; p < k; p++) s += A[i * k + p] * B[p * n + j];
        C[i * n + j] = s;
    }
}
static void mm_bw(const double* A, const double* B, const double* G,
                  double* dA, double* dB, int m, int k, int n) {
    for (int i = 0; i < m; i++) for (int p = 0; p < k; p++) {
        double s = 0.0; for (int j = 0; j < n; j++) s += G[i * n + j] * B[p * n + j];
        dA[i * k + p] = s;
    }
    for (int p = 0; p < k; p++) for (int j = 0; j < n; j++) {
        double s = 0.0; for (int i = 0; i < m; i++) s += A[i * k + p] * G[i * n + j];
        dB[p * n + j] = s;
    }
}
static void trn(const double* A, double* O, int r, int c) {
    for (int i = 0; i < r; i++) for (int j = 0; j < c; j++) O[j * r + i] = A[i * c + j];
}
static void rms(const double* x, double* y, int r, int c, double eps) {
    for (int i = 0; i < r; i++) {
        double ss = 0.0; for (int j = 0; j < c; j++) ss += x[i * c + j] * x[i * c + j];
        double inv = 1.0 / sqrt(ss / c + eps);
        for (int j = 0; j < c; j++) y[i * c + j] = x[i * c + j] * inv;
    }
}
static void rms_bw(const double* x, const double* g, double* dx, int r, int c, double eps) {
    for (int i = 0; i < r; i++) {
        double ss = 0.0; for (int j = 0; j < c; j++) ss += x[i * c + j] * x[i * c + j];
        double var = ss / c, inv = 1.0 / sqrt(var + eps);
        double dot = 0.0; for (int j = 0; j < c; j++) dot += g[i * c + j] * x[i * c + j];
        for (int j = 0; j < c; j++)
            dx[i * c + j] = inv * (g[i * c + j] - x[i * c + j] * dot * inv * inv / c);
    }
}
static void smax(const double* a, double* o, int r, int c) {
    for (int i = 0; i < r; i++) {
        double mx = a[i * c]; for (int j = 1; j < c; j++) if (a[i * c + j] > mx) mx = a[i * c + j];
        double sm = 0.0; for (int j = 0; j < c; j++) { double e = exp(a[i * c + j] - mx); o[i * c + j] = e; sm += e; }
        for (int j = 0; j < c; j++) o[i * c + j] /= sm;
    }
}
static void smax_bw(const double* y, const double* g, double* gin, int r, int c) {
    for (int i = 0; i < r; i++) {
        double dot = 0.0; for (int j = 0; j < c; j++) dot += g[i * c + j] * y[i * c + j];
        for (int j = 0; j < c; j++) gin[i * c + j] = y[i * c + j] * (g[i * c + j] - dot);
    }
}
static void sig(const double* a, double* o, int n) { for (int i = 0; i < n; i++) o[i] = 1.0 / (1.0 + exp(-a[i])); }
static void emul(const double* a, const double* b, double* o, int n) { for (int i = 0; i < n; i++) o[i] = a[i] * b[i]; }
static void eadd(const double* a, const double* b, double* o, int n) { for (int i = 0; i < n; i++) o[i] = a[i] + b[i]; }
static void esub(const double* a, const double* b, double* o, int n) { for (int i = 0; i < n; i++) o[i] = a[i] - b[i]; }
static void muls(const double* a, double* o, int n, double s) { for (int i = 0; i < n; i++) o[i] = a[i] * s; }
static void gath(const double* w, const double* idx, double* o, int rows, int dim, int vocab) {
    for (int i = 0; i < rows; i++) { int v = (int)idx[i]; if (v < 0) v = 0; if (v >= vocab) v = vocab - 1;
        for (int d = 0; d < dim; d++) o[i * dim + d] = w[v * dim + d]; }
}
static void scat(const double* g, const double* idx, double* gw, int rows, int dim, int vocab) {
    for (int i = 0; i < vocab * dim; i++) gw[i] = 0.0;
    for (int i = 0; i < rows; i++) { int v = (int)idx[i]; if (v < 0) v = 0; if (v >= vocab) v = vocab - 1;
        for (int d = 0; d < dim; d++) gw[v * dim + d] += g[i * dim + d]; }
}
static void ce_f(const double* logits, const double* tgt, int r, int c, double* loss) {
    double tot = 0.0;
    for (int i = 0; i < r; i++) {
        double mx = logits[i * c]; for (int j = 1; j < c; j++) if (logits[i * c + j] > mx) mx = logits[i * c + j];
        double sm = 0.0; for (int j = 0; j < c; j++) sm += exp(logits[i * c + j] - mx);
        double lse = mx + log(sm);
        for (int j = 0; j < c; j++) tot += -tgt[i * c + j] * (logits[i * c + j] - lse);
    }
    *loss = tot / r;
}
static void ce_b(const double* logits, const double* tgt, double* grad, int r, int c, double scale) {
    for (int i = 0; i < r; i++) {
        double mx = logits[i * c]; for (int j = 1; j < c; j++) if (logits[i * c + j] > mx) mx = logits[i * c + j];
        double sm = 0.0; for (int j = 0; j < c; j++) sm += exp(logits[i * c + j] - mx);
        for (int j = 0; j < c; j++) grad[i * c + j] = (exp(logits[i * c + j] - mx) / sm - tgt[i * c + j]) * scale;
    }
}
static void adam(double* p, const double* g, double* m, double* v, int n,
                 double lr, double b1, double b2, double eps, int t) {
    double bc1 = 1.0 - pow(b1, t), bc2 = 1.0 - pow(b2, t);
    for (int i = 0; i < n; i++) {
        m[i] = b1 * m[i] + (1 - b1) * g[i];
        v[i] = b2 * v[i] + (1 - b2) * g[i] * g[i];
        p[i] -= lr * (m[i] / bc1) / (sqrt(v[i] / bc2) + eps);
    }
}
/* interleaved RoPE (double-Referenz, bit-genau zu shader_ki/rope.comp) */
static void rope(const double* a, double* o, int rows, int dim, int hd, int p0, int fwd) {
    int ppr = dim / 2;
    for (int t = 0; t < rows; t++) for (int pr = 0; pr < ppr; pr++) {
        int col0 = 2 * pr, ihp = (col0 % hd) / 2;
        double pos = (double)(p0 + t), freq = pow(10000.0, -2.0 * (double)ihp / (double)hd);
        double ang = pos * freq, c = cos(ang), s = sin(ang);
        double x0 = a[t * dim + col0], x1 = a[t * dim + col0 + 1], r0, r1;
        if (fwd) { r0 = x0 * c - x1 * s; r1 = x1 * c + x0 * s; }
        else     { r0 = x0 * c + x1 * s; r1 = x1 * c - x0 * s; }
        o[t * dim + col0] = r0; o[t * dim + col0 + 1] = r1;
    }
}
static void ex_head(const double* a, double* o, int rows, int dim, int hd, int off) {
    for (int t = 0; t < rows; t++) for (int j = 0; j < hd; j++) o[t * hd + j] = a[t * dim + off + j];
}
static void ins_head(const double* head, double* o, int rows, int dim, int hd, int off) {
    for (int t = 0; t < rows; t++) for (int j = 0; j < hd; j++) o[t * dim + off + j] = head[t * hd + j];
}

/* ===================== Parameter-Registry (CPU) ===================== */
#define NP (L * 7 + 2)
typedef struct { double* p; double* g; double* m; double* v; int n; const char* name; } CpuParam;
static CpuParam CP[NP];
static double* g_snap[NP];
static int np = 0;
static void reg(double* p, double* g, int n, const char* name) {
    CP[np].p = p; CP[np].g = g; CP[np].m = aa(n); CP[np].v = aa(n);
    CP[np].n = n; CP[np].name = name; np++;
}

static double *cEmb, *cWhead, *cBias, *cIdx, *cTgt;
static double *cWq[L], *cWk[L], *cWv[L], *cWo[L], *cW1[L], *cW3[L], *cW2[L];
static double *gcEmb, *gcWhead;
static double *gcWq[L], *gcWk[L], *gcWv[L], *gcWo[L], *gcW1[L], *gcW3[L], *gcW2[L];
static double *cStream[L + 1], *cMid[L], *cXn[L], *cQ[L], *cK[L], *cVv[L],
              *cAtt[L][H], *cCtx[L], *cXn2[L], *cG[L], *cU[L], *cS[L], *cSg[L], *cH[L];
static double *cXf, *cLogits;

static void cpu_init_params(void) {
    cEmb = aa(VD); for (int i = 0; i < VD; i++) cEmb[i] = 0.30 * sin(0.07 * i + 0.3);
    cWhead = aa(DV); for (int i = 0; i < DV; i++) cWhead[i] = 0.25 * cos(0.05 * i + 0.1);
    for (int l = 0; l < L; l++) {
        cWq[l] = aa(DD); cWk[l] = aa(DKD); cWv[l] = aa(DKD); cWo[l] = aa(DD);
        for (int i = 0; i < DD; i++) {
            cWq[l][i] = 0.35 * sin(0.031 * i + 1.0 * l + 0.1);
            cWo[l][i] = 0.30 * cos(0.033 * i + 1.0 * l + 0.4);
        }
        for (int i = 0; i < DKD; i++) {
            cWk[l][i] = 0.35 * cos(0.029 * i + 1.0 * l + 0.2);
            cWv[l][i] = 0.30 * sin(0.027 * i + 1.0 * l + 0.3);
        }
        cW1[l] = aa(DF); cW3[l] = aa(DF); cW2[l] = aa(FD);
        for (int i = 0; i < DF; i++) {
            cW1[l][i] = 0.30 * sin(0.017 * i + 2.0 * l + 0.5);
            cW3[l][i] = 0.30 * cos(0.019 * i + 2.0 * l + 0.6);
        }
        for (int i = 0; i < FD; i++) cW2[l][i] = 0.25 * sin(0.021 * i + 2.0 * l + 0.7);
    }
    gcEmb = aa(VD); reg(cEmb, gcEmb, VD, "Emb");
    gcWhead = aa(DV); reg(cWhead, gcWhead, DV, "Whead");
    for (int l = 0; l < L; l++) {
        gcWq[l] = aa(DD);  reg(cWq[l], gcWq[l], DD,  "Wq");
        gcWk[l] = aa(DKD); reg(cWk[l], gcWk[l], DKD, "Wk");
        gcWv[l] = aa(DKD); reg(cWv[l], gcWv[l], DKD, "Wv");
        gcWo[l] = aa(DD);  reg(cWo[l], gcWo[l], DD,  "Wo");
        gcW1[l] = aa(DF);  reg(cW1[l], gcW1[l], DF,  "W1");
        gcW3[l] = aa(DF);  reg(cW3[l], gcW3[l], DF,  "W3");
        gcW2[l] = aa(FD);  reg(cW2[l], gcW2[l], FD,  "W2");
    }
    for (int l = 0; l <= L; l++) cStream[l] = aa(TD);
    for (int l = 0; l < L; l++) {
        cMid[l] = aa(TD); cXn[l] = aa(TD); cQ[l] = aa(TD); cK[l] = aa(TDKV); cVv[l] = aa(TDKV);
        for (int h = 0; h < H; h++) cAtt[l][h] = aa(TT);
        cCtx[l] = aa(TD); cXn2[l] = aa(TD);
        cG[l] = aa(TF); cU[l] = aa(TF); cS[l] = aa(TF); cSg[l] = aa(TF); cH[l] = aa(TF);
    }
    cXf = aa(TD); cLogits = aa(TV);
    cIdx = aa(T); for (int i = 0; i < T; i++) cIdx[i] = (double)((i * 7 + 3) % V);
    cTgt = aa(TV);
    for (int i = 0; i < T; i++) cTgt[i * V + ((i * 5 + 1) % V)] = 1.0;
    cBias = aa(TT);
    for (int t = 0; t < T; t++) for (int j = 0; j < T; j++) cBias[t * T + j] = (j <= t) ? 0.0 : -1e9;
}

static double cpu_forward(void) {
    double* qh = aa(TDH); double* kh = aa(TDH); double* vh = aa(TDH); double* kt = aa(DH * T);
    double* sc = aa(TT); double* ctxh = aa(TDH); double* tmpTD = aa(TD);
    gath(cEmb, cIdx, cStream[0], T, D, V);
    for (int l = 0; l < L; l++) {
        rms(cStream[l], cXn[l], T, D, EPS);
        mm(cXn[l], cWq[l], cQ[l], T, D, D);
        mm(cXn[l], cWk[l], cK[l], T, D, DKV);
        mm(cXn[l], cWv[l], cVv[l], T, D, DKV);
        rope(cQ[l], cQ[l], T, D, DH, 0, 1);
        rope(cK[l], cK[l], T, DKV, DH, 0, 1);
        for (int h = 0; h < H; h++) {
            int gseg = h / REP;
            ex_head(cQ[l], qh, T, D, DH, h * DH);
            ex_head(cK[l], kh, T, DKV, DH, gseg * DH);
            ex_head(cVv[l], vh, T, DKV, DH, gseg * DH);
            trn(kh, kt, T, DH);
            mm(qh, kt, sc, T, DH, T);
            muls(sc, sc, TT, SCALE);
            eadd(sc, cBias, sc, TT);
            smax(sc, cAtt[l][h], T, T);
            mm(cAtt[l][h], vh, ctxh, T, T, DH);
            ins_head(ctxh, cCtx[l], T, D, DH, h * DH);
        }
        mm(cCtx[l], cWo[l], tmpTD, T, D, D);
        eadd(cStream[l], tmpTD, cMid[l], TD);
        rms(cMid[l], cXn2[l], T, D, EPS);
        mm(cXn2[l], cW1[l], cG[l], T, D, F);
        mm(cXn2[l], cW3[l], cU[l], T, D, F);
        sig(cG[l], cS[l], TF);
        emul(cG[l], cS[l], cSg[l], TF);
        emul(cSg[l], cU[l], cH[l], TF);
        mm(cH[l], cW2[l], tmpTD, T, F, D);
        eadd(cMid[l], tmpTD, cStream[l + 1], TD);
    }
    rms(cStream[L], cXf, T, D, EPS);
    mm(cXf, cWhead, cLogits, T, D, V);
    double loss; ce_f(cLogits, cTgt, T, V, &loss);
    return loss;
}

static void cpu_backward(void) {
    double* dlog = aa(TV); double* dX = aa(TD); double* dXf = aa(TD);
    double* dH = aa(TF); double* dsg = aa(TF); double* du = aa(TF); double* dg = aa(TF); double* tf = aa(TF);
    double* dxn2a = aa(TD); double* dxn2b = aa(TD); double* dxb = aa(TD);
    double* dCtx = aa(TD); double* tmpTD = aa(TD);
    double* dQr = aa(TD); double* dKr = aa(TDKV); double* dVf = aa(TDKV);
    double* dQ = aa(TD); double* dK = aa(TDKV);
    double* dxnq = aa(TD); double* dxnk = aa(TD); double* dxnv = aa(TD);
    double* qh = aa(TDH); double* kh = aa(TDH); double* vh = aa(TDH); double* kt = aa(DH * T);
    double* dctxh = aa(TDH); double* dAtt = aa(TT); double* dScm = aa(TT);
    double* dqh = aa(TDH); double* dkth = aa(DH * T); double* dkh = aa(TDH); double* dvh = aa(TDH);
    double* dKg[HKV]; double* dVg[HKV];
    for (int g = 0; g < HKV; g++) { dKg[g] = aa(TDH); dVg[g] = aa(TDH); }

    ce_b(cLogits, cTgt, dlog, T, V, 1.0 / (double)T);
    mm_bw(cXf, cWhead, dlog, dXf, gcWhead, T, D, V);
    rms_bw(cStream[L], dXf, dX, T, D, EPS);
    for (int l = L - 1; l >= 0; l--) {
        /* FFN backward */
        mm_bw(cH[l], cW2[l], dX, dH, gcW2[l], T, F, D);
        emul(dH, cU[l], dsg, TF);
        emul(dH, cSg[l], du, TF);
        emul(cSg[l], cS[l], tf, TF);
        esub(cSg[l], tf, tf, TF);
        eadd(cS[l], tf, tf, TF);
        emul(dsg, tf, dg, TF);
        mm_bw(cXn2[l], cW1[l], dg, dxn2a, gcW1[l], T, D, F);
        mm_bw(cXn2[l], cW3[l], du, dxn2b, gcW3[l], T, D, F);
        eadd(dxn2a, dxn2b, dxn2a, TD);
        rms_bw(cMid[l], dxn2a, dxb, T, D, EPS);
        eadd(dX, dxb, dX, TD);                    /* dX = grad w.r.t. mid */
        /* Attention backward */
        mm_bw(cCtx[l], cWo[l], dX, dCtx, gcWo[l], T, D, D);
        for (int h = 0; h < H; h++) {
            int gseg = h / REP;
            ex_head(dCtx, dctxh, T, D, DH, h * DH);
            ex_head(cVv[l], vh, T, DKV, DH, gseg * DH);
            ex_head(cQ[l], qh, T, D, DH, h * DH);
            ex_head(cK[l], kh, T, DKV, DH, gseg * DH);
            mm_bw(cAtt[l][h], vh, dctxh, dAtt, dvh, T, T, DH);
            smax_bw(cAtt[l][h], dAtt, dScm, T, T);
            muls(dScm, dScm, TT, SCALE);
            trn(kh, kt, T, DH);
            mm_bw(qh, kt, dScm, dqh, dkth, T, DH, T);
            trn(dkth, dkh, DH, T);
            ins_head(dqh, dQr, T, D, DH, h * DH);
            if (h % REP == 0) { memcpy(dKg[gseg], dkh, sizeof(double) * TDH); memcpy(dVg[gseg], dvh, sizeof(double) * TDH); }
            else { eadd(dKg[gseg], dkh, dKg[gseg], TDH); eadd(dVg[gseg], dvh, dVg[gseg], TDH); }
        }
        for (int g = 0; g < HKV; g++) {
            ins_head(dKg[g], dKr, T, DKV, DH, g * DH);
            ins_head(dVg[g], dVf, T, DKV, DH, g * DH);
        }
        rope(dQr, dQ, T, D, DH, 0, 0);
        rope(dKr, dK, T, DKV, DH, 0, 0);
        mm_bw(cXn[l], cWq[l], dQ, dxnq, gcWq[l], T, D, D);
        mm_bw(cXn[l], cWk[l], dK, dxnk, gcWk[l], T, D, DKV);
        mm_bw(cXn[l], cWv[l], dVf, dxnv, gcWv[l], T, D, DKV);
        eadd(dxnq, dxnk, dxnq, TD);
        eadd(dxnq, dxnv, dxnq, TD);
        rms_bw(cStream[l], dxnq, dxb, T, D, EPS);
        eadd(dX, dxb, dX, TD);                    /* dX = grad w.r.t. stream[l] */
        (void)tmpTD;
    }
    scat(dX, cIdx, gcEmb, T, D, V);
}

static void cpu_adam(int t) {
    for (int i = 0; i < np; i++) adam(CP[i].p, CP[i].g, CP[i].m, CP[i].v, CP[i].n, LR, B1, B2, EPSA, t);
}

/* ===================== FD-Gradcheck (double Forward) ===================== */
static bool fd_check_param(int pi, int samples) {
    CpuParam* q = &CP[pi];
    double h = 1e-4, max_rel = 0.0;
    for (int s = 0; s < samples; s++) {
        int idx = (int)((s * 2654435761u) % (unsigned)q->n);
        double orig = q->p[idx]; size_t sv = g_apos;
        q->p[idx] = orig + h; double lp = cpu_forward();
        q->p[idx] = orig - h; double lm = cpu_forward();
        q->p[idx] = orig; g_apos = sv;
        double num = (lp - lm) / (2.0 * h), ana = q->g[idx];
        double rel = fabs(num - ana) / (fabs(num) + fabs(ana) + 1e-9);
        if (rel > max_rel) max_rel = rel;
    }
    printf("    FD %-6s max_rel=%.3e (%d samples)\n", q->name, max_rel, samples);
    return max_rel < 1e-3;
}

#ifndef G4B_CPU_ONLY
/* ===================== GPU-residenter Pfad (float) ===================== */
#define MAXH 2048
static void* g_handles[MAXH]; static int g_nh = 0;
static void* gb(int64_t floats) {
    void* h = moo_ki_gpu_buf_belegen(floats * 4);
    if (h && g_nh < MAXH) g_handles[g_nh++] = h;
    return h;
}
static void gb_free_all(void) { for (int i = 0; i < g_nh; i++) moo_ki_gpu_buf_freigeben(g_handles[i]); g_nh = 0; }

static float* g_upf = NULL;
static bool up(void* h, const double* src, int n) {
    for (int i = 0; i < n; i++) g_upf[i] = (float)src[i];
    return moo_ki_gpu_upload(h, g_upf, (int64_t)n * 4);
}

static void *hEmb, *hWhead, *hBias, *hIdx, *hTgt;
static void *hWq[L], *hWk[L], *hWv[L], *hWo[L], *hW1[L], *hW3[L], *hW2[L];
static void *hgEmb, *hgWhead, *hgWq[L], *hgWk[L], *hgWv[L], *hgWo[L], *hgW1[L], *hgW3[L], *hgW2[L];
static void *hmvEmb, *hmvWhead, *hmvWq[L], *hmvWk[L], *hmvWv[L], *hmvWo[L], *hmvW1[L], *hmvW3[L], *hmvW2[L];
static void *hStream[L + 1], *hMid[L], *hXn[L], *hQ[L], *hK[L], *hVv[L], *hAtt[L][H], *hCtx[L],
            *hXn2[L], *hG[L], *hU[L], *hS[L], *hSg[L], *hH[L], *hXf, *hLogits, *hDlog;
typedef struct { void* p; void* g; void* mv; int n; } GpuParam;
static GpuParam GP[NP]; static int ngp = 0;
static void greg(void* p, void* g, void* mv, int n) { GP[ngp].p = p; GP[ngp].g = g; GP[ngp].mv = mv; GP[ngp].n = n; ngp++; }

/* Attention-Scratch (pro Op frisch beschrieben, ueber Koepfe/Layer wiederverwendet) */
static void *sQh, *sKh, *sVh, *sKt, *sSc, *sCtxh, *sTD;
static void *dX, *dXf, *dHb, *dsg, *ddu, *dg2, *dxn2a, *dxn2b, *dxb, *dCtx, *sTF3;
static void *sDctxh, *sDAtt, *sDScm, *sDqh, *sDkth, *sDkh, *sDvh;
static void *dQr, *dKr, *dVf, *dQ, *dK, *dxnq, *dxnk, *dxnv;
static void *dKg[HKV], *dVg[HKV];

static bool gpu_alloc(void) {
    hEmb = gb(VD); hWhead = gb(DV); hBias = gb(TT); hIdx = gb(T); hTgt = gb(TV);
    hgEmb = gb(VD); hgWhead = gb(DV); hmvEmb = gb(2 * VD); hmvWhead = gb(2 * DV);
    for (int l = 0; l < L; l++) {
        hWq[l] = gb(DD); hWk[l] = gb(DKD); hWv[l] = gb(DKD); hWo[l] = gb(DD);
        hW1[l] = gb(DF); hW3[l] = gb(DF); hW2[l] = gb(FD);
        hgWq[l] = gb(DD); hgWk[l] = gb(DKD); hgWv[l] = gb(DKD); hgWo[l] = gb(DD);
        hgW1[l] = gb(DF); hgW3[l] = gb(DF); hgW2[l] = gb(FD);
        hmvWq[l] = gb(2 * DD); hmvWk[l] = gb(2 * DKD); hmvWv[l] = gb(2 * DKD); hmvWo[l] = gb(2 * DD);
        hmvW1[l] = gb(2 * DF); hmvW3[l] = gb(2 * DF); hmvW2[l] = gb(2 * FD);
        hStream[l] = gb(TD); hMid[l] = gb(TD); hXn[l] = gb(TD); hQ[l] = gb(TD); hK[l] = gb(TDKV);
        hVv[l] = gb(TDKV); hCtx[l] = gb(TD); hXn2[l] = gb(TD);
        for (int h = 0; h < H; h++) hAtt[l][h] = gb(TT);
        hG[l] = gb(TF); hU[l] = gb(TF); hS[l] = gb(TF); hSg[l] = gb(TF); hH[l] = gb(TF);
    }
    hStream[L] = gb(TD); hXf = gb(TD); hLogits = gb(TV); hDlog = gb(TV);
    sQh = gb(TDH); sKh = gb(TDH); sVh = gb(TDH); sKt = gb(DH * T); sSc = gb(TT); sCtxh = gb(TDH); sTD = gb(TD);
    dX = gb(TD); dXf = gb(TD); dHb = gb(TF); dsg = gb(TF); ddu = gb(TF); dg2 = gb(TF);
    dxn2a = gb(TD); dxn2b = gb(TD); dxb = gb(TD); dCtx = gb(TD); sTF3 = gb(TF);
    sDctxh = gb(TDH); sDAtt = gb(TT); sDScm = gb(TT); sDqh = gb(TDH); sDkth = gb(DH * T); sDkh = gb(TDH); sDvh = gb(TDH);
    dQr = gb(TD); dKr = gb(TDKV); dVf = gb(TDKV); dQ = gb(TD); dK = gb(TDKV);
    dxnq = gb(TD); dxnk = gb(TD); dxnv = gb(TD);
    for (int g = 0; g < HKV; g++) { dKg[g] = gb(TDH); dVg[g] = gb(TDH); }
    greg(hEmb, hgEmb, hmvEmb, VD); greg(hWhead, hgWhead, hmvWhead, DV);
    for (int l = 0; l < L; l++) {
        greg(hWq[l], hgWq[l], hmvWq[l], DD); greg(hWk[l], hgWk[l], hmvWk[l], DKD);
        greg(hWv[l], hgWv[l], hmvWv[l], DKD); greg(hWo[l], hgWo[l], hmvWo[l], DD);
        greg(hW1[l], hgW1[l], hmvW1[l], DF); greg(hW3[l], hgW3[l], hmvW3[l], DF);
        greg(hW2[l], hgW2[l], hmvW2[l], FD);
    }
    for (int i = 0; i < g_nh; i++) if (!g_handles[i]) return false;
    return true;
}

static bool gpu_upload_params(void) {
    int zmax = 2 * MAX2(MAX2(MAX2(DD, DKD), MAX2(DF, FD)), MAX2(VD, DV));
    double* z = aa(zmax);
    bool ok = up(hEmb, cEmb, VD) && up(hWhead, cWhead, DV)
           && up(hBias, cBias, TT) && up(hIdx, cIdx, T) && up(hTgt, cTgt, TV)
           && up(hmvEmb, z, 2 * VD) && up(hmvWhead, z, 2 * DV);
    for (int l = 0; l < L && ok; l++) {
        ok = up(hWq[l], cWq[l], DD) && up(hWk[l], cWk[l], DKD) && up(hWv[l], cWv[l], DKD)
          && up(hWo[l], cWo[l], DD) && up(hW1[l], cW1[l], DF) && up(hW3[l], cW3[l], DF)
          && up(hW2[l], cW2[l], FD)
          && up(hmvWq[l], z, 2 * DD) && up(hmvWk[l], z, 2 * DKD) && up(hmvWv[l], z, 2 * DKD)
          && up(hmvWo[l], z, 2 * DD) && up(hmvW1[l], z, 2 * DF) && up(hmvW3[l], z, 2 * DF)
          && up(hmvW2[l], z, 2 * FD);
    }
    return ok;
}

#define G(e) do { if (!(e)) return false; } while (0)
static bool gpu_forward(double* loss) {
    G(moo_ki_gpu_gather_res(hEmb, hIdx, hStream[0], T, D, V));
    for (int l = 0; l < L; l++) {
        G(moo_ki_gpu_norm_res(MOO_KI_NORM_RMS, hStream[l], hXn[l], T, D, (float)EPS));
        G(moo_ki_gpu_matmul_res(hXn[l], hWq[l], hQ[l], T, D, D));
        G(moo_ki_gpu_matmul_res(hXn[l], hWk[l], hK[l], T, D, DKV));
        G(moo_ki_gpu_matmul_res(hXn[l], hWv[l], hVv[l], T, D, DKV));
        G(moo_ki_gpu_rope_res(hQ[l], hQ[l], T, D, DH, 0, 1));
        G(moo_ki_gpu_rope_res(hK[l], hK[l], T, DKV, DH, 0, 1));
        for (int h = 0; h < H; h++) {
            int gseg = h / REP;
            G(moo_ki_gpu_head_slice_res(hQ[l], sQh, T, D, DH, h * DH, 1));
            G(moo_ki_gpu_head_slice_res(hK[l], sKh, T, DKV, DH, gseg * DH, 1));
            G(moo_ki_gpu_head_slice_res(hVv[l], sVh, T, DKV, DH, gseg * DH, 1));
            G(moo_ki_gpu_transpose_res(sKh, sKt, T, DH));
            G(moo_ki_gpu_matmul_res(sQh, sKt, sSc, T, DH, T));
            G(moo_ki_gpu_unary_res(MOO_KI_U_MULS, sSc, sSc, TT, (float)SCALE));
            G(moo_ki_gpu_ew_res(0, sSc, hBias, sSc, TT));
            G(moo_ki_gpu_softmax_res(MOO_KI_SM_SOFTMAX, sSc, hAtt[l][h], T, T));
            G(moo_ki_gpu_matmul_res(hAtt[l][h], sVh, sCtxh, T, T, DH));
            G(moo_ki_gpu_head_slice_res(sCtxh, hCtx[l], T, D, DH, h * DH, 0));
        }
        G(moo_ki_gpu_matmul_res(hCtx[l], hWo[l], sTD, T, D, D));
        G(moo_ki_gpu_ew_res(0, hStream[l], sTD, hMid[l], TD));
        G(moo_ki_gpu_norm_res(MOO_KI_NORM_RMS, hMid[l], hXn2[l], T, D, (float)EPS));
        G(moo_ki_gpu_matmul_res(hXn2[l], hW1[l], hG[l], T, D, F));
        G(moo_ki_gpu_matmul_res(hXn2[l], hW3[l], hU[l], T, D, F));
        G(moo_ki_gpu_unary_res(MOO_KI_U_SIGMOID, hG[l], hS[l], TF, 0.0f));
        G(moo_ki_gpu_ew_res(2, hG[l], hS[l], hSg[l], TF));
        G(moo_ki_gpu_ew_res(2, hSg[l], hU[l], hH[l], TF));
        G(moo_ki_gpu_matmul_res(hH[l], hW2[l], sTD, T, F, D));
        G(moo_ki_gpu_ew_res(0, hMid[l], sTD, hStream[l + 1], TD));
    }
    G(moo_ki_gpu_norm_res(MOO_KI_NORM_RMS, hStream[L], hXf, T, D, (float)EPS));
    G(moo_ki_gpu_matmul_res(hXf, hWhead, hLogits, T, D, V));
    G(moo_ki_gpu_ce_fwd_res(hLogits, hTgt, T, V, loss));
    return true;
}

static bool gpu_backward(void) {
    G(moo_ki_gpu_ce_bw_res(hLogits, hTgt, hDlog, T, V, 1.0f / (float)T));
    G(moo_ki_gpu_matmul_bw_res(hXf, hWhead, hDlog, dXf, hgWhead, T, D, V));
    G(moo_ki_gpu_norm_bw_res(MOO_KI_NORM_RMS, hStream[L], dXf, dX, T, D, (float)EPS));
    for (int l = L - 1; l >= 0; l--) {
        G(moo_ki_gpu_matmul_bw_res(hH[l], hW2[l], dX, dHb, hgW2[l], T, F, D));
        G(moo_ki_gpu_ew_res(2, dHb, hU[l], dsg, TF));
        G(moo_ki_gpu_ew_res(2, dHb, hSg[l], ddu, TF));
        G(moo_ki_gpu_ew_res(2, hSg[l], hS[l], sTF3, TF));
        G(moo_ki_gpu_ew_res(1, hSg[l], sTF3, sTF3, TF));
        G(moo_ki_gpu_ew_res(0, hS[l], sTF3, sTF3, TF));
        G(moo_ki_gpu_ew_res(2, dsg, sTF3, dg2, TF));
        G(moo_ki_gpu_matmul_bw_res(hXn2[l], hW1[l], dg2, dxn2a, hgW1[l], T, D, F));
        G(moo_ki_gpu_matmul_bw_res(hXn2[l], hW3[l], ddu, dxn2b, hgW3[l], T, D, F));
        G(moo_ki_gpu_grad_accum_res(dxn2a, dxn2b, TD));
        G(moo_ki_gpu_norm_bw_res(MOO_KI_NORM_RMS, hMid[l], dxn2a, dxb, T, D, (float)EPS));
        G(moo_ki_gpu_grad_accum_res(dX, dxb, TD));
        G(moo_ki_gpu_matmul_bw_res(hCtx[l], hWo[l], dX, dCtx, hgWo[l], T, D, D));
        for (int h = 0; h < H; h++) {
            int gseg = h / REP;
            G(moo_ki_gpu_head_slice_res(dCtx, sDctxh, T, D, DH, h * DH, 1));
            G(moo_ki_gpu_head_slice_res(hVv[l], sVh, T, DKV, DH, gseg * DH, 1));
            G(moo_ki_gpu_head_slice_res(hQ[l], sQh, T, D, DH, h * DH, 1));
            G(moo_ki_gpu_head_slice_res(hK[l], sKh, T, DKV, DH, gseg * DH, 1));
            G(moo_ki_gpu_matmul_bw_res(hAtt[l][h], sVh, sDctxh, sDAtt, sDvh, T, T, DH));
            G(moo_ki_gpu_softmax_bw_res(MOO_KI_SM_SOFTMAX, hAtt[l][h], sDAtt, sDScm, T, T));
            G(moo_ki_gpu_unary_res(MOO_KI_U_MULS, sDScm, sDScm, TT, (float)SCALE));
            G(moo_ki_gpu_transpose_res(sKh, sKt, T, DH));
            G(moo_ki_gpu_matmul_bw_res(sQh, sKt, sDScm, sDqh, sDkth, T, DH, T));
            G(moo_ki_gpu_transpose_res(sDkth, sDkh, DH, T));
            G(moo_ki_gpu_head_slice_res(sDqh, dQr, T, D, DH, h * DH, 0));
            if (h % REP == 0) {
                G(moo_ki_gpu_copy_res(sDkh, dKg[gseg], TDH, 0, 0));
                G(moo_ki_gpu_copy_res(sDvh, dVg[gseg], TDH, 0, 0));
            } else {
                G(moo_ki_gpu_grad_accum_res(dKg[gseg], sDkh, TDH));
                G(moo_ki_gpu_grad_accum_res(dVg[gseg], sDvh, TDH));
            }
        }
        for (int g = 0; g < HKV; g++) {
            G(moo_ki_gpu_head_slice_res(dKg[g], dKr, T, DKV, DH, g * DH, 0));
            G(moo_ki_gpu_head_slice_res(dVg[g], dVf, T, DKV, DH, g * DH, 0));
        }
        G(moo_ki_gpu_rope_res(dQr, dQ, T, D, DH, 0, 0));
        G(moo_ki_gpu_rope_res(dKr, dK, T, DKV, DH, 0, 0));
        G(moo_ki_gpu_matmul_bw_res(hXn[l], hWq[l], dQ, dxnq, hgWq[l], T, D, D));
        G(moo_ki_gpu_matmul_bw_res(hXn[l], hWk[l], dK, dxnk, hgWk[l], T, D, DKV));
        G(moo_ki_gpu_matmul_bw_res(hXn[l], hWv[l], dVf, dxnv, hgWv[l], T, D, DKV));
        G(moo_ki_gpu_grad_accum_res(dxnq, dxnk, TD));
        G(moo_ki_gpu_grad_accum_res(dxnq, dxnv, TD));
        G(moo_ki_gpu_norm_bw_res(MOO_KI_NORM_RMS, hStream[l], dxnq, dxb, T, D, (float)EPS));
        G(moo_ki_gpu_grad_accum_res(dX, dxb, TD));
    }
    G(moo_ki_gpu_scatter_add_res(dX, hIdx, hgEmb, T, D, V));
    return true;
}

static bool gpu_adam(int t) {
    for (int i = 0; i < ngp; i++)
        G(moo_ki_gpu_opt_adam_res(GP[i].p, GP[i].g, GP[i].mv, GP[i].n,
                                  (float)LR, (float)B1, (float)B2, (float)EPSA, 0.0f, 0, t));
    return true;
}
#undef G
#endif /* !G4B_CPU_ONLY */

int main(void) {
    SCALE = 1.0 / sqrt((double)DH);
    {
        size_t PS = (size_t)VD + DV + (size_t)L * (2 * DD + 2 * DKD + 2 * DF + FD);
        size_t ACT = (size_t)(L + 1) * TD + (size_t)L * (6 * TD + 2 * TDKV + H * TT + 5 * TF);
        size_t TMP = (size_t)(8 * TDH + 6 * TT + 30 * TD + 6 * TDKV + 2 * DH * T)
                   + (size_t)(2 * (VD > DF ? VD : DF)) + 4096;
        g_acap = (4 * PS + ACT + 3 * TMP + 2 * VD) * 2 + 1024 * 1024;
    }
    g_arena = (double*)malloc(g_acap * sizeof(double));
    if (!g_arena) { fprintf(stderr, "arena malloc fail\n"); return 2; }

    printf("== KIP-G4b: residentes Multi-Head/GQA + RoPE Training-PoC ==\n");
    printf("   Dims: D=%d T=%d V=%d F=%d L=%d H=%d HKV=%d head_dim=%d rep=%d STEPS=%d\n",
           D, T, V, F, L, H, HKV, DH, REP, STEPS);

    cpu_init_params();

    printf("\n[1] FD-Gradcheck der CPU-Referenz (double):\n");
    size_t apos_save = g_apos;
    (void)cpu_forward();
    cpu_backward();
    bool fd_ok = true;
    fd_ok &= fd_check_param(0, G4B_FD_SAMPLES);                 /* Emb */
    fd_ok &= fd_check_param(1, G4B_FD_SAMPLES);                 /* Whead */
    fd_ok &= fd_check_param(2, G4B_FD_SAMPLES);                 /* Wq layer0 */
    fd_ok &= fd_check_param(3, G4B_FD_SAMPLES);                 /* Wk layer0 (GQA-Breite) */
    fd_ok &= fd_check_param(4, G4B_FD_SAMPLES);                 /* Wv layer0 */
    fd_ok &= fd_check_param(5, G4B_FD_SAMPLES);                 /* Wo layer0 */
    fd_ok &= fd_check_param(6, G4B_FD_SAMPLES);                 /* W1 layer0 */
    if (L > 1) fd_ok &= fd_check_param(3 + 7, G4B_FD_SAMPLES);  /* Wk layer1 */
    CHECK(fd_ok, "FD-Gradcheck: analytische == numerische Grads (rel < 1e-3)");
    g_apos = apos_save;

    for (int i = 0; i < np; i++) { g_snap[i] = aa(CP[i].n); memcpy(g_snap[i], CP[i].p, (size_t)CP[i].n * sizeof(double)); }

    printf("\n[2] CPU-Referenz-Trainingskurve:\n");
    double cpu_loss[STEPS]; clock_t c0 = clock();
    for (int t = 1; t <= STEPS; t++) {
        size_t sv = g_apos;
        cpu_loss[t - 1] = cpu_forward(); cpu_backward(); cpu_adam(t);
        g_apos = sv;
        printf("   CPU Schritt %d: loss=%.6f\n", t, cpu_loss[t - 1]);
    }
    double cpu_sec = (double)(clock() - c0) / CLOCKS_PER_SEC;

#ifdef G4B_CPU_ONLY
    printf("\nG4B_CPU_ONLY: nur CPU-Mathe validiert.\n");
    free(g_arena); return fehler == 0 ? 0 : 1;
#else
    void* probe0 = moo_ki_gpu_buf_belegen(64);
    if (!probe0) { printf("\nSKIP: keine GPU-Residenz (kein Vulkan/keine GPU) — G4b nicht bewiesen\n"); free(g_arena); return 0; }
    moo_ki_gpu_buf_freigeben(probe0);

    { int zmax = 2 * MAX2(MAX2(MAX2(DD, DKD), MAX2(DF, FD)), MAX2(VD, DV));
      g_upf = (float*)malloc((size_t)zmax * sizeof(float));
      if (!g_upf) { fprintf(stderr, "upf malloc fail\n"); return 2; } }

    CHECK(gpu_alloc(), "GPU-Buffers belegt");
    for (int i = 0; i < np; i++) memcpy(CP[i].p, g_snap[i], (size_t)CP[i].n * sizeof(double));

    double gpu_loss[2][STEPS]; double gpu_sec = 0.0;
    uint64_t submits_step1 = 0;
    MooKiGpuTelemetrie tel_loop; memset(&tel_loop, 0, sizeof tel_loop);

    for (int run = 0; run < 2; run++) {
        CHECK(gpu_upload_params(), run == 0 ? "Setup-Upload Lauf A" : "Setup-Upload Lauf B");
        moo_ki_gpu_telemetrie_reset();
        clock_t g0 = clock(); bool ok = true;
        for (int t = 1; t <= STEPS; t++) {
            double loss = 0.0;
            MooKiGpuTelemetrie a; moo_ki_gpu_telemetrie(&a);
            ok = ok && gpu_forward(&loss) && gpu_backward() && gpu_adam(t);
            MooKiGpuTelemetrie b; moo_ki_gpu_telemetrie(&b);
            gpu_loss[run][t - 1] = loss;
            if (run == 0 && t == 1) submits_step1 = b.submits - a.submits;
            if (run == 0) printf("   GPU Schritt %d: loss=%.6f (submits=%llu)\n",
                                 t, loss, (unsigned long long)(b.submits - a.submits));
        }
        gpu_sec = (double)(clock() - g0) / CLOCKS_PER_SEC;
        CHECK(ok, run == 0 ? "GPU-Trainingsloop Lauf A" : "GPU-Trainingsloop Lauf B");
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
    CHECK(tel_loop.uploads == 0, "uploads == 0 im Loop (alles resident)");
    CHECK(tel_loop.downloads == (uint64_t)STEPS, "downloads == STEPS (nur CE-Loss raus)");
    CHECK(submits_step1 > 0 && tel_loop.submits == submits_step1 * (uint64_t)STEPS,
          "submits == konst. Op-Positivliste je Schritt (deterministisch)");

    printf("\n[6] Protokoll:\n");
    printf("   submits/Schritt = %llu   (RoPE+head_slice strided, echte %d-Head/%d-KV-GQA-Attention)\n",
           (unsigned long long)submits_step1, H, HKV);
    printf("   Wallclock: CPU(double)=%.4fs  GPU(float)=%.4fs\n", cpu_sec, gpu_sec);

    gb_free_all(); free(g_upf); free(g_arena);
    printf("\n%s — KIP-G4b %s (fehler=%d)\n",
           fehler == 0 ? "ALLE PASS" : "FEHLER",
           fehler == 0 ? "GRUEN (residentes MHA/GQA+RoPE-Training == CPU, deterministisch, resident)" : "ROT",
           fehler);
    return fehler == 0 ? 0 : 1;
#endif
}
