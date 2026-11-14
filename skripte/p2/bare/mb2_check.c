#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
int main(int argc, char **argv) {
    static const unsigned char expected[24] = {
        0xd6,0x50,0x52,0xe8, 0,0,0,0, 24,0,0,0, 0x12,0xaf,0xad,0x17,
        0,0,0,0, 8,0,0,0
    };
    unsigned char bytes[25];
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) { perror("header"); return 2; }
    size_t n = fread(bytes, 1, sizeof bytes, file);
    int failed = ferror(file);
    if (fclose(file) != 0) failed = 1;
    if (failed || n != sizeof expected || memcmp(bytes, expected, sizeof expected) ||
        (uint32_t)(le32(bytes) + le32(bytes+4) + le32(bytes+8) + le32(bytes+12)) != 0) {
        fputs("MB2 C-Referenz ungueltig\n", stderr);
        return 1;
    }
    puts("MB2 C-Referenz: 24 Bytes, Checksumme und Endtag OK");
    return 0;
}
