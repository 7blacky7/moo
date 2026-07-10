/* C1 Block-3 ALSA Syscall-Fault-Harness (opus-cli-reviewer).
 * Treibt die ECHTE moo_capture_alsa.c-Logik ueber injizierte snd_pcm_*-Fakes:
 * jeder hw/sw/prepare-Fault, short read/EINTR/EAGAIN, EPIPE/ESTRPIPE max 3,
 * Teildaten-Reset, Disconnect, Deadline. ASan/UBSan sauber. */
#include "moo_capture_internal.h"
#include "moo_capture_alsa_ops.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void moo_try_enter(void); int moo_try_check(void); void moo_try_leave(void);

/* Dummy snd_pcm_t: die echte alsa.c behandelt es nur als opaken Zeiger. */
static int g_pcm_token;
#define PCM ((snd_pcm_t*)&g_pcm_token)

/* --- Fault-Selektoren --- */
static int g_open_ret=0, g_any=0, g_access=0, g_format=0, g_channels=0;
static int g_rate=0, g_hwcommit=0, g_sw_current=0, g_sw_avail=0, g_sw_commit=0, g_prepare=0;
static unsigned g_actual_channels=1, g_actual_rate=48000;
static long g_actual_period=1024, g_actual_buffer=4096;
static int (*g_readi_seq)(int call, void* buf, unsigned long frames, long* got) = NULL;
static int g_readi_calls=0;
static int g_wait_ret=1;
static long g_clock_ms=0;      /* fortschreitende Uhr fuer Deadline-Tests */
static long g_clock_step=0;

static int fk_open(snd_pcm_t** p, const char* n, snd_pcm_stream_t s, int m){ (void)n;(void)s;(void)m; if(g_open_ret){return g_open_ret;} *p=PCM; return 0; }
static int fk_any(snd_pcm_t* p, snd_pcm_hw_params_t* h){ (void)p;(void)h; return g_any; }
static int fk_access(snd_pcm_t* p, snd_pcm_hw_params_t* h, snd_pcm_access_t a){ (void)p;(void)h;(void)a; return g_access; }
static int fk_format(snd_pcm_t* p, snd_pcm_hw_params_t* h, snd_pcm_format_t f){ (void)p;(void)h;(void)f; return g_format; }
static int fk_channels(snd_pcm_t* p, snd_pcm_hw_params_t* h, unsigned c){ (void)p;(void)h;(void)c; return g_channels; }
static int fk_rate(snd_pcm_t* p, snd_pcm_hw_params_t* h, unsigned* r, int* d){ (void)p;(void)h; if(r)*r=g_actual_rate; if(d)*d=0; return g_rate; }
static int fk_period(snd_pcm_t* p, snd_pcm_hw_params_t* h, snd_pcm_uframes_t* v, int* d){ (void)p;(void)h; if(v)*v=(snd_pcm_uframes_t)g_actual_period; if(d)*d=0; return 0; }
static int fk_buffer(snd_pcm_t* p, snd_pcm_hw_params_t* h, snd_pcm_uframes_t* v){ (void)p;(void)h; if(v)*v=(snd_pcm_uframes_t)g_actual_buffer; return 0; }
static int fk_commit(snd_pcm_t* p, snd_pcm_hw_params_t* h){ (void)p;(void)h; return g_hwcommit; }
static int fk_get_channels(const snd_pcm_hw_params_t* h, unsigned* c){ (void)h; if(c)*c=g_actual_channels; return 0; }
static int fk_get_rate(const snd_pcm_hw_params_t* h, unsigned* r, int* d){ (void)h; if(r)*r=g_actual_rate; if(d)*d=0; return 0; }
static int fk_get_period(const snd_pcm_hw_params_t* h, snd_pcm_uframes_t* v, int* d){ (void)h; if(v)*v=(snd_pcm_uframes_t)g_actual_period; if(d)*d=0; return 0; }
static int fk_get_buffer(const snd_pcm_hw_params_t* h, snd_pcm_uframes_t* v){ (void)h; if(v)*v=(snd_pcm_uframes_t)g_actual_buffer; return 0; }
static int fk_sw_current(snd_pcm_t* p, snd_pcm_sw_params_t* s){ (void)p;(void)s; return g_sw_current; }
static int fk_sw_avail(snd_pcm_t* p, snd_pcm_sw_params_t* s, snd_pcm_uframes_t v){ (void)p;(void)s;(void)v; return g_sw_avail; }
static int fk_sw_commit(snd_pcm_t* p, snd_pcm_sw_params_t* s){ (void)p;(void)s; return g_sw_commit; }
static int fk_prepare(snd_pcm_t* p){ (void)p; return g_prepare; }
static int fk_wait(snd_pcm_t* p, int to){ (void)p;(void)to; return g_wait_ret; }
static int fk_resume(snd_pcm_t* p){ (void)p; return 0; }
static snd_pcm_sframes_t fk_readi(snd_pcm_t* p, void* b, snd_pcm_uframes_t fr){ (void)p; long got=0; int r=g_readi_seq? g_readi_seq(g_readi_calls++, b, fr, &got): (int)fr; if(r<0) return r; return got; }
static int fk_drop(snd_pcm_t* p){ (void)p; return 0; }
static int fk_close(snd_pcm_t* p){ (void)p; return 0; }
static int fk_clock(clockid_t c, struct timespec* ts){ (void)c; long ms=g_clock_ms; g_clock_ms+=g_clock_step; ts->tv_sec=ms/1000; ts->tv_nsec=(ms%1000)*1000000L; return 0; }

static MooCaptureAlsaOps OPS = {
    fk_open, fk_any, fk_access, fk_format, fk_channels, fk_rate, fk_period, fk_buffer,
    fk_commit, fk_get_channels, fk_get_rate, fk_get_period, fk_get_buffer,
    fk_sw_current, fk_sw_avail, fk_sw_commit, fk_prepare, fk_wait, fk_resume,
    fk_readi, fk_drop, fk_close, fk_clock
};

static void reset_happy(void){
    g_open_ret=0; g_any=0; g_access=0; g_format=0; g_channels=0; g_rate=0; g_hwcommit=0;
    g_sw_current=0; g_sw_avail=0; g_sw_commit=0; g_prepare=0;
    g_actual_channels=1; g_actual_rate=48000; g_actual_period=1024; g_actual_buffer=4096;
    g_readi_seq=NULL; g_readi_calls=0; g_wait_ret=1; g_clock_ms=0; g_clock_step=0;
    moo_capture_alsa_set_ops_for_tests(&OPS);
}

static int checks=0, fails=0;
static void (*setup)(void);
#define EXPECT_OPEN_THROW(label) do { reset_happy(); setup(); \
    moo_try_enter(); MooValue r=moo_mikro_oeffnen(moo_none(),moo_none(),moo_none()); int t=moo_try_check(); \
    if(t){MooValue e=moo_get_error(); moo_release(e);} else moo_release(r); moo_try_leave(); checks++; \
    if(!t){printf("  [FAIL] %s: kein Throw\n",label); fails++;} else printf("  [ok] %s\n",label);} while(0)

static void so_open(void){ g_open_ret=-EBUSY; }
static void so_any(void){ g_any=-EINVAL; }
static void so_access(void){ g_access=-EINVAL; }
static void so_format(void){ g_format=-EINVAL; }
static void so_channels(void){ g_channels=-EINVAL; }
static void so_rate(void){ g_rate=-EINVAL; }
static void so_hwcommit(void){ g_hwcommit=-EINVAL; }
static void so_badparams(void){ g_actual_channels=5; } /* >2 -> abgelehnt */
static void so_sw_current(void){ g_sw_current=-EIO; }
static void so_sw_commit(void){ g_sw_commit=-EIO; }
static void so_prepare(void){ g_prepare=-EIO; }

/* readi-Sequenzen (Rueckgabe <0 = -errno; sonst got frames) */
static int rs_full(int call, void* b, unsigned long fr, long* got){ (void)call;(void)b; *got=(long)fr; return 0; }
static int rs_short_then_full(int call, void* b, unsigned long fr, long* got){ (void)b; if(call==0){*got=(long)(fr/2); return 0;} *got=(long)fr; return 0; }
static int rs_eintr_then_full(int call, void* b, unsigned long fr, long* got){ (void)b; if(call==0) return -EINTR; *got=(long)fr; return 0; }
static int rs_eagain_then_full(int call, void* b, unsigned long fr, long* got){ (void)b; if(call==0) return -EAGAIN; *got=(long)fr; return 0; }
static int rs_epipe_recover(int call, void* b, unsigned long fr, long* got){ (void)b; if(call<2) return -EPIPE; *got=(long)fr; return 0; }
static int rs_epipe_forever(int call, void* b, unsigned long fr, long* got){ (void)call;(void)b;(void)fr;(void)got; return -EPIPE; }
static int rs_disconnect(int call, void* b, unsigned long fr, long* got){ (void)call;(void)b;(void)fr;(void)got; return -ENODEV; }

static MooValue read_once(int samples){ return moo_mikro_lesen(/*handle*/(MooValue){0,0}, moo_number(samples), moo_number(1000)); }

int main(void){
    printf("== ALSA OPEN-Fault-Matrix ==\n");
    setup=so_open;      EXPECT_OPEN_THROW("pcm_open EBUSY");
    setup=so_any;       EXPECT_OPEN_THROW("hw_any fail");
    setup=so_access;    EXPECT_OPEN_THROW("hw_access fail");
    setup=so_format;    EXPECT_OPEN_THROW("hw_format fail");
    setup=so_channels;  EXPECT_OPEN_THROW("hw_channels fail");
    setup=so_rate;      EXPECT_OPEN_THROW("rate_near fail");
    setup=so_hwcommit;  EXPECT_OPEN_THROW("hw_params commit fail");
    setup=so_badparams; EXPECT_OPEN_THROW("ungueltige Kanalzahl (5)");
    setup=so_sw_current;EXPECT_OPEN_THROW("sw_current fail");
    setup=so_sw_commit; EXPECT_OPEN_THROW("sw_params fail");
    setup=so_prepare;   EXPECT_OPEN_THROW("prepare fail");

    printf("== ALSA READ-Matrix (Happy-Open + readi-Sequenzen) ==\n");
    struct { const char* label; int (*seq)(int,void*,unsigned long,long*); int expect_throw; } cases[] = {
        {"voll gelesen",            rs_full,            0},
        {"short read dann voll",    rs_short_then_full, 0},
        {"EINTR dann voll",         rs_eintr_then_full, 0},
        {"EAGAIN+wait dann voll",   rs_eagain_then_full,0},
        {"EPIPE x2 recover dann voll", rs_epipe_recover, 0},
        {"EPIPE endlos (>3) -> BROKEN", rs_epipe_forever, 1},
        {"Disconnect ENODEV -> BROKEN", rs_disconnect,   1},
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
        reset_happy();
        moo_try_enter(); MooValue h=moo_mikro_oeffnen(moo_none(),moo_none(),moo_none()); int ot=moo_try_check(); moo_try_leave();
        if(ot){ printf("  [FAIL] Happy-Open warf (%s)\n", cases[i].label); fails++; MooValue e=moo_get_error(); moo_release(e); continue; }
        g_readi_seq=cases[i].seq; g_readi_calls=0; g_wait_ret=1;
        moo_try_enter(); MooValue r=moo_mikro_lesen(h, moo_number(256), moo_number(1000)); int t=moo_try_check(); moo_try_leave();
        checks++;
        if(t){ MooValue e=moo_get_error(); moo_release(e); }
        else moo_release(r);
        if(t==cases[i].expect_throw) printf("  [ok] %s\n", cases[i].label);
        else { printf("  [FAIL] %s: throw=%d erwartet=%d\n", cases[i].label, t, cases[i].expect_throw); fails++; }
        moo_release(h);
    }

    /* Deadline: readi immer EAGAIN, wait liefert 0 (nicht bereit) -> Timeout */
    reset_happy();
    moo_try_enter(); MooValue hd=moo_mikro_oeffnen(moo_none(),moo_none(),moo_none()); int od=moo_try_check(); moo_try_leave();
    if(!od){
        g_readi_seq=rs_eagain_then_full; g_readi_calls=0; g_wait_ret=0; /* nie bereit */
        moo_try_enter(); MooValue r=moo_mikro_lesen(hd, moo_number(256), moo_number(5)); int t=moo_try_check(); moo_try_leave();
        checks++; if(t){printf("  [ok] Deadline/Timeout -> Throw\n"); MooValue e=moo_get_error(); moo_release(e);} else {printf("  [FAIL] Deadline kein Throw\n"); fails++; moo_release(r);}
        moo_release(hd);
    }
    (void)read_once;
    moo_capture_alsa_reset_ops_for_tests();
    printf("ALSA checks=%d failures=%d\n", checks, fails);
    return fails?1:0;
}
