#include "moo_runtime.h"
/* P013: /dev/urandom ist POSIX — Windows nutzt BCryptGenRandom (CSPRNG).
 * fcntl.h/unistd.h existieren unter MSVC nicht (CI C1083). */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

// ============================================================
// SHA-256 Implementation
// ============================================================

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
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
#ifdef _WIN32
    /* System-CSPRNG — kryptographisch aequivalent zu /dev/urandom.
     * Kein rand()-Fallback: das waere fuer secure_random ein Sicherheits-Hack. */
    if (BCryptGenRandom(NULL, buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        free(buf);
        return moo_error("secure_random: BCryptGenRandom fehlgeschlagen");
    }
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) { free(buf); return moo_error("secure_random: /dev/urandom nicht verfuegbar"); }
    ssize_t rd = read(fd, buf, (size_t)n);
    close(fd);
    if (rd != n) { free(buf); return moo_error("secure_random: Lesefehler"); }
#endif
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
    // Worst case: every char becomes &#x27;/&quot; (6 chars).
    // P007-U3: s->length*6 wurde frueher als int gerechnet (signed-Overflow-UB
    // bei length>~357M, dann negativer/zu kleiner malloc). Produkt in int64
    // pruefen; bei Ueberlauf sauberer moo-Fehler statt Crash.
    size_t cap = (size_t)moo_checked_mul_i32(s->length, 6, "sanitize_html") + 1;
    // moo_throw kehrt im try-Kontext zurueck -> nach dem Wurf nicht weiterlaufen.
    if (moo_error_flag) return moo_string_new("");
    char *out = (char*)malloc(cap);
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
    // Worst case: every ' becomes '' (double), -- becomes removed.
    // P007-U3: s->length*2 wurde frueher als int gerechnet (signed-Overflow-UB
    // bei length>~1.07G). Produkt in int64 pruefen; bei Ueberlauf moo-Fehler.
    size_t cap = (size_t)moo_checked_mul_i32(s->length, 2, "sanitize_sql") + 1;
    // moo_throw kehrt im try-Kontext zurueck -> nach dem Wurf nicht weiterlaufen.
    if (moo_error_flag) return moo_string_new("");
    char *out = (char*)malloc(cap);
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


// ============================================================
// SHA-256 Raw Bytes (32-Byte Output, fuer HMAC/PBKDF2-Chaining)
// ============================================================
MooValue moo_sha256_bytes(MooValue input) {
    if (input.tag != MOO_STRING) return moo_error("sha256_bytes: String erwartet");
    MooString *s = MV_STR(input);
    uint8_t hash[32];
    sha256_hash((const uint8_t*)s->chars, (size_t)s->length, hash);
    return moo_string_new_len((const char*)hash, 32);
}

// ============================================================
// HMAC-SHA-256 (RFC 2104)
// ============================================================
static void hmac_sha256_raw(const uint8_t *key, size_t key_len,
                            const uint8_t *msg, size_t msg_len,
                            uint8_t out[32]) {
    uint8_t k_pad[64];
    uint8_t key_hash[32];
    if (key_len > 64) {
        sha256_hash(key, key_len, key_hash);
        memcpy(k_pad, key_hash, 32);
        memset(k_pad + 32, 0, 32);
    } else {
        memcpy(k_pad, key, key_len);
        if (key_len < 64) memset(k_pad + key_len, 0, 64 - key_len);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k_pad[i] ^ 0x36;
        opad[i] = k_pad[i] ^ 0x5c;
    }
    uint8_t *inner_buf = (uint8_t*)malloc(64 + msg_len);
    memcpy(inner_buf, ipad, 64);
    if (msg_len > 0) memcpy(inner_buf + 64, msg, msg_len);
    uint8_t inner_hash[32];
    sha256_hash(inner_buf, 64 + msg_len, inner_hash);
    free(inner_buf);
    uint8_t outer_buf[96];
    memcpy(outer_buf, opad, 64);
    memcpy(outer_buf + 64, inner_hash, 32);
    sha256_hash(outer_buf, 96, out);
}

MooValue moo_hmac_sha256(MooValue key, MooValue msg) {
    if (key.tag != MOO_STRING) return moo_error("hmac_sha256: key (String) erwartet");
    if (msg.tag != MOO_STRING) return moo_error("hmac_sha256: msg (String) erwartet");
    MooString *k = MV_STR(key);
    MooString *m = MV_STR(msg);
    uint8_t out[32];
    hmac_sha256_raw((const uint8_t*)k->chars, (size_t)k->length,
                    (const uint8_t*)m->chars, (size_t)m->length, out);
    return moo_string_new_len((const char*)out, 32);
}

// ============================================================
// PBKDF2-HMAC-SHA-256 (RFC 8018, Section 5.2)
// ============================================================
MooValue moo_pbkdf2_sha256(MooValue password, MooValue salt,
                           MooValue iterations, MooValue dk_len) {
    if (password.tag != MOO_STRING) return moo_error("pbkdf2_sha256: password (String) erwartet");
    if (salt.tag != MOO_STRING) return moo_error("pbkdf2_sha256: salt (String) erwartet");
    if (iterations.tag != MOO_NUMBER) return moo_error("pbkdf2_sha256: iterations (Zahl) erwartet");
    if (dk_len.tag != MOO_NUMBER) return moo_error("pbkdf2_sha256: dk_len (Zahl) erwartet");
    int iter = (int)MV_NUM(iterations);
    int dk = (int)MV_NUM(dk_len);
    if (iter < 1 || iter > 10000000) return moo_error("pbkdf2_sha256: iterations 1..1e7");
    if (dk < 1 || dk > 1024) return moo_error("pbkdf2_sha256: dk_len 1..1024");
    MooString *pw = MV_STR(password);
    MooString *s = MV_STR(salt);
    const int hLen = 32;
    int blocks = (dk + hLen - 1) / hLen;
    uint8_t *out = (uint8_t*)malloc((size_t)(blocks * hLen));
    uint8_t *salt_buf = (uint8_t*)malloc((size_t)(s->length + 4));
    if (!out || !salt_buf) {
        free(out); free(salt_buf);
        return moo_error("pbkdf2_sha256: malloc fehlgeschlagen");
    }
    memcpy(salt_buf, s->chars, (size_t)s->length);
    uint8_t U[32], T[32];
    for (int i = 1; i <= blocks; i++) {
        salt_buf[s->length + 0] = (uint8_t)((i >> 24) & 0xff);
        salt_buf[s->length + 1] = (uint8_t)((i >> 16) & 0xff);
        salt_buf[s->length + 2] = (uint8_t)((i >> 8) & 0xff);
        salt_buf[s->length + 3] = (uint8_t)(i & 0xff);
        hmac_sha256_raw((const uint8_t*)pw->chars, (size_t)pw->length,
                        salt_buf, (size_t)(s->length + 4), U);
        memcpy(T, U, 32);
        for (int j = 1; j < iter; j++) {
            hmac_sha256_raw((const uint8_t*)pw->chars, (size_t)pw->length,
                            U, 32, U);
            for (int k = 0; k < 32; k++) T[k] ^= U[k];
        }
        memcpy(out + (i - 1) * hLen, T, 32);
    }
    free(salt_buf);
    MooValue result = moo_string_new_len((const char*)out, dk);
    free(out);
    return result;
}


// ============================================================
// AES-256 (FIPS-197) — Block-Cipher-Kern
// Nur Encrypt noetig: der CTR-Modus nutzt AES ausschliesslich als
// Keystream-Generator, Ver- und Entschluesselung sind identisch.
// State-Layout column-major: Byte-Index = row + 4*col.
// ============================================================

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

#define AES256_NR 14
#define AES256_RK_BYTES 240  /* (Nr+1)*16 = 15*16 */

/* Key-Expansion AES-256 (Nk=8, Nr=14) -> 60 Words = 240 Bytes Round-Keys. */
void moo_aes256_key_expansion(const uint8_t key[32], uint8_t rk[AES256_RK_BYTES]) {
    static const uint8_t rcon[7] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40};
    memcpy(rk, key, 32);
    int bytes = 32;
    int rcon_i = 0;
    uint8_t t[4];
    while (bytes < AES256_RK_BYTES) {
        memcpy(t, rk + bytes - 4, 4);
        if (bytes % 32 == 0) {
            /* RotWord + SubWord + Rcon */
            uint8_t tmp = t[0];
            t[0] = (uint8_t)(aes_sbox[t[1]] ^ rcon[rcon_i++]);
            t[1] = aes_sbox[t[2]];
            t[2] = aes_sbox[t[3]];
            t[3] = aes_sbox[tmp];
        } else if (bytes % 32 == 16) {
            /* SubWord (AES-256-spezifischer Zwischenschritt) */
            t[0] = aes_sbox[t[0]];
            t[1] = aes_sbox[t[1]];
            t[2] = aes_sbox[t[2]];
            t[3] = aes_sbox[t[3]];
        }
        for (int i = 0; i < 4; i++)
            rk[bytes + i] = (uint8_t)(rk[bytes - 32 + i] ^ t[i]);
        bytes += 4;
    }
}

static uint8_t aes_xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ (uint8_t)((x >> 7) * 0x1b));
}

/* Ein AES-256-Block (16 Byte) verschluesseln. */
void moo_aes256_encrypt_block(const uint8_t rk[AES256_RK_BYTES],
                              const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);
    for (int i = 0; i < 16; i++) s[i] ^= rk[i];            /* AddRoundKey Runde 0 */
    for (int round = 1; round <= AES256_NR; round++) {
        for (int i = 0; i < 16; i++) s[i] = aes_sbox[s[i]]; /* SubBytes */
        uint8_t tmp[16];                                    /* ShiftRows */
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                tmp[r + 4*c] = s[r + 4*((c + r) & 3)];
        memcpy(s, tmp, 16);
        if (round != AES256_NR) {                           /* MixColumns */
            for (int c = 0; c < 4; c++) {
                uint8_t *col = s + 4*c;
                uint8_t a0=col[0], a1=col[1], a2=col[2], a3=col[3];
                col[0] = (uint8_t)(aes_xtime(a0) ^ (aes_xtime(a1)^a1) ^ a2 ^ a3);
                col[1] = (uint8_t)(a0 ^ aes_xtime(a1) ^ (aes_xtime(a2)^a2) ^ a3);
                col[2] = (uint8_t)(a0 ^ a1 ^ aes_xtime(a2) ^ (aes_xtime(a3)^a3));
                col[3] = (uint8_t)((aes_xtime(a0)^a0) ^ a1 ^ a2 ^ aes_xtime(a3));
            }
        }
        for (int i = 0; i < 16; i++) s[i] ^= rk[round*16 + i]; /* AddRoundKey */
    }
    memcpy(out, s, 16);
}

/* AES-256-CTR: XOR-t buf[0..len) in-place mit dem Keystream (iv = 128-Bit
 * Initial-Counter, big-endian inkrementiert). */
static void aes256_ctr_xor(const uint8_t rk[AES256_RK_BYTES],
                           const uint8_t iv[16], uint8_t *buf, size_t len) {
    uint8_t counter[16], ks[16];
    memcpy(counter, iv, 16);
    size_t off = 0;
    while (off < len) {
        moo_aes256_encrypt_block(rk, counter, ks);
        size_t n = len - off; if (n > 16) n = 16;
        for (size_t i = 0; i < n; i++) buf[off + i] ^= ks[i];
        off += n;
        for (int i = 15; i >= 0; i--) { if (++counter[i]) break; } /* 128-Bit BE +1 */
    }
    memset(ks, 0, sizeof ks);
}

/* Konstantzeit-Vergleich (verhindert Timing-Leak bei MAC-Pruefung). 0 == gleich. */
static int aes_ct_memcmp(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
    return d;
}

/* CSPRNG-Bytes fuer den IV (roh, im Gegensatz zu moo_secure_random das hex liefert). */
static int aes_random_iv(uint8_t *buf, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r <= 0) { close(fd); return -1; }
        got += (size_t)r;
    }
    close(fd);
    return 0;
#endif
}

// ============================================================
// AES-256 authentifizierte Verschluesselung (Encrypt-then-MAC)
// Container: IV(16) || ciphertext || HMAC-SHA256(32)
// schluessel: exakt 32 Byte. mac_key = sha256(schluessel) (Domain-Trennung
// von der CTR-Verschluesselung mit demselben Master-Key).
// ============================================================

MooValue moo_aes_encrypt(MooValue key, MooValue plaintext) {
    if (key.tag != MOO_STRING) return moo_error("aes_verschluessle: schluessel (32-Byte String) erwartet");
    if (plaintext.tag != MOO_STRING) return moo_error("aes_verschluessle: klartext (String) erwartet");
    MooString *k = MV_STR(key);
    MooString *p = MV_STR(plaintext);
    if (k->length != 32) return moo_error("aes_verschluessle: schluessel muss genau 32 Byte sein");
    size_t pl = (size_t)p->length;
    size_t total = 16 + pl + 32;
    uint8_t *out = (uint8_t*)malloc(total);
    if (!out) return moo_error("aes_verschluessle: malloc fehlgeschlagen");
    if (aes_random_iv(out, 16) != 0) { free(out); return moo_error("aes_verschluessle: RNG fehlgeschlagen"); }
    uint8_t rk[AES256_RK_BYTES];
    moo_aes256_key_expansion((const uint8_t*)k->chars, rk);
    uint8_t mac_key[32];
    sha256_hash((const uint8_t*)k->chars, 32, mac_key);
    if (pl) memcpy(out + 16, p->chars, pl);
    aes256_ctr_xor(rk, out, out + 16, pl);
    hmac_sha256_raw(mac_key, 32, out, 16 + pl, out + 16 + pl);
    MooValue result = moo_string_new_len((const char*)out, (int)total);
    memset(rk, 0, sizeof rk);
    memset(mac_key, 0, sizeof mac_key);
    free(out);
    return result;
}

MooValue moo_aes_decrypt(MooValue key, MooValue blob) {
    if (key.tag != MOO_STRING) return moo_error("aes_entschluessle: schluessel (32-Byte String) erwartet");
    if (blob.tag != MOO_STRING) return moo_error("aes_entschluessle: daten (String) erwartet");
    MooString *k = MV_STR(key);
    MooString *b = MV_STR(blob);
    if (k->length != 32) return moo_error("aes_entschluessle: schluessel muss genau 32 Byte sein");
    if (b->length < 16 + 32) return moo_error("aes_entschluessle: daten zu kurz oder beschaedigt");
    size_t cl = (size_t)b->length - 16 - 32;
    const uint8_t *iv  = (const uint8_t*)b->chars;
    const uint8_t *ct  = (const uint8_t*)b->chars + 16;
    const uint8_t *mac = (const uint8_t*)b->chars + 16 + cl;
    uint8_t mac_key[32], calc[32];
    sha256_hash((const uint8_t*)k->chars, 32, mac_key);
    hmac_sha256_raw(mac_key, 32, (const uint8_t*)b->chars, 16 + cl, calc);
    if (aes_ct_memcmp(calc, mac, 32) != 0) {
        memset(mac_key, 0, 32); memset(calc, 0, 32);
        return moo_error("aes_entschluessle: Authentifizierung fehlgeschlagen (falscher Schluessel oder manipulierte Daten)");
    }
    uint8_t rk[AES256_RK_BYTES];
    moo_aes256_key_expansion((const uint8_t*)k->chars, rk);
    uint8_t *out = (uint8_t*)malloc(cl ? cl : 1);
    if (!out) { memset(rk, 0, sizeof rk); memset(mac_key, 0, 32); return moo_error("aes_entschluessle: malloc fehlgeschlagen"); }
    if (cl) memcpy(out, ct, cl);
    aes256_ctr_xor(rk, iv, out, cl);
    MooValue result = moo_string_new_len((const char*)out, (int)cl);
    memset(rk, 0, sizeof rk);
    memset(mac_key, 0, sizeof mac_key);
    free(out);
    return result;
}