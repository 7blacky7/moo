/**
 * moo_eval.c — Fuehrt moo-Code als String aus und gibt Output zurueck.
 * Nutzt den moo-compiler als Subprocess.
 */

#include "moo_runtime.h"
#include <unistd.h>
#include <sys/wait.h>

extern MooValue moo_string_new(const char* s);
extern MooValue moo_none(void);
extern MooValue moo_file_read(MooValue path);

MooValue moo_eval(MooValue code) {
    if (code.tag != MOO_STRING) return moo_string_new("");

    const char* src = MV_STR(code)->chars;
    int pid_val = (int)getpid();

    // Eindeutige Temp-Dateien pro Prozess
    char src_path[128], out_path[128];
    snprintf(src_path, sizeof(src_path), "/tmp/moo_eval_%d.moo", pid_val);
    snprintf(out_path, sizeof(out_path), "/tmp/moo_eval_%d.txt", pid_val);

    // Code in Temp-Datei schreiben
    FILE* f = fopen(src_path, "w");
    if (!f) return moo_string_new("Fehler: Temp-Datei schreiben fehlgeschlagen");
    fputs(src, f);
    fclose(f);

    // moo-compiler ausfuehren mit Timeout
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "timeout 5 moo-compiler run %s > %s 2>&1",
        src_path, out_path);

    int ret = system(cmd);

    // Output lesen
    FILE* out = fopen(out_path, "r");
    if (!out) {
        unlink(src_path);
        return moo_string_new("Fehler: Output lesen fehlgeschlagen");
    }

    fseek(out, 0, SEEK_END);
    long size = ftell(out);
    fseek(out, 0, SEEK_SET);

    if (size <= 0) {
        fclose(out);
        unlink(src_path);
        unlink(out_path);
        return moo_string_new("");
    }

    // Max 64KB Output
    if (size > 65536) size = 65536;

    char* buf = (char*)malloc(size + 1);
    fread(buf, 1, size, out);
    buf[size] = '\0';
    fclose(out);

    // Trailing newline entfernen
    while (size > 0 && (buf[size-1] == '\n' || buf[size-1] == '\r')) {
        buf[--size] = '\0';
    }

    MooValue result = moo_string_new(buf);
    free(buf);

    // Aufraeumen
    unlink(src_path);
    unlink(out_path);

    return result;
}
