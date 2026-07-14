/**
 * bench_shard_ram.c — KIP-E1 RAM-Gate (Korpus >> RAM). KEIN Sanitizer.
 * ============================================================================
 * Zwei Modi (getrennte Prozesse, damit Peak-RSS nur die jeweilige Seite misst):
 *   write <pfad> : schreibt ein 20M-Token-Shard (~80 MB) via moo_shard_schreiben.
 *   read  <pfad> : liest ALLE seq_len-Bloecke gefenstert und meldet Peak-RSS.
 * Das RAM-Gate ist erfuellt, wenn der Lese-Prozess-Peak << Shardgroesse bleibt
 * (der Loader haelt nur Header + Blockreihenfolge + ein Fenster im RAM).
 * ============================================================================
 */
#include "../moo_shard.h"
#include <sys/resource.h>
#include <string.h>

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) { moo_release(error); moo_error_flag = 1; }

void moo_socket_free(void* p){(void)p;} void moo_thread_free(void* p){(void)p;}
void moo_channel_free(void* p){(void)p;} void moo_db_free(void* p){(void)p;}
void moo_db_stmt_free(void* p){(void)p;} void moo_window_free(void* p){(void)p;}
void moo_web_free(void* p){(void)p;} void moo_voxel_free(void* p){(void)p;}
void moo_frame_free(void* p){(void)p;} void moo_gif_handle_free(void* p){(void)p;}
void moo_video_handle_free(void* p){(void)p;}

static long peak_rss_kb(void) {
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;   /* Linux: KB */
}

#define N_DOCS   20
#define DOC_LEN  1000000       /* 20 * 1e6 = 20M Tokens => ~80 MB u32 */
#define SEQ_LEN  1024

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s write|read <pfad>\n", argv[0]); return 2; }
    const char* mode = argv[1]; const char* pfad = argv[2];

    if (strcmp(mode, "write") == 0) {
        MooValue docs = moo_list_new(N_DOCS);
        for (int d = 0; d < N_DOCS; d++) {
            int32_t shape[1] = { DOC_LEN };
            MooTensor* t = moo_tensor_raw(1, shape);
            for (int64_t i = 0; i < DOC_LEN; i++) t->data[i] = (float)((d * 131 + i) % 60000);
            MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
            moo_list_append(docs, v);
        }
        MooValue pf = moo_string_new(pfad), da = moo_string_new("pretrain"), tv = moo_string_new("0");
        MooValue ok = moo_shard_schreiben(pf, docs, da, tv, moo_none());
        moo_release(pf); moo_release(da); moo_release(tv); moo_release(docs);
        if (moo_error_flag || !MV_BOOL(ok)) { fprintf(stderr, "WRITE-FAIL\n"); return 1; }
        fprintf(stderr, "WRITE OK: %d Tokens\n", N_DOCS * DOC_LEN);
        return 0;
    }
    if (strcmp(mode, "read") == 0) {
        MooValue pf = moo_string_new(pfad);
        MooValue order = moo_shard_reihenfolge(pf, moo_number(42), moo_number(SEQ_LEN));
        moo_release(pf);
        if (moo_error_flag || order.tag != MOO_LIST) { fprintf(stderr, "READ-FAIL order\n"); return 1; }
        int32_t nb = MV_LIST(order)->length;
        double sink = 0.0;
        for (int32_t i = 0; i < nb; i++) {
            double off = MV_NUM(MV_LIST(order)->items[i]);
            MooValue p2 = moo_string_new(pfad);
            MooValue w = moo_shard_fenster(p2, moo_number(off), moo_number(SEQ_LEN));
            moo_release(p2);
            if (moo_error_flag || w.tag != MOO_TENSOR) { fprintf(stderr, "READ-FAIL fenster @%d\n", i); return 1; }
            sink += (double)MV_TENSOR(w)->data[0] + (double)MV_TENSOR(w)->data[SEQ_LEN-1];
            moo_release(w);
        }
        moo_release(order);
        long peak = peak_rss_kb();
        long shard_mb = (long)N_DOCS * DOC_LEN * 4 / (1024*1024);
        fprintf(stderr, "READ OK: %d Bloecke, sink=%.0f, PEAK_KB=%ld (Shard ~%ld MB)\n", nb, sink, peak, shard_mb);
        printf("PEAK_KB=%ld\n", peak);
        return 0;
    }
    fprintf(stderr, "unbekannter Modus\n"); return 2;
}
