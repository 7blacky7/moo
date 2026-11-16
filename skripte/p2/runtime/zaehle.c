/* P2-T112: Fuehrt ein Programm aus und zaehlt Instruktionen und Takte im
 * Benutzermodus (perf_event_open, vererbt an Threads). Instruktionen sind
 * lastunabhaengig und damit ein robustes Regressionsmass, wenn der Rechner
 * durch andere Jobs verrauscht ist. Ausgabe auf stderr:
 *   ZAEHLER instr=<n> takte=<n>
 * Bauen: cc -O2 zaehle.c -o zaehle ; Aufruf: zaehle PROGRAMM [ARG...] */
#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int oeffne(uint64_t config) {
    struct perf_event_attr a;
    memset(&a, 0, sizeof(a));
    a.type = PERF_TYPE_HARDWARE;
    a.size = sizeof(a);
    a.config = config;
    a.disabled = 1;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    a.inherit = 1;
    a.enable_on_exec = 1;
    return (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    int fi = oeffne(PERF_COUNT_HW_INSTRUCTIONS);
    int fc = oeffne(PERF_COUNT_HW_CPU_CYCLES);
    if (fi < 0 || fc < 0) { perror("perf_event_open"); return 2; }
    pid_t p = fork();
    if (p == 0) { execv(argv[1], argv + 1); _exit(127); }
    int st = 0;
    waitpid(p, &st, 0);
    uint64_t ni = 0, nc = 0;
    if (read(fi, &ni, sizeof(ni)) != sizeof(ni) || read(fc, &nc, sizeof(nc)) != sizeof(nc)) return 2;
    fprintf(stderr, "ZAEHLER instr=%llu takte=%llu\n", (unsigned long long)ni, (unsigned long long)nc);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
