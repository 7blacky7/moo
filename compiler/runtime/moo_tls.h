#ifndef MOO_TLS_H
#define MOO_TLS_H
/*
 * moo_tls.h — Backend-Vertrag fuer den TLS-Client von moo.
 *
 * Dual-Path (siehe Memory moo-krypto-tls-dual-path-architektur): jeder Pfad
 * — OpenSSL nativ (Linux), vendored mbedTLS (self-contained/MoOS), spaeter
 * Windows SChannel — implementiert GENAU diese vtable. Die Moo-Builtins
 * (moo_tls_*) sind backend-agnostisch: sie halten eine Handle-Tabelle und
 * rufen ausschliesslich ueber moo_tls_backend().
 */
#include "moo_runtime.h"

typedef struct MooTlsBackend {
    const char* name;
    /* Baut TCP + TLS zu host:port auf. timeout_ms > 0 begrenzt den TCP-Connect
     * (0 = blockierend/System-Default). Zertifikat-Verifikation gegen den
     * System-CA-Store ist DEFAULT AN (inkl. Hostname-Check). Rueckgabe:
     * opaker Verbindungszeiger oder NULL bei Fehler (errbuf gesetzt). */
    void* (*verbinde)(const char* host, int port, int timeout_ms, char* errbuf, int errlen);
    /* Schreibt bis len Bytes. Rueckgabe: geschriebene Bytes, <=0 Fehler. */
    int   (*schreibe)(void* conn, const char* buf, int len);
    /* Liest bis max Bytes. Rueckgabe: gelesene Bytes, 0 EOF, <0 Fehler. */
    int   (*lese)(void* conn, char* buf, int max);
    /* Schliesst Verbindung + gibt alle Ressourcen frei. */
    void  (*schliesse)(void* conn);
    /* STARTTLS: wickelt einen bereits verbundenen TCP-fd in TLS (SNI +
     * CA-/Hostname-Verifikation wie verbinde). Uebernimmt den fd. Rueckgabe:
     * opaker Verbindungszeiger oder NULL bei Fehler (errbuf gesetzt). */
    void* (*upgrade)(int fd, const char* host, char* errbuf, int errlen);
} MooTlsBackend;

/* Aktiver Backend (Build-/Laufzeit-Auswahl via MOO_TLS_BACKEND). */
const MooTlsBackend* moo_tls_backend(void);

/* Moo-Builtins (backend-agnostisch; Handle = Integer-Slot als MOO_NUMBER). */
MooValue moo_tls_connect(MooValue host, MooValue port);              /* -> Handle */
MooValue moo_tls_connect_timeout(MooValue host, MooValue port, MooValue timeout_ms); /* -> Handle */
MooValue moo_tls_send(MooValue handle, MooValue data);              /* -> geschriebene Bytes */
MooValue moo_tls_recv(MooValue handle, MooValue max_bytes);         /* -> String (binaersafe) */
MooValue moo_tls_close(MooValue handle);                            /* -> none */
MooValue moo_tls_starttls(MooValue tcp_handle, MooValue host);      /* -> TLS-Handle */

#endif /* MOO_TLS_H */
