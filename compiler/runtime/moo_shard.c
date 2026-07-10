/**
 * moo_shard.c — Streaming-Token-Shards + Dataloader (KIP-E1, Task f91d782c).
 * ============================================================================
 * Muster: moo_dataset.c / moo_tokenizer.c. UB-Policy: Groessen size_t/int64_t,
 * Serialisierung/CRC explizit unsigned, kein Aliasing-Cast (byteweise LE).
 * Der Dataloader liest gefenstert (fseek+fread) — Token-Daten bleiben auf der
 * Platte; nur Header + Blockreihenfolge leben im RAM. Siehe moo_shard.h fuer
 * das Format + den X3-§3-Vertrag.
 * ============================================================================
 */
#include "moo_shard.h"

#define SHARD_MAGIC   "MOOSHARD"
#define SHARD_MAGIC_N 8
#define SHARD_HEADER  48u
#define SHARD_VERSION 1u

/* ------------------------------------------------------------------ *
 * LE Lese-/Schreibhilfen (kein Aliasing-Cast)
 * ------------------------------------------------------------------ */
static void put_u32(uint8_t* p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void put_u64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8*i));
}
static uint32_t get_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t get_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8*i);
    return v;
}

/* FNV-1a-64 (bewusst unsigned-wrapend), inkrementell. */
#define FNV64_INIT 0xCBF29CE484222325ULL
static uint64_t fnv64_update(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) { h ^= (uint64_t)b[i]; h *= 0x100000001B3ULL; }
    return h;
}
/* splitmix64 fuer seed-deterministische Permutation. */
static uint64_t sm64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* ------------------------------------------------------------------ *
 * Header lesen + validieren (inkl. Dateigroessen-Konsistenz)
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t version, flags, datenart, n_docs;
    uint64_t n_tokens, tokver, crc;
    bool hat_maske;
    long file_size;
    long doc_off;   /* == SHARD_HEADER */
    long tok_off;   /* == SHARD_HEADER + (n_docs+1)*8 */
    long mask_off;  /* == tok_off + n_tokens*4 */
    long mask_bytes;
} ShardHdr;

/* Oeffnet die Datei (rb) + liest/validiert den Header. FILE* via *out_f
 * zurueck (Caller schliesst). false + *err bei Fehler (Datei geschlossen). */
static bool shard_open(const char* pfad, FILE** out_f, ShardHdr* h, const char** err) {
    *out_f = NULL;
    FILE* f = fopen(pfad, "rb");
    if (!f) { *err = "kann Datei nicht oeffnen"; return false; }
    uint8_t hb[SHARD_HEADER];
    if (fread(hb, 1, SHARD_HEADER, f) != SHARD_HEADER) { fclose(f); *err = "Header abgeschnitten"; return false; }
    if (memcmp(hb, SHARD_MAGIC, SHARD_MAGIC_N) != 0) { fclose(f); *err = "falsche Signatur (kein MOOSHARD)"; return false; }
    h->version  = get_u32(hb + 8);
    h->flags    = get_u32(hb + 12);
    h->datenart = get_u32(hb + 16);
    h->n_docs   = get_u32(hb + 20);
    h->n_tokens = get_u64(hb + 24);
    h->tokver   = get_u64(hb + 32);
    h->crc      = get_u64(hb + 40);
    if (h->version != SHARD_VERSION) { fclose(f); *err = "unbekannte Shard-Version"; return false; }
    if (h->datenart > 1) { fclose(f); *err = "unbekannte datenart"; return false; }
    h->hat_maske = (h->flags & 1u) != 0;
    /* Groessen gegen Overflow + Plausibilitaet. */
    if (h->n_tokens > (uint64_t)INT64_MAX / 4) { fclose(f); *err = "n_tokens unplausibel"; return false; }
    uint64_t doc_entries = (uint64_t)h->n_docs + 1;
    h->doc_off  = (long)SHARD_HEADER;
    h->tok_off  = (long)(SHARD_HEADER + doc_entries * 8u);
    h->mask_bytes = h->hat_maske ? (long)((h->n_tokens + 7) / 8) : 0;
    h->mask_off = h->tok_off + (long)(h->n_tokens * 4u);
    long expect = h->mask_off + h->mask_bytes;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); *err = "nicht seekbar"; return false; }
    h->file_size = ftell(f);
    if (h->file_size != expect) { fclose(f); *err = "Dateigroesse passt nicht zum Header (beschaedigt/abgeschnitten)"; return false; }
    *out_f = f;
    return true;
}

/* ================================================================== *
 * SCHREIBEN
 * ================================================================== */
static bool tensor_1d(MooValue v, const float** data, int64_t* n) {
    if (v.tag != MOO_TENSOR) return false;
    MooTensor* t = MV_TENSOR(v);
    *data = t->data; *n = t->size;
    return true;
}

MooValue moo_shard_schreiben(MooValue pfad, MooValue docs, MooValue datenart,
                             MooValue tokenizer_version, MooValue masken) {
    if (pfad.tag != MOO_STRING) { moo_throw(moo_error("shard_schreiben: erwarte einen Dateinamen")); return moo_none(); }
    if (docs.tag != MOO_LIST)   { moo_throw(moo_error("shard_schreiben: docs muss eine Liste von Token-Tensoren sein")); return moo_none(); }
    if (datenart.tag != MOO_STRING) { moo_throw(moo_error("shard_schreiben: datenart muss \"pretrain\" oder \"sft\" sein")); return moo_none(); }
    bool sft;
    if (strcmp(MV_STR(datenart)->chars, "sft") == 0) sft = true;
    else if (strcmp(MV_STR(datenart)->chars, "pretrain") == 0) sft = false;
    else { moo_throw(moo_error("shard_schreiben: datenart muss \"pretrain\" oder \"sft\" sein")); return moo_none(); }

    uint64_t tokver = 0;
    if (tokenizer_version.tag == MOO_STRING) tokver = strtoull(MV_STR(tokenizer_version)->chars, NULL, 16);
    else if (tokenizer_version.tag == MOO_NUMBER) tokver = (uint64_t)(int64_t)MV_NUM(tokenizer_version);

    int32_t n_docs = moo_list_iter_len(docs);
    if (n_docs <= 0) { moo_throw(moo_error("shard_schreiben: keine Dokumente")); return moo_none(); }
    if (sft) {
        if (masken.tag != MOO_LIST || moo_list_iter_len(masken) != n_docs) {
            moo_throw(moo_error("shard_schreiben: sft braucht eine Masken-Liste gleicher Laenge wie docs"));
            return moo_none();
        }
    }

    /* Pass 1: doc_offsets + Gesamt-Tokenzahl. */
    uint64_t* offs = (uint64_t*)malloc((size_t)(n_docs + 1) * sizeof(uint64_t));
    if (!offs) { moo_throw(moo_error("shard_schreiben: nicht genug Speicher")); return moo_none(); }
    offs[0] = 0;
    bool bad = false; const char* berr = NULL;
    for (int32_t i = 0; i < n_docs; i++) {
        MooValue el = moo_list_iter_get(docs, i);      /* +1 */
        const float* d; int64_t n;
        if (!tensor_1d(el, &d, &n)) { moo_release(el); bad = true; berr = "docs muss Tensoren enthalten"; break; }
        offs[i + 1] = offs[i] + (uint64_t)n;
        moo_release(el);
    }
    if (bad) { free(offs); moo_throw(moo_error(berr)); return moo_none(); }
    uint64_t n_tokens = offs[n_docs];
    if (n_tokens == 0) { free(offs); moo_throw(moo_error("shard_schreiben: Dokumente sind leer")); return moo_none(); }

    /* Loss-Maske (nur sft) vorbereiten: volles Bitfeld im RAM (1/8 der Tokens). */
    uint8_t* maskbits = NULL;
    long mask_bytes = 0;
    if (sft) {
        mask_bytes = (long)((n_tokens + 7) / 8);
        maskbits = (uint8_t*)calloc((size_t)mask_bytes, 1);
        if (!maskbits) { free(offs); moo_throw(moo_error("shard_schreiben: nicht genug Speicher")); return moo_none(); }
        uint64_t pos = 0;
        for (int32_t i = 0; i < n_docs && !bad; i++) {
            MooValue mv = moo_list_iter_get(masken, i);   /* +1 */
            const float* md; int64_t mn;
            if (!tensor_1d(mv, &md, &mn) || (uint64_t)mn != offs[i+1]-offs[i]) {
                moo_release(mv); bad = true; berr = "Maske passt nicht zum Dokument (Laenge)"; break;
            }
            for (int64_t k = 0; k < mn; k++) {
                if (md[k] >= 0.5f) maskbits[pos >> 3] |= (uint8_t)(1u << (pos & 7u));
                pos++;
            }
            moo_release(mv);
        }
        if (bad) { free(offs); free(maskbits); moo_throw(moo_error(berr)); return moo_none(); }
    }

    FILE* f = fopen(MV_STR(pfad)->chars, "wb");
    if (!f) { free(offs); free(maskbits);
        char msg[600]; snprintf(msg, sizeof(msg), "shard_schreiben: kann \"%s\" nicht schreiben", MV_STR(pfad)->chars);
        moo_throw(moo_error(msg)); return moo_none(); }

    /* Header (crc zunaechst 0). */
    uint8_t hb[SHARD_HEADER];
    memset(hb, 0, sizeof(hb));
    memcpy(hb, SHARD_MAGIC, SHARD_MAGIC_N);
    put_u32(hb + 8, SHARD_VERSION);
    put_u32(hb + 12, sft ? 1u : 0u);
    put_u32(hb + 16, sft ? 1u : 0u);
    put_u32(hb + 20, (uint32_t)n_docs);
    put_u64(hb + 24, n_tokens);
    put_u64(hb + 32, tokver);
    put_u64(hb + 40, 0u);
    bool wok = fwrite(hb, 1, SHARD_HEADER, f) == SHARD_HEADER;

    uint64_t crc = FNV64_INIT;
    /* doc_offsets. */
    for (int32_t i = 0; i <= n_docs && wok; i++) {
        uint8_t ob[8]; put_u64(ob, offs[i]);
        wok = fwrite(ob, 1, 8, f) == 8;
        crc = fnv64_update(crc, ob, 8);
    }
    /* token_ids je Dokument (streaming, per-Doc-Puffer). */
    for (int32_t i = 0; i < n_docs && wok; i++) {
        MooValue el = moo_list_iter_get(docs, i);      /* +1 */
        const float* d; int64_t n; tensor_1d(el, &d, &n);
        uint8_t* tb = (uint8_t*)malloc((size_t)(n > 0 ? n : 1) * 4);
        if (!tb) { moo_release(el); wok = false; break; }
        for (int64_t k = 0; k < n; k++) {
            double val = (double)d[k];
            uint32_t id = (val >= 0.0 && val < 4294967296.0) ? (uint32_t)val : 0u;
            put_u32(tb + k*4, id);
        }
        wok = fwrite(tb, 1, (size_t)n * 4, f) == (size_t)n * 4;
        crc = fnv64_update(crc, tb, (size_t)n * 4);
        free(tb);
        moo_release(el);
    }
    /* loss_maske. */
    if (wok && sft) {
        wok = fwrite(maskbits, 1, (size_t)mask_bytes, f) == (size_t)mask_bytes;
        crc = fnv64_update(crc, maskbits, (size_t)mask_bytes);
    }
    /* crc in den Header patchen. */
    if (wok) {
        uint8_t cb[8]; put_u64(cb, crc);
        wok = (fseek(f, 40, SEEK_SET) == 0) && (fwrite(cb, 1, 8, f) == 8);
    }
    int cerr = fclose(f);
    free(offs); free(maskbits);
    if (!wok || cerr != 0) { moo_throw(moo_error("shard_schreiben: Schreiben unvollstaendig")); return moo_none(); }
    return moo_bool(true);
}

/* ================================================================== *
 * INFO / PRUEFEN
 * ================================================================== */
MooValue moo_shard_info(MooValue pfad) {
    if (pfad.tag != MOO_STRING) { moo_throw(moo_error("shard_info: erwarte einen Dateinamen")); return moo_none(); }
    FILE* f; ShardHdr h; const char* err = NULL;
    if (!shard_open(MV_STR(pfad)->chars, &f, &h, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "shard_info: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    fclose(f);
    char hex[17]; snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h.tokver);
    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("version"),  moo_number((double)h.version));
    moo_dict_set(d, moo_string_new("datenart"), moo_string_new(h.datenart == 1 ? "sft" : "pretrain"));
    moo_dict_set(d, moo_string_new("n_docs"),   moo_number((double)h.n_docs));
    moo_dict_set(d, moo_string_new("n_tokens"), moo_number((double)h.n_tokens));
    moo_dict_set(d, moo_string_new("tokenizer_version"), moo_string_new(hex));
    moo_dict_set(d, moo_string_new("hat_maske"), moo_bool(h.hat_maske));
    return d;
}

MooValue moo_shard_pruefen(MooValue pfad) {
    if (pfad.tag != MOO_STRING) { moo_throw(moo_error("shard_pruefen: erwarte einen Dateinamen")); return moo_none(); }
    FILE* f; ShardHdr h; const char* err = NULL;
    if (!shard_open(MV_STR(pfad)->chars, &f, &h, &err)) { return moo_bool(false); }
    /* CRC ueber [48 .. EOF] neu berechnen. */
    if (fseek(f, (long)SHARD_HEADER, SEEK_SET) != 0) { fclose(f); return moo_bool(false); }
    uint64_t crc = FNV64_INIT;
    uint8_t buf[65536];
    size_t r;
    bool ok = true;
    long rest = h.file_size - (long)SHARD_HEADER;
    while (rest > 0) {
        size_t want = rest < (long)sizeof(buf) ? (size_t)rest : sizeof(buf);
        r = fread(buf, 1, want, f);
        if (r != want) { ok = false; break; }
        crc = fnv64_update(crc, buf, r);
        rest -= (long)r;
    }
    fclose(f);
    if (!ok) return moo_bool(false);
    return moo_bool(crc == h.crc);
}

/* ================================================================== *
 * GEFENSTERTES LESEN
 * ================================================================== */
static MooValue fenster_impl(MooValue pfad, MooValue start, MooValue laenge,
                             bool maske, const char* fn) {
    if (pfad.tag != MOO_STRING || start.tag != MOO_NUMBER || laenge.tag != MOO_NUMBER) {
        char msg[120]; snprintf(msg, sizeof(msg), "%s: erwarte (pfad, start, laenge)", fn);
        moo_throw(moo_error(msg)); return moo_none();
    }
    double sd = MV_NUM(start), ld = MV_NUM(laenge);
    if (!(sd >= 0.0) || !(ld >= 1.0)) { char msg[120]; snprintf(msg, sizeof(msg), "%s: start>=0 und laenge>=1 noetig", fn); moo_throw(moo_error(msg)); return moo_none(); }
    int64_t s = (int64_t)sd, l = (int64_t)ld;
    FILE* f; ShardHdr h; const char* err = NULL;
    if (!shard_open(MV_STR(pfad)->chars, &f, &h, &err)) { char msg[200]; snprintf(msg, sizeof(msg), "%s: %s", fn, err); moo_throw(moo_error(msg)); return moo_none(); }
    if (maske && !h.hat_maske) { fclose(f); char msg[120]; snprintf(msg, sizeof(msg), "%s: Shard hat keine Loss-Maske (pretrain)", fn); moo_throw(moo_error(msg)); return moo_none(); }
    if ((uint64_t)s + (uint64_t)l > h.n_tokens) { fclose(f); char msg[160]; snprintf(msg, sizeof(msg), "%s: Fenster [%lld,%lld) ausserhalb von %llu Tokens", fn, (long long)s, (long long)(s+l), (unsigned long long)h.n_tokens); moo_throw(moo_error(msg)); return moo_none(); }

    int32_t shape[1] = { (int32_t)l };
    MooTensor* t = moo_tensor_raw(1, shape);
    if (!t) { fclose(f); return moo_none(); }
    bool ok = true;
    if (!maske) {
        if (fseek(f, h.tok_off + s * 4, SEEK_SET) != 0) ok = false;
        uint8_t* tb = ok ? (uint8_t*)malloc((size_t)l * 4) : NULL;
        if (!tb) ok = false;
        if (ok) ok = fread(tb, 1, (size_t)l * 4, f) == (size_t)l * 4;
        if (ok) for (int64_t i = 0; i < l; i++) t->data[i] = (float)get_u32(tb + i*4);
        free(tb);
    } else {
        /* Maskenbits einzeln lesen (Fenster meist klein = seq_len). */
        for (int64_t i = 0; i < l && ok; i++) {
            uint64_t bitpos = (uint64_t)(s + i);
            if (fseek(f, h.mask_off + (long)(bitpos >> 3), SEEK_SET) != 0) { ok = false; break; }
            int c = fgetc(f);
            if (c == EOF) { ok = false; break; }
            t->data[i] = ((unsigned)c >> (bitpos & 7u)) & 1u ? 1.0f : 0.0f;
        }
    }
    fclose(f);
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    if (!ok) { moo_release(v); char msg[120]; snprintf(msg, sizeof(msg), "%s: Lesefehler", fn); moo_throw(moo_error(msg)); return moo_none(); }
    return v;
}

MooValue moo_shard_fenster(MooValue pfad, MooValue start, MooValue laenge) {
    return fenster_impl(pfad, start, laenge, false, "shard_fenster");
}
MooValue moo_shard_fenster_maske(MooValue pfad, MooValue start, MooValue laenge) {
    return fenster_impl(pfad, start, laenge, true, "shard_fenster_maske");
}

/* ================================================================== *
 * SEED-DETERMINISTISCHE BLOCKREIHENFOLGE
 * ================================================================== */
MooValue moo_shard_reihenfolge(MooValue pfad, MooValue seed, MooValue seq_len) {
    if (pfad.tag != MOO_STRING || seed.tag != MOO_NUMBER || seq_len.tag != MOO_NUMBER) {
        moo_throw(moo_error("shard_reihenfolge: erwarte (pfad, seed, seq_len)")); return moo_none();
    }
    double sl = MV_NUM(seq_len);
    if (!(sl >= 1.0)) { moo_throw(moo_error("shard_reihenfolge: seq_len>=1 noetig")); return moo_none(); }
    int64_t seq = (int64_t)sl;
    FILE* f; ShardHdr h; const char* err = NULL;
    if (!shard_open(MV_STR(pfad)->chars, &f, &h, &err)) { char msg[200]; snprintf(msg, sizeof(msg), "shard_reihenfolge: %s", err); moo_throw(moo_error(msg)); return moo_none(); }
    fclose(f);
    int64_t n_blocks = (int64_t)(h.n_tokens / (uint64_t)seq);
    if (n_blocks <= 0) { moo_throw(moo_error("shard_reihenfolge: seq_len groesser als der Shard")); return moo_none(); }
    int64_t* start = (int64_t*)malloc((size_t)n_blocks * sizeof(int64_t));
    if (!start) { moo_throw(moo_error("shard_reihenfolge: nicht genug Speicher")); return moo_none(); }
    for (int64_t i = 0; i < n_blocks; i++) start[i] = i * seq;
    /* Fisher-Yates mit splitmix64(seed). */
    uint64_t rng = (uint64_t)(int64_t)MV_NUM(seed);
    rng = rng * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL;
    for (int64_t i = n_blocks - 1; i > 0; i--) {
        int64_t j = (int64_t)(sm64(&rng) % (uint64_t)(i + 1));
        int64_t tmp = start[i]; start[i] = start[j]; start[j] = tmp;
    }
    MooValue lst = moo_list_new((int32_t)(n_blocks > 0 ? n_blocks : 1));
    for (int64_t i = 0; i < n_blocks; i++) moo_list_append(lst, moo_number((double)start[i]));
    free(start);
    return lst;
}

/* ================================================================== *
 * TRAIN/VAL-SPLIT AUF SHARD-EBENE
 * ================================================================== */
MooValue moo_shard_split(MooValue pfad_liste, MooValue val_anteil, MooValue seed) {
    if (pfad_liste.tag != MOO_LIST || val_anteil.tag != MOO_NUMBER) {
        moo_throw(moo_error("shard_split: erwarte (pfad_liste, val_anteil, seed)")); return moo_none();
    }
    double va = MV_NUM(val_anteil);
    if (!(va >= 0.0) || va > 1.0) { moo_throw(moo_error("shard_split: val_anteil muss in [0,1] liegen")); return moo_none(); }
    int32_t n = moo_list_iter_len(pfad_liste);
    if (n <= 0) { moo_throw(moo_error("shard_split: leere Shard-Liste")); return moo_none(); }
    int32_t* idx = (int32_t*)malloc((size_t)n * sizeof(int32_t));
    if (!idx) { moo_throw(moo_error("shard_split: nicht genug Speicher")); return moo_none(); }
    for (int32_t i = 0; i < n; i++) idx[i] = i;
    uint64_t rng = (uint64_t)(int64_t)MV_NUM(seed);
    rng = rng * 0x2545F4914F6CDD1DULL + 0x9E3779B97F4A7C15ULL;
    for (int32_t i = n - 1; i > 0; i--) {
        int32_t j = (int32_t)(sm64(&rng) % (uint64_t)(i + 1));
        int32_t tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
    }
    int32_t n_val = (int32_t)(va * (double)n + 0.5);
    if (n_val > n) n_val = n;
    MooValue train = moo_list_new(n - n_val > 0 ? n - n_val : 1);
    MooValue val   = moo_list_new(n_val > 0 ? n_val : 1);
    for (int32_t k = 0; k < n; k++) {
        MooValue el = moo_list_iter_get(pfad_liste, idx[k]);   /* +1 */
        if (k < n_val) moo_list_append(val, el); else moo_list_append(train, el);   /* Transfer */
    }
    free(idx);
    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("train"), train);
    moo_dict_set(d, moo_string_new("val"), val);
    return d;
}
