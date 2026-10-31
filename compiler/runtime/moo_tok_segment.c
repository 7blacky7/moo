#include "moo_tok_segment.h"
#include <string.h>
#include "moo_silben.h"

enum moo_seg_lang { MOO_TEXT, MOO_MOO, MOO_C, MOO_PY, MOO_SH, MOO_JSON, MOO_MD, MOO_DIFF };
static int moo_seg_alpha(uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int moo_seg_digit(uint8_t c) { return c >= '0' && c <= '9'; }
static int moo_seg_space(uint8_t c) { return c == ' ' || c == '\t' || c == '\v' || c == '\f'; }
/* Identifier and comment rules informed by synapse: packages/core/src/parser/moo.ts:23-55,
 * c.ts:49-73 and c.ts:106-110; preserve original bytes rather than extracted symbols. */
static size_t moo_seg_letter(const uint8_t *p, size_t n) {
    if (moo_seg_alpha(*p) || moo_seg_digit(*p)) return 1;
    if (n >= 2 && p[0] == 0xc3 && (p[1] == 0x84 || p[1] == 0x96 || p[1] == 0x9c ||
        p[1] == 0xa4 || p[1] == 0xb6 || p[1] == 0xbc || p[1] == 0x9f)) return 2;
    return 0;
}
static size_t moo_seg_utf8(const uint8_t *p, size_t n) {
    uint8_t c = p[0];
    if (c < 128) return c ? 1 : 0;
    if (c >= 0xc2 && c <= 0xdf && n >= 2 && (p[1] & 0xc0) == 0x80) return 2;
    if (c >= 0xe0 && c <= 0xef && n >= 3 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 &&
        !(c == 0xe0 && p[1] < 0xa0) && !(c == 0xed && p[1] >= 0xa0)) return 3;
    if (c >= 0xf0 && c <= 0xf4 && n >= 4 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 &&
        (p[3] & 0xc0) == 0x80 && !(c == 0xf0 && p[1] < 0x90) && !(c == 0xf4 && p[1] >= 0x90)) return 4;
    return 0;
}
static int moo_seg_eq(const uint8_t *p, size_t n, const char *s) { return strlen(s) == n && memcmp(p, s, n) == 0; }
static enum moo_seg_lang moo_seg_language(const uint8_t *p, size_t n) {
    if (moo_seg_eq(p,n,"moo") || moo_seg_eq(p,n,"moos")) return MOO_MOO;
    if (moo_seg_eq(p,n,"c") || moo_seg_eq(p,n,"h") || moo_seg_eq(p,n,"cpp")) return MOO_C;
    if (moo_seg_eq(p,n,"python") || moo_seg_eq(p,n,"py")) return MOO_PY;
    if (moo_seg_eq(p,n,"shell") || moo_seg_eq(p,n,"sh") || moo_seg_eq(p,n,"bash")) return MOO_SH;
    if (moo_seg_eq(p,n,"json")) return MOO_JSON;
    if (moo_seg_eq(p,n,"markdown") || moo_seg_eq(p,n,"md")) return MOO_MD;
    if (moo_seg_eq(p,n,"diff") || moo_seg_eq(p,n,"patch")) return MOO_DIFF;
    return MOO_TEXT;
}
static int moo_seg_op(uint8_t c) {
    return c == '=' || c == '!' || c == '<' || c == '>' || c == '+' ||
           c == '-' || c == '*' || c == '/' || c == '%' || c == '&' ||
           c == '|' || c == '^' || c == ':' || c == '.' || c == '?';
}
static void moo_seg_emit(size_t end, size_t *out, size_t cap, size_t *count) {
    if (out && *count < cap) out[*count] = end;
    ++*count;
}
size_t moo_tok_segmente_ctx(const uint8_t *buf, size_t len, size_t *grenzen, size_t max_grenzen, MooValue silben_artefakt) {
    size_t i = 0, count = 0, cache_start = 0, cache_end = 0;
    int32_t cache_idx = 0;
    MooValue cache = {MOO_NONE, 0};
    enum moo_seg_lang lang = MOO_TEXT;
    int fenced = 0, bol = 1, block_comment = 0;
    uint8_t triple_quote = 0;
    if (!buf) return 0;
    while (i < len) {
        uint8_t c = buf[i];
        if (bol && c == 96 && len - i >= 3 && buf[i+1] == 96 && buf[i+2] == 96) {
            size_t j = i + 3, name;
            moo_seg_emit(j, grenzen, max_grenzen, &count);
            name = j;
            while (j < len && !moo_seg_space(buf[j]) && buf[j] != '\r' && buf[j] != '\n') ++j;
            if (j > name) moo_seg_emit(j, grenzen, max_grenzen, &count);
            if (!fenced) { lang = moo_seg_language(buf + name, j - name); fenced = 1; }
            else { lang = MOO_TEXT; fenced = 0; }
            block_comment = 0; triple_quote = 0; i = j; bol = 0; continue;
        }
        if (c == '\r' || c == '\n') {
            ++i; if (c == '\r' && i < len && buf[i] == '\n') ++i;
            moo_seg_emit(i, grenzen, max_grenzen, &count); bol = 1; continue;
        }
        if (bol && lang == MOO_DIFF && (c == '+' || c == '-' || c == ' ' || c == '\\')) {
            moo_seg_emit(++i, grenzen, max_grenzen, &count); bol = 0; continue;
        }
        if (moo_seg_space(c)) {
            do { ++i; } while (i < len && moo_seg_space(buf[i]));
            moo_seg_emit(i, grenzen, max_grenzen, &count); bol = 0; continue;
        }
        bol = 0;
        if (triple_quote) {
            while (i < len && buf[i] != '\r' && buf[i] != '\n') {
                if (buf[i] == '\\' && len-i >= 2 && buf[i+1] != '\r' && buf[i+1] != '\n') { i += 2; continue; }
                if (buf[i] == triple_quote && len-i >= 3 && buf[i+1] == triple_quote && buf[i+2] == triple_quote) {
                    i += 3; triple_quote = 0; break;
                }
                ++i;
            }
        } else if (block_comment || (lang == MOO_C && c == '/' && len-i >= 2 && buf[i+1] == '*')) {
            if (!block_comment) { i += 2; block_comment = 1; }
            while (i < len && buf[i] != '\r' && buf[i] != '\n') {
                if (buf[i] == '*' && len-i >= 2 && buf[i+1] == '/') { i += 2; block_comment = 0; break; }
                ++i;
            }
        } else if (((lang == MOO_MOO || lang == MOO_PY || lang == MOO_SH) && c == '#') ||
                   (lang == MOO_C && c == '/' && len-i >= 2 && buf[i+1] == '/')) {
            while (i < len && buf[i] != '\r' && buf[i] != '\n') ++i;
        } else if (lang != MOO_TEXT && lang != MOO_MD && (c == '"' || c == '\'' || (lang == MOO_SH && c == 96))) {
            uint8_t quote = c;
            int triple = lang == MOO_PY && len-i >= 3 && buf[i+1] == c && buf[i+2] == c;
            i += triple ? 3 : 1;
            if (triple) triple_quote = quote;
            while (i < len && buf[i] != '\r' && buf[i] != '\n') {
                if (buf[i] == '\\' && len-i >= 2 && buf[i+1] != '\r' && buf[i+1] != '\n') { i += 2; continue; }
                if (buf[i] == quote) {
                    if (!triple) { ++i; break; }
                    if (len-i >= 3 && buf[i+1] == quote && buf[i+2] == quote) { i += 3; triple_quote = 0; break; }
                }
                ++i;
            }
        } else if (moo_seg_digit(c)) {
            ++i;
            while (i < len && (moo_seg_alpha(buf[i]) || moo_seg_digit(buf[i]) || buf[i] == '.')) ++i;
        } else if (moo_seg_letter(buf + i, len - i)) {
            size_t start = i, k;
            while (i < len && (k = moo_seg_letter(buf + i, len - i)) != 0) i += k;
            if (!fenced && silben_artefakt.tag != MOO_NONE) {
                if (start >= cache_end) {
                    if (cache.tag == MOO_LIST) moo_release(cache);
                    cache = (MooValue){MOO_NONE, 0}; cache_idx = 0;
                    cache_start = start; cache_end = start;
                    while (cache_end < len && cache_end - cache_start < 65536 &&
                           buf[cache_end] != 0) {
                        k = moo_seg_utf8(buf + cache_end, len - cache_end);
                        if (!k) break;
                        cache_end += k;
                    }
                    if (cache_end >= i) {
                        MooValue text = moo_string_new_len((const char *)buf + cache_start, (int32_t)(cache_end - cache_start));
                        cache = moo_silben_grenzen(silben_artefakt, text);
                        moo_release(text);
                    }
                }
                size_t last = start;
                if (cache.tag == MOO_LIST) {
                    MooList *spans = MV_LIST(cache);
                    while (cache_idx < spans->length) {
                        MooValue row = spans->items[cache_idx];
                        if (row.tag != MOO_LIST || MV_LIST(row)->length < 2 || MV_LIST(row)->items[1].tag != MOO_NUMBER) break;
                        size_t end = cache_start + (size_t)MV_NUM(MV_LIST(row)->items[1]);
                        if (end > i) break;
                        ++cache_idx;
                        if (end > last) { moo_seg_emit(end, grenzen, max_grenzen, &count); last = end; }
                    }
                }
                if (last < i) moo_seg_emit(i, grenzen, max_grenzen, &count);
                continue;
            }
        } else if (moo_seg_op(c) && lang != MOO_TEXT && lang != MOO_MD) {
            ++i;
            if (i < len && (buf[i] == '=' || buf[i] == c || (c == '-' && buf[i] == '>'))) ++i;
        } else ++i;
        moo_seg_emit(i, grenzen, max_grenzen, &count);
    }
    if (cache.tag == MOO_LIST) moo_release(cache);
    return count;
}

size_t moo_tok_segmente(const uint8_t *buf, size_t len, size_t *grenzen, size_t max_grenzen) {
    return moo_tok_segmente_ctx(buf, len, grenzen, max_grenzen, (MooValue){MOO_NONE, 0});
}
