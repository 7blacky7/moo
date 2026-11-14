/* P2-T73: Windows-x64-Gegenprobe der nativen Zahlen-/Cast-/bool-Semantik.
 * Der Rechencode ist Moo (zahlen_probe.moos); dieses C-Harness prueft nur
 * unabhaengig die Werte ueber die Win64-ABI und liefert den Exitcode. Die
 * Erwartungen entsprechen den Linux-Fixtures compiler/tests/p2/native_*.moos. */
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
    if (fehler != 0) {
        fprintf(stderr, "MOO_WIN64_ZAHLEN_FAIL: %d von %d Faellen\n", fehler, faelle);
        return 1;
    }
    printf("MOO_WIN64_ZAHLEN_OK: %d/%d\n", faelle, faelle);
    return 0;
}
