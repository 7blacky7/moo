#include "moo_capture_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

#include "moo_capture_v4l2_ops.h"

static int system_open_device(const char* path, int flags, int mode) {
    return v4l2_open(path, flags, mode);
}
static int system_ioctl_device(int fd, unsigned long request, void* argument) {
    return v4l2_ioctl(fd, request, argument);
}
static void* system_mmap_device(void* address, size_t length, int protection,
                                int flags, int fd, off_t offset) {
    return v4l2_mmap(address, length, protection, flags, fd, offset);
}
static int system_munmap_device(void* address, size_t length) {
    return v4l2_munmap(address, length);
}
static int system_close_device(int fd) { return v4l2_close(fd); }
static int system_poll_wait(struct pollfd* fds, nfds_t count, int timeout) {
    return poll(fds, count, timeout);
}
static int system_clock_now(clockid_t clock, struct timespec* value) {
    return clock_gettime(clock, value);
}

static const MooCaptureV4l2Ops system_ops = {
    system_open_device, system_ioctl_device, system_mmap_device,
    system_munmap_device, system_close_device, system_poll_wait,
    system_clock_now
};
static MooCaptureV4l2Ops selected_ops;
static bool selected_ops_ready = false;
static const MooCaptureV4l2Ops* v4l2_ops(void) {
    if (!selected_ops_ready) {
        selected_ops = system_ops;
        selected_ops_ready = true;
    }
    return &selected_ops;
}
void moo_capture_v4l2_set_ops_for_tests(const MooCaptureV4l2Ops* ops) {
    selected_ops = ops ? *ops : system_ops;
    selected_ops_ready = true;
}
void moo_capture_v4l2_reset_ops_for_tests(void) {
    selected_ops = system_ops;
    selected_ops_ready = true;
}

#define v4l2_open(path, flags, mode) v4l2_ops()->open_device((path), (flags), (mode))
#define v4l2_ioctl(fd, req, arg) v4l2_ops()->ioctl_device((fd), (req), (arg))
#define v4l2_mmap(addr, len, prot, flags, fd, off) v4l2_ops()->mmap_device((addr), (len), (prot), (flags), (fd), (off))
#define v4l2_munmap(addr, len) v4l2_ops()->munmap_device((addr), (len))
#define v4l2_close(fd) v4l2_ops()->close_device((fd))
#define poll(fds, count, timeout) v4l2_ops()->poll_wait((fds), (count), (timeout))
#define clock_gettime(clock, value) v4l2_ops()->clock_now((clock), (value))

typedef struct { void* start; size_t length; } CameraBuffer;
typedef struct {
    int fd;
    bool streaming;
    uint32_t pixel_format;
    uint32_t bytes_per_line;
    uint32_t size_image;
    uint32_t mapped_count;
    CameraBuffer buffers[MOO_CAPTURE_MAX_BUFFERS];
} CameraNative;

typedef struct {
    uint32_t width, height, fourcc;
    double fps;
    double distance;
} CameraCandidate;

#define MAX_CANDIDATES 1024

static MooValue camera_fail(const char* message) {
    moo_throw(moo_error(message));
    return moo_none();
}

static bool camera_open_error(MooKamera* camera, const char* message) {
    snprintf(camera->last_error, sizeof(camera->last_error), "%s", message);
    return false;
}

static MooValue camera_broken(MooKamera* camera, const char* message) {
    MooValue error = moo_error(message);
    camera->state = MOO_CAPTURE_BROKEN;
    moo_capture_camera_close_native(camera);
    moo_throw(error);
    return moo_none();
}

static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int remaining_ms(int64_t deadline) {
    int64_t now = monotonic_ms();
    if (now < 0 || now >= deadline) return 0;
    int64_t left = deadline - now;
    return left > INT32_MAX ? INT32_MAX : (int)left;
}

static bool checked_frame(uint32_t width, uint32_t height, size_t* bytes) {
    if (!width || !height || width > MOO_CAPTURE_MAX_WIDTH ||
        height > MOO_CAPTURE_MAX_HEIGHT) return false;
    uint64_t total = (uint64_t)width * (uint64_t)height * 4u;
    if (total > MOO_CAPTURE_MAX_FRAME_BYTES || total > SIZE_MAX) return false;
    *bytes = (size_t)total;
    return true;
}

static void candidate_add(CameraCandidate* out, size_t* count,
                          uint32_t w, uint32_t h, double fps, uint32_t fourcc,
                          uint32_t rw, uint32_t rh, double rfps) {
    if (*count >= MAX_CANDIDATES || !w || !h || !isfinite(fps) || fps <= 0.0)
        return;
    if (w > MOO_CAPTURE_MAX_WIDTH || h > MOO_CAPTURE_MAX_HEIGHT) return;
    for (size_t i = 0; i < *count; ++i)
        if (out[i].width == w && out[i].height == h &&
            fabs(out[i].fps - fps) < 1e-9 && out[i].fourcc == fourcc) return;
    CameraCandidate* c = &out[(*count)++];
    c->width = w; c->height = h; c->fps = fps; c->fourcc = fourcc;
    c->distance = fabs((double)w - rw) / rw +
                  fabs((double)h - rh) / rh + fabs(fps - rfps) / rfps;
}

static uint32_t step_near(uint32_t value, uint32_t min, uint32_t max,
                          uint32_t step, bool up) {
    if (value <= min) return min;
    if (value >= max) return max;
    if (!step) step = 1;
    uint64_t delta = value - min;
    uint64_t n = delta / step + (up && delta % step ? 1 : 0);
    uint64_t result = (uint64_t)min + n * step;
    return result > max ? max : (uint32_t)result;
}

static void enumerate_intervals(int fd, uint32_t fourcc, uint32_t w, uint32_t h,
                                CameraCandidate* out, size_t* count,
                                uint32_t rw, uint32_t rh, double rfps) {
    bool found = false;
    for (uint32_t i = 0; i < 256; ++i) {
        struct v4l2_frmivalenum iv = {0};
        iv.index = i; iv.pixel_format = fourcc; iv.width = w; iv.height = h;
        if (v4l2_ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &iv) < 0) break;
        found = true;
        if (iv.type == V4L2_FRMIVAL_TYPE_DISCRETE && iv.discrete.numerator)
            candidate_add(out, count, w, h,
                          (double)iv.discrete.denominator / iv.discrete.numerator,
                          fourcc, rw, rh, rfps);
        else if ((iv.type == V4L2_FRMIVAL_TYPE_STEPWISE ||
                  iv.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) &&
                 iv.stepwise.min.numerator && iv.stepwise.max.numerator) {
            double fast = (double)iv.stepwise.min.denominator /
                          iv.stepwise.min.numerator;
            double slow = (double)iv.stepwise.max.denominator /
                          iv.stepwise.max.numerator;
            candidate_add(out, count, w, h, fast, fourcc, rw, rh, rfps);
            candidate_add(out, count, w, h, slow, fourcc, rw, rh, rfps);
            double chosen = rfps < slow ? slow : (rfps > fast ? fast : rfps);
            candidate_add(out, count, w, h, chosen, fourcc, rw, rh, rfps);
        }
    }
    if (!found) candidate_add(out, count, w, h, rfps, fourcc, rw, rh, rfps);
}

static void enumerate_size(int fd, uint32_t fourcc, uint32_t rw, uint32_t rh,
                           double rfps, CameraCandidate* out, size_t* count) {
    for (uint32_t i = 0; i < 256; ++i) {
        struct v4l2_frmsizeenum fs = {0};
        fs.index = i; fs.pixel_format = fourcc;
        if (v4l2_ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) < 0) break;
        if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            enumerate_intervals(fd, fourcc, fs.discrete.width, fs.discrete.height,
                                out, count, rw, rh, rfps);
        } else {
            uint32_t ws[4] = {
                fs.stepwise.min_width, fs.stepwise.max_width,
                step_near(rw, fs.stepwise.min_width, fs.stepwise.max_width,
                          fs.stepwise.step_width, false),
                step_near(rw, fs.stepwise.min_width, fs.stepwise.max_width,
                          fs.stepwise.step_width, true)
            };
            uint32_t hs[4] = {
                fs.stepwise.min_height, fs.stepwise.max_height,
                step_near(rh, fs.stepwise.min_height, fs.stepwise.max_height,
                          fs.stepwise.step_height, false),
                step_near(rh, fs.stepwise.min_height, fs.stepwise.max_height,
                          fs.stepwise.step_height, true)
            };
            for (size_t wi = 0; wi < 4; ++wi)
                for (size_t hi = 0; hi < 4; ++hi)
                    enumerate_intervals(fd, fourcc, ws[wi], hs[hi], out, count,
                                        rw, rh, rfps);
        }
    }
}

static int candidate_cmp(const void* av, const void* bv) {
    const CameraCandidate* a = av; const CameraCandidate* b = bv;
    if (a->distance < b->distance) return -1;
    if (a->distance > b->distance) return 1;
    uint64_t ap = (uint64_t)a->width * a->height;
    uint64_t bp = (uint64_t)b->width * b->height;
    if (ap < bp) return -1;
    if (ap > bp) return 1;
    if (a->fps > b->fps) return -1;
    if (a->fps < b->fps) return 1;
    return a->fourcc < b->fourcc ? -1 : (a->fourcc > b->fourcc ? 1 : 0);
}

MooValue moo_capture_camera_list_native(void) {
    MooValue list = moo_list_new(4);
    glob_t matches = {0};
    if (glob("/dev/video*", 0, NULL, &matches) != 0) return list;
    for (size_t i = 0; i < matches.gl_pathc; ++i) {
        int fd = v4l2_open(matches.gl_pathv[i], O_RDWR | O_NONBLOCK, 0);
        if (fd < 0) continue;
        struct v4l2_capability cap = {0};
        if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            uint32_t caps = cap.device_caps ? cap.device_caps : cap.capabilities;
            if ((caps & V4L2_CAP_VIDEO_CAPTURE) && (caps & V4L2_CAP_STREAMING)) {
                MooValue item = moo_dict_new();
                moo_dict_set(item, moo_string_new("pfad"),
                             moo_string_new(matches.gl_pathv[i]));
                moo_dict_set(item, moo_string_new("name"),
                             moo_string_new((const char*)cap.card));
                moo_dict_set(item, moo_string_new("id"),
                             moo_string_new((const char*)cap.bus_info));
                moo_list_append(list, item);
            }
        }
        v4l2_close(fd);
    }
    globfree(&matches);
    return list;
}

bool moo_capture_camera_open_native(MooKamera* camera, const char* path,
                                    int32_t width, int32_t height, double fps,
                                    bool require_exact) {
    const char* device = path ? path : "/dev/video0";
    CameraNative* native = calloc(1, sizeof(*native));
    if (!native) return camera_open_error(camera, "kamera_oeffnen: Speicher voll");
    native->fd = -1;
    camera->backend = native;
    native->fd = v4l2_open(device, O_RDWR | O_NONBLOCK, 0);
    if (native->fd < 0) {
        return camera_open_error(camera,
            errno == EACCES ? "kamera_oeffnen: keine Berechtigung" :
            (errno == EBUSY ? "kamera_oeffnen: Geraet belegt" :
             "kamera_oeffnen: Geraet konnte nicht geoeffnet werden"));
    }
    struct v4l2_capability cap = {0};
    if (v4l2_ioctl(native->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        return camera_open_error(camera, "kamera_oeffnen: VIDIOC_QUERYCAP fehlgeschlagen");
    }
    uint32_t caps = cap.device_caps ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        return camera_open_error(camera, "kamera_oeffnen: Capture/Streaming nicht unterstuetzt (nur Single-Plane in v1)");
    }

    CameraCandidate candidates[MAX_CANDIDATES];
    size_t count = 0;
    for (uint32_t i = 0; i < 256; ++i) {
        struct v4l2_fmtdesc desc = {0};
        desc.index = i; desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (v4l2_ioctl(native->fd, VIDIOC_ENUM_FMT, &desc) < 0) break;
        if (desc.pixelformat == V4L2_PIX_FMT_RGB24 ||
            desc.pixelformat == V4L2_PIX_FMT_BGR24)
            enumerate_size(native->fd, desc.pixelformat, width, height, fps,
                           candidates, &count);
    }
    if (!count) {
        return camera_open_error(camera, "kamera_oeffnen: kein unterstuetztes RGB24/BGR24-Format");
    }
    qsort(candidates, count, sizeof(candidates[0]), candidate_cmp);
    size_t chosen = SIZE_MAX;
    for (size_t i = 0; i < count; ++i) {
        bool exact = candidates[i].width == (uint32_t)width &&
                     candidates[i].height == (uint32_t)height &&
                     fabs(candidates[i].fps - fps) < 1e-6;
        if (!require_exact || exact) { chosen = i; break; }
    }
    if (chosen == SIZE_MAX) {
        return camera_open_error(camera, "kamera_oeffnen: exaktes Format/FPS nicht unterstuetzt");
    }
    CameraCandidate c = candidates[chosen];
    struct v4l2_format format = {0};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = c.width; format.fmt.pix.height = c.height;
    format.fmt.pix.pixelformat = c.fourcc;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if (v4l2_ioctl(native->fd, VIDIOC_S_FMT, &format) < 0) {
        return camera_open_error(camera, "kamera_oeffnen: S_FMT fehlgeschlagen");
    }
    size_t frame_bytes = 0;
    if ((format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB24 &&
         format.fmt.pix.pixelformat != V4L2_PIX_FMT_BGR24) ||
        !checked_frame(format.fmt.pix.width, format.fmt.pix.height, &frame_bytes) ||
        format.fmt.pix.bytesperline < format.fmt.pix.width * 3u ||
        (uint64_t)format.fmt.pix.sizeimage <
            (uint64_t)format.fmt.pix.bytesperline * format.fmt.pix.height ||
        format.fmt.pix.sizeimage > MOO_CAPTURE_MAX_FRAME_BYTES ||
        (require_exact && (format.fmt.pix.width != (uint32_t)width ||
                           format.fmt.pix.height != (uint32_t)height))) {
        return camera_open_error(camera, "kamera_oeffnen: Treiber lieferte ungueltige Dimensionen/Strides");
    }

    struct v4l2_streamparm parm = {0};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1000000;
    parm.parm.capture.timeperframe.denominator = (uint32_t)llround(c.fps * 1000000.0);
    if (v4l2_ioctl(native->fd, VIDIOC_S_PARM, &parm) < 0 ||
        v4l2_ioctl(native->fd, VIDIOC_G_PARM, &parm) < 0 ||
        !parm.parm.capture.timeperframe.numerator) {
        return camera_open_error(camera, "kamera_oeffnen: FPS-Verhandlung fehlgeschlagen");
    }
    double actual_fps = (double)parm.parm.capture.timeperframe.denominator /
                        parm.parm.capture.timeperframe.numerator;
    if (require_exact && fabs(actual_fps - fps) > 1e-6) {
        return camera_open_error(camera, "kamera_oeffnen: exakte FPS nicht verfuegbar");
    }

    struct v4l2_requestbuffers req = {0};
    req.count = 4; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(native->fd, VIDIOC_REQBUFS, &req) < 0 ||
        !req.count || req.count > MOO_CAPTURE_MAX_BUFFERS) {
        return camera_open_error(camera, "kamera_oeffnen: ungueltige REQBUFS-Antwort");
    }
    native->size_image = format.fmt.pix.sizeimage;
    native->mapped_count = req.count;
    for (uint32_t i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (v4l2_ioctl(native->fd, VIDIOC_QUERYBUF, &buf) < 0 ||
            !buf.length || buf.length < native->size_image ||
            buf.length > MOO_CAPTURE_MAX_FRAME_BYTES) {
            return camera_open_error(camera, "kamera_oeffnen: ungueltige Mapping-Laenge");
        }
        void* mapped = v4l2_mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, native->fd, buf.m.offset);
        if (mapped == MAP_FAILED) {
            return camera_open_error(camera, "kamera_oeffnen: mmap fehlgeschlagen");
        }
        native->buffers[i].start = mapped; native->buffers[i].length = buf.length;
        if (v4l2_ioctl(native->fd, VIDIOC_QBUF, &buf) < 0) {
            return camera_open_error(camera, "kamera_oeffnen: initiales QBUF fehlgeschlagen");
        }
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2_ioctl(native->fd, VIDIOC_STREAMON, &type) < 0) {
        return camera_open_error(camera, "kamera_oeffnen: STREAMON fehlgeschlagen");
    }
    native->streaming = true; native->pixel_format = format.fmt.pix.pixelformat;
    native->bytes_per_line = format.fmt.pix.bytesperline;
    native->size_image = format.fmt.pix.sizeimage;
    camera->width = format.fmt.pix.width; camera->height = format.fmt.pix.height;
    camera->fps = actual_fps; camera->state = MOO_CAPTURE_STREAMING;
    return true;
}

MooValue moo_capture_camera_frame_native(MooKamera* camera, int32_t timeout_ms) {
    CameraNative* native = camera->backend;
    int64_t now = monotonic_ms();
    if (!native || now < 0) return camera_fail("kamera_frame: monotone Uhr nicht verfuegbar");
    int64_t deadline = now + timeout_ms;
    struct pollfd pfd = { native->fd, POLLIN | POLLERR | POLLHUP, 0 };
    int poll_result;
    do {
        poll_result = poll(&pfd, 1, remaining_ms(deadline));
        if (poll_result < 0 && errno == EINTR && remaining_ms(deadline) == 0)
            return camera_fail("kamera_frame: Timeout");
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result == 0) return camera_fail("kamera_frame: Timeout");
    if (poll_result < 0 || (pfd.revents & (POLLERR | POLLHUP))) {
        return camera_broken(camera, "kamera_frame: Geraet getrennt");
    }

    struct v4l2_buffer held[MOO_CAPTURE_MAX_BUFFERS];
    bool seen[MOO_CAPTURE_MAX_BUFFERS] = {0};
    uint32_t held_count = 0;
    for (uint32_t n = 0; n < native->mapped_count &&
                         (n == 0 || remaining_ms(deadline) > 0); ++n) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP;
        if (v4l2_ioctl(native->fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) break;
            camera->state = MOO_CAPTURE_BROKEN; goto fail_requeue;
        }
        if (buf.index >= native->mapped_count || seen[buf.index]) {
            camera->state = MOO_CAPTURE_BROKEN; goto fail_requeue;
        }
        seen[buf.index] = true; held[held_count++] = buf;
    }
    if (!held_count) return camera_fail("kamera_frame: Timeout");

    struct v4l2_buffer* last = &held[held_count - 1];
    if (last->bytesused > native->buffers[last->index].length) {
        camera->state = MOO_CAPTURE_BROKEN; goto fail_requeue;
    }
    size_t output_bytes;
    if (!checked_frame(camera->width, camera->height, &output_bytes)) goto fail_requeue;
    uint8_t* rgba = malloc(output_bytes);
    if (!rgba) { camera_fail("kamera_frame: Speicher voll"); goto fail_requeue; }
    const uint8_t* src = native->buffers[last->index].start;
    for (int32_t y = 0; y < camera->height; ++y) {
        const uint8_t* row = src + (size_t)y * native->bytes_per_line;
        uint8_t* dst = rgba + (size_t)y * camera->width * 4u;
        for (int32_t x = 0; x < camera->width; ++x) {
            if (native->pixel_format == V4L2_PIX_FMT_BGR24) {
                dst[x*4] = row[x*3+2]; dst[x*4+1] = row[x*3+1];
                dst[x*4+2] = row[x*3]; dst[x*4+3] = 255;
            } else {
                dst[x*4] = row[x*3]; dst[x*4+1] = row[x*3+1];
                dst[x*4+2] = row[x*3+2]; dst[x*4+3] = 255;
            }
        }
    }
    bool requeue_ok = true;
    for (uint32_t i = 0; i < held_count; ++i)
        if (v4l2_ioctl(native->fd, VIDIOC_QBUF, &held[i]) < 0) requeue_ok = false;
    if (!requeue_ok) {
        free(rgba);
        return camera_broken(camera, "kamera_frame: QBUF fehlgeschlagen");
    }
    if (timeout_ms > 0 && remaining_ms(deadline) == 0) {
        free(rgba); return camera_fail("kamera_frame: Timeout");
    }
    return moo_frame_new_take(camera->width, camera->height, rgba);

fail_requeue:
    for (uint32_t i = 0; i < held_count; ++i)
        (void)v4l2_ioctl(native->fd, VIDIOC_QBUF, &held[i]);
    return camera_broken(camera, "kamera_frame: DQBUF/QBUF oder Treiberdaten ungueltig");
}

void moo_capture_camera_close_native(MooKamera* camera) {
    if (!camera || !camera->backend) return;
    CameraNative* native = camera->backend;
    if (native->streaming && native->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        (void)v4l2_ioctl(native->fd, VIDIOC_STREAMOFF, &type);
        native->streaming = false;
    }
    for (uint32_t i = 0; i < native->mapped_count; ++i) {
        if (native->buffers[i].start) {
            (void)v4l2_munmap(native->buffers[i].start, native->buffers[i].length);
            native->buffers[i].start = NULL;
        }
    }
    native->mapped_count = 0;
    if (native->fd >= 0) { v4l2_close(native->fd); native->fd = -1; }
    free(native); camera->backend = NULL;
}
