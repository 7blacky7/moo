/* test_ub_ops_string.c — UBSan-Harness fuer Plan-007 P007-U3.
 * ============================================================================
 * ZWECK
 *   Uebt die in P007-U3 gehaerteten Pfade (Shifts in moo_ops.c, Groessen-
 *   Arithmetik in moo_string.c + Checked-Helper in moo_memory.c) gezielt unter
 *   -fsanitize=undefined aus. VOR den Fixes triggerten diese Eingaben echtes
 *   Undefined Behavior (signed-int-overflow, shift-out-of-range); NACH den
 *   Fixes muessen sie entweder ein definiertes Ergebnis liefern ODER einen
 *   sauberen moo_throw ausloesen — niemals UB.
 *
 * BAUEN/LAUFEN: ueber run_sanitize.sh (Eintrag in der OPS-Harness-Liste) bzw.
 *   manuell siehe Header von run_sanitize.sh.
 *
 * moo_throw-MODELL FUER TESTS (wie in den Voxel-Harnesses):
 *   Das echte moo_throw (moo_error.c) ruft bei unbehandeltem Fehler exit(1).
 *   Wir wollen die GEPRUEFTEN Fehlerpfade ausueben, ohne dass der Prozess
 *   stirbt — daher betreten wir vor jedem "soll-werfen"-Aufruf einen try-Block
 *   (moo_try_enter), pruefen danach das Flag (moo_try_check) und verlassen ihn
 *   wieder (moo_try_leave). So bleibt UBSan der eigentliche Richter: ein echter
 *   UB-Fund liesse den Sanitizer abbrechen, unabhaengig vom Flag.
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <assert.h>

/* Resource-Free-Stubs: moo_memory.c referenziert die typspezifischen Free-
 * Funktionen aller Heap-Typen. Dieser Harness uebt nur String/List-Pfade aus,
 * daher genuegen leere Stubs (werden nie aufgerufen). moo_list.c wird echt
 * mitgelinkt (saubere Container-Implementierung). */
void moo_socket_free(void* p)   { (void)p; }
void moo_thread_free(void* p)   { (void)p; }
void moo_channel_free(void* p)  { (void)p; }
void moo_db_free(void* p)       { (void)p; }
void moo_db_stmt_free(void* p)  { (void)p; }
void moo_window_free(void* p)   { (void)p; }
void moo_tensor_free(void* p)   { (void)p; }  /* P014-A1: MOO_TENSOR-Dispatch */
MooValue moo_tensor_to_string(MooValue v) { (void)v; return moo_string_new("<Tensor>"); }
void moo_web_free(void* p)      { (void)p; }
void moo_voxel_free(void* p)    { (void)p; }

static int g_fail = 0;

/* Ruft fn(a,b) im try-Kontext auf und erwartet einen moo_throw (Flag gesetzt). */
#define EXPECT_THROW(expr, label) do {                       \
    moo_try_enter();                                         \
    (void)(expr);                                            \
    int threw = moo_try_check();                             \
    /* Fehler-Wert (Meldungs-String) freigeben, damit der ASan-Leak-Detektor  \
     * (detect_leaks=1 im run_sanitize.sh) sauber bleibt — moo_get_error gibt  \
     * den zuletzt gesetzten Fehler zurueck. */                                \
    if (threw) { MooValue _e = moo_get_error(); moo_release(_e); }             \
    moo_try_leave();                                         \
    if (!threw) {                                            \
        printf("  [FAIL] %s: erwarteter Wurf blieb aus\n", label); \
        g_fail = 1;                                          \
    } else {                                                 \
        printf("  [ok]   %s: sauber geworfen\n", label);     \
    }                                                        \
} while (0)

#define EXPECT_OK_NUM(expr, want, label) do {                \
    MooValue _r = (expr);                                    \
    double _g = moo_as_number(_r);                           \
    if (_g != (double)(want)) {                              \
        printf("  [FAIL] %s: erwartet %g, bekam %g\n", label, (double)(want), _g); \
        g_fail = 1;                                          \
    } else {                                                 \
        printf("  [ok]   %s = %g\n", label, _g);             \
    }                                                        \
} while (0)

int main(void) {
    printf("== P007-U3 UBSan-Harness: ops/string ==\n");

    /* --- Shifts: legale Faelle (Semantik unveraendert) --- */
    EXPECT_OK_NUM(moo_lshift(moo_number(1), moo_number(4)), 16, "1 << 4");
    EXPECT_OK_NUM(moo_rshift(moo_number(256), moo_number(2)), 64, "256 >> 2");
    EXPECT_OK_NUM(moo_rshift(moo_number(-8), moo_number(1)), -4, "-8 >> 1 (arith)");
    /* 1 << 63 setzt das Vorzeichenbit -> als signed int64 = INT64_MIN.
     * Das ist definiert (uint64-Shift, Rueckcast am Rand) und entspricht der
     * bisherigen Zweierkomplement-Semantik. KEIN UB. */
    EXPECT_OK_NUM(moo_lshift(moo_number(1), moo_number(63)),
                  (double)(int64_t)((uint64_t)1 << 63), "1 << 63 (Rand)");

    /* --- Shifts: UB-Faelle frueher -> jetzt sauberer Wurf --- */
    EXPECT_THROW(moo_lshift(moo_number(1), moo_number(64)), "1 << 64");
    EXPECT_THROW(moo_lshift(moo_number(1), moo_number(1000)), "1 << 1000");
    EXPECT_THROW(moo_lshift(moo_number(1), moo_number(-1)), "1 << -1");
    EXPECT_THROW(moo_rshift(moo_number(1), moo_number(64)), "1 >> 64");
    EXPECT_THROW(moo_rshift(moo_number(1), moo_number(-1)), "1 >> -1");

    /* --- String repeat: legal + Overflow --- */
    {
        MooValue s = moo_string_new("ab");
        MooValue r = moo_string_repeat(s, moo_number(3));
        if (r.tag != MOO_STRING || strcmp(MV_STR(r)->chars, "ababab") != 0) {
            printf("  [FAIL] repeat legal: '%s'\n", MV_STR(r)->chars); g_fail = 1;
        } else {
            printf("  [ok]   repeat legal = '%s'\n", MV_STR(r)->chars);
        }
        moo_release(r);
        /* n riesig -> len*n ueberlaeuft int32 -> frueher UB, jetzt Wurf */
        EXPECT_THROW(moo_string_repeat(s, moo_number(2000000000.0)), "repeat *2e9");
        moo_release(s);
    }

    /* --- String replace: Unterschaetzungs-Bug (8x 'a' -> 'XYZ' = 24 Zeichen) --- */
    {
        MooValue s = moo_string_new("aaaaaaaa");
        MooValue old_s = moo_string_new("a");
        MooValue new_s = moo_string_new("XYZ");
        MooValue r = moo_string_replace(s, old_s, new_s);
        const char* want = "XYZXYZXYZXYZXYZXYZXYZXYZ";
        if (r.tag != MOO_STRING || strcmp(MV_STR(r)->chars, want) != 0
            || MV_STR(r)->length != 24) {
            printf("  [FAIL] replace expand: '%s' (len %d)\n",
                   MV_STR(r)->chars, MV_STR(r)->length); g_fail = 1;
        } else {
            printf("  [ok]   replace expand = '%s' (len %d)\n",
                   MV_STR(r)->chars, MV_STR(r)->length);
        }
        moo_release(r); moo_release(s); moo_release(old_s); moo_release(new_s);
    }

    /* --- String concat: legal --- */
    {
        MooValue a = moo_string_new("foo");
        MooValue b = moo_string_new("bar");
        MooValue r = moo_string_concat(a, b);
        if (r.tag != MOO_STRING || strcmp(MV_STR(r)->chars, "foobar") != 0) {
            printf("  [FAIL] concat legal: '%s'\n", MV_STR(r)->chars); g_fail = 1;
        } else {
            printf("  [ok]   concat legal = '%s'\n", MV_STR(r)->chars);
        }
        moo_release(r); moo_release(a); moo_release(b);
    }

    /* --- Checked-Helper direkt --- */
    EXPECT_OK_NUM(moo_number((double)moo_checked_mul_i32(1000, 1000, "t")), 1000000, "checked_mul ok");
    EXPECT_OK_NUM(moo_number((double)moo_checked_add_i32(2000000000, 100, "t")), 2000000100, "checked_add ok");
    EXPECT_THROW(moo_checked_mul_i32(2000000000, 2000000000, "t"), "checked_mul overflow");
    EXPECT_THROW(moo_checked_add_i32(2000000000, 2000000000, "t"), "checked_add overflow");

    if (g_fail) {
        printf("== ERGEBNIS: FEHLER ==\n");
        return 1;
    }
    printf("== ERGEBNIS: alle ops/string-Pfade sauber (kein UB, Fehlerpfade greifen) ==\n");
    return 0;
}
