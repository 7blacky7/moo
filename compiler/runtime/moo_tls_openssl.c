/*
 * moo_tls_openssl.c — TLS-Client-Runtime fuer moo, nativer Linux-Pfad.
 * Backend: OpenSSL (System-libssl/libcrypto). Erfuellt den Vertrag moo_tls.h.
 *
 * Zwei Schichten:
 *  (1) OpenSSL-Backend (ossl_*): reine OpenSSL-Aufrufe, hinter der vtable.
 *  (2) Moo-Builtin-Schicht (moo_tls_*): backend-AGNOSTISCH — Handle-Tabelle
 *      + Aufrufe ueber moo_tls_backend(). Wenn ein zweiter Backend dazukommt
 *      (vendored mbedTLS), wandert Schicht (2) unveraendert in ein gemeinsames
 *      moo_tls.c; hier bleibt dann nur der OpenSSL-Backend.
 *
 * Handle-Modell: Integer-Slot (MOO_NUMBER) in eine statische Tabelle. Das
 * vermeidet einen neuen MooValue-Tag und Aenderungen an moo_memory.c/
 * moo_runtime.h. tls_schliesse gibt den Slot frei; nicht geschlossene
 * Verbindungen lecken bewusst (expliziter Close-Vertrag wie ein Datei-Handle).
 */
#include "moo_runtime.h"
#include "moo_tls.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

extern MooValue moo_string_new(const char* s);
extern MooValue moo_string_new_len(const char* chars, int32_t len);
extern MooValue moo_number(double n);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

/* ================= Schicht 1: OpenSSL-Backend ================= */

typedef struct {
    SSL*     ssl;
    SSL_CTX* ctx;
    int      fd;
} OsslConn;

/* TCP-connect mit DNS (getaddrinfo, IPv4+IPv6). Gibt fd oder -1. */
static int ossl_tcp_connect(const char* host, int port) {
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static void ossl_err(char* errbuf, int errlen, const char* msg) {
    unsigned long e = ERR_get_error();
    if (e) {
        char es[160];
        ERR_error_string_n(e, es, sizeof es);
        snprintf(errbuf, errlen, "%s (%s)", msg, es);
    } else {
        snprintf(errbuf, errlen, "%s", msg);
    }
}

static void* ossl_verbinde(const char* host, int port, char* errbuf, int errlen) {
    int fd = ossl_tcp_connect(host, port);
    if (fd < 0) { snprintf(errbuf, errlen, "TCP-Verbindung/DNS fehlgeschlagen: %s:%d", host, port); return NULL; }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { close(fd); ossl_err(errbuf, errlen, "SSL_CTX_new fehlgeschlagen"); return NULL; }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);            /* Cert-Verifikation an */
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {          /* System-CA-Store */
        SSL_CTX_free(ctx); close(fd);
        ossl_err(errbuf, errlen, "System-CA-Store nicht ladbar");
        return NULL;
    }

    SSL* ssl = SSL_new(ctx);
    if (!ssl) { SSL_CTX_free(ctx); close(fd); ossl_err(errbuf, errlen, "SSL_new fehlgeschlagen"); return NULL; }
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host);                       /* SNI */
    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    SSL_set1_host(ssl, host);                                  /* Hostname-Verifikation */

    if (SSL_connect(ssl) != 1) {
        ossl_err(errbuf, errlen, "TLS-Handshake/Zertifikat-Verifikation fehlgeschlagen");
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
        return NULL;
    }

    OsslConn* c = (OsslConn*)malloc(sizeof(OsslConn));
    if (!c) { SSL_free(ssl); SSL_CTX_free(ctx); close(fd); snprintf(errbuf, errlen, "malloc fehlgeschlagen"); return NULL; }
    c->ssl = ssl; c->ctx = ctx; c->fd = fd;
    return c;
}

static int ossl_schreibe(void* conn, const char* buf, int len) {
    return SSL_write(((OsslConn*)conn)->ssl, buf, len);
}

static int ossl_lese(void* conn, char* buf, int max) {
    return SSL_read(((OsslConn*)conn)->ssl, buf, max);
}

static void ossl_schliesse(void* conn) {
    OsslConn* c = (OsslConn*)conn;
    if (!c) return;
    SSL_shutdown(c->ssl);
    SSL_free(c->ssl);
    SSL_CTX_free(c->ctx);
    close(c->fd);
    free(c);
}

static const MooTlsBackend OSSL_BACKEND = {
    "openssl", ossl_verbinde, ossl_schreibe, ossl_lese, ossl_schliesse
};

const MooTlsBackend* moo_tls_backend(void) {
    return &OSSL_BACKEND;
}

/* ============ Schicht 2: Moo-Builtins (backend-agnostisch) ============ */

#define MOO_TLS_MAX 256
static void* g_tls_conns[MOO_TLS_MAX];  /* Slot frei = NULL */

static int tls_slot_of(MooValue handle) {
    if (handle.tag != MOO_NUMBER) return -1;
    int i = (int)MV_NUM(handle);
    if (i < 0 || i >= MOO_TLS_MAX || !g_tls_conns[i]) return -1;
    return i;
}

MooValue moo_tls_connect(MooValue host, MooValue port) {
    if (host.tag != MOO_STRING) { moo_throw(moo_string_new("tls_verbinde: host muss ein String sein")); return moo_none(); }
    const char* h = MV_STR(host)->chars;
    int p = (port.tag == MOO_NUMBER) ? (int)MV_NUM(port) : 443;

    int slot = -1;
    for (int i = 0; i < MOO_TLS_MAX; i++) if (!g_tls_conns[i]) { slot = i; break; }
    if (slot < 0) { moo_throw(moo_string_new("tls_verbinde: zu viele offene TLS-Verbindungen")); return moo_none(); }

    char err[256];
    void* conn = moo_tls_backend()->verbinde(h, p, err, sizeof err);
    if (!conn) {
        char msg[320];
        snprintf(msg, sizeof msg, "tls_verbinde: %s", err);
        moo_throw(moo_string_new(msg));
        return moo_none();
    }
    g_tls_conns[slot] = conn;
    return moo_number((double)slot);
}

MooValue moo_tls_send(MooValue handle, MooValue data) {
    int i = tls_slot_of(handle);
    if (i < 0 || data.tag != MOO_STRING) return moo_number(0);
    const char* s = MV_STR(data)->chars;
    int32_t len = MV_STR(data)->length;
    const MooTlsBackend* be = moo_tls_backend();
    int off = 0;
    while (off < len) {
        int n = be->schreibe(g_tls_conns[i], s + off, len - off);
        if (n <= 0) break;
        off += n;
    }
    return moo_number((double)off);
}

MooValue moo_tls_recv(MooValue handle, MooValue max_bytes) {
    int i = tls_slot_of(handle);
    if (i < 0) return moo_string_new_len("", 0);
    int max = (max_bytes.tag == MOO_NUMBER) ? (int)MV_NUM(max_bytes) : 4096;
    if (max <= 0) max = 4096;
    char* buf = (char*)malloc((size_t)max);
    if (!buf) return moo_string_new_len("", 0);
    int n = moo_tls_backend()->lese(g_tls_conns[i], buf, max);
    if (n <= 0) { free(buf); return moo_string_new_len("", 0); }
    MooValue r = moo_string_new_len(buf, (int32_t)n);
    free(buf);
    return r;
}

MooValue moo_tls_close(MooValue handle) {
    int i = tls_slot_of(handle);
    if (i < 0) return moo_none();
    moo_tls_backend()->schliesse(g_tls_conns[i]);
    g_tls_conns[i] = NULL;
    return moo_none();
}
