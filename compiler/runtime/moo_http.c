#include "moo_runtime.h"
#include "moo_tls.h"
#include "moo_net_compat.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_MAX_URL       8192u
#define HTTP_MAX_RESPONSE  (64u * 1024u * 1024u)
#define HTTP_MAX_HEADERS   (256u * 1024u)
#define HTTP_IO_CHUNK      16384u
#define HTTP_TIMEOUT_MS    30000
#define HTTP_MAX_REDIRECTS 5

/* Shared TCP-Connect aus moo_net.c; DNS + IPv4/IPv6 + Connect-Timeout. */
extern intptr_t moo_net_tcp_connect_timeout(const char* host, int port, int timeout_ms,
                                            char* errbuf, int errlen);
/* JSON-Serializer fuer die bestehende POST-API. */
extern MooValue moo_json_string(MooValue value);

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} HttpBuf;

typedef struct {
    bool secure;
    bool host_is_ipv6;
    int port;
    char host[256];
    char path[HTTP_MAX_URL];
} HttpUrl;

typedef struct {
    bool secure;
    moo_sockfd_t fd;
    void* tls;
    const MooTlsBackend* tls_backend;
} HttpTransport;

typedef struct {
    long status;
    const char* body;
    size_t body_len;
    char* owned_body;
    MooValue headers;
} HttpParsed;

static void http_set_err(char* err, size_t errlen, const char* fmt, ...) {
    if (!err || errlen == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static bool http_buf_init(HttpBuf* b, size_t initial, char* err, size_t errlen) {
    if (initial < 64u) initial = 64u;
    b->data = (char*)malloc(initial);
    if (!b->data) {
        http_set_err(err, errlen, "HTTP: Speicherreservierung fehlgeschlagen");
        b->len = b->cap = 0;
        return false;
    }
    b->len = 0;
    b->cap = initial;
    b->data[0] = '\0';
    return true;
}

static void http_buf_free(HttpBuf* b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static bool http_buf_reserve(HttpBuf* b, size_t extra, size_t limit,
                             char* err, size_t errlen) {
    if (extra > limit || b->len > limit - extra) {
        http_set_err(err, errlen, "HTTP: Groessenlimit von %zu Bytes ueberschritten", limit);
        return false;
    }
    size_t need = b->len + extra + 1u;
    if (need <= b->cap) return true;
    size_t next = b->cap;
    while (next < need) {
        if (next > limit / 2u) { next = limit + 1u; break; }
        next *= 2u;
    }
    if (next > limit + 1u) next = limit + 1u;
    if (next < need) {
        http_set_err(err, errlen, "HTTP: Groessenlimit von %zu Bytes ueberschritten", limit);
        return false;
    }
    char* grown = (char*)realloc(b->data, next);
    if (!grown) {
        http_set_err(err, errlen, "HTTP: Speichervergroesserung fehlgeschlagen");
        return false;
    }
    b->data = grown;
    b->cap = next;
    return true;
}

static bool http_buf_append(HttpBuf* b, const void* data, size_t len, size_t limit,
                            char* err, size_t errlen) {
    if (!http_buf_reserve(b, len, limit, err, errlen)) return false;
    if (len) memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return true;
}

static bool http_buf_text(HttpBuf* b, const char* text, size_t limit,
                          char* err, size_t errlen) {
    return http_buf_append(b, text, strlen(text), limit, err, errlen);
}

static int http_ascii_lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static bool http_ieq_n(const char* a, size_t alen, const char* b) {
    size_t blen = strlen(b);
    if (alen != blen) return false;
    for (size_t i = 0; i < alen; i++) {
        if (http_ascii_lower((unsigned char)a[i]) != http_ascii_lower((unsigned char)b[i])) return false;
    }
    return true;
}

static bool http_starts_ci(const char* text, const char* prefix) {
    while (*prefix) {
        if (!*text || http_ascii_lower((unsigned char)*text) !=
                      http_ascii_lower((unsigned char)*prefix)) return false;
        text++;
        prefix++;
    }
    return true;
}

static bool http_contains_ci(const char* text, size_t len, const char* needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > len) return false;
    for (size_t i = 0; i + nlen <= len; i++) {
        if (http_ieq_n(text + i, nlen, needle)) return true;
    }
    return false;
}

static const char* http_find_bytes(const char* data, size_t len,
                                   const char* needle, size_t nlen) {
    if (nlen == 0 || nlen > len) return NULL;
    for (size_t i = 0; i + nlen <= len; i++) {
        if (memcmp(data + i, needle, nlen) == 0) return data + i;
    }
    return NULL;
}

static bool http_parse_port(const char* text, size_t len, int* out) {
    if (len == 0 || len > 5) return false;
    unsigned value = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10u + (unsigned)(text[i] - '0');
        if (value > 65535u) return false;
    }
    if (value == 0u) return false;
    *out = (int)value;
    return true;
}

static bool http_copy_part(char* dst, size_t dstcap, const char* src, size_t len,
                           const char* label, char* err, size_t errlen) {
    if (len == 0 || len >= dstcap) {
        http_set_err(err, errlen, "HTTP: %s ist leer oder zu lang", label);
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == 0 || c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            http_set_err(err, errlen, "HTTP: ungueltiges Zeichen in %s", label);
            return false;
        }
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return true;
}

static bool http_parse_url(const char* text, HttpUrl* out, char* err, size_t errlen) {
    memset(out, 0, sizeof(*out));
    const char* p;
    if (http_starts_ci(text, "https://")) {
        out->secure = true;
        out->port = 443;
        p = text + 8;
    } else if (http_starts_ci(text, "http://")) {
        out->secure = false;
        out->port = 80;
        p = text + 7;
    } else {
        http_set_err(err, errlen, "HTTP: nur http:// und https:// werden unterstuetzt");
        return false;
    }

    const char* authority = p;
    while (*p && *p != '/' && *p != '?' && *p != '#') p++;
    size_t authority_len = (size_t)(p - authority);
    if (authority_len == 0 || authority_len >= 320u || memchr(authority, '@', authority_len)) {
        http_set_err(err, errlen, "HTTP: ungueltige oder nicht unterstuetzte Authority");
        return false;
    }

    const char* host_start = authority;
    size_t host_len = authority_len;
    if (authority[0] == '[') {
        const char* close = memchr(authority, ']', authority_len);
        if (!close) {
            http_set_err(err, errlen, "HTTP: IPv6-Adresse braucht schliessende ]");
            return false;
        }
        host_start = authority + 1;
        host_len = (size_t)(close - host_start);
        out->host_is_ipv6 = true;
        size_t used = (size_t)(close - authority) + 1u;
        if (used < authority_len) {
            if (authority[used] != ':' ||
                !http_parse_port(authority + used + 1u, authority_len - used - 1u, &out->port)) {
                http_set_err(err, errlen, "HTTP: ungueltiger Port nach IPv6-Adresse");
                return false;
            }
        }
    } else {
        const char* colon = NULL;
        for (size_t i = 0; i < authority_len; i++) {
            if (authority[i] == ':') {
                if (colon) {
                    http_set_err(err, errlen, "HTTP: IPv6-Literal muss in [Klammern] stehen");
                    return false;
                }
                colon = authority + i;
            }
        }
        if (colon) {
            host_len = (size_t)(colon - authority);
            if (!http_parse_port(colon + 1, authority_len - host_len - 1u, &out->port)) {
                http_set_err(err, errlen, "HTTP: ungueltiger Port");
                return false;
            }
        }
    }
    if (!http_copy_part(out->host, sizeof(out->host), host_start, host_len,
                        "Host", err, errlen)) return false;

    const char* end = text + strlen(text);
    const char* fragment = strchr(p, '#');
    if (fragment) end = fragment;
    size_t path_len;
    if (p >= end || *p == '#') {
        strcpy(out->path, "/");
    } else if (*p == '?') {
        path_len = (size_t)(end - p);
        if (path_len + 1u >= sizeof(out->path)) {
            http_set_err(err, errlen, "HTTP: Pfad ist zu lang");
            return false;
        }
        out->path[0] = '/';
        memcpy(out->path + 1, p, path_len);
        out->path[path_len + 1u] = '\0';
    } else {
        path_len = (size_t)(end - p);
        if (!http_copy_part(out->path, sizeof(out->path), p, path_len,
                            "Pfad", err, errlen)) return false;
    }
    return true;
}

static bool http_host_header(const HttpUrl* url, char* out, size_t outcap) {
    bool default_port = (url->secure && url->port == 443) || (!url->secure && url->port == 80);
    int n;
    if (url->host_is_ipv6) {
        n = default_port ? snprintf(out, outcap, "[%s]", url->host)
                         : snprintf(out, outcap, "[%s]:%d", url->host, url->port);
    } else {
        n = default_port ? snprintf(out, outcap, "%s", url->host)
                         : snprintf(out, outcap, "%s:%d", url->host, url->port);
    }
    return n > 0 && (size_t)n < outcap;
}

static bool http_header_name_ok(const char* s, size_t len) {
    if (len == 0 || len > 127u) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c)) continue;
        if (strchr("!#$%&'*+-.^_`|~", c)) continue;
        return false;
    }
    return true;
}

static bool http_header_value_ok(const char* s, size_t len) {
    if (len > 8192u) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0 || c == '\r' || c == '\n') return false;
    }
    return true;
}

static bool http_reserved_request_header(const char* name, size_t len) {
    return http_ieq_n(name, len, "host") ||
           http_ieq_n(name, len, "content-length") ||
           http_ieq_n(name, len, "connection");
}

static bool http_append_request_headers(HttpBuf* req, MooValue headers,
                                        bool allow_sensitive, bool* has_content_type,
                                        char* err, size_t errlen) {
    if (headers.tag == MOO_NONE) return true;
    if (headers.tag != MOO_DICT) {
        http_set_err(err, errlen, "HTTP: Header muessen ein Dict sein");
        return false;
    }
    MooDict* d = MV_DICT(headers);
    for (int i = 0; i < d->capacity; i++) {
        if (!d->entries[i].occupied || !d->entries[i].key) continue;
        MooString* key = d->entries[i].key;
        MooValue value = d->entries[i].value;
        const char* val = value.tag == MOO_STRING ? MV_STR(value)->chars : "";
        size_t vlen = value.tag == MOO_STRING ? (size_t)MV_STR(value)->length : 0u;
        size_t klen = (size_t)key->length;
        if (!http_header_name_ok(key->chars, klen) || !http_header_value_ok(val, vlen)) {
            http_set_err(err, errlen, "HTTP: ungueltiger Request-Header");
            return false;
        }
        if (http_reserved_request_header(key->chars, klen)) continue;
        if (!allow_sensitive && (http_ieq_n(key->chars, klen, "authorization") ||
                                 http_ieq_n(key->chars, klen, "proxy-authorization") ||
                                 http_ieq_n(key->chars, klen, "cookie"))) continue;
        if (http_ieq_n(key->chars, klen, "content-type")) *has_content_type = true;
        if (!http_buf_append(req, key->chars, klen, HTTP_MAX_RESPONSE, err, errlen) ||
            !http_buf_text(req, ": ", HTTP_MAX_RESPONSE, err, errlen) ||
            !http_buf_append(req, val, vlen, HTTP_MAX_RESPONSE, err, errlen) ||
            !http_buf_text(req, "\r\n", HTTP_MAX_RESPONSE, err, errlen)) return false;
    }
    return true;
}

static bool http_set_socket_timeout(moo_sockfd_t fd, int timeout_ms) {
#ifdef _WIN32
    DWORD value = (DWORD)timeout_ms;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, MOO_SOPT(&value), sizeof(value)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, MOO_SOPT(&value), sizeof(value)) == 0;
#else
    struct timeval value;
    value.tv_sec = timeout_ms / 1000;
    value.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, MOO_SOPT(&value), sizeof(value)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, MOO_SOPT(&value), sizeof(value)) == 0;
#endif
}

static bool http_transport_open(HttpTransport* t, const HttpUrl* url,
                                char* err, size_t errlen) {
    memset(t, 0, sizeof(*t));
    t->fd = MOO_INVALID_SOCK;
    t->secure = url->secure;
    if (url->secure) {
        t->tls_backend = moo_tls_backend();
        if (!t->tls_backend) {
            http_set_err(err, errlen, "HTTPS: kein TLS-Backend verfuegbar");
            return false;
        }
        t->tls = t->tls_backend->verbinde(url->host, url->port, HTTP_TIMEOUT_MS,
                                          err, (int)errlen);
        return t->tls != NULL;
    }
    intptr_t raw = moo_net_tcp_connect_timeout(url->host, url->port, HTTP_TIMEOUT_MS,
                                               err, (int)errlen);
    if (raw < 0) return false;
    t->fd = (moo_sockfd_t)raw;
    if (!http_set_socket_timeout(t->fd, HTTP_TIMEOUT_MS)) {
        moo_closesock(t->fd);
        t->fd = MOO_INVALID_SOCK;
        http_set_err(err, errlen, "HTTP: Socket-Timeout konnte nicht gesetzt werden");
        return false;
    }
    return true;
}

static void http_transport_close(HttpTransport* t) {
    if (t->secure) {
        if (t->tls && t->tls_backend) t->tls_backend->schliesse(t->tls);
        t->tls = NULL;
    } else if (!MOO_SOCK_BAD(t->fd)) {
        moo_closesock(t->fd);
        t->fd = MOO_INVALID_SOCK;
    }
}

static bool http_was_interrupted(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

static int http_transport_write(HttpTransport* t, const char* data, int len) {
    if (t->secure) return t->tls_backend->schreibe(t->tls, data, len);
    return (int)send(t->fd, data, len, 0);
}

static int http_transport_read(HttpTransport* t, char* data, int max) {
    if (t->secure) return t->tls_backend->lese(t->tls, data, max);
    return (int)recv(t->fd, data, max, 0);
}

static bool http_transport_write_all(HttpTransport* t, const char* data, size_t len,
                                     char* err, size_t errlen) {
    size_t off = 0;
    while (off < len) {
        size_t remain = len - off;
        int part = remain > (size_t)INT_MAX ? INT_MAX : (int)remain;
        int n = http_transport_write(t, data + off, part);
        if (n < 0 && !t->secure && http_was_interrupted()) continue;
        if (n <= 0) {
            http_set_err(err, errlen, "HTTP: Schreiben fehlgeschlagen");
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

static bool http_transport_read_all(HttpTransport* t, HttpBuf* raw,
                                    char* err, size_t errlen) {
    char chunk[HTTP_IO_CHUNK];
    for (;;) {
        int n = http_transport_read(t, chunk, (int)sizeof(chunk));
        if (n < 0 && !t->secure && http_was_interrupted()) continue;
        if (n < 0) {
            http_set_err(err, errlen, "HTTP: Lesen fehlgeschlagen oder Timeout");
            return false;
        }
        if (n == 0) return true;
        if (!http_buf_append(raw, chunk, (size_t)n,
                             HTTP_MAX_RESPONSE + HTTP_MAX_HEADERS, err, errlen)) return false;
    }
}

static bool http_parse_decimal(const char* s, size_t len, size_t* out) {
    while (len && (*s == ' ' || *s == '\t')) { s++; len--; }
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
    if (len == 0) return false;
    size_t value = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        unsigned digit = (unsigned)(s[i] - '0');
        if (value > (HTTP_MAX_RESPONSE - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    *out = value;
    return true;
}

static bool http_parse_hex(const char* s, size_t len, size_t* out) {
    size_t value = 0;
    bool any = false;
    for (size_t i = 0; i < len && s[i] != ';'; i++) {
        unsigned char c = (unsigned char)s[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10u;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10u;
        else if (c == ' ' || c == '\t') continue;
        else return false;
        any = true;
        if (value > (HTTP_MAX_RESPONSE - digit) / 16u) return false;
        value = value * 16u + digit;
    }
    if (!any) return false;
    *out = value;
    return true;
}

static bool http_decode_chunked(const char* src, size_t len, HttpBuf* decoded,
                                char* err, size_t errlen) {
    size_t pos = 0;
    while (pos < len) {
        const char* line_end = http_find_bytes(src + pos, len - pos, "\r\n", 2u);
        if (!line_end) {
            http_set_err(err, errlen, "HTTP: abgeschnittene Chunk-Groesse");
            return false;
        }
        size_t line_len = (size_t)(line_end - (src + pos));
        size_t chunk_len;
        if (!http_parse_hex(src + pos, line_len, &chunk_len)) {
            http_set_err(err, errlen, "HTTP: ungueltige Chunk-Groesse");
            return false;
        }
        pos += line_len + 2u;
        if (chunk_len == 0) return true;
        if (chunk_len > len - pos || len - pos - chunk_len < 2u ||
            src[pos + chunk_len] != '\r' || src[pos + chunk_len + 1u] != '\n') {
            http_set_err(err, errlen, "HTTP: abgeschnittener Chunk");
            return false;
        }
        if (!http_buf_append(decoded, src + pos, chunk_len, HTTP_MAX_RESPONSE, err, errlen)) return false;
        pos += chunk_len + 2u;
    }
    http_set_err(err, errlen, "HTTP: terminierender Null-Chunk fehlt");
    return false;
}

static bool http_parse_response(HttpBuf* raw, HttpParsed* parsed,
                                char* err, size_t errlen) {
    memset(parsed, 0, sizeof(*parsed));
    parsed->headers = moo_none();
    const char* header_mark = http_find_bytes(raw->data, raw->len, "\r\n\r\n", 4u);
    if (!header_mark) {
        http_set_err(err, errlen, "HTTP: Response-Header unvollstaendig");
        return false;
    }
    size_t header_len = (size_t)(header_mark - raw->data);
    if (header_len > HTTP_MAX_HEADERS) {
        http_set_err(err, errlen, "HTTP: Response-Header zu gross");
        return false;
    }
    const char* status_end = http_find_bytes(raw->data, header_len, "\r\n", 2u);
    if (!status_end) {
        http_set_err(err, errlen, "HTTP: Statuszeile fehlt");
        return false;
    }
    char status_line[160];
    size_t status_len = (size_t)(status_end - raw->data);
    if (status_len == 0 || status_len >= sizeof(status_line)) {
        http_set_err(err, errlen, "HTTP: Statuszeile ungueltig");
        return false;
    }
    memcpy(status_line, raw->data, status_len);
    status_line[status_len] = '\0';
    if (sscanf(status_line, "HTTP/%*u.%*u %ld", &parsed->status) != 1 ||
        parsed->status < 100 || parsed->status > 999) {
        http_set_err(err, errlen, "HTTP: Statuscode ungueltig");
        return false;
    }

    parsed->headers = moo_dict_new();
    bool chunked = false;
    bool has_content_length = false;
    size_t content_length = 0;
    const char* line = status_end + 2;
    const char* header_end = header_mark;
    while (line < header_end) {
        const char* line_end = http_find_bytes(line, (size_t)(header_end - line), "\r\n", 2u);
        if (!line_end) line_end = header_end;
        const char* colon = memchr(line, ':', (size_t)(line_end - line));
        if (!colon) {
            moo_release(parsed->headers);
            parsed->headers = moo_none();
            http_set_err(err, errlen, "HTTP: ungueltige Headerzeile");
            return false;
        }
        const char* name_start = line;
        const char* name_end = colon;
        while (name_end > name_start && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end--;
        const char* value_start = colon + 1;
        while (value_start < line_end && (*value_start == ' ' || *value_start == '\t')) value_start++;
        const char* value_end = line_end;
        while (value_end > value_start && (value_end[-1] == ' ' || value_end[-1] == '\t')) value_end--;
        size_t name_len = (size_t)(name_end - name_start);
        size_t value_len = (size_t)(value_end - value_start);
        if (!http_header_name_ok(name_start, name_len) || value_len > 8192u) {
            moo_release(parsed->headers);
            parsed->headers = moo_none();
            http_set_err(err, errlen, "HTTP: Response-Header ungueltig oder zu lang");
            return false;
        }
        char lower[128];
        for (size_t i = 0; i < name_len; i++) lower[i] = (char)http_ascii_lower((unsigned char)name_start[i]);
        lower[name_len] = '\0';
        moo_dict_set(parsed->headers, moo_string_new(lower),
                     moo_string_new_len(value_start, (int32_t)value_len));
        if (http_ieq_n(name_start, name_len, "content-length")) {
            if (!http_parse_decimal(value_start, value_len, &content_length)) {
                moo_release(parsed->headers);
                parsed->headers = moo_none();
                http_set_err(err, errlen, "HTTP: Content-Length ungueltig");
                return false;
            }
            has_content_length = true;
        }
        if (http_ieq_n(name_start, name_len, "transfer-encoding") &&
            http_contains_ci(value_start, value_len, "chunked")) chunked = true;
        line = line_end + (line_end < header_end ? 2 : 0);
    }

    size_t body_start = header_len + 4u;
    size_t available = raw->len - body_start;
    if (chunked) {
        HttpBuf decoded;
        if (!http_buf_init(&decoded, 256u, err, errlen)) {
            moo_release(parsed->headers);
            parsed->headers = moo_none();
            return false;
        }
        if (!http_decode_chunked(raw->data + body_start, available, &decoded, err, errlen)) {
            http_buf_free(&decoded);
            moo_release(parsed->headers);
            parsed->headers = moo_none();
            return false;
        }
        parsed->owned_body = decoded.data;
        parsed->body = decoded.data;
        parsed->body_len = decoded.len;
    } else if (has_content_length) {
        if (content_length > available) {
            moo_release(parsed->headers);
            parsed->headers = moo_none();
            http_set_err(err, errlen, "HTTP: Body kuerzer als Content-Length");
            return false;
        }
        parsed->body = raw->data + body_start;
        parsed->body_len = content_length;
    } else {
        if (available > HTTP_MAX_RESPONSE) {
            moo_release(parsed->headers);
            parsed->headers = moo_none();
            http_set_err(err, errlen, "HTTP: Body zu gross");
            return false;
        }
        parsed->body = raw->data + body_start;
        parsed->body_len = available;
    }
    return true;
}

static void http_parsed_free(HttpParsed* parsed) {
    free(parsed->owned_body);
    parsed->owned_body = NULL;
    if (parsed->headers.tag != MOO_NONE) moo_release(parsed->headers);
    parsed->headers = moo_none();
}

static const MooString* http_header_borrow(MooValue headers, const char* wanted) {
    if (headers.tag != MOO_DICT) return NULL;
    MooDict* d = MV_DICT(headers);
    for (int i = 0; i < d->capacity; i++) {
        if (!d->entries[i].occupied || !d->entries[i].key) continue;
        MooString* key = d->entries[i].key;
        if (http_ieq_n(key->chars, (size_t)key->length, wanted) &&
            d->entries[i].value.tag == MOO_STRING) return MV_STR(d->entries[i].value);
    }
    return NULL;
}

static bool http_same_origin(const HttpUrl* a, const HttpUrl* b) {
    return a->secure == b->secure && a->port == b->port && moo_strcasecmp(a->host, b->host) == 0;
}

static bool http_build_redirect(const HttpUrl* base, const char* location, size_t loc_len,
                                char* out, size_t outcap, char* err, size_t errlen) {
    while (loc_len && (*location == ' ' || *location == '\t')) { location++; loc_len--; }
    while (loc_len && (location[loc_len - 1] == ' ' || location[loc_len - 1] == '\t')) loc_len--;
    const char* hash = memchr(location, '#', loc_len);
    if (hash) loc_len = (size_t)(hash - location);
    if (loc_len == 0 || loc_len >= outcap) {
        http_set_err(err, errlen, "HTTP: Redirect-Location leer oder zu lang");
        return false;
    }
    if ((loc_len >= 7u && http_starts_ci(location, "http://")) ||
        (loc_len >= 8u && http_starts_ci(location, "https://"))) {
        memcpy(out, location, loc_len);
        out[loc_len] = '\0';
        return true;
    }
    char authority[320];
    if (!http_host_header(base, authority, sizeof(authority))) {
        http_set_err(err, errlen, "HTTP: Redirect-Authority zu lang");
        return false;
    }
    const char* scheme = base->secure ? "https" : "http";
    int n;
    if (loc_len >= 2u && location[0] == '/' && location[1] == '/') {
        n = snprintf(out, outcap, "%s:%.*s", scheme, (int)loc_len, location);
    } else if (location[0] == '/') {
        n = snprintf(out, outcap, "%s://%s%.*s", scheme, authority, (int)loc_len, location);
    } else {
        const char* slash = strrchr(base->path, '/');
        size_t dir_len = slash ? (size_t)(slash - base->path) + 1u : 1u;
        n = snprintf(out, outcap, "%s://%s%.*s%.*s", scheme, authority,
                     (int)dir_len, base->path, (int)loc_len, location);
    }
    if (n <= 0 || (size_t)n >= outcap) {
        http_set_err(err, errlen, "HTTP: Redirect-URL zu lang");
        return false;
    }
    return true;
}

static bool http_build_request(HttpBuf* request, const char* method, const HttpUrl* url,
                               const char* body, size_t body_len, MooValue headers,
                               bool allow_sensitive, char* err, size_t errlen) {
    char host_header[320];
    if (!http_host_header(url, host_header, sizeof(host_header))) {
        http_set_err(err, errlen, "HTTP: Host-Header zu lang");
        return false;
    }
    if (!http_buf_text(request, method, HTTP_MAX_RESPONSE, err, errlen) ||
        !http_buf_text(request, " ", HTTP_MAX_RESPONSE, err, errlen) ||
        !http_buf_text(request, url->path, HTTP_MAX_RESPONSE, err, errlen) ||
        !http_buf_text(request, " HTTP/1.1\r\nHost: ", HTTP_MAX_RESPONSE, err, errlen) ||
        !http_buf_text(request, host_header, HTTP_MAX_RESPONSE, err, errlen) ||
        !http_buf_text(request, "\r\nUser-Agent: moo-http/1.0\r\nAccept: */*\r\nConnection: close\r\n",
                       HTTP_MAX_RESPONSE, err, errlen)) return false;

    bool has_content_type = false;
    if (!http_append_request_headers(request, headers, allow_sensitive,
                                     &has_content_type, err, errlen)) return false;
    if (body) {
        if (!has_content_type &&
            !http_buf_text(request, "Content-Type: application/json\r\n",
                           HTTP_MAX_RESPONSE, err, errlen)) return false;
        char length_line[64];
        int n = snprintf(length_line, sizeof(length_line), "Content-Length: %zu\r\n", body_len);
        if (n <= 0 || (size_t)n >= sizeof(length_line) ||
            !http_buf_append(request, length_line, (size_t)n,
                             HTTP_MAX_RESPONSE, err, errlen)) return false;
    }
    if (!http_buf_text(request, "\r\n", HTTP_MAX_RESPONSE, err, errlen)) return false;
    return !body || http_buf_append(request, body, body_len, HTTP_MAX_RESPONSE, err, errlen);
}

static MooValue http_make_response(HttpParsed* parsed, bool include_headers) {
    MooValue result = moo_dict_new();
    moo_dict_set(result, moo_string_new("status"), moo_number((double)parsed->status));
    moo_dict_set(result, moo_string_new("body"),
                 moo_string_new_len(parsed->body, (int32_t)parsed->body_len));
    moo_dict_set(result, moo_string_new("ok"),
                 moo_bool(parsed->status >= 200 && parsed->status < 300));
    if (include_headers) {
        moo_dict_set(result, moo_string_new("headers"), parsed->headers);
        parsed->headers = moo_none();
    }
    return result;
}

static MooValue http_execute(const char* method, const char* initial_url,
                             const char* body, size_t body_len, MooValue headers,
                             bool include_headers) {
    char current[HTTP_MAX_URL];
    size_t initial_len = strlen(initial_url);
    if (initial_len == 0 || initial_len >= sizeof(current)) return moo_error("HTTP: URL ist leer oder zu lang");
    memcpy(current, initial_url, initial_len + 1u);
    char current_method[5];
    snprintf(current_method, sizeof(current_method), "%s", method);
    bool send_body = body != NULL;
    bool allow_sensitive = true;

    for (int redirect = 0; redirect <= HTTP_MAX_REDIRECTS; redirect++) {
        char err[512] = {0};
        HttpUrl url;
        if (!http_parse_url(current, &url, err, sizeof(err))) return moo_error(err);

        HttpBuf request;
        if (!http_buf_init(&request, 1024u, err, sizeof(err))) return moo_error(err);
        if (!http_build_request(&request, current_method, &url,
                                send_body ? body : NULL, send_body ? body_len : 0u,
                                headers, allow_sensitive, err, sizeof(err))) {
            http_buf_free(&request);
            return moo_error(err);
        }

        HttpTransport transport;
        if (!http_transport_open(&transport, &url, err, sizeof(err))) {
            http_buf_free(&request);
            return moo_error(err[0] ? err : "HTTP: Verbindung fehlgeschlagen");
        }
        bool wrote = http_transport_write_all(&transport, request.data, request.len, err, sizeof(err));
        http_buf_free(&request);
        if (!wrote) {
            http_transport_close(&transport);
            return moo_error(err);
        }

        HttpBuf raw;
        if (!http_buf_init(&raw, 4096u, err, sizeof(err))) {
            http_transport_close(&transport);
            return moo_error(err);
        }
        bool read_ok = http_transport_read_all(&transport, &raw, err, sizeof(err));
        http_transport_close(&transport);
        if (!read_ok) {
            http_buf_free(&raw);
            return moo_error(err);
        }

        HttpParsed parsed;
        if (!http_parse_response(&raw, &parsed, err, sizeof(err))) {
            http_buf_free(&raw);
            return moo_error(err);
        }
        bool is_redirect = parsed.status == 301 || parsed.status == 302 || parsed.status == 303 ||
                           parsed.status == 307 || parsed.status == 308;
        const MooString* location = is_redirect ? http_header_borrow(parsed.headers, "location") : NULL;
        if (location) {
            if (redirect == HTTP_MAX_REDIRECTS) {
                http_parsed_free(&parsed);
                http_buf_free(&raw);
                return moo_error("HTTP: zu viele Redirects");
            }
            char next[HTTP_MAX_URL];
            if (!http_build_redirect(&url, location->chars, (size_t)location->length,
                                     next, sizeof(next), err, sizeof(err))) {
                http_parsed_free(&parsed);
                http_buf_free(&raw);
                return moo_error(err);
            }
            HttpUrl next_url;
            if (!http_parse_url(next, &next_url, err, sizeof(err))) {
                http_parsed_free(&parsed);
                http_buf_free(&raw);
                return moo_error(err);
            }
            if (!http_same_origin(&url, &next_url)) allow_sensitive = false;
            if (parsed.status == 303 || ((parsed.status == 301 || parsed.status == 302) && send_body)) {
                strcpy(current_method, "GET");
                send_body = false;
            }
            snprintf(current, sizeof(current), "%s", next);
            http_parsed_free(&parsed);
            http_buf_free(&raw);
            continue;
        }

        MooValue result = http_make_response(&parsed, include_headers);
        http_parsed_free(&parsed);
        http_buf_free(&raw);
        return result;
    }
    return moo_error("HTTP: Redirect-Schleife");
}

static bool http_url_value(MooValue url, const char** text, char* err, size_t errlen) {
    if (url.tag != MOO_STRING) {
        http_set_err(err, errlen, "HTTP: URL muss ein String sein");
        return false;
    }
    MooString* s = MV_STR(url);
    if (s->length <= 0 || (size_t)s->length >= HTTP_MAX_URL ||
        memchr(s->chars, '\0', (size_t)s->length) != NULL) {
        http_set_err(err, errlen, "HTTP: URL ist leer, zu lang oder enthaelt NUL");
        return false;
    }
    *text = s->chars;
    return true;
}

MooValue moo_http_get(MooValue url) {
    char err[256];
    const char* text;
    if (!http_url_value(url, &text, err, sizeof(err))) return moo_error(err);
    return http_execute("GET", text, NULL, 0u, moo_none(), false);
}

MooValue moo_http_post(MooValue url, MooValue body) {
    char err[256];
    const char* text;
    if (!http_url_value(url, &text, err, sizeof(err))) return moo_error(err);
    MooValue json = moo_json_string(body);
    if (json.tag != MOO_STRING) return json;
    MooString* s = MV_STR(json);
    MooValue result = http_execute("POST", text, s->chars, (size_t)s->length, moo_none(), false);
    moo_release(json);
    return result;
}

MooValue moo_http_get_with_headers(MooValue url, MooValue headers) {
    char err[256];
    const char* text;
    if (!http_url_value(url, &text, err, sizeof(err))) return moo_error(err);
    return http_execute("GET", text, NULL, 0u, headers, true);
}

MooValue moo_http_post_with_headers(MooValue url, MooValue body, MooValue headers) {
    char err[256];
    const char* text;
    if (!http_url_value(url, &text, err, sizeof(err))) return moo_error(err);
    MooValue json = moo_json_string(body);
    if (json.tag != MOO_STRING) return json;
    MooString* s = MV_STR(json);
    MooValue result = http_execute("POST", text, s->chars, (size_t)s->length, headers, true);
    moo_release(json);
    return result;
}
