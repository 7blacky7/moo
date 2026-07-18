/*
 * moo_tls.c — Backend-agnostische Moo-Builtins fuer den TLS-Client.
 * Aus moo_tls_openssl.c herausgezogen, damit OpenSSL- UND mbedTLS-Backend
 * (Dual-Path, siehe Memory moo-krypto-tls-dual-path-architektur) dieselbe
 * Builtin-Schicht teilen. Genau EIN Backend liefert moo_tls_backend()
 * (Build-Flag MOO_TLS_BACKEND in build.rs). Diese Datei wird IMMER gebaut.
 *
 * Handle-Modell: Integer-Slot (MOO_NUMBER) in eine statische Tabelle —
 * vermeidet einen neuen MooValue-Tag. tls_schliesse gibt den Slot frei;
 * nicht geschlossene Verbindungen lecken bewusst (expliziter Close-Vertrag).
 */
#include "moo_runtime.h"
#include "moo_tls.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern MooValue moo_string_new(const char* s);
extern MooValue moo_string_new_len(const char* chars, int32_t len);
extern MooValue moo_number(double n);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

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
