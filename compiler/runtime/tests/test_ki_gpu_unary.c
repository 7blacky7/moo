/**
 * test_ki_gpu_unary.c — KIP-G3d-a Gate: GPU unaere/Skalar/Aktivierungs-Ops.
 *
 * Beweist je Op auf echter Vulkan-Hardware (sonst transparenter SKIP):
 *  FWD:  moo_ki_gpu_unary_res(op, x, o, n, s)  == CPU-Referenz (u_x / ews_op / pow)
 *  GRAD: moo_ki_gpu_unary_bw_res(op, src, g, gin, n, s) == g * f'(...)  (bw_*)
 * fuer adds/subs/muls/divs, exp/log/sqrt/neg/pow, relu/sigmoid/tanh/gelu.
 *
 * TOLERANZ (dokumentiert): kombiniert |gpu-cpu| <= ATOL + RTOL*|cpu| mit
 *  ATOL=1e-5, RTOL=1e-4. Rein algebraische Ops (adds/subs/muls/neg/relu)
 *  liegen praktisch exakt; transzendente GPU-Builtins (exp/log/sqrt/tanh/pow
 *  und die daraus zusammengesetzten sigmoid/gelu) sowie reziproke Faktoren
 *  (divs/log/sqrt) weichen um wenige ULP von den libc-Funktionen (expf/logf/
 *  tanhf/powf) ab — das ist erwartetes float-Verhalten, nicht ein Fehler.
 *
 * n = 1000 (kein Vielfaches von 256) prueft zugleich die Rand-Guard im Shader.
 * Baut ohne Autograd/Tensor-Schicht — nur gegen moo_ki_gpu_api.h. OHNE ASan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "moo_ki_gpu_api.h"

static int fehler = 0;
#define CHECK(bed, msg) do { \
    if (bed) { printf("  OK   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); fehler++; } \
} while (0)

#define SZ  1000                 /* bewusst kein Vielfaches von 256 */
#define BY  ((int64_t)SZ * 4)

#define ATOL 1e-5f
#define RTOL 1e-4f
static bool nahe(float x, float y) {
    return fabsf(x - y) <= ATOL + RTOL * fabsf(y);
}

/* GELU-Konstante wie u_gelu/bw_gelu in moo_tensor_ops.c / moo_autograd.c */
static const float GELU_K = 0.7978845608f;

/* CPU-Forward-Referenz (identische Semantik zu unary_fwd.comp). */
static float cpu_fwd(int op, float x, float s) {
    switch (op) {
        case MOO_KI_U_ADDS:    return x + s;
        case MOO_KI_U_SUBS:    return x - s;
        case MOO_KI_U_MULS:    return x * s;
        case MOO_KI_U_DIVS:    return x / s;
        case MOO_KI_U_EXP:     return expf(x);
        case MOO_KI_U_LOG:     return logf(x);
        case MOO_KI_U_SQRT:    return sqrtf(x);
        case MOO_KI_U_NEG:     return -x;
        case MOO_KI_U_POW:     return powf(x, s);
        case MOO_KI_U_RELU:    return x > 0.0f ? x : 0.0f;
        case MOO_KI_U_SIGMOID: return 1.0f / (1.0f + expf(-x));
        case MOO_KI_U_TANH:    return tanhf(x);
        default: {  /* gelu */
            float inner = GELU_K * (x + 0.044715f * x * x * x);
            return 0.5f * x * (1.0f + tanhf(inner));
        }
    }
}

/* CPU-Backward-Referenz: lokale Ableitung f'(x,y,s) (identisch zu bw_* /
 * unary_bw.comp). y = cpu_fwd(op,x,s). */
static float cpu_deriv(int op, float x, float y, float s) {
    switch (op) {
        case MOO_KI_U_ADDS:    return 1.0f;
        case MOO_KI_U_SUBS:    return 1.0f;
        case MOO_KI_U_MULS:    return s;
        case MOO_KI_U_DIVS:    return 1.0f / s;
        case MOO_KI_U_EXP:     return y;
        case MOO_KI_U_LOG:     return 1.0f / x;
        case MOO_KI_U_SQRT:    return 0.5f / y;
        case MOO_KI_U_NEG:     return -1.0f;
        case MOO_KI_U_POW:     return s * powf(x, s - 1.0f);
        case MOO_KI_U_RELU:    return x > 0.0f ? 1.0f : 0.0f;
        case MOO_KI_U_SIGMOID: return y * (1.0f - y);
        case MOO_KI_U_TANH:    return 1.0f - y * y;
        default: {  /* gelu' */
            float u = GELU_K * (x + 0.044715f * x * x * x);
            float th = tanhf(u);
            float du = GELU_K * (1.0f + 3.0f * 0.044715f * x * x);
            return 0.5f * (1.0f + th) + 0.5f * x * (1.0f - th * th) * du;
        }
    }
}

typedef struct {
    int op; const char* name; float s; bool positiv; bool src_ist_output;
} OpSpec;

/* positiv=true: Eingang aus (0,3] (log/sqrt/pow-Definitionsbereich).
 * src_ist_output: Backward-Quelle ist y statt x (exp/sqrt/sigmoid/tanh). */
static const OpSpec SPECS[] = {
    { MOO_KI_U_ADDS,    "adds",    1.5f, false, false },
    { MOO_KI_U_SUBS,    "subs",    1.5f, false, false },
    { MOO_KI_U_MULS,    "muls",    2.5f, false, false },
    { MOO_KI_U_DIVS,    "divs",    2.5f, false, false },
    { MOO_KI_U_EXP,     "exp",     0.0f, false, true  },
    { MOO_KI_U_LOG,     "log",     0.0f, true,  false },
    { MOO_KI_U_SQRT,    "sqrt",    0.0f, true,  true  },
    { MOO_KI_U_NEG,     "neg",     0.0f, false, false },
    { MOO_KI_U_POW,     "pow",     2.5f, true,  false },
    { MOO_KI_U_RELU,    "relu",    0.0f, false, false },
    { MOO_KI_U_SIGMOID, "sigmoid", 0.0f, false, true  },
    { MOO_KI_U_TANH,    "tanh",    0.0f, false, true  },
    { MOO_KI_U_GELU,    "gelu",    0.0f, false, false },
};
#define NSPEC ((int)(sizeof(SPECS) / sizeof(SPECS[0])))

int main(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) {
        printf("SKIP: keine GPU-Residenz zur Laufzeit (kein Vulkan/keine GPU)\n");
        return 0;
    }
    moo_ki_gpu_buf_freigeben(probe);
    printf("== KIP-G3d-a unaere/Skalar/Aktivierungs-Ops (Fwd+Grad) ==\n");

    /* Zwei Eingangs-Muster: allgemein [-2,2] und strikt positiv (0,3]. */
    float xg[SZ], xp[SZ], g[SZ];
    for (int i = 0; i < SZ; i++) {
        xg[i] = ((float)(i % 41) - 20.0f) * 0.1f;          /* [-2.0, 2.0] */
        xp[i] = 0.05f + (float)(i % 30) * 0.1f;            /* [0.05, 3.0] */
        g[i]  = ((float)(i % 13) - 6.0f) * 0.25f + 0.05f;  /* grad_out, != 0 */
    }

    void* hx   = moo_ki_gpu_buf_belegen(BY);   /* Eingang x */
    void* ho   = moo_ki_gpu_buf_belegen(BY);   /* Forward-Ergebnis */
    void* hsrc = moo_ki_gpu_buf_belegen(BY);   /* Backward-Quelle (x oder y) */
    void* hg   = moo_ki_gpu_buf_belegen(BY);   /* grad_out */
    void* hgin = moo_ki_gpu_buf_belegen(BY);   /* grad_in-Ergebnis */
    CHECK(hx && ho && hsrc && hg && hgin, "5 residente Buffers belegt");

    moo_ki_gpu_telemetrie_reset();
    float y[SZ], gpu_o[SZ], gpu_gin[SZ], src[SZ];

    for (int k = 0; k < NSPEC; k++) {
        OpSpec sp = SPECS[k];
        const float* x = sp.positiv ? xp : xg;
        char msg[128];

        /* --- Forward --- */
        for (int i = 0; i < SZ; i++) y[i] = cpu_fwd(sp.op, x[i], sp.s);
        bool okf = moo_ki_gpu_upload(hx, x, BY)
                && moo_ki_gpu_unary_res(sp.op, hx, ho, SZ, sp.s)
                && moo_ki_gpu_download(ho, gpu_o, BY);
        int df = 0; float worst_f = 0.0f;
        for (int i = 0; i < SZ; i++) {
            if (!nahe(gpu_o[i], y[i])) df++;
            float e = fabsf(gpu_o[i] - y[i]); if (e > worst_f) worst_f = e;
        }
        snprintf(msg, sizeof(msg), "%-7s FWD == CPU (max|d|=%.2e)", sp.name, worst_f);
        CHECK(okf && df == 0, msg);

        /* --- Backward: gin = g * f'(src) --- */
        for (int i = 0; i < SZ; i++) src[i] = sp.src_ist_output ? y[i] : x[i];
        bool okb = moo_ki_gpu_upload(hsrc, src, BY)
                && moo_ki_gpu_upload(hg, g, BY)
                && moo_ki_gpu_unary_bw_res(sp.op, hsrc, hg, hgin, SZ, sp.s)
                && moo_ki_gpu_download(hgin, gpu_gin, BY);
        int db = 0; float worst_b = 0.0f;
        for (int i = 0; i < SZ; i++) {
            float ref = g[i] * cpu_deriv(sp.op, x[i], y[i], sp.s);
            if (!nahe(gpu_gin[i], ref)) db++;
            float e = fabsf(gpu_gin[i] - ref); if (e > worst_b) worst_b = e;
        }
        snprintf(msg, sizeof(msg), "%-7s GRAD == g*f' (max|d|=%.2e)", sp.name, worst_b);
        CHECK(okb && db == 0, msg);
    }

    MooKiGpuTelemetrie t;
    moo_ki_gpu_telemetrie(&t);
    CHECK(t.cpu_fallbacks == 0, "keine CPU-Fallbacks im residenten Pfad");
    /* je Op: 1 Fwd-Dispatch + 1 Bwd-Dispatch = 2*NSPEC Compute-Submits. */
    CHECK(t.submits == (uint64_t)(2 * NSPEC), "genau 2 Compute-Submits pro Op (Fwd+Bwd)");

    moo_ki_gpu_buf_freigeben(hx);   moo_ki_gpu_buf_freigeben(ho);
    moo_ki_gpu_buf_freigeben(hsrc); moo_ki_gpu_buf_freigeben(hg);
    moo_ki_gpu_buf_freigeben(hgin);

    printf("\n%s (fehler=%d)\n", fehler == 0 ? "ALLE PASS" : "FEHLER", fehler);
    return fehler == 0 ? 0 : 1;
}
