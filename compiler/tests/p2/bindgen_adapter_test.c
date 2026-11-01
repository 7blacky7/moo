#include <stdio.h>
#include <string.h>
/* Synthetic collector contract fixture, not a native or GPU performance result. */
int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "multi") == 0) {
        puts("{\"schema\":\"p2-g5-adapter-v1\",\"kind\":\"native_transition\","
             "\"adapter\":\"synthetic-fixture\",\"work_items\":3,\"checksum\":6,"
             "\"native_core_s\":0.25,\"transition_s\":0.5}");
        puts("{}");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "gpu") == 0) {
        puts("{\"schema\":\"p2-g5-adapter-v1\",\"kind\":\"gpu_transfer\","
             "\"backend\":\"synthetic\",\"device\":\"fixture-only\",\"bytes\":16,"
             "\"synchronized\":true,\"work_items\":3,\"checksum\":6,"
             "\"upload_s\":0.125,\"download_s\":0.75}");
    } else {
        puts("{\"schema\":\"p2-g5-adapter-v1\",\"kind\":\"native_transition\","
             "\"adapter\":\"synthetic-fixture\",\"work_items\":3,\"checksum\":6,"
             "\"native_core_s\":0.25,\"transition_s\":0.5}");
    }
    return 0;
}
