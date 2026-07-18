/*
 * moo_tls_mbedtls.c — TLS-Backend fuer moo, self-contained-Pfad (mbedTLS).
 * Erfuellt den Vertrag moo_tls.h (nur Schicht 1). Die backend-agnostischen
 * Builtins liegen in moo_tls.c (gemeinsam mit dem OpenSSL-Backend).
 *
 * Dual-Path (Memory moo-krypto-tls-dual-path-architektur): der "ui_moo"-Weg
 * — portabel/MoOS-faehig. Aktiv via Build-Flag MOO_TLS_BACKEND=mbedtls.
 * Vendored mbedTLS-Quellen unter runtime/mbedtls/ (kein System-libmbedtls).
 */
#include "moo_runtime.h"
#include "moo_tls.h"
#include "moo_net_compat.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>

/* Shared TCP-Connect (DNS + optionaler Connect-Timeout) aus moo_net.c. */
extern intptr_t moo_net_tcp_connect_timeout(const char* host, int port, int timeout_ms,
                                            char* errbuf, int errlen);

typedef struct {
    mbedtls_net_context      net;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context  entropy;
    mbedtls_x509_crt         cacert;
} MbedConn;

/* System-CA-Bundle-Kandidaten (Distro-abhaengig). */
static const char* MBED_CA_PATHS[] = {
    "/etc/ssl/certs/ca-certificates.crt",  /* Debian/Arch/CachyOS */
    "/etc/pki/tls/certs/ca-bundle.crt",    /* Fedora/RHEL */
    "/etc/ssl/cert.pem",                   /* Alpine/BSD/macOS */
    NULL
};

static void mbed_free(MbedConn* c) {
    if (!c) return;
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_x509_crt_free(&c->cacert);
    mbedtls_ctr_drbg_free(&c->drbg);
    mbedtls_entropy_free(&c->entropy);
    mbedtls_net_free(&c->net);
    free(c);
}

static void mbed_err(char* errbuf, int errlen, const char* msg, int ret) {
    char es[128];
    mbedtls_strerror(ret, es, sizeof es);
    snprintf(errbuf, errlen, "%s (mbedtls -0x%04x: %s)", msg, (unsigned)(-ret), es);
}

/* Wickelt einen bereits verbundenen fd in TLS: CA-/Hostname-Verifikation, SNI,
 * Handshake. Uebernimmt den fd (mbedtls_net_free schliesst ihn). */
static void* mbed_wrap_fd(int fd, const char* host, char* errbuf, int errlen) {
    MbedConn* c = (MbedConn*)calloc(1, sizeof(MbedConn));
    if (!c) { snprintf(errbuf, errlen, "malloc fehlgeschlagen"); close(fd); return NULL; }
    mbedtls_net_init(&c->net);
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_ctr_drbg_init(&c->drbg);
    mbedtls_entropy_init(&c->entropy);
    mbedtls_x509_crt_init(&c->cacert);
    c->net.fd = fd;   /* uebernimmt den bestehenden Socket */

    int ret;
    const char* pers = "moo_tls_client";
    if ((ret = mbedtls_ctr_drbg_seed(&c->drbg, mbedtls_entropy_func, &c->entropy,
                                     (const unsigned char*)pers, strlen(pers))) != 0) {
        mbed_err(errbuf, errlen, "CTR_DRBG-Seed fehlgeschlagen", ret); mbed_free(c); return NULL;
    }

    int ca_ok = 0;
    for (int i = 0; MBED_CA_PATHS[i]; i++) {
        if (mbedtls_x509_crt_parse_file(&c->cacert, MBED_CA_PATHS[i]) >= 0) { ca_ok = 1; break; }
    }
    if (!ca_ok) { snprintf(errbuf, errlen, "System-CA-Store nicht gefunden"); mbed_free(c); return NULL; }

    if ((ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        mbed_err(errbuf, errlen, "ssl_config_defaults fehlgeschlagen", ret); mbed_free(c); return NULL;
    }
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);  /* Cert-Verifikation an */
    mbedtls_ssl_conf_ca_chain(&c->conf, &c->cacert, NULL);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);
    mbedtls_ssl_conf_min_tls_version(&c->conf, MBEDTLS_SSL_VERSION_TLS1_2);

    if ((ret = mbedtls_ssl_setup(&c->ssl, &c->conf)) != 0) {
        mbed_err(errbuf, errlen, "ssl_setup fehlgeschlagen", ret); mbed_free(c); return NULL;
    }
    if ((ret = mbedtls_ssl_set_hostname(&c->ssl, host)) != 0) {   /* SNI + Hostname-Verifikation */
        mbed_err(errbuf, errlen, "set_hostname fehlgeschlagen", ret); mbed_free(c); return NULL;
    }
    mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, mbedtls_net_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            mbed_err(errbuf, errlen, "TLS-Handshake/Zertifikat-Verifikation fehlgeschlagen", ret);
            mbed_free(c); return NULL;
        }
    }
    return c;
}

static int mbed_fd_from_socket(intptr_t raw_fd, char* errbuf, int errlen) {
    if (raw_fd < 0 || raw_fd > INT_MAX) {
        if (raw_fd >= 0) moo_closesock((moo_sockfd_t)raw_fd);
        snprintf(errbuf, errlen, "Socket-Handle passt nicht in mbedTLS-fd");
        return -1;
    }
    return (int)raw_fd;
}

static void* mbed_verbinde(const char* host, int port, int timeout_ms, char* errbuf, int errlen) {
    intptr_t raw_fd = moo_net_tcp_connect_timeout(host, port, timeout_ms, errbuf, errlen);
    if (raw_fd < 0) return NULL;
    int fd = mbed_fd_from_socket(raw_fd, errbuf, errlen);
    if (fd < 0) return NULL;
    return mbed_wrap_fd(fd, host, errbuf, errlen);
}

static void* mbed_upgrade(intptr_t raw_fd, const char* host, char* errbuf, int errlen) {
    int fd = mbed_fd_from_socket(raw_fd, errbuf, errlen);
    if (fd < 0) return NULL;
    return mbed_wrap_fd(fd, host, errbuf, errlen);
}

static int mbed_schreibe(void* conn, const char* buf, int len) {
    MbedConn* c = (MbedConn*)conn;
    int ret;
    do { ret = mbedtls_ssl_write(&c->ssl, (const unsigned char*)buf, (size_t)len); }
    while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    return ret;
}

static int mbed_lese(void* conn, char* buf, int max) {
    MbedConn* c = (MbedConn*)conn;
    int ret;
    do { ret = mbedtls_ssl_read(&c->ssl, (unsigned char*)buf, (size_t)max); }
    while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    return ret;
}

static void mbed_schliesse(void* conn) {
    MbedConn* c = (MbedConn*)conn;
    if (!c) return;
    mbedtls_ssl_close_notify(&c->ssl);
    mbed_free(c);
}

static const MooTlsBackend MBED_BACKEND = {
    "mbedtls", mbed_verbinde, mbed_schreibe, mbed_lese, mbed_schliesse, mbed_upgrade
};

const MooTlsBackend* moo_tls_backend(void) {
    return &MBED_BACKEND;
}
