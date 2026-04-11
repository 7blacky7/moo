#include "moo_runtime.h"
#include <fcntl.h>
#include <unistd.h>

// ============================================================
// SHA-256 Implementation
// ============================================================

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f11f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define RR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z) (((x)&(y))^((~(x))&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (RR(x,2)^RR(x,13)^RR(x,22))
#define EP1(x) (RR(x,6)^RR(x,11)^RR(x,25))
#define SIG0(x) (RR(x,7)^RR(x,18)^((x)>>3))
#define SIG1(x) (RR(x,17)^RR(x,19)^((x)>>10))

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|
               ((uint32_t)block[i*4+2]<<8)|((uint32_t)block[i*4+3]);
    for (int i = 16; i < 64; i++)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];
    a=state[0]; b=state[1]; c=state[2]; d=state[3];
    e=state[4]; f=state[5]; g=state[6]; h=state[7];
    for (int i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + sha256_k[i] + w[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha256_hash(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t state[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    uint8_t block[64];
    size_t i = 0;
    // Process full blocks
    for (; i + 64 <= len; i += 64)
        sha256_transform(state, data + i);
    // Padding
    size_t rem = len - i;
    memset(block, 0, 64);
    memcpy(block, data + i, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        sha256_transform(state, block);
        memset(block, 0, 64);
    }
    uint64_t bits = (uint64_t)len * 8;
    for (int j = 0; j < 8; j++)
        block[56 + j] = (uint8_t)(bits >> (56 - j * 8));
    sha256_transform(state, block);
    for (int j = 0; j < 8; j++) {
        out[j*4+0] = (uint8_t)(state[j]>>24);
        out[j*4+1] = (uint8_t)(state[j]>>16);
        out[j*4+2] = (uint8_t)(state[j]>>8);
        out[j*4+3] = (uint8_t)(state[j]);
    }
}

MooValue moo_sha256(MooValue input) {
    if (input.tag != MOO_STRING) return moo_error("sha256: String erwartet");
    MooString *s = MV_STR(input);
    uint8_t hash[32];
    sha256_hash((const uint8_t*)s->chars, (size_t)s->length, hash);
    char hex[65];
    for (int i = 0; i < 32; i++)
        sprintf(hex + i*2, "%02x", hash[i]);
    hex[64] = '\0';
    return moo_string_new(hex);
}

// ============================================================
// SHA1 (RFC 3174) — fuer WebSocket-Handshake (Sec-WebSocket-Accept) und MySQL native_password
// ============================================================

#define SHA1_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a, b, c, d, e, t, w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)buffer[i*4] << 24) | ((uint32_t)buffer[i*4+1] << 16)
             | ((uint32_t)buffer[i*4+2] << 8) | (uint32_t)buffer[i*4+3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = SHA1_ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];
    for (int i = 0; i < 80; i++) {
        if (i < 20)      t = SHA1_ROL(a, 5) + ((b & c) | ((~b) & d)) + e + w[i] + 0x5A827999;
        else if (i < 40) t = SHA1_ROL(a, 5) + (b ^ c ^ d) + e + w[i] + 0x6ED9EBA1;
        else if (i < 60) t = SHA1_ROL(a, 5) + ((b & c) | (b & d) | (c & d)) + e + w[i] + 0x8F1BBCDC;
        else             t = SHA1_ROL(a, 5) + (b ^ c ^ d) + e + w[i] + 0xCA62C1D6;
        e = d; d = c; c = SHA1_ROL(b, 30); b = a; a = t;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void sha1_hash(const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t state[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint64_t bit_len = (uint64_t)len * 8;
    size_t i = 0;
    for (; i + 64 <= len; i += 64) sha1_transform(state, data + i);
    uint8_t block[64] = {0};
    size_t rem = len - i;
    if (rem) memcpy(block, data + i, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        sha1_transform(state, block);
        memset(block, 0, 64);
    }
    for (int j = 0; j < 8; j++) block[63 - j] = (uint8_t)(bit_len >> (j * 8));
    sha1_transform(state, block);
    for (int j = 0; j < 5; j++) {
        out[j*4]   = (uint8_t)(state[j] >> 24);
        out[j*4+1] = (uint8_t)(state[j] >> 16);
        out[j*4+2] = (uint8_t)(state[j] >> 8);
        out[j*4+3] = (uint8_t)(state[j]);
    }
}

extern MooValue moo_string_new_len(const char* chars, int32_t len);

MooValue moo_sha1(MooValue input) {
    if (input.tag != MOO_STRING) return moo_error("sha1: String erwartet");
    MooString *s = MV_STR(input);
    uint8_t hash[20];
    sha1_hash((const uint8_t*)s->chars, (size_t)s->length, hash);
    char hex[41];
    for (int i = 0; i < 20; i++) sprintf(hex + i*2, "%02x", hash[i]);
    hex[40] = '\0';
    return moo_string_new(hex);
}

// Raw 20-byte SHA1-Output als binary-safe String. Fuer Sec-WebSocket-Accept:
// base64_encode(sha1_bytes(key + GUID))
MooValue moo_sha1_bytes(MooValue input) {
    if (input.tag != MOO_STRING) return moo_error("sha1_bytes: String erwartet");
    MooString *s = MV_STR(input);
    uint8_t hash[20];
    sha1_hash((const uint8_t*)s->chars, (size_t)s->length, hash);
    return moo_string_new_len((const char*)hash, 20);
}

// ============================================================
// Secure Random
// ============================================================

MooValue moo_secure_random(MooValue length) {
    if (length.tag != MOO_NUMBER) return moo_error("secure_random: Zahl erwartet");
    int n = (int)MV_NUM(length);
    if (n <= 0 || n > 1024) return moo_error("secure_random: Laenge 1-1024");
    uint8_t *buf = (uint8_t*)malloc((size_t)n);
    if (!buf) return moo_error("secure_random: malloc fehlgeschlagen");
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) { free(buf); return moo_error("secure_random: /dev/urandom nicht verfuegbar"); }
    ssize_t rd = read(fd, buf, (size_t)n);
    close(fd);
    if (rd != n) { free(buf); return moo_error("secure_random: Lesefehler"); }
    char *hex = (char*)malloc((size_t)(n * 2 + 1));
    for (int i = 0; i < n; i++)
        sprintf(hex + i*2, "%02x", buf[i]);
    hex[n*2] = '\0';
    free(buf);
    MooValue result = moo_string_new(hex);
    free(hex);
    return result;
}

// ============================================================
// Base64
// ============================================================

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

MooValue moo_base64_encode(MooValue input) {
    if (input.tag != MOO_STRING) return moo_error("base64_encode: String erwartet");
    MooString *s = MV_STR(input);
    int len = s->length;
    int out_len = 4 * ((len + 2) / 3);
    char *out = (char*)malloc((size_t)(out_len + 1));
    int j = 0;
    for (int i = 0; i < len; i += 3) {
        uint32_t a = (uint8_t)s->chars[i];
        uint32_t b = (i+1 < len) ? (uint8_t)s->chars[i+1] : 0;
        uint32_t c = (i+2 < len) ? (uint8_t)s->chars[i+2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i+1 < len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = (i+2 < len) ? b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    MooValue result = moo_string_new(out);
    free(out);
    return result;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

MooValue moo_base64_decode(MooValue input) {
    if (input.tag != MOO_STRING) return moo_error("base64_decode: String erwartet");
    MooString *s = MV_STR(input);
    int len = s->length;
    if (len % 4 != 0) return moo_error("base64_decode: ungueltige Laenge");
    int out_len = len / 4 * 3;
    if (len > 0 && s->chars[len-1] == '=') out_len--;
    if (len > 1 && s->chars[len-2] == '=') out_len--;
    char *out = (char*)malloc((size_t)(out_len + 1));
    int j = 0;
    for (int i = 0; i < len; i += 4) {
        int a = b64_decode_char(s->chars[i]);
        int b = b64_decode_char(s->chars[i+1]);
        int c = (s->chars[i+2] == '=') ? 0 : b64_decode_char(s->chars[i+2]);
        int d = (s->chars[i+3] == '=') ? 0 : b64_decode_char(s->chars[i+3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) { free(out); return moo_error("base64_decode: ungueltiges Zeichen"); }
        uint32_t triple = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
        if (j < out_len) out[j++] = (char)((triple >> 16) & 0xFF);
        if (j < out_len) out[j++] = (char)((triple >> 8) & 0xFF);
        if (j < out_len) out[j++] = (char)(triple & 0xFF);
    }
    out[j] = '\0';
    MooValue result = moo_string_new(out);
    free(out);
    return result;
}

// ============================================================
// Input Sanitization
// ============================================================

MooValue moo_sanitize_html(MooValue input) {
    if (input.tag != MOO_STRING) return moo_error("sanitize_html: String erwartet");
    MooString *s = MV_STR(input);
    // Worst case: every char becomes &amp; (5 chars)
    char *out = (char*)malloc((size_t)(s->length * 6 + 1));
    int j = 0;
    for (int i = 0; i < s->length; i++) {
        switch (s->chars[i]) {
            case '<':  memcpy(out+j, "&lt;", 4); j+=4; break;
            case '>':  memcpy(out+j, "&gt;", 4); j+=4; break;
            case '&':  memcpy(out+j, "&amp;", 5); j+=5; break;
            case '"':  memcpy(out+j, "&quot;", 6); j+=6; break;
            case '\'': memcpy(out+j, "&#x27;", 6); j+=6; break;
            default:   out[j++] = s->chars[i]; break;
        }
    }
    out[j] = '\0';
    MooValue result = moo_string_new(out);
    free(out);
    return result;
}

MooValue moo_sanitize_sql(MooValue input) {
    if (input.tag != MOO_STRING) return moo_error("sanitize_sql: String erwartet");
    MooString *s = MV_STR(input);
    // Worst case: every ' becomes '' (double), -- becomes removed
    char *out = (char*)malloc((size_t)(s->length * 2 + 1));
    int j = 0;
    for (int i = 0; i < s->length; i++) {
        if (s->chars[i] == '\'') {
            out[j++] = '\'';
            out[j++] = '\'';
        } else if (s->chars[i] == '-' && i+1 < s->length && s->chars[i+1] == '-') {
            // Skip SQL comment marker
            i++; // skip the second -
        } else {
            out[j++] = s->chars[i];
        }
    }
    out[j] = '\0';
    MooValue result = moo_string_new(out);
    free(out);
    return result;
}
