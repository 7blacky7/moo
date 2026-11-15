/* P2-T21/T57 Batch E (Plan f4655d20 E-4): C-Definitionen mit denselben
 * Signaturen wie die @rückruf-Funktionen in compiler/tests/p2/rueckruf_orakel.moos.
 * Clang liefert je Ziel die erwartete define-Zeile des Rueckruf-Thunks. */
#include <stdbool.h>
#include <stdint.h>

typedef struct { int32_t a; int64_t b; } Paar;
typedef struct { float x, y, z; } V3;
typedef struct { uint64_t a, b, c; } G24;

int8_t r_i8(int8_t a, uint8_t b) { (void)b; return a; }
uint16_t r_i16(int16_t a, uint16_t b) { (void)a; return b; }
bool r_bool(bool a) { return a; }
double r_f(float a, double b) { (void)a; return b; }
void *r_ptr(void *p, Paar *q) { (void)q; return p; }
Paar r_paar(Paar p) { return p; }
V3 r_v3(V3 v) { return v; }
G24 r_g24(G24 g) { return g; }
void r_void(int32_t x) { (void)x; }
int32_t r_teile(int32_t a, int32_t b) { return b ? a / b : 0; }
