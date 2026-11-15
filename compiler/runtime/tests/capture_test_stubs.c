/* No-op *_free-Stubs fuer nicht-gelinkte Heap-Typen (werden im Capture-Harness
 * nie aufgerufen; nur zur Aufloesung der moo_memory.c-Dispatch-Referenzen).
 * Alle Symbole sind schwach, damit echte Produktions- oder Harness-Symbole
 * beim Linken immer Vorrang haben. */
#if defined(__GNUC__) || defined(__clang__)
#define MOO_CAPTURE_TEST_WEAK __attribute__((weak))
#else
#define MOO_CAPTURE_TEST_WEAK
#endif
MOO_CAPTURE_TEST_WEAK void moo_channel_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_db_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_db_stmt_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_socket_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_thread_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_voxel_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_web_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_window_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_kamera_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_mikro_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_buffer_free(void* p){ (void)p; }
MOO_CAPTURE_TEST_WEAK void moo_struct_box_free(void* p){ (void)p; }

/* P2 (CI 464cc54): moo_ops.c (moo_eq) und moo_print.c (moo_to_string) rufen
 * fuer MOO_HANDLE/MOO_STRUCT moo_handle.c und moo_struct_dyn.c auf. Die
 * Capture-Harnesses erzeugen nie Griffe oder Strukturwerte; wird ein solcher
 * Zweig doch erreicht, bricht der Stub hart ab (wie capture_free_stubs.c). */
#include "moo_handle.h"
#include "moo_struct_dyn.h"

#include <stdio.h>
#include <stdlib.h>

static void moo_capture_test_unerwartet(const char* name) {
    fprintf(stderr, "FATAL: Test-Stub %s erreicht - Harness muss das echte Modul linken\n", name);
    abort();
}

MOO_CAPTURE_TEST_WEAK bool moo_handle_equal(MooValue a, MooValue b) {
    (void)a; (void)b;
    moo_capture_test_unerwartet("moo_handle_equal");
    return false;
}

MOO_CAPTURE_TEST_WEAK const char* moo_handle_text(MooValue value) {
    (void)value;
    moo_capture_test_unerwartet("moo_handle_text");
    return "";
}

MOO_CAPTURE_TEST_WEAK bool moo_struct_gleich(MooValue a, MooValue b) {
    (void)a; (void)b;
    moo_capture_test_unerwartet("moo_struct_gleich");
    return false;
}

MOO_CAPTURE_TEST_WEAK MooValue moo_struct_text(MooValue s) {
    moo_capture_test_unerwartet("moo_struct_text");
    return s;
}
