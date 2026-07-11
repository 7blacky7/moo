#include "../moo_capture_pull_internal.h"
#include <stdio.h>

int main(void) {
    const MooCapturePullOps *ops = moo_capture_pull_system_ops();
    char error[256] = {0};
    if (!ops->startup(error, sizeof error)) {
        fprintf(stderr, "C2MAC_SMOKE startup=FAIL %s\n", error);
        return 1;
    }
    MooPullCameraInfo cameras[4];
    int32_t total = 0;
    MooPullResult result =
        ops->camera_enumerate(cameras, 4, &total, error, sizeof error);
    printf("C2MAC_SMOKE startup=OK enumerate=%d total=%d detail=%s\n",
           (int)result, total, error);
    ops->shutdown();
    return result == MOO_PULL_OK && total >= 0 ? 0 : 2;
}
