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

/* WSAStartup einmal pro Translation-Unit beim Programmstart (vor main).
 * MinGW unterstuetzt __attribute__((constructor)) ueber die CRT-.ctors.
 * Mehrfaches WSAStartup (net + web) ist erlaubt — Windows refcountet;
 * auf WSACleanup wird bewusst verzichtet (Prozessende raeumt auf). */
static void moo_net_wsa_init_(void) __attribute__((constructor));
static void moo_net_wsa_init_(void) {
    WSADATA wsa;
    (void)WSAStartup(MAKEWORD(2, 2), &wsa);
}

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

#endif /* _WIN32 */

#endif /* MOO_NET_COMPAT_H */
