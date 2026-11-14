/* P2-T73: Windows-x64-Gegenprobe der nativen Zahlen-/Cast-/bool-Semantik.
 * Der Rechencode ist Moo (zahlen_probe.moos); dieses C-Harness prueft nur
 * unabhaengig die Werte ueber die Win64-ABI und liefert den Exitcode. Die
 * Erwartungen entsprechen den Linux-Fixtures compiler/tests/p2/native_*.moos. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

extern int32_t moo_win_div(int32_t a, int32_t b);
extern int32_t moo_win_rest(int32_t a, int32_t b);
extern uint8_t moo_win_links(uint8_t a, uint8_t n);
extern int8_t moo_win_rechts(int8_t a, int8_t n);
extern int8_t moo_win_wrap8(int8_t a);
extern uint8_t moo_win_als_u8(int32_t a);
extern uint8_t moo_win_wrap_u8(int32_t a);
extern int8_t moo_win_satt_i8(int32_t a);
extern uint8_t moo_win_satt_f(double a);
extern int32_t moo_win_f2i(double a);
extern int64_t moo_win_rund(int32_t a);
extern int32_t moo_win_kleiner(int32_t a, int32_t b);
extern int32_t moo_win_sicher(int32_t a, int32_t b);
extern int32_t moo_win_nan(double a, double b);
/* P2-T58 B8: Batch A (Local anderen Typs, Pure->Pure i64), i64-Wrap, -u64, f32-Rundung. */
extern uint8_t moo_win_lokal(int32_t a);
extern int64_t moo_win_i64_add(int64_t a, int64_t b);
extern uint64_t moo_win_u64_neg(uint64_t a);
extern int32_t moo_win_f32_rund(float a, float b);
/* P2-T58 D2c: Float-% (C-fmod) und Float-** (C99-pow) aus moo_native_math.c. */
extern double moo_win_frest(double a, double b);
extern float moo_win_fpow32(float a, float b);

static int fehler = 0;
static int faelle = 0;

static void pruefe(const char *name, long long erhalten, long long erwartet) {
    faelle++;
    if (erhalten != erwartet) {
        fprintf(stderr, "FAIL %s: %lld statt %lld\n", name, erhalten, erwartet);
        fehler++;
    }
}

int main(void) {
    /* Ganzzahlen: Division zur Null, Rest mit Vorzeichen des Dividenden, Shifts, Wrap. */
    pruefe("div -7/3", moo_win_div(-7, 3), -2);
    pruefe("rest -7%3", moo_win_rest(-7, 3), -1);
    pruefe("div 7/-3", moo_win_div(7, -3), -2);
    pruefe("rest 7%-3", moo_win_rest(7, -3), 1);
    pruefe("u8 129<<1", moo_win_links(129, 1), 2);
    pruefe("i8 -128>>7", moo_win_rechts(-128, 7), -1);
    pruefe("i8 127+1", moo_win_wrap8(127), -128);
    /* Casts: geprueft, Wrap, saettigend, Float-Rundung. */
    pruefe("als u8 200", moo_win_als_u8(200), 200);
    pruefe("als_wrap u8 -1", moo_win_wrap_u8(-1), 255);
    pruefe("als_saettigend i8 300", moo_win_satt_i8(300), 127);
    pruefe("als_saettigend i8 -300", moo_win_satt_i8(-300), -128);
    pruefe("als_saettigend u8 3.7", moo_win_satt_f(3.7), 3);
    pruefe("als_saettigend u8 -5.0", moo_win_satt_f(-5.0), 0);
    pruefe("als i32 7.0", moo_win_f2i(7.0), 7);
    pruefe("i32->f32->i64 16777217", moo_win_rund(16777217), 16777216);
    /* Vergleiche, Kurzschluss, IEEE-NaN. */
    pruefe("1 < 2", moo_win_kleiner(1, 2), 1);
    pruefe("2 < 1", moo_win_kleiner(2, 1), 0);
    pruefe("sicher 7/0 (Kurzschluss)", moo_win_sicher(7, 0), 0);
    pruefe("sicher 7/2", moo_win_sicher(7, 2), 1);
    pruefe("NaN == NaN", moo_win_nan(0.0, 0.0), 0);
    /* P2-T58 B8 */
    pruefe("lokal 600 -> i64 -> halb -> u8", moo_win_lokal(600), 44);
    pruefe("i64 MAX+1", moo_win_i64_add(INT64_MAX, 1), INT64_MIN);
    pruefe("u64 -1", moo_win_u64_neg(1) == UINT64_MAX, 1);
    pruefe("f32 0.1+0.2 != f64", moo_win_f32_rund(0.1f, 0.2f), 1);
    /* P2-T58 D2c: Vorzeichen des Dividenden, -0, exakte Wurzel/Quadrat. */
    pruefe("f64 -5.5 % 2 (mal 4)", (long long)(moo_win_frest(-5.5, 2.0) * 4.0), -6);
    pruefe("f64 7 % -3", (long long)moo_win_frest(7.0, -3.0), 1);
    pruefe("f64 -4 % 2 = -0", signbit(moo_win_frest(-4.0, 2.0)) != 0, 1);
    pruefe("f32 9 ** 0.5", (long long)moo_win_fpow32(9.0f, 0.5f), 3);
    pruefe("f32 1.5 ** 2 (mal 4)", (long long)(moo_win_fpow32(1.5f, 2.0f) * 4.0f), 9);
    if (fehler != 0) {
        fprintf(stderr, "MOO_WIN64_ZAHLEN_FAIL: %d von %d Faellen\n", fehler, faelle);
        return 1;
    }
    printf("MOO_WIN64_ZAHLEN_OK: %d/%d\n", faelle, faelle);
    return 0;
}
