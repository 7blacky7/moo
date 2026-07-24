#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "moo_krypto.h"

static int fails = 0;
#define CHECK(c, n) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", (n)); fails++; } } while (0)
static int hx(char c) { if (c>='0'&&c<='9') return c-'0'; if (c>='a'&&c<='f') return c-'a'+10; if (c>='A'&&c<='F') return c-'A'+10; return -1; }
static size_t unhex(const char *s, uint8_t *o) { size_t n=0; while (*s) { int a=hx(*s++), b=hx(*s++); if(a<0||b<0)return 0; o[n++]=(uint8_t)((a<<4)|b); } return n; }
static int all_zero(const uint8_t *p, size_t n) { uint8_t x=0; for(size_t i=0;i<n;i++) x|=p[i]; return x==0; }

static void vector(const char *skh, const char *uh, const char *eh, const char *name) {
    uint8_t sk[32], u[32], exp[32], out[32];
    unhex(skh, sk); unhex(uh, u); unhex(eh, exp);
    CHECK(moo_krypto_x25519(out, sk, u) == 0, name);
    CHECK(memcmp(out, exp, 32) == 0, name);
}

int main(void) {
    vector("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
           "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
           "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552",
           "RFC7748 vector 1");
    vector("4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
           "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493",
           "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957",
           "RFC7748 vector 2");

    uint8_t ask[32], apk[32], bsk[32], bpk[32], shared[32], out[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", ask);
    unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", apk);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bsk);
    unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bpk);
    unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", shared);
    CHECK(moo_krypto_x25519_basis(out, ask) == 0 && memcmp(out, apk, 32) == 0, "Alice public key");
    CHECK(moo_krypto_x25519_basis(out, bsk) == 0 && memcmp(out, bpk, 32) == 0, "Bob public key");
    CHECK(moo_krypto_x25519(out, ask, bpk) == 0 && memcmp(out, shared, 32) == 0, "Alice shared secret");
    CHECK(moo_krypto_x25519(out, bsk, apk) == 0 && memcmp(out, shared, 32) == 0, "Bob shared secret");

    const char *low[] = {
      "0000000000000000000000000000000000000000000000000000000000000000",
      "0100000000000000000000000000000000000000000000000000000000000000",
      "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800",
      "5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157",
      "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
      "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
      "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"
    };
    for (size_t i=0;i<sizeof(low)/sizeof(low[0]);i++) {
        uint8_t u[32]; unhex(low[i], u); memset(out, 0xA5, 32);
        CHECK(moo_krypto_x25519(out, ask, u) == -1, "low-order rejected");
        CHECK(all_zero(out, 32), "low-order output zeroed");
    }

    printf("test_krypto_x25519_asan: alle %d Checks bestanden\n", 22 - fails);
    return fails ? 1 : 0;
}
