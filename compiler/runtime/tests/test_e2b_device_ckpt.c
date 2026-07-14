/**
 * test_e2b_device_ckpt.c — KIP-E2b GPU-/Device-Checkpoint + Cross-Device-Restore.
 * ============================================================================
 * GPU-SMOKE: ohne libvulkan/GPU -> SKIP (Exit 0, KEIN Beweis). Baut via
 * skripte/kip_e2b.sh. Ohne ASan (NVIDIA-Treiber-Leak-Noise), wie kip_g4_lm.sh.
 *
 * v1-VERTRAG (koordinator2, Channel 12780): duenne Device<->Host-Schicht um das
 * BESTEHENDE E2-f32-Checkpointformat — KEIN zweiter Serializer.
 *  - Speichern nur an sauberer Optimizer-Schritt-Grenze; moo_ki_gpu_download
 *    fenced intern (transfer_submit wartet) -> Snapshot ist konsistent.
 *  - Autoritative Device-Zustaende p* + Optimizer m/v ueber die G3c-Download-
 *    Schnittstelle (moo_ki_gpu_download; Adam mv=2n, m in [0,n), v in [n,2n)),
 *    t haelt der Aufrufer host-seitig -> dann bestehender moo_nn_ckpt_speichern.
 *  - GRAD-BUFFER NICHT serialisiert (nach abgeschlossenem Schritt transient).
 *  - Restore: moo_nn_ckpt_laden -> moo_ki_gpu_upload auf Device + t an opt_adam_res.
 *  - Format bleibt E2/E2c-kompatibel; kein GPU-spezifisches zweites Format.
 *  - GENERISCHER Device-State-/G3c-Adapter + Harness (kein Production-Wiring der
 *    moo_nn-API vor KIP-G4c).
 *
 * Adam ist elementweise (keine Reduktion) -> auf DERSELBEN GPU deterministisch,
 * hier sogar BIT-identisch. Der Toleranz-/Determinismusvertrag der Task greift
 * erst bei reduktions-lastigem echten Training (Reduktionsreihenfolge).
 *
 * GATES:
 *  1. Device-Training K Schritte, fenced Download reflektiert den Schritt
 *     (Parameter bewegt + finit).
 *  2. Device-State-Download p*, m/v -> moo_nn_ckpt_speichern (E2-Format).
 *  3. CPU-RESTORE: moo_nn_ckpt_laden liefert p* + Optimizer m/v/t bit-genau,
 *     OHNE GPU (Cross-Device: Device-Checkpoint auf reinem CPU-Pfad ladbar).
 *  4. GPU-RESTORE: upload p*+m/v + weiter L Schritte == ununterbrochen K+L
 *     (bit-identisch, elementweiser Adam).
 *  5. Cross-Device-Roundtrip: upload->download f32 bit-identisch.
 *  6. Alter reiner f32-E2-Checkpoint (ohne Device) bleibt ladbar.
 *  7. Telemetrie: cpu_fallbacks==0 waehrend der Device-Ops.
 * ============================================================================
 */
#include "../moo_runtime.h"
#include "../moo_ki_gpu_api.h"
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* --- Test-throw-Modell (ersetzt moo_error.c) ------------------------------ */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    moo_release(error);
    moo_error_flag = 1;
}
static void fehler_reset(void) { moo_error_flag = 0; }

void moo_socket_free(void* p)       { (void)p; }
void moo_thread_free(void* p)       { (void)p; }
void moo_channel_free(void* p)      { (void)p; }
void moo_db_free(void* p)           { (void)p; }
void moo_db_stmt_free(void* p)      { (void)p; }
void moo_window_free(void* p)       { (void)p; }
void moo_web_free(void* p)          { (void)p; }
void moo_voxel_free(void* p)        { (void)p; }
void moo_frame_free(void* p)        { (void)p; }
void moo_gif_handle_free(void* p)   { (void)p; }
void moo_video_handle_free(void* p) { (void)p; }

static int checks = 0;
#define CHECK(cond, name) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__); return 1; } \
    checks++; \
} while (0)

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }
static MooValue str_(const char* s) { return moo_string_new(s); }
static MooValue num_(double d) { return moo_number(d); }
static MooValue dget_(MooValue d, const char* k) { return moo_dict_get(d, moo_string_new(k)); }

static MooValue ckpt_speichern_p(MooValue zustand, const char* pfad) {
    MooValue p = str_(pfad);
    MooValue r = moo_nn_ckpt_speichern(zustand, p);
    moo_release(p);
    return r;
}
static MooValue ckpt_laden_p(const char* pfad, MooValue erw) {
    MooValue p = str_(pfad);
    MooValue r = moo_nn_ckpt_laden(p, erw);
    moo_release(p);
    return r;
}

static MooValue mk_dicht(int ein, int aus, const char* akt, double seed) {
    MooValue a = akt ? str_(akt) : moo_none();
    MooValue s = (seed >= 0.0) ? num_(seed) : moo_none();
    MooValue d = moo_nn_schicht_dicht(num_(ein), num_(aus), a, s);
    moo_release(a);
    return d;
}
/* dicht(4,6,tanh) -> dicht(6,3): deterministische, seedbare Parameter. */
static MooValue baue_netz(void) {
    MooValue schichten = moo_list_new(2);
    moo_list_append(schichten, mk_dicht(4, 6, "tanh", 1.0));
    moo_list_append(schichten, mk_dicht(6, 3, "keine", 2.0));
    MooValue netz = moo_nn_ki_netz(schichten);
    moo_release(schichten);
    return netz;
}

/* Deterministischer synthetischer Gradient fuer (Schritt, Param-Index, Element).
 * Identisch zwischen unterbrochenem und ununterbrochenem Lauf -> Vergleichbarkeit. */
static void fuelle_grad(float* g, int64_t n, int step, int pi) {
    for (int64_t j = 0; j < n; j++)
        g[j] = 0.05f * (float)(((j + pi) % 5) - 2) + 0.002f * (float)step;
}

#define MAXP 16
#define K 4   /* Schritte vor dem Checkpoint */
#define L 3   /* Schritte nach dem Restore   */

static const float LR = 0.02f, B1 = 0.9f, B2 = 0.999f, EPS = 1e-8f;

/* Ein residenter Trainingslauf ueber [t0, t0+steps) auf vorgegebenen Init-
 * Parametern + Init-mv. Nach dem Lauf werden Parameter (out_p) und mv (out_mv,
 * je 2n) heruntergeladen. Gibt false bei GPU-Fehler. */
static bool device_train(int np, const int64_t* nn, float* const* init_p,
                         float* const* init_mv, int t0, int steps,
                         float** out_p, float** out_mv) {
    void* db[MAXP]; void* mvb[MAXP]; void* gb[MAXP];
    for (int i = 0; i < np; i++) { db[i] = mvb[i] = gb[i] = NULL; }
    bool ok = true;
    int64_t maxn = 1;
    for (int i = 0; i < np && ok; i++) {
        if (nn[i] > maxn) maxn = nn[i];
        db[i]  = moo_ki_gpu_buf_belegen(nn[i] * 4);
        mvb[i] = moo_ki_gpu_buf_belegen(nn[i] * 2 * 4);
        gb[i]  = moo_ki_gpu_buf_belegen(nn[i] * 4);
        if (!db[i] || !mvb[i] || !gb[i]) { ok = false; break; }
        ok = moo_ki_gpu_upload(db[i], init_p[i], nn[i] * 4)
          && moo_ki_gpu_upload(mvb[i], init_mv[i], nn[i] * 2 * 4);
    }
    float* g = (float*)malloc((size_t)maxn * sizeof(float));
    for (int step = t0; step < t0 + steps && ok; step++) {
        for (int i = 0; i < np && ok; i++) {
            fuelle_grad(g, nn[i], step, i);
            ok = moo_ki_gpu_upload(gb[i], g, nn[i] * 4)
              && moo_ki_gpu_opt_adam_res(db[i], gb[i], mvb[i], nn[i],
                                         LR, B1, B2, EPS, 0.f, 0, step);
        }
    }
    for (int i = 0; i < np && ok; i++) {
        ok = moo_ki_gpu_download(db[i], out_p[i], nn[i] * 4)
          && moo_ki_gpu_download(mvb[i], out_mv[i], nn[i] * 2 * 4);
    }
    free(g);
    for (int i = 0; i < np; i++) {
        moo_ki_gpu_buf_freigeben(db[i]);
        moo_ki_gpu_buf_freigeben(mvb[i]);
        moo_ki_gpu_buf_freigeben(gb[i]);
    }
    return ok;
}

int main(void) {
    fprintf(stderr, "== KIP-E2b GPU-/Device-Checkpoint Harness ==\n");

    /* SKIP wenn keine GPU-Residenz. */
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) {
        fprintf(stderr, "SKIP: keine GPU-Residenz (kein Vulkan/keine GPU) — E2b nicht bewiesen\n");
        return 0;
    }
    moo_ki_gpu_buf_freigeben(probe);

    MooValue netz = baue_netz();
    MooValue params = moo_nn_parameter(netz);
    CHECK(params.tag == MOO_LIST, "params-Liste");
    int np = MV_LIST(params)->length;
    CHECK(np > 0 && np <= MAXP, "Param-Anzahl im Rahmen");

    int64_t nn[MAXP];
    float* init_p[MAXP];   /* Anfangs-Parameter (Kopie) */
    float* zero_mv[MAXP];  /* mv-Init = 0 */
    float* dp[MAXP];       /* Download-Parameter nach K */
    float* dmv[MAXP];      /* Download-mv nach K (2n) */
    for (int i = 0; i < np; i++) {
        MooTensor* p = T(MV_LIST(params)->items[i]);
        nn[i] = p->size;
        init_p[i]  = (float*)malloc((size_t)nn[i] * 4);
        zero_mv[i] = (float*)calloc((size_t)nn[i] * 2, 4);
        dp[i]      = (float*)malloc((size_t)nn[i] * 4);
        dmv[i]     = (float*)malloc((size_t)nn[i] * 2 * 4);
        memcpy(init_p[i], p->data, (size_t)nn[i] * 4);
    }

    /* ===== [1] Device-Training K Schritte + Download ===== */
    MooKiGpuTelemetrie t_a, t_b;
    moo_ki_gpu_telemetrie(&t_a);
    CHECK(device_train(np, nn, init_p, zero_mv, 1, K, dp, dmv),
          "e2b: Device-Training K Schritte + Download");
    moo_ki_gpu_telemetrie(&t_b);
    CHECK(t_b.cpu_fallbacks == t_a.cpu_fallbacks, "e2b: kein cpu_fallback im Device-Training");
    { bool moved = false, fin = true;
      for (int64_t j = 0; j < nn[0]; j++) {
          if (dp[0][j] != init_p[0][j]) moved = true;
          if (!isfinite(dp[0][j])) fin = false;
      }
      CHECK(moved, "e2b: Training bewegt die Parameter (fenced Download)");
      CHECK(fin, "e2b: Parameter nach K Schritten finit"); }

    /* ===== [2] Device-State -> bestehender moo_nn_ckpt_speichern ===== */
    /* Netz-Parameter = Device-Download. */
    for (int i = 0; i < np; i++)
        memcpy(T(MV_LIST(params)->items[i])->data, dp[i], (size_t)nn[i] * 4);
    /* Optimizer bauen + m/v aus mv-Download setzen, t = K. */
    MooValue opt;
    { MooValue rn = num_(LR); opt = moo_nn_opt_adam(params, rn); moo_release(rn); }
    CHECK(opt.tag == MOO_DICT, "e2b: Optimizer gebaut");
    { MooValue ml = dget_(opt, "m"), vl = dget_(opt, "v");
      CHECK(ml.tag == MOO_LIST && vl.tag == MOO_LIST && MV_LIST(ml)->length == np,
            "e2b: Optimizer m/v-Listen");
      for (int i = 0; i < np; i++) {
          memcpy(T(MV_LIST(ml)->items[i])->data, dmv[i],              (size_t)nn[i] * 4);
          memcpy(T(MV_LIST(vl)->items[i])->data, dmv[i] + nn[i],      (size_t)nn[i] * 4);
      }
      moo_release(ml); moo_release(vl); }
    { MooValue kk = num_((double)K); moo_dict_set(opt, str_("t"), kk); }

    char pfad_dev[] = "/tmp/moo_e2b_dev_XXXXXX";
    int fdd = mkstemp(pfad_dev); CHECK(fdd >= 0, "e2b: tmp dev"); close(fdd);

    MooValue z = moo_dict_new();
    moo_retain(netz); moo_dict_set(z, str_("netz"), netz);
    moo_retain(opt);  moo_dict_set(z, str_("opt"), opt);
    moo_dict_set(z, str_("schritt"), num_(K));
    moo_dict_set(z, str_("tokenizer_version"), str_("dev-v1"));
    moo_release(ckpt_speichern_p(z, pfad_dev));
    CHECK(!moo_error_flag, "e2b: Device-Checkpoint geschrieben (moo_nn_ckpt_speichern)");
    moo_release(z);

    /* ===== [3] CPU-RESTORE (reiner Host-Pfad, keine GPU) ===== */
    MooValue z2 = ckpt_laden_p(pfad_dev, moo_none());
    CHECK(!moo_error_flag && z2.tag == MOO_DICT, "e2b: CPU-Restore laedt Device-Checkpoint");
    MooValue net2 = dget_(z2, "netz");
    MooValue prm2 = moo_nn_parameter(net2);
    { bool ok = (prm2.tag == MOO_LIST && MV_LIST(prm2)->length == np);
      for (int i = 0; i < np && ok; i++)
          for (int64_t j = 0; j < nn[i]; j++)
              if (T(MV_LIST(prm2)->items[i])->data[j] != dp[i][j]) ok = false;
      CHECK(ok, "e2b: CPU-Restore p* bit-identisch zum Device-Download"); }
    MooValue opt2 = dget_(z2, "opt");
    { MooValue tt = dget_(opt2, "t");
      CHECK(tt.tag == MOO_NUMBER && MV_NUM(tt) == (double)K, "e2b: Optimizer-t == K restauriert");
      moo_release(tt); }
    { MooValue ml = dget_(opt2, "m"), vl = dget_(opt2, "v"); bool ok = true;
      for (int i = 0; i < np && ok; i++)
          for (int64_t j = 0; j < nn[i]; j++) {
              if (T(MV_LIST(ml)->items[i])->data[j] != dmv[i][j])         ok = false;
              if (T(MV_LIST(vl)->items[i])->data[j] != dmv[i][nn[i] + j]) ok = false;
          }
      CHECK(ok, "e2b: CPU-Restore Optimizer m/v bit-identisch");
      moo_release(ml); moo_release(vl); }

    /* ===== [5] Cross-Device-Roundtrip upload->download f32 bit-identisch ===== */
    { void* tb = moo_ki_gpu_buf_belegen(nn[0] * 4);
      float* rt = (float*)malloc((size_t)nn[0] * 4);
      bool ok = tb && moo_ki_gpu_upload(tb, dp[0], nn[0] * 4)
                   && moo_ki_gpu_download(tb, rt, nn[0] * 4);
      for (int64_t j = 0; j < nn[0] && ok; j++) if (rt[j] != dp[0][j]) ok = false;
      CHECK(ok, "e2b: Cross-Device upload->download f32 bit-identisch");
      free(rt); moo_ki_gpu_buf_freigeben(tb); }

    /* ===== [4] GPU-RESTORE + weiter L == ununterbrochen K+L ===== */
    /* Restore-mv aus dem geladenen Optimizer (Host) rekonstruieren. */
    float* rmv[MAXP]; float* fin_int[MAXP]; float* fin_un[MAXP]; float* junk[MAXP];
    { MooValue ml = dget_(opt2, "m"), vl = dget_(opt2, "v");
      for (int i = 0; i < np; i++) {
          rmv[i]     = (float*)malloc((size_t)nn[i] * 2 * 4);
          fin_int[i] = (float*)malloc((size_t)nn[i] * 4);
          fin_un[i]  = (float*)malloc((size_t)nn[i] * 4);
          junk[i]    = (float*)malloc((size_t)nn[i] * 2 * 4);
          memcpy(rmv[i],         T(MV_LIST(ml)->items[i])->data, (size_t)nn[i] * 4);
          memcpy(rmv[i] + nn[i], T(MV_LIST(vl)->items[i])->data, (size_t)nn[i] * 4);
      }
      moo_release(ml); moo_release(vl); }

    /* Unterbrochen: restaurierte p (=dp) + restauriertes mv, weiter t=K+1..K+L. */
    float* rp[MAXP]; for (int i = 0; i < np; i++) rp[i] = dp[i];
    CHECK(device_train(np, nn, rp, rmv, K + 1, L, fin_int, junk),
          "e2b: GPU-Restore + weiter L Schritte");
    /* Ununterbrochen: init + Null-mv, t=1..K+L. */
    CHECK(device_train(np, nn, init_p, zero_mv, 1, K + L, fin_un, junk),
          "e2b: ununterbrochener Referenzlauf K+L Schritte");
    { bool ok = true; double maxd = 0.0;
      for (int i = 0; i < np; i++)
          for (int64_t j = 0; j < nn[i]; j++) {
              double d = fabs((double)fin_int[i][j] - (double)fin_un[i][j]);
              if (d > maxd) maxd = d;
              if (fin_int[i][j] != fin_un[i][j]) ok = false;
          }
      fprintf(stderr, "   [4] max|resume-uninterrupted| = %.3e\n", maxd);
      CHECK(ok, "e2b: GPU-Resume == ununterbrochen (bit-identisch, elementweiser Adam)"); }

    /* ===== [6] Alter reiner f32-E2-Checkpoint (ohne Device) bleibt ladbar ===== */
    { char pfad_cpu[] = "/tmp/moo_e2b_cpu_XXXXXX";
      int fdc = mkstemp(pfad_cpu); CHECK(fdc >= 0, "e2b: tmp cpu"); close(fdc);
      MooValue z6 = moo_dict_new();
      moo_retain(netz); moo_dict_set(z6, str_("netz"), netz);
      moo_dict_set(z6, str_("schritt"), num_(1));
      moo_release(ckpt_speichern_p(z6, pfad_cpu));
      CHECK(!moo_error_flag, "e2b: reiner f32-E2-Checkpoint geschrieben");
      MooValue z6l = ckpt_laden_p(pfad_cpu, moo_none());
      CHECK(!moo_error_flag && z6l.tag == MOO_DICT, "e2b: alter f32-E2-Checkpoint bleibt ladbar");
      moo_release(z6l); moo_release(z6); remove(pfad_cpu); }

    /* Aufraeumen (Smoke, kein ASan-Gate). */
    moo_release(prm2); moo_release(net2); moo_release(opt2); moo_release(z2);
    moo_release(opt); moo_release(params); moo_release(netz);
    for (int i = 0; i < np; i++) {
        free(init_p[i]); free(zero_mv[i]); free(dp[i]); free(dmv[i]);
        free(rmv[i]); free(fin_int[i]); free(fin_un[i]); free(junk[i]);
    }
    remove(pfad_dev);

    fprintf(stderr, "OK: %d Checks bestanden.\n", checks);
    return 0;
}
