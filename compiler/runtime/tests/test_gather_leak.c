/**
 * test_gather_leak.c — KIP-T1 1M-Leak-Gate fuer den Gather-Op.
 * ============================================================================
 * Schleife: out = gather(W, idx); loss = sum(out); rueckwaerts(loss);
 *           grad loeschen; Tape reset; out/loss releasen.
 * W + idx werden EINMAL angelegt und wiederverwendet (wie im Embedding-
 * Hotpath). Ein Refcount-/Tape-Leak wuerde die RSS monoton wachsen lassen
 * bzw. unter ASan (detect_leaks=1) am Ende gemeldet.
 *
 * NUTZUNG:  test_gather_leak [iterationen]   (Default 1000000)
 * ERFOLG:   rc=0 und RSS-Wachstum vom Warmlauf zum Ende < 5 %.
 *           Unter ASan zusaetzlich: keine Leak-Meldung.
 * LINK:     moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c
 *           moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c
 *           moo_list.c moo_ops.c  -lm
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <stdio.h>
#include <stdlib.h>

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
    moo_error_flag = 1;
}
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

/* Resident-Set in KB aus /proc/self/statm (Seiten * Pagesize). 0 = unbekannt. */
static long rss_kb(void) {
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long size = 0, resident = 0;
    if (fscanf(f, "%ld %ld", &size, &resident) != 2) { fclose(f); return 0; }
    fclose(f);
    long pg = 4096;   /* uebliche Pagesize; fuer die Wachstums-Relation egal */
    return resident * (pg / 1024);
}

int main(int argc, char** argv) {
#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
    long default_iters = 30000L;   /* ASan: LSan detektiert Per-Iter-Leaks frueh */
#else
    long default_iters = 1000000L; /* Standalone: echtes 1M-RSS-Stabilitaets-Gate */
#endif
    long iters = (argc > 1) ? atol(argv[1]) : default_iters;
    if (iters < 1) iters = 1;

    /* W [vokab=8, dim=4], requires_grad. idx [6] mit Duplikaten. */
    int32_t sw[2] = { 8, 4 };
    MooTensor* wt = moo_tensor_raw(2, sw);
    for (int64_t i = 0; i < wt->size; i++) wt->data[i] = 0.01f * (float)(i + 1);
    wt->requires_grad = true;
    MooValue W; W.tag = MOO_TENSOR; moo_val_set_ptr(&W, wt);

    int32_t si[1] = { 6 };
    MooTensor* it = moo_tensor_raw(1, si);
    float idxvals[6] = { 0, 3, 3, 7, 1, 3 };   /* Duplikat 3 dreifach */
    for (int i = 0; i < 6; i++) it->data[i] = idxvals[i];
    MooValue idx; idx.tag = MOO_TENSOR; moo_val_set_ptr(&idx, it);

    long rss_warm = 0;
    const long warm_at = (iters > 2000) ? 1000 : 0;

    for (long k = 0; k < iters; k++) {
        MooValue out = moo_tensor_gather(W, idx);
        MooValue loss = moo_tensor_summe(out, moo_number(-1));
        moo_tensor_rueckwaerts(loss);
        moo_tensor_gradient_loeschen(W);
        moo_ag_reset();
        moo_release(out);
        moo_release(loss);
        if (k == warm_at) rss_warm = rss_kb();
    }
    long rss_end = rss_kb();

    /* Aufraeumen (ASan-sauber): W + idx. Tape ist bereits leer. */
    moo_release(W);
    moo_release(idx);

    double wachstum = (rss_warm > 0)
        ? (double)(rss_end - rss_warm) / (double)rss_warm : 0.0;
    printf("test_gather_leak: %ld iter, RSS warm=%ld kB end=%ld kB "
           "wachstum=%.2f%%\n", iters, rss_warm, rss_end, wachstum * 100.0);

#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
    /* Unter ASan wird die RSS durch Shadow-Memory + Quarantine dominiert und
     * ist KEIN Leak-Signal. Hier zaehlt einzig LeakSanitizer (at-exit). */
    (void)wachstum;
    printf("test_gather_leak: ASan-Modus — RSS-Check uebersprungen "
           "(LeakSanitizer ist hier das Gate)\n");
    return 0;
#else
    if (rss_warm > 0 && wachstum > 0.05) {
        fprintf(stderr, "LEAK-GATE FAIL: RSS-Wachstum %.2f%% > 5%%\n",
                wachstum * 100.0);
        return 1;
    }
    printf("test_gather_leak: OK (kein signifikantes RSS-Wachstum)\n");
    return 0;
#endif
}
