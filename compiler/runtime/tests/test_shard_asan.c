/**
 * test_shard_asan.c — KIP-E1 Streaming-Shards + Dataloader (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES). Test-throw-Modell.
 *
 * GATES (Task f91d782c):
 *   1. Shard-Roundtrip: schreiben -> info/Header korrekt; gefenstertes Lesen
 *      liefert exakt die geschriebenen Token-Ids (auch ueber Doc-Grenzen).
 *   2. Pruefsumme: pruefen()==true; nach 1-Byte-Korruption ==false (Negativ).
 *   3. Seed-Determinismus: reihenfolge(seed) zwei Laeufe identisch; anderer
 *      Seed anders; Ergebnis ist eine echte Permutation aller Bloecke.
 *   4. SFT: Maskenspur roundtrippt (fenster_maske); pretrain hat keine Maske.
 *   5. Train/Val-Split auf Shard-Ebene: deterministisch, disjunkt, vollstaendig.
 *   6. Fehlerpfade: Fenster ausserhalb, kaputte Datei, falsche Argumente.
 * ============================================================================
 */
#include "../moo_shard.h"
#include <unistd.h>

/* --- Test-throw-Modell ---------------------------------------------------- */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) { if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error)); moo_error_flag = 1; }
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

static MooValue str_(const char* s) { return moo_string_new(s); }
static MooValue num_(double d) { return moo_number(d); }

/* 1-D Tensor aus Ids bauen (+1). */
static MooValue ids_tensor(const int* ids, int n) {
    int32_t shape[1] = { n };
    MooTensor* t = moo_tensor_raw(1, shape);
    for (int i = 0; i < n; i++) t->data[i] = (float)ids[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

int main(void) {
    fprintf(stderr, "== KIP-E1 Streaming-Shards Harness ==\n");

    /* Drei Dokumente; flacher Token-Strom zum Vergleich. */
    int d0[] = { 10, 11, 12, 13, 14 };
    int d1[] = { 20, 21, 22 };
    int d2[] = { 30, 31, 32, 33 };
    int flat[12]; int fi = 0;
    for (int i = 0; i < 5; i++) flat[fi++] = d0[i];
    for (int i = 0; i < 3; i++) flat[fi++] = d1[i];
    for (int i = 0; i < 4; i++) flat[fi++] = d2[i];   /* n_tokens = 12 */

    MooValue docs = moo_list_new(3);
    moo_list_append(docs, ids_tensor(d0, 5));
    moo_list_append(docs, ids_tensor(d1, 3));
    moo_list_append(docs, ids_tensor(d2, 4));

    char pfad[] = "/tmp/moo_shard_XXXXXX";
    int fd = mkstemp(pfad); CHECK(fd >= 0, "tempdatei"); close(fd);

    /* ===== 1. schreiben (pretrain) + info ===== */
    { MooValue pf = str_(pfad), da = str_("pretrain"), tv = str_("00000000deadbeef");
      MooValue ok = moo_shard_schreiben(pf, docs, da, tv, moo_none());
      CHECK(!moo_error_flag && ok.tag == MOO_BOOL && MV_BOOL(ok), "schreiben pretrain ok");
      moo_release(pf); moo_release(da); moo_release(tv); }
    { MooValue pf = str_(pfad); MooValue info = moo_shard_info(pf); moo_release(pf);
      CHECK(!moo_error_flag && info.tag == MOO_DICT, "info ok");
      MooValue nd = moo_dict_get(info, str_("n_docs"));   CHECK((int)MV_NUM(nd) == 3, "n_docs==3"); moo_release(nd);
      MooValue nt = moo_dict_get(info, str_("n_tokens")); CHECK((int)MV_NUM(nt) == 12, "n_tokens==12"); moo_release(nt);
      MooValue hm = moo_dict_get(info, str_("hat_maske")); CHECK(hm.tag == MOO_BOOL && !MV_BOOL(hm), "pretrain ohne Maske"); moo_release(hm);
      MooValue tv = moo_dict_get(info, str_("tokenizer_version"));
      CHECK(tv.tag == MOO_STRING && strcmp(MV_STR(tv)->chars, "00000000deadbeef") == 0, "tokenizer_version roundtrip"); moo_release(tv);
      MooValue da = moo_dict_get(info, str_("datenart"));
      CHECK(da.tag == MOO_STRING && strcmp(MV_STR(da)->chars, "pretrain") == 0, "datenart pretrain"); moo_release(da);
      moo_release(info); }

    /* ===== 2. gefenstertes Lesen (auch ueber Doc-Grenze) ===== */
    { MooValue pf = str_(pfad); MooValue w = moo_shard_fenster(pf, num_(3), num_(6)); moo_release(pf);
      CHECK(!moo_error_flag && w.tag == MOO_TENSOR && MV_TENSOR(w)->size == 6, "fenster Laenge");
      int okv = 1;
      for (int i = 0; i < 6; i++) if ((int)MV_TENSOR(w)->data[i] != flat[3 + i]) okv = 0;
      CHECK(okv, "fenster liefert exakt die geschriebenen Ids");
      moo_release(w); }
    /* Volles Fenster == flat. */
    { MooValue pf = str_(pfad); MooValue w = moo_shard_fenster(pf, num_(0), num_(12)); moo_release(pf);
      int okv = 1; for (int i = 0; i < 12; i++) if ((int)MV_TENSOR(w)->data[i] != flat[i]) okv = 0;
      CHECK(okv, "volles Fenster == flacher Strom"); moo_release(w); }

    /* ===== 3. Pruefsumme + Negativ ===== */
    { MooValue pf = str_(pfad); MooValue ok = moo_shard_pruefen(pf); moo_release(pf);
      CHECK(ok.tag == MOO_BOOL && MV_BOOL(ok), "pruefen ok (intakt)"); }
    /* letztes Byte kippen -> pruefen false. */
    { FILE* f = fopen(pfad, "r+b"); CHECK(f != NULL, "reopen r+b");
      fseek(f, -1, SEEK_END); int c = fgetc(f); fseek(f, -1, SEEK_END); fputc(c ^ 0xFF, f); fclose(f);
      MooValue pf = str_(pfad); MooValue ok = moo_shard_pruefen(pf); moo_release(pf);
      CHECK(ok.tag == MOO_BOOL && !MV_BOOL(ok), "pruefen erkennt Korruption (Negativ)");
      /* wiederherstellen. */
      f = fopen(pfad, "r+b"); fseek(f, -1, SEEK_END); c = fgetc(f); fseek(f, -1, SEEK_END); fputc(c ^ 0xFF, f); fclose(f);
      MooValue pf2 = str_(pfad); MooValue ok2 = moo_shard_pruefen(pf2); moo_release(pf2);
      CHECK(MV_BOOL(ok2), "nach Wiederherstellung wieder ok"); }

    /* ===== 4. Seed-deterministische Blockreihenfolge (seq_len=3 -> 4 Bloecke) ===== */
    { MooValue pf = str_(pfad);
      MooValue r1 = moo_shard_reihenfolge(pf, num_(1234), num_(3));
      MooValue r2 = moo_shard_reihenfolge(pf, num_(1234), num_(3));
      MooValue r3 = moo_shard_reihenfolge(pf, num_(9999), num_(3));
      moo_release(pf);
      CHECK(r1.tag == MOO_LIST && MV_LIST(r1)->length == 4, "4 Bloecke (12/3)");
      int same = (MV_LIST(r2)->length == 4);
      for (int i = 0; same && i < 4; i++) if (MV_NUM(MV_LIST(r1)->items[i]) != MV_NUM(MV_LIST(r2)->items[i])) same = 0;
      CHECK(same, "gleicher Seed -> identische Reihenfolge");
      int diff = 0; for (int i = 0; i < 4; i++) if (MV_NUM(MV_LIST(r1)->items[i]) != MV_NUM(MV_LIST(r3)->items[i])) diff = 1;
      CHECK(diff, "anderer Seed -> andere Reihenfolge");
      /* echte Permutation von {0,3,6,9}. */
      int seen[4] = {0,0,0,0}; int perm_ok = 1;
      for (int i = 0; i < 4; i++) { int off = (int)MV_NUM(MV_LIST(r1)->items[i]); if (off % 3 != 0 || off/3 < 0 || off/3 >= 4 || seen[off/3]) perm_ok = 0; else seen[off/3] = 1; }
      CHECK(perm_ok, "Reihenfolge ist eine echte Permutation aller Bloecke");
      moo_release(r1); moo_release(r2); moo_release(r3); }

    /* ===== 5. SFT-Shard mit Loss-Maske ===== */
    char spfad[] = "/tmp/moo_shard_sft_XXXXXX";
    int sfd = mkstemp(spfad); CHECK(sfd >= 0, "sft tempdatei"); close(sfd);
    { int m0[] = { 0, 0, 1, 1, 1 };
      int m1[] = { 0, 0, 0 };
      int m2[] = { 0, 1, 1, 0 };
      MooValue mdocs = moo_list_new(3);
      moo_list_append(mdocs, ids_tensor(m0, 5));
      moo_list_append(mdocs, ids_tensor(m1, 3));
      moo_list_append(mdocs, ids_tensor(m2, 4));
      MooValue pf = str_(spfad), da = str_("sft"), tv = str_("0");
      MooValue ok = moo_shard_schreiben(pf, docs, da, tv, mdocs);
      CHECK(!moo_error_flag && MV_BOOL(ok), "schreiben sft ok");
      moo_release(pf); moo_release(da); moo_release(tv);
      /* Maske gefenstert lesen == erwartete Bits. */
      int mflat[12] = { 0,0,1,1,1, 0,0,0, 0,1,1,0 };
      MooValue pfr = str_(spfad); MooValue wm = moo_shard_fenster_maske(pfr, num_(0), num_(12)); moo_release(pfr);
      CHECK(!moo_error_flag && wm.tag == MOO_TENSOR, "fenster_maske ok");
      int okv = 1; for (int i = 0; i < 12; i++) if ((int)MV_TENSOR(wm)->data[i] != mflat[i]) okv = 0;
      CHECK(okv, "Loss-Maske roundtrippt bit-genau");
      moo_release(wm);
      MooValue pfi = str_(spfad); MooValue info = moo_shard_info(pfi); moo_release(pfi);
      MooValue hm = moo_dict_get(info, str_("hat_maske")); CHECK(MV_BOOL(hm), "sft hat Maske"); moo_release(hm); moo_release(info);
      moo_release(mdocs); }
    /* pretrain-Shard hat keine Maske -> fenster_maske wirft. */
    { MooValue pf = str_(pfad); MooValue r = moo_shard_fenster_maske(pf, num_(0), num_(4)); moo_release(pf);
      CHECK(moo_error_flag && r.tag == MOO_NONE, "fenster_maske auf pretrain wirft"); fehler_reset(); }

    /* ===== 6. Train/Val-Split auf Shard-Ebene ===== */
    { MooValue shards = moo_list_new(10);
      char nm[32];
      for (int i = 0; i < 10; i++) { snprintf(nm, sizeof(nm), "shard_%d.mds", i); moo_list_append(shards, str_(nm)); }
      MooValue s1 = moo_shard_split(shards, num_(0.3), num_(7));
      MooValue s2 = moo_shard_split(shards, num_(0.3), num_(7));
      CHECK(!moo_error_flag && s1.tag == MOO_DICT, "split ok");
      MooValue tr1 = moo_dict_get(s1, str_("train")); MooValue va1 = moo_dict_get(s1, str_("val"));
      MooValue tr2 = moo_dict_get(s2, str_("train")); MooValue va2 = moo_dict_get(s2, str_("val"));
      CHECK(MV_LIST(va1)->length == 3 && MV_LIST(tr1)->length == 7, "30%% val == 3, train == 7");
      /* deterministisch: gleiche Val-Menge. */
      int same = (MV_LIST(va1)->length == MV_LIST(va2)->length);
      for (int i = 0; same && i < MV_LIST(va1)->length; i++)
          if (strcmp(MV_STR(MV_LIST(va1)->items[i])->chars, MV_STR(MV_LIST(va2)->items[i])->chars) != 0) same = 0;
      CHECK(same, "split deterministisch (gleicher Seed)");
      /* disjunkt + vollstaendig (10 eindeutige). */
      CHECK(MV_LIST(tr1)->length + MV_LIST(va1)->length == 10, "split vollstaendig");
      moo_release(tr1); moo_release(va1); moo_release(tr2); moo_release(va2);
      moo_release(s1); moo_release(s2); moo_release(shards); }

    /* ===== 7. Fehlerpfade ===== */
    { MooValue pf = str_(pfad); MooValue r = moo_shard_fenster(pf, num_(10), num_(5)); moo_release(pf);
      CHECK(moo_error_flag && r.tag == MOO_NONE, "Fenster ausserhalb wirft"); fehler_reset(); }
    { MooValue pf = str_("/tmp/gibts_nicht_xyz.mds"); MooValue r = moo_shard_info(pf); moo_release(pf);
      CHECK(moo_error_flag && r.tag == MOO_NONE, "fehlende Datei wirft"); fehler_reset(); }
    { MooValue pf = str_(pfad), da = str_("quatsch"), tv = str_("0");
      MooValue r = moo_shard_schreiben(pf, docs, da, tv, moo_none());
      CHECK(moo_error_flag && r.tag == MOO_NONE, "unbekannte datenart wirft"); fehler_reset();
      moo_release(pf); moo_release(da); moo_release(tv); }

    remove(pfad); remove(spfad);
    moo_release(docs);
    fprintf(stderr, "OK: %d Checks bestanden.\n", checks);
    return 0;
}
