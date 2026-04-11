#include "moo_runtime.h"
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

extern MooValue moo_string_new_len(const char* chars, int32_t len);
extern MooValue moo_list_new(int32_t capacity);
extern void moo_list_append(MooValue list, MooValue value);
extern MooValue moo_list_get(MooValue list, MooValue index);
extern MooValue moo_list_length(MooValue list);

MooValue moo_file_read(MooValue path) {
    const char* p = MV_STR(path)->chars;
    FILE* f = fopen(p, "rb");
    if (!f) return moo_string_new_len("", 0);

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return moo_string_new_len("", 0); }

    char* buf = (char*)moo_alloc(len + 1);
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);

    // Binary-safe: explizite Laenge, NUL-bytes erhalten.
    MooValue result = moo_string_new_len(buf, (int32_t)got);
    moo_free(buf);
    return result;
}

// Liest Datei in eine Liste<Zahl> (binary-safe, fuer TAR/Git/PNG/...).
MooValue moo_file_read_bytes(MooValue path) {
    const char* p = MV_STR(path)->chars;
    FILE* f = fopen(p, "rb");
    if (!f) return moo_list_new(0);

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return moo_list_new(0); }

    unsigned char* buf = (unsigned char*)moo_alloc((size_t)len);
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);

    MooValue list = moo_list_new((int32_t)got);
    for (size_t i = 0; i < got; i++) {
        moo_list_append(list, moo_number((double)buf[i]));
    }
    moo_free(buf);
    return list;
}

MooValue moo_file_write(MooValue path, MooValue content) {
    const char* p = MV_STR(path)->chars;
    if (content.tag != MOO_STRING) return moo_bool(false);
    MooString* s = MV_STR(content);
    FILE* f = fopen(p, "wb");
    if (!f) return moo_bool(false);
    // Binary-safe: nutzt explizite length statt strlen
    fwrite(s->chars, 1, (size_t)s->length, f);
    fclose(f);
    return moo_bool(true);
}

// Schreibt eine Liste<Zahl> als rohe Bytes in eine Datei.
MooValue moo_file_write_bytes(MooValue path, MooValue list) {
    if (list.tag != MOO_LIST) return moo_bool(false);
    const char* p = MV_STR(path)->chars;
    int32_t len = (int32_t)MV_NUM(moo_list_length(list));
    FILE* f = fopen(p, "wb");
    if (!f) return moo_bool(false);
    if (len > 0) {
        unsigned char* buf = (unsigned char*)moo_alloc((size_t)len);
        for (int32_t i = 0; i < len; i++) {
            MooValue v = moo_list_get(list, moo_number((double)i));
            int b = (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : 0;
            buf[i] = (unsigned char)(b & 0xFF);
        }
        fwrite(buf, 1, (size_t)len, f);
        moo_free(buf);
    }
    fclose(f);
    return moo_bool(true);
}

MooValue moo_file_append(MooValue path, MooValue content) {
    const char* p = MV_STR(path)->chars;
    const char* c = MV_STR(content)->chars;
    FILE* f = fopen(p, "ab");
    if (!f) return moo_bool(false);
    fwrite(c, 1, strlen(c), f);
    fclose(f);
    return moo_bool(true);
}

MooValue moo_file_lines(MooValue path) {
    const char* p = MV_STR(path)->chars;
    FILE* f = fopen(p, "r");
    if (!f) return moo_list_new(0);

    MooValue list = moo_list_new(8);
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        // Strip trailing newline
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
            if (len > 1 && buf[len - 2] == '\r') {
                buf[len - 2] = '\0';
            }
        }
        moo_list_append(list, moo_string_new(buf));
    }
    fclose(f);
    return list;
}

MooValue moo_file_exists(MooValue path) {
    const char* p = MV_STR(path)->chars;
    struct stat st;
    return moo_bool(stat(p, &st) == 0);
}

MooValue moo_file_delete(MooValue path) {
    const char* p = MV_STR(path)->chars;
    return moo_bool(remove(p) == 0);
}

MooValue moo_dir_list(MooValue path) {
    const char* p = MV_STR(path)->chars;
    DIR* d = opendir(p);
    if (!d) return moo_list_new(0);

    MooValue list = moo_list_new(16);
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        moo_list_append(list, moo_string_new(entry->d_name));
    }
    closedir(d);
    return list;
}
