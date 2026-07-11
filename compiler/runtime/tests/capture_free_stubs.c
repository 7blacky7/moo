/* Gemeinsame schwache Capture-Free-Stubs fuer Sanitizer-Harnesses, die
 * moo_memory.c linken, aber Capture nicht testen. Echte moo_capture.c-Symbole
 * sind stark und gewinnen automatisch in den Capture-Harnesses. */
#if defined(__GNUC__) || defined(__clang__)
#define MOO_TEST_WEAK __attribute__((weak))
#else
#define MOO_TEST_WEAK
#endif
MOO_TEST_WEAK void moo_kamera_free(void* p) { (void)p; }
MOO_TEST_WEAK void moo_mikro_free(void* p) { (void)p; }
