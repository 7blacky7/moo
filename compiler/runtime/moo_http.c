#include "moo_runtime.h"
#include <curl/curl.h>

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
