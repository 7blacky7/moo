#include "moo_tok_segment.h"
#include "moo_silben.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void moo_frame_free(void *p) { (void)p; }
void moo_gif_handle_free(void *p) { (void)p; }
void moo_video_handle_free(void *p) { (void)p; }

static void pruefe(const unsigned char *text, size_t len, int statistik, MooValue tok) {
    size_t n = moo_tok_segmente_ctx(text, len, NULL, 0, tok), pos = 0;
    size_t *ends = malloc((n ? n : 1) * sizeof(*ends));
    size_t *again = malloc((n ? n : 1) * sizeof(*again));
    unsigned char *copy = malloc(len ? len : 1);
    unsigned char *area = calloc(len ? len : 1, 1);
    size_t bytes[3] = {0}, pieces[3] = {0};
    assert(ends && again && copy && area);
    assert(moo_tok_segmente_ctx(text, len, ends, n, tok) == n);
    assert(moo_tok_segmente_ctx(text, len, again, n, tok) == n);
    if (n) assert(memcmp(ends, again, n * sizeof(*ends)) == 0);
    if (n) {
        size_t guard = (size_t)-1;
        assert(moo_tok_segmente_ctx(text, len, &guard, 0, tok) == n);
        assert(guard == (size_t)-1);
    }
    int zone = 0, fenced = 0;
    for (size_t i = 0; i < len;) {
        size_t start = i;
        while (i < len && text[i] != '\r' && text[i] != '\n') ++i;
        size_t end = i;
        if (i < len && text[i++] == '\r' && i < len && text[i] == '\n') ++i;
        if (end - start >= 3 && !memcmp(text + start, "\x60\x60\x60", 3)) {
            if (!fenced) {
                zone = end - start >= 7 && !memcmp(text + start + 3, "diff", 4) ? 2 : 1;
                fenced = 1;
            } else { zone = 0; fenced = 0; }
        }
        for (size_t j = start; j < i; ++j) { area[j] = (unsigned char)zone; ++bytes[zone]; }
    }
    for (size_t j = 0; j < n; ++j) {
        size_t end = ends[j];
        assert(end > pos && end <= len);
        memcpy(copy + pos, text + pos, end - pos);
        ++pieces[area[pos]];
        pos = end;
    }
    assert(pos == len);
    assert(memcmp(copy, text, len) == 0);
    if (statistik) {
        const char *names[] = {"text", "code", "diff"};
        for (size_t k = 0; k < 3; ++k)
            printf("%s bytes=%zu stuecke=%zu stuecke_pro_byte=%.8f\n", names[k], bytes[k], pieces[k],
                   bytes[k] ? (double)pieces[k] / (double)bytes[k] : 0.0);
    }
    free(area); free(copy); free(again); free(ends);
}
static void erwartete_grenzen(void) {
    static const unsigned char text[] = "\x60\x60\x60" "c\n  int x=42; //hi\n\x60\x60\x60\n";
    static const size_t want[] = {3,4,5,7,10,11,12,13,15,16,17,21,22,25,26};
    size_t ends[sizeof(want) / sizeof(*want)];
    size_t n = moo_tok_segmente(text, sizeof(text)-1, ends, sizeof(ends)/sizeof(*ends));
    assert(n == sizeof(want)/sizeof(*want));
    assert(memcmp(ends, want, sizeof(want)) == 0);
    size_t short_out[2] = {0, (size_t)-1};
    assert(moo_tok_segmente(text, sizeof(text)-1, short_out, 1) == n);
    assert(short_out[0] == 3 && short_out[1] == (size_t)-1);
    static const unsigned char diff[] = "\x60\x60\x60" "diff\n+foo\n-bar\n baz\n\x60\x60\x60\n";
    static const size_t diff_want[] = {3,7,8,9,12,13,14,17,18,19,22,23,26,27};
    size_t diff_ends[sizeof(diff_want)/sizeof(*diff_want)];
    assert(moo_tok_segmente(diff, sizeof(diff)-1, diff_ends, sizeof(diff_ends)/sizeof(*diff_ends)) == sizeof(diff_want)/sizeof(*diff_want));
    assert(memcmp(diff_ends, diff_want, sizeof(diff_want)) == 0);
    static const unsigned char python[] = "\x60\x60\x60" "python\ns=\"\"\"a\n# b\"\"\"\n\x60\x60\x60\n";
    static const size_t py_want[] = {3,9,10,11,12,16,17,23,24,27,28};
    size_t py_ends[sizeof(py_want)/sizeof(*py_want)];
    assert(moo_tok_segmente(python, sizeof(python)-1, py_ends, sizeof(py_ends)/sizeof(*py_ends)) == sizeof(py_want)/sizeof(*py_want));
    assert(memcmp(py_ends, py_want, sizeof(py_want)) == 0);
    static const unsigned char unknown[] = {0xff, 0x80, 0};
    size_t unknown_ends[3];
    assert(moo_tok_segmente(unknown, sizeof(unknown), unknown_ends, 3) == 3);
    assert(unknown_ends[0] == 1 && unknown_ends[1] == 2 && unknown_ends[2] == 3);
}
static void silben_grenzen(void) {
    MooValue sample = moo_string_new("banane");
    MooValue patterns = moo_string_new("\\patterns{ba1na}");
    MooValue tok = moo_silben_trainiere(sample, moo_number(256), patterns);
    static const unsigned char text[] = "banane\n\x60\x60\x60" "c\nbanane\n\x60\x60\x60\n";
    static const size_t want[] = {2,6,7,10,11,12,18,19,22,23};
    size_t out[sizeof(want)/sizeof(*want)];
    assert(moo_tok_segmente_ctx(text, sizeof(text)-1, out, sizeof(out)/sizeof(*out), tok) == sizeof(want)/sizeof(*want));
    assert(memcmp(out, want, sizeof(want)) == 0);
    moo_release(tok); moo_release(patterns); moo_release(sample);
}
int main(int argc, char **argv) {
    static const unsigned char probe[] =
        "Deutsch: Grüße!\r\n\x60\x60\x60" "c\n  int x = 5; // c\n/* a\nb */\n"
        "\x60\x60\x60\n\x60\x60\x60" "python\n  s = \"#x\\\"y\" # c\n"
        "\x60\x60\x60\n\x60\x60\x60" "diff\n+eins\n-zwei\n drei\n\x60\x60\x60\n"
        "\x00\xff";
    assert(argc == 2);
    erwartete_grenzen();
    silben_grenzen();
    pruefe(probe, sizeof(probe) - 1, 0, moo_none());
    pruefe(probe, 0, 0, moo_none());
    for (size_t n = 1; n < sizeof(probe); ++n) pruefe(probe, n, 0, moo_none());
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    assert(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    assert(size >= 0 && fseek(f, 0, SEEK_SET) == 0);
    unsigned char *buf = malloc(size ? (size_t)size : 1);
    assert(buf && fread(buf, 1, (size_t)size, f) == (size_t)size);
    assert(fclose(f) == 0);
    puts("WORT-FALLBACK");
    pruefe(buf, (size_t)size, 1, moo_none());
    FILE *pf = fopen("compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat", "rb");
    assert(pf && fseek(pf, 0, SEEK_END) == 0);
    long plen = ftell(pf);
    assert(plen > 0 && plen <= INT32_MAX && fseek(pf, 0, SEEK_SET) == 0);
    char *pbuf = malloc((size_t)plen);
    assert(pbuf && fread(pbuf, 1, (size_t)plen, pf) == (size_t)plen);
    assert(fclose(pf) == 0);
    MooValue sample = moo_string_new("banane");
    MooValue patterns = moo_string_new_len(pbuf, (int32_t)plen);
    free(pbuf);
    MooValue tok = moo_silben_trainiere(sample, moo_number(256), patterns);
    puts("SILBEN-KONTEXT (deutsche Projektmuster)");
    pruefe(buf, (size_t)size, 1, tok);
    moo_release(tok); moo_release(patterns); moo_release(sample);
    free(buf);
    puts("SEGMENT ROUNDTRIP OK");
    return 0;
}
