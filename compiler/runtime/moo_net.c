/**
 * moo_net.c — TCP/UDP Netzwerk-Runtime fuer moo.
 * Socket-basiert: Server, Client, Read, Write.
 */

#include "moo_runtime.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Neuer Tag fuer Sockets
// MOO_SOCKET = 14 (in moo_runtime.h definiert)

typedef struct {
    int fd;
    int type; // SOCK_STREAM oder SOCK_DGRAM
    bool is_server;
} MooSocket;

// Vorwaertsdeklarationen
extern MooValue moo_string_new(const char* s);
extern MooValue moo_number(double n);
extern MooValue moo_bool(bool b);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

static MooSocket* get_socket(MooValue v) {
    if (v.tag != MOO_SOCKET) return NULL;
    return (MooSocket*)moo_val_as_ptr(v);
}

static MooValue make_socket(int fd, int type, bool is_server) {
    MooSocket* s = (MooSocket*)malloc(sizeof(MooSocket));
    s->fd = fd;
    s->type = type;
    s->is_server = is_server;
    MooValue v;
    v.tag = MOO_SOCKET;
    moo_val_set_ptr(&v, s);
    return v;
}

// === TCP Server ===

MooValue moo_tcp_server(MooValue port) {
    int p = (int)MV_NUM(port);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        moo_throw(moo_string_new("Socket erstellen fehlgeschlagen"));
        return moo_none();
    }

    // SO_REUSEADDR damit Port sofort wiederverwendbar
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(p);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        moo_throw(moo_string_new("Bind fehlgeschlagen"));
        return moo_none();
    }

    if (listen(fd, 128) < 0) {
        close(fd);
        moo_throw(moo_string_new("Listen fehlgeschlagen"));
        return moo_none();
    }

    return make_socket(fd, SOCK_STREAM, true);
}

// === TCP Connect (Client) ===

MooValue moo_tcp_connect(MooValue host, MooValue port) {
    if (host.tag != MOO_STRING) {
        moo_throw(moo_string_new("Host muss ein String sein"));
        return moo_none();
    }

    const char* h = MV_STR(host)->chars;
    int p = (int)MV_NUM(port);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        moo_throw(moo_string_new("Socket erstellen fehlgeschlagen"));
        return moo_none();
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(p);

    if (inet_pton(AF_INET, h, &addr.sin_addr) <= 0) {
        close(fd);
        char err[128];
        snprintf(err, sizeof(err), "Ungueltige Adresse: %s", h);
        moo_throw(moo_string_new(err));
        return moo_none();
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        moo_throw(moo_string_new("Verbindung fehlgeschlagen"));
        return moo_none();
    }

    return make_socket(fd, SOCK_STREAM, false);
}

// === UDP Socket ===

MooValue moo_udp_socket(MooValue port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        moo_throw(moo_string_new("UDP Socket erstellen fehlgeschlagen"));
        return moo_none();
    }

    if (port.tag == MOO_NUMBER && MV_NUM(port) > 0) {
        int p = (int)MV_NUM(port);
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(p);
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            moo_throw(moo_string_new("UDP Bind fehlgeschlagen"));
            return moo_none();
        }
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

    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int client_fd = accept(s->fd, (struct sockaddr*)&client_addr, &len);
    if (client_fd < 0) {
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
    ssize_t n = read(s->fd, buf, max);
    if (n <= 0) {
        free(buf);
        return moo_string_new("");
    }
    buf[n] = '\0';
    MooValue result = moo_string_new(buf);
    free(buf);
    return result;
}

void moo_socket_write(MooValue sock, MooValue data) {
    MooSocket* s = get_socket(sock);
    if (!s || data.tag != MOO_STRING) return;

    const char* str = MV_STR(data)->chars;
    int32_t len = MV_STR(data)->length;
    write(s->fd, str, len);
}

void moo_socket_close(MooValue sock) {
    MooSocket* s = get_socket(sock);
    if (!s) return;
    close(s->fd);
    s->fd = -1;
}
