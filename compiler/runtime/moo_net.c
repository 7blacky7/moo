/**
 * moo_net.c — TCP/UDP Netzwerk-Runtime fuer moo.
 * Socket-basiert: Server, Client, Read, Write.
 */

#include "moo_runtime.h"
#include "moo_net_compat.h"
#include <limits.h>
#ifndef _WIN32
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>
#endif

// Neuer Tag fuer Sockets
// MOO_SOCKET = 14 (in moo_runtime.h definiert)

typedef struct {
    int32_t refcount;  // MUSS ERSTES FELD SEIN — get_refcount_ptr nimmt *(int32_t*)ptr
    moo_sockfd_t fd;
    int type; // SOCK_STREAM oder SOCK_DGRAM
    bool is_server;
} MooSocket;

// Vorwaertsdeklarationen
extern MooValue moo_string_new(const char* s);
extern MooValue moo_string_new_len(const char* chars, int32_t len);
extern MooValue moo_number(double n);
extern MooValue moo_bool(bool b);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);
extern MooValue moo_list_new(int32_t capacity);
extern void moo_list_append(MooValue list, MooValue value);
extern MooValue moo_list_get(MooValue list, MooValue index);
extern MooValue moo_list_length(MooValue list);

static MooSocket* get_socket(MooValue v) {
    if (v.tag != MOO_SOCKET) return NULL;
    return (MooSocket*)moo_val_as_ptr(v);
}

static MooValue make_socket(moo_sockfd_t fd, int type, bool is_server) {
    MooSocket* s = (MooSocket*)malloc(sizeof(MooSocket));
    s->refcount = 1;  // owner hat 1 ref, Release bei refcount==0 schliesst+free
    s->fd = fd;
    s->type = type;
    s->is_server = is_server;
    MooValue v;
    v.tag = MOO_SOCKET;
    moo_val_set_ptr(&v, s);
    return v;
}

/* Rohes Socket-Handle fuer STARTTLS, pointerbreit oder -1. */
intptr_t moo_net_socket_fd(MooValue v) {
    MooSocket* s = get_socket(v);
    return s ? (intptr_t)s->fd : (intptr_t)-1;
}

/* TCP-Connect mit DNS (getaddrinfo, IPv4+IPv6) + optionalem Connect-Timeout
 * (ms; 0 = blockierend). Rueckgabe: fd oder -1 (errbuf gesetzt). Aufrufer
 * (TLS-Backends) uebernimmt den fd. Der Timeout-Pfad arbeitet auf POSIX
 * und Windows nonblocking und stellt erfolgreiche Sockets wieder auf blocking. */
intptr_t moo_net_tcp_connect_timeout(const char* host, int port, int timeout_ms,
                                     char* errbuf, int errlen) {
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) {
        if (errbuf) snprintf(errbuf, errlen, "DNS-Aufloesung fehlgeschlagen: %s", host);
        return -1;
    }
    moo_sockfd_t fd = MOO_INVALID_SOCK;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (MOO_SOCK_BAD(fd)) continue;
#ifndef _WIN32
        if (timeout_ms > 0) {
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
            if (rc == 0) { fcntl(fd, F_SETFL, fl); break; }
            if (errno != EINPROGRESS) { moo_closesock(fd); fd = MOO_INVALID_SOCK; continue; }
            fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
            struct timeval tv;
            tv.tv_sec  = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            int sel = select((int)fd + 1, NULL, &wf, NULL, &tv);
            if (sel <= 0) { moo_closesock(fd); fd = MOO_INVALID_SOCK; continue; }
            int soerr = 0; socklen_t sl = sizeof soerr;
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
            if (soerr != 0) { moo_closesock(fd); fd = MOO_INVALID_SOCK; continue; }
            fcntl(fd, F_SETFL, fl);
            break;
        }
#else
        if (timeout_ms > 0) {
            u_long nonblocking = 1;
            if (ioctlsocket(fd, FIONBIO, &nonblocking) != 0) {
                moo_closesock(fd); fd = MOO_INVALID_SOCK; continue;
            }
            int rc = connect(fd, rp->ai_addr, (int)rp->ai_addrlen);
            if (rc == 0) {
                u_long blocking = 0;
                (void)ioctlsocket(fd, FIONBIO, &blocking);
                break;
            }
            int connect_error = WSAGetLastError();
            if (connect_error != WSAEWOULDBLOCK &&
                connect_error != WSAEINPROGRESS &&
                connect_error != WSAEALREADY) {
                moo_closesock(fd); fd = MOO_INVALID_SOCK; continue;
            }
            fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
            struct timeval tv;
            tv.tv_sec  = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            int sel = select(0, NULL, &wf, NULL, &tv);
            if (sel <= 0) { moo_closesock(fd); fd = MOO_INVALID_SOCK; continue; }
            int soerr = 0;
            int sl = (int)sizeof soerr;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl) != 0 || soerr != 0) {
                moo_closesock(fd); fd = MOO_INVALID_SOCK; continue;
            }
            u_long blocking = 0;
            if (ioctlsocket(fd, FIONBIO, &blocking) != 0) {
                moo_closesock(fd); fd = MOO_INVALID_SOCK; continue;
            }
            break;
        }
#endif
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        moo_closesock(fd); fd = MOO_INVALID_SOCK;
    }
    freeaddrinfo(res);
    if (MOO_SOCK_BAD(fd)) {
        if (errbuf) snprintf(errbuf, errlen, "TCP-Verbindung fehlgeschlagen: %s:%d%s",
                             host, port, timeout_ms > 0 ? " (Timeout)" : "");
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, MOO_SOPT(&one), sizeof(one));
    return (intptr_t)fd;
}

// Wird von moo_release aufgerufen wenn Socket-refcount auf 0 faellt.
// Exportiert damit moo_memory.c es bei refcount==0 aufrufen kann.
void moo_socket_free(void* ptr) {
    if (!ptr) return;
    MooSocket* s = (MooSocket*)ptr;
    if (!MOO_SOCK_BAD(s->fd)) {
        moo_closesock(s->fd);
        s->fd = MOO_INVALID_SOCK;
    }
    free(s);
}

// === TCP Server ===

MooValue moo_tcp_server(MooValue port) {
    if (port.tag != MOO_NUMBER) {
        moo_throw(moo_string_new("tcp_server: Port muss eine Zahl sein"));
        return moo_none();
    }
    int p = (int)MV_NUM(port);
    if (p < 0 || p > 65535) {
        moo_throw(moo_string_new("tcp_server: Port muss zwischen 0 und 65535 liegen"));
        return moo_none();
    }

    moo_sockfd_t fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (!MOO_SOCK_BAD(fd)) {
        int opt = 1;
        int v6only = 0;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, MOO_SOPT(&opt), sizeof(opt));
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, MOO_SOPT(&v6only), sizeof(v6only));

        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons((uint16_t)p);
        if (bind(fd, (struct sockaddr*)&addr6, sizeof(addr6)) != 0 ||
            listen(fd, 128) != 0) {
            moo_closesock(fd);
            fd = MOO_INVALID_SOCK;
        }
    }

    if (MOO_SOCK_BAD(fd)) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (!MOO_SOCK_BAD(fd)) {
            int opt = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, MOO_SOPT(&opt), sizeof(opt));
            struct sockaddr_in addr4;
            memset(&addr4, 0, sizeof(addr4));
            addr4.sin_family = AF_INET;
            addr4.sin_addr.s_addr = htonl(INADDR_ANY);
            addr4.sin_port = htons((uint16_t)p);
            if (bind(fd, (struct sockaddr*)&addr4, sizeof(addr4)) != 0 ||
                listen(fd, 128) != 0) {
                moo_closesock(fd);
                fd = MOO_INVALID_SOCK;
            }
        }
    }

    if (MOO_SOCK_BAD(fd)) {
        moo_throw(moo_string_new("tcp_server: Bind oder Listen fehlgeschlagen"));
        return moo_none();
    }
    return make_socket(fd, SOCK_STREAM, true);
}

// === TCP Connect (Client) ===

MooValue moo_tcp_connect(MooValue host, MooValue port) {
    if (host.tag != MOO_STRING || port.tag != MOO_NUMBER) {
        moo_throw(moo_string_new("tcp_verbinde: Host muss Text und Port eine Zahl sein"));
        return moo_none();
    }

    const char* h = MV_STR(host)->chars;
    int p = (int)MV_NUM(port);
    if (p < 1 || p > 65535) {
        moo_throw(moo_string_new("tcp_verbinde: Port muss zwischen 1 und 65535 liegen"));
        return moo_none();
    }

    char err[256];
    intptr_t raw_fd = moo_net_tcp_connect_timeout(h, p, 0, err, (int)sizeof err);
    if (raw_fd < 0) {
        moo_throw(moo_string_new(err));
        return moo_none();
    }

    return make_socket((moo_sockfd_t)raw_fd, SOCK_STREAM, false);
}

// === UDP Socket ===

MooValue moo_udp_socket(MooValue port) {
    int p = (port.tag == MOO_NUMBER) ? (int)MV_NUM(port) : 0;
    if (p < 0 || p > 65535) {
        moo_throw(moo_string_new("udp_socket: Port muss zwischen 0 und 65535 liegen"));
        return moo_none();
    }

    moo_sockfd_t fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (!MOO_SOCK_BAD(fd)) {
        int v6only = 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, MOO_SOPT(&v6only), sizeof(v6only));
        if (p > 0) {
            struct sockaddr_in6 addr6;
            memset(&addr6, 0, sizeof(addr6));
            addr6.sin6_family = AF_INET6;
            addr6.sin6_addr = in6addr_any;
            addr6.sin6_port = htons((uint16_t)p);
            if (bind(fd, (struct sockaddr*)&addr6, sizeof(addr6)) != 0) {
                moo_closesock(fd);
                fd = MOO_INVALID_SOCK;
            }
        }
    }

    if (MOO_SOCK_BAD(fd)) {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (!MOO_SOCK_BAD(fd) && p > 0) {
            struct sockaddr_in addr4;
            memset(&addr4, 0, sizeof(addr4));
            addr4.sin_family = AF_INET;
            addr4.sin_addr.s_addr = htonl(INADDR_ANY);
            addr4.sin_port = htons((uint16_t)p);
            if (bind(fd, (struct sockaddr*)&addr4, sizeof(addr4)) != 0) {
                moo_closesock(fd);
                fd = MOO_INVALID_SOCK;
            }
        }
    }

    if (MOO_SOCK_BAD(fd)) {
        moo_throw(moo_string_new("udp_socket: Socket oder Bind fehlgeschlagen"));
        return moo_none();
    }
    return make_socket(fd, SOCK_DGRAM, false);
}

// === Socket-Methoden ===

MooValue moo_socket_accept(MooValue server) {
    MooSocket* s = get_socket(server);
    if (!s || !s->is_server) {
        moo_throw(moo_string_new("Nicht ein Server-Socket"));
        return moo_none();
    }

    struct sockaddr_storage client_addr;
    socklen_t len = sizeof(client_addr);
    moo_sockfd_t client_fd = accept(s->fd, (struct sockaddr*)&client_addr, &len);
    if (MOO_SOCK_BAD(client_fd)) {
        moo_throw(moo_string_new("Accept fehlgeschlagen"));
        return moo_none();
    }

    return make_socket(client_fd, SOCK_STREAM, false);
}

MooValue moo_socket_read(MooValue sock, MooValue max_bytes) {
    MooSocket* s = get_socket(sock);
    if (!s) return moo_none();

    int max = (max_bytes.tag == MOO_NUMBER) ? (int)MV_NUM(max_bytes) : 4096;
    if (max <= 0) max = 4096;

    char* buf = (char*)malloc(max + 1);
    moo_ssize_t n = recv(s->fd, buf, max, 0);
    if (n <= 0) {
        free(buf);
        return moo_string_new_len("", 0);
    }
    // Binary-safe: nutzt explizite Laenge statt strlen, damit NUL-Bytes
    // im Stream (z.B. Postgres-Wire-Protocol) erhalten bleiben.
    MooValue result = moo_string_new_len(buf, (int32_t)n);
    free(buf);
    return result;
}

static bool moo_socket_send_all(MooSocket* s, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t rest = len - off;
        int part = rest > (size_t)INT_MAX ? INT_MAX : (int)rest;
        moo_ssize_t n = send(s->fd, data + off, part, 0);
        if (n < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            return false;
        }
        if (n == 0) return false;
        off += (size_t)n;
    }
    return true;
}

void moo_socket_write(MooValue sock, MooValue data) {
    MooSocket* s = get_socket(sock);
    if (!s || data.tag != MOO_STRING) return;
    if (!moo_socket_send_all(s, MV_STR(data)->chars, (size_t)MV_STR(data)->length)) {
        moo_throw(moo_string_new("Socket schreiben fehlgeschlagen"));
    }
}

// === Byte-Array API (binary-safe, fuer wire protocols) ===

extern MooValue moo_string_new_len(const char* chars, int32_t len);
extern MooValue moo_list_new(int32_t capacity);
extern void moo_list_append(MooValue list, MooValue value);
extern MooValue moo_list_get(MooValue list, MooValue index);
extern MooValue moo_list_length(MooValue list);

// Liest n Bytes (blockierend) und gibt sie als Liste<Zahl> zurueck.
MooValue moo_socket_read_bytes(MooValue sock, MooValue max_bytes) {
    MooSocket* s = get_socket(sock);
    if (!s) return moo_list_new(0);
    int max = (max_bytes.tag == MOO_NUMBER) ? (int)MV_NUM(max_bytes) : 4096;
    if (max <= 0) max = 4096;

    unsigned char* buf = (unsigned char*)malloc(max);
    moo_ssize_t n = recv(s->fd, (char*)buf, max, 0);
    if (n <= 0) {
        free(buf);
        return moo_list_new(0);
    }
    MooValue list = moo_list_new((int32_t)n);
    for (moo_ssize_t i = 0; i < n; i++) {
        moo_list_append(list, moo_number((double)buf[i]));
    }
    free(buf);
    return list;
}

// Schreibt eine Liste von Zahlen (0..255) als Bytes.
void moo_socket_write_bytes(MooValue sock, MooValue list) {
    MooSocket* s = get_socket(sock);
    if (!s || list.tag != MOO_LIST) return;
    MooValue len_value = moo_list_length(list);
    int32_t len = (int32_t)MV_NUM(len_value);
    if (len <= 0) return;
    unsigned char* buf = (unsigned char*)malloc((size_t)len);
    if (!buf) {
        moo_throw(moo_string_new("Socket Byte-Schreiben: Speicherreservierung fehlgeschlagen"));
        return;
    }
    for (int32_t i = 0; i < len; i++) {
        MooValue index = moo_number((double)i);
        MooValue v = moo_list_get(list, index);
        int b = (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : 0;
        buf[i] = (unsigned char)(b & 0xFF);
        moo_release(v);
    }
    bool ok = moo_socket_send_all(s, (const char*)buf, (size_t)len);
    free(buf);
    if (!ok) moo_throw(moo_string_new("Socket Byte-Schreiben fehlgeschlagen"));
}

// Konvertiert eine Liste von Zahlen in einen binary-safe String (length-tagged).
MooValue moo_bytes_to_string(MooValue list) {
    if (list.tag != MOO_LIST) return moo_string_new_len("", 0);
    int32_t len = (int32_t)MV_NUM(moo_list_length(list));
    if (len <= 0) return moo_string_new_len("", 0);
    char* buf = (char*)malloc(len + 1);
    for (int32_t i = 0; i < len; i++) {
        MooValue index = moo_number((double)i);
        MooValue v = moo_list_get(list, index);
        int b = (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : 0;
        buf[i] = (char)(b & 0xFF);
        moo_release(v);
    }
    buf[len] = '\0';
    MooValue result = moo_string_new_len(buf, len);
    free(buf);
    return result;
}

// Konvertiert einen String in eine Liste seiner Bytes (binary-safe).
MooValue moo_string_to_bytes(MooValue s) {
    if (s.tag != MOO_STRING) return moo_list_new(0);
    int32_t len = MV_STR(s)->length;
    MooValue list = moo_list_new(len);
    const unsigned char* chars = (const unsigned char*)MV_STR(s)->chars;
    for (int32_t i = 0; i < len; i++) {
        moo_list_append(list, moo_number((double)chars[i]));
    }
    return list;
}

void moo_socket_close(MooValue sock) {
    MooSocket* s = get_socket(sock);
    if (!s) return;
    if (!MOO_SOCK_BAD(s->fd)) {
        moo_closesock(s->fd);
        s->fd = MOO_INVALID_SOCK;
    }
}

// Verbindet einen UDP-Socket zu einem Default-Peer (host, port). Danach
// koennen schreibe_bytes/lesen_bytes wie bei TCP genutzt werden — der
// Kernel routet die packets an den connected peer.
void moo_udp_connect(MooValue sock, MooValue host, MooValue port) {
    MooSocket* s = get_socket(sock);
    if (!s || MOO_SOCK_BAD(s->fd)) return;
    if (host.tag != MOO_STRING || port.tag != MOO_NUMBER) {
        moo_throw(moo_string_new("udp_verbinden: Host muss Text und Port eine Zahl sein"));
        return;
    }
    const char* h = MV_STR(host)->chars;
    int p = (int)MV_NUM(port);
    if (p < 1 || p > 65535) {
        moo_throw(moo_string_new("udp_verbinden: Port muss zwischen 1 und 65535 liegen"));
        return;
    }

    struct sockaddr_storage local_addr;
    socklen_t local_len = sizeof(local_addr);
    if (getsockname(s->fd, (struct sockaddr*)&local_addr, &local_len) != 0) {
        moo_throw(moo_string_new("udp_verbinden: Socket-Familie nicht bestimmbar"));
        return;
    }

    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", p);
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(h, portstr, &hints, &res) != 0) {
        moo_throw(moo_string_new("udp_verbinden: DNS-Aufloesung fehlgeschlagen"));
        return;
    }

    int connected = 0;
    for (rp = res; rp; rp = rp->ai_next) {
        if (local_addr.ss_family == rp->ai_family) {
            if (connect(s->fd, rp->ai_addr, rp->ai_addrlen) == 0) {
                connected = 1;
                break;
            }
        } else if (local_addr.ss_family == AF_INET6 && rp->ai_family == AF_INET) {
            const struct sockaddr_in* src4 = (const struct sockaddr_in*)rp->ai_addr;
            struct sockaddr_in6 mapped;
            memset(&mapped, 0, sizeof(mapped));
            mapped.sin6_family = AF_INET6;
            mapped.sin6_port = src4->sin_port;
            mapped.sin6_addr.s6_addr[10] = 0xff;
            mapped.sin6_addr.s6_addr[11] = 0xff;
            memcpy(&mapped.sin6_addr.s6_addr[12], &src4->sin_addr, 4);
            if (connect(s->fd, (struct sockaddr*)&mapped, sizeof(mapped)) == 0) {
                connected = 1;
                break;
            }
        }
    }
    freeaddrinfo(res);
    if (!connected) {
        moo_throw(moo_string_new("udp_verbinden: keine passende Adresse erreichbar"));
    }
}

// Setzt Receive- und Accept-Timeout in Millisekunden. 0 = blocking.
// Nach Timeout liefert lesen/lesen_bytes/annehmen einen leeren String/Liste
// und der MooSocket bleibt nutzbar.
void moo_socket_set_timeout(MooValue sock, MooValue ms) {
    MooSocket* s = get_socket(sock);
    if (!s || MOO_SOCK_BAD(s->fd)) return;
    int millis = (ms.tag == MOO_NUMBER) ? (int)MV_NUM(ms) : 0;
    if (millis < 0) millis = 0;
#ifdef _WIN32
    // Windows: SO_RCVTIMEO/SO_SNDTIMEO erwarten DWORD-Millisekunden.
    DWORD tv = (DWORD)millis;
    setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, MOO_SOPT(&tv), sizeof(tv));
    setsockopt(s->fd, SOL_SOCKET, SO_SNDTIMEO, MOO_SOPT(&tv), sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec  = millis / 1000;
    tv.tv_usec = (millis % 1000) * 1000;
    setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, MOO_SOPT(&tv), sizeof(tv));
    setsockopt(s->fd, SOL_SOCKET, SO_SNDTIMEO, MOO_SOPT(&tv), sizeof(tv));
#endif
}

// Tag-dispatchender close — wird vom Codegen fuer den Method-Namen
// 'schliessen'/'close' aufgerufen. Vermeidet dass channel_close auf
// einem Socket landet (oder umgekehrt) was im pthread-mutex Code
// von moo_thread.c heap-corruption ausloest (tpp.c:83).
extern void moo_channel_close(MooValue ch);
extern void moo_db_close(MooValue db);
extern void moo_db_stmt_close(MooValue stmt);

#if defined(MOO_HAS_GL21) || defined(MOO_HAS_GL33) || defined(MOO_HAS_VULKAN)
extern void moo_window_close(MooValue window);
extern void moo_3d_close(MooValue win);
extern void moo_world_close(MooValue win);
#endif

void moo_smart_close(MooValue v) {
    switch (v.tag) {
        case MOO_SOCKET:   moo_socket_close(v); break;
        case MOO_DATABASE: moo_db_close(v); break;
        case MOO_DB_STMT:  moo_db_stmt_close(v); break;
#if defined(MOO_HAS_GL21) || defined(MOO_HAS_GL33) || defined(MOO_HAS_VULKAN)
        case MOO_WINDOW:   moo_window_close(v); break;
        case MOO_WINDOW3D:
            // MOO_WINDOW3D deckt sowohl plain-3D als auch Welt-Engine
            // (moo_world_create gibt dieselbe tag zurueck). moo_world_close
            // ist no-op bei !world.initialized, moo_3d_close ist idempotent
            // (prueft g_ctx==NULL) — Reihenfolge sicher.
            moo_world_close(v);
            moo_3d_close(v);
            break;
#else
        case MOO_WINDOW:
        case MOO_WINDOW3D:
            // UI-only/stdlib-only Builds enthalten die 2D/3D Runtime nicht.
            // Dann duerfen diese Tags keine harten Link-Abhaengigkeiten auf
            // moo_graphics/moo_3d/moo_world erzeugen. No-op ist sicherer als
            // ein Linker-Fehler; echte 3D-Programme brauchen ein 3D-Feature.
            break;
#endif
        default:
            // Channels (MooObject) und alles andere → channel_close
            // (das ignoriert non-channel objects safe).
            moo_channel_close(v);
            break;
    }
}

// Tag-dispatchender contains/enthaelt — Dicts → moo_dict_has, Listen →
// moo_list_contains, Strings → moo_string_contains.
// ARG-KONVENTION (CG1-v3): verbraucht item IMMER (Transfer-Semantik).
// moo_dict_has verbraucht den Key selbst (release_key_after_lookup);
// list/string_contains sind pure Reader → hier explizit releasen. Der
// Codegen-Arm (einziger Aufrufer) darf item danach NICHT anfassen —
// tag-abhaengiges Release im Codegen waere unmoeglich (Dict: Double-Free,
// String/Liste: Leak, empirisch ~60B/iter, ASan-bewiesen 2026-07-05).
// container bleibt borrowed (Methoden-Objekt, T1-Slot released).
extern MooValue moo_dict_has(MooValue dict, MooValue key);
extern MooValue moo_list_contains(MooValue list, MooValue item);
extern MooValue moo_string_contains(MooValue s, MooValue sub);
extern MooValue moo_bool(bool b);

MooValue moo_smart_contains(MooValue container, MooValue item) {
    switch (container.tag) {
        case MOO_DICT:   return moo_dict_has(container, item);
        case MOO_LIST: {
            MooValue r = moo_list_contains(container, item);
            moo_release(item);
            return r;
        }
        case MOO_STRING: {
            MooValue r = moo_string_contains(container, item);
            moo_release(item);
            return r;
        }
        default:
            moo_release(item);
            return moo_bool(false);
    }
}
