#include "moo_runtime.h"
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
// Windows nutzt _stat64 fuer 64-Bit mtime; struct stat existiert nicht sauber.
typedef struct _stat64 moo_stat_t;
#define moo_stat_call(p, buf) _stat64((p), (buf))
#define MOO_S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#else
#include <dirent.h>
typedef struct stat moo_stat_t;
#define moo_stat_call(p, buf) stat((p), (buf))
#define MOO_S_ISDIR(m) S_ISDIR(m)
#endif

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
    if (path.tag != MOO_STRING) return moo_bool(false);
    const char* p = MV_STR(path)->chars;
    moo_stat_t st;
    int rc = moo_stat_call(p, &st);
    moo_release(path);  // Transfer-Semantik (siehe mtime)
    return moo_bool(rc == 0);
}

MooValue moo_file_delete(MooValue path) {
    if (path.tag != MOO_STRING) return moo_bool(false);
    const char* p = MV_STR(path)->chars;
    int rc = remove(p);
    moo_release(path);
    return moo_bool(rc == 0);
}

// === Metadaten ===

// Liefert Modification-Time als Unix-Timestamp (Sekunden seit 1970).
// -1 wenn Datei nicht existiert oder stat fehlschlaegt.
// Cross-platform: POSIX nutzt stat(), Windows nutzt _stat64().
MooValue moo_file_mtime(MooValue path) {
    if (path.tag != MOO_STRING) return moo_number(-1.0);
    const char* p = MV_STR(path)->chars;
    moo_stat_t st;
    int rc = moo_stat_call(p, &st);
    // Transfer-Semantik: Caller gibt path mit +1, diese Funktion gibt sie
    // frei — sonst leakt die im Hot-Loop pro Aufruf konstruierte
    // Pfad-Konkatenation (daemon-relevanter Hauptleak).
    moo_release(path);
    if (rc != 0) return moo_number(-1.0);
    return moo_number((double)st.st_mtime);
}

// Erstellt ein Verzeichnis inklusive aller fehlenden Parent-Dirs (rekursiv).
// Liefert wahr bei Erfolg, falsch wenn z.B. ein existierendes Nicht-Dir im Weg steht.
// Cross-platform: nutzt mkdir bzw. _mkdir.
MooValue moo_file_mkdir(MooValue path) {
    if (path.tag != MOO_STRING) return moo_bool(false);
    const char* p = MV_STR(path)->chars;
    size_t len = strlen(p);
    if (len == 0) return moo_bool(false);

    char* buf = (char*)moo_alloc(len + 1);
    memcpy(buf, p, len);
    buf[len] = '\0';

    // Segmentweise mkdir. Separator POSIX '/' — auf Windows akzeptiert _mkdir beide.
    for (size_t i = 1; i <= len; i++) {
        char c = buf[i];
        if (c == '/' || c == '\\' || c == '\0') {
            buf[i] = '\0';
            // Leere Komponente (z.B. "//") ueberspringen
            if (buf[i - 1] != '\0') {
#ifdef _WIN32
                _mkdir(buf);
#else
                mkdir(buf, 0755);
#endif
                // errno == EEXIST ist OK (schon da)
            }
            if (i < len) buf[i] = c;
        }
    }
    moo_free(buf);

    // Final-Check: ist der Ziel-Pfad jetzt ein Verzeichnis?
    moo_stat_t st;
    if (moo_stat_call(p, &st) != 0) return moo_bool(false);
    return moo_bool(MOO_S_ISDIR(st.st_mode));
}

// Prueft ob der Pfad ein Verzeichnis ist.
// Gibt falsch zurueck wenn Pfad nicht existiert oder keine Directory ist.
MooValue moo_file_is_dir(MooValue path) {
    if (path.tag != MOO_STRING) return moo_bool(false);
    const char* p = MV_STR(path)->chars;
    moo_stat_t st;
    int rc = moo_stat_call(p, &st);
    bool is_dir = (rc == 0) && MOO_S_ISDIR(st.st_mode);
    moo_release(path);  // Transfer-Semantik (siehe mtime)
    return moo_bool(is_dir);
}

// === Verzeichnis-Listing ===

#ifdef _WIN32
MooValue moo_dir_list(MooValue path) {
    const char* p = MV_STR(path)->chars;
    // Windows: FindFirstFile mit Wildcard "\*".
    size_t plen = strlen(p);
    char* pattern = (char*)moo_alloc(plen + 3);
    memcpy(pattern, p, plen);
    // Trailing separator falls noch nicht vorhanden
    if (plen > 0 && pattern[plen - 1] != '\\' && pattern[plen - 1] != '/') {
        pattern[plen++] = '\\';
    }
    pattern[plen++] = '*';
    pattern[plen] = '\0';

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    moo_free(pattern);
    if (h == INVALID_HANDLE_VALUE) return moo_list_new(0);

    MooValue list = moo_list_new(16);
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        moo_list_append(list, moo_string_new(fd.cFileName));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return list;
}
#else
MooValue moo_dir_list(MooValue path) {
    if (path.tag != MOO_STRING) return moo_list_new(0);
    const char* p = MV_STR(path)->chars;
    DIR* d = opendir(p);
    if (!d) { moo_release(path); return moo_list_new(0); }

    MooValue list = moo_list_new(16);
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        moo_list_append(list, moo_string_new(entry->d_name));
    }
    closedir(d);
    moo_release(path);  // Transfer-Semantik
    return list;
}
#endif
