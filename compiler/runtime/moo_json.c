#include "moo_runtime.h"

// === Simple recursive JSON parser ===

typedef struct {
    const char* src;
    int pos;
    int len;
} JsonParser;

static void json_skip_ws(JsonParser* p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->pos++;
        else break;
    }
}

static char json_peek(JsonParser* p) {
    json_skip_ws(p);
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos];
}

static char json_next(JsonParser* p) {
    json_skip_ws(p);
    if (p->pos >= p->len) return '\0';
    return p->src[p->pos++];
}

static MooValue json_parse_value(JsonParser* p);

static MooValue json_parse_string(JsonParser* p) {
    // skip opening quote
    p->pos++;
    int start = p->pos;
    // Calculate needed size first (for escape sequences)
    int cap = 64;
    char* buf = (char*)moo_alloc(cap);
    int len = 0;

    while (p->pos < p->len && p->src[p->pos] != '"') {
        char c = p->src[p->pos];
        if (c == '\\') {
            p->pos++;
            if (p->pos >= p->len) break;
            char esc = p->src[p->pos];
            switch (esc) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                default: c = esc; break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char*)moo_realloc(buf, cap);
        }
        buf[len++] = c;
        p->pos++;
    }
    // skip closing quote
    if (p->pos < p->len) p->pos++;

    buf[len] = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

static MooValue json_parse_number(JsonParser* p) {
    int start = p->pos;
    if (p->src[p->pos] == '-') p->pos++;
    while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') p->pos++;
    if (p->pos < p->len && p->src[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') p->pos++;
    }
    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-')) p->pos++;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9') p->pos++;
    }
    char tmp[64];
    int n = p->pos - start;
    if (n >= 63) n = 63;
    memcpy(tmp, p->src + start, n);
    tmp[n] = '\0';
    return moo_number(atof(tmp));
}

static MooValue json_parse_array(JsonParser* p) {
    p->pos++; // skip '['
    MooValue list = moo_list_new(4);
    json_skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == ']') {
        p->pos++;
        return list;
    }
    while (1) {
        MooValue val = json_parse_value(p);
        moo_list_append(list, val);
        json_skip_ws(p);
        if (p->pos >= p->len) break;
        if (p->src[p->pos] == ',') { p->pos++; continue; }
        if (p->src[p->pos] == ']') { p->pos++; break; }
        break;
    }
    return list;
}

static MooValue json_parse_object(JsonParser* p) {
    p->pos++; // skip '{'
    MooValue dict = moo_dict_new();
    json_skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == '}') {
        p->pos++;
        return dict;
    }
    while (1) {
        json_skip_ws(p);
        if (p->pos >= p->len || p->src[p->pos] != '"') break;
        MooValue key = json_parse_string(p);
        json_skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ':') p->pos++;
        MooValue val = json_parse_value(p);
        moo_dict_set(dict, key, val);
        json_skip_ws(p);
        if (p->pos >= p->len) break;
        if (p->src[p->pos] == ',') { p->pos++; continue; }
        if (p->src[p->pos] == '}') { p->pos++; break; }
        break;
    }
    return dict;
}

static MooValue json_parse_value(JsonParser* p) {
    json_skip_ws(p);
    if (p->pos >= p->len) return moo_none();
    char c = p->src[p->pos];
    if (c == '"') return json_parse_string(p);
    if (c == '{') return json_parse_object(p);
    if (c == '[') return json_parse_array(p);
    if (c == '-' || (c >= '0' && c <= '9')) return json_parse_number(p);
    if (strncmp(p->src + p->pos, "true", 4) == 0) { p->pos += 4; return moo_bool(true); }
    if (strncmp(p->src + p->pos, "false", 5) == 0) { p->pos += 5; return moo_bool(false); }
    if (strncmp(p->src + p->pos, "null", 4) == 0) { p->pos += 4; return moo_none(); }
    return moo_error("JSON parse error");
}

MooValue moo_json_parse(MooValue json_string) {
    if (json_string.tag != MOO_STRING) return moo_error("json_parse: argument must be string");
    MooString* s = MV_STR(json_string);
    JsonParser parser = { s->chars, 0, s->length };
    return json_parse_value(&parser);
}

// === JSON serializer ===

typedef struct {
    char* buf;
    int len;
    int cap;
} JsonBuf;

static void jbuf_init(JsonBuf* b) {
    b->cap = 128;
    b->buf = (char*)moo_alloc(b->cap);
    b->len = 0;
}

static void jbuf_push(JsonBuf* b, const char* s, int n) {
    while (b->len + n >= b->cap) {
        b->cap *= 2;
        b->buf = (char*)moo_realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
}

static void jbuf_str(JsonBuf* b, const char* s) {
    jbuf_push(b, s, strlen(s));
}

static void jbuf_char(JsonBuf* b, char c) {
    jbuf_push(b, &c, 1);
}

static void json_serialize(JsonBuf* b, MooValue v) {
    switch (v.tag) {
        case MOO_NUMBER: {
            char tmp[64];
            double d = MV_NUM(v);
            if (d == (int64_t)d) snprintf(tmp, sizeof(tmp), "%lld", (long long)(int64_t)d);
            else snprintf(tmp, sizeof(tmp), "%g", d);
            jbuf_str(b, tmp);
            break;
        }
        case MOO_STRING: {
            MooString* s = MV_STR(v);
            jbuf_char(b, '"');
            for (int i = 0; i < s->length; i++) {
                char c = s->chars[i];
                switch (c) {
                    case '"': jbuf_str(b, "\\\""); break;
                    case '\\': jbuf_str(b, "\\\\"); break;
                    case '\n': jbuf_str(b, "\\n"); break;
                    case '\t': jbuf_str(b, "\\t"); break;
                    case '\r': jbuf_str(b, "\\r"); break;
                    default: jbuf_char(b, c); break;
                }
            }
            jbuf_char(b, '"');
            break;
        }
        case MOO_BOOL:
            jbuf_str(b, MV_BOOL(v) ? "true" : "false");
            break;
        case MOO_NONE:
            jbuf_str(b, "null");
            break;
        case MOO_LIST: {
            MooList* l = MV_LIST(v);
            jbuf_char(b, '[');
            for (int i = 0; i < l->length; i++) {
                if (i > 0) jbuf_char(b, ',');
                json_serialize(b, l->items[i]);
            }
            jbuf_char(b, ']');
            break;
        }
        case MOO_DICT: {
            MooDict* d = MV_DICT(v);
            jbuf_char(b, '{');
            int first = 1;
            for (int i = 0; i < d->capacity; i++) {
                if (!d->entries[i].occupied) continue;
                if (!first) jbuf_char(b, ',');
                first = 0;
                jbuf_char(b, '"');
                MooString* key = d->entries[i].key;
                for (int j = 0; j < key->length; j++) {
                    char c = key->chars[j];
                    if (c == '"') jbuf_str(b, "\\\"");
                    else if (c == '\\') jbuf_str(b, "\\\\");
                    else jbuf_char(b, c);
                }
                jbuf_char(b, '"');
                jbuf_char(b, ':');
                json_serialize(b, d->entries[i].value);
            }
            jbuf_char(b, '}');
            break;
        }
        default:
            jbuf_str(b, "null");
            break;
    }
}

MooValue moo_json_string(MooValue value) {
    JsonBuf buf;
    jbuf_init(&buf);
    json_serialize(&buf, value);
    jbuf_char(&buf, '\0');
    MooValue result = moo_string_new(buf.buf);
    moo_free(buf.buf);
    return result;
}
