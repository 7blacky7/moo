/**
 * moo_net.c — TCP/UDP Netzwerk-Runtime fuer moo.
 * Socket-basiert: Server, Client, Read, Write.
 */

#include "moo_runtime.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Neuer Tag fuer Sockets
// MOO_SOCKET = 14 (in moo_runtime.h definiert)

typedef struct {
    int32_t refcount;  // MUSS ERSTES FELD SEIN — get_refcount_ptr nimmt *(int32_t*)ptr
    int fd;
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

static MooValue make_socket(int fd, int type, bool is_server) {
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

// Wird von moo_release aufgerufen wenn Socket-refcount auf 0 faellt.
// Exportiert damit moo_memory.c es bei refcount==0 aufrufen kann.
void moo_socket_free(void* ptr) {
    if (!ptr) return;
    MooSocket* s = (MooSocket*)ptr;
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    free(s);
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

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

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
        return moo_string_new_len("", 0);
    }
    // Binary-safe: nutzt explizite Laenge statt strlen, damit NUL-Bytes
    // im Stream (z.B. Postgres-Wire-Protocol) erhalten bleiben.
    MooValue result = moo_string_new_len(buf, (int32_t)n);
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
    ssize_t n = read(s->fd, buf, max);
    if (n <= 0) {
        free(buf);
        return moo_list_new(0);
    }
    MooValue list = moo_list_new((int32_t)n);
    for (ssize_t i = 0; i < n; i++) {
        moo_list_append(list, moo_number((double)buf[i]));
    }
    free(buf);
    return list;
}

// Schreibt eine Liste von Zahlen (0..255) als Bytes.
void moo_socket_write_bytes(MooValue sock, MooValue list) {
    MooSocket* s = get_socket(sock);
    if (!s || list.tag != MOO_LIST) return;
    int32_t len = (int32_t)MV_NUM(moo_list_length(list));
    if (len <= 0) return;
    unsigned char* buf = (unsigned char*)malloc(len);
    for (int32_t i = 0; i < len; i++) {
        MooValue v = moo_list_get(list, moo_number((double)i));
        int b = (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : 0;
        buf[i] = (unsigned char)(b & 0xFF);
    }
    write(s->fd, buf, len);
    free(buf);
}

// Konvertiert eine Liste von Zahlen in einen binary-safe String (length-tagged).
MooValue moo_bytes_to_string(MooValue list) {
    if (list.tag != MOO_LIST) return moo_string_new_len("", 0);
    int32_t len = (int32_t)MV_NUM(moo_list_length(list));
    if (len <= 0) return moo_string_new_len("", 0);
    char* buf = (char*)malloc(len + 1);
    for (int32_t i = 0; i < len; i++) {
        MooValue v = moo_list_get(list, moo_number((double)i));
        int b = (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : 0;
        buf[i] = (char)(b & 0xFF);
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
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
}

// Verbindet einen UDP-Socket zu einem Default-Peer (host, port). Danach
// koennen schreibe_bytes/lesen_bytes wie bei TCP genutzt werden — der
// Kernel routet die packets an den connected peer.
void moo_udp_connect(MooValue sock, MooValue host, MooValue port) {
    MooSocket* s = get_socket(sock);
    if (!s || s->fd < 0) return;
    if (host.tag != MOO_STRING || port.tag != MOO_NUMBER) return;
    const char* h = MV_STR(host)->chars;
    int p = (int)MV_NUM(port);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(p);
    if (inet_pton(AF_INET, h, &addr.sin_addr) <= 0) {
        moo_throw(moo_string_new("udp_verbinden: ungueltige IP"));
        return;
    }
    if (connect(s->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        moo_throw(moo_string_new("udp_verbinden: connect fehlgeschlagen"));
    }
}

#include <sys/time.h>
// Setzt Receive- und Accept-Timeout in Millisekunden. 0 = blocking.
// Nach Timeout liefert lesen/lesen_bytes/annehmen einen leeren String/Liste
// und der MooSocket bleibt nutzbar.
void moo_socket_set_timeout(MooValue sock, MooValue ms) {
    MooSocket* s = get_socket(sock);
    if (!s || s->fd < 0) return;
    int millis = (ms.tag == MOO_NUMBER) ? (int)MV_NUM(ms) : 0;
    struct timeval tv;
    tv.tv_sec  = millis / 1000;
    tv.tv_usec = (millis % 1000) * 1000;
    setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
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
extern MooValue moo_dict_has(MooValue dict, MooValue key);
extern MooValue moo_list_contains(MooValue list, MooValue item);
extern MooValue moo_string_contains(MooValue s, MooValue sub);
extern MooValue moo_bool(bool b);

MooValue moo_smart_contains(MooValue container, MooValue item) {
    switch (container.tag) {
        case MOO_DICT:   return moo_dict_has(container, item);
        case MOO_LIST:   return moo_list_contains(container, item);
        case MOO_STRING: return moo_string_contains(container, item);
        default:         return moo_bool(false);
    }
}
