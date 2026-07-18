/*
 * moo_tls_openssl.c — TLS-Backend fuer moo, nativer Linux-Pfad (OpenSSL).
 * Erfuellt den Vertrag moo_tls.h (nur Schicht 1: der Backend). Die backend-
 * agnostischen Builtins (moo_tls_connect/send/recv/close/starttls) liegen in
 * moo_tls.c und sind fuer beide Backends gemeinsam. Genau ein Backend wird
 * kompiliert (Build-Flag MOO_TLS_BACKEND, Default openssl).
 */
#include "moo_runtime.h"
#include "moo_tls.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

/* Shared TCP-Connect (DNS + optionaler Connect-Timeout) aus moo_net.c. */
extern int moo_net_tcp_connect_timeout(const char* host, int port, int timeout_ms,
                                       char* errbuf, int errlen);

typedef struct {
    SSL*     ssl;
    SSL_CTX* ctx;
    int      fd;
} OsslConn;

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

/* Wickelt einen bereits verbundenen fd in TLS: SNI, System-CA-Verifikation,
 * Hostname-Check, Handshake. Uebernimmt den fd (schliesst ihn bei Fehler). */
static void* ossl_wrap_fd(int fd, const char* host, char* errbuf, int errlen) {
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

static void* ossl_verbinde(const char* host, int port, int timeout_ms, char* errbuf, int errlen) {
    int fd = moo_net_tcp_connect_timeout(host, port, timeout_ms, errbuf, errlen);
    if (fd < 0) return NULL;   /* errbuf ist von moo_net_tcp_connect_timeout gesetzt */
    return ossl_wrap_fd(fd, host, errbuf, errlen);
}

static void* ossl_upgrade(int fd, const char* host, char* errbuf, int errlen) {
    return ossl_wrap_fd(fd, host, errbuf, errlen);
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
    "openssl", ossl_verbinde, ossl_schreibe, ossl_lese, ossl_schliesse, ossl_upgrade
};

const MooTlsBackend* moo_tls_backend(void) {
    return &OSSL_BACKEND;
}
