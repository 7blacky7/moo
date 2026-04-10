/**
 * moo_regex.c — Native Regex via POSIX regex.h
 * Funktionen: moo_regex_new, moo_regex_match, moo_regex_find, moo_regex_replace
 */

#include "moo_runtime.h"
#include <regex.h>

// Kompilierte Regex als Heap-Objekt
typedef struct {
    regex_t compiled;
    char* pattern;
} MooRegex;

extern MooValue moo_string_new(const char* s);
extern MooValue moo_bool(bool b);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);
extern MooValue moo_error(const char* msg);
extern MooValue moo_list_new(int32_t cap);
extern void moo_list_append(MooValue list, MooValue item);

static MooRegex* get_regex(MooValue v) {
    if (v.tag != MOO_REGEX) return NULL;
    return (MooRegex*)moo_val_as_ptr(v);
}

// regex("pattern") → kompiliertes Regex-Objekt
MooValue moo_regex_new(MooValue pattern) {
    if (pattern.tag != MOO_STRING) {
        moo_throw(moo_error("regex() erwartet einen String"));
        return moo_none();
    }
    const char* pat = MV_STR(pattern)->chars;

    MooRegex* rx = (MooRegex*)malloc(sizeof(MooRegex));
    rx->pattern = strdup(pat);

    int ret = regcomp(&rx->compiled, pat, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[128];
        regerror(ret, &rx->compiled, errbuf, sizeof(errbuf));
        free(rx->pattern);
        free(rx);
        moo_throw(moo_error(errbuf));
        return moo_none();
    }

    MooValue result;
    result.tag = MOO_REGEX;
    moo_val_set_ptr(&result, rx);
    return result;
}

// passt(text, regex) → bool
MooValue moo_regex_match(MooValue text, MooValue regex) {
    if (text.tag != MOO_STRING) return moo_bool(false);
    MooRegex* rx = get_regex(regex);
    if (!rx) return moo_bool(false);

    int ret = regexec(&rx->compiled, MV_STR(text)->chars, 0, NULL, 0);
    return moo_bool(ret == 0);
}

// finde(text, regex) → erster Treffer als String oder nichts
MooValue moo_regex_find(MooValue text, MooValue regex) {
    if (text.tag != MOO_STRING) return moo_none();
    MooRegex* rx = get_regex(regex);
    if (!rx) return moo_none();

    regmatch_t match[1];
    const char* s = MV_STR(text)->chars;
    int ret = regexec(&rx->compiled, s, 1, match, 0);
    if (ret != 0) return moo_none();

    int len = match[0].rm_eo - match[0].rm_so;
    char* buf = (char*)malloc(len + 1);
    memcpy(buf, s + match[0].rm_so, len);
    buf[len] = '\0';
    MooValue result = moo_string_new(buf);
    free(buf);
    return result;
}

// finde_alle(text, regex) → Liste aller Treffer
MooValue moo_regex_find_all(MooValue text, MooValue regex) {
    if (text.tag != MOO_STRING) return moo_list_new(0);
    MooRegex* rx = get_regex(regex);
    if (!rx) return moo_list_new(0);

    MooValue list = moo_list_new(8);
    regmatch_t match[1];
    const char* s = MV_STR(text)->chars;
    int offset = 0;

    while (regexec(&rx->compiled, s + offset, 1, match, offset > 0 ? REG_NOTBOL : 0) == 0) {
        int len = match[0].rm_eo - match[0].rm_so;
        if (len == 0) { offset++; continue; } // Leere Matches vermeiden
        char* buf = (char*)malloc(len + 1);
        memcpy(buf, s + offset + match[0].rm_so, len);
        buf[len] = '\0';
        moo_list_append(list, moo_string_new(buf));
        free(buf);
        offset += match[0].rm_eo;
    }

    return list;
}

// ersetze(text, regex, ersetzung) → neuer String
MooValue moo_regex_replace(MooValue text, MooValue regex, MooValue replacement) {
    if (text.tag != MOO_STRING || replacement.tag != MOO_STRING) return text;
    MooRegex* rx = get_regex(regex);
    if (!rx) return text;

    const char* s = MV_STR(text)->chars;
    const char* rep = MV_STR(replacement)->chars;
    int rep_len = strlen(rep);

    // Einfache Implementierung: ersten Treffer ersetzen
    regmatch_t match[1];
    if (regexec(&rx->compiled, s, 1, match, 0) != 0) return text;

    int before_len = match[0].rm_so;
    int after_start = match[0].rm_eo;
    int after_len = strlen(s) - after_start;
    int total = before_len + rep_len + after_len;

    char* buf = (char*)malloc(total + 1);
    memcpy(buf, s, before_len);
    memcpy(buf + before_len, rep, rep_len);
    memcpy(buf + before_len + rep_len, s + after_start, after_len);
    buf[total] = '\0';

    MooValue result = moo_string_new(buf);
    free(buf);
    return result;
}
