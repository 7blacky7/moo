/* NETZK-4: Moo-Strings sind laengenmarkierte, binaersichere Bytepuffer.
 * Kein Wrapper verwendet strlen() fuer Key, Nonce, AAD oder Nutzdaten. */
#include "moo_runtime.h"
#include "moo_krypto.h"

static int krypto_string(MooValue v, const uint8_t** p, size_t* n) {
    if (v.tag != MOO_STRING) return 0;
    *p = (const uint8_t*)MV_STR(v)->chars;
    *n = (size_t)MV_STR(v)->length;
    return 1;
}

static int krypto_art(MooValue art, int* ist_aes) {
    if (art.tag != MOO_STRING) return 0;
    const char* s = MV_STR(art)->chars;
    if (strcmp(s, "aes_gcm") == 0 || strcmp(s, "aes-gcm") == 0) {
        *ist_aes = 1;
        return 1;
    }
    if (strcmp(s, "chacha20") == 0 || strcmp(s, "chacha20_poly1305") == 0 ||
        strcmp(s, "chacha20-poly1305") == 0) {
        *ist_aes = 0;
        return 1;
    }
    return 0;
}

MooValue moo_krypto_hkdf_api(MooValue salz, MooValue ikm, MooValue info, MooValue laenge) {
    const uint8_t *sp, *ip, *fp;
    size_t sn, in, fn;
    if (!krypto_string(salz, &sp, &sn) || !krypto_string(ikm, &ip, &in) ||
        !krypto_string(info, &fp, &fn) || laenge.tag != MOO_NUMBER)
        return moo_error("hkdf_sha256: salz, ikm und info muessen Byte-Strings sein; laenge muss Zahl sein");
    double d = MV_NUM(laenge);
    if (!isfinite(d) || d < 1.0 || d > 8160.0 || d != floor(d))
        return moo_error("hkdf_sha256: laenge muss ganzzahlig 1..8160 sein");
    size_t n = (size_t)d;
    uint8_t* out = (uint8_t*)malloc(n);
    if (!out) return moo_error("hkdf_sha256: Speicherreservierung fehlgeschlagen");
    if (moo_krypto_hkdf_sha256(sp, sn, ip, in, fp, fn, out, n) != 0) {
        free(out);
        return moo_error("hkdf_sha256: Ausgabelaenge ungueltig");
    }
    MooValue r = moo_string_new_len((const char*)out, (int32_t)n);
    memset(out, 0, n);
    free(out);
    return r;
}

MooValue moo_krypto_aead_encrypt_api(MooValue art, MooValue key, MooValue nonce,
                                     MooValue aad, MooValue klartext) {
    const uint8_t *kp, *np, *ap, *pp;
    size_t kn, nn, an, pn;
    int aes;
    if (!krypto_art(art, &aes) || !krypto_string(key, &kp, &kn) ||
        !krypto_string(nonce, &np, &nn) || !krypto_string(aad, &ap, &an) ||
        !krypto_string(klartext, &pp, &pn))
        return moo_error("aead_verschluessle: art/key/nonce/aad/klartext ungueltig");
    if (nn != 12) return moo_error("aead_verschluessle: Nonce muss exakt 12 Byte lang sein");
    if ((aes && kn != 16 && kn != 32) || (!aes && kn != 32))
        return moo_error("aead_verschluessle: AES-Key 16/32 Byte, ChaCha-Key 32 Byte");
    if (pn > INT32_MAX - 16) return moo_error("aead_verschluessle: Klartext zu gross");
    uint8_t* out = (uint8_t*)malloc(pn + 16);
    if (!out) return moo_error("aead_verschluessle: Speicherreservierung fehlgeschlagen");
    if (aes) {
        MooAesCtx ctx;
        if (moo_krypto_aes_init(&ctx, kp, (int)(kn * 8)) != 0) { free(out); return moo_error("aead_verschluessle: AES-Key ungueltig"); }
        moo_krypto_aes_gcm_encrypt(&ctx, np, nn, ap, an, pp, pn, out, out + pn, 16);
        memset(&ctx, 0, sizeof(ctx));
    } else {
        moo_krypto_chacha20_poly1305_encrypt(kp, np, ap, an, pp, pn, out, out + pn);
    }
    MooValue r = moo_string_new_len((const char*)out, (int32_t)(pn + 16));
    memset(out, 0, pn + 16);
    free(out);
    return r;
}

MooValue moo_krypto_aead_decrypt_api(MooValue art, MooValue key, MooValue nonce,
                                     MooValue aad, MooValue blob) {
    const uint8_t *kp, *np, *ap, *bp;
    size_t kn, nn, an, bn;
    int aes, ok;
    if (!krypto_art(art, &aes) || !krypto_string(key, &kp, &kn) ||
        !krypto_string(nonce, &np, &nn) || !krypto_string(aad, &ap, &an) ||
        !krypto_string(blob, &bp, &bn))
        return moo_error("aead_entschluessle: art/key/nonce/aad/blob ungueltig");
    if (nn != 12 || bn < 16) return moo_error("aead_entschluessle: Nonce 12 Byte und Blob mindestens 16 Byte");
    if ((aes && kn != 16 && kn != 32) || (!aes && kn != 32))
        return moo_error("aead_entschluessle: AES-Key 16/32 Byte, ChaCha-Key 32 Byte");
    size_t pn = bn - 16;
    uint8_t* out = (uint8_t*)malloc(pn ? pn : 1);
    if (!out) return moo_error("aead_entschluessle: Speicherreservierung fehlgeschlagen");
    if (aes) {
        MooAesCtx ctx;
        if (moo_krypto_aes_init(&ctx, kp, (int)(kn * 8)) != 0) { free(out); return moo_error("aead_entschluessle: AES-Key ungueltig"); }
        ok = moo_krypto_aes_gcm_decrypt(&ctx, np, nn, ap, an, bp, pn, bp + pn, 16, out);
        memset(&ctx, 0, sizeof(ctx));
    } else {
        ok = moo_krypto_chacha20_poly1305_decrypt(kp, np, ap, an, bp, pn, bp + pn, out);
    }
    if (ok != 0) { memset(out, 0, pn); free(out); return moo_error("aead_entschluessle: Authentifizierung fehlgeschlagen"); }
    MooValue r = moo_string_new_len((const char*)out, (int32_t)pn);
    memset(out, 0, pn);
    free(out);
    return r;
}

MooValue moo_krypto_x25519_api(MooValue skalar, MooValue u) {
    const uint8_t *sp, *up;
    size_t sn, un;
    if (!krypto_string(skalar, &sp, &sn) || !krypto_string(u, &up, &un) || sn != 32 || un != 32)
        return moo_error("x25519: skalar und u muessen exakt 32 Byte lang sein");
    uint8_t out[32];
    if (moo_krypto_x25519(out, sp, up) != 0) return moo_error("x25519: Punkt kleiner Ordnung / Null-Secret abgelehnt");
    MooValue r = moo_string_new_len((const char*)out, 32);
    memset(out, 0, sizeof(out));
    return r;
}

MooValue moo_krypto_x25519_public_api(MooValue skalar) {
    const uint8_t* sp;
    size_t sn;
    if (!krypto_string(skalar, &sp, &sn) || sn != 32)
        return moo_error("x25519_oeffentlich: skalar muss exakt 32 Byte lang sein");
    uint8_t out[32];
    if (moo_krypto_x25519_basis(out, sp) != 0) return moo_error("x25519_oeffentlich: ungueltiger Skalar");
    MooValue r = moo_string_new_len((const char*)out, 32);
    memset(out, 0, sizeof(out));
    return r;
}
