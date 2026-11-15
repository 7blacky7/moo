/* P2-T19/T56 Batch E (Plan 91033131): C-Gegenstelle der Struktur-ABI-Matrix.
 * abi_*  : C-Callees fuer Moo -> C (extern "C" in abi_matrix.moos).
 * probe_ und moo_ : Moo-Exporte (@einstieg), hier vom C-Hauptprogramm gerufen.
 * Mit -DABI_MATRIX_ORAKEL entstehen statt main() Stub-Definitionen der Exporte,
 * damit Clang fuer jedes Ziel die Referenzsignaturen liefert
 * (skripte/p2/ffi/abi_matrix.sh). */
#include <stdbool.h>
#include <stdint.h>

typedef struct { float x, y, z; } V3;
typedef struct { uint64_t a, b; } P2;
typedef struct { uint64_t a, b, c; } G24;
typedef struct { uint8_t t; uint16_t w; } K4;

/* Moo -> C */
V3 abi_v3_skal(V3 v, float f) { V3 r = { v.x * f, v.y * f, v.z * f }; return r; }
uint64_t abi_zwei(V3 v, P2 p) { return (uint64_t)(v.x + v.y + v.z) + p.a + p.b; }
G24 abi_g24(G24 x, uint64_t k) { G24 r = { x.a + k, x.b + k, x.c + k }; return r; }
uint64_t abi_druck(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, P2 p, V3 v) {
    return a + b + c + d + e + f + p.a + p.b + (uint64_t)(v.x + v.y + v.z);
}
K4 abi_k4(K4 k, double d) { K4 r = { (uint8_t)(k.t + 1), (uint16_t)(k.w + (uint16_t)d) }; return r; }
bool abi_k4_gleich(K4 a, K4 b) { return a.t == b.t && a.w == b.w; }

/* C -> Moo (Exporte aus abi_matrix.moos) */
uint32_t probe_v3(uint64_t adr);
uint64_t probe_zwei(uint64_t k);
uint64_t probe_g24(uint64_t adr);
uint64_t probe_druck(uint64_t start);
uint32_t probe_k4(uint64_t adr);
bool probe_k4_gleich(uint16_t w);
V3 moo_v3_add(V3 a, V3 b);
float moo_v3_summe(V3 v);
uint64_t moo_g24_summe(G24 g, uint64_t k);
G24 moo_g24_plus(G24 g, uint64_t k);
uint64_t moo_druck(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, P2 p, V3 v, K4 k);
K4 moo_k4(K4 k, bool b);

/* Nachzug Review 1e51cd93: A2-Formen, je Form Identitaet Moo->C und C->Moo. */
typedef struct { uint32_t a, b; } P8;              /* 8 Byte */
typedef struct { uint8_t *p; uint32_t n; } PU;      /* Zeiger + u32 */
typedef struct { float f; uint32_t n; } FU;        /* f32 + u32 */
typedef struct { uint8_t t; float w; } Innen;
typedef struct { Innen i; uint16_t k[3]; } Nest;    /* verschachtelt + Arrayfeld, 16 Byte */
typedef struct { uint8_t a, b, c; } B3;            /* 3 Byte */
typedef struct { uint8_t x[5]; } B5;               /* 5 Byte */
typedef struct { uint16_t a, b, c; } B6;           /* 6 Byte */
typedef struct { uint8_t x[7]; } B7;               /* 7 Byte */
typedef struct { double x; } D1;                   /* HFA 1 */
typedef struct { double x, y; } D2;                /* HFA 2 / SysV SSE+SSE */
typedef struct { float x, y, z, w; } F4;           /* HFA 4, 16 Byte */
typedef struct { double x, y, z, w; } D4;          /* HFA 4, 32 Byte */
typedef struct { FU x; D2 y; B3 z; } MischEin;

#define MATRIX_FORMEN(X) X(P8) X(PU) X(FU) X(Nest) X(B3) X(B5) X(B6) X(B7) X(D1) X(D2) X(F4) X(D4)
#define MATRIX_IDENTITAET(F) F abi_id_##F(F x) { return x; }
MATRIX_FORMEN(MATRIX_IDENTITAET)
#define MATRIX_PROTOTYPEN(F) uint32_t probe_id_##F(uint64_t ein, uint64_t aus); F moo_id_##F(F x);
MATRIX_FORMEN(MATRIX_PROTOTYPEN)

/* Drei verschiedene Strukturen unter gemischtem u64/f64/ptr-Druck; void-Fall. */
double abi_misch(uint64_t a, double b, void *p, uint64_t c, double d, uint64_t e, FU x, D2 y, B3 z) {
    (void)p;
    return (double)(a + c + e + x.n + z.a + z.b + z.c) + b + d + (double)x.f + y.x + y.y;
}
void abi_setze(P8 *ziel, P8 wert, Nest n) { ziel->a = wert.a + n.k[2]; ziel->b = wert.b + n.i.t; }
double probe_misch(uint64_t ein);
uint32_t probe_setze(uint64_t ziel, uint64_t ein_wert, uint64_t ein_nest);
double moo_misch(uint64_t a, double b, void *p, uint64_t c, double d, uint64_t e, FU x, D2 y, B3 z);
void moo_setze(P8 *ziel, P8 wert, Nest n);

#ifdef ABI_MATRIX_ORAKEL
uint32_t probe_v3(uint64_t adr) { return (uint32_t)adr; }
uint64_t probe_zwei(uint64_t k) { return k; }
uint64_t probe_g24(uint64_t adr) { return adr; }
uint64_t probe_druck(uint64_t start) { return start; }
uint32_t probe_k4(uint64_t adr) { return (uint32_t)adr; }
bool probe_k4_gleich(uint16_t w) { return w != 0; }
V3 moo_v3_add(V3 a, V3 b) { (void)b; return a; }
float moo_v3_summe(V3 v) { return v.x; }
uint64_t moo_g24_summe(G24 g, uint64_t k) { return g.a + k; }
G24 moo_g24_plus(G24 g, uint64_t k) { (void)k; return g; }
uint64_t moo_druck(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, P2 p, V3 v, K4 k) {
    (void)b; (void)c; (void)d; (void)e; (void)f; (void)p; (void)v; (void)k; return a;
}
K4 moo_k4(K4 k, bool b) { (void)b; return k; }
#define MATRIX_STUBS(F) uint32_t probe_id_##F(uint64_t ein, uint64_t aus) { return (uint32_t)(ein + aus); } \
    F moo_id_##F(F x) { return x; }
MATRIX_FORMEN(MATRIX_STUBS)
double probe_misch(uint64_t ein) { return (double)ein; }
uint32_t probe_setze(uint64_t ziel, uint64_t ein_wert, uint64_t ein_nest) { return (uint32_t)(ziel + ein_wert + ein_nest); }
double moo_misch(uint64_t a, double b, void *p, uint64_t c, double d, uint64_t e, FU x, D2 y, B3 z) {
    (void)p; (void)c; (void)d; (void)e; (void)x; (void)y; (void)z; return (double)a + b;
}
void moo_setze(P8 *ziel, P8 wert, Nest n) { (void)n; *ziel = wert; }
#else
#include <string.h>
#include <stdio.h>

static int fehler, pruefungen;
static void pruefe(const char *name, int ok) {
    pruefungen++;
    if (!ok) { printf("FAIL %s\n", name); fehler++; }
}

static int gleich_P8(const P8 *a, const P8 *b) { return a->a == b->a && a->b == b->b; }
static int gleich_PU(const PU *a, const PU *b) { return a->p == b->p && a->n == b->n; }
static int gleich_FU(const FU *a, const FU *b) { return a->f == b->f && a->n == b->n; }
static int gleich_Nest(const Nest *a, const Nest *b) {
    return a->i.t == b->i.t && a->i.w == b->i.w && memcmp(a->k, b->k, sizeof a->k) == 0;
}
static int gleich_B3(const B3 *a, const B3 *b) { return a->a == b->a && a->b == b->b && a->c == b->c; }
static int gleich_B5(const B5 *a, const B5 *b) { return memcmp(a->x, b->x, sizeof a->x) == 0; }
static int gleich_B6(const B6 *a, const B6 *b) { return a->a == b->a && a->b == b->b && a->c == b->c; }
static int gleich_B7(const B7 *a, const B7 *b) { return memcmp(a->x, b->x, sizeof a->x) == 0; }
static int gleich_D1(const D1 *a, const D1 *b) { return a->x == b->x; }
static int gleich_D2(const D2 *a, const D2 *b) { return a->x == b->x && a->y == b->y; }
static int gleich_F4(const F4 *a, const F4 *b) { return a->x == b->x && a->y == b->y && a->z == b->z && a->w == b->w; }
static int gleich_D4(const D4 *a, const D4 *b) { return a->x == b->x && a->y == b->y && a->z == b->z && a->w == b->w; }

/* Identitaet in beide Richtungen: Moo liest ein, ruft abi_id_F (C), schreibt aus;
   C ruft den Moo-Export moo_id_F. */
#define PRUEFE_ID(F, ...) do { \
        F ein = __VA_ARGS__, aus; memset(&aus, 0, sizeof aus); \
        pruefe("moo->c id " #F, probe_id_##F((uint64_t)(uintptr_t)&ein, (uint64_t)(uintptr_t)&aus) == 1 \
            && gleich_##F(&ein, &aus)); \
        F zurueck = moo_id_##F(ein); \
        pruefe("c->moo id " #F, gleich_##F(&ein, &zurueck)); \
    } while (0)

int main(void) {
    V3 v = {0}; G24 g = {0}; K4 k = {0};
    /* Moo -> C (Moo-Exporte rufen die abi_*-Callees) */
    pruefe("moo->c v3", probe_v3((uint64_t)(uintptr_t)&v) == 1 && v.x == 2.0f && v.y == 4.0f && v.z == 6.0f);
    pruefe("moo->c zwei", probe_zwei(10) == 36);
    pruefe("moo->c g24", probe_g24((uint64_t)(uintptr_t)&g) == 103 && g.a == 101 && g.b == 102 && g.c == 103);
    pruefe("moo->c druck", probe_druck(1) == 42);
    pruefe("moo->c k4", probe_k4((uint64_t)(uintptr_t)&k) == 1024 && k.t == 5 && k.w == 1024);
    pruefe("moo->c k4_gleich wahr", probe_k4_gleich(2));
    pruefe("moo->c k4_gleich falsch", !probe_k4_gleich(3));
    /* C -> Moo (Export-Thunks) */
    V3 a = {1, 2, 3}, b = {10, 20, 30};
    V3 r = moo_v3_add(a, b);
    pruefe("c->moo v3_add", r.x == 11 && r.y == 22 && r.z == 33);
    pruefe("c->moo v3_summe", moo_v3_summe(a) == 6.0f);
    G24 h = {1, 2, 3};
    pruefe("c->moo g24_summe", moo_g24_summe(h, 100) == 106);
    G24 hp = moo_g24_plus(h, 100);
    pruefe("c->moo g24_plus", hp.a == 101 && hp.b == 102 && hp.c == 103);
    P2 p = {7, 8}; K4 kk = {4, 1000};
    pruefe("c->moo druck", moo_druck(1, 2, 3, 4, 5, 6, p, a, kk) == 1040);
    K4 k1 = moo_k4(kk, true), k0 = moo_k4(kk, false);
    pruefe("c->moo k4", k1.t == 5 && k1.w == 1001 && k0.t == 4 && k0.w == 1000);
    static uint8_t marke = 7;
    PRUEFE_ID(P8, {0x11223344u, 0x55667788u});
    PRUEFE_ID(PU, {&marke, 0xA5A5A5A5u});
    PRUEFE_ID(FU, {-1.5f, 4000000000u});
    PRUEFE_ID(Nest, {{0xC3, 2.25f}, {1, 0xFFFF, 777}});
    PRUEFE_ID(B3, {1, 2, 0xFE});
    PRUEFE_ID(B5, {{1, 2, 3, 4, 0xF5}});
    PRUEFE_ID(B6, {1, 0xFFFE, 300});
    PRUEFE_ID(B7, {{9, 8, 7, 6, 5, 4, 0xF3}});
    PRUEFE_ID(D1, {-0.5});
    PRUEFE_ID(D2, {1.25, -2.5});
    PRUEFE_ID(F4, {1.0f, -2.0f, 3.5f, 1e30f});
    PRUEFE_ID(D4, {1.0, 2.0, -3.0, 4.5e100});
    MischEin m = {{0.5f, 10u}, {1.25, 2.5}, {1, 2, 3}};
    double erwartet = (double)(1 + 3 + 5 + 10 + 1 + 2 + 3) + 2.0 + 4.0 + 0.5 + 1.25 + 2.5;
    pruefe("moo->c misch", probe_misch((uint64_t)(uintptr_t)&m) == erwartet);
    pruefe("c->moo misch", moo_misch(1, 2.0, &m, 3, 4.0, 5, m.x, m.y, m.z) == erwartet);
    P8 wert = {100, 200}, ziel = {0, 0};
    Nest nest = {{7, 0.0f}, {0, 0, 30}};
    pruefe("moo->c setze", probe_setze((uint64_t)(uintptr_t)&ziel, (uint64_t)(uintptr_t)&wert,
        (uint64_t)(uintptr_t)&nest) == 1 && ziel.a == 130 && ziel.b == 207);
    P8 ziel2 = {0, 0};
    moo_setze(&ziel2, wert, nest);
    pruefe("c->moo setze", ziel2.a == 130 && ziel2.b == 207);
    if (fehler) return 1;
    printf("PASS ABI-Matrix %d/%d\n", pruefungen, pruefungen);
    return 0;
}
#endif
