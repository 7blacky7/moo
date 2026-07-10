/**
 * moo_tokenizer.c — Byte-level BPE-Tokenizer (KIP-T2, Task 0f4118c2).
 * ============================================================================
 * Muster: moo_dataset.c (Refcount: Args geliehen, Rueckgabe +1; UB-Policy:
 * Groessen size_t/int64_t, Hash/Bit-Mischung explizit unsigned).
 *
 * Der Tokenizer IST sein versioniertes Binaerartefakt (siehe moo_tokenizer.h
 * fuer Format + Determinismus-/Tie-Break-Vertrag). Alle oeffentlichen
 * Funktionen nehmen/geben nur MooValue (LLVM-ABI).
 * ============================================================================
 */
#include "moo_tokenizer.h"

#define TOK_MAGIC   "MOOBPE01"
#define TOK_MAGIC_N 8
#define TOK_HEADER  32u          /* Bytes bis zum ersten Merge */
#define TOK_VERSION 1u
#define TOK_BASE    256u         /* 256 Einzelbyte-Basistokens */
#define TOK_VOKAB_MAX 65536u     /* harte Obergrenze (Speicher-/ABI-Schutz) */

/* ------------------------------------------------------------------ *
 * Little-Endian u32 Lese-/Schreibhilfen (kein Aliasing-Cast)
 * ------------------------------------------------------------------ */
static void put_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static uint32_t get_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ------------------------------------------------------------------ *
 * Paar-Hashmap (Open Addressing). Schluessel = (u64)links<<32 | rechts.
 * Doppelnutzung: (a) Trainings-Paarzaehler, (b) Encode-Rang-Lookup.
 * ------------------------------------------------------------------ */
typedef struct { uint64_t key; uint32_t val; uint8_t used; } PCell;
typedef struct { PCell* cells; size_t cap; size_t count; } PMap;

/* Mischfunktion fuer u64-Schluessel — bewusst unsigned-wrapend (splitmix64). */
static uint64_t pm_mix(uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static bool pm_init(PMap* m, size_t cap) {
    size_t c = 16;
    while (c < cap) c <<= 1;                 /* Zweierpotenz */
    m->cells = (PCell*)calloc(c, sizeof(PCell));
    if (!m->cells) return false;
    m->cap = c; m->count = 0;
    return true;
}
static void pm_free(PMap* m) { free(m->cells); m->cells = NULL; m->cap = 0; m->count = 0; }
static void pm_clear(PMap* m) { memset(m->cells, 0, m->cap * sizeof(PCell)); m->count = 0; }

/* Findet oder legt den Slot fuer key an. NULL nur bei OOM (Grow scheitert). */
static PCell* pm_slot(PMap* m, uint64_t key);
static bool pm_grow(PMap* m) {
    size_t ncap = m->cap << 1;
    PCell* old = m->cells; size_t ocap = m->cap;
    PCell* nc = (PCell*)calloc(ncap, sizeof(PCell));
    if (!nc) return false;
    m->cells = nc; m->cap = ncap; m->count = 0;
    for (size_t i = 0; i < ocap; i++) {
        if (old[i].used) {
            PCell* s = pm_slot(m, old[i].key);   /* kann nicht growen: frisch */
            s->key = old[i].key; s->used = 1; s->val = old[i].val;
            m->count++;
        }
    }
    free(old);
    return true;
}
static PCell* pm_slot(PMap* m, uint64_t key) {
    size_t mask = m->cap - 1;
    size_t i = (size_t)pm_mix(key) & mask;
    while (m->cells[i].used && m->cells[i].key != key)
        i = (i + 1) & mask;
    return &m->cells[i];
}
/* +1 auf den Zaehler von key (legt bei Bedarf an). false = OOM. */
static bool pm_inc(PMap* m, uint64_t key) {
    if ((m->count + 1) * 10 >= m->cap * 7) {     /* Last > 0.7 -> grow */
        if (!pm_grow(m)) return false;
    }
    PCell* s = pm_slot(m, key);
    if (!s->used) { s->used = 1; s->key = key; s->val = 0; m->count++; }
    if (s->val < 0xFFFFFFFFu) s->val++;
    return true;
}
/* val zu key setzen (legt an). false = OOM. */
static bool pm_put(PMap* m, uint64_t key, uint32_t val) {
    if ((m->count + 1) * 10 >= m->cap * 7) {
        if (!pm_grow(m)) return false;
    }
    PCell* s = pm_slot(m, key);
    if (!s->used) { s->used = 1; s->key = key; m->count++; }
    s->val = val;
    return true;
}
/* Liefert val oder setzt *found=false. */
static uint32_t pm_get(PMap* m, uint64_t key, bool* found) {
    PCell* s = pm_slot(m, key);
    *found = s->used != 0;
    return s->used ? s->val : 0;
}
static uint64_t pair_key(uint32_t a, uint32_t b) {
    return ((uint64_t)a << 32) | (uint64_t)b;
}

/* ------------------------------------------------------------------ *
 * Kern-Rewrite: ersetzt jedes NICHT-ueberlappende Vorkommen von (a,b)
 * durch c, links-nach-rechts. Rueckgabe: neue Laenge. In-place (out<=in).
 * ------------------------------------------------------------------ */
static int64_t seq_merge_pair(int32_t* seq, int64_t len,
                              int32_t a, int32_t b, int32_t c) {
    int64_t w = 0;
    for (int64_t r = 0; r < len; ) {
        if (r + 1 < len && seq[r] == a && seq[r + 1] == b) {
            seq[w++] = c;
            r += 2;
        } else {
            seq[w++] = seq[r];
            r += 1;
        }
    }
    return w;
}

/* ------------------------------------------------------------------ *
 * FNV-1a 64-bit ueber Bytes (bewusst unsigned-wrapend).
 * ------------------------------------------------------------------ */
static uint64_t fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= (uint64_t)p[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

/* ================================================================== *
 * TRAINING
 * ================================================================== */

/* Lexikographischer Vergleich der MERGE-Bytefolge von (a,b) vs (a2,b2):
 * bytes(a)++bytes(b)  gegen  bytes(a2)++bytes(b2). Rueckgabe <0/0/>0.
 * tl/tb = Token-Laengen/-Bytes-Tabelle. Kein Alloc (byteweise Stream). */
static int cmp_merged(const uint8_t* const* tb, const uint32_t* tl,
                      int32_t a, int32_t b, int32_t a2, int32_t b2) {
    /* Virtuelle Konkatenation ueber je zwei Segmente vergleichen. */
    uint32_t la = tl[a], lb = tl[b], la2 = tl[a2], lb2 = tl[b2];
    uint64_t L1 = (uint64_t)la + lb, L2 = (uint64_t)la2 + lb2;
    uint64_t n = (L1 < L2) ? L1 : L2;
    for (uint64_t i = 0; i < n; i++) {
        uint8_t c1 = (i < la) ? tb[a][i]  : tb[b][i - la];
        uint8_t c2 = (i < la2) ? tb[a2][i] : tb[b2][i - la2];
        if (c1 != c2) return (c1 < c2) ? -1 : 1;
    }
    if (L1 != L2) return (L1 < L2) ? -1 : 1;
    return 0;
}

MooValue moo_tok_trainiere(MooValue korpus, MooValue vokab_groesse) {
    if (korpus.tag != MOO_STRING) {
        moo_throw(moo_error("tokenizer_trainiere: erwarte einen Text/Bytes als Korpus"));
        return moo_none();
    }
    if (vokab_groesse.tag != MOO_NUMBER) {
        moo_throw(moo_error("tokenizer_trainiere: die Vokabulargroesse muss eine Zahl sein"));
        return moo_none();
    }
    double vd = MV_NUM(vokab_groesse);
    if (!(vd >= (double)TOK_BASE) || vd > (double)TOK_VOKAB_MAX) {
        char msg[160];
        snprintf(msg, sizeof(msg), "tokenizer_trainiere: Vokabulargroesse %.0f "
                 "unzulaessig (erlaubt: %u..%u)", vd, TOK_BASE, TOK_VOKAB_MAX);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    uint32_t Vt = (uint32_t)vd;
    const uint8_t* korp = (const uint8_t*)MV_STR(korpus)->chars;
    int64_t n = (int64_t)MV_STR(korpus)->length;
    if (n <= 0) {
        moo_throw(moo_error("tokenizer_trainiere: der Korpus ist leer"));
        return moo_none();
    }

    /* Token-Tabellen: Id -> Bytes. Vorab auf Vt dimensioniert. */
    uint8_t** tb = (uint8_t**)calloc(Vt, sizeof(uint8_t*));
    uint32_t* tl = (uint32_t*)calloc(Vt, sizeof(uint32_t));
    int32_t* ma = (int32_t*)calloc(Vt, sizeof(int32_t));   /* merges links  */
    int32_t* mb = (int32_t*)calloc(Vt, sizeof(int32_t));   /* merges rechts */
    int32_t* seq = (int32_t*)malloc((size_t)n * sizeof(int32_t));
    if (!tb || !tl || !ma || !mb || !seq) {
        free(tb); free(tl); free(ma); free(mb); free(seq);
        moo_throw(moo_error("tokenizer_trainiere: nicht genug Speicher"));
        return moo_none();
    }
    /* Basis 0..255: je ein Byte. */
    bool oom = false;
    for (uint32_t bID = 0; bID < TOK_BASE; bID++) {
        tb[bID] = (uint8_t*)malloc(1);
        if (!tb[bID]) { oom = true; break; }
        tb[bID][0] = (uint8_t)bID;
        tl[bID] = 1;
    }
    for (int64_t i = 0; i < n; i++) seq[i] = (int32_t)korp[i];
    int64_t len = n;
    uint32_t M = 0;

    PMap pc; if (!oom && !pm_init(&pc, 1024)) oom = true;

    /* Merge-Schleife bis Vt erreicht oder kein Paar >=2 mehr. */
    while (!oom && (TOK_BASE + M) < Vt && len >= 2) {
        pm_clear(&pc);
        for (int64_t i = 0; i + 1 < len; i++) {
            if (!pm_inc(&pc, pair_key((uint32_t)seq[i], (uint32_t)seq[i + 1]))) {
                oom = true; break;
            }
        }
        if (oom) break;
        /* Bestes Paar nach (Haeufigkeit desc, Merge-Bytes lex asc, (a,b) asc). */
        uint32_t best_cnt = 0; int32_t best_a = -1, best_b = -1;
        for (size_t s = 0; s < pc.cap; s++) {
            if (!pc.cells[s].used) continue;
            uint32_t cnt = pc.cells[s].val;
            if (cnt < 2) continue;                 /* nur echt wiederkehrende */
            int32_t a = (int32_t)(pc.cells[s].key >> 32);
            int32_t b = (int32_t)(pc.cells[s].key & 0xFFFFFFFFu);
            bool nimm;
            if (best_a < 0)               nimm = true;
            else if (cnt != best_cnt)     nimm = (cnt > best_cnt);
            else {
                int c = cmp_merged((const uint8_t* const*)tb, tl, a, b, best_a, best_b);
                nimm = (c < 0) || (c == 0 && (a < best_a || (a == best_a && b < best_b)));
            }
            if (nimm) { best_cnt = cnt; best_a = a; best_b = b; }
        }
        if (best_a < 0) break;                      /* nichts >=2 -> fertig */

        uint32_t nid = TOK_BASE + M;                /* neue Merge-Id */
        uint32_t nlen = tl[best_a] + tl[best_b];
        uint8_t* nbytes = (uint8_t*)malloc(nlen ? nlen : 1);
        if (!nbytes) { oom = true; break; }
        memcpy(nbytes, tb[best_a], tl[best_a]);
        memcpy(nbytes + tl[best_a], tb[best_b], tl[best_b]);
        tb[nid] = nbytes; tl[nid] = nlen;
        ma[M] = best_a; mb[M] = best_b; M++;
        len = seq_merge_pair(seq, len, best_a, best_b, (int32_t)nid);
    }
    pm_free(&pc);
    free(seq);

    if (oom) {
        for (uint32_t i = 0; i < Vt; i++) free(tb[i]);
        free(tb); free(tl); free(ma); free(mb);
        moo_throw(moo_error("tokenizer_trainiere: nicht genug Speicher"));
        return moo_none();
    }

    uint32_t V = TOK_BASE + M;                        /* S == 0 in T2 */
    /* Blobgroesse: Header + Merges + Vokabtabelle. */
    size_t sz = TOK_HEADER + (size_t)M * 8u;
    for (uint32_t id = 0; id < V; id++) sz += 4u + tl[id];
    uint8_t* blob = (uint8_t*)malloc(sz);
    if (!blob) {
        for (uint32_t i = 0; i < Vt; i++) free(tb[i]);
        free(tb); free(tl); free(ma); free(mb);
        moo_throw(moo_error("tokenizer_trainiere: nicht genug Speicher"));
        return moo_none();
    }
    memcpy(blob, TOK_MAGIC, TOK_MAGIC_N);
    put_u32(blob + 8,  TOK_VERSION);
    put_u32(blob + 12, 0u);                            /* flags: kein Normalize */
    put_u32(blob + 16, V);
    put_u32(blob + 20, M);
    put_u32(blob + 24, 0u);                            /* S */
    put_u32(blob + 28, 0u);                            /* reserved */
    size_t off = TOK_HEADER;
    for (uint32_t i = 0; i < M; i++) {
        put_u32(blob + off, (uint32_t)ma[i]); off += 4;
        put_u32(blob + off, (uint32_t)mb[i]); off += 4;
    }
    for (uint32_t id = 0; id < V; id++) {
        put_u32(blob + off, tl[id]); off += 4;
        memcpy(blob + off, tb[id], tl[id]); off += tl[id];
    }
    /* off == sz garantiert. */
    for (uint32_t i = 0; i < Vt; i++) free(tb[i]);
    free(tb); free(tl); free(ma); free(mb);

    MooValue out = moo_string_new_len((const char*)blob, (int32_t)sz);
    free(blob);
    return out;
}

/* ================================================================== *
 * ARTEFAKT-PARSING (bounds-gecheckt, untrusted-hart)
 * ================================================================== */
typedef struct {
    const uint8_t* blob; size_t blob_len;
    uint32_t version, flags, V, M, S;
    const uint8_t* merges;          /* zeigt auf M*8 Bytes im Blob */
    const uint8_t** tok_ptr;        /* [V] Zeiger in den Blob (malloc) */
    uint32_t* tok_len;              /* [V] (malloc) */
    /* Spezial-Index (KIP-T3): Name -> Id fuer die Render-API. */
    uint32_t* spec_id;              /* [S] (malloc) */
    const uint8_t** spec_name;      /* [S] (malloc) */
    uint32_t* spec_name_len;        /* [S] (malloc) */
} ParsedTok;

static void pt_free(ParsedTok* pt) {
    free(pt->tok_ptr); free(pt->tok_len);
    free(pt->spec_id); free(pt->spec_name); free(pt->spec_name_len);
}

/* Spezial-Id per Name (KIP-T3). Rueckgabe -1 wenn unbekannt. */
static int64_t spec_lookup(const ParsedTok* pt, const char* name, uint32_t nlen) {
    for (uint32_t i = 0; i < pt->S; i++) {
        if (pt->spec_name_len[i] == nlen &&
            memcmp(pt->spec_name[i], name, nlen) == 0)
            return (int64_t)pt->spec_id[i];
    }
    return -1;
}

/* Parst + validiert. false + gesetztes err bei kaputtem Artefakt. */
static bool tok_parse(MooValue tok, ParsedTok* pt, const char** err) {
    memset(pt, 0, sizeof(*pt));
    if (tok.tag != MOO_STRING) { *err = "kein Tokenizer-Artefakt (String erwartet)"; return false; }
    const uint8_t* b = (const uint8_t*)MV_STR(tok)->chars;
    int32_t blen32 = MV_STR(tok)->length;
    if (blen32 < (int32_t)TOK_HEADER) { *err = "Artefakt zu kurz (Header fehlt)"; return false; }
    size_t blen = (size_t)blen32;
    if (memcmp(b, TOK_MAGIC, TOK_MAGIC_N) != 0) { *err = "falsche Signatur (kein MOOBPE-Artefakt)"; return false; }
    pt->blob = b; pt->blob_len = blen;
    pt->version = get_u32(b + 8);
    pt->flags   = get_u32(b + 12);
    pt->V       = get_u32(b + 16);
    pt->M       = get_u32(b + 20);
    pt->S       = get_u32(b + 24);
    if (pt->version != TOK_VERSION) { *err = "unbekannte Artefakt-Version"; return false; }
    if (pt->V > TOK_VOKAB_MAX || pt->V < TOK_BASE) { *err = "Vokabulargroesse ausserhalb des Rahmens"; return false; }
    if ((uint64_t)TOK_BASE + pt->M + pt->S != pt->V) { *err = "Header inkonsistent (V != 256 + M + S)"; return false; }

    /* Merges-Sektion. */
    size_t off = TOK_HEADER;
    size_t merges_bytes = (size_t)pt->M * 8u;
    if (merges_bytes / 8u != pt->M || off + merges_bytes > blen) { *err = "Merge-Sektion abgeschnitten"; return false; }
    pt->merges = b + off;
    off += merges_bytes;
    /* Merge-Ids muessen auf frueher definierte Tokens zeigen (a,b < 256+i). */
    for (uint32_t i = 0; i < pt->M; i++) {
        uint32_t a = get_u32(pt->merges + (size_t)i * 8);
        uint32_t c = get_u32(pt->merges + (size_t)i * 8 + 4);
        if (a >= TOK_BASE + i || c >= TOK_BASE + i) { *err = "Merge verweist auf undefinierte Id"; return false; }
    }

    /* Vokabtabelle: V Eintraege, Zeiger aufbauen. */
    pt->tok_ptr = (const uint8_t**)malloc((size_t)pt->V * sizeof(uint8_t*));
    pt->tok_len = (uint32_t*)malloc((size_t)pt->V * sizeof(uint32_t));
    if (!pt->tok_ptr || !pt->tok_len) { pt_free(pt); *err = "nicht genug Speicher"; return false; }
    for (uint32_t id = 0; id < pt->V; id++) {
        if (off + 4 > blen) { pt_free(pt); *err = "Vokabtabelle abgeschnitten (Laenge)"; return false; }
        uint32_t l = get_u32(b + off); off += 4;
        if (l > blen || off + l > blen) { pt_free(pt); *err = "Vokabtabelle abgeschnitten (Bytes)"; return false; }
        pt->tok_ptr[id] = b + off;
        pt->tok_len[id] = l;
        off += l;
    }
    /* Basis-Invariante: Id 0..255 sind Einzelbytes mit ihrem Wert. */
    for (uint32_t id = 0; id < TOK_BASE && id < pt->V; id++) {
        if (pt->tok_len[id] != 1 || pt->tok_ptr[id][0] != (uint8_t)id) {
            pt_free(pt); *err = "Basis-Byte-Token verletzt (Roundtrip-Garantie gebrochen)"; return false;
        }
    }
    /* Spezial-Sektion (S): { id, namelen, name-bytes } indizieren. */
    if (pt->S > 0) {
        pt->spec_id = (uint32_t*)malloc((size_t)pt->S * sizeof(uint32_t));
        pt->spec_name = (const uint8_t**)malloc((size_t)pt->S * sizeof(uint8_t*));
        pt->spec_name_len = (uint32_t*)malloc((size_t)pt->S * sizeof(uint32_t));
        if (!pt->spec_id || !pt->spec_name || !pt->spec_name_len) { pt_free(pt); *err = "nicht genug Speicher"; return false; }
    }
    for (uint32_t i = 0; i < pt->S; i++) {
        if (off + 8 > blen) { pt_free(pt); *err = "Spezial-Sektion abgeschnitten"; return false; }
        uint32_t sid = get_u32(b + off); off += 4;
        uint32_t nl  = get_u32(b + off); off += 4;
        if (sid >= pt->V || nl > blen || off + nl > blen) { pt_free(pt); *err = "Spezial-Token ungueltig"; return false; }
        /* Spezial-Ids liegen strukturell HINTER Basis+Merges (>=256+M) —
         * damit kann kein Merge/kein Byte-Encode je eine Spezial-Id erzeugen
         * (Injection-Schutz X3 §1). */
        if (sid < TOK_BASE + pt->M) { pt_free(pt); *err = "Spezial-Id verletzt Injection-Invariante (< 256+M)"; return false; }
        pt->spec_id[i] = sid;
        pt->spec_name[i] = b + off;
        pt->spec_name_len[i] = nl;
        off += nl;
    }
    return true;
}

/* ================================================================== *
 * ENCODE / DECODE
 * ================================================================== */

/* Baut die Rang-Map (Paar -> rank) aus der Merge-Sektion. false = OOM. */
static bool build_rank_map(const ParsedTok* pt, PMap* rm) {
    if (!pm_init(rm, (size_t)pt->M * 2 + 16)) return false;
    for (uint32_t i = 0; i < pt->M; i++) {
        uint32_t a = get_u32(pt->merges + (size_t)i * 8);
        uint32_t c = get_u32(pt->merges + (size_t)i * 8 + 4);
        if (!pm_put(rm, pair_key(a, c), i)) return false;   /* rank == i */
    }
    return true;
}

/* Wendet BPE-Merges auf eine Byte-Sequenz an. seq/plen in-out.
 * Standard: wiederholt das global rang-niedrigste Paar mergen, bis keins
 * mehr passt. Deterministisch. false = OOM. */
static bool bpe_apply(const ParsedTok* pt, PMap* rm, int32_t* seq, int64_t* plen) {
    int64_t len = *plen;
    for (;;) {
        uint32_t best_rank = 0xFFFFFFFFu; int32_t ba = -1, bb = -1;
        for (int64_t i = 0; i + 1 < len; i++) {
            bool f;
            uint32_t r = pm_get(rm, pair_key((uint32_t)seq[i], (uint32_t)seq[i + 1]), &f);
            if (f && r < best_rank) { best_rank = r; ba = seq[i]; bb = seq[i + 1]; }
        }
        if (ba < 0) break;
        int32_t nid = (int32_t)(TOK_BASE + best_rank);
        len = seq_merge_pair(seq, len, ba, bb, nid);
    }
    *plen = len;
    return true;
}

/* Encode eines Byte-Puffers -> frischer Tensor[n] (+1). throws bei leer/OOM. */
static MooValue encode_bytes(const ParsedTok* pt, PMap* rm,
                             const uint8_t* txt, int64_t tlen) {
    if (tlen <= 0) {
        moo_throw(moo_error("encode: leerer Text hat keine Tokens (Tensor braucht >=1 Element)"));
        return moo_none();
    }
    int32_t* seq = (int32_t*)malloc((size_t)tlen * sizeof(int32_t));
    if (!seq) { moo_throw(moo_error("encode: nicht genug Speicher")); return moo_none(); }
    for (int64_t i = 0; i < tlen; i++) seq[i] = (int32_t)txt[i];
    int64_t len = tlen;
    bpe_apply(pt, rm, seq, &len);
    int32_t shape[1] = { (int32_t)len };
    MooTensor* t = moo_tensor_raw(1, shape);
    if (!t) { free(seq); return moo_none(); }   /* moo_tensor_raw hat geworfen */
    for (int64_t i = 0; i < len; i++) t->data[i] = (float)seq[i];
    free(seq);
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

MooValue moo_tok_kodiere(MooValue tok, MooValue text) {
    if (text.tag != MOO_STRING) {
        moo_throw(moo_error("encode: erwarte einen Text/Bytes"));
        return moo_none();
    }
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "encode: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    PMap rm;
    if (!build_rank_map(&pt, &rm)) {
        pm_free(&rm); pt_free(&pt);
        moo_throw(moo_error("encode: nicht genug Speicher")); return moo_none();
    }
    MooValue r = encode_bytes(&pt, &rm,
                              (const uint8_t*)MV_STR(text)->chars,
                              (int64_t)MV_STR(text)->length);
    pm_free(&rm); pt_free(&pt);
    return r;
}

MooValue moo_tok_kodiere_stapel(MooValue tok, MooValue texte) {
    if (texte.tag != MOO_LIST) {
        moo_throw(moo_error("encode_stapel: erwarte eine Liste von Texten"));
        return moo_none();
    }
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "encode_stapel: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    PMap rm;
    if (!build_rank_map(&pt, &rm)) {
        pm_free(&rm); pt_free(&pt);
        moo_throw(moo_error("encode_stapel: nicht genug Speicher")); return moo_none();
    }
    int32_t cnt = moo_list_iter_len(texte);
    MooValue out = moo_list_new(cnt > 0 ? cnt : 1);
    for (int32_t i = 0; i < cnt; i++) {
        MooValue el = moo_list_iter_get(texte, i);   /* +1 */
        if (el.tag != MOO_STRING) {
            moo_release(el); moo_release(out);
            pm_free(&rm); pt_free(&pt);
            moo_throw(moo_error("encode_stapel: alle Elemente muessen Texte sein"));
            return moo_none();
        }
        MooValue ids = encode_bytes(&pt, &rm,
                                    (const uint8_t*)MV_STR(el)->chars,
                                    (int64_t)MV_STR(el)->length);
        moo_release(el);
        if (moo_error_flag) {     /* encode_bytes hat geworfen (z.B. leer) */
            moo_release(out); pm_free(&rm); pt_free(&pt);
            return moo_none();
        }
        moo_list_append(out, ids);                    /* Transfer */
    }
    pm_free(&rm); pt_free(&pt);
    return out;
}

MooValue moo_tok_dekodiere(MooValue tok, MooValue ids) {
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "decode: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    /* Ids aus Tensor[n] ODER Liste von Zahlen lesen. */
    int64_t n; const float* td = NULL; MooValue lst = moo_none();
    if (ids.tag == MOO_TENSOR) {
        MooTensor* t = MV_TENSOR(ids);
        n = t->size; td = t->data;
    } else if (ids.tag == MOO_LIST) {
        lst = ids; n = moo_list_iter_len(ids);
    } else {
        pt_free(&pt);
        moo_throw(moo_error("decode: erwarte Token-Ids als Tensor oder Liste"));
        return moo_none();
    }
    /* Gesamtlaenge vorab bestimmen (mit Id-Validierung). */
    int64_t total = 0;
    for (int64_t i = 0; i < n; i++) {
        double dv = td ? (double)td[i] : moo_as_number(MV_LIST(lst)->items[i]);
        if (!(dv >= 0.0) || dv >= (double)pt.V) {
            pt_free(&pt);
            char msg[160];
            snprintf(msg, sizeof(msg), "decode: ungueltige Token-Id %.0f "
                     "(gueltig 0..%u)", dv, pt.V - 1);
            moo_throw(moo_error(msg)); return moo_none();
        }
        total += pt.tok_len[(uint32_t)dv];
    }
    uint8_t* buf = (uint8_t*)malloc(total ? (size_t)total : 1);
    if (!buf) { pt_free(&pt); moo_throw(moo_error("decode: nicht genug Speicher")); return moo_none(); }
    int64_t w = 0;
    for (int64_t i = 0; i < n; i++) {
        double dv = td ? (double)td[i] : moo_as_number(MV_LIST(lst)->items[i]);
        uint32_t id = (uint32_t)dv;
        memcpy(buf + w, pt.tok_ptr[id], pt.tok_len[id]);
        w += pt.tok_len[id];
    }
    MooValue s = moo_string_new_len((const char*)buf, (int32_t)w);
    free(buf); pt_free(&pt);
    return s;
}

/* ================================================================== *
 * SPEICHERN / LADEN / INFO / HASH
 * ================================================================== */

MooValue moo_tok_speichern(MooValue tok, MooValue pfad) {
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "tokenizer_speichern: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    pt_free(&pt);   /* Validierung reichte; wir schreiben die Rohbytes */
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_error("tokenizer_speichern: erwarte einen Dateinamen"));
        return moo_none();
    }
    FILE* f = fopen(MV_STR(pfad)->chars, "wb");
    if (!f) {
        char msg[600];
        snprintf(msg, sizeof(msg), "tokenizer_speichern: kann \"%s\" nicht schreiben",
                 MV_STR(pfad)->chars);
        moo_throw(moo_error(msg)); return moo_none();
    }
    size_t len = (size_t)MV_STR(tok)->length;
    size_t wr = fwrite(MV_STR(tok)->chars, 1, len, f);
    int cerr = fclose(f);
    if (wr != len || cerr != 0) {
        moo_throw(moo_error("tokenizer_speichern: Schreiben unvollstaendig"));
        return moo_none();
    }
    return moo_bool(true);
}

MooValue moo_tok_laden(MooValue pfad) {
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_error("tokenizer_laden: erwarte einen Dateinamen"));
        return moo_none();
    }
    FILE* f = fopen(MV_STR(pfad)->chars, "rb");
    if (!f) {
        char msg[600];
        snprintf(msg, sizeof(msg), "tokenizer_laden: kann \"%s\" nicht oeffnen",
                 MV_STR(pfad)->chars);
        moo_throw(moo_error(msg)); return moo_none();
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); moo_throw(moo_error("tokenizer_laden: Datei nicht seekbar")); return moo_none(); }
    long sz = ftell(f);
    if (sz < 0 || sz > (long)MOO_MAX_ALLOC_SIZE) { fclose(f); moo_throw(moo_error("tokenizer_laden: Datei zu gross oder ungueltig")); return moo_none(); }
    rewind(f);
    uint8_t* buf = (uint8_t*)malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) { fclose(f); moo_throw(moo_error("tokenizer_laden: nicht genug Speicher")); return moo_none(); }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); moo_throw(moo_error("tokenizer_laden: Datei unvollstaendig gelesen")); return moo_none(); }
    MooValue tok = moo_string_new_len((const char*)buf, (int32_t)sz);
    free(buf);
    /* Header validieren — kaputte Datei soll beim Laden auffallen, nicht erst
     * beim ersten encode. */
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        moo_release(tok);
        char msg[200]; snprintf(msg, sizeof(msg), "tokenizer_laden: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    pt_free(&pt);
    return tok;
}

MooValue moo_tok_info(MooValue tok) {
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "tokenizer_info: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    uint64_t h = fnv1a64(pt.blob, pt.blob_len);
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h);
    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("version"), moo_number((double)pt.version));
    moo_dict_set(d, moo_string_new("vokab"),   moo_number((double)pt.V));
    moo_dict_set(d, moo_string_new("merges"),  moo_number((double)pt.M));
    moo_dict_set(d, moo_string_new("spezial"), moo_number((double)pt.S));
    moo_dict_set(d, moo_string_new("flags"),   moo_number((double)pt.flags));
    moo_dict_set(d, moo_string_new("hash"),    moo_string_new(hex));
    pt_free(&pt);
    return d;
}

MooValue moo_tok_hash(MooValue tok) {
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "tokenizer_hash: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    uint64_t h = fnv1a64(pt.blob, pt.blob_len);
    pt_free(&pt);
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h);
    return moo_string_new(hex);
}

/* ================================================================== *
 * KIP-T3: CHAT-/SPEZIAL-TOKENS + TEMPLATE-RENDER (X3 §1, X2 §3)
 * ------------------------------------------------------------------
 * Reservierter Chat-Token-Satz. NAME = Lookup-Schluessel (Render-API),
 * MARKER = die Bytes, die decode() fuer diese Id ausgibt. Spezial-Ids
 * liegen strukturell HINTER Basis+Merges (>=256+M) — daher kann weder ein
 * Byte-Encode noch ein Merge je eine Spezial-Id erzeugen (Injection-Schutz:
 * Steuer-Tokens gelangen NUR out-of-band ueber diese Render-API in die
 * Sequenz, nie aus rohem User-/Tool-Text).
 * ================================================================== */
#define CHAT_N 11
static const char* const CHAT_NAMES[CHAT_N] = {
    "bos", "eos", "pad", "ende",
    "system", "user", "assistant", "tool", "tool_result",
    "werkzeug", "/werkzeug"
};
static const char* const CHAT_MARKERS[CHAT_N] = {
    "<|bos|>", "<|eos|>", "<|pad|>", "<|ende|>",
    "<|system|>", "<|user|>", "<|assistant|>", "<|tool|>", "<|tool_result|>",
    "<|werkzeug|>", "<|/werkzeug|>"
};
/* Erlaubte Rollen-Startnamen (Teilmenge — NICHT bos/eos/pad/ende/werkzeug). */
static bool chat_is_rolle(const char* name, uint32_t n) {
    static const char* const R[5] = { "system", "user", "assistant", "tool", "tool_result" };
    for (int i = 0; i < 5; i++)
        if (strlen(R[i]) == n && memcmp(R[i], name, n) == 0) return true;
    return false;
}

/* chat_init(tok): haengt den reservierten Chat-Token-Satz an ein T2-Artefakt
 * (S==0) an -> neues Artefakt mit S==CHAT_N. Deterministisch, kein Training. */
MooValue moo_tok_chat_init(MooValue tok) {
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "tokenizer_chat_init: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    if (pt.S != 0) {
        pt_free(&pt);
        moo_throw(moo_error("tokenizer_chat_init: Artefakt hat bereits Spezial-Tokens"));
        return moo_none();
    }
    uint32_t V0 = pt.V, M = pt.M;
    uint32_t Vn = V0 + CHAT_N;
    if (Vn > TOK_VOKAB_MAX) { pt_free(&pt); moo_throw(moo_error("tokenizer_chat_init: Vokabular zu gross")); return moo_none(); }

    /* Groesse: Header + Merges + Vokabtabelle(alt) + Vokab(neu marker) + Spezial. */
    size_t sz = TOK_HEADER + (size_t)M * 8u;
    for (uint32_t id = 0; id < V0; id++) sz += 4u + pt.tok_len[id];
    for (int i = 0; i < CHAT_N; i++) sz += 4u + (uint32_t)strlen(CHAT_MARKERS[i]);   /* Vokab-Bytes = Marker */
    for (int i = 0; i < CHAT_N; i++) sz += 4u + 4u + (uint32_t)strlen(CHAT_NAMES[i]); /* Spezial: id+namelen+name */

    uint8_t* blob = (uint8_t*)malloc(sz);
    if (!blob) { pt_free(&pt); moo_throw(moo_error("tokenizer_chat_init: nicht genug Speicher")); return moo_none(); }
    memcpy(blob, TOK_MAGIC, TOK_MAGIC_N);
    put_u32(blob + 8, TOK_VERSION);
    put_u32(blob + 12, pt.flags);
    put_u32(blob + 16, Vn);
    put_u32(blob + 20, M);
    put_u32(blob + 24, (uint32_t)CHAT_N);
    put_u32(blob + 28, 0u);
    size_t off = TOK_HEADER;
    memcpy(blob + off, pt.merges, (size_t)M * 8u); off += (size_t)M * 8u;
    /* bestehende Vokabtabelle 0..V0-1 unveraendert kopieren */
    for (uint32_t id = 0; id < V0; id++) {
        put_u32(blob + off, pt.tok_len[id]); off += 4;
        memcpy(blob + off, pt.tok_ptr[id], pt.tok_len[id]); off += pt.tok_len[id];
    }
    /* neue Vokab-Eintraege: Marker-Bytes fuer Ids V0..V0+CHAT_N-1 */
    for (int i = 0; i < CHAT_N; i++) {
        uint32_t ml = (uint32_t)strlen(CHAT_MARKERS[i]);
        put_u32(blob + off, ml); off += 4;
        memcpy(blob + off, CHAT_MARKERS[i], ml); off += ml;
    }
    /* Spezial-Sektion: { id, namelen, name } */
    for (int i = 0; i < CHAT_N; i++) {
        uint32_t nl = (uint32_t)strlen(CHAT_NAMES[i]);
        put_u32(blob + off, V0 + (uint32_t)i); off += 4;
        put_u32(blob + off, nl); off += 4;
        memcpy(blob + off, CHAT_NAMES[i], nl); off += nl;
    }
    pt_free(&pt);
    MooValue out = moo_string_new_len((const char*)blob, (int32_t)sz);
    free(blob);
    return out;
}

/* spezial_id(tok, name) -> Number (Id) oder wirft. */
MooValue moo_tok_spezial_id(MooValue tok, MooValue name) {
    if (name.tag != MOO_STRING) { moo_throw(moo_error("tokenizer_spezial_id: erwarte einen Namen")); return moo_none(); }
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "tokenizer_spezial_id: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    int64_t id = spec_lookup(&pt, MV_STR(name)->chars, (uint32_t)MV_STR(name)->length);
    pt_free(&pt);
    if (id < 0) { moo_throw(moo_error("tokenizer_spezial_id: unbekanntes Spezial-Token")); return moo_none(); }
    return moo_number((double)id);
}

/* ---- Dynamischer Id/Masken-Puffer fuer den Render ---- */
typedef struct { int32_t* ids; uint8_t* mask; int64_t len, cap; } IdVec;
static bool iv_push(IdVec* v, int32_t id, uint8_t m) {
    if (v->len >= v->cap) {
        int64_t nc = v->cap ? v->cap * 2 : 64;
        int32_t* ni = (int32_t*)realloc(v->ids, (size_t)nc * sizeof(int32_t));
        if (!ni) return false;
        v->ids = ni;
        uint8_t* nm = (uint8_t*)realloc(v->mask, (size_t)nc);
        if (!nm) return false;
        v->mask = nm; v->cap = nc;
    }
    v->ids[v->len] = id; v->mask[v->len] = m; v->len++;
    return true;
}
/* Inhalt byte-encoden und mit Maske m anhaengen. leerer Inhalt = nichts. */
static bool render_content(const ParsedTok* pt, PMap* rm, const uint8_t* txt,
                           int64_t tlen, IdVec* out, uint8_t m) {
    if (tlen <= 0) return true;
    int32_t* seq = (int32_t*)malloc((size_t)tlen * sizeof(int32_t));
    if (!seq) return false;
    for (int64_t i = 0; i < tlen; i++) seq[i] = (int32_t)txt[i];
    int64_t len = tlen;
    bpe_apply(pt, rm, seq, &len);
    for (int64_t i = 0; i < len; i++)
        if (!iv_push(out, seq[i], m)) { free(seq); return false; }
    free(seq);
    return true;
}

/* rendern(tok, dialog, mit_bos?) -> Dict { ids, loss_maske, stop_ids }.
 * dialog = Liste von Dicts { rolle, inhalt }. Loss-Maske: 1 auf
 * Assistant-Inhalt + dessen <|ende|>, 0 sonst. */
MooValue moo_tok_rendern(MooValue tok, MooValue dialog, MooValue mit_bos) {
    ParsedTok pt; const char* err = NULL;
    if (!tok_parse(tok, &pt, &err)) {
        char msg[200]; snprintf(msg, sizeof(msg), "tokenizer_rendern: %s", err);
        moo_throw(moo_error(msg)); return moo_none();
    }
    if (pt.S == 0) { pt_free(&pt); moo_throw(moo_error("tokenizer_rendern: kein Chat-Tokenizer (erst tokenizer_chat_init)")); return moo_none(); }
    if (dialog.tag != MOO_LIST) { pt_free(&pt); moo_throw(moo_error("tokenizer_rendern: erwarte eine Dialogliste")); return moo_none(); }
    int64_t bos_id  = spec_lookup(&pt, "bos", 3);
    int64_t eos_id  = spec_lookup(&pt, "eos", 3);
    int64_t ende_id = spec_lookup(&pt, "ende", 4);
    if (bos_id < 0 || eos_id < 0 || ende_id < 0) { pt_free(&pt); moo_throw(moo_error("tokenizer_rendern: Steuer-Tokens fehlen im Artefakt")); return moo_none(); }

    PMap rm;
    if (!build_rank_map(&pt, &rm)) { pm_free(&rm); pt_free(&pt); moo_throw(moo_error("tokenizer_rendern: nicht genug Speicher")); return moo_none(); }

    bool bos = true;   /* Default an; explizit falsch abschaltbar */
    if (mit_bos.tag == MOO_BOOL) bos = MV_BOOL(mit_bos);

    IdVec v; v.ids = NULL; v.mask = NULL; v.len = 0; v.cap = 0;
    bool oom = false;
    const char* rerr = NULL;
    if (bos && !iv_push(&v, (int32_t)bos_id, 0)) oom = true;

    int32_t cnt = moo_list_iter_len(dialog);
    for (int32_t i = 0; i < cnt && !oom && !rerr; i++) {
        MooValue msg = moo_list_iter_get(dialog, i);           /* +1 */
        if (msg.tag != MOO_DICT) { moo_release(msg); rerr = "Dialog-Eintrag ist kein Dict"; break; }
        MooValue rolleV  = moo_dict_get(msg, moo_string_new("rolle"));   /* +1, Key konsumiert */
        MooValue inhaltV = moo_dict_get(msg, moo_string_new("inhalt"));  /* +1 */
        if (rolleV.tag != MOO_STRING) { moo_release(rolleV); moo_release(inhaltV); moo_release(msg); rerr = "Dialog-Eintrag ohne Textfeld 'rolle'"; break; }
        const char* rname = MV_STR(rolleV)->chars;
        uint32_t rlen = (uint32_t)MV_STR(rolleV)->length;
        if (!chat_is_rolle(rname, rlen)) { moo_release(rolleV); moo_release(inhaltV); moo_release(msg); rerr = "unbekannte Rolle (erlaubt: system/user/assistant/tool/tool_result)"; break; }
        int64_t role_id = spec_lookup(&pt, rname, rlen);
        uint8_t is_assist = (rlen == 9 && memcmp(rname, "assistant", 9) == 0) ? 1u : 0u;

        /* Rollen-Start (Maske 0), dann Inhalt (Assistant=1), dann <|ende|>. */
        if (!iv_push(&v, (int32_t)role_id, 0)) { oom = true; }
        if (!oom && inhaltV.tag == MOO_STRING) {
            if (!render_content(&pt, &rm, (const uint8_t*)MV_STR(inhaltV)->chars,
                                (int64_t)MV_STR(inhaltV)->length, &v, is_assist)) oom = true;
        } else if (!oom && inhaltV.tag != MOO_STRING && inhaltV.tag != MOO_NONE) {
            rerr = "Feld 'inhalt' muss Text sein";
        }
        if (!oom && !rerr && !iv_push(&v, (int32_t)ende_id, is_assist)) oom = true;

        moo_release(rolleV); moo_release(inhaltV); moo_release(msg);
    }
    /* globales EOS beendet den Dialog (Maske 0). */
    if (!oom && !rerr && !iv_push(&v, (int32_t)eos_id, 0)) oom = true;

    if (oom || rerr) {
        free(v.ids); free(v.mask); pm_free(&rm); pt_free(&pt);
        moo_throw(moo_error(rerr ? rerr : "tokenizer_rendern: nicht genug Speicher"));
        return moo_none();
    }

    /* Ergebnis-Tensoren (v.len >= 1 durch EOS). */
    int32_t shape[1] = { (int32_t)v.len };
    MooTensor* ti = moo_tensor_raw(1, shape);
    MooTensor* tm = moo_tensor_raw(1, shape);
    if (!ti || !tm) {
        if (ti) { MooValue x; x.tag = MOO_TENSOR; moo_val_set_ptr(&x, ti); moo_release(x); }
        if (tm) { MooValue x; x.tag = MOO_TENSOR; moo_val_set_ptr(&x, tm); moo_release(x); }
        free(v.ids); free(v.mask); pm_free(&rm); pt_free(&pt);
        return moo_none();   /* moo_tensor_raw hat geworfen */
    }
    for (int64_t i = 0; i < v.len; i++) { ti->data[i] = (float)v.ids[i]; tm->data[i] = (float)v.mask[i]; }
    free(v.ids); free(v.mask); pm_free(&rm); pt_free(&pt);

    MooValue idsv; idsv.tag = MOO_TENSOR; moo_val_set_ptr(&idsv, ti);
    MooValue mskv; mskv.tag = MOO_TENSOR; moo_val_set_ptr(&mskv, tm);
    MooValue stop = moo_list_new(2);
    moo_list_append(stop, moo_number((double)eos_id));
    moo_list_append(stop, moo_number((double)ende_id));
    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("ids"), idsv);
    moo_dict_set(d, moo_string_new("loss_maske"), mskv);
    moo_dict_set(d, moo_string_new("stop_ids"), stop);
    return d;
}
