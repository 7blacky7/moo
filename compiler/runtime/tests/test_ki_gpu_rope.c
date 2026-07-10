/**
 * test_ki_gpu_rope.c — KIP-G4b: CPU-Differentialtest fuer die zwei neuen
 * strided Kernels rope_res (interleaved RoPE-Paarrotation, Fwd/Bwd) und
 * head_slice_res (Kopf-Extraktion/-Einfuegung fuer Multi-Head/GQA/MQA).
 *
 * Standalone-Harness (wird NICHT von run_all geglobt). Ohne echte Vulkan-GPU:
 * SKIP (Exit 0 — SKIP ist KEIN Beweis). Referenz-Semantik = kip-ops B2
 * (moo_nn.c rope_cossin/rope_anwenden), interleaved Paare, dh gerade:
 *   angle(p,i) = p * 10000^(-2i/dh),  i in [0, dh/2), p = pos_offset + Zeile.
 *   Fwd (+ang):  o[2i]=x[2i]c - x[2i+1]s ;  o[2i+1]=x[2i+1]c + x[2i]s
 *   Bwd (-ang):  o[2i]=g[2i]c + g[2i+1]s ;  o[2i+1]=g[2i+1]c - g[2i]s
 *
 * Deckung:
 *  (1) RoPE Fwd vs double-Referenz ueber Shape-Matrix (MHA/GQA/MQA-Kopfbreiten,
 *      Positionsoffset 0 und !=0/chunked-Prefill).
 *  (2) Position-0-Identitaet: p=0 -> cos=1/sin=0 -> Ausgang BIT-identisch Eingang.
 *  (3) Orthogonalitaet: bwd(fwd(x)) == x (Rotation ist ihre eigene Inverse mit
 *      -angle) — starker, referenz-unabhaengiger Korrektheitsbeweis.
 *  (4) RoPE Bwd vs double-Referenz.
 *  (5) head_slice extract/insert vs Referenz ueber MHA/GQA/MQA-Shapes.
 *  (6) Negativfaelle: ungerade head_dim / dim, Slice-Ueberlauf -> Op false.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "moo_ki_gpu_api.h"

static int fehler = 0;
#define CHECK(b, msg) do { if (b) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fehler++; } } while (0)

#define TOL 1.5e-4

static float frand(int i) { return 0.37f * sinf(0.11f * (float)i + 0.3f)
                                 - 0.21f * cosf(0.07f * (float)i + 0.9f); }

static void cpu_rope(const float* a, float* o, int rows, int dim, int hd, int p0, int fwd) {
    int ppr = dim / 2;
    for (int t = 0; t < rows; t++)
        for (int pr = 0; pr < ppr; pr++) {
            int col0 = 2 * pr;
            int ihp = (col0 % hd) / 2;
            double pos = (double)(p0 + t);
            double freq = pow(10000.0, -2.0 * (double)ihp / (double)hd);
            double ang = pos * freq, c = cos(ang), s = sin(ang);
            double x0 = a[t * dim + col0], x1 = a[t * dim + col0 + 1], r0, r1;
            if (fwd) { r0 = x0 * c - x1 * s; r1 = x1 * c + x0 * s; }
            else     { r0 = x0 * c + x1 * s; r1 = x1 * c - x0 * s; }
            o[t * dim + col0]     = (float)r0;
            o[t * dim + col0 + 1] = (float)r1;
        }
}
static void cpu_extract(const float* a, float* o, int rows, int dim, int hd, int off) {
    for (int t = 0; t < rows; t++) for (int j = 0; j < hd; j++) o[t * hd + j] = a[t * dim + off + j];
}
static void cpu_insert(const float* head, float* o, int rows, int dim, int hd, int off) {
    for (int t = 0; t < rows; t++) for (int j = 0; j < hd; j++) o[t * dim + off + j] = head[t * hd + j];
}
static double maxdiff(const float* x, const float* y, int n) {
    double m = 0; for (int i = 0; i < n; i++) { double d = fabs((double)x[i] - (double)y[i]); if (d > m) m = d; } return m;
}

static bool run_rope(const float* a, float* o, int rows, int dim, int hd, int p0, int fwd) {
    int64_t nb = (int64_t)rows * dim * 4;
    void* ha = moo_ki_gpu_buf_belegen(nb);
    void* ho = moo_ki_gpu_buf_belegen(nb);
    bool ok = ha && ho && moo_ki_gpu_upload(ha, a, nb)
           && moo_ki_gpu_rope_res(ha, ho, rows, dim, hd, p0, fwd)
           && moo_ki_gpu_download(ho, o, nb);
    moo_ki_gpu_buf_freigeben(ha); moo_ki_gpu_buf_freigeben(ho);
    return ok;
}
static bool run_extract(const float* a, float* o, int rows, int dim, int hd, int off) {
    int64_t nib = (int64_t)rows * dim * 4, nob = (int64_t)rows * hd * 4;
    void* ha = moo_ki_gpu_buf_belegen(nib);
    void* ho = moo_ki_gpu_buf_belegen(nob);
    bool ok = ha && ho && moo_ki_gpu_upload(ha, a, nib)
           && moo_ki_gpu_head_slice_res(ha, ho, rows, dim, hd, off, 1)
           && moo_ki_gpu_download(ho, o, nob);
    moo_ki_gpu_buf_freigeben(ha); moo_ki_gpu_buf_freigeben(ho);
    return ok;
}
static bool run_insert(const float* head, const float* base, float* o,
                       int rows, int dim, int hd, int off) {
    int64_t nib = (int64_t)rows * hd * 4, nob = (int64_t)rows * dim * 4;
    void* ha = moo_ki_gpu_buf_belegen(nib);
    void* ho = moo_ki_gpu_buf_belegen(nob);
    bool ok = ha && ho && moo_ki_gpu_upload(ha, head, nib) && moo_ki_gpu_upload(ho, base, nob)
           && moo_ki_gpu_head_slice_res(ha, ho, rows, dim, hd, off, 0)
           && moo_ki_gpu_download(ho, o, nob);
    moo_ki_gpu_buf_freigeben(ha); moo_ki_gpu_buf_freigeben(ho);
    return ok;
}

/* Shape-Matrix: rows, dim, head_dim. Deckt MHA (dim=H*hd), GQA-KV-Breite
 * (dim=kv*hd) und MQA (dim=hd) ueber verschiedene Kopfbreiten ab. */
typedef struct { int rows, dim, hd; const char* name; } Shape;
static Shape SH[] = {
    { 8, 32, 8, "MHA D=32 hd=8 (4 Koepfe)" },
    { 8, 16, 8, "GQA-KV D=16 hd=8 (2 Gruppen)" },
    { 8,  8, 8, "MQA D=8 hd=8 (1 Kopf)" },
    { 5, 24, 4, "hd=4 D=24 (6 Koepfe, ungerade rows)" },
    { 4, 16, 2, "hd=2 (min) D=16" },
    { 1, 32, 8, "rows=1 (Decode-Schritt)" },
};
enum { NSH = (int)(sizeof(SH) / sizeof(SH[0])) };

int main(void) {
    printf("== KIP-G4b: rope_res + head_slice_res CPU-Differentialtest ==\n");
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) { printf("SKIP: keine GPU-Residenz (kein Vulkan/keine GPU)\n"); return 0; }
    moo_ki_gpu_buf_freigeben(probe);

    static float a[8 * 32], gpu[8 * 32], ref[8 * 32], back[8 * 32], head[8 * 32], base[8 * 32];
    for (int i = 0; i < 8 * 32; i++) { a[i] = frand(i); base[i] = 0.5f * frand(i + 100); }

    printf("\n[1] RoPE Forward vs double-Referenz (Shape-Matrix, p0=0 und p0=3):\n");
    for (int p0 = 0; p0 <= 3; p0 += 3)
        for (int k = 0; k < NSH; k++) {
            int r = SH[k].rows, d = SH[k].dim, hd = SH[k].hd, n = r * d;
            bool ok = run_rope(a, gpu, r, d, hd, p0, 1);
            cpu_rope(a, ref, r, d, hd, p0, 1);
            double md = maxdiff(gpu, ref, n);
            char msg[128]; snprintf(msg, sizeof msg, "Fwd p0=%d %s (maxdiff=%.2e)", p0, SH[k].name, md);
            CHECK(ok && md < TOL, msg);
        }

    printf("\n[2] Position-0-Identitaet (rows=1, p0=0 -> Ausgang == Eingang, bit-exakt):\n");
    {
        bool ok = run_rope(a, gpu, 1, 32, 8, 0, 1);
        double md = maxdiff(gpu, a, 32);
        CHECK(ok && md == 0.0, "p=0: cos=1/sin=0 -> Identitaet bit-exakt (maxdiff==0)");
    }

    printf("\n[3] Orthogonalitaet: bwd(fwd(x)) == x (Rotation ist eigene Inverse):\n");
    for (int k = 0; k < NSH; k++) {
        int r = SH[k].rows, d = SH[k].dim, hd = SH[k].hd, n = r * d;
        bool ok = run_rope(a, ref, r, d, hd, 2, 1) && run_rope(ref, back, r, d, hd, 2, 0);
        double md = maxdiff(back, a, n);
        char msg[128]; snprintf(msg, sizeof msg, "Roundtrip %s (maxdiff=%.2e)", SH[k].name, md);
        CHECK(ok && md < TOL, msg);
    }

    printf("\n[4] RoPE Backward vs double-Referenz (p0=3):\n");
    for (int k = 0; k < NSH; k++) {
        int r = SH[k].rows, d = SH[k].dim, hd = SH[k].hd, n = r * d;
        bool ok = run_rope(a, gpu, r, d, hd, 3, 0);
        cpu_rope(a, ref, r, d, hd, 3, 0);
        double md = maxdiff(gpu, ref, n);
        char msg[128]; snprintf(msg, sizeof msg, "Bwd %s (maxdiff=%.2e)", SH[k].name, md);
        CHECK(ok && md < TOL, msg);
    }

    printf("\n[5] head_slice extract/insert vs Referenz (MHA/GQA/MQA):\n");
    {
        /* extract: alle Koepfe/Gruppen aus [8,32] hd=8 und [8,16] hd=8 und [8,8] hd=8. */
        struct { int dim, hd; const char* name; } EC[] = {
            { 32, 8, "MHA 32/8" }, { 16, 8, "GQA-KV 16/8" }, { 8, 8, "MQA 8/8" }, { 24, 4, "24/4" }
        };
        for (int e = 0; e < 4; e++) {
            int d = EC[e].dim, hd = EC[e].hd, r = 8;
            bool all = true; double mmax = 0;
            for (int off = 0; off + hd <= d; off += hd) {
                bool ok = run_extract(a, gpu, r, d, hd, off);
                cpu_extract(a, ref, r, d, hd, off);
                double md = maxdiff(gpu, ref, r * hd);
                all = all && ok; if (md > mmax) mmax = md;
            }
            char msg[128]; snprintf(msg, sizeof msg, "extract alle Koepfe %s (maxdiff=%.2e)", EC[e].name, mmax);
            CHECK(all && mmax == 0.0, msg);
        }
        /* insert: Kopf in Base-Muster einfuegen, nur Kopf-Spalten aendern. */
        int d = 32, hd = 8, r = 8, off = 16;
        for (int t = 0; t < r; t++) for (int j = 0; j < hd; j++) head[t * hd + j] = a[t * hd + j];
        bool ok = run_insert(head, base, gpu, r, d, hd, off);
        memcpy(ref, base, sizeof(float) * r * d);
        cpu_insert(head, ref, r, d, hd, off);
        double md = maxdiff(gpu, ref, r * d);
        CHECK(ok && md == 0.0, "insert Kopf@off=16: nur Kopf-Spalten geschrieben, Rest unberuehrt");
    }

    printf("\n[6] Negativfaelle (Op muss false liefern, kein stiller Unsinn):\n");
    {
        void* h1 = moo_ki_gpu_buf_belegen(8 * 32 * 4);
        void* h2 = moo_ki_gpu_buf_belegen(8 * 32 * 4);
        bool odd_hd  = moo_ki_gpu_rope_res(h1, h2, 4, 32, 7, 0, 1);   /* head_dim ungerade */
        bool odd_dim = moo_ki_gpu_rope_res(h1, h2, 4, 32, 6, 0, 1);   /* dim%head_dim != 0 (32%6=2) */
        bool ovf     = moo_ki_gpu_head_slice_res(h1, h2, 4, 32, 8, 28, 1); /* 28+8>32 */
        moo_ki_gpu_buf_freigeben(h1); moo_ki_gpu_buf_freigeben(h2);
        CHECK(!odd_hd,  "rope_res mit ungeradem head_dim -> false");
        CHECK(!odd_dim, "rope_res mit dim%head_dim!=0 -> false");
        CHECK(!ovf,     "head_slice_res mit col_offset+head_dim>dim -> false");
    }

    printf("\n%s — KIP-G4b Kernel-Differentialtest %s (fehler=%d)\n",
           fehler == 0 ? "ALLE PASS" : "FEHLER",
           fehler == 0 ? "GRUEN" : "ROT", fehler);
    return fehler == 0 ? 0 : 1;
}
