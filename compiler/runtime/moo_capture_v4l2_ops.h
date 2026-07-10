#ifndef MOO_CAPTURE_V4L2_OPS_H
#define MOO_CAPTURE_V4L2_OPS_H

#include <poll.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

typedef struct {
    int (*open_device)(const char*, int, int);
    int (*ioctl_device)(int, unsigned long, void*);
    void* (*mmap_device)(void*, size_t, int, int, int, off_t);
    int (*munmap_device)(void*, size_t);
    int (*close_device)(int);
    int (*poll_wait)(struct pollfd*, nfds_t, int);
    int (*clock_now)(clockid_t, struct timespec*);
} MooCaptureV4l2Ops;

void moo_capture_v4l2_set_ops_for_tests(const MooCaptureV4l2Ops* ops);
void moo_capture_v4l2_reset_ops_for_tests(void);

#endif
