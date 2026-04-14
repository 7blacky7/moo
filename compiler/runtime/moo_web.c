/**
 * moo_web.c — HTTP Webserver Runtime fuer moo.
 * Baut auf TCP-Sockets auf (moo_net.c).
 * Parst HTTP-Requests und erzeugt HTTP-Responses.
 */

#include "moo_runtime.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

// Vorwaertsdeklarationen
extern MooValue moo_string_new(const char* s);
extern MooValue moo_number(double n);
extern MooValue moo_bool(bool b);
extern MooValue moo_none(void);
extern MooValue moo_dict_new(void);
extern void moo_dict_set(MooValue dict, MooValue key, MooValue value);
extern MooValue moo_json_string(MooValue value);
extern void moo_throw(MooValue v);

// MOO_WEBSERVER = 16
typedef struct {
    int server_fd;
    int port;
} MooWebServer;

// === HTTP Request Parser ===

static MooValue parse_http_request(const char* raw, int len) {
    MooValue req = moo_dict_new();

    // Methode extrahieren (GET, POST, etc.)
    int i = 0;
    while (i < len && raw[i] != ' ') i++;
    char method[16] = {0};
    if (i < 16) memcpy(method, raw, i);
    moo_dict_set(req, moo_string_new("methode"), moo_string_new(method));
    moo_dict_set(req, moo_string_new("method"), moo_string_new(method));

    // Pfad extrahieren
    i++; // skip space
    int path_start = i;
    while (i < len && raw[i] != ' ' && raw[i] != '?') i++;
    char path[1024] = {0};
    int path_len = i - path_start;
    if (path_len > 1023) path_len = 1023;
    memcpy(path, raw + path_start, path_len);
    moo_dict_set(req, moo_string_new("pfad"), moo_string_new(path));
    moo_dict_set(req, moo_string_new("path"), moo_string_new(path));

    // Query-String
    if (i < len && raw[i] == '?') {
        i++;
        int qs_start = i;
        while (i < len && raw[i] != ' ') i++;
        char qs[2048] = {0};
        int qs_len = i - qs_start;
        if (qs_len > 2047) qs_len = 2047;
        memcpy(qs, raw + qs_start, qs_len);
        moo_dict_set(req, moo_string_new("query"), moo_string_new(qs));
    }

    // Body: nach \r\n\r\n
    const char* body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        moo_dict_set(req, moo_string_new("body"), moo_string_new(body_start));
    } else {
        moo_dict_set(req, moo_string_new("body"), moo_string_new(""));
    }

    // Headers: parse lines between first CRLF and body boundary.
    // Keys are lowercased for stable lookup (headers["cookie"], headers["user-agent"]).
    MooValue headers = moo_dict_new();
    const char* hdr_start = strstr(raw, "\r\n");
    if (hdr_start) {
        hdr_start += 2;
        const char* hdr_end = body_start ? (body_start - 4) : (raw + len);
        const char* p = hdr_start;
        while (p < hdr_end) {
            const char* line_end = p;
            while (line_end < hdr_end && !(line_end[0] == '\r' && line_end + 1 < hdr_end && line_end[1] == '\n')) line_end++;
            if (line_end == p) break;
            const char* colon = p;
            while (colon < line_end && *colon != ':') colon++;
            if (colon < line_end) {
                char name[128] = {0};
                int name_len = (int)(colon - p);
                if (name_len > 127) name_len = 127;
                for (int k = 0; k < name_len; k++) {
                    char c = p[k];
                    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                    name[k] = c;
                }
                const char* vstart = colon + 1;
                while (vstart < line_end && (*vstart == ' ' || *vstart == '\t')) vstart++;
                int val_len = (int)(line_end - vstart);
                if (val_len < 0) val_len = 0;
                char value[2048] = {0};
                if (val_len > 2047) val_len = 2047;
                memcpy(value, vstart, val_len);
                moo_dict_set(headers, moo_string_new(name), moo_string_new(value));
            }
            if (line_end + 1 < hdr_end) p = line_end + 2; else break;
        }
    }
    moo_dict_set(req, moo_string_new("headers"), headers);

    return req;
}

// === Webserver erstellen ===

MooValue moo_web_server(MooValue port) {
    int p = (int)MV_NUM(port);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        moo_throw(moo_string_new("WebServer: Socket erstellen fehlgeschlagen"));
        return moo_none();
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(p);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        moo_throw(moo_string_new("WebServer: Bind fehlgeschlagen"));
        return moo_none();
    }

    if (listen(fd, 128) < 0) {
        close(fd);
        moo_throw(moo_string_new("WebServer: Listen fehlgeschlagen"));
        return moo_none();
    }

    MooWebServer* ws = (MooWebServer*)malloc(sizeof(MooWebServer));
    ws->server_fd = fd;
    ws->port = p;

    MooValue v;
    v.tag = MOO_WEBSERVER;
    moo_val_set_ptr(&v, ws);
    return v;
}

// === Request annehmen und parsen ===

MooValue moo_web_accept(MooValue server) {
    if (server.tag != MOO_WEBSERVER) {
        moo_throw(moo_string_new("Kein WebServer"));
        return moo_none();
    }
    MooWebServer* ws = (MooWebServer*)moo_val_as_ptr(server);

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(ws->server_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
        return moo_none();
    }

    // Request lesen
    char buf[8192] = {0};
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        close(client_fd);
        return moo_none();
    }

    // Request parsen
    MooValue req = parse_http_request(buf, (int)n);

    // Client-FD im Request speichern (fuer Response)
    moo_dict_set(req, moo_string_new("_fd"), moo_number((double)client_fd));

    return req;
}

// === HTTP Response senden ===

MooValue moo_web_respond(MooValue request, MooValue body, MooValue status_code) {
    // Client-FD aus Request holen
    MooValue fd_val = moo_dict_get(request, moo_string_new("_fd"));
    if (fd_val.tag != MOO_NUMBER) return moo_none();
    int client_fd = (int)MV_NUM(fd_val);

    int status = (status_code.tag == MOO_NUMBER) ? (int)MV_NUM(status_code) : 200;
    const char* body_str = (body.tag == MOO_STRING) ? MV_STR(body)->chars : "";
    int body_len = (body.tag == MOO_STRING) ? MV_STR(body)->length : 0;

    const char* status_text = "OK";
    if (status == 404) status_text = "Not Found";
    else if (status == 500) status_text = "Internal Server Error";
    else if (status == 301) status_text = "Moved Permanently";
    else if (status == 400) status_text = "Bad Request";

    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, body_len);

    write(client_fd, header, strlen(header));
    if (body_len > 0) write(client_fd, body_str, body_len);
    close(client_fd);

    return moo_none();
}

// === JSON Response senden ===

MooValue moo_web_json(MooValue request, MooValue data) {
    MooValue fd_val = moo_dict_get(request, moo_string_new("_fd"));
    if (fd_val.tag != MOO_NUMBER) return moo_none();
    int client_fd = (int)MV_NUM(fd_val);

    // Dict zu JSON String
    MooValue json = moo_json_string(data);
    const char* json_str = MV_STR(json)->chars;
    int json_len = MV_STR(json)->length;

    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        json_len);

    write(client_fd, header, strlen(header));
    write(client_fd, json_str, json_len);
    close(client_fd);

    return moo_none();
}

// === Static File Serving ===

static const char* guess_content_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcmp(ext, ".woff2") == 0) return "font/woff2";
    if (strcmp(ext, ".txt") == 0) return "text/plain; charset=utf-8";
    if (strcmp(ext, ".xml") == 0) return "application/xml";
    if (strcmp(ext, ".moo") == 0) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

MooValue moo_web_file(MooValue request, MooValue filepath) {
    MooValue fd_val = moo_dict_get(request, moo_string_new("_fd"));
    if (fd_val.tag != MOO_NUMBER) return moo_none();
    int client_fd = (int)MV_NUM(fd_val);

    if (filepath.tag != MOO_STRING) {
        // 400 Bad Request
        const char* err = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
        write(client_fd, err, strlen(err));
        close(client_fd);
        return moo_none();
    }

    const char* path = MV_STR(filepath)->chars;

    // Sicherheit: keine .. im Pfad (Path-Traversal verhindern)
    if (strstr(path, "..")) {
        const char* err = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n";
        write(client_fd, err, strlen(err));
        close(client_fd);
        return moo_none();
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        const char* err = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\nDatei nicht gefunden";
        write(client_fd, err, strlen(err));
        close(client_fd);
        return moo_none();
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    const char* ctype = guess_content_type(path);

    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        ctype, size);
    write(client_fd, header, strlen(header));

    // Datei in Chunks senden
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        write(client_fd, buf, n);
    }
    fclose(f);
    close(client_fd);

    return moo_bool(true);
}

// === Template-Engine ===

MooValue moo_web_template(MooValue request, MooValue html, MooValue vars) {
    MooValue fd_val = moo_dict_get(request, moo_string_new("_fd"));
    if (fd_val.tag != MOO_NUMBER || html.tag != MOO_STRING || vars.tag != MOO_DICT) {
        return moo_none();
    }
    int client_fd = (int)MV_NUM(fd_val);

    const char* src = MV_STR(html)->chars;
    int src_len = MV_STR(html)->length;

    // Output Buffer (max 4x Quellgroesse)
    int out_cap = src_len * 4 + 1024;
    char* out = (char*)malloc(out_cap);
    int out_len = 0;

    int i = 0;
    while (i < src_len) {
        // {{variable}} suchen
        if (i + 1 < src_len && src[i] == '{' && src[i+1] == '{') {
            i += 2;
            // Variablenname extrahieren
            char varname[256] = {0};
            int vi = 0;
            while (i < src_len && !(src[i] == '}' && i+1 < src_len && src[i+1] == '}')) {
                // Whitespace ueberspringen
                if (src[i] != ' ' && vi < 255) varname[vi++] = src[i];
                i++;
            }
            if (i + 1 < src_len) i += 2; // }}

            // Wert aus Dict holen
            MooValue key = moo_string_new(varname);
            MooValue val = moo_dict_get(vars, key);
            MooValue val_str = moo_to_string(val);
            const char* replacement = MV_STR(val_str)->chars;
            int repl_len = MV_STR(val_str)->length;

            if (out_len + repl_len < out_cap - 1) {
                memcpy(out + out_len, replacement, repl_len);
                out_len += repl_len;
            }
        } else {
            if (out_len < out_cap - 1) out[out_len++] = src[i];
            i++;
        }
    }
    out[out_len] = '\0';

    // HTTP Response senden
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        out_len);
    write(client_fd, header, strlen(header));
    write(client_fd, out, out_len);
    free(out);
    close(client_fd);

    return moo_bool(true);
}

// === HTTP Response mit zusaetzlichen Headers ===
//
// Wie moo_web_respond, akzeptiert zusaetzlich ein headers-Dict. Bestehende
// Default-Header (Content-Type, Content-Length, Connection) werden nur dann
// gesetzt, wenn das headers-Dict sie nicht ueberschreibt. Damit koennen
// Cookies (Set-Cookie), CORS-Header oder Cache-Control gesetzt werden, ohne
// die alte moo_web_respond-Variante zu brechen.
static void write_extra_headers(int client_fd, MooValue headers, bool* seen_content_type) {
    if (headers.tag != MOO_DICT) return;
    MooDict* d = MV_DICT(headers);
    for (int i = 0; i < d->capacity; i++) {
        if (!d->entries[i].occupied) continue;
        MooString* ks = d->entries[i].key;
        MooValue v = d->entries[i].value;
        if (!ks) continue;
        const char* kn = ks->chars;
        // Content-Length/Connection werden von uns gesetzt — ignorieren um
        // widerspruechliche Antworten zu vermeiden. User-Content-Type wird
        // durchgereicht; wir merken das, um das Default-Content-Type zu
        // unterdruecken.
        if (strcasecmp(kn, "content-length") == 0) continue;
        if (strcasecmp(kn, "connection") == 0) continue;
        if (strcasecmp(kn, "content-type") == 0 && seen_content_type) *seen_content_type = true;
        const char* vs = (v.tag == MOO_STRING) ? MV_STR(v)->chars : "";
        char line[1024];
        int n = snprintf(line, sizeof(line), "%s: %s\r\n", kn, vs);
        if (n > 0 && n < (int)sizeof(line)) write(client_fd, line, n);
    }
}

MooValue moo_web_respond_with_headers(MooValue request, MooValue body, MooValue status_code, MooValue headers) {
    MooValue fd_val = moo_dict_get(request, moo_string_new("_fd"));
    if (fd_val.tag != MOO_NUMBER) return moo_none();
    int client_fd = (int)MV_NUM(fd_val);

    int status = (status_code.tag == MOO_NUMBER) ? (int)MV_NUM(status_code) : 200;
    const char* body_str = (body.tag == MOO_STRING) ? MV_STR(body)->chars : "";
    int body_len = (body.tag == MOO_STRING) ? MV_STR(body)->length : 0;

    const char* status_text = "OK";
    if (status == 404) status_text = "Not Found";
    else if (status == 500) status_text = "Internal Server Error";
    else if (status == 301) status_text = "Moved Permanently";
    else if (status == 302) status_text = "Found";
    else if (status == 400) status_text = "Bad Request";
    else if (status == 401) status_text = "Unauthorized";
    else if (status == 403) status_text = "Forbidden";

    char start_line[128];
    int sn = snprintf(start_line, sizeof(start_line), "HTTP/1.1 %d %s\r\n", status, status_text);
    write(client_fd, start_line, sn);

    bool seen_ct = false;
    write_extra_headers(client_fd, headers, &seen_ct);

    char fixed[256];
    int fn = snprintf(fixed, sizeof(fixed),
        "%sContent-Length: %d\r\nConnection: close\r\n\r\n",
        seen_ct ? "" : "Content-Type: text/html; charset=utf-8\r\n",
        body_len);
    write(client_fd, fixed, fn);

    if (body_len > 0) write(client_fd, body_str, body_len);
    close(client_fd);
    return moo_none();
}

MooValue moo_web_json_with_headers(MooValue request, MooValue data, MooValue status_code, MooValue headers) {
    MooValue fd_val = moo_dict_get(request, moo_string_new("_fd"));
    if (fd_val.tag != MOO_NUMBER) return moo_none();
    int client_fd = (int)MV_NUM(fd_val);

    int status = (status_code.tag == MOO_NUMBER) ? (int)MV_NUM(status_code) : 200;
    MooValue json = moo_json_string(data);
    const char* json_str = MV_STR(json)->chars;
    int json_len = MV_STR(json)->length;

    const char* status_text = "OK";
    if (status == 404) status_text = "Not Found";
    else if (status == 500) status_text = "Internal Server Error";
    else if (status == 400) status_text = "Bad Request";
    else if (status == 401) status_text = "Unauthorized";
    else if (status == 403) status_text = "Forbidden";

    char start_line[128];
    int sn = snprintf(start_line, sizeof(start_line), "HTTP/1.1 %d %s\r\n", status, status_text);
    write(client_fd, start_line, sn);

    bool seen_ct = false;
    write_extra_headers(client_fd, headers, &seen_ct);

    char fixed[256];
    int fn = snprintf(fixed, sizeof(fixed),
        "%sContent-Length: %d\r\nConnection: close\r\n\r\n",
        seen_ct ? "" : "Content-Type: application/json; charset=utf-8\r\n",
        json_len);
    write(client_fd, fixed, fn);

    write(client_fd, json_str, json_len);
    close(client_fd);
    return moo_none();
}

// === Server schliessen ===

void moo_web_close(MooValue server) {
    if (server.tag != MOO_WEBSERVER) return;
    MooWebServer* ws = (MooWebServer*)moo_val_as_ptr(server);
    close(ws->server_fd);
    ws->server_fd = -1;
}
