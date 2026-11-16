#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>

/* P2-T117: erwarteter Exit-Code der Kindprozesse (Standard 0). Programme mit
   absichtlich fehlschlagendem teste_alle() enden mit 1: MOO_RSS_EXIT=1. */
static int erwarteter_exit(void) {
    const char *wert = getenv("MOO_RSS_EXIT");
    return wert ? atoi(wert) : 0;
}

static int measure(const char *path, long *rss) {
    pid_t child = fork();
    if (child < 0) { perror("fork"); return 1; }
    if (child == 0) {
        int sink = open("/dev/null", O_WRONLY);
        if (sink < 0 || dup2(sink, STDOUT_FILENO) < 0) _exit(126);
        close(sink);
        execl(path, path, (char *)0);
        _exit(127);
    }
    int status;
    struct rusage usage;
    pid_t result;
    do { result = wait4(child, &status, 0, &usage); } while (result < 0 && errno == EINTR);
    if (result < 0) { perror("wait4"); return 1; }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != erwarteter_exit()) {
        fprintf(stderr, "Kindprozess %s fehlgeschlagen: status=%d\n", path, status);
        return 1;
    }
    *rss = usage.ru_maxrss;
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "Erwartet zwei Programmdateien\n"); return 2; }
    long s1, s2, l1, l2;
    if (measure(argv[1], &s1) || measure(argv[1], &s2) ||
        measure(argv[2], &l1) || measure(argv[2], &l2)) return 1;
    long small = s1 < s2 ? s1 : s2;
    long large = l1 < l2 ? l1 : l2;
    long delta = large - small;
    printf("RSS KiB 200k=%ld/%ld 800k=%ld/%ld min-delta=%ld\n", s1,s2,l1,l2,delta);
    if (delta > 10240) { puts("FAIL RSS wächst"); return 1; }
    puts("PASS RSS innerhalb 10 MiB");
    return 0;
}
