#include "moo_runtime.h"

MooValue moo_string_new(const char* chars) {
    MooValue v;
    v.tag = MOO_STRING;
    MooString* s = moo_alloc(sizeof(MooString));
    int32_t len = strlen(chars);
    s->length = len;
    s->capacity = len + 1;
    s->chars = moo_alloc(s->capacity);
    memcpy(s->chars, chars, len + 1);
    v.data.string = s;
    return v;
}

MooValue moo_string_concat(MooValue a, MooValue b) {
    MooValue sa = moo_to_string(a);
    MooValue sb = moo_to_string(b);
    int32_t len = sa.data.string->length + sb.data.string->length;
    char* buf = moo_alloc(len + 1);
    memcpy(buf, sa.data.string->chars, sa.data.string->length);
    memcpy(buf + sa.data.string->length, sb.data.string->chars, sb.data.string->length);
    buf[len] = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_string_length(MooValue s) {
    if (s.tag != MOO_STRING) return moo_number(0);
    return moo_number((double)s.data.string->length);
}

MooValue moo_string_index(MooValue s, MooValue idx) {
    if (s.tag != MOO_STRING) return moo_none();
    int32_t i = (int32_t)moo_as_number(idx);
    if (i < 0) i += s.data.string->length;
    if (i < 0 || i >= s.data.string->length) return moo_none();
    char buf[2] = { s.data.string->chars[i], '\0' };
    return moo_string_new(buf);
}

MooValue moo_string_compare(MooValue a, MooValue b) {
    if (a.tag != MOO_STRING || b.tag != MOO_STRING) return moo_bool(false);
    return moo_bool(strcmp(a.data.string->chars, b.data.string->chars) == 0);
}

MooValue moo_string_contains(MooValue haystack, MooValue needle) {
    if (haystack.tag != MOO_STRING || needle.tag != MOO_STRING) return moo_bool(false);
    return moo_bool(strstr(haystack.data.string->chars, needle.data.string->chars) != NULL);
}

MooValue moo_string_split(MooValue s, MooValue delim) {
    if (s.tag != MOO_STRING || delim.tag != MOO_STRING) return moo_list_new(0);
    MooValue result = moo_list_new(4);
    char* str = strdup(s.data.string->chars);
    char* token = strtok(str, delim.data.string->chars);
    while (token) {
        moo_list_append(result, moo_string_new(token));
        token = strtok(NULL, delim.data.string->chars);
    }
    free(str);
    return result;
}

MooValue moo_string_replace(MooValue s, MooValue old_s, MooValue new_s) {
    if (s.tag != MOO_STRING || old_s.tag != MOO_STRING || new_s.tag != MOO_STRING)
        return s;
    char* src = s.data.string->chars;
    char* find = old_s.data.string->chars;
    char* repl = new_s.data.string->chars;
    int find_len = old_s.data.string->length;
    int repl_len = new_s.data.string->length;
    int buf_size = s.data.string->length * 2 + repl_len + 1;
    char* buf = moo_alloc(buf_size);
    char* dst = buf;
    while (*src) {
        if (strncmp(src, find, find_len) == 0) {
            memcpy(dst, repl, repl_len);
            dst += repl_len;
            src += find_len;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_string_trim(MooValue s) {
    if (s.tag != MOO_STRING) return s;
    char* start = s.data.string->chars;
    char* end = start + s.data.string->length - 1;
    while (start <= end && (*start == ' ' || *start == '\t' || *start == '\n')) start++;
    while (end >= start && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
    int len = end - start + 1;
    if (len < 0) len = 0;
    char* buf = moo_alloc(len + 1);
    memcpy(buf, start, len);
    buf[len] = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_string_upper(MooValue s) {
    if (s.tag != MOO_STRING) return s;
    char* buf = strdup(s.data.string->chars);
    for (int i = 0; buf[i]; i++) {
        if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
    }
    MooValue result = moo_string_new(buf);
    free(buf);
    return result;
}

MooValue moo_string_lower(MooValue s) {
    if (s.tag != MOO_STRING) return s;
    char* buf = strdup(s.data.string->chars);
    for (int i = 0; buf[i]; i++) {
        if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] += 32;
    }
    MooValue result = moo_string_new(buf);
    free(buf);
    return result;
}
