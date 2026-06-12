/**
 * moo_eval.c — Fuehrt moo-Code als String aus und gibt Output zurueck.
 * Nutzt den moo-compiler als Subprocess.
 *
 * P013: Plattform-Strategie
 *   POSIX   → system() mit `timeout 5`-Utility + ENV-Prefix (unveraendert).
 *   Windows → CreateProcess mit WaitForSingleObject(5s) + TerminateProcess
 *             und Handle-Redirection (stdout/stderr → Datei bzw. NUL).
 *             Kein cmd.exe noetig — vermeidet Quoting-Probleme und liefert
 *             ein ECHTES Timeout (Windows' timeout.exe kann keine Prozesse
 *             killen). compile→run-Verkettung wie POSIX (&&) via rc==0.
 */

#include "moo_runtime.h"
#include <unistd.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

extern MooValue moo_string_new(const char* s);
extern MooValue moo_none(void);
extern MooValue moo_file_read(MooValue path);

// Baut einen Temp-Pfad: POSIX fest /tmp, Windows %TEMP% (Fallback %TMP%, ".").
static void eval_tmp_path(char* out, size_t cap, const char* prefix, int pid, const char* suffix) {
#ifdef _WIN32
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = ".";
    snprintf(out, cap, "%s\\%s_%d%s", tmp, prefix, pid, suffix);
#else
    snprintf(out, cap, "/tmp/%s_%d%s", prefix, pid, suffix);
#endif
}

#ifdef _WIN32
// Fuehrt cmdline (mutable, CreateProcess-Anforderung) mit hartem Timeout aus.
// stdout+stderr gehen nach out_file ("NUL" = verwerfen). Rueckgabe: Exit-Code,
// -1 bei Startfehler, 1 nach Timeout-Kill.
static int eval_run_with_timeout(char* cmdline, const char* out_file, DWORD timeout_ms) {
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE h_out = CreateFileA(out_file, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h_out == INVALID_HANDLE_VALUE) return -1;

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h_out;
    si.hStdError  = h_out;
    si.hStdInput  = NULL;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(h_out);
    if (!ok) return -1;

    DWORD wr = WaitForSingleObject(pi.hProcess, timeout_ms);
    if (wr == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}
#endif

MooValue moo_eval(MooValue code) {
    if (code.tag != MOO_STRING) return moo_string_new("");

    const char* src = MV_STR(code)->chars;
    int pid_val = (int)getpid();

    // Eindeutige Temp-Dateien pro Prozess
    char src_path[256], out_path[256], bin_path[256];
    eval_tmp_path(src_path, sizeof(src_path), "moo_eval", pid_val, ".moo");
    eval_tmp_path(out_path, sizeof(out_path), "moo_eval", pid_val, ".txt");
    eval_tmp_path(bin_path, sizeof(bin_path), "moo_eval_bin", pid_val, "");

    // Code in Temp-Datei schreiben
    FILE* f = fopen(src_path, "w");
    if (!f) return moo_string_new("Fehler: Temp-Datei schreiben fehlgeschlagen");
    fputs(src, f);
    fclose(f);

    // Compiler-Pfad: MOO_COMPILER env MUSS gesetzt sein fuer Self-Hosting
    // Weil /proc/self/exe = das laufende Programm (z.B. playground), NICHT der Compiler!
    const char* compiler = getenv("MOO_COMPILER");
    if (!compiler) compiler = "moo-compiler";

#ifdef _WIN32
    // MOO_QUIET fuer die Kindprozesse setzen (CreateProcess erbt das ENV);
    // danach zuruecknehmen, um den eigenen Prozess nicht zu veraendern.
    _putenv("MOO_QUIET=1");
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "\"%s\" compile \"%s\" -o \"%s\"",
             compiler, src_path, bin_path);
    int rc = eval_run_with_timeout(cmd, "NUL", 5000);
    if (rc == 0) {
        snprintf(cmd, sizeof(cmd), "\"%s\"", bin_path);
        (void)eval_run_with_timeout(cmd, out_path, 5000);
    }
    _putenv("MOO_QUIET=");
#else
    // Compile + Execute separat (nicht "run" — das wuerde den ganzen Server nochmal starten)
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "MOO_QUIET=1 timeout 5 %s compile %s -o %s 2>/dev/null && timeout 5 %s > %s 2>&1",
        compiler, src_path, bin_path, bin_path, out_path);
    int ret = system(cmd);
    (void)ret;
#endif

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
    unlink(bin_path);

    return result;
}
