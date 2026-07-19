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
