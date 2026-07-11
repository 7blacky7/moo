#include "../moo_capture_pull_internal.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const MooCapturePullOps *ops = moo_capture_pull_system_ops();
    char err[256] = {0};
    int init_only = argc == 2 && strcmp(argv[1], "--init-only") == 0;

    if (!ops->startup(err, sizeof err)) {
        printf("C2WIN_SMOKE startup=FAIL %s\n", err);
        return 1;
    }
    printf("C2WIN_SMOKE startup=OK\n");

    if (init_only) {
        ops->shutdown();
        printf("C2WIN_SMOKE shutdown=OK mode=init-only\n");
        return 0;
    }

    MooPullCameraInfo cameras[4];
    int32_t total = 0;
    MooPullResult camera_result =
        ops->camera_enumerate(cameras, 4, &total, err, sizeof err);
    printf("C2WIN_SMOKE camera_enumerate=%d total=%d detail=%s\n",
           (int)camera_result, total, err);

    void *stream = NULL;
    int32_t rate = 0, channels = 0, period = 0, buffer = 0;
    err[0] = 0;
    MooPullResult audio_result =
        ops->microphone_open("default", 48000, 1, &stream, &rate, &channels,
                             &period, &buffer, err, sizeof err);
    printf("C2WIN_SMOKE wasapi_open=%d rate=%d channels=%d detail=%s\n",
           (int)audio_result, rate, channels, err);
    if (stream) {
        ops->microphone_close(stream);
    }
    ops->shutdown();

    return (camera_result >= MOO_PULL_OK && camera_result <= MOO_PULL_ERROR &&
            audio_result >= MOO_PULL_OK && audio_result <= MOO_PULL_ERROR)
               ? 0
               : 2;
}
