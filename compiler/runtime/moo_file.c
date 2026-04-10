#include "moo_runtime.h"
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

MooValue moo_file_read(MooValue path) {
    const char* p = MV_STR(path)->chars;
    FILE* f = fopen(p, "rb");
    if (!f) return moo_string_new("");

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)moo_alloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_file_write(MooValue path, MooValue content) {
    const char* p = MV_STR(path)->chars;
    const char* c = MV_STR(content)->chars;
    FILE* f = fopen(p, "wb");
    if (!f) return moo_bool(false);
    fwrite(c, 1, strlen(c), f);
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
