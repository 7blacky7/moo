#include "moo_bare_krypto.h"

#include <stdbool.h>
#include <string.h>

enum {
    DRBG_SEED_BYTES = 48,    /* EntropyInput 256 Bit + Nonce 128 Bit */
    ENTROPY_SAMPLES = 8,
};

typedef bool (*ReadWord)(void *arg, uint64_t *word);

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

uint64_t moo_bare_entropie_seed(uint8_t seed[DRBG_SEED_BYTES])
{
    const unsigned features = entropy_features();
    uint8_t fallback[DRBG_SEED_BYTES];
    const MooEntropyStatus rdseed =
        collect_seed((features & MOO_ENTROPY_RDSEED) != 0, read_rdseed, NULL, 64, seed);
    const MooEntropyStatus rdrand =
        collect_seed((features & MOO_ENTROPY_RDRAND) != 0, read_rdrand, NULL, 10, fallback);
    const MooEntropySource source = select_source(rdseed, rdrand);
    if (source == MOO_ENTROPY_RDRAND)
        memcpy(seed, fallback, sizeof(fallback));
    if (source == MOO_ENTROPY_NONE)
        crypto_wipe(seed, DRBG_SEED_BYTES);
    crypto_wipe(fallback, sizeof(fallback));
    return (uint64_t)rdseed | ((uint64_t)rdrand << 4) | ((uint64_t)source << 8);
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

uint64_t moo_bare_entropie_kat(void)
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