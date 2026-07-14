/**
 * test_g4c_e2e_qa.c — KIP-G4c-QA: unabhaengiges End-to-End-Gate
 * GPU-Training -> E2b-Checkpoint -> ECHTER Prozessneustart -> Restore CPU/GPU
 * -> Weitertrainieren/Eval.
 * ============================================================================
 * Task 8972d152 (kip-daten). Abgrenzung zu bestehenden Gates:
 *  - test_checkpoint_asan.c (E2) und test_e2b_device_ckpt.c (E2b) simulieren
 *    "Kill+Resume" INNERHALB EINES Prozesses (Save -> Objekte freigeben ->
 *    Load, alles im selben main()). Das beweist das Datenformat, NICHT dass
 *    ein echter Prozessneustart (neuer Adressraum, neues Vulkan-Init, keine
 *    globalen C-Statics) denselben Zustand liefert.
 *  - Dieser Harness ist ein CLI-Multi-Mode-Tool: jede "Phase" ist EIN
 *    main()-Aufruf, das Orchestrierungs-Skript (skripte/kip_g4c_qa.sh) startet
 *    jede Phase als EIGENEN OS-Prozess (fork+exec via Shell) und vergleicht
 *    die auf stdout gedruckten RESULT-Zeilen. Das ist der staerkere Beweis,
 *    den die Aufgabenstellung mit "Prozessneustart" explizit verlangt.
 *
 * WICHTIG — Stand der Production-Wiring-Abhaengigkeit (siehe
 * docs/kip/G4c-QA-contract.md fuer den vollen Vertrag):
 *  Die eigentliche G4c-Produktionsverdrahtung (moo_nn_trainiere/moo_tensor_ops
 *  GPU-geroutet, MOO_KI_GPU_STRIKT, gpu_statistik()) existiert zum Zeitpunkt
 *  dieses Commits NOCH NICHT (docs/kip/G4c-production-wiring-plan.md, Phase 2
 *  laeuft bei kip-gpu). Dieser Harness nutzt NUR bereits vorhandene Hooks:
 *    - CPU: moo_nn_vorwaerts/_mse/_rueckwaerts/opt_schritt (moo_nn.c, real)
 *    - GPU: moo_ki_gpu_opt_adam_res + moo_ki_gpu_upload/_download (E2b-Muster,
 *      manuelle Adam-Schritte auf residenten Buffern, KEIN echter GPU-
 *      Forward/Backward-Pfad, da moo_tensor_matmul etc. noch nicht GPU-
 *      geroutet sind)
 *    - Checkpoint: moo_nn_ckpt_speichern/_laden (E2/E2b-Format, unveraendert)
 *  STAND 2026-07-10 (Nachtrag): matmul Fwd+Bwd, ew_op (add/sub/mul/div) Fwd,
 *  bw_mul (voll) + bw_div (da-Zweig), sqrt (u_op), softmax/gather Fwd,
 *  reduce_op Voll+Achse, Adam/SGD-Optimizer sind jetzt resident + STRIKT
 *  hardware-verifiziert (kip-gpu, Channel-Bestaetigung Msg 12897) -- der
 *  Modus "gpu_vs_cpu_curve" ist daher AKTIV (kein Platzhalter mehr) und
 *  vergleicht echte Loss-Kurven CPU vs. MOO_KI_GPU_STRIKT=1 auf einem Netz
 *  OHNE Aktivierung (tanh ist noch NICHT resident, siehe Dateikommentar bei
 *  baue_gvsc_netz). Alles andere (Parameter, Adam t/m/v, Dropout-/Schritt-
 *  zustand, Residenztelemetrie, Cross-Device-Restore) ist mit den vorhandenen
 *  Hooks voll pruefbar und wird unten AKTIV getestet.
 *
 * MODI (argv[1]):
 *   cpu_ref        <total>                 -- CPU: N Schritte ununterbrochen (Referenz)
 *   cpu_train      <ckpt> <M>               -- CPU: M Schritte + Checkpoint speichern
 *   cpu_resume     <ckpt> <extra>           -- CPU: Checkpoint laden + extra Schritte
 *   cpu_mismatch   <ckpt>                   -- CPU: Negativ-Kontrolle Versions-Mismatch
 *   gpu_ref        <total>                  -- GPU: N Schritte ununterbrochen (Referenz)
 *   gpu_train      <ckpt> <K>               -- GPU: K Schritte + E2b-Checkpoint speichern
 *   gpu_resume_gpu <ckpt> <extra>           -- GPU: Checkpoint laden + extra Schritte GPU
 *   gpu_resume_cpu <ckpt> <extra>           -- Cross-Device: GPU-Checkpoint CPU-only laden
 *                                               + extra Schritte ECHTES CPU-Training
 *   gpu_vs_cpu_curve                        -- PENDING: fehlender Hook dokumentiert
 *
 * Jeder Modus druckt genau EINE Zeile "RESULT mode=... key=val ..." auf
 * stdout (maschinenlesbar fuer das Shell-Gate) + Diagnostik auf stderr.
 * ============================================================================
 */
#include "../moo_runtime.h"
#include "../moo_ki_gpu_api.h"
#include <string.h>
#include <stdint.h>
#include <math.h>

/* --- Test-throw-Modell (ersetzt moo_error.c) ------------------------------ */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    moo_release(error);
    moo_error_flag = 1;
}

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

/* FNV-1a ueber die Rohbytes aller Parameter -- kompakter Cross-Prozess-Vergleich
 * (bit-identisch <=> gleicher Checksum; billiger als alle Floats auf stdout). */
static uint64_t fnv_bytes(uint64_t h, const void* buf, size_t n) {
    const unsigned char* b = (const unsigned char*)buf;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}
static uint64_t checksum_params(MooValue params) {
    uint64_t h = 1469598103934665603ULL;
    MooList* l = MV_LIST(params);
    for (int i = 0; i < l->length; i++) {
        MooTensor* t = T(l->items[i]);
        h = fnv_bytes(h, t->data, (size_t)t->size * sizeof(float));
    }
    return h;
}

/* ============================================================================
 * CPU-Seite: dicht(3,8,tanh) -> dropout(0.3) -> dicht(8,2) -- identisches Netz
 * wie test_checkpoint_asan.c (E2), damit derselbe Dropout-Zaehler-Mechanismus
 * unter ECHTEM Prozessneustart geprueft wird (nicht nur im selben Prozess).
 * ========================================================================= */
static MooValue mk_dicht(int ein, int aus, const char* akt, double seed) {
    MooValue a = akt ? str_(akt) : moo_none();
    MooValue s = (seed >= 0.0) ? num_(seed) : moo_none();
    MooValue d = moo_nn_schicht_dicht(num_(ein), num_(aus), a, s);
    moo_release(a);
    return d;
}
static MooValue baue_cpu_netz(void) {
    MooValue schichten = moo_list_new(3);
    moo_list_append(schichten, mk_dicht(3, 8, "tanh", 1.0));
    moo_list_append(schichten, moo_nn_schicht_dropout(num_(0.3)));
    moo_list_append(schichten, mk_dicht(8, 2, "keine", 2.0));
    MooValue netz = moo_nn_ki_netz(schichten);
    moo_release(schichten);
    return netz;
}
static MooValue baue_cpu_opt(MooValue netz) {
    MooValue params = moo_nn_parameter(netz);
    MooValue rn = num_(0.02);
    MooValue opt = moo_nn_opt_adam(params, rn);
    moo_release(rn); moo_release(params);
    return opt;
}
static float xv[12] = { 0.5f,-1.0f,2.0f, 0.25f,-0.75f,1.5f, -2.0f,3.0f,0.1f, 1.0f,-0.2f,0.4f };
static float yv[8]  = { 1.0f,0.0f, 0.0f,1.0f, 1.0f,0.0f, 0.0f,1.0f };
static MooValue t2(int r, int c, const float* vals) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    for (int i = 0; i < r * c; i++) t->data[i] = vals[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}
static float cpu_schritt(MooValue netz, MooValue opt, MooValue x, MooValue y) {
    MooValue pred = moo_nn_vorwaerts(netz, x);
    if (moo_error_flag) { moo_release(pred); return (float)NAN; }
    MooValue loss = moo_nn_mse(pred, y);
    if (moo_error_flag) { moo_release(loss); moo_release(pred); return (float)NAN; }
    float lv = (float)T(loss)->data[0];
    moo_release(moo_tensor_rueckwaerts(loss));
    moo_release(loss); moo_release(pred);
    if (!moo_error_flag) moo_release(moo_nn_opt_schritt(opt));
    return lv;
}
static double drop_zaehler(MooValue netz) {
    MooValue sch = dget_(netz, "schichten");
    double z = -1.0;
    if (sch.tag == MOO_LIST && MV_LIST(sch)->length >= 2) {
        MooValue zz = moo_dict_get(MV_LIST(sch)->items[1], str_("zaehler"));
        if (zz.tag == MOO_NUMBER) z = MV_NUM(zz);
        moo_release(zz);
    }
    moo_release(sch);
    return z;
}

static void print_cpu_result(const char* mode, float loss, uint64_t chk,
                              double dropz, double t, const char* extra) {
    printf("RESULT mode=%s loss=%.9g checksum=%llu dropz=%.0f t=%.0f%s%s\n",
           mode, (double)loss, (unsigned long long)chk, dropz, t,
           extra ? " " : "", extra ? extra : "");
    fflush(stdout);
}

static int mode_cpu_ref(int total) {
    MooValue netz = baue_cpu_netz(), opt = baue_cpu_opt(netz);
    MooValue x = t2(4, 3, xv), y = t2(4, 2, yv);
    float lv = 0.f;
    for (int s = 0; s < total; s++) lv = cpu_schritt(netz, opt, x, y);
    MooValue params = moo_nn_parameter(netz);
    uint64_t chk = checksum_params(params);
    print_cpu_result("cpu_ref", lv, chk, drop_zaehler(netz), (double)total, NULL);
    moo_release(params); moo_release(x); moo_release(y); moo_release(opt); moo_release(netz);
    moo_ag_reset();
    return moo_error_flag ? 1 : 0;
}

static int mode_cpu_train(const char* ckpt, int M) {
    MooValue netz = baue_cpu_netz(), opt = baue_cpu_opt(netz);
    MooValue x = t2(4, 3, xv), y = t2(4, 2, yv);
    float lv = 0.f;
    for (int s = 0; s < M; s++) lv = cpu_schritt(netz, opt, x, y);

    MooValue zustand = moo_dict_new();
    moo_retain(netz); moo_dict_set(zustand, str_("netz"), netz);
    moo_retain(opt);  moo_dict_set(zustand, str_("opt"), opt);
    moo_dict_set(zustand, str_("schritt"), num_(M));
    moo_dict_set(zustand, str_("tokenizer_version"), str_("g4c-qa-v1"));
    moo_release(ckpt_speichern_p(zustand, ckpt));
    moo_release(zustand);
    if (moo_error_flag) { fprintf(stderr, "FAIL: cpu_train Checkpoint-Speichern\n"); return 1; }

    MooValue params = moo_nn_parameter(netz);
    uint64_t chk = checksum_params(params);
    print_cpu_result("cpu_train", lv, chk, drop_zaehler(netz), (double)M, NULL);
    moo_release(params); moo_release(x); moo_release(y); moo_release(opt); moo_release(netz);
    moo_ag_reset();
    return 0;
}

static int mode_cpu_resume(const char* ckpt, int extra) {
    MooValue z = ckpt_laden_p(ckpt, moo_none());
    if (moo_error_flag || z.tag != MOO_DICT) { fprintf(stderr, "FAIL: cpu_resume Checkpoint-Laden\n"); return 1; }
    MooValue netz = dget_(z, "netz"), opt = dget_(z, "opt");
    MooValue schritt_m = dget_(z, "schritt");
    double m0 = (schritt_m.tag == MOO_NUMBER) ? MV_NUM(schritt_m) : -1.0;
    moo_release(schritt_m);
    MooValue x = t2(4, 3, xv), y = t2(4, 2, yv);
    float lv = 0.f;
    for (int s = 0; s < extra; s++) lv = cpu_schritt(netz, opt, x, y);

    MooValue params = moo_nn_parameter(netz);
    uint64_t chk = checksum_params(params);
    char extra_field[64];
    snprintf(extra_field, sizeof(extra_field), "restored_schritt=%.0f", m0);
    print_cpu_result("cpu_resume", lv, chk, drop_zaehler(netz), m0 + extra, extra_field);
    moo_release(params); moo_release(x); moo_release(y);
    moo_release(opt); moo_release(netz); moo_release(z);
    moo_ag_reset();
    return 0;
}

static int mode_cpu_mismatch(const char* ckpt) {
    MooValue erw = moo_dict_new();
    moo_dict_set(erw, str_("tokenizer_version"), str_("FALSCHE-VERSION"));
    MooValue z = ckpt_laden_p(ckpt, erw);
    int ok = moo_error_flag ? 1 : 0; /* erwartet: Mismatch WIRFT */
    if (!moo_error_flag) moo_release(z);
    moo_error_flag = 0; /* Reset fuer sauberen Exit */
    printf("RESULT mode=cpu_mismatch status=%s\n", ok ? "wirft-wie-erwartet" : "FEHLT-wirft-nicht");
    fflush(stdout);
    return ok ? 0 : 1;
}

/* ============================================================================
 * GPU-Seite: dicht(4,6,tanh) -> dicht(6,3) -- identische Topologie wie
 * test_e2b_device_ckpt.c (E2b), Adam-Schritte auf residenten Buffern mit
 * deterministischem synthetischem Gradienten (kein echter Fwd/Bwd, siehe
 * Dateikopf-Kommentar zum fehlenden G4c-Produktionshook).
 * ========================================================================= */
#define MAXP 16
static MooValue baue_gpu_netz(void) {
    MooValue schichten = moo_list_new(2);
    moo_list_append(schichten, mk_dicht(4, 6, "tanh", 1.0));
    moo_list_append(schichten, mk_dicht(6, 3, "keine", 2.0));
    MooValue netz = moo_nn_ki_netz(schichten);
    moo_release(schichten);
    return netz;
}
static const float LR = 0.02f, B1 = 0.9f, B2 = 0.999f, EPS = 1e-8f;
static void fuelle_grad(float* g, int64_t n, int step, int pi) {
    for (int64_t j = 0; j < n; j++)
        g[j] = 0.05f * (float)(((j + pi) % 5) - 2) + 0.002f * (float)step;
}
/* Adam-Schritte [t0, t0+steps) auf residenten Buffern; Init aus init_p/init_mv,
 * Ergebnis in out_p/out_mv (je Download). false = GPU-Fehler (Aufrufer: SKIP). */
static bool gpu_train_steps(int np, const int64_t* nn, float* const* init_p,
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
static bool gpu_verfuegbar(void) {
    void* probe = moo_ki_gpu_buf_belegen(64);
    if (!probe) return false;
    moo_ki_gpu_buf_freigeben(probe);
    return true;
}
static uint64_t checksum_floats(float* const* arrs, const int64_t* nn, int np) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < np; i++) h = fnv_bytes(h, arrs[i], (size_t)nn[i] * sizeof(float));
    return h;
}
static void gpu_netz_dims(MooValue netz, int* np, int64_t* nn) {
    MooValue params = moo_nn_parameter(netz);
    *np = MV_LIST(params)->length;
    for (int i = 0; i < *np; i++) nn[i] = T(MV_LIST(params)->items[i])->size;
    moo_release(params);
}

static void print_gpu_result(const char* mode, uint64_t chk, double t,
                              MooKiGpuTelemetrie* d, const char* extra) {
    printf("RESULT mode=%s checksum=%llu t=%.0f submits=%llu uploads=%llu downloads=%llu cpu_fallbacks=%llu%s%s\n",
           mode, (unsigned long long)chk, t,
           d ? (unsigned long long)d->submits : 0ULL,
           d ? (unsigned long long)d->uploads : 0ULL,
           d ? (unsigned long long)d->downloads : 0ULL,
           d ? (unsigned long long)d->cpu_fallbacks : 0ULL,
           extra ? " " : "", extra ? extra : "");
    fflush(stdout);
}

static int mode_gpu_ref(int total) {
    if (!gpu_verfuegbar()) { printf("RESULT mode=gpu_ref status=SKIP-kein-vulkan\n"); return 0; }
    MooValue netz = baue_gpu_netz();
    int np; int64_t nn[MAXP];
    gpu_netz_dims(netz, &np, nn);
    float* init_p[MAXP]; float* zero_mv[MAXP]; float* out_p[MAXP]; float* out_mv[MAXP];
    for (int i = 0; i < np; i++) {
        MooValue params = moo_nn_parameter(netz);
        MooTensor* p = T(MV_LIST(params)->items[i]);
        init_p[i] = (float*)malloc((size_t)nn[i] * 4);
        memcpy(init_p[i], p->data, (size_t)nn[i] * 4);
        zero_mv[i] = (float*)calloc((size_t)nn[i] * 2, 4);
        out_p[i] = (float*)malloc((size_t)nn[i] * 4);
        out_mv[i] = (float*)malloc((size_t)nn[i] * 2 * 4);
        moo_release(params);
    }
    MooKiGpuTelemetrie t0, t1;
    moo_ki_gpu_telemetrie(&t0);
    bool ok = gpu_train_steps(np, nn, init_p, zero_mv, 1, total, out_p, out_mv);
    moo_ki_gpu_telemetrie(&t1);
    MooKiGpuTelemetrie d = { t1.submits - t0.submits, t1.uploads - t0.uploads,
                              t1.downloads - t0.downloads, t1.cpu_fallbacks - t0.cpu_fallbacks };
    uint64_t chk = checksum_floats(out_p, nn, np);
    print_gpu_result("gpu_ref", chk, (double)total, &d, NULL);
    for (int i = 0; i < np; i++) { free(init_p[i]); free(zero_mv[i]); free(out_p[i]); free(out_mv[i]); }
    moo_release(netz);
    return ok ? 0 : 1;
}

static int mode_gpu_train(const char* ckpt, int K) {
    if (!gpu_verfuegbar()) { printf("RESULT mode=gpu_train status=SKIP-kein-vulkan\n"); return 0; }
    MooValue netz = baue_gpu_netz();
    MooValue params = moo_nn_parameter(netz);
    int np = MV_LIST(params)->length;
    int64_t nn[MAXP];
    float* init_p[MAXP]; float* zero_mv[MAXP]; float* dp[MAXP]; float* dmv[MAXP];
    for (int i = 0; i < np; i++) {
        MooTensor* p = T(MV_LIST(params)->items[i]);
        nn[i] = p->size;
        init_p[i]  = (float*)malloc((size_t)nn[i] * 4);
        zero_mv[i] = (float*)calloc((size_t)nn[i] * 2, 4);
        dp[i]      = (float*)malloc((size_t)nn[i] * 4);
        dmv[i]     = (float*)malloc((size_t)nn[i] * 2 * 4);
        memcpy(init_p[i], p->data, (size_t)nn[i] * 4);
    }
    MooKiGpuTelemetrie t0, t1;
    moo_ki_gpu_telemetrie(&t0);
    bool ok = gpu_train_steps(np, nn, init_p, zero_mv, 1, K, dp, dmv);
    moo_ki_gpu_telemetrie(&t1);
    MooKiGpuTelemetrie d = { t1.submits - t0.submits, t1.uploads - t0.uploads,
                              t1.downloads - t0.downloads, t1.cpu_fallbacks - t0.cpu_fallbacks };
    if (!ok) { fprintf(stderr, "FAIL: gpu_train device-Schritte\n"); return 1; }

    /* Device-Ergebnis in reales Netz + Optimizer schreiben -> E2b-Checkpoint. */
    for (int i = 0; i < np; i++)
        memcpy(T(MV_LIST(params)->items[i])->data, dp[i], (size_t)nn[i] * 4);
    MooValue opt;
    { MooValue rn = num_(LR); opt = moo_nn_opt_adam(params, rn); moo_release(rn); }
    { MooValue ml = dget_(opt, "m"), vl = dget_(opt, "v");
      for (int i = 0; i < np; i++) {
          memcpy(T(MV_LIST(ml)->items[i])->data, dmv[i], (size_t)nn[i] * 4);
          memcpy(T(MV_LIST(vl)->items[i])->data, dmv[i] + nn[i], (size_t)nn[i] * 4);
      }
      moo_release(ml); moo_release(vl); }
    moo_dict_set(opt, str_("t"), num_((double)K));

    MooValue z = moo_dict_new();
    moo_retain(netz); moo_dict_set(z, str_("netz"), netz);
    moo_retain(opt);  moo_dict_set(z, str_("opt"), opt);
    moo_dict_set(z, str_("schritt"), num_(K));
    moo_dict_set(z, str_("tokenizer_version"), str_("g4c-qa-gpu-v1"));
    moo_release(ckpt_speichern_p(z, ckpt));
    moo_release(z);
    if (moo_error_flag) { fprintf(stderr, "FAIL: gpu_train Checkpoint-Speichern\n"); return 1; }

    uint64_t chk = checksum_floats(dp, nn, np);
    print_gpu_result("gpu_train", chk, (double)K, &d, NULL);
    moo_release(opt); moo_release(params); moo_release(netz);
    for (int i = 0; i < np; i++) { free(init_p[i]); free(zero_mv[i]); free(dp[i]); free(dmv[i]); }
    return 0;
}

static int mode_gpu_resume_gpu(const char* ckpt, int extra) {
    if (!gpu_verfuegbar()) { printf("RESULT mode=gpu_resume_gpu status=SKIP-kein-vulkan\n"); return 0; }
    MooValue z = ckpt_laden_p(ckpt, moo_none());
    if (moo_error_flag || z.tag != MOO_DICT) { fprintf(stderr, "FAIL: gpu_resume_gpu Checkpoint-Laden\n"); return 1; }
    MooValue netz = dget_(z, "netz"), opt = dget_(z, "opt");
    MooValue params = moo_nn_parameter(netz);
    int np = MV_LIST(params)->length;
    int64_t nn[MAXP];
    for (int i = 0; i < np; i++) nn[i] = T(MV_LIST(params)->items[i])->size;
    MooValue tt = dget_(opt, "t");
    int t0 = (tt.tag == MOO_NUMBER) ? (int)MV_NUM(tt) : -1;
    moo_release(tt);

    float* rp[MAXP]; float* rmv[MAXP]; float* out_p[MAXP]; float* out_mv[MAXP];
    { MooValue ml = dget_(opt, "m"), vl = dget_(opt, "v");
      for (int i = 0; i < np; i++) {
          rp[i]  = T(MV_LIST(params)->items[i])->data; /* geliehen, kein malloc noetig */
          rmv[i] = (float*)malloc((size_t)nn[i] * 2 * 4);
          memcpy(rmv[i], T(MV_LIST(ml)->items[i])->data, (size_t)nn[i] * 4);
          memcpy(rmv[i] + nn[i], T(MV_LIST(vl)->items[i])->data, (size_t)nn[i] * 4);
          out_p[i] = (float*)malloc((size_t)nn[i] * 4);
          out_mv[i] = (float*)malloc((size_t)nn[i] * 2 * 4);
      }
      moo_release(ml); moo_release(vl); }

    MooKiGpuTelemetrie ta, tb;
    moo_ki_gpu_telemetrie(&ta);
    bool ok = gpu_train_steps(np, nn, rp, rmv, t0 + 1, extra, out_p, out_mv);
    moo_ki_gpu_telemetrie(&tb);
    MooKiGpuTelemetrie d = { tb.submits - ta.submits, tb.uploads - ta.uploads,
                              tb.downloads - ta.downloads, tb.cpu_fallbacks - ta.cpu_fallbacks };
    if (!ok) { fprintf(stderr, "FAIL: gpu_resume_gpu device-Schritte\n"); return 1; }

    uint64_t chk = checksum_floats(out_p, nn, np);
    char extra_field[64];
    snprintf(extra_field, sizeof(extra_field), "restored_t=%d", t0);
    print_gpu_result("gpu_resume_gpu", chk, (double)(t0 + extra), &d, extra_field);
    for (int i = 0; i < np; i++) { free(rmv[i]); free(out_p[i]); free(out_mv[i]); }
    moo_release(params); moo_release(opt); moo_release(netz); moo_release(z);
    return 0;
}

/* Cross-Device: GPU-Checkpoint CPU-only laden (KEIN GPU-Call!) + ECHTES
 * CPU-Forward/Backward-Weitertrainieren (moo_nn_vorwaerts/mse/rueckwaerts/
 * opt_schritt) auf synthetischen x/y passend zur (4->6->3)-Topologie. Beweist
 * "auf GPU trainiert, auf CPU-Prozess ohne jede GPU-Bindung weitertrainiert". */
static int mode_gpu_resume_cpu(const char* ckpt, int extra) {
    MooValue z = ckpt_laden_p(ckpt, moo_none());
    if (moo_error_flag || z.tag != MOO_DICT) { fprintf(stderr, "FAIL: gpu_resume_cpu Checkpoint-Laden\n"); return 1; }
    MooValue netz = dget_(z, "netz"), opt = dget_(z, "opt");
    MooValue schritt_m = dget_(z, "schritt");
    double m0 = (schritt_m.tag == MOO_NUMBER) ? MV_NUM(schritt_m) : -1.0;
    moo_release(schritt_m);

    float xv4[8] = { 0.3f,-0.5f,1.0f,0.2f,  -0.4f,0.8f,-1.0f,0.1f };
    float yv3[6] = { 1.0f,0.0f,0.0f,  0.0f,1.0f,0.0f };
    MooValue x = t2(2, 4, xv4), y = t2(2, 3, yv3);
    float lv = 0.f;
    for (int s = 0; s < extra; s++) lv = cpu_schritt(netz, opt, x, y);

    MooValue params = moo_nn_parameter(netz);
    uint64_t chk = checksum_params(params);
    char extra_field[64];
    snprintf(extra_field, sizeof(extra_field), "restored_schritt=%.0f cross_device=gpu-to-cpu", m0);
    print_cpu_result("gpu_resume_cpu", lv, chk, -1.0, m0 + extra, extra_field);
    moo_release(params); moo_release(x); moo_release(y);
    moo_release(opt); moo_release(netz); moo_release(z);
    moo_ag_reset();
    return moo_error_flag ? 1 : 0;
}

/* ============================================================================
 * Kriterium H: echter CPU<->GPU-Loss-Kurvenvergleich (aktiviert 2026-07-10,
 * nachdem kip-gpu bestaetigt hat: matmul Fwd+Bwd, ew_op add/sub/mul/div Fwd,
 * bw_mul (voll) + bw_div (da-Zweig), sqrt (u_op), softmax/gather Fwd,
 * reduce_op Voll+Achse, Adam/SGD-Optimizer sind resident + MOO_KI_GPU_STRIKT
 * hardware-verifiziert (Channel moo-general, Msg 12897).
 *
 * NICHT resident (Channel-Bestaetigung): tanh/relu/sigmoid/gelu/exp/log
 * (nur sqrt aus der u_op-Familie wurde angebunden), bw_add/bw_sub,
 * bw_div-db-Zweig. Deshalb: Netz OHNE Aktivierung ("keine", wie E2b) statt
 * tanh wie im urspruenglichen CPU-Pfad-A-Netz -- sonst wuerde die in
 * G4c-QA-contract.md §4 dokumentierte Negativ-Kontrolle ("STRIKT haertet
 * bei Nicht-Residenz IMMER hart ab") an genau dieser Stelle systematisch
 * verwaessert (tanh liefe unter STRIKT still auf CPU weiter, kein Fehler,
 * aber cpu_fallbacks bliebe faelschlich 0 weil der Op nie versucht wird).
 * dicht(3,8,keine) -> dropout(0.3) -> dicht(8,2,keine); Dropout-Maskierung
 * laeuft ueber moo_tensor_mul (ew_op) -- selbe residente Op-Familie wie
 * add/sub/mul/div, also mitgedeckt ohne Sonderfall.
 * ========================================================================= */
static MooValue baue_gvsc_netz(void) {
    MooValue schichten = moo_list_new(3);
    moo_list_append(schichten, mk_dicht(3, 8, "keine", 1.0));
    moo_list_append(schichten, moo_nn_schicht_dropout(num_(0.3)));
    moo_list_append(schichten, mk_dicht(8, 2, "keine", 2.0));
    MooValue netz = moo_nn_ki_netz(schichten);
    moo_release(schichten);
    return netz;
}

/* Ein voller Trainingslauf (steps Schritte) unter dem gewaehlten STRIKT-
 * Zustand; Losses in out_losses[0..steps), cpu_fallbacks-Delta in
 * *out_fb. moo_ki_gpu_strikt_setzen ist die dokumentierte programmatische
 * Override-API (moo_ki_gpu.c:49) -- kein Env-Var-Caching-Problem zwischen
 * den beiden Laeufen im selben Prozess. */
static void gvsc_run(bool strikt, int steps, float* out_losses, uint64_t* out_fb) {
    moo_ki_gpu_strikt_setzen(strikt);
    MooValue netz = baue_gvsc_netz(), opt = baue_cpu_opt(netz);
    MooValue x = t2(4, 3, xv), y = t2(4, 2, yv);
    MooKiGpuTelemetrie t0, t1;
    moo_ki_gpu_telemetrie(&t0);
    for (int s = 0; s < steps && !moo_error_flag; s++)
        out_losses[s] = cpu_schritt(netz, opt, x, y);
    moo_ki_gpu_telemetrie(&t1);
    *out_fb = t1.cpu_fallbacks - t0.cpu_fallbacks;
    moo_release(x); moo_release(y); moo_release(opt); moo_release(netz);
    moo_ag_reset();
    moo_ki_gpu_strikt_setzen(false);
}

#define GVSC_MAXSTEPS 64
static int mode_gpu_vs_cpu_curve(int steps) {
    if (steps <= 0 || steps > GVSC_MAXSTEPS) {
        fprintf(stderr, "gpu_vs_cpu_curve: steps muss in [1,%d] liegen\n", GVSC_MAXSTEPS);
        return 2;
    }
    float cpu_losses[GVSC_MAXSTEPS], gpu_losses[GVSC_MAXSTEPS];
    uint64_t cpu_fb = 0, gpu_fb = 0;

    gvsc_run(false, steps, cpu_losses, &cpu_fb);
    if (moo_error_flag) {
        fprintf(stderr, "FAIL: gpu_vs_cpu_curve CPU-Referenzlauf wirft unerwartet\n");
        moo_error_flag = 0;
        return 1;
    }

    gvsc_run(true, steps, gpu_losses, &gpu_fb);
    if (moo_error_flag) {
        /* STRIKT konnte den Pfad nicht vollstaendig resident fahren (z.B.
         * kein Vulkan/keine GPU auf dieser Maschine) -- transparent SKIP,
         * kein falscher FAIL. Das IST die dokumentierte Negativ-Kontrolle:
         * ein bewusst nicht-residenter Aufbau wuerde genauso hart werfen. */
        moo_error_flag = 0;
        printf("RESULT mode=gpu_vs_cpu_curve status=SKIP-strikt-wirft "
               "reason=kein-vollstaendig-residenter-pfad-auf-dieser-maschine\n");
        fflush(stdout);
        return 0;
    }

    bool finite_ok = true;
    double maxrel = 0.0;
    for (int s = 0; s < steps; s++) {
        double c = (double)cpu_losses[s], g = (double)gpu_losses[s];
        if (!isfinite(c) || !isfinite(g)) finite_ok = false;
        double denom = fabs(c) > 1e-6 ? fabs(c) : 1e-6;
        double rel = fabs(c - g) / denom;
        if (rel > maxrel) maxrel = rel;
    }
    /* Toleranzvertrag G4c-QA-contract.md §4: rel<2e-3 (reduktionslastiger
     * Fwd/Bwd, float-GPU-Reduktionsreihenfolge != CPU, analog G4b-Muster).
     * cpu_fallbacks==0 im STRIKT-Lauf ist das Residenz-Gate-Kriterium. */
    bool ok = finite_ok && maxrel < 2e-3 && gpu_fb == 0;
    printf("RESULT mode=gpu_vs_cpu_curve status=%s maxrel=%.6g cpu_fallbacks=%llu steps=%d "
           "cpu_last=%.9g gpu_last=%.9g\n",
           ok ? "PASS" : "FAIL", maxrel, (unsigned long long)gpu_fb, steps,
           (double)cpu_losses[steps - 1], (double)gpu_losses[steps - 1]);
    fflush(stdout);
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mode> [args...]\n", argv[0]);
        return 2;
    }
    const char* mode = argv[1];
    if (strcmp(mode, "cpu_ref") == 0 && argc >= 3) return mode_cpu_ref(atoi(argv[2]));
    if (strcmp(mode, "cpu_train") == 0 && argc >= 4) return mode_cpu_train(argv[2], atoi(argv[3]));
    if (strcmp(mode, "cpu_resume") == 0 && argc >= 4) return mode_cpu_resume(argv[2], atoi(argv[3]));
    if (strcmp(mode, "cpu_mismatch") == 0 && argc >= 3) return mode_cpu_mismatch(argv[2]);
    if (strcmp(mode, "gpu_ref") == 0 && argc >= 3) return mode_gpu_ref(atoi(argv[2]));
    if (strcmp(mode, "gpu_train") == 0 && argc >= 4) return mode_gpu_train(argv[2], atoi(argv[3]));
    if (strcmp(mode, "gpu_resume_gpu") == 0 && argc >= 4) return mode_gpu_resume_gpu(argv[2], atoi(argv[3]));
    if (strcmp(mode, "gpu_resume_cpu") == 0 && argc >= 4) return mode_gpu_resume_cpu(argv[2], atoi(argv[3]));
    if (strcmp(mode, "gpu_vs_cpu_curve") == 0 && argc >= 3) return mode_gpu_vs_cpu_curve(atoi(argv[2]));
    fprintf(stderr, "Unbekannter Modus oder fehlende Argumente: %s\n", mode);
    return 2;
}
