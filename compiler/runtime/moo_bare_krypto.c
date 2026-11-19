#include "moo_bare_krypto.h"

#include <stdbool.h>
#include <string.h>

enum {
    DRBG_SEED_BYTES = 48,    /* EntropyInput 256 Bit + Nonce 128 Bit */
    DRBG_MAX_REQUESTS = 4096,
    DRBG_MAX_BYTES = 65536,  /* SP 800-90A Rev. 1, Tabelle 2 */
    ENTROPY_SAMPLES = 8,
};

typedef bool (*ReadWord)(void *arg, uint64_t *word);

/* SP 800-90A Rev. 1 10.1.2.2. Nur Instantiate liefert Seed-Material.
 * Generate laeuft ohne Additional Input. */
static void drbg_update(MooBareDrbg *rng, const uint8_t *seed)
{
    uint8_t input[64 + 1 + DRBG_SEED_BYTES];
    unsigned steps = seed ? 2 : 1;
    size_t len = 65 + (seed ? DRBG_SEED_BYTES : 0);
    if (seed)
        memcpy(input + 65, seed, DRBG_SEED_BYTES);
    for (unsigned marker = 0; marker < steps; marker++) {
        memcpy(input, rng->value, sizeof(rng->value));
        input[64] = (uint8_t)marker;
        crypto_sha512_hmac(rng->key, rng->key, sizeof(rng->key), input, len);
        crypto_sha512_hmac(rng->value, rng->key, sizeof(rng->key),
                          rng->value, sizeof(rng->value));
    }
    crypto_wipe(input, sizeof(input));
}

static void drbg_instantiate(MooBareDrbg *rng, const uint8_t seed[DRBG_SEED_BYTES])
{
    memset(rng->key, 0, sizeof(rng->key));
    memset(rng->value, 1, sizeof(rng->value));
    drbg_update(rng, seed);
    rng->requests_left = DRBG_MAX_REQUESTS;
}

int moo_bare_drbg_generate(MooBareDrbg *rng, uint8_t *out, size_t n)
{
    if (rng->requests_left == 0 || n == 0 || n > DRBG_MAX_BYTES)
        return -1;
    while (n > 0) {
        crypto_sha512_hmac(rng->value, rng->key, sizeof(rng->key),
                          rng->value, sizeof(rng->value));
        size_t block = n < sizeof(rng->value) ? n : sizeof(rng->value);
        memcpy(out, rng->value, block);
        out += block;
        n -= block;
    }
    drbg_update(rng, NULL);
    rng->requests_left--;
    return 0;
}

void moo_bare_drbg_wipe(MooBareDrbg *rng)
{
    crypto_wipe(rng, sizeof(*rng));
}

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c)
{
    uint32_t d;
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(d)
                      : "0"(leaf), "2"(0));
}

/* CPUID.01H:ECX[30], CPUID.07H.00H:EBX[18]. Erst Leaf-Grenze pruefen. */
static unsigned entropy_features(void)
{
    uint32_t max_leaf, a, b, c;
    unsigned features = 0;
    cpuid(0, &max_leaf, &b, &c);
    if (max_leaf >= 1) {
        cpuid(1, &a, &b, &c);
        if ((c & (1u << 30)) != 0)
            features |= MOO_ENTROPY_RDRAND;
    }
    if (max_leaf >= 7) {
        cpuid(7, &a, &b, &c);
        if ((b & (1u << 18)) != 0)
            features |= MOO_ENTROPY_RDSEED;
    }
    return features;
}

static bool read_rdseed(void *arg, uint64_t *word)
{
    bool valid;
    (void)arg;
    __asm__ volatile ("rdseed %0" : "=r"(*word), "=@ccc"(valid));
    return valid;
}

static bool read_rdrand(void *arg, uint64_t *word)
{
    bool valid;
    (void)arg;
    __asm__ volatile ("rdrand %0" : "=r"(*word), "=@ccc"(valid));
    return valid;
}

/* Nur Woerter mit CF=1 zaehlen. Acht Samples erkennen auch kurze
 * Wiederholungszyklen. Die ersten zwei dienen nur der Gesundheitsprobe.
 * Das restliche Material wird nur bei vollstaendigem Erfolg kopiert. */
static MooEntropyStatus collect_seed(bool available, ReadWord read_word, void *arg,
                                     unsigned retries, uint8_t seed[DRBG_SEED_BYTES])
{
    uint64_t samples[ENTROPY_SAMPLES];
    MooEntropyStatus status = MOO_ENTROPY_EXHAUSTED;
    if (!available)
        return MOO_ENTROPY_UNSUPPORTED;

    for (size_t i = 0; i < ENTROPY_SAMPLES; i++) {
        bool valid = false;
        for (unsigned attempt = 0; attempt < retries; attempt++) {
            if (read_word(arg, &samples[i])) {
                valid = true;
                break;
            }
            __asm__ volatile ("pause");
        }
        if (!valid)
            goto out;
        for (size_t j = 0; j < i; j++) {
            if (samples[j] == samples[i]) {
                status = MOO_ENTROPY_REPEATED;
                goto out;
            }
        }
    }
    memcpy(seed, samples + 2, DRBG_SEED_BYTES);
    status = MOO_ENTROPY_OK;
out:
    crypto_wipe(samples, sizeof(samples));
    return status;
}

static MooEntropySource select_source(MooEntropyStatus seed, MooEntropyStatus rand)
{
    if (seed == MOO_ENTROPY_REPEATED || rand == MOO_ENTROPY_REPEATED)
        return MOO_ENTROPY_NONE;
    if (seed == MOO_ENTROPY_OK)
        return MOO_ENTROPY_RDSEED;
    if (rand == MOO_ENTROPY_OK)
        return MOO_ENTROPY_RDRAND;
    return MOO_ENTROPY_NONE;
}

int moo_bare_drbg_start(MooBareDrbg *rng, MooEntropyReport *report)
{
    const unsigned features = entropy_features();
    uint8_t seed[DRBG_SEED_BYTES], fallback[DRBG_SEED_BYTES];
    report->rdseed = collect_seed((features & MOO_ENTROPY_RDSEED) != 0, read_rdseed, NULL, 64, seed);
    report->rdrand = collect_seed((features & MOO_ENTROPY_RDRAND) != 0, read_rdrand, NULL, 10, fallback);
    report->source = select_source(report->rdseed, report->rdrand);
    if (report->source == MOO_ENTROPY_NONE)
        moo_bare_drbg_wipe(rng);
    else
        drbg_instantiate(rng, report->source == MOO_ENTROPY_RDSEED ? seed : fallback);
    crypto_wipe(seed, sizeof(seed));
    crypto_wipe(fallback, sizeof(fallback));
    return report->source == MOO_ENTROPY_NONE ? -1 : 0;
}

uint64_t moo_bare_entropie_selbsttest(void)
{
    MooBareDrbg rng;
    MooEntropyReport report;
    uint8_t a[32], b[32];
    bool ready = moo_bare_drbg_start(&rng, &report) == 0 &&
                 moo_bare_drbg_generate(&rng, a, sizeof(a)) == 0 &&
                 moo_bare_drbg_generate(&rng, b, sizeof(b)) == 0 &&
                 crypto_verify32(a, b) != 0;
    moo_bare_drbg_wipe(&rng);
    crypto_wipe(a, sizeof(a));
    crypto_wipe(b, sizeof(b));
    return (uint64_t)report.rdseed | ((uint64_t)report.rdrand << 4) |
           (ready ? (uint64_t)report.source << 8 : 0);
}

typedef enum { TEST_RETRY, TEST_ALTERNATING, TEST_EXHAUSTED } FakeMode;

typedef struct {
    unsigned calls;
    FakeMode mode;
} FakeWords;

static bool fake_read(void *arg, uint64_t *word)
{
    FakeWords *fake = arg;
    const unsigned attempt = ++fake->calls;
    if (fake->mode == TEST_EXHAUSTED)
        return false;
    if (fake->mode == TEST_ALTERNATING) {
        *word = attempt & 1;
        return true;
    }
    *word = attempt;
    return (attempt & 1) == 0;
}

static bool entropy_kat(void)
{
    uint8_t seed[DRBG_SEED_BYTES] = {0};
    FakeWords fake = {0, TEST_RETRY};
    bool ok = collect_seed(false, fake_read, &fake, 10, seed) == MOO_ENTROPY_UNSUPPORTED &&
              fake.calls == 0;
    ok = collect_seed(true, fake_read, &fake, 10, seed) == MOO_ENTROPY_OK &&
         fake.calls == 16 && seed[0] == 6 && seed[40] == 16 && ok;
    FakeWords repeating = {0, TEST_ALTERNATING};
    ok = collect_seed(true, fake_read, &repeating, 10, seed) == MOO_ENTROPY_REPEATED &&
         repeating.calls == 3 && seed[0] == 6 && ok;
    FakeWords exhausted = {0, TEST_EXHAUSTED};
    ok = collect_seed(true, fake_read, &exhausted, 10, seed) == MOO_ENTROPY_EXHAUSTED &&
         exhausted.calls == 10 && seed[0] == 6 && ok;

    return select_source(MOO_ENTROPY_OK, MOO_ENTROPY_OK) == MOO_ENTROPY_RDSEED &&
           select_source(MOO_ENTROPY_EXHAUSTED, MOO_ENTROPY_OK) == MOO_ENTROPY_RDRAND &&
           select_source(MOO_ENTROPY_UNSUPPORTED, MOO_ENTROPY_OK) == MOO_ENTROPY_RDRAND &&
           select_source(MOO_ENTROPY_REPEATED, MOO_ENTROPY_OK) == MOO_ENTROPY_NONE &&
           select_source(MOO_ENTROPY_OK, MOO_ENTROPY_REPEATED) == MOO_ENTROPY_NONE &&
           select_source(MOO_ENTROPY_UNSUPPORTED, MOO_ENTROPY_UNSUPPORTED) == MOO_ENTROPY_NONE && ok;
}

/* NIST CAVP drbgvectors_no_reseed/HMAC_DRBG.rsp, SHA-512, COUNT 0,
 * EntropyInputLen=256, NonceLen=128, ReturnedBitsLen=2048. */
static const uint8_t drbg_nist_seed[] = {
    0x35, 0x04, 0x9f, 0x38, 0x9a, 0x33, 0xc0, 0xec, 0xb1, 0x29, 0x32, 0x38,
    0xfd, 0x95, 0x1f, 0x8f, 0xfd, 0x51, 0x7d, 0xfd, 0xe0, 0x60, 0x41, 0xd3,
    0x29, 0x45, 0xb3, 0xe2, 0x69, 0x14, 0xba, 0x15, 0xf7, 0x32, 0x87, 0x60,
    0xbe, 0x61, 0x68, 0xe6, 0xaa, 0x9f, 0xb5, 0x47, 0x84, 0x98, 0x9a, 0x11,
};

static const uint8_t drbg_nist_output[] = {
    0xe7, 0x64, 0x91, 0xb0, 0x26, 0x0a, 0xac, 0xfd, 0xed, 0x01, 0xad, 0x39,
    0xfb, 0xf1, 0xa6, 0x6a, 0x88, 0x28, 0x4c, 0xaa, 0x51, 0x23, 0x36, 0x8a,
    0x2a, 0xd9, 0x33, 0x0e, 0xe4, 0x83, 0x35, 0xe3, 0xc9, 0xc9, 0xba, 0x90,
    0xe6, 0xcb, 0xc9, 0x42, 0x99, 0x62, 0xd6, 0x0c, 0x1a, 0x66, 0x61, 0xed,
    0xcf, 0xaa, 0x31, 0xd9, 0x72, 0xb8, 0x26, 0x4b, 0x9d, 0x45, 0x62, 0xcf,
    0x18, 0x49, 0x41, 0x28, 0xa0, 0x92, 0xc1, 0x7a, 0x8d, 0xa6, 0xf3, 0x11,
    0x3e, 0x8a, 0x7e, 0xdf, 0xcd, 0x44, 0x27, 0x08, 0x2b, 0xd3, 0x90, 0x67,
    0x5e, 0x96, 0x62, 0x40, 0x81, 0x44, 0x97, 0x17, 0x17, 0x30, 0x3d, 0x8d,
    0xc3, 0x52, 0xc9, 0xe8, 0xb9, 0x5e, 0x7f, 0x35, 0xfa, 0x2a, 0xc9, 0xf5,
    0x49, 0xb2, 0x92, 0xbc, 0x7c, 0x4b, 0xc7, 0xf0, 0x1e, 0xe0, 0xa5, 0x77,
    0x85, 0x9e, 0xf6, 0xe8, 0x2d, 0x79, 0xef, 0x23, 0x89, 0x2d, 0x16, 0x7c,
    0x14, 0x0d, 0x22, 0xaa, 0xc3, 0x2b, 0x64, 0xcc, 0xdf, 0xee, 0xe2, 0x73,
    0x05, 0x28, 0xa3, 0x87, 0x63, 0xb2, 0x42, 0x27, 0xf9, 0x1a, 0xc3, 0xff,
    0xe4, 0x7f, 0xb1, 0x15, 0x38, 0xe4, 0x35, 0x30, 0x7e, 0x77, 0x48, 0x18,
    0x02, 0xb0, 0xf6, 0x13, 0xf3, 0x70, 0xff, 0xb0, 0xdb, 0xea, 0xb7, 0x74,
    0xfe, 0x1e, 0xfb, 0xb1, 0xa8, 0x0d, 0x01, 0x15, 0x4a, 0x94, 0x59, 0xe7,
    0x3a, 0xd3, 0x61, 0x10, 0x8b, 0xbc, 0x86, 0xb0, 0x91, 0x4f, 0x09, 0x51,
    0x36, 0xcb, 0xe6, 0x34, 0x55, 0x5c, 0xe0, 0xbb, 0x26, 0x36, 0x18, 0xdc,
    0x5c, 0x36, 0x72, 0x91, 0xce, 0x08, 0x25, 0x51, 0x89, 0x87, 0x15, 0x4f,
    0xe9, 0xec, 0xb0, 0x52, 0xb3, 0xf0, 0xa2, 0x56, 0xfc, 0xc3, 0x0c, 0xc1,
    0x45, 0x72, 0x53, 0x1c, 0x96, 0x28, 0x97, 0x36, 0x39, 0xbe, 0xda, 0x45,
    0x6f, 0x2b, 0xdd, 0xf6,
};

static bool drbg_kat(void)
{
    MooBareDrbg rng;
    uint8_t output[sizeof(drbg_nist_output)] = {0};
    drbg_instantiate(&rng, drbg_nist_seed);
    bool runs = moo_bare_drbg_generate(&rng, output, sizeof(output)) == 0 &&
                moo_bare_drbg_generate(&rng, output, sizeof(output)) == 0;
    unsigned diff = 0;
    for (size_t i = 0; i < sizeof(output); i++)
        diff |= (unsigned)(output[i] ^ drbg_nist_output[i]);
    moo_bare_drbg_wipe(&rng);
    crypto_wipe(output, sizeof(output));
    return runs && diff == 0;
}

uint64_t moo_bare_drbg_kat(void)
{
    return (uint64_t)drbg_kat() | ((uint64_t)entropy_kat() << 1);
}
