#include "moo_runtime.h"
#include <curl/curl.h>

// Shared header-capture callback: parses "Name: value\r\n" lines that libcurl
// delivers via CURLOPT_HEADERFUNCTION and stuffs them into a MooDict with
// lowercased keys. The first line ("HTTP/1.1 200 OK") is skipped.
typedef struct {
    MooValue dict;
} HeaderCap;

static size_t header_cap_cb(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    HeaderCap* hc = (HeaderCap*)userdata;
    if (total < 2) return total;
    // Skip CRLF-only line (end of headers).
    if (buffer[0] == '\r' && total <= 2) return total;
    // Skip HTTP status line.
    if (total >= 5 && buffer[0] == 'H' && buffer[1] == 'T' && buffer[2] == 'T' && buffer[3] == 'P' && buffer[4] == '/') return total;
    // Find ':'
    size_t c = 0;
    while (c < total && buffer[c] != ':') c++;
    if (c >= total) return total;
    char name[128] = {0};
    size_t nlen = c < 127 ? c : 127;
    for (size_t i = 0; i < nlen; i++) {
        char ch = buffer[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        name[i] = ch;
    }
    size_t vstart = c + 1;
    while (vstart < total && (buffer[vstart] == ' ' || buffer[vstart] == '\t')) vstart++;
    size_t vend = total;
    while (vend > vstart && (buffer[vend - 1] == '\r' || buffer[vend - 1] == '\n')) vend--;
    char value[2048] = {0};
    size_t vlen = vend - vstart;
    if (vlen > 2047) vlen = 2047;
    memcpy(value, buffer + vstart, vlen);
    moo_dict_set(hc->dict, moo_string_new(name), moo_string_new(value));
    return total;
}

// Build a curl_slist from a MooDict {"Name": "value"} (headers dict).
static struct curl_slist* build_curl_headers(MooValue headers) {
    if (headers.tag != MOO_DICT) return NULL;
    MooDict* d = MV_DICT(headers);
    struct curl_slist* list = NULL;
    for (int i = 0; i < d->capacity; i++) {
        if (!d->entries[i].occupied) continue;
        MooString* ks = d->entries[i].key;
        MooValue v = d->entries[i].value;
        if (!ks) continue;
        const char* vs = (v.tag == MOO_STRING) ? MV_STR(v)->chars : "";
        char line[2176];
        int n = snprintf(line, sizeof(line), "%s: %s", ks->chars, vs);
        if (n > 0 && n < (int)sizeof(line)) list = curl_slist_append(list, line);
    }
    return list;
}


// === Buffer for curl response ===

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} CurlBuf;

static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    CurlBuf* buf = (CurlBuf*)userdata;
    size_t total = size * nmemb;
    while (buf->len + total + 1 > buf->cap) {
        buf->cap *= 2;
        buf->data = (char*)moo_realloc(buf->data, buf->cap);
    }
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

static void curl_buf_init(CurlBuf* buf) {
    buf->cap = 256;
    buf->data = (char*)moo_alloc(buf->cap);
    buf->len = 0;
    buf->data[0] = '\0';
}

// Build response dict: {"status": N, "body": "...", "ok": true/false}
static MooValue make_response(long status, const char* body) {
    MooValue dict = moo_dict_new();
    moo_dict_set(dict, moo_string_new("status"), moo_number((double)status));
    moo_dict_set(dict, moo_string_new("body"), moo_string_new(body));
    moo_dict_set(dict, moo_string_new("ok"), moo_bool(status >= 200 && status < 300));
    return dict;
}

MooValue moo_http_get(MooValue url) {
    if (url.tag != MOO_STRING) return moo_error("http_get: URL muss ein String sein");
    MooString* url_str = MV_STR(url);

    CURL* curl = curl_easy_init();
    if (!curl) return moo_error("http_get: curl init failed");

    CurlBuf buf;
    curl_buf_init(&buf);

    curl_easy_setopt(curl, CURLOPT_URL, url_str->chars);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        moo_free(buf.data);
        char err[256];
        snprintf(err, sizeof(err), "http_get: %s", curl_easy_strerror(res));
        return moo_error(err);
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    MooValue result = make_response(status, buf.data);
    moo_free(buf.data);
    return result;
}

// Forward declaration from moo_json.c
MooValue moo_json_string(MooValue value);

MooValue moo_http_post(MooValue url, MooValue body) {
    if (url.tag != MOO_STRING) return moo_error("http_post: URL muss ein String sein");
    MooString* url_str = MV_STR(url);

    // Convert body to JSON string
    MooValue json_body = moo_json_string(body);
    MooString* json_str = MV_STR(json_body);

    CURL* curl = curl_easy_init();
    if (!curl) return moo_error("http_post: curl init failed");

    CurlBuf buf;
    curl_buf_init(&buf);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url_str->chars);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str->chars);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        moo_free(buf.data);
        char err[256];
        snprintf(err, sizeof(err), "http_post: %s", curl_easy_strerror(res));
        return moo_error(err);
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    MooValue result = make_response(status, buf.data);
    moo_free(buf.data);
    return result;
}

// Build response dict with captured response headers: {status, body, ok, headers}.
static MooValue make_response_with_headers(long status, const char* body, MooValue headers) {
    MooValue dict = moo_dict_new();
    moo_dict_set(dict, moo_string_new("status"), moo_number((double)status));
    moo_dict_set(dict, moo_string_new("body"), moo_string_new(body));
    moo_dict_set(dict, moo_string_new("ok"), moo_bool(status >= 200 && status < 300));
    moo_dict_set(dict, moo_string_new("headers"), headers);
    return dict;
}

MooValue moo_http_get_with_headers(MooValue url, MooValue hdr_dict) {
    if (url.tag != MOO_STRING) return moo_error("http_get_with_headers: URL muss ein String sein");
    MooString* url_str = MV_STR(url);

    CURL* curl = curl_easy_init();
    if (!curl) return moo_error("http_get_with_headers: curl init failed");

    CurlBuf buf;
    curl_buf_init(&buf);

    HeaderCap cap;
    cap.dict = moo_dict_new();

    struct curl_slist* req_headers = build_curl_headers(hdr_dict);

    curl_easy_setopt(curl, CURLOPT_URL, url_str->chars);
    if (req_headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cap_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &cap);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    if (req_headers) curl_slist_free_all(req_headers);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        moo_free(buf.data);
        char err[256];
        snprintf(err, sizeof(err), "http_get_with_headers: %s", curl_easy_strerror(res));
        return moo_error(err);
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    MooValue result = make_response_with_headers(status, buf.data, cap.dict);
    moo_free(buf.data);
    return result;
}

MooValue moo_http_post_with_headers(MooValue url, MooValue body, MooValue hdr_dict) {
    if (url.tag != MOO_STRING) return moo_error("http_post_with_headers: URL muss ein String sein");
    MooString* url_str = MV_STR(url);

    MooValue json_body = moo_json_string(body);
    MooString* json_str = MV_STR(json_body);

    CURL* curl = curl_easy_init();
    if (!curl) return moo_error("http_post_with_headers: curl init failed");

    CurlBuf buf;
    curl_buf_init(&buf);

    HeaderCap cap;
    cap.dict = moo_dict_new();

    struct curl_slist* req_headers = build_curl_headers(hdr_dict);
    bool has_ct = false;
    if (hdr_dict.tag == MOO_DICT) {
        MooDict* d = MV_DICT(hdr_dict);
        for (int i = 0; i < d->capacity && !has_ct; i++) {
            if (!d->entries[i].occupied) continue;
            MooString* ks = d->entries[i].key;
            if (ks && strcasecmp(ks->chars, "content-type") == 0) has_ct = true;
        }
    }
    if (!has_ct) req_headers = curl_slist_append(req_headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url_str->chars);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str->chars);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cap_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &cap);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(req_headers);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        moo_free(buf.data);
        char err[256];
        snprintf(err, sizeof(err), "http_post_with_headers: %s", curl_easy_strerror(res));
        return moo_error(err);
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    MooValue result = make_response_with_headers(status, buf.data, cap.dict);
    moo_free(buf.data);
    return result;
}
