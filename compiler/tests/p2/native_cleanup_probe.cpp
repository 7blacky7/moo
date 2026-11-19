#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
extern "C" {
void hw_cleanup(std::uint8_t *) asm("__moo_native.p126_hw_cleanup");
void reset() asm("__moo_native.p126_reset");
std::uint64_t spur() asm("__moo_native.p126_spur");
std::uint64_t normal() asm("__moo_native.p126_normal");
std::uint64_t spaet() asm("__moo_native.p126_spaet");
void schleife() asm("__moo_native.p126_schleife");
void nie(bool) asm("__moo_native.p126_nie");
std::uint64_t fehler(std::uint64_t) asm("__moo_native.p126_fehler");
std::uint64_t cleanup_fehler(std::uint64_t) asm("__moo_native.p126_cleanup_fehler");
std::uint64_t erstfehler(std::uint64_t) asm("__moo_native.p126_erstfehler");
}
static int output_fd = -1;
static void trapped(int) {
    const std::uint64_t value = spur();
    const ssize_t written = write(output_fd, &value, sizeof(value));
    _exit(written == sizeof(value) ? 77 : 78);
}
static void check(bool ok, const char *name) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
}
static void error_case(std::uint64_t (*fn)(std::uint64_t), const char *name) {
    int fds[2];
    check(pipe(fds) == 0, "pipe");
    const pid_t pid = fork();
    check(pid >= 0, "fork");
    if (pid == 0) {
        close(fds[0]); output_fd = fds[1]; reset();
        struct sigaction action {};
        action.sa_handler = trapped;
        sigemptyset(&action.sa_mask);
        sigaction(SIGILL, &action, nullptr);
        sigaction(SIGTRAP, &action, nullptr);
        (void)fn(0);
        _exit(79);
    }
    close(fds[1]);
    std::uint64_t value = 0;
    const ssize_t bytes = read(fds[0], &value, sizeof(value));
    close(fds[0]);
    int status = 0;
    check(waitpid(pid, &status, 0) == pid, "waitpid");
    check(bytes == sizeof(value) && value == 21 && WIFEXITED(status)
        && WEXITSTATUS(status) == 77, name);
}
int main() {
    std::uint8_t byte = 0; hw_cleanup(&byte); check(byte == 1, "Hardware-Defer LIFO");
    reset(); check(normal() == 42 && spur() == 3241, "Scope/Return LIFO");
    reset(); check(spaet() == 8 && spur() == 8, "spaete Slotbindung");
    reset(); schleife(); check(spur() == 123, "continue/break");
    reset(); nie(false); check(spur() == 0, "nicht registriert");
    nie(true); check(spur() == 9, "bedingt registriert");
    error_case(fehler, "Bare-Callee: Caller-Cleanups vor Trap");
    error_case(cleanup_fehler, "Bare-Cleanupfehler: restliche Aktionen");
    error_case(erstfehler, "Bare-Fehler plus Cleanupfehler");
    std::puts("PASS native Cleanup: 9 C++-Orakel inklusive drei Trap-Pfade");
}
