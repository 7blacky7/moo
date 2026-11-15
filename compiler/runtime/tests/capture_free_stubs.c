/* Gemeinsame schwache Stubs fuer Sanitizer-Harnesses, die moo_memory.c bzw.
 * den Kern (moo_ops.c, moo_print.c) linken, die betroffenen Module aber nicht
 * testen. Echte Symbole sind stark und gewinnen automatisch.
 *
 * Capture (C1): moo_memory.c kennt Kamera/Mikro-Tags.
 * P2: moo_memory.c dispatcht MOO_BUFFER/MOO_STRUCT; moo_ops.c (moo_eq) und
 * moo_print.c (moo_to_string) rufen fuer MOO_HANDLE/MOO_STRUCT moo_handle.c und
 * moo_struct_dyn.c auf (CI be48d76). Diese Harnesses erzeugen nie Griffe oder
 * Strukturwerte; moo_struct_dyn.c wuerde ausserdem moo_buffer_index.c und
 * moo_error.c nachziehen (Konflikt mit den moo_throw-Stubs der Harnesses).
 * Erreicht ein Harness doch einen Vergleichs-/Ausgabezweig, bricht der Stub
 * hart ab statt still ein Ergebnis zu erfinden. */
#include "moo_handle.h"
#include "moo_struct_dyn.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#define MOO_TEST_WEAK __attribute__((weak))
#else
#define MOO_TEST_WEAK
#endif
MOO_TEST_WEAK void moo_kamera_free(void* p) { (void)p; }
MOO_TEST_WEAK void moo_mikro_free(void* p) { (void)p; }
/* Native Ebene (P2): moo_memory.c dispatcht MOO_BUFFER/MOO_STRUCT. */
MOO_TEST_WEAK void moo_buffer_free(void* p) { (void)p; }
MOO_TEST_WEAK void moo_struct_box_free(void* p) { (void)p; }

static void moo_test_stub_unerwartet(const char* name) {
    fprintf(stderr, "FATAL: Test-Stub %s erreicht - Harness muss das echte Modul linken\n", name);
    abort();
}

MOO_TEST_WEAK bool moo_handle_equal(MooValue a, MooValue b) {
    (void)a; (void)b;
    moo_test_stub_unerwartet("moo_handle_equal");
    return false;
}

MOO_TEST_WEAK const char* moo_handle_text(MooValue value) {
    (void)value;
    moo_test_stub_unerwartet("moo_handle_text");
    return "";
}

MOO_TEST_WEAK bool moo_struct_gleich(MooValue a, MooValue b) {
    (void)a; (void)b;
    moo_test_stub_unerwartet("moo_struct_gleich");
    return false;
}

MOO_TEST_WEAK MooValue moo_struct_text(MooValue s) {
    moo_test_stub_unerwartet("moo_struct_text");
    return s;
}
