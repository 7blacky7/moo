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
    // Compiler-Pfad: MOO_COMPILER env MUSS gesetzt sein fuer Self-Hosting
    // Weil /proc/self/exe = das laufende Programm (z.B. playground), NICHT der Compiler!
    const char* compiler = getenv("MOO_COMPILER");
    if (!compiler) compiler = "moo-compiler";

    // Compile + Execute separat (nicht "run" — das wuerde den ganzen Server nochmal starten)
    char bin_path[128];
    snprintf(bin_path, sizeof(bin_path), "/tmp/moo_eval_bin_%d", pid_val);

    snprintf(cmd, sizeof(cmd),
        "MOO_QUIET=1 timeout 5 %s compile %s -o %s 2>/dev/null && timeout 5 %s > %s 2>&1",
        compiler, src_path, bin_path, bin_path, out_path);

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
    char bin_cleanup[128];
    snprintf(bin_cleanup, sizeof(bin_cleanup), "/tmp/moo_eval_bin_%d", pid_val);
    unlink(src_path);
    unlink(out_path);
    unlink(bin_cleanup);

    return result;
}
