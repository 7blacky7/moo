/* No-op *_free-Stubs fuer nicht-gelinkte Heap-Typen (werden im Capture-Harness
 * nie aufgerufen; nur zur Aufloesung der moo_memory.c-Dispatch-Referenzen). */
void moo_channel_free(void* p){ (void)p; }
void moo_db_free(void* p){ (void)p; }
void moo_db_stmt_free(void* p){ (void)p; }
void moo_socket_free(void* p){ (void)p; }
void moo_thread_free(void* p){ (void)p; }
void moo_voxel_free(void* p){ (void)p; }
void moo_web_free(void* p){ (void)p; }
void moo_window_free(void* p){ (void)p; }
