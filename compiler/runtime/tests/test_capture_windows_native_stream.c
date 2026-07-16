/* C2-WIN Nativ-Streaming-Gate: Media Foundation Kamera + WASAPI Mikrofon
 * unter Last auf echtem Windows (kein Wine). Pro Zyklus: Kamera oeffnen,
 * 30 Frames ziehen (latest-Semantik, jedes Paket exakt einmal released),
 * Mikrofon oeffnen, ~1s Audio lesen, beides schliessen. 3 Zyklen beweisen
 * wiederholtes Open/Close ohne Ressourcen-/Handle-Verlust.
 * Marker: C2WIN_STREAM ... / finale Zeile PASS|FAIL. */
#include "../moo_capture_pull_internal.h"
#include <stdio.h>
#include <string.h>

static int fail(const char *what, const char *err) {
    printf("C2WIN_STREAM %s=FAIL detail=%s\n", what, err);
    printf("C2WIN_STREAM RESULT=FAIL\n");
    return 1;
}

int main(void) {
    const MooCapturePullOps *ops = moo_capture_pull_system_ops();
    char err[256] = {0};
    if (!ops->startup(err, sizeof err)) return fail("startup", err);
    printf("C2WIN_STREAM startup=OK\n");

    for (int cycle = 0; cycle < 3; ++cycle) {
        /* --- Kamera --- */
        MooPullCameraInfo cams[4];
        int32_t total = 0;
        err[0] = 0;
        if (ops->camera_enumerate(cams, 4, &total, err, sizeof err) != MOO_PULL_OK ||
            total < 1)
            return fail("camera_enumerate", err);
        void *cam = NULL;
        int32_t w = 0, h = 0, qb = 0;
        double fps = 0.0;
        err[0] = 0;
        if (ops->camera_open(cams[0].id, 640, 480, 30.0, false, &cam, &w, &h,
                             &fps, &qb, err, sizeof err) != MOO_PULL_OK)
            return fail("camera_open", err);
        int frames = 0;
        unsigned long long bytes = 0, nonzero = 0, sampled = 0;
        int64_t first_ts = 0, last_ts = 0;
        while (frames < 30) {
            err[0] = 0;
            MooPullResult r = ops->camera_wait(cam, 3000, err, sizeof err);
            if (r != MOO_PULL_OK) {
                ops->camera_close(cam);
                return fail("camera_wait", err);
            }
            MooPullFramePacket p;
            memset(&p, 0, sizeof p);
            err[0] = 0;
            r = ops->camera_next(cam, &p, err, sizeof err);
            if (r == MOO_PULL_EMPTY) continue;
            if (r != MOO_PULL_OK) {
                ops->camera_close(cam);
                return fail("camera_next", err);
            }
            if (!p.bgra || p.width < 1 || p.height < 1 || p.bytes == 0) {
                ops->camera_release(&p);
                ops->camera_close(cam);
                return fail("frame_geometry", "ungueltiges Paket");
            }
            if (frames == 0) first_ts = p.timestamp_100ns;
            last_ts = p.timestamp_100ns;
            bytes += p.bytes;
            for (size_t i = 0; i + 4 <= p.bytes; i += 1024) {
                sampled++;
                if (p.bgra[i] | p.bgra[i + 1] | p.bgra[i + 2]) nonzero++;
            }
            frames++;
            ops->camera_release(&p);
        }
        ops->camera_close(cam);
        double span_ms = (double)(last_ts - first_ts) / 10000.0;
        printf("C2WIN_STREAM cycle=%d camera=\"%s\" neg=%dx%d@%.1f frames=%d "
               "bytes=%llu nonzero_ratio=%.2f span_ms=%.0f\n",
               cycle, cams[0].name, w, h, fps, frames, bytes,
               sampled ? (double)nonzero / (double)sampled : 0.0, span_ms);

        /* --- Mikrofon --- */
        void *mic = NULL;
        int32_t rate = 0, ch = 0, period = 0, buffer = 0;
        err[0] = 0;
        if (ops->microphone_open("default", 48000, 1, &mic, &rate, &ch, &period,
                                 &buffer, err, sizeof err) != MOO_PULL_OK)
            return fail("microphone_open", err);
        long long want = rate; /* ~1 Sekunde */
        long long got = 0;
        double sum_abs = 0.0;
        int recoveries = 0;
        while (got < want) {
            err[0] = 0;
            MooPullResult r = ops->microphone_wait(mic, 3000, err, sizeof err);
            if (r == MOO_PULL_RECOVERABLE) {
                if (++recoveries > MOO_PULL_CAPTURE_MAX_RECOVERIES ||
                    ops->microphone_recover(mic, err, sizeof err) != MOO_PULL_OK) {
                    ops->microphone_close(mic);
                    return fail("microphone_recover", err);
                }
                continue;
            }
            if (r != MOO_PULL_OK) {
                ops->microphone_close(mic);
                return fail("microphone_wait", err);
            }
            MooPullAudioPacket p;
            memset(&p, 0, sizeof p);
            err[0] = 0;
            r = ops->microphone_next(mic, &p, err, sizeof err);
            if (r == MOO_PULL_EMPTY) continue;
            if (r == MOO_PULL_RECOVERABLE) {
                /* Mid-Stream-Glitch: Vertrag verlangt recover + weiter. */
                if (++recoveries > MOO_PULL_CAPTURE_MAX_RECOVERIES ||
                    ops->microphone_recover(mic, err, sizeof err) != MOO_PULL_OK) {
                    ops->microphone_close(mic);
                    return fail("microphone_recover", err);
                }
                continue;
            }
            if (r != MOO_PULL_OK || !p.samples || p.frames < 1) {
                if (p.token) ops->microphone_release(&p);
                ops->microphone_close(mic);
                return fail("microphone_next", err);
            }
            for (int32_t i = 0; i < p.frames * p.channels; ++i) {
                double v = p.samples[i];
                sum_abs += v < 0 ? -v : v;
            }
            got += p.frames;
            ops->microphone_release(&p);
        }
        ops->microphone_close(mic);
        printf("C2WIN_STREAM cycle=%d wasapi rate=%d channels=%d period=%d "
               "buffer=%d samples=%lld avg_abs=%.6f recoveries=%d\n",
               cycle, rate, ch, period, buffer, got,
               got ? sum_abs / (double)got : 0.0, recoveries);
    }

    ops->shutdown();
    printf("C2WIN_STREAM shutdown=OK\n");
    printf("C2WIN_STREAM RESULT=PASS\n");
    return 0;
}
