/**
 * test_tokenizer_asan.c — KIP-T2 Byte-level BPE (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES). Test-throw-Modell.
 *
 * GATES (Task 0f4118c2):
 *   1. Determinismus OHNE Seed: zwei Trainingslaeufe auf identischem Korpus
 *      liefern BYTE-IDENTISCHE Artefakte.
 *   2. Byte-exakter Roundtrip: decode(encode(x)) == x fuer ASCII, Umlaute,
 *      Emoji, INVALIDES UTF-8 und NULL-Bytes.
 *   3. Artefakt-Roundtrip: speichern -> laden -> byte-identisch; encode nach
 *      dem Laden identisch.
 *   4. UNK-Negativ-Gate: ein im Korpus NIE gesehenes Byte encodet trotzdem
 *      (Byte-Fallback) und roundtrippt; ALLE Ids liegen in [0, V) — es gibt
 *      kein UNK/Out-of-Vocab.
 *   5. Kompression: wiederholter Text wird kuerzer als seine Bytezahl.
 *   6. Fehlerpfade: leerer Encode, ungueltige Id beim Decode, kaputtes
 *      Artefakt (Signatur/abgeschnitten), Vokab<256 — alle werfen erklaerend.
 *   7. Hash stabil + laengenabhaengig; info-Dict konsistent.
 * ============================================================================
 */
#include "../moo_tokenizer.h"
#include <unistd.h>   /* close(), mkstemp() */

/* --- Test-throw-Modell (ersetzt moo_error.c) ------------------------------ */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
    moo_error_flag = 1;
}
static void fehler_reset(void) { moo_error_flag = 0; }

/* --- Stubs fuer moo_release()-Dispatch-Ziele ------------------------------ */
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
static MooValue strn_(const char* s, int32_t n) { return moo_string_new_len(s, n); }
static MooValue num_(double d) { return moo_number(d); }

/* Bytegleichheit zweier MooStrings. */
static bool str_eq_bytes(MooValue a, MooValue b) {
    if (a.tag != MOO_STRING || b.tag != MOO_STRING) return false;
    MooString* sa = MV_STR(a); MooString* sb = MV_STR(b);
    if (sa->length != sb->length) return false;
    return memcmp(sa->chars, sb->chars, (size_t)sa->length) == 0;
}

/* Roundtrip-Helfer: encode+decode und pruefe Byte-Gleichheit mit dem Input.
 * Gibt 1 (Fail) oder 0 (ok) zurueck; gibt bei ok max-Id via *maxid aus. */
static int roundtrip(MooValue tok, MooValue text, uint32_t V, const char* name) {
    (void)name;
    MooValue ids = moo_tok_kodiere(tok, text);
    if (moo_error_flag || ids.tag != MOO_TENSOR) { fehler_reset(); return 2; }
    /* Alle Ids in [0, V) — UNK-Negativ-Gate. */
    MooTensor* t = MV_TENSOR(ids);
    for (int64_t i = 0; i < t->size; i++) {
        double d = (double)t->data[i];
        if (!(d >= 0.0) || d >= (double)V) { moo_release(ids); return 3; }
    }
    MooValue back = moo_tok_dekodiere(tok, ids);
    int rc = 0;
    if (moo_error_flag || !str_eq_bytes(back, text)) rc = 1;
    moo_release(ids);
    moo_release(back);
    return rc;
}

int main(void) {
    fprintf(stderr, "== KIP-T2 Byte-level BPE Harness ==\n");

    /* Korpus mit klarer, wiederkehrender Struktur (deterministische Merges).
     * Enthaelt Umlaute (Mehrbyte-UTF-8) damit Byte-Merges ueber Bytegrenzen
     * hinweg geprueft werden. */
    const char* korpus_txt =
        "die katze sass auf der matte. die katze sass. "
        "die maus rennt. die maus rennt schnell. "
        "aeaeae oeoeoe ueueue Baer Baer Baer stroemt stroemt.";
    MooValue korpus = str_(korpus_txt);

    /* ===== 1. Determinismus ohne Seed ===== */
    MooValue tokA = moo_tok_trainiere(korpus, num_(320));
    MooValue tokB = moo_tok_trainiere(korpus, num_(320));
    CHECK(!moo_error_flag, "training wirft nicht");
    CHECK(tokA.tag == MOO_STRING && tokB.tag == MOO_STRING, "artefakt ist String");
    CHECK(str_eq_bytes(tokA, tokB), "zwei Laeufe byte-identisch (kein Seed)");
    moo_release(tokB);

    /* V aus dem info-Dict ziehen. */
    MooValue info = moo_tok_info(tokA);
    CHECK(!moo_error_flag && info.tag == MOO_DICT, "info-Dict");
    MooValue vv = moo_dict_get(info, str_("vokab"));   /* dict_get konsumiert den Key */
    uint32_t V = (uint32_t)MV_NUM(vv);
    moo_release(vv);
    MooValue mm = moo_dict_get(info, str_("merges"));
    uint32_t M = (uint32_t)MV_NUM(mm);
    moo_release(mm);
    CHECK(V == 256u + M, "V == 256 + M (S==0 in T2)");
    CHECK(M > 0, "es wurden Merges gelernt");
    moo_release(info);

    /* ===== 2. Byte-exakter Roundtrip inkl. Emoji / invalid-UTF8 / NUL ===== */
    /* 2a. ASCII aus dem Korpus. */
    { MooValue x = str_("die katze sass auf der matte.");
      CHECK(roundtrip(tokA, x, V, "ascii") == 0, "roundtrip ascii byte-exakt");
      moo_release(x); }
    /* 2b. Umlaute (mehrbyte). */
    { MooValue x = str_("Baer stroemt ueber die Strasse");
      CHECK(roundtrip(tokA, x, V, "umlaut") == 0, "roundtrip umlaut byte-exakt");
      moo_release(x); }
    /* 2c. Emoji (4-Byte-UTF-8) + gemischt. */
    { MooValue x = str_("hallo \xF0\x9F\x98\x80 welt \xE2\x9C\x93 ok");
      CHECK(roundtrip(tokA, x, V, "emoji") == 0, "roundtrip emoji byte-exakt");
      moo_release(x); }
    /* 2d. INVALIDES UTF-8: einzelne 0x80/0xFF/0xC0-Bytes ohne gueltige Folge. */
    { const char inval[] = { (char)0x00, (char)0x80, (char)0xFF, (char)0xC0,
                             (char)0x41, (char)0xED, (char)0xA0, (char)0x80,
                             (char)0xF5, (char)0x90 };
      MooValue x = strn_(inval, (int32_t)sizeof(inval));
      CHECK(roundtrip(tokA, x, V, "invalid-utf8") == 0,
            "roundtrip invalid-UTF8 byte-exakt");
      moo_release(x); }
    /* 2e. Reine NUL-Bytes + alle 256 Bytewerte am Stueck. */
    { char all[256]; for (int i = 0; i < 256; i++) all[i] = (char)i;
      MooValue x = strn_(all, 256);
      CHECK(roundtrip(tokA, x, V, "alle-256-bytes") == 0,
            "roundtrip aller 256 Bytewerte byte-exakt (Byte-Fallback vollstaendig)");
      moo_release(x); }

    /* ===== 3. Artefakt-Roundtrip ueber Datei ===== */
    { char pfad[] = "/tmp/moo_tok_test_XXXXXX";
      int fd = mkstemp(pfad);
      CHECK(fd >= 0, "tempdatei anlegbar");
      close(fd);
      MooValue pfv1 = str_(pfad); MooValue okv = moo_tok_speichern(tokA, pfv1); moo_release(pfv1);
      CHECK(!moo_error_flag && okv.tag == MOO_BOOL && MV_BOOL(okv), "speichern ok");
      MooValue pfv2 = str_(pfad); MooValue tokL = moo_tok_laden(pfv2); moo_release(pfv2);
      CHECK(!moo_error_flag && tokL.tag == MOO_STRING, "laden ok");
      CHECK(str_eq_bytes(tokA, tokL), "geladenes Artefakt byte-identisch");
      /* encode nach dem Laden identisch. */
      MooValue x = str_("die katze sass");
      MooValue i1 = moo_tok_kodiere(tokA, x);
      MooValue i2 = moo_tok_kodiere(tokL, x);
      CHECK(i1.tag == MOO_TENSOR && i2.tag == MOO_TENSOR, "encode liefert Tensor");
      CHECK(MV_TENSOR(i1)->size == MV_TENSOR(i2)->size, "encode-Laenge nach Laden gleich");
      int same = 1;
      for (int64_t i = 0; i < MV_TENSOR(i1)->size; i++)
          if (MV_TENSOR(i1)->data[i] != MV_TENSOR(i2)->data[i]) same = 0;
      CHECK(same, "encode-Ids nach Laden identisch");
      moo_release(i1); moo_release(i2); moo_release(x); moo_release(tokL);
      remove(pfad); }

    /* ===== 4. UNK-Negativ-Gate ===== */
    /* Byte 0x7F (DEL) kommt im Korpus nicht vor -> muss trotzdem encoden. */
    { char c = (char)0x7F; MooValue x = strn_(&c, 1);
      MooValue ids = moo_tok_kodiere(tokA, x);
      CHECK(!moo_error_flag && ids.tag == MOO_TENSOR, "unbekanntes Byte encodet (kein UNK)");
      CHECK(MV_TENSOR(ids)->size == 1, "ein unbekanntes Byte -> genau 1 Basis-Token");
      CHECK((double)MV_TENSOR(ids)->data[0] == (double)0x7F,
            "Byte-Fallback: Id == Bytewert");
      MooValue back = moo_tok_dekodiere(tokA, ids);
      CHECK(str_eq_bytes(back, x), "unbekanntes Byte roundtrippt");
      moo_release(ids); moo_release(back); moo_release(x); }

    /* ===== 5. Kompression ===== */
    { MooValue x = str_("die katze sass auf der matte. die katze sass.");
      MooValue ids = moo_tok_kodiere(tokA, x);
      CHECK(ids.tag == MOO_TENSOR, "encode ok");
      CHECK(MV_TENSOR(ids)->size < (int64_t)MV_STR(x)->length,
            "wiederkehrender Text komprimiert (weniger Tokens als Bytes)");
      moo_release(ids); moo_release(x); }

    /* ===== 6. Fehlerpfade ===== */
    /* 6a. leerer Encode wirft. */
    { MooValue x = str_(""); MooValue r = moo_tok_kodiere(tokA, x);
      CHECK(moo_error_flag && r.tag == MOO_NONE, "leerer Encode wirft"); fehler_reset();
      moo_release(x); }
    /* 6b. ungueltige Id beim Decode wirft. */
    { int32_t shape[1] = { 2 }; MooTensor* t = moo_tensor_raw(1, shape);
      t->data[0] = 0.0f; t->data[1] = (float)(V + 5);
      MooValue idv; idv.tag = MOO_TENSOR; moo_val_set_ptr(&idv, t);
      MooValue r = moo_tok_dekodiere(tokA, idv);
      CHECK(moo_error_flag && r.tag == MOO_NONE, "Decode mit Id>=V wirft"); fehler_reset();
      moo_release(idv); }
    /* 6c. kaputte Signatur wirft beim Parsen. */
    { MooValue bad = str_("NICHTBPE\x01\x00\x00\x00 muellmuellmuell");
      MooValue xb = str_("x"); MooValue r = moo_tok_kodiere(bad, xb); moo_release(xb);
      CHECK(moo_error_flag && r.tag == MOO_NONE, "kaputte Signatur wirft"); fehler_reset();
      moo_release(bad); }
    /* 6d. abgeschnittenes Artefakt (nur die ersten 12 Bytes von tokA). */
    { MooValue trunc = strn_(MV_STR(tokA)->chars, 12);
      MooValue r = moo_tok_info(trunc);
      CHECK(moo_error_flag && r.tag == MOO_NONE, "abgeschnittenes Artefakt wirft"); fehler_reset();
      moo_release(trunc); }
    /* 6e. Vokab < 256 wirft. */
    { MooValue r = moo_tok_trainiere(korpus, num_(100));
      CHECK(moo_error_flag && r.tag == MOO_NONE, "Vokab<256 wirft"); fehler_reset(); }
    /* 6f. leerer Korpus wirft. */
    { MooValue leer = str_(""); MooValue r = moo_tok_trainiere(leer, num_(300));
      CHECK(moo_error_flag && r.tag == MOO_NONE, "leerer Korpus wirft"); fehler_reset();
      moo_release(leer); }

    /* ===== 7. Hash / info-Konsistenz ===== */
    { MooValue h1 = moo_tok_hash(tokA);
      MooValue h2 = moo_tok_hash(tokA);
      CHECK(h1.tag == MOO_STRING && MV_STR(h1)->length == 16, "hash 16 hex-Zeichen");
      CHECK(str_eq_bytes(h1, h2), "hash stabil");
      /* ANDERER Korpus -> anderes Artefakt -> anderer Hash. */
      MooValue korpus2 = str_("voellig anderer text zzz zzz zzz qqq qqq mit anderen mustern");
      MooValue tokC = moo_tok_trainiere(korpus2, num_(300));
      MooValue h3 = moo_tok_hash(tokC);
      CHECK(!str_eq_bytes(h1, h3), "anderer Korpus -> anderer Hash");
      moo_release(h1); moo_release(h2); moo_release(h3); moo_release(tokC); moo_release(korpus2); }

    moo_release(korpus);
    moo_release(tokA);

    fprintf(stderr, "OK: %d Checks bestanden.\n", checks);
    return 0;
}
