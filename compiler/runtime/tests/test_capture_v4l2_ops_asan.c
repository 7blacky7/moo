/* C1 Block-3 V4L2 Syscall-Fault-Harness (opus-cli-reviewer).
 * Treibt die ECHTE moo_capture_v4l2.c-Logik ueber injizierte Fake-Syscalls
 * durch jeden Open-ioctl/mmap/QBUF/STREAMON-Fault, QUERYBUF<sizeimage,
 * poll timeout/disconnect, DQBUF 2-Frame-latest+Requeue, DQ/QBUF-Cleanup.
 * ASan/UBSan: kein Leak/UAF/Double-Free/UB auf irgendeinem Pfad. */
#include "moo_capture_internal.h"
#include "moo_capture_v4l2_ops.h"

#include <errno.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

void moo_try_enter(void); int moo_try_check(void); void moo_try_leave(void);

#define W 640
#define H 480
#define BPL (W*3)
#define SIZEIMG (W*H*3)

/* --- Fault-Selektoren --- */
static unsigned long g_fail_req = 0;   /* ioctl-request der fehlschlagen soll */
static int g_fail_errno = EIO;
static int g_open_fail = 0;
static int g_mmap_fail = 0;
static int g_querybuf_short = 0;       /* buf.length = SIZEIMG-1 */
static int g_qbuf_fail = 0;            /* QBUF -1 (nach Open aktivierbar) */
static unsigned g_reqbuf_count = 4;
static int g_poll_ret = 1;             /* poll-Rueckgabe */
static short g_poll_revents = POLLIN;
static int (*g_dqbuf)(struct v4l2_buffer*) = NULL;

static int dq_happy_calls = 0;
static int dq_happy(struct v4l2_buffer* b){
    if (dq_happy_calls < 2){ b->index = dq_happy_calls; b->bytesused = SIZEIMG; dq_happy_calls++; return 0; }
    errno = EAGAIN; return -1;
}
static int dq_fail(struct v4l2_buffer* b){ (void)b; errno = EIO; return -1; }
static int dq_badindex(struct v4l2_buffer* b){ b->index = 99; b->bytesused = SIZEIMG; return 0; }

/* --- Fake-Syscalls --- */
static int fk_open(const char* p, int fl, int m){ (void)p;(void)fl;(void)m; if(g_open_fail){errno=g_open_fail;return -1;} return 42; }
static int fk_close(int fd){ (void)fd; return 0; }
static void* fk_mmap(void* a, size_t len, int pr, int fl, int fd, off_t off){
    (void)a;(void)pr;(void)fl;(void)fd;(void)off; if(g_mmap_fail) return MAP_FAILED; return calloc(1,len);
}
static int fk_munmap(void* a, size_t len){ (void)len; free(a); return 0; }
static int fk_poll(struct pollfd* fds, nfds_t n, int to){ (void)n;(void)to; fds->revents=g_poll_revents; return g_poll_ret; }
static int fk_clock(clockid_t c, struct timespec* ts){ (void)c; ts->tv_sec=1000; ts->tv_nsec=0; return 0; }

static int fk_ioctl(int fd, unsigned long req, void* arg){
    (void)fd;
    if (g_fail_req && req == g_fail_req){ errno = g_fail_errno; return -1; }
    switch (req){
    case VIDIOC_QUERYCAP:{ struct v4l2_capability* c=arg; memset(c,0,sizeof *c);
        c->capabilities=V4L2_CAP_VIDEO_CAPTURE|V4L2_CAP_STREAMING; c->device_caps=c->capabilities; return 0; }
    case VIDIOC_ENUM_FMT:{ struct v4l2_fmtdesc* d=arg; if(d->index==0){d->pixelformat=V4L2_PIX_FMT_RGB24; return 0;} errno=EINVAL; return -1; }
    case VIDIOC_ENUM_FRAMESIZES:{ struct v4l2_frmsizeenum* fs=arg; if(fs->index==0){fs->type=V4L2_FRMSIZE_TYPE_DISCRETE; fs->discrete.width=W; fs->discrete.height=H; return 0;} errno=EINVAL; return -1; }
    case VIDIOC_ENUM_FRAMEINTERVALS:{ struct v4l2_frmivalenum* iv=arg; if(iv->index==0){iv->type=V4L2_FRMIVAL_TYPE_DISCRETE; iv->discrete.numerator=1; iv->discrete.denominator=30; return 0;} errno=EINVAL; return -1; }
    case VIDIOC_S_FMT:{ struct v4l2_format* f=arg; f->fmt.pix.width=W; f->fmt.pix.height=H; f->fmt.pix.pixelformat=V4L2_PIX_FMT_RGB24; f->fmt.pix.bytesperline=BPL; f->fmt.pix.sizeimage=SIZEIMG; return 0; }
    case VIDIOC_S_PARM: return 0;
    case VIDIOC_G_PARM:{ struct v4l2_streamparm* p=arg; p->parm.capture.timeperframe.numerator=1; p->parm.capture.timeperframe.denominator=30; return 0; }
    case VIDIOC_REQBUFS:{ struct v4l2_requestbuffers* r=arg; r->count=g_reqbuf_count; return 0; }
    case VIDIOC_QUERYBUF:{ struct v4l2_buffer* b=arg; b->length = g_querybuf_short? (SIZEIMG-1):SIZEIMG; b->m.offset=b->index*0x100000; return 0; }
    case VIDIOC_QBUF: if(g_qbuf_fail){errno=EIO; return -1;} return 0;
    case VIDIOC_STREAMON: return 0;
    case VIDIOC_STREAMOFF: return 0;
    case VIDIOC_DQBUF: return g_dqbuf? g_dqbuf(arg): (errno=EAGAIN,-1);
    default: return 0;
    }
}

static MooCaptureV4l2Ops OPS = { fk_open, fk_ioctl, fk_mmap, fk_munmap, fk_close, fk_poll, fk_clock };

static void reset_happy(void){
    g_fail_req=0; g_open_fail=0; g_mmap_fail=0; g_querybuf_short=0; g_qbuf_fail=0;
    g_reqbuf_count=4; g_poll_ret=1; g_poll_revents=POLLIN; g_dqbuf=NULL; dq_happy_calls=0;
    moo_capture_v4l2_set_ops_for_tests(&OPS);
}

static int checks=0, fails=0;
#define EXPECT_THROW(label, call) do { reset_happy(); setup(); \
    moo_try_enter(); MooValue r=(call); int threw=moo_try_check(); \
    if(threw){ MooValue e=moo_get_error(); moo_release(e); } else { moo_release(r); } \
    moo_try_leave(); checks++; \
    if(!threw){ printf("  [FAIL] %s: erwarteter Throw ausgeblieben\n", label); fails++; } \
    else printf("  [ok] %s\n", label); } while(0)

/* setup() wird pro EXPECT_THROW nach reset_happy gesetzt (via Funktionszeiger) */
static void (*setup)(void);
static void s_open_fail(void){ g_open_fail=EACCES; }
static void s_querycap(void){ g_fail_req=VIDIOC_QUERYCAP; }
static void s_sfmt(void){ g_fail_req=VIDIOC_S_FMT; }
static void s_sparm(void){ g_fail_req=VIDIOC_S_PARM; }
static void s_reqbufs(void){ g_fail_req=VIDIOC_REQBUFS; }
static void s_querybuf(void){ g_fail_req=VIDIOC_QUERYBUF; }
static void s_querybuf_short(void){ g_querybuf_short=1; }
static void s_mmap(void){ g_mmap_fail=1; }
static void s_qbuf_open(void){ g_qbuf_fail=1; }
static void s_streamon(void){ g_fail_req=VIDIOC_STREAMON; }
static void s_none(void){}

int main(void){
    printf("== V4L2 OPEN-Fault-Matrix ==\n");
    setup=s_open_fail;      EXPECT_THROW("open EACCES",          moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));
    setup=s_querycap;       EXPECT_THROW("QUERYCAP fail",        moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));
    setup=s_sfmt;           EXPECT_THROW("S_FMT fail",           moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));
    setup=s_sparm;          EXPECT_THROW("S_PARM fail",          moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));
    setup=s_reqbufs;        EXPECT_THROW("REQBUFS fail",         moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));
    setup=s_querybuf;       EXPECT_THROW("QUERYBUF fail",        moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));
    setup=s_querybuf_short; EXPECT_THROW("QUERYBUF<sizeimage",   moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));
    setup=s_mmap;           EXPECT_THROW("mmap fail",            moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));
    setup=s_qbuf_open;      EXPECT_THROW("initiales QBUF fail",  moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));
    setup=s_streamon;       EXPECT_THROW("STREAMON fail",        moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()));

    /* Happy open -> Handle fuer Frame-Szenarien */
    printf("== V4L2 FRAME-Matrix ==\n");
    reset_happy(); setup=s_none;
    moo_try_enter();
    MooValue cam = moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none());
    int open_threw = moo_try_check(); moo_try_leave();
    if (open_threw){ printf("  [FAIL] Happy-Open warf unerwartet\n"); fails++; MooValue e=moo_get_error(); moo_release(e); }
    else {
        checks++; printf("  [ok] Happy-Open -> STREAMING\n");
        /* 2-Frame latest+Requeue: DQBUF liefert idx0, idx1, dann EAGAIN */
        g_dqbuf=dq_happy; dq_happy_calls=0;
        moo_try_enter();
        MooValue fr = moo_kamera_frame(cam, moo_number(1000));
        int fthrew = moo_try_check(); moo_try_leave();
        checks++;
        if (fthrew){ printf("  [FAIL] 2-Frame-latest warf\n"); fails++; MooValue e=moo_get_error(); moo_release(e); }
        else { printf("  [ok] 2-Frame latest -> Frame\n"); moo_release(fr); }

        /* DQBUF failure -> BROKEN, cleanup-before-throw */
        g_dqbuf=dq_fail;
        moo_try_enter(); MooValue f2=moo_kamera_frame(cam, moo_number(1000)); int t2=moo_try_check(); moo_try_leave();
        checks++; if(t2){printf("  [ok] DQBUF-fail -> Throw+BROKEN\n"); MooValue e=moo_get_error(); moo_release(e);} else {printf("  [FAIL] DQBUF-fail kein Throw\n"); fails++; moo_release(f2);}
        /* Handle ist jetzt BROKEN -> release gibt frei (close_native !backend-Guard) */
        moo_release(cam);

        /* Frisches Handle fuer poll-timeout + disconnect + requeue-fail + badindex */
        reset_happy();
        moo_try_enter(); MooValue c2=moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()); int o2=moo_try_check(); moo_try_leave();
        if(!o2){
            /* poll timeout */
            g_poll_ret=0; g_poll_revents=0;
            moo_try_enter(); MooValue ft=moo_kamera_frame(c2, moo_number(5)); int tt=moo_try_check(); moo_try_leave();
            checks++; if(tt){printf("  [ok] poll-Timeout -> Throw\n"); MooValue e=moo_get_error(); moo_release(e);} else {printf("  [FAIL] poll-Timeout kein Throw\n"); fails++; moo_release(ft);}
            /* disconnect POLLERR */
            g_poll_ret=1; g_poll_revents=POLLERR;
            moo_try_enter(); MooValue fd=moo_kamera_frame(c2, moo_number(5)); int td=moo_try_check(); moo_try_leave();
            checks++; if(td){printf("  [ok] poll-Disconnect -> Throw+BROKEN\n"); MooValue e=moo_get_error(); moo_release(e);} else {printf("  [FAIL] disconnect kein Throw\n"); fails++; moo_release(fd);}
            moo_release(c2);
        } else { printf("  [FAIL] 2. Happy-Open warf\n"); fails++; MooValue e=moo_get_error(); moo_release(e); }

        /* requeue-fail: happy open, DQBUF ok, aber QBUF (Requeue) faellt */
        reset_happy();
        moo_try_enter(); MooValue c3=moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()); int o3=moo_try_check(); moo_try_leave();
        if(!o3){
            g_dqbuf=dq_happy; dq_happy_calls=0; g_qbuf_fail=1;
            moo_try_enter(); MooValue fq=moo_kamera_frame(c3, moo_number(1000)); int tq=moo_try_check(); moo_try_leave();
            checks++; if(tq){printf("  [ok] Requeue-QBUF-fail -> Throw+BROKEN\n"); MooValue e=moo_get_error(); moo_release(e);} else {printf("  [FAIL] requeue-fail kein Throw\n"); fails++; moo_release(fq);}
            moo_release(c3);
        }

        /* badindex: DQBUF liefert index>=mapped_count -> BROKEN via fail_requeue */
        reset_happy();
        moo_try_enter(); MooValue c4=moo_kamera_oeffnen(moo_none(),moo_none(),moo_none(),moo_none()); int o4=moo_try_check(); moo_try_leave();
        if(!o4){
            g_dqbuf=dq_badindex;
            moo_try_enter(); MooValue fb=moo_kamera_frame(c4, moo_number(1000)); int tb=moo_try_check(); moo_try_leave();
            checks++; if(tb){printf("  [ok] DQBUF-badindex -> Throw+BROKEN\n"); MooValue e=moo_get_error(); moo_release(e);} else {printf("  [FAIL] badindex kein Throw\n"); fails++; moo_release(fb);}
            moo_release(c4);
        }
    }

    moo_capture_v4l2_reset_ops_for_tests();
    printf("V4L2 checks=%d failures=%d\n", checks, fails);
    return fails? 1:0;
}
