/**
 * moo_net_compat.h — Plattform-Kompatibilitaet fuer Socket-Code (P013).
 *
 * Gemeinsame Abstraktion fuer moo_net.c und moo_web.c:
 *   POSIX  → BSD-Sockets (int fd, close, timeval-Timeouts)
 *   Windows → Winsock2 (SOCKET, closesocket, DWORD-ms-Timeouts, WSAStartup)
 *
 * Regeln fuer Nutzer dieses Headers:
 *   - Socket-Deskriptoren IMMER als moo_sockfd_t, nie int.
 *   - Gueltigkeit IMMER via MOO_SOCK_BAD(fd) pruefen, nie `fd < 0`
 *     (SOCKET ist unter Windows unsigned).
 *   - Schliessen IMMER via moo_closesock(fd).
 *   - setsockopt-Wertzeiger IMMER durch MOO_SOPT(...) casten
 *     (Windows verlangt const char*).
 *   - Auf Sockets recv/send statt read/write verwenden — unter POSIX
 *     semantisch identisch, unter Windows zwingend.
 */
#ifndef MOO_NET_COMPAT_H
#define MOO_NET_COMPAT_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <errno.h>

typedef SOCKET moo_sockfd_t;
typedef int    moo_ssize_t;

#define MOO_INVALID_SOCK   INVALID_SOCKET
#define MOO_SOCK_BAD(fd)   ((fd) == INVALID_SOCKET)
#define moo_closesock(fd)  closesocket(fd)
#define MOO_SOPT(p)        ((const char*)(p))

/* P2-T102: Windows kennt kein SIGPIPE; moo_send ist nur der einheitliche Name. */
static inline moo_ssize_t moo_send(moo_sockfd_t fd, const void* buf, size_t len, int flags) {
    return send(fd, (const char*)buf, (int)len, flags);
}
static inline void moo_sock_nosigpipe(moo_sockfd_t fd) { (void)fd; }

#ifndef MOO_UEFI_COLLECTIONS
/* WSAStartup einmal pro Translation-Unit beim Programmstart (vor main).
 * GCC/MinGW: __attribute__((constructor)) ueber die CRT-.ctors.
 * MSVC: Funktionspointer in der CRT-Initializer-Section .CRT$XCU.
 * Mehrfaches WSAStartup (net + web) ist erlaubt — Windows refcountet;
 * auf WSACleanup wird bewusst verzichtet (Prozessende raeumt auf). */
#if defined(_MSC_VER)
static int moo_net_wsa_init_impl_(void) {
    WSADATA wsa;
    (void)WSAStartup(MAKEWORD(2, 2), &wsa);
    return 0;
}
__pragma(section(".CRT$XCU", read))
__declspec(allocate(".CRT$XCU"))
static int (*moo_net_wsa_init_ptr_)(void) = moo_net_wsa_init_impl_;
#else
static void moo_net_wsa_init_(void) __attribute__((constructor));
static void moo_net_wsa_init_(void) {
    WSADATA wsa;
    (void)WSAStartup(MAKEWORD(2, 2), &wsa);
}
#endif
#endif /* !MOO_UEFI_COLLECTIONS */

#else /* POSIX */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

typedef int     moo_sockfd_t;
typedef ssize_t moo_ssize_t;

#define MOO_INVALID_SOCK   (-1)
#define MOO_SOCK_BAD(fd)   ((fd) < 0)
#define moo_closesock(fd)  close(fd)
#define MOO_SOPT(p)        ((const void*)(p))

/* P2-T102: Schreiben auf einen vom Gegenueber geschlossenen Socket loest unter
 * POSIX SIGPIPE aus; die Standardaktion beendet den Prozess (RC 141), bevor der
 * Moo-Fehler geworfen werden kann. Geschuetzt wird pro Aufruf bzw. pro Socket,
 * ohne den prozessweiten Signalzustand zu aendern (Handler anderer Threads und
 * das Verhalten von stdout-Pipes bleiben unberuehrt):
 *   - moo_send: send() mit MSG_NOSIGNAL (Linux/BSD) -> EPIPE statt Signal.
 *   - moo_sock_nosigpipe: SO_NOSIGPIPE am Socket (macOS/BSD), deckt auch
 *     write() fremder Bibliotheken ab.
 * OpenSSL bekommt in moo_tls_openssl.c einen eigenen Socket-BIO, der ebenfalls
 * ueber moo_send schreibt. Alle Socket-Sends der Runtime laufen ueber moo_send. */
#if defined(MSG_NOSIGNAL)
#define MOO_MSG_NOSIGNAL MSG_NOSIGNAL
#else
#define MOO_MSG_NOSIGNAL 0
#endif

static inline moo_ssize_t moo_send(moo_sockfd_t fd, const void* buf, size_t len, int flags) {
    return send(fd, buf, len, flags | MOO_MSG_NOSIGNAL);
}

static inline void moo_sock_nosigpipe(moo_sockfd_t fd) {
#if defined(SO_NOSIGPIPE)
    int ein = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &ein, sizeof ein);
#else
    (void)fd;
#endif
}

#endif /* _WIN32 */

#endif /* MOO_NET_COMPAT_H */
