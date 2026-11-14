#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
/* Process wall clock. Child stdout/stderr pass through; nanoseconds go to argv[1]. */
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    struct timespec a, b;
    if (clock_gettime(CLOCK_MONOTONIC, &a)) return 3;
    pid_t child = fork();
    if (child < 0) return 3;
    if (child == 0) { execvp(argv[2], &argv[2]); _exit(127); }
    int status;
    while (waitpid(child, &status, 0) < 0) { if (errno != EINTR) return 3; }
    if (clock_gettime(CLOCK_MONOTONIC, &b)) return 3;
    int64_t ns=(int64_t)(b.tv_sec-a.tv_sec)*1000000000LL + (b.tv_nsec-a.tv_nsec);
    if (ns <= 0) return 3;
    FILE *f=fopen(argv[1], "w");
    if (!f) return 3;
    if (fprintf(f, "%lld\n", (long long)ns) < 0 || fclose(f) != 0) return 3;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 3;
}
