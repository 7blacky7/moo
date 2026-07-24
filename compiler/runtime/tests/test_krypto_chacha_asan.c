#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "moo_krypto.h"

static int fails = 0;
#define CHECK(c, n) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", (n)); fails++; } } while (0)

static int hx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static size_t unhex(const char *s, uint8_t *out) {
    size_t n = 0;
    while (*s) {
        int a = hx(*s++), b = hx(*s++);
        if (a < 0 || b < 0) return 0;
        out[n++] = (uint8_t)((a << 4) | b);
    }
    return n;
}

int main(void) {
    uint8_t key[32], nonce[12], out[256], exp[256], tag[16], exp_tag[16];

    unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key);
    unhex("000000090000004a00000000", nonce);
    unhex("10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4ed2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e", exp);
    moo_krypto_chacha20_block(key, 1, nonce, out);
    CHECK(memcmp(out, exp, 64) == 0, "RFC8439 ChaCha20 block");

    uint8_t pkey[32];
    unhex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b", pkey);
    const uint8_t pmsg[] = "Cryptographic Forum Research Group";
    unhex("a8061dc1305136c6c22b8baf0c0127a9", exp_tag);
    moo_krypto_poly1305(pkey, pmsg, sizeof(pmsg)-1, tag);
    CHECK(memcmp(tag, exp_tag, 16) == 0, "RFC8439 Poly1305");

    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(0x80 + i);
    unhex("000000000001020304050607", nonce);
    unhex("8ad5a08b905f81cc815040274ab29471a833b637e3fd0da508dbb8e2fdd1a646", exp);
    moo_krypto_chacha20_poly1305_keygen(key, nonce, out);
    CHECK(memcmp(out, exp, 32) == 0, "RFC8439 Poly1305 keygen");

    unhex("070000004041424344454647", nonce);
    const uint8_t aad[] = {0x50,0x51,0x52,0x53,0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7};
    const uint8_t pt[] = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    const char *ct_hex =
      "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
      "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
      "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
      "3ff4def08e4b7a9de576d26586cec64b6116";
    size_t pt_n = sizeof(pt)-1;
    unhex(ct_hex, exp);
    unhex("1ae10b594f09e26a7e902ecbd0600691", exp_tag);
    moo_krypto_chacha20_poly1305_encrypt(key, nonce, aad, sizeof(aad), pt, pt_n, out, tag);
    CHECK(memcmp(out, exp, pt_n) == 0, "RFC8439 AEAD ciphertext");
    CHECK(memcmp(tag, exp_tag, 16) == 0, "RFC8439 AEAD tag");

    uint8_t dec[256];
    memset(dec, 0xA5, sizeof(dec));
    CHECK(moo_krypto_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad), out, pt_n, tag, dec) == 0,
          "AEAD decrypt ok");
    CHECK(memcmp(dec, pt, pt_n) == 0, "AEAD roundtrip");

    uint8_t bad[16]; memcpy(bad, tag, 16); bad[0] ^= 1;
    memset(dec, 0xA5, pt_n);
    CHECK(moo_krypto_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad), out, pt_n, bad, dec) == -1,
          "AEAD bad tag rejected");
    int untouched = 1; for (size_t i = 0; i < pt_n; i++) if (dec[i] != 0xA5) untouched = 0;
    CHECK(untouched, "AEAD fail-closed plaintext untouched");

    printf("test_krypto_chacha_asan: alle %d Checks bestanden\n", 9 - fails);
    return fails ? 1 : 0;
}
