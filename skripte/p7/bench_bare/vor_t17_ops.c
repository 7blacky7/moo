/* P7-T20: Referenz "vor T17" fuer die KVM-Messung.
 * Exakt die Arithmetik/Vergleiche/truthy aus moo_bare.c, files-Version
 * 207403 (vor P7-T17). cc_vor_t17.sh benennt die aktuellen Definitionen in
 * moo_bare.c per -D um und linkt diese Datei dazu. Nur fuer Messungen. */
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint64_t tag; uint64_t data; } MooValue;

static inline double as_num(MooValue v) { double d; __builtin_memcpy(&d, &v.data, sizeof d); return d; }
static inline MooValue num(double n) { MooValue v; v.tag = 0; __builtin_memcpy(&v.data, &n, sizeof n); return v; }
static inline MooValue boo(bool b) { MooValue v; v.tag = 2; v.data = (uint64_t)b; return v; }

MooValue moo_add(MooValue a, MooValue b) { return num(as_num(a) + as_num(b)); }
MooValue moo_sub(MooValue a, MooValue b) { return num(as_num(a) - as_num(b)); }
MooValue moo_mul(MooValue a, MooValue b) { return num(as_num(a) * as_num(b)); }
MooValue moo_div(MooValue a, MooValue b) { return num(as_num(a) / as_num(b)); }
MooValue moo_lt(MooValue a, MooValue b) { return boo(as_num(a) < as_num(b)); }
MooValue moo_gt(MooValue a, MooValue b) { return boo(as_num(a) > as_num(b)); }
MooValue moo_lte(MooValue a, MooValue b) { return boo(as_num(a) <= as_num(b)); }
MooValue moo_gte(MooValue a, MooValue b) { return boo(as_num(a) >= as_num(b)); }
bool moo_is_truthy(MooValue v) {
    if (v.tag == 0) return as_num(v) != 0.0;
    if (v.tag == 2) return (bool)v.data;
    return false; /* Strings kommen im Bench nicht vor */
}
