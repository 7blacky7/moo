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
    moo_val_set_ptr(&v, s);
    return v;
}

MooValue moo_string_concat(MooValue a, MooValue b) {
    MooValue sa = moo_to_string(a);
    MooValue sb = moo_to_string(b);
    int32_t len = MV_STR(sa)->length + MV_STR(sb)->length;
    char* buf = moo_alloc(len + 1);
    memcpy(buf, MV_STR(sa)->chars, MV_STR(sa)->length);
    memcpy(buf + MV_STR(sa)->length, MV_STR(sb)->chars, MV_STR(sb)->length);
    buf[len] = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_string_length(MooValue s) {
    if (s.tag != MOO_STRING) return moo_number(0);
    return moo_number((double)MV_STR(s)->length);
}

MooValue moo_string_index(MooValue s, MooValue idx) {
    if (s.tag != MOO_STRING) return moo_none();
    int32_t i = (int32_t)moo_as_number(idx);
    if (i < 0) i += MV_STR(s)->length;
    if (i < 0 || i >= MV_STR(s)->length) return moo_none();
    char buf[2] = { MV_STR(s)->chars[i], '\0' };
    return moo_string_new(buf);
}

MooValue moo_string_compare(MooValue a, MooValue b) {
    if (a.tag != MOO_STRING || b.tag != MOO_STRING) return moo_bool(false);
    return moo_bool(strcmp(MV_STR(a)->chars, MV_STR(b)->chars) == 0);
}

MooValue moo_string_contains(MooValue haystack, MooValue needle) {
    if (haystack.tag != MOO_STRING || needle.tag != MOO_STRING) return moo_bool(false);
    return moo_bool(strstr(MV_STR(haystack)->chars, MV_STR(needle)->chars) != NULL);
}

MooValue moo_string_split(MooValue s, MooValue delim) {
    if (s.tag != MOO_STRING || delim.tag != MOO_STRING) return moo_list_new(0);
    MooValue result = moo_list_new(4);
    char* str = strdup(MV_STR(s)->chars);
    char* token = strtok(str, MV_STR(delim)->chars);
    while (token) {
        moo_list_append(result, moo_string_new(token));
        token = strtok(NULL, MV_STR(delim)->chars);
    }
    free(str);
    return result;
}

MooValue moo_string_replace(MooValue s, MooValue old_s, MooValue new_s) {
    if (s.tag != MOO_STRING || old_s.tag != MOO_STRING || new_s.tag != MOO_STRING)
        return s;
    char* src = MV_STR(s)->chars;
    char* find = MV_STR(old_s)->chars;
    char* repl = MV_STR(new_s)->chars;
    int find_len = MV_STR(old_s)->length;
    int repl_len = MV_STR(new_s)->length;
    int buf_size = MV_STR(s)->length * 2 + repl_len + 1;
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
    char* start = MV_STR(s)->chars;
    char* end = start + MV_STR(s)->length - 1;
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
    char* buf = strdup(MV_STR(s)->chars);
    for (int i = 0; buf[i]; i++)
        if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
    MooValue result = moo_string_new(buf);
    free(buf);
    return result;
}

MooValue moo_string_repeat(MooValue s, MooValue count) {
    if (s.tag != MOO_STRING) return s;
    int32_t n = (int32_t)moo_as_number(count);
    if (n <= 0) return moo_string_new("");
    int32_t len = MV_STR(s)->length;
    char* buf = moo_alloc(len * n + 1);
    for (int32_t i = 0; i < n; i++)
        memcpy(buf + i * len, MV_STR(s)->chars, len);
    buf[len * n] = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_string_slice(MooValue s, MooValue start, MooValue end) {
    if (s.tag != MOO_STRING) return moo_string_new("");
    int32_t len = MV_STR(s)->length;
    int32_t a = (int32_t)moo_as_number(start);
    int32_t b = (int32_t)moo_as_number(end);
    if (a < 0) a += len;
    if (b < 0) b += len;
    if (a < 0) a = 0;
    if (b > len) b = len;
    if (a >= b) return moo_string_new("");
    int32_t slice_len = b - a;
    char* buf = moo_alloc(slice_len + 1);
    memcpy(buf, MV_STR(s)->chars + a, slice_len);
    buf[slice_len] = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_string_lower(MooValue s) {
    if (s.tag != MOO_STRING) return s;
    char* buf = strdup(MV_STR(s)->chars);
    for (int i = 0; buf[i]; i++)
        if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] += 32;
    MooValue result = moo_string_new(buf);
    free(buf);
    return result;
}
