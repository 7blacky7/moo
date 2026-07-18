/*
 * moo_tls_schannel.c — nativer Windows-TLS-Client fuer moo.
 *
 * Erfuellt nach vollstaendiger Verdrahtung denselben MooTlsBackend-Vertrag
 * wie OpenSSL und vendored mbedTLS. Zertifikat und Hostname werden explizit
 * gegen die Windows-Zertifikatkette mit CERT_CHAIN_POLICY_SSL geprueft.
 */
#ifdef _WIN32

#define SECURITY_WIN32
#include "moo_runtime.h"
#include "moo_tls.h"
#include "moo_net_compat.h"

#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

extern intptr_t moo_net_tcp_connect_timeout(const char* host, int port, int timeout_ms,
                                            char* errbuf, int errlen);

#define SCH_HANDSHAKE_LIMIT (1024u * 1024u)
#define SCH_IO_CHUNK 16384u

typedef struct {
    moo_sockfd_t fd;
    CredHandle cred;
    CtxtHandle ctxt;
    bool cred_valid;
    bool ctxt_valid;
    SecPkgContext_StreamSizes sizes;
    unsigned char* enc;
    size_t enc_len;
    size_t enc_cap;
    unsigned char* plain;
    size_t plain_off;
    size_t plain_len;
    size_t plain_cap;
} SchConn;

static void sch_set_error(char* errbuf, int errlen, const char* text) {
    if (errbuf && errlen > 0) snprintf(errbuf, (size_t)errlen, "%s", text);
}

static void sch_set_status_error(char* errbuf, int errlen, const char* text,
                                 SECURITY_STATUS status) {
    if (errbuf && errlen > 0) {
        snprintf(errbuf, (size_t)errlen, "%s (SSPI 0x%08lx)",
                 text, (unsigned long)status);
    }
}

static void sch_set_win_error(char* errbuf, int errlen, const char* text, DWORD error) {
    if (errbuf && errlen > 0) {
        snprintf(errbuf, (size_t)errlen, "%s (Win32 0x%08lx)",
                 text, (unsigned long)error);
    }
}

static bool sch_reserve(unsigned char** data, size_t* cap, size_t needed,
                        size_t limit, char* errbuf, int errlen) {
    if (needed > limit) {
        sch_set_error(errbuf, errlen, "SChannel-Pufferlimit ueberschritten");
        return false;
    }
    if (*cap >= needed) return true;
    size_t next = *cap ? *cap : SCH_IO_CHUNK;
    while (next < needed) {
        if (next > limit / 2u) { next = limit; break; }
        next *= 2u;
    }
    unsigned char* grown = (unsigned char*)realloc(*data, next);
    if (!grown) {
        sch_set_error(errbuf, errlen, "SChannel-Speicherreservierung fehlgeschlagen");
        return false;
    }
    *data = grown;
    *cap = next;
    return true;
}

static bool sch_send_all(moo_sockfd_t fd, const unsigned char* data, size_t len,
                         char* errbuf, int errlen) {
    size_t off = 0;
    while (off < len) {
        size_t rest = len - off;
        int part = rest > (size_t)INT_MAX ? INT_MAX : (int)rest;
        int sent = send(fd, (const char*)data + off, part, 0);
        if (sent > 0) { off += (size_t)sent; continue; }
        if (sent == 0) {
            sch_set_error(errbuf, errlen, "SChannel-Socket wurde beim Schreiben geschlossen");
            return false;
        }
        int e = WSAGetLastError();
        if (e == WSAEINTR) continue;
        sch_set_win_error(errbuf, errlen, "SChannel-Socket-Schreiben fehlgeschlagen", (DWORD)e);
        return false;
    }
    return true;
}

static bool sch_recv_more(SchConn* c, size_t limit, char* errbuf, int errlen) {
    if (!sch_reserve(&c->enc, &c->enc_cap, c->enc_len + SCH_IO_CHUNK,
                     limit, errbuf, errlen)) return false;
    size_t room = c->enc_cap - c->enc_len;
    int part = room > (size_t)INT_MAX ? INT_MAX : (int)room;
    int got;
    do {
        got = recv(c->fd, (char*)c->enc + c->enc_len, part, 0);
    } while (got < 0 && WSAGetLastError() == WSAEINTR);
    if (got <= 0) {
        if (got == 0) sch_set_error(errbuf, errlen, "SChannel-Peer schloss die Verbindung");
        else sch_set_win_error(errbuf, errlen, "SChannel-Socket-Lesen fehlgeschlagen",
                               (DWORD)WSAGetLastError());
        return false;
    }
    c->enc_len += (size_t)got;
    return true;
}

static void sch_conn_destroy(SchConn* c) {
    if (!c) return;
    if (c->ctxt_valid) {
        DeleteSecurityContext(&c->ctxt);
        c->ctxt_valid = false;
    }
    if (c->cred_valid) {
        FreeCredentialsHandle(&c->cred);
        c->cred_valid = false;
    }
    if (!MOO_SOCK_BAD(c->fd)) {
        moo_closesock(c->fd);
        c->fd = MOO_INVALID_SOCK;
    }
    free(c->enc);
    free(c->plain);
    free(c);
}

static bool sch_acquire_credentials(SchConn* c, char* errbuf, int errlen) {
    SCHANNEL_CRED options;
    memset(&options, 0, sizeof options);
    options.dwVersion = SCHANNEL_CRED_VERSION;
    options.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION |
                      SCH_CRED_NO_DEFAULT_CREDS |
                      SCH_USE_STRONG_CRYPTO;

    TimeStamp expiry;
    SECURITY_STATUS status = AcquireCredentialsHandleA(
        NULL, UNISP_NAME_A, SECPKG_CRED_OUTBOUND, NULL, &options,
        NULL, NULL, &c->cred, &expiry);
    if (status != SEC_E_OK) {
        sch_set_status_error(errbuf, errlen, "SChannel-Credentials konnten nicht erstellt werden", status);
        return false;
    }
    c->cred_valid = true;
    return true;
}

static wchar_t* sch_host_wide(const char* host, char* errbuf, int errlen) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host, -1, NULL, 0);
    if (n <= 0) {
        sch_set_win_error(errbuf, errlen, "TLS-Hostname ist kein gueltiges UTF-8",
                          GetLastError());
        return NULL;
    }
    wchar_t* wide = (wchar_t*)calloc((size_t)n, sizeof(wchar_t));
    if (!wide) {
        sch_set_error(errbuf, errlen, "TLS-Hostname-Speicherreservierung fehlgeschlagen");
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host, -1, wide, n) <= 0) {
        sch_set_win_error(errbuf, errlen, "TLS-Hostname-Konvertierung fehlgeschlagen",
                          GetLastError());
        free(wide);
        return NULL;
    }
    return wide;
}

static bool sch_verify_peer(SchConn* c, const char* host, char* errbuf, int errlen) {
    PCCERT_CONTEXT remote = NULL;
    SECURITY_STATUS status = QueryContextAttributesA(
        &c->ctxt, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &remote);
    if (status != SEC_E_OK || !remote) {
        sch_set_status_error(errbuf, errlen, "SChannel lieferte kein Peer-Zertifikat", status);
        return false;
    }

    CERT_CHAIN_PARA chain_para;
    memset(&chain_para, 0, sizeof chain_para);
    chain_para.cbSize = sizeof chain_para;
    PCCERT_CHAIN_CONTEXT chain = NULL;
    BOOL chain_ok = CertGetCertificateChain(
        NULL, remote, NULL, remote->hCertStore, &chain_para, 0, NULL, &chain);
    if (!chain_ok || !chain) {
        DWORD e = GetLastError();
        CertFreeCertificateContext(remote);
        sch_set_win_error(errbuf, errlen, "Windows-Zertifikatkette konnte nicht erstellt werden", e);
        return false;
    }

    wchar_t* wide_host = sch_host_wide(host, errbuf, errlen);
    if (!wide_host) {
        CertFreeCertificateChain(chain);
        CertFreeCertificateContext(remote);
        return false;
    }

    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_para;
    memset(&ssl_para, 0, sizeof ssl_para);
    ssl_para.cbSize = sizeof ssl_para;
    ssl_para.dwAuthType = AUTHTYPE_SERVER;
    ssl_para.pwszServerName = wide_host;

    CERT_CHAIN_POLICY_PARA policy_para;
    memset(&policy_para, 0, sizeof policy_para);
    policy_para.cbSize = sizeof policy_para;
    policy_para.pvExtraPolicyPara = &ssl_para;

    CERT_CHAIN_POLICY_STATUS policy_status;
    memset(&policy_status, 0, sizeof policy_status);
    policy_status.cbSize = sizeof policy_status;

    BOOL policy_ok = CertVerifyCertificateChainPolicy(
        CERT_CHAIN_POLICY_SSL, chain, &policy_para, &policy_status);
    bool verified = policy_ok && policy_status.dwError == 0;
    if (!verified) {
        DWORD e = policy_ok ? policy_status.dwError : GetLastError();
        sch_set_win_error(errbuf, errlen,
                          "TLS-Zertifikat oder Hostname wurde von Windows abgelehnt", e);
    }

    free(wide_host);
    CertFreeCertificateChain(chain);
    CertFreeCertificateContext(remote);
    return verified;
}

static bool sch_send_token(SchConn* c, SecBuffer* token, char* errbuf, int errlen) {
    if (!token->pvBuffer || token->cbBuffer == 0) return true;
    bool ok = sch_send_all(c->fd, (const unsigned char*)token->pvBuffer,
                           (size_t)token->cbBuffer, errbuf, errlen);
    FreeContextBuffer(token->pvBuffer);
    token->pvBuffer = NULL;
    token->cbBuffer = 0;
    return ok;
}

static void sch_keep_extra(SchConn* c, SecBuffer* extra_buffer, size_t input_len) {
    if (extra_buffer->BufferType == SECBUFFER_EXTRA && extra_buffer->cbBuffer > 0) {
        size_t extra = (size_t)extra_buffer->cbBuffer;
        if (extra <= input_len) {
            memmove(c->enc, c->enc + input_len - extra, extra);
            c->enc_len = extra;
            return;
        }
    }
    c->enc_len = 0;
}

static bool sch_complete_status(SchConn* c, SECURITY_STATUS* status,
                                SecBufferDesc* output_desc,
                                char* errbuf, int errlen) {
    if (*status != SEC_I_COMPLETE_NEEDED && *status != SEC_I_COMPLETE_AND_CONTINUE) {
        return true;
    }
    SECURITY_STATUS complete = CompleteAuthToken(&c->ctxt, output_desc);
    if (complete != SEC_E_OK) {
        sch_set_status_error(errbuf, errlen, "SChannel-Handshake-Token konnte nicht vervollstaendigt werden", complete);
        return false;
    }
    *status = (*status == SEC_I_COMPLETE_AND_CONTINUE)
        ? SEC_I_CONTINUE_NEEDED : SEC_E_OK;
    return true;
}

static bool sch_handshake(SchConn* c, const char* host, char* errbuf, int errlen) {
    if (!sch_acquire_credentials(c, errbuf, errlen)) return false;

    ULONG requested = ISC_REQ_SEQUENCE_DETECT |
                            ISC_REQ_REPLAY_DETECT |
                            ISC_REQ_CONFIDENTIALITY |
                            ISC_REQ_EXTENDED_ERROR |
                            ISC_REQ_ALLOCATE_MEMORY |
                            ISC_REQ_STREAM;
    ULONG attributes = 0;
    TimeStamp expiry;
    SecBuffer output_buffer;
    SecBufferDesc output_desc;
    memset(&output_buffer, 0, sizeof output_buffer);
    output_buffer.BufferType = SECBUFFER_TOKEN;
    output_desc.ulVersion = SECBUFFER_VERSION;
    output_desc.cBuffers = 1;
    output_desc.pBuffers = &output_buffer;

    SECURITY_STATUS status = InitializeSecurityContextA(
        &c->cred, NULL, (SEC_CHAR*)host, requested, 0, SECURITY_NATIVE_DREP,
        NULL, 0, &c->ctxt, &output_desc, &attributes, &expiry);
    if (status >= 0) c->ctxt_valid = true;
    if (!sch_complete_status(c, &status, &output_desc, errbuf, errlen)) {
        if (output_buffer.pvBuffer) FreeContextBuffer(output_buffer.pvBuffer);
        return false;
    }
    if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK) {
        if (output_buffer.pvBuffer) FreeContextBuffer(output_buffer.pvBuffer);
        sch_set_status_error(errbuf, errlen, "SChannel-Handshake konnte nicht gestartet werden", status);
        return false;
    }
    if (!sch_send_token(c, &output_buffer, errbuf, errlen)) return false;

    bool read_needed = true;
    while (status == SEC_I_CONTINUE_NEEDED) {
        if (read_needed && !sch_recv_more(c, SCH_HANDSHAKE_LIMIT, errbuf, errlen)) return false;

        SecBuffer input_buffers[2];
        SecBufferDesc input_desc;
        memset(input_buffers, 0, sizeof input_buffers);
        input_buffers[0].BufferType = SECBUFFER_TOKEN;
        input_buffers[0].pvBuffer = c->enc;
        input_buffers[0].cbBuffer = (unsigned long)c->enc_len;
        input_buffers[1].BufferType = SECBUFFER_EMPTY;
        input_desc.ulVersion = SECBUFFER_VERSION;
        input_desc.cBuffers = 2;
        input_desc.pBuffers = input_buffers;

        memset(&output_buffer, 0, sizeof output_buffer);
        output_buffer.BufferType = SECBUFFER_TOKEN;
        size_t input_len = c->enc_len;
        status = InitializeSecurityContextA(
            &c->cred, &c->ctxt, (SEC_CHAR*)host, requested, 0,
            SECURITY_NATIVE_DREP, &input_desc, 0, NULL,
            &output_desc, &attributes, &expiry);

        if (!sch_complete_status(c, &status, &output_desc, errbuf, errlen)) {
            if (output_buffer.pvBuffer) FreeContextBuffer(output_buffer.pvBuffer);
            return false;
        }
        if (!sch_send_token(c, &output_buffer, errbuf, errlen)) return false;
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            read_needed = true;
            continue;
        }
        if (status == SEC_I_INCOMPLETE_CREDENTIALS &&
            !(requested & ISC_REQ_USE_SUPPLIED_CREDS)) {
            requested |= ISC_REQ_USE_SUPPLIED_CREDS;
            status = SEC_I_CONTINUE_NEEDED;
            read_needed = false;
            continue;
        }
        if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK) {
            sch_set_status_error(errbuf, errlen, "SChannel-TLS-Handshake fehlgeschlagen", status);
            return false;
        }
        sch_keep_extra(c, &input_buffers[1], input_len);
        read_needed = c->enc_len == 0;
    }

    status = QueryContextAttributesA(&c->ctxt, SECPKG_ATTR_STREAM_SIZES, &c->sizes);
    if (status != SEC_E_OK || c->sizes.cbMaximumMessage == 0) {
        sch_set_status_error(errbuf, errlen, "SChannel-Streamgroessen nicht verfuegbar", status);
        return false;
    }
    return sch_verify_peer(c, host, errbuf, errlen);
}

static void* sch_wrap_fd(intptr_t raw_fd, const char* host, char* errbuf, int errlen) {
    if (raw_fd < 0 || !host || !host[0]) {
        sch_set_error(errbuf, errlen, "SChannel erhielt keinen gueltigen Socket oder Hostnamen");
        if (raw_fd >= 0) moo_closesock((moo_sockfd_t)raw_fd);
        return NULL;
    }
    SchConn* c = (SchConn*)calloc(1, sizeof(SchConn));
    if (!c) {
        moo_closesock((moo_sockfd_t)raw_fd);
        sch_set_error(errbuf, errlen, "SChannel-Verbindung konnte nicht reserviert werden");
        return NULL;
    }
    c->fd = (moo_sockfd_t)raw_fd;
    SecInvalidateHandle(&c->cred);
    SecInvalidateHandle(&c->ctxt);
    if (!sch_handshake(c, host, errbuf, errlen)) {
        sch_conn_destroy(c);
        return NULL;
    }
    return c;
}

static void* sch_verbinde(const char* host, int port, int timeout_ms,
                          char* errbuf, int errlen) {
    intptr_t raw_fd = moo_net_tcp_connect_timeout(host, port, timeout_ms, errbuf, errlen);
    if (raw_fd < 0) return NULL;
    return sch_wrap_fd(raw_fd, host, errbuf, errlen);
}

static void* sch_upgrade(intptr_t raw_fd, const char* host, char* errbuf, int errlen) {
    return sch_wrap_fd(raw_fd, host, errbuf, errlen);
}

static int sch_schreibe(void* conn, const char* buf, int len) {
    SchConn* c = (SchConn*)conn;
    if (!c || !c->ctxt_valid || !buf || len < 0) return -1;
    if (len == 0) return 0;

    size_t off = 0;
    while (off < (size_t)len) {
        size_t rest = (size_t)len - off;
        size_t chunk = rest;
        if (chunk > (size_t)c->sizes.cbMaximumMessage) {
            chunk = (size_t)c->sizes.cbMaximumMessage;
        }
        size_t total = (size_t)c->sizes.cbHeader + chunk +
                       (size_t)c->sizes.cbTrailer;
        unsigned char* packet = (unsigned char*)malloc(total);
        if (!packet) return -1;
        memcpy(packet + c->sizes.cbHeader, buf + off, chunk);

        SecBuffer buffers[4];
        SecBufferDesc desc;
        memset(buffers, 0, sizeof buffers);
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].pvBuffer = packet;
        buffers[0].cbBuffer = c->sizes.cbHeader;
        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].pvBuffer = packet + c->sizes.cbHeader;
        buffers[1].cbBuffer = (unsigned long)chunk;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].pvBuffer = packet + c->sizes.cbHeader + chunk;
        buffers[2].cbBuffer = c->sizes.cbTrailer;
        buffers[3].BufferType = SECBUFFER_EMPTY;
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = buffers;

        SECURITY_STATUS status = EncryptMessage(&c->ctxt, 0, &desc, 0);
        if (status != SEC_E_OK) {
            free(packet);
            return -1;
        }
        bool ok = sch_send_all(c->fd,
                               (const unsigned char*)buffers[0].pvBuffer,
                               (size_t)buffers[0].cbBuffer, NULL, 0) &&
                  sch_send_all(c->fd,
                               (const unsigned char*)buffers[1].pvBuffer,
                               (size_t)buffers[1].cbBuffer, NULL, 0) &&
                  sch_send_all(c->fd,
                               (const unsigned char*)buffers[2].pvBuffer,
                               (size_t)buffers[2].cbBuffer, NULL, 0);
        free(packet);
        if (!ok) return -1;
        off += chunk;
    }
    return (int)off;
}

static bool sch_store_plain(SchConn* c, const unsigned char* data, size_t len) {
    if (len == 0) {
        c->plain_off = 0;
        c->plain_len = 0;
        return true;
    }
    if (len > SCH_HANDSHAKE_LIMIT) return false;
    if (c->plain_cap < len) {
        unsigned char* grown = (unsigned char*)realloc(c->plain, len);
        if (!grown) return false;
        c->plain = grown;
        c->plain_cap = len;
    }
    memcpy(c->plain, data, len);
    c->plain_off = 0;
    c->plain_len = len;
    return true;
}

static int sch_copy_plain(SchConn* c, char* out, int max) {
    if (c->plain_off >= c->plain_len || max <= 0) return 0;
    size_t available = c->plain_len - c->plain_off;
    size_t take = available < (size_t)max ? available : (size_t)max;
    memcpy(out, c->plain + c->plain_off, take);
    c->plain_off += take;
    if (c->plain_off == c->plain_len) {
        c->plain_off = 0;
        c->plain_len = 0;
    }
    return (int)take;
}

static int sch_lese(void* conn, char* out, int max) {
    SchConn* c = (SchConn*)conn;
    if (!c || !c->ctxt_valid || !out || max <= 0) return -1;

    int ready = sch_copy_plain(c, out, max);
    if (ready > 0) return ready;

    for (;;) {
        if (c->enc_len == 0 && !sch_recv_more(c, SCH_HANDSHAKE_LIMIT, NULL, 0)) {
            return 0;
        }

        SecBuffer buffers[4];
        SecBufferDesc desc;
        memset(buffers, 0, sizeof buffers);
        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].pvBuffer = c->enc;
        buffers[0].cbBuffer = (unsigned long)c->enc_len;
        buffers[1].BufferType = SECBUFFER_EMPTY;
        buffers[2].BufferType = SECBUFFER_EMPTY;
        buffers[3].BufferType = SECBUFFER_EMPTY;
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = buffers;

        size_t input_len = c->enc_len;
        SECURITY_STATUS status = DecryptMessage(&c->ctxt, &desc, 0, NULL);
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            if (!sch_recv_more(c, SCH_HANDSHAKE_LIMIT, NULL, 0)) return 0;
            continue;
        }
        if (status == SEC_I_CONTEXT_EXPIRED) return 0;
        if (status == SEC_I_RENEGOTIATE) return -1;
        if (status != SEC_E_OK) return -1;

        const unsigned char* decrypted = NULL;
        size_t decrypted_len = 0;
        size_t extra = 0;
        for (int i = 0; i < 4; i++) {
            if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer > 0) {
                decrypted = (const unsigned char*)buffers[i].pvBuffer;
                decrypted_len = (size_t)buffers[i].cbBuffer;
            } else if (buffers[i].BufferType == SECBUFFER_EXTRA) {
                extra = (size_t)buffers[i].cbBuffer;
            }
        }
        if (decrypted_len > 0 && !sch_store_plain(c, decrypted, decrypted_len)) return -1;

        if (extra > 0 && extra <= input_len) {
            memmove(c->enc, c->enc + input_len - extra, extra);
            c->enc_len = extra;
        } else {
            c->enc_len = 0;
        }

        ready = sch_copy_plain(c, out, max);
        if (ready > 0) return ready;
    }
}

static void sch_send_shutdown(SchConn* c) {
    if (!c || !c->ctxt_valid || !c->cred_valid) return;
    DWORD shutdown = SCHANNEL_SHUTDOWN;
    SecBuffer control_buffer;
    SecBufferDesc control_desc;
    memset(&control_buffer, 0, sizeof control_buffer);
    control_buffer.BufferType = SECBUFFER_TOKEN;
    control_buffer.pvBuffer = &shutdown;
    control_buffer.cbBuffer = sizeof shutdown;
    control_desc.ulVersion = SECBUFFER_VERSION;
    control_desc.cBuffers = 1;
    control_desc.pBuffers = &control_buffer;
    if (ApplyControlToken(&c->ctxt, &control_desc) != SEC_E_OK) return;

    SecBuffer output_buffer;
    SecBufferDesc output_desc;
    memset(&output_buffer, 0, sizeof output_buffer);
    output_buffer.BufferType = SECBUFFER_TOKEN;
    output_desc.ulVersion = SECBUFFER_VERSION;
    output_desc.cBuffers = 1;
    output_desc.pBuffers = &output_buffer;
    ULONG attributes = 0;
    TimeStamp expiry;
    const ULONG requested = ISC_REQ_SEQUENCE_DETECT |
                            ISC_REQ_REPLAY_DETECT |
                            ISC_REQ_CONFIDENTIALITY |
                            ISC_REQ_ALLOCATE_MEMORY |
                            ISC_REQ_STREAM;
    SECURITY_STATUS status = InitializeSecurityContextA(
        &c->cred, &c->ctxt, NULL, requested, 0, SECURITY_NATIVE_DREP,
        NULL, 0, NULL, &output_desc, &attributes, &expiry);
    if ((status == SEC_E_OK || status == SEC_I_CONTEXT_EXPIRED) &&
        output_buffer.pvBuffer && output_buffer.cbBuffer > 0) {
        (void)sch_send_all(c->fd, (const unsigned char*)output_buffer.pvBuffer,
                           (size_t)output_buffer.cbBuffer, NULL, 0);
    }
    if (output_buffer.pvBuffer) FreeContextBuffer(output_buffer.pvBuffer);
}

static void sch_schliesse(void* conn) {
    SchConn* c = (SchConn*)conn;
    if (!c) return;
    sch_send_shutdown(c);
    sch_conn_destroy(c);
}

static const MooTlsBackend BACKEND = {
    "schannel",
    sch_verbinde,
    sch_schreibe,
    sch_lese,
    sch_schliesse,
    sch_upgrade
};

const MooTlsBackend* moo_tls_backend(void) {
    return &BACKEND;
}

#endif /* _WIN32 */
