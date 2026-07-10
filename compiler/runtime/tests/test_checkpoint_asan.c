/**
 * test_checkpoint_asan.c — KIP-E2 CPU-Voll-Checkpoint (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES). Quell-Satz wie test_nn_asan.
 * Test-throw-Modell (ersetzt moo_error.c).
 *
 * KONVENTION: moo_nn_ckpt_* sind DOMAENEN-Builtins -> Args GELIEHEN. Im echten
 * Programm released der Codegen-Arm die Heap-Args; im Standalone muessen WIR die
 * uebergebenen str_()-/num_()-Temporaries selbst freigeben (ckpt_*_p-Helfer).
 *
 * GATES (Task ca932b39):
 *   1. KILL+RESUME BIT-IDENTISCH *MIT DROPOUT*: ununterbrochenes Training ueber
 *      N Schritte vs. Kill nach M + Resume liefert BIT-gleiche Verlustkurve.
 *      Dropout-Zaehler (mutierender Layer-State) + Optimizer (m/v/t) + Gewichte
 *      werden exakt wiederhergestellt.
 *   2. NEGATIV-KONTROLLE: Dropout-Zaehler NICHT restauriert (=0) -> Kurve
 *      divergiert -> beweist, dass der Zaehler zwingend ist.
 *   3. STRUKTUR: nach Resume Dropout-Zaehler==M und Optimizer-t==M.
 *   4. ROTATION: letzte N + best behalten, aeltere geloescht.
 *   5. ATOMARITAET: Abbruch beim Schreiben (tmp nicht anlegbar) zerstoert den
 *      vorhandenen Checkpoint NICHT.
 *   6. VERSIONS-MISMATCH: falsche tokenizer_version wirft erklaerend; passende
 *      laedt; Metadaten (schritt/tokenizer_version/dataloader) roundtrippen.
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* --- Test-throw-Modell ---------------------------------------------------- */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
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

/* Domaenen-Arg-Helfer: Pfad-/Praefix-Temporaries selbst releasen. */
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
static MooValue ckpt_rotieren_p(const char* dir, const char* prx, int keep) {
    MooValue d = str_(dir), p = str_(prx), n = num_(keep);
    MooValue r = moo_nn_ckpt_rotieren(d, p, n);
    moo_release(d); moo_release(p); moo_release(n);
    return r;
}

static MooValue t2(int r, int c, const float* vals) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    for (int i = 0; i < r * c; i++) t->data[i] = vals[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

/* dicht mit Aktivierung (Aktivierungs-String borrowed -> selbst freigeben). */
static MooValue mk_dicht(int ein, int aus, const char* akt, double seed) {
    MooValue a = akt ? str_(akt) : moo_none();
    MooValue s = (seed >= 0.0) ? num_(seed) : moo_none();
    MooValue d = moo_nn_schicht_dicht(num_(ein), num_(aus), a, s);
    moo_release(a);
    return d;
}

/* Frisches, deterministisches Netz: dicht(3,8,tanh) -> dropout(0.3) -> dicht(8,2). */
static MooValue baue_netz(void) {
    MooValue schichten = moo_list_new(3);
    moo_list_append(schichten, mk_dicht(3, 8, "tanh", 1.0));
    moo_list_append(schichten, moo_nn_schicht_dropout(num_(0.3)));
    moo_list_append(schichten, mk_dicht(8, 2, "keine", 2.0));
    MooValue netz = moo_nn_ki_netz(schichten);
    moo_release(schichten);
    return netz;
}

/* Adam(0.02) fuer die Netz-Parameter. */
static MooValue baue_opt(MooValue netz) {
    MooValue params = moo_nn_parameter(netz);
    MooValue rn = num_(0.02);
    MooValue opt = moo_nn_opt_adam(params, rn);
    moo_release(rn); moo_release(params);
    return opt;
}

/* Ein Trainingsschritt (Dropout aktiv) -> Verlust VOR dem Update. */
static float schritt(MooValue netz, MooValue opt, MooValue x, MooValue y) {
    MooValue pred = moo_nn_vorwaerts(netz, x);
    MooValue loss = moo_nn_mse(pred, y);
    float lv = (float)T(loss)->data[0];
    moo_release(moo_tensor_rueckwaerts(loss));
    moo_release(loss); moo_release(pred);
    moo_release(moo_nn_opt_schritt(opt));
    return lv;
}

/* Dropout-Schicht (Index 1) eines Netzes: zaehler lesen/setzen. */
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
static void drop_zaehler_setzen(MooValue netz, double z) {
    MooValue sch = dget_(netz, "schichten");
    if (sch.tag == MOO_LIST && MV_LIST(sch)->length >= 2)
        moo_dict_set(MV_LIST(sch)->items[1], str_("zaehler"), num_(z));
    moo_release(sch);
}

/* KIP-E2c: Referenz-Spiegel von d3_f32_zu_bf16 + d3_bf16_zu_f32 (moo_nn_easy.c,
 * dort static). Geladenes bf16-Gewicht == ref_bf16_round(original), bit-exakt. */
static float ref_bf16_round(float f) {
    uint32_t x; memcpy(&x, &f, sizeof(x));
    uint16_t bf;
    if (((x >> 23) & 0xFFu) == 0xFFu && (x & 0x7FFFFFu)) {
        bf = (uint16_t)((x >> 16) | 0x0040u);
    } else {
        uint32_t bias = 0x7FFFu + ((x >> 16) & 1u);
        x += bias;
        bf = (uint16_t)(x >> 16);
    }
    uint32_t y = (uint32_t)bf << 16;
    float out; memcpy(&out, &y, sizeof(out));
    return out;
}

#define N 12
#define M 6

int main(void) {
    fprintf(stderr, "== KIP-E2 CPU-Voll-Checkpoint Harness ==\n");

    /* Feste Trainingsdaten. */
    float xv[12] = { 0.5f,-1.0f,2.0f, 0.25f,-0.75f,1.5f, -2.0f,3.0f,0.1f, 1.0f,-0.2f,0.4f };
    float yv[8]  = { 1.0f,0.0f, 0.0f,1.0f, 1.0f,0.0f, 0.0f,1.0f };

    /* ===== Referenz: N Schritte ununterbrochen ===== */
    float ref[N];
    {
        MooValue netz = baue_netz();
        MooValue opt  = baue_opt(netz);
        MooValue x = t2(4, 3, xv), y = t2(4, 2, yv);
        for (int s = 0; s < N; s++) ref[s] = schritt(netz, opt, x, y);
        moo_release(x); moo_release(y); moo_release(opt); moo_release(netz);
        moo_ag_reset();
    }
    CHECK(ref[0] != ref[N-1], "Referenz: Training bewegt den Verlust");

    char pfad[] = "/tmp/moo_ckpt_XXXXXX";
    int fd = mkstemp(pfad); CHECK(fd >= 0, "tempdatei"); close(fd);

    /* ===== Resume: M Schritte, Checkpoint, Kill, Reload, weiter bis N ===== */
    float res[N];
    {
        MooValue netz = baue_netz();
        MooValue opt  = baue_opt(netz);
        MooValue x = t2(4, 3, xv), y = t2(4, 2, yv);
        for (int s = 0; s < M; s++) res[s] = schritt(netz, opt, x, y);

        /* Checkpoint bauen (netz + opt + Meta + Dataloader-Position). */
        MooValue zustand = moo_dict_new();
        moo_retain(netz); moo_dict_set(zustand, str_("netz"), netz);
        moo_retain(opt);  moo_dict_set(zustand, str_("opt"), opt);
        moo_dict_set(zustand, str_("schritt"), num_(M));
        moo_dict_set(zustand, str_("tokenizer_version"), str_("tok-v1"));
        MooValue dl = moo_dict_new();
        moo_dict_set(dl, str_("seed"), num_(1234));
        moo_dict_set(dl, str_("pos"),  num_(M));
        moo_dict_set(zustand, str_("dataloader"), dl);
        moo_release(ckpt_speichern_p(zustand, pfad));
        CHECK(!moo_error_flag, "checkpoint_speichern ok");
        moo_release(zustand);

        /* --- KILL: gesamten Trainingszustand freigeben --- */
        moo_release(netz); moo_release(opt); moo_release(x); moo_release(y);
        moo_ag_reset();
    }

    /* --- RESUME: Checkpoint laden --- */
    MooValue z2 = ckpt_laden_p(pfad, moo_none());
    CHECK(!moo_error_flag && z2.tag == MOO_DICT, "checkpoint_laden ok");
    MooValue net2 = dget_(z2, "netz");
    MooValue opt2 = dget_(z2, "opt");
    CHECK(net2.tag == MOO_DICT && opt2.tag == MOO_DICT, "netz+opt aus Checkpoint");

    /* STRUKTUR: Dropout-Zaehler==M, Optimizer-t==M. */
    CHECK(drop_zaehler(net2) == (double)M, "Dropout-Zaehler == M restauriert");
    { MooValue tt = dget_(opt2, "t");
      CHECK(tt.tag == MOO_NUMBER && MV_NUM(tt) == (double)M, "Optimizer-t == M restauriert");
      moo_release(tt); }
    /* Metadaten roundtrippen. */
    { MooValue sm = dget_(z2, "schritt");
      CHECK(sm.tag == MOO_NUMBER && MV_NUM(sm) == (double)M, "schritt roundtrip"); moo_release(sm); }
    { MooValue tv = dget_(z2, "tokenizer_version");
      CHECK(tv.tag == MOO_STRING && strcmp(MV_STR(tv)->chars, "tok-v1") == 0, "tokenizer_version roundtrip"); moo_release(tv); }
    { MooValue dl = dget_(z2, "dataloader");
      CHECK(dl.tag == MOO_DICT, "dataloader roundtrip");
      MooValue pos = moo_dict_get(dl, str_("pos"));
      CHECK(pos.tag == MOO_NUMBER && MV_NUM(pos) == (double)M, "dataloader.pos == M");
      moo_release(pos); moo_release(dl); }

    /* Weiter bis N -> Kurve muss BIT-identisch zur Referenz sein. */
    {
        MooValue x = t2(4, 3, xv), y = t2(4, 2, yv);
        for (int s = M; s < N; s++) res[s] = schritt(net2, opt2, x, y);
        moo_release(x); moo_release(y);
        moo_ag_reset();
    }
    { bool gleich = true;
      for (int s = 0; s < N; s++) if (res[s] != ref[s]) gleich = false;
      CHECK(gleich, "Kill+Resume: Verlustkurve BIT-identisch (MIT Dropout)"); }
    moo_release(net2); moo_release(opt2); moo_release(z2);

    /* ===== NEGATIV-KONTROLLE: Dropout-Zaehler NICHT restauriert -> divergiert ===== */
    {
        MooValue z3 = ckpt_laden_p(pfad, moo_none());
        MooValue net3 = dget_(z3, "netz");
        MooValue opt3 = dget_(z3, "opt");
        drop_zaehler_setzen(net3, 0.0);   /* naiver Checkpoint ohne Zaehler */
        float bad[N];
        MooValue x = t2(4, 3, xv), y = t2(4, 2, yv);
        for (int s = M; s < N; s++) bad[s] = schritt(net3, opt3, x, y);
        moo_release(x); moo_release(y); moo_ag_reset();
        bool divergiert = false;
        for (int s = M; s < N; s++) if (bad[s] != ref[s]) divergiert = true;
        CHECK(divergiert, "Negativ: ohne Dropout-Zaehler divergiert die Kurve");
        moo_release(net3); moo_release(opt3); moo_release(z3);
    }

    /* ===== VERSIONS-MISMATCH ===== */
    {
        MooValue erw = moo_dict_new();
        moo_dict_set(erw, str_("tokenizer_version"), str_("tok-v2"));   /* falsch */
        MooValue bad = ckpt_laden_p(pfad, erw);
        CHECK(moo_error_flag && bad.tag == MOO_NONE, "Tokenizer-Versions-Mismatch wirft");
        fehler_reset();
        moo_release(erw);

        MooValue erw2 = moo_dict_new();
        moo_dict_set(erw2, str_("tokenizer_version"), str_("tok-v1"));  /* passt */
        MooValue gut = ckpt_laden_p(pfad, erw2);
        CHECK(!moo_error_flag && gut.tag == MOO_DICT, "passende Version laedt ohne Fehler");
        moo_release(gut); moo_release(erw2);
    }

    /* ===== ATOMARITAET: Abbruch zerstoert alten Checkpoint nicht ===== */
    {
        char dir[] = "/tmp/moo_ckpt_atom_XXXXXX";
        CHECK(mkdtemp(dir) != NULL, "atom: tempdir");
        char pA[300]; snprintf(pA, sizeof(pA), "%s/ckpt.mook", dir);

        /* Gueltigen Checkpoint A schreiben. */
        MooValue netz = baue_netz();
        MooValue zustand = moo_dict_new();
        moo_retain(netz); moo_dict_set(zustand, str_("netz"), netz);
        moo_dict_set(zustand, str_("schritt"), num_(1));
        moo_release(ckpt_speichern_p(zustand, pA));
        CHECK(!moo_error_flag, "atom: A geschrieben");

        /* A laden -> ein Gewicht merken. */
        MooValue zA = ckpt_laden_p(pA, moo_none());
        MooValue nA = dget_(zA, "netz");
        MooValue prmA = moo_nn_parameter(nA);
        float wA0 = T(MV_LIST(prmA)->items[0])->data[0];
        moo_release(prmA); moo_release(nA); moo_release(zA);

        /* tmp-Pfad als VERZEICHNIS anlegen -> fopen("<pA>.tmp","wb") scheitert. */
        char tmpdir[320]; snprintf(tmpdir, sizeof(tmpdir), "%s.tmp", pA);
        CHECK(mkdir(tmpdir, 0700) == 0, "atom: tmp-Verzeichnis blockiert");
        MooValue r = ckpt_speichern_p(zustand, pA);
        CHECK(moo_error_flag && r.tag == MOO_NONE, "atom: Abbruch beim Schreiben wirft");
        fehler_reset();
        rmdir(tmpdir);

        /* A ist unveraendert ladbar (gleiches Gewicht). */
        MooValue zA2 = ckpt_laden_p(pA, moo_none());
        CHECK(!moo_error_flag && zA2.tag == MOO_DICT, "atom: alter Checkpoint intakt");
        MooValue nA2 = dget_(zA2, "netz");
        MooValue prmA2 = moo_nn_parameter(nA2);
        CHECK(T(MV_LIST(prmA2)->items[0])->data[0] == wA0, "atom: alter Checkpoint bit-unveraendert");
        moo_release(prmA2); moo_release(nA2); moo_release(zA2);

        moo_release(zustand); moo_release(netz);
        remove(pA); rmdir(dir);
    }

    /* ===== ROTATION: letzte 3 + best behalten ===== */
    {
        char dir[] = "/tmp/moo_ckpt_rot_XXXXXX";
        CHECK(mkdtemp(dir) != NULL, "rot: tempdir");
        char nm[400];
        for (int s = 1; s <= 5; s++) {
            snprintf(nm, sizeof(nm), "%s/ckpt_%d.mook", dir, s);
            FILE* f = fopen(nm, "wb"); if (f) { fputc('x', f); fclose(f); }
        }
        snprintf(nm, sizeof(nm), "%s/ckpt_best.mook", dir);
        { FILE* f = fopen(nm, "wb"); if (f) { fputc('x', f); fclose(f); } }

        MooValue del = ckpt_rotieren_p(dir, "ckpt_", 3);
        CHECK(!moo_error_flag && del.tag == MOO_NUMBER && (int)MV_NUM(del) == 2,
              "rotation: 2 aelteste geloescht"); moo_release(del);

        int da[6]; /* Existenz ckpt_1..5 + best */
        for (int s = 1; s <= 5; s++) {
            snprintf(nm, sizeof(nm), "%s/ckpt_%d.mook", dir, s);
            FILE* f = fopen(nm, "rb"); da[s] = (f != NULL); if (f) fclose(f);
        }
        snprintf(nm, sizeof(nm), "%s/ckpt_best.mook", dir);
        { FILE* f = fopen(nm, "rb"); da[0] = (f != NULL); if (f) fclose(f); }
        CHECK(!da[1] && !da[2], "rotation: ckpt_1/2 geloescht");
        CHECK(da[3] && da[4] && da[5], "rotation: ckpt_3/4/5 behalten");
        CHECK(da[0], "rotation: best-Datei bleibt immer");

        for (int s = 3; s <= 5; s++) { snprintf(nm, sizeof(nm), "%s/ckpt_%d.mook", dir, s); remove(nm); }
        snprintf(nm, sizeof(nm), "%s/ckpt_best.mook", dir); remove(nm);
        rmdir(dir);
    }

    /* ===== KIP-E2c: bf16-Gewichts-Checkpoint (Storage-Option, nur p*) ===== */
    {
        char pf[] = "/tmp/moo_ckpt_f32_XXXXXX";
        int fdf = mkstemp(pf); CHECK(fdf >= 0, "e2c: tmp f32"); close(fdf);
        char pb[] = "/tmp/moo_ckpt_bf16_XXXXXX";
        int fdb = mkstemp(pb); CHECK(fdb >= 0, "e2c: tmp bf16"); close(fdb);

        MooValue netz = baue_netz();
        MooValue prm = moo_nn_parameter(netz);
        MooTensor* p0 = T(MV_LIST(prm)->items[0]);
        int64_t n0 = p0->size;
        float* orig = (float*)malloc((size_t)n0 * sizeof(float));
        for (int64_t j = 0; j < n0; j++) orig[j] = p0->data[j];
        moo_release(prm);

        MooValue zustand = moo_dict_new();
        moo_retain(netz); moo_dict_set(zustand, str_("netz"), netz);
        moo_dict_set(zustand, str_("schritt"), num_(3));

        /* f32-Default (env AUS). */
        unsetenv("MOO_KI_CKPT_BF16");
        moo_release(ckpt_speichern_p(zustand, pf));
        CHECK(!moo_error_flag, "e2c: f32-Checkpoint schreibt");

        /* bf16 (env AN). */
        setenv("MOO_KI_CKPT_BF16", "1", 1);
        moo_release(ckpt_speichern_p(zustand, pb));
        CHECK(!moo_error_flag, "e2c: bf16-Checkpoint schreibt");
        unsetenv("MOO_KI_CKPT_BF16");

        /* GATE: bf16-Datei kleiner (p* halbiert). */
        struct stat sf, sb;
        CHECK(stat(pf, &sf) == 0 && stat(pb, &sb) == 0, "e2c: stat beider CPs");
        CHECK(sb.st_size < sf.st_size, "e2c: bf16-CP kleiner (p* halbiert)");

        /* GATE: bf16-Roundtrip == bf16-gerundete Originale (Laden dtype-getrieben). */
        MooValue zb = ckpt_laden_p(pb, moo_none());
        CHECK(!moo_error_flag && zb.tag == MOO_DICT, "e2c: bf16-CP laedt");
        MooValue nb = dget_(zb, "netz");
        MooValue prmb = moo_nn_parameter(nb);
        MooTensor* pb0 = T(MV_LIST(prmb)->items[0]);
        bool bf16_ok = (pb0->size == n0);
        for (int64_t j = 0; j < n0 && bf16_ok; j++)
            if (pb0->data[j] != ref_bf16_round(orig[j])) bf16_ok = false;
        CHECK(bf16_ok, "e2c: bf16-Roundtrip == bf16-gerundete Originale (bit-exakt)");
        moo_release(prmb); moo_release(nb); moo_release(zb);

        /* GATE: f32-Default-CP laedt, p* BIT-identisch (kein Runden). */
        MooValue zf = ckpt_laden_p(pf, moo_none());
        CHECK(!moo_error_flag && zf.tag == MOO_DICT, "e2c: f32-CP laedt");
        MooValue nf = dget_(zf, "netz");
        MooValue prmf = moo_nn_parameter(nf);
        MooTensor* pf0 = T(MV_LIST(prmf)->items[0]);
        bool f32_ok = (pf0->size == n0);
        for (int64_t j = 0; j < n0 && f32_ok; j++)
            if (pf0->data[j] != orig[j]) f32_ok = false;
        CHECK(f32_ok, "e2c: f32-Default bit-identisch");
        moo_release(prmf); moo_release(nf); moo_release(zf);

        free(orig);
        moo_release(zustand); moo_release(netz);
        remove(pf); remove(pb);
    }

    /* ===== Fehlerpfade ===== */
    { MooValue r = ckpt_laden_p("/tmp/gibts_nicht_ckpt_xyz.mook", moo_none());
      CHECK(moo_error_flag && r.tag == MOO_NONE, "fehlende Datei wirft"); fehler_reset(); }
    { MooValue bad = moo_dict_new();  /* Zustand ohne "netz" */
      MooValue r = ckpt_speichern_p(bad, pfad);
      CHECK(moo_error_flag && r.tag == MOO_NONE, "Zustand ohne netz wirft"); fehler_reset();
      moo_release(bad); }

    remove(pfad);
    fprintf(stderr, "OK: %d Checks bestanden.\n", checks);
    return 0;
}
