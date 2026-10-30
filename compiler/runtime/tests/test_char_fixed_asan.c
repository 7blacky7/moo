/* SIL-10: unabhängige Black-Box-Tests für feste Zeichen-Tokenisierung. */
#include "../moo_char_fixed.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#define getpid _getpid
#define unlink _unlink
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) { moo_release(error); moo_error_flag = 1; }
void moo_socket_free(void *p) { (void)p; }
void moo_thread_free(void *p) { (void)p; }
void moo_channel_free(void *p) { (void)p; }
void moo_db_free(void *p) { (void)p; }
void moo_db_stmt_free(void *p) { (void)p; }
void moo_window_free(void *p) { (void)p; }
void moo_web_free(void *p) { (void)p; }
void moo_voxel_free(void *p) { (void)p; }
void moo_frame_free(void *p) { (void)p; }
void moo_gif_handle_free(void *p) { (void)p; }
void moo_video_handle_free(void *p) { (void)p; }
void moo_kamera_free(void *p) { (void)p; }
void moo_mikro_free(void *p) { (void)p; }

static int checks;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (%d)\n", msg, __LINE__); return 1; } checks++; } while (0)
static MooValue str(const char *s) { return moo_string_new(s); }
static int same(MooValue a, MooValue b) {
    if (a.tag != MOO_STRING || b.tag != MOO_STRING) return 0;
    MooString *x = MV_STR(a), *y = MV_STR(b);
    return x->length == y->length && memcmp(x->chars, y->chars, (size_t)x->length) == 0;
}
static int same_ids(MooValue a, MooValue b) {
    if (a.tag != MOO_TENSOR || b.tag != MOO_TENSOR) return 0;
    MooTensor *x = MV_TENSOR(a), *y = MV_TENSOR(b);
    if (x->size != y->size) return 0;
    for (int64_t i = 0; i < x->size; ++i) if (x->data[i] != y->data[i]) return 0;
    return 1;
}
static MooValue ids1(float n) {
    int32_t shape[1] = { 1 };
    MooTensor *t = moo_tensor_raw(1, shape);
    t->data[0] = n;
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t); return v;
}
static int test_roundtrip(MooValue tok, const char *input) {
    MooValue x = str(input), ids = moo_char_fixed_kodiere(tok, x);
    if (moo_error_flag || ids.tag != MOO_TENSOR) { moo_release(x); moo_release(ids); return 0; }
    MooValue back = moo_char_fixed_dekodiere(tok, ids);
    int ok = !moo_error_flag && same(x, back);
    moo_release(x); moo_release(ids); moo_release(back); return ok;
}
int main(void) {
    MooValue corpus = str("aaaa bbbb ä ä Wald Wald\n"), limit = moo_number(260);
    MooValue a = moo_char_fixed_trainiere(corpus, limit);
    MooValue b = moo_char_fixed_trainiere(corpus, limit);
    CHECK(!moo_error_flag && a.tag == MOO_STRING && b.tag == MOO_STRING, "trainiert");
    CHECK(same(a, b), "deterministisches Artefakt");
    MooValue h = moo_char_fixed_hash(a), h2 = moo_char_fixed_hash(b);
    CHECK(!moo_error_flag && h.tag == MOO_STRING && MV_STR(h)->length == 64, "SHA256");
    CHECK(same(h, h2), "stabiler Hash");
    CHECK(test_roundtrip(a, "Straße, Ähre!\r\n"), "Umlaute und Zeilenenden");
    CHECK(test_roundtrip(a, "e\xCC\x81 vs. é"), "keine stille Normalisierung");
    CHECK(test_roundtrip(a, "😀 👩‍👩‍👧‍👦"), "unbekanntes Unicode");
    CHECK(test_roundtrip(a, "Thal und Thür!"), "historische Schreibungen");
    MooValue input = str("äX");
    MooValue before = moo_char_fixed_kodiere(a, input);
    CHECK(!moo_error_flag && before.tag == MOO_TENSOR, "encode");
    MooValue z = str("Z");
    MooValue probe = moo_char_fixed_kodiere(a, z);
    moo_release(z);
    CHECK(!moo_error_flag && probe.tag == MOO_TENSOR, "unbekanntes ASCII");
    CHECK(MV_TENSOR(probe)->size == 1 && MV_TENSOR(probe)->data[0] == 90.0f, "Bytefallback ID");
    moo_release(probe);
    MooValue after = moo_char_fixed_kodiere(a, input);
    CHECK(!moo_error_flag && same_ids(before, after), "encode verändert IDs nicht");
    moo_release(after);

    MooValue other_corpus = str("öööö cccc öööö dddd");
    MooValue other = moo_char_fixed_trainiere(other_corpus, limit);
    CHECK(other.tag == MOO_STRING && MV_STR(a)->length == MV_STR(other)->length, "identische Vokabulargröße");
    MooValue other_hash = moo_char_fixed_hash(other);
    CHECK(!moo_error_flag && !same(h, other_hash), "unterschiedliche Tokenbelegung verändert Hash");
    moo_release(other_hash); moo_release(other); moo_release(other_corpus);

    MooValue empty = str("");
    MooValue out = moo_char_fixed_kodiere(a, empty);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "leere Eingabe abgelehnt");
    moo_error_flag = 0; moo_release(out); moo_release(empty);
    const char bad_utf8[] = { (char)0xc0, (char)0xaf };
    MooValue bad = moo_string_new_len(bad_utf8, 2);
    out = moo_char_fixed_kodiere(a, bad);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "überlanges UTF-8 abgelehnt");
    moo_error_flag = 0; moo_release(out); moo_release(bad);
    const char with_nul[] = { 'a', 0, 'b' };
    bad = moo_string_new_len(with_nul, 3);
    out = moo_char_fixed_kodiere(a, bad);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "NUL abgelehnt");
    moo_error_flag = 0; moo_release(out); moo_release(bad);
    const float invalid[] = { -1.0f, 0.5f, NAN, INFINITY, 16777216.0f };
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        MooValue v = ids1(invalid[i]); out = moo_char_fixed_dekodiere(a, v);
        CHECK(moo_error_flag && out.tag == MOO_NONE, "ungültige Token-ID abgelehnt");
        moo_error_flag = 0; moo_release(out); moo_release(v);
    }
    out = moo_char_fixed_trainiere(corpus, moo_number(255));
    CHECK(moo_error_flag && out.tag == MOO_NONE, "Vokabulargrenze unter 256 abgelehnt");
    moo_error_flag = 0; moo_release(out);
    out = moo_char_fixed_trainiere(corpus, moo_number(256.5));
    CHECK(moo_error_flag && out.tag == MOO_NONE, "gebrochene Vokabulargrenze abgelehnt");
    moo_error_flag = 0; moo_release(out);
    bad = moo_string_new_len(MV_STR(a)->chars, 12);
    out = moo_char_fixed_info(bad);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "abgeschnittenes Artefakt abgelehnt");
    moo_error_flag = 0; moo_release(out); moo_release(bad);

#ifndef _WIN32
    /* Existing destination survives a failed same-directory temporary write. */
    if (geteuid() != 0) {
        char dir[] = "/tmp/moo-p4-atomic-XXXXXX", dst[128];
        CHECK(mkdtemp(dir) != NULL, "Testverzeichnis");
        CHECK(snprintf(dst, sizeof(dst), "%s/old.bin", dir) > 0, "Testpfad");
        MooValue dest = str(dst);
        MooValue saved = moo_char_fixed_speichern(a, dest);
        CHECK(!moo_error_flag && saved.tag == MOO_BOOL, "Startdatei");
        moo_release(saved);
        CHECK(chmod(dir, 0500) == 0, "Schreibzugriff entziehen");
        saved = moo_char_fixed_speichern(b, dest);
        int denied = moo_error_flag && saved.tag == MOO_NONE;
        moo_error_flag = 0; moo_release(saved);
        CHECK(chmod(dir, 0700) == 0, "Schreibzugriff wiederherstellen");
        CHECK(denied, "Schreibfehler signalisiert");
        MooValue existing = moo_char_fixed_laden(dest);
        CHECK(!moo_error_flag && same(a, existing), "alte Datei bei Schreibfehler erhalten");
        moo_release(existing); moo_release(dest);
        CHECK(unlink(dst) == 0 && rmdir(dir) == 0, "Testdatei entfernt");
    } else {
        fprintf(stderr, "SKIP atomic-write-fault: root kann Verzeichnissperre umgehen\n");
    }
#else
    fprintf(stderr, "SKIP atomic-write-fault: POSIX-Berechtigungsprobe auf Windows nicht anwendbar\n");
#endif

    /* BF16-Storage muss den veralteten f32-Puffer ersetzen. 0x4282 ist
     * BF16 für 65 ('A'); der Hostpuffer enthält absichtlich 66 ('B'). */
    {
        MooValue v = ids1(66.0f);
        MooTensor *t = MV_TENSOR(v);
        t->store = malloc(sizeof(uint16_t));
        CHECK(t->store != NULL, "BF16 Testallokation");
        ((uint16_t *)t->store)[0] = 0x4282u;
        t->dtype = MOO_DT_BF16;
        t->valid = MOO_V_STORE;
        MooValue decoded = moo_char_fixed_dekodiere(a, v), expected = str("A");
        CHECK(!moo_error_flag && same(decoded, expected), "BF16 statt veraltetem Hostbuffer");
        moo_release(decoded); moo_release(expected); moo_release(v);
    }
    {
        MooValue v = ids1(65.0f);
        MV_TENSOR(v)->valid = 0;
        MooValue decoded = moo_char_fixed_dekodiere(a, v);
        CHECK(moo_error_flag && decoded.tag == MOO_NONE, "fehlgeschlagene Host-Sicherung");
        moo_error_flag = 0; moo_release(decoded); moo_release(v);
    }

    {
        MooValue info = moo_char_fixed_info(a);
        CHECK(!moo_error_flag && info.tag == MOO_DICT, "Info-Dict");
        MooValue ih = moo_dict_get(info, str("hash"));
        MooValue ih64 = moo_dict_get(info, str("hash64"));
        MooValue h64 = moo_char_fixed_hash64(a);
        CHECK(!moo_error_flag && same(ih, h) && same(ih64, h64), "Info-Hashes stimmen mit direkten Hashes");
        moo_release(ih); moo_release(ih64); moo_release(h64); moo_release(info);
    }

    char pathbuf[128];
    #ifdef _WIN32
    const char *tmpdir = getenv("TEMP");
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) tmpdir = "/tmp";
#endif
    CHECK(tmpdir && *tmpdir, "temporäres Verzeichnis");
    int path_len = snprintf(pathbuf, sizeof(pathbuf), "%s/moo-p4-char_fixed-%ld.bin",
                   tmpdir, (long)getpid());
    CHECK(path_len > 0 && (size_t)path_len < sizeof(pathbuf), "temporärer Pfad");
    MooValue path = str(pathbuf);
    out = moo_char_fixed_speichern(a, path);
    CHECK(!moo_error_flag, "Speichern erfolgreich"); moo_release(out);
    MooValue loaded = moo_char_fixed_laden(path);
    CHECK(!moo_error_flag && same(a, loaded), "Save/Load Artefakt");
    after = moo_char_fixed_kodiere(loaded, input);
    CHECK(!moo_error_flag && same_ids(before, after), "Save/Load stabile IDs");
    MooValue loaded_hash = moo_char_fixed_hash(loaded);
    CHECK(!moo_error_flag && same(h, loaded_hash), "Save/Load Hash");
    unlink(pathbuf);
    moo_release(loaded_hash); moo_release(after); moo_release(loaded); moo_release(path);
    moo_release(before); moo_release(input); moo_release(h); moo_release(h2);
    moo_release(a); moo_release(b); moo_release(corpus);
    fprintf(stderr, "char-fixed: %d checks bestanden\n", checks);
    return 0;
}
