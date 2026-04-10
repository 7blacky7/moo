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

// === Server schliessen ===

void moo_web_close(MooValue server) {
    if (server.tag != MOO_WEBSERVER) return;
    MooWebServer* ws = (MooWebServer*)moo_val_as_ptr(server);
    close(ws->server_fd);
    ws->server_fd = -1;
}
