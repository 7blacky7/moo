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
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

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
    moo_sockfd_t             fd;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context  entropy;
    mbedtls_x509_crt         cacert;
} MbedConn;

#ifndef _WIN32
/* System-CA-Bundle-Kandidaten (Distro-abhaengig). */
static const char* MBED_CA_PATHS[] = {
    "/etc/ssl/certs/ca-certificates.crt",  /* Debian/Arch/CachyOS */
    "/etc/pki/tls/certs/ca-bundle.crt",    /* Fedora/RHEL */
    "/etc/ssl/cert.pem",                   /* Alpine/BSD/macOS */
    NULL
};
#endif

#ifdef _WIN32
static int mbed_load_windows_root_store(mbedtls_x509_crt* chain, DWORD location) {
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_A,
                                    X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                    0,
                                    location | CERT_STORE_READONLY_FLAG,
                                    "ROOT");
    if (!store) return 0;
    int parsed = 0;
    PCCERT_CONTEXT cert = NULL;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != NULL) {
        int ret = mbedtls_x509_crt_parse_der(chain, cert->pbCertEncoded,
                                             (size_t)cert->cbCertEncoded);
        if (ret >= 0) parsed++;
    }
    CertCloseStore(store, 0);
    return parsed;
}
#endif

static int mbed_load_system_cas(mbedtls_x509_crt* chain, char* errbuf, int errlen) {
#ifdef _WIN32
    int parsed = 0;
    parsed += mbed_load_windows_root_store(chain, CERT_SYSTEM_STORE_CURRENT_USER);
    parsed += mbed_load_windows_root_store(chain, CERT_SYSTEM_STORE_LOCAL_MACHINE);
    if (parsed <= 0) {
        snprintf(errbuf, errlen, "Windows-ROOT-Zertifikatsspeicher ist leer oder nicht lesbar");
        return -1;
    }
    return 0;
#else
    for (int i = 0; MBED_CA_PATHS[i]; i++) {
        if (mbedtls_x509_crt_parse_file(chain, MBED_CA_PATHS[i]) >= 0) return 0;
    }
    snprintf(errbuf, errlen, "System-CA-Store nicht gefunden");
    return -1;
#endif
}

static void mbed_free(MbedConn* c) {
    if (!c) return;
    mbedtls_ssl_free(&c->ssl);
    mbedtls_ssl_config_free(&c->conf);
    mbedtls_x509_crt_free(&c->cacert);
    mbedtls_ctr_drbg_free(&c->drbg);
    mbedtls_entropy_free(&c->entropy);
    if (!MOO_SOCK_BAD(c->fd)) {
        moo_closesock(c->fd);
        c->fd = MOO_INVALID_SOCK;
    }
    free(c);
}

static void mbed_err(char* errbuf, int errlen, const char* msg, int ret) {
    char es[128];
    mbedtls_strerror(ret, es, sizeof es);
    snprintf(errbuf, errlen, "%s (mbedtls -0x%04x: %s)", msg, (unsigned)(-ret), es);
}

static int mbed_sock_send(void* ctx, const unsigned char* buf, size_t len) {
    moo_sockfd_t fd = *(moo_sockfd_t*)ctx;
    int part = len > (size_t)INT_MAX ? INT_MAX : (int)len;
    moo_ssize_t n = send(fd, (const char*)buf, part, 0);
    if (n >= 0) return (int)n;
#ifdef _WIN32
    int e = WSAGetLastError();
    if (e == WSAEINTR || e == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
#else
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
#endif
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int mbed_sock_recv(void* ctx, unsigned char* buf, size_t len) {
    moo_sockfd_t fd = *(moo_sockfd_t*)ctx;
    int part = len > (size_t)INT_MAX ? INT_MAX : (int)len;
    moo_ssize_t n = recv(fd, (char*)buf, part, 0);
    if (n >= 0) return (int)n;
#ifdef _WIN32
    int e = WSAGetLastError();
    if (e == WSAEINTR || e == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
#else
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
#endif
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

/* Wickelt einen bereits verbundenen fd in TLS: CA-/Hostname-Verifikation, SNI,
 * Handshake. Uebernimmt den fd (mbedtls_net_free schliesst ihn). */
static void* mbed_wrap_fd(intptr_t raw_fd, const char* host, char* errbuf, int errlen) {
    if (raw_fd < 0) { snprintf(errbuf, errlen, "ungueltiger Socket"); return NULL; }
    MbedConn* c = (MbedConn*)calloc(1, sizeof(MbedConn));
    if (!c) {
        snprintf(errbuf, errlen, "malloc fehlgeschlagen");
        moo_closesock((moo_sockfd_t)raw_fd);
        return NULL;
    }
    c->fd = (moo_sockfd_t)raw_fd;
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_ctr_drbg_init(&c->drbg);
    mbedtls_entropy_init(&c->entropy);
    mbedtls_x509_crt_init(&c->cacert);

    int ret;
    const char* pers = "moo_tls_client";
    if ((ret = mbedtls_ctr_drbg_seed(&c->drbg, mbedtls_entropy_func, &c->entropy,
                                     (const unsigned char*)pers, strlen(pers))) != 0) {
        mbed_err(errbuf, errlen, "CTR_DRBG-Seed fehlgeschlagen", ret); mbed_free(c); return NULL;
    }

    if (mbed_load_system_cas(&c->cacert, errbuf, errlen) != 0) {
        mbed_free(c);
        return NULL;
    }

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
    mbedtls_ssl_set_bio(&c->ssl, &c->fd, mbed_sock_send, mbed_sock_recv, NULL);

    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            mbed_err(errbuf, errlen, "TLS-Handshake/Zertifikat-Verifikation fehlgeschlagen", ret);
            mbed_free(c); return NULL;
        }
    }
    return c;
}

static void* mbed_verbinde(const char* host, int port, int timeout_ms, char* errbuf, int errlen) {
    intptr_t raw_fd = moo_net_tcp_connect_timeout(host, port, timeout_ms, errbuf, errlen);
    if (raw_fd < 0) return NULL;
    return mbed_wrap_fd(raw_fd, host, errbuf, errlen);
}

static void* mbed_upgrade(intptr_t raw_fd, const char* host, char* errbuf, int errlen) {
    return mbed_wrap_fd(raw_fd, host, errbuf, errlen);
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
