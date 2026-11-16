/* P2-T112: Mikrobenchmark retain+release je Paar gegen ein moo_memory.o.
 * Bauen: cc -O2 rc_mikro.c <moo_memory.o> -lpthread -o rc_mikro
 * Aufruf: rc_mikro N THREADS GETEILT_OBJEKT VORMARKIERT
 *   GETEILT_OBJEKT 1: alle Threads auf demselben Objekt, 0: je Thread eigenes
 *   VORMARKIERT    1: Zaehler startet als geteilt (-1, nur Runtime nachher). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
typedef struct { uint64_t tag; uint64_t data; } MooValue;
void moo_retain(MooValue v);
void moo_release(MooValue v);
#define STUB(n) void n(void) {}
STUB(moo_buffer_free) STUB(moo_channel_free) STUB(moo_db_free) STUB(moo_db_stmt_free)
STUB(moo_error) STUB(moo_frame_free) STUB(moo_gif_handle_free) STUB(moo_kamera_free)
STUB(moo_mikro_free) STUB(moo_socket_free) STUB(moo_struct_box_free) STUB(moo_surface_free)
STUB(moo_tensor_free) STUB(moo_thread_free) STUB(moo_throw) STUB(moo_video_handle_free)
STUB(moo_voxel_free) STUB(moo_web_free) STUB(moo_window_free) STUB(moo_struct_box_teilen)
typedef struct { int32_t rc; int32_t pad[15]; } Obj;
static long N;
static void* lauf(void* p) {
    MooValue v = { 1, (uint64_t)(uintptr_t)p };  /* Tag 1 = MOO_STRING */
    for (long i = 0; i < N; i++) { moo_retain(v); moo_release(v); }
    return NULL;
}
static double jetzt(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    N = atol(argv[1]); int threads = atoi(argv[2]); int geteilt = atoi(argv[3]); int markiert = atoi(argv[4]);
    Obj* objs = aligned_alloc(64, sizeof(Obj) * 64);
    for (int i = 0; i < 64; i++) objs[i].rc = markiert ? -1 : 1;
    pthread_t th[64];
    double t0 = jetzt();
    for (int i = 0; i < threads; i++) pthread_create(&th[i], NULL, lauf, geteilt ? &objs[0] : &objs[i]);
    for (int i = 0; i < threads; i++) pthread_join(th[i], NULL);
    double t = jetzt() - t0;
    printf("%.3f ns/Paar rc_ende=%d\n", t * 1e9 / N, objs[0].rc);
    return 0;
}
