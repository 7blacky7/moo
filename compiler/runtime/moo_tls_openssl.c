/*
 * moo_tls_openssl.c — TLS-Backend fuer moo, nativer Linux-Pfad (OpenSSL).
 * Erfuellt den Vertrag moo_tls.h (nur Schicht 1: der Backend). Die backend-
 * agnostischen Builtins (moo_tls_connect/send/recv/close/starttls) liegen in
 * moo_tls.c und sind fuer beide Backends gemeinsam. Genau ein Backend wird
 * kompiliert (Build-Flag MOO_TLS_BACKEND, Default openssl).
 */
#include "moo_runtime.h"
#include "moo_tls.h"
#include "moo_net_compat.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

/* Shared TCP-Connect (DNS + optionaler Connect-Timeout) aus moo_net.c. */
extern intptr_t moo_net_tcp_connect_timeout(const char* host, int port, int timeout_ms,
                                            char* errbuf, int errlen);

/* P2-T102: Der Standard-Socket-BIO von OpenSSL schreibt per write(); auf einem
 * vom Gegenueber geschlossenen Socket loest das SIGPIPE aus und beendet den
 * Prozess (RC 141). Eigener Socket-BIO: Schreiben ueber moo_send (send mit
 * MSG_NOSIGNAL), Lesen ueber recv. Kein Eingriff in Signalmasken oder Handler,
 * fremde und anstehende SIGPIPE bleiben unberuehrt. Der fd gehoert weiter
 * OsslConn (ossl_schliesse schliesst ihn); der BIO schliesst nichts. */
static int moo_bio_write(BIO* b, const char* buf, int len) {
    int fd = (int)(intptr_t)BIO_get_data(b);
    BIO_clear_retry_flags(b);
    if (len <= 0) return 0;
    moo_ssize_t n = moo_send(fd, buf, (size_t)len, 0);
    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) BIO_set_retry_write(b);
    return (int)n;
}

static int moo_bio_read(BIO* b, char* buf, int len) {
    int fd = (int)(intptr_t)BIO_get_data(b);
    BIO_clear_retry_flags(b);
    if (len <= 0) return 0;
    moo_ssize_t n = recv(fd, buf, (size_t)len, 0);
    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) BIO_set_retry_read(b);
    return (int)n;
}

static int moo_bio_puts(BIO* b, const char* text) {
    size_t laenge = strlen(text);
    return moo_bio_write(b, text, laenge > (size_t)INT_MAX ? INT_MAX : (int)laenge);
}

static long moo_bio_ctrl(BIO* b, int cmd, long num, void* ptr) {
    (void)b; (void)num; (void)ptr;
    switch (cmd) {
        case BIO_CTRL_FLUSH: return 1;  /* ungepuffert */
        case BIO_CTRL_DUP:   return 1;
        default:             return 0;
    }
}

static int moo_bio_create(BIO* b) {
    BIO_set_data(b, NULL);
    BIO_set_init(b, 0);
    return 1;
}

static int moo_bio_destroy(BIO* b) {
    return b != NULL;  /* fd schliesst ossl_schliesse */
}

static BIO_METHOD* moo_bio_methode_wert = NULL;
static pthread_once_t moo_bio_methode_einmal = PTHREAD_ONCE_INIT;

static void moo_bio_methode_anlegen(void) {
    BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "moo-socket-nosignal");
    if (!m) return;
    if (!BIO_meth_set_write(m, moo_bio_write) || !BIO_meth_set_read(m, moo_bio_read) ||
        !BIO_meth_set_puts(m, moo_bio_puts) || !BIO_meth_set_ctrl(m, moo_bio_ctrl) ||
        !BIO_meth_set_create(m, moo_bio_create) || !BIO_meth_set_destroy(m, moo_bio_destroy)) {
        BIO_meth_free(m);
        return;
    }
    moo_bio_methode_wert = m;
}

static BIO* moo_bio_neu(int fd) {
    pthread_once(&moo_bio_methode_einmal, moo_bio_methode_anlegen);
    if (!moo_bio_methode_wert) return NULL;
    BIO* b = BIO_new(moo_bio_methode_wert);
    if (!b) return NULL;
    BIO_set_data(b, (void*)(intptr_t)fd);
    BIO_set_init(b, 1);
    return b;
}

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
    BIO* bio = moo_bio_neu(fd);
    if (!bio) {
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
        ossl_err(errbuf, errlen, "TLS-Socket-BIO nicht anlegbar");
        return NULL;
    }
    SSL_set_bio(ssl, bio, bio);  /* gleicher BIO lesend/schreibend: eine Referenz */
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

static int ossl_fd_from_socket(intptr_t raw_fd, char* errbuf, int errlen) {
    if (raw_fd < 0 || raw_fd > INT_MAX) {
        if (raw_fd >= 0) moo_closesock((moo_sockfd_t)raw_fd);
        snprintf(errbuf, errlen, "Socket-Handle passt nicht in OpenSSL-fd");
        return -1;
    }
    return (int)raw_fd;
}

static void* ossl_verbinde(const char* host, int port, int timeout_ms, char* errbuf, int errlen) {
    intptr_t raw_fd = moo_net_tcp_connect_timeout(host, port, timeout_ms, errbuf, errlen);
    if (raw_fd < 0) return NULL;
    int fd = ossl_fd_from_socket(raw_fd, errbuf, errlen);
    if (fd < 0) return NULL;
    return ossl_wrap_fd(fd, host, errbuf, errlen);
}

static void* ossl_upgrade(intptr_t raw_fd, const char* host, char* errbuf, int errlen) {
    int fd = ossl_fd_from_socket(raw_fd, errbuf, errlen);
    if (fd < 0) return NULL;
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
