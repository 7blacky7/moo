#include "moo_buffer_index.h"
#include "moo_buffer_value.h"
#include <float.h>
#include <stdint.h>

/* Groesste Ganzzahl, bis zu der jede Ganzzahl exakt als double darstellbar ist. */
#define MOO_BUFFER_EXAKT_MAX 9007199254740992LL /* 2^53 */
/* P2-T48 D: Strukturelemente haben dynamisch (noch) keine Darstellung. */
#define MOO_BUFFER_STRUKTUR_DYN "Elementzugriff auf puffer[Struktur] nur in nativen Funktionen (Boxing: P2-T49)"

typedef union {
    int8_t i8; uint8_t u8; int16_t i16; uint16_t u16; int32_t i32; uint32_t u32;
    int64_t i64; uint64_t u64; float f32; double f64;
} MooBufferElement;

static int buffer_index(MooValue index, size_t *out) {
    if (index.tag != MOO_NUMBER) return 0;
    double d = moo_val_as_double(index);
    /* Strict upper bound makes the cast defined even when SIZE_MAX rounds up. */
    if (!(d >= 0.0 && d < (double)SIZE_MAX)) return 0;
    size_t n = (size_t)d;
    if ((double)n != d) return 0;
    *out = n;
    return 1;
}

static MooBuffer *buffer_of(MooValue value, MooBufferScalarType *type) {
    MooBuffer *buffer = (value.tag == MOO_BUFFER) ? moo_val_as_ptr(value) : NULL;
    *type = moo_buffer_scalar_type(buffer);
    return buffer;
}

/* Moo-Zahl -> Element. Ganzzahltypen: erst Bereich pruefen (macht den Cast
 * definiert), dann Exaktheit (verwirft Bruchwerte). f32: endliche Werte
 * ausserhalb von FLT_MAX waeren bei der Konversion UB und werden verworfen;
 * NaN/Unendlich bleiben erhalten. */
static int element_aus_zahl(MooBufferScalarType type, double d, MooBufferElement *e) {
    switch (type) {
#define MOO_GANZ(feld, c_typ, lo, hi) \
        if (!(d >= (lo) && d <= (hi))) return 0; \
        e->feld = (c_typ)d; return (double)e->feld == d
    case MOO_BUFFER_I8:  MOO_GANZ(i8, int8_t, -128.0, 127.0);
    case MOO_BUFFER_U8:  MOO_GANZ(u8, uint8_t, 0.0, 255.0);
    case MOO_BUFFER_I16: MOO_GANZ(i16, int16_t, -32768.0, 32767.0);
    case MOO_BUFFER_U16: MOO_GANZ(u16, uint16_t, 0.0, 65535.0);
    case MOO_BUFFER_I32: MOO_GANZ(i32, int32_t, -2147483648.0, 2147483647.0);
    case MOO_BUFFER_U32: MOO_GANZ(u32, uint32_t, 0.0, 4294967295.0);
#undef MOO_GANZ
    case MOO_BUFFER_I64:
        /* Obergrenze exklusiv: 2^63 ist als double exakt, als int64 nicht. */
        if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0)) return 0;
        e->i64 = (int64_t)d; return (double)e->i64 == d;
    case MOO_BUFFER_U64:
        if (!(d >= 0.0 && d < 18446744073709551616.0)) return 0;
        e->u64 = (uint64_t)d; return (double)e->u64 == d;
    case MOO_BUFFER_F32:
        if (d == d && d - d == 0.0 && (d > FLT_MAX || d < -FLT_MAX)) return 0;
        e->f32 = (float)d; return 1;
    case MOO_BUFFER_F64:
        e->f64 = d; return 1;
    default:
        return 0;
    }
}

/* Element -> Moo-Zahl. 64-Bit-Ganzzahlen nur, wenn exakt darstellbar. */
static int zahl_aus_element(MooBufferScalarType type, const MooBufferElement *e, double *out) {
    switch (type) {
    case MOO_BUFFER_I8:  *out = e->i8;  return 1;
    case MOO_BUFFER_U8:  *out = e->u8;  return 1;
    case MOO_BUFFER_I16: *out = e->i16; return 1;
    case MOO_BUFFER_U16: *out = e->u16; return 1;
    case MOO_BUFFER_I32: *out = e->i32; return 1;
    case MOO_BUFFER_U32: *out = e->u32; return 1;
    case MOO_BUFFER_I64:
        if (e->i64 < -MOO_BUFFER_EXAKT_MAX || e->i64 > MOO_BUFFER_EXAKT_MAX) return 0;
        *out = (double)e->i64; return 1;
    case MOO_BUFFER_U64:
        if (e->u64 > (uint64_t)MOO_BUFFER_EXAKT_MAX) return 0;
        *out = (double)e->u64; return 1;
    case MOO_BUFFER_F32: *out = (double)e->f32; return 1;
    case MOO_BUFFER_F64: *out = e->f64; return 1;
    default: return 0;
    }
}

MooValue moo_buffer_index_get(MooValue container, MooValue index) {
    MooBufferScalarType type;
    MooBuffer *buffer = buffer_of(container, &type);
    MooBufferElement element;
    size_t i = 0;
    double result = 0.0;
    int ok = buffer_index(index, &i);
    moo_release(index);
    if (buffer && moo_buffer_struct_id(buffer)) {
        moo_throw(moo_error(MOO_BUFFER_STRUKTUR_DYN));
        return moo_none();
    }
    if (!ok) {
        moo_throw(moo_error("Pufferindex muss eine nichtnegative Ganzzahl sein"));
        return moo_none();
    }
    MooBufferStatus status = buffer ? moo_buffer_get_scalar(buffer, type, i, &element)
                                    : MOO_BUFFER_INVALID;
    if (status != MOO_BUFFER_OK) {
        moo_throw(moo_error("Pufferzugriff: falscher Typ oder Index ausserhalb"));
        return moo_none();
    }
    if (!zahl_aus_element(type, &element, &result)) {
        moo_throw(moo_error("Pufferzugriff: 64-Bit-Wert nicht exakt als Moo-Zahl darstellbar"));
        return moo_none();
    }
    return moo_number(result);
}

void moo_buffer_index_set(MooValue container, MooValue index, MooValue value) {
    MooBufferScalarType type;
    MooBuffer *buffer = buffer_of(container, &type);
    MooBufferElement element;
    size_t i = 0;
    int index_ok = buffer_index(index, &i);
    int value_ok = value.tag == MOO_NUMBER
        && element_aus_zahl(type, moo_val_as_double(value), &element);
    moo_release(index);
    moo_release(value);
    if (buffer && moo_buffer_struct_id(buffer)) {
        moo_throw(moo_error(MOO_BUFFER_STRUKTUR_DYN));
        return;
    }
    if (!index_ok || !value_ok) {
        moo_throw(moo_error("Pufferzuweisung erwartet Index und passenden Elementwert (Ganzzahl im Bereich bzw. Kommazahl)"));
        return;
    }
    MooBufferStatus status = buffer ? moo_buffer_set_scalar(buffer, type, i, &element)
                                    : MOO_BUFFER_INVALID;
    if (status != MOO_BUFFER_OK)
        moo_throw(moo_error("Pufferzuweisung: falscher Typ oder Index ausserhalb"));
}

MooValue moo_buffer_create(int32_t type, MooValue len) {
    size_t n = 0;
    int ok = buffer_index(len, &n);
    moo_release(len);
    if (!ok) {
        moo_throw(moo_error("Pufferlaenge muss eine nichtnegative Ganzzahl sein"));
        return moo_none();
    }
    if (type < MOO_BUFFER_I8 || type > MOO_BUFFER_F64) {
        moo_throw(moo_error("Unbekannter Pufferelementtyp"));
        return moo_none();
    }
    MooValue out;
    MooBufferStatus status = moo_buffer_value_new((MooBufferScalarType)type, n, &out);
    if (status != MOO_BUFFER_OK) {
        moo_throw(moo_error("Puffer konnte nicht angelegt werden (Laenge zu gross oder kein Speicher)"));
        return moo_none();
    }
    return out;
}


void moo_buffer_append_value(MooValue container, MooValue value) {
    MooBufferScalarType type;
    MooBuffer *buffer = buffer_of(container, &type);
    MooBufferElement element;
    int value_ok = value.tag == MOO_NUMBER
        && element_aus_zahl(type, moo_val_as_double(value), &element);
    moo_release(value);
    if (buffer && moo_buffer_struct_id(buffer)) {
        moo_throw(moo_error(MOO_BUFFER_STRUKTUR_DYN));
        return;
    }
    if (!buffer || type == MOO_BUFFER_SCALAR_NONE) {
        moo_throw(moo_error("puffer_anhaengen erwartet einen typisierten Puffer"));
        return;
    }
    if (!value_ok) {
        moo_throw(moo_error("puffer_anhaengen: Wert passt nicht zum Elementtyp (Ganzzahl im Bereich bzw. Kommazahl)"));
        return;
    }
    switch (moo_buffer_append_scalar(buffer, type, &element)) {
    case MOO_BUFFER_OK:
        return;
    case MOO_BUFFER_PINNED:
        moo_throw(moo_error("puffer_anhaengen: Puffer ist durch eine Ansicht oder einen nativen Aufruf gesperrt"));
        return;
    case MOO_BUFFER_INVALID:
        moo_throw(moo_error("puffer_anhaengen: an eine Pufferansicht kann nicht angehaengt werden"));
        return;
    default:
        moo_throw(moo_error("puffer_anhaengen: Puffer konnte nicht wachsen (Laenge zu gross oder kein Speicher)"));
        return;
    }
}

MooBufferPin *moo_buffer_native_pin(const MooValue *value, int32_t type, void **daten, uint64_t *laenge) {
    if (daten) *daten = NULL;
    if (laenge) *laenge = 0;
    MooBuffer *buffer = NULL;
    if (!value || !daten || !laenge || type < MOO_BUFFER_I8 || type > MOO_BUFFER_F64
        || moo_buffer_value_borrow(*value, (MooBufferScalarType)type, &buffer) != MOO_BUFFER_OK) {
        moo_throw(moo_error("Native Funktion erwartet einen Puffer mit passendem Elementtyp (puffer[T])"));
        return NULL;
    }
    MooBufferPin *pin = NULL;
    void *data = NULL;
    if (moo_buffer_pin(buffer, &pin, &data) != MOO_BUFFER_OK) {
        moo_throw(moo_error("Puffer konnte fuer den nativen Aufruf nicht gesperrt werden (kein Speicher)"));
        return NULL;
    }
    *daten = data;
    *laenge = (uint64_t)moo_buffer_len(buffer);
    return pin;
}

MooValue moo_buffer_create_struct(uint64_t size, uint64_t align, uint64_t type_id, MooValue len) {
    size_t n = 0;
    int ok = buffer_index(len, &n);
    moo_release(len);
    if (!ok) {
        moo_throw(moo_error("Pufferlaenge muss eine nichtnegative Ganzzahl sein"));
        return moo_none();
    }
    MooBuffer *buffer = NULL;
    MooBufferStatus status = (size <= SIZE_MAX && align <= SIZE_MAX)
        ? moo_buffer_new_struct((size_t)size, (size_t)align, type_id, n, &buffer)
        : MOO_BUFFER_INVALID;
    if (status != MOO_BUFFER_OK) {
        moo_throw(moo_error("Puffer konnte nicht angelegt werden (Laenge zu gross, Ausrichtung nicht unterstuetzt oder kein Speicher)"));
        return moo_none();
    }
    MooValue out = moo_none();
    out.tag = MOO_BUFFER;
    moo_val_set_ptr(&out, buffer);
    return out;
}

MooBufferPin *moo_buffer_native_pin_struct(const MooValue *value, uint64_t type_id, void **daten, uint64_t *laenge) {
    if (daten) *daten = NULL;
    if (laenge) *laenge = 0;
    MooBuffer *buffer = (value && value->tag == MOO_BUFFER) ? moo_val_as_ptr(*value) : NULL;
    if (!daten || !laenge || !buffer || !type_id || moo_buffer_struct_id(buffer) != type_id) {
        moo_throw(moo_error("Native Funktion erwartet einen Puffer mit passender Struktur (puffer[Struktur], gleiche Strukturdefinition)"));
        return NULL;
    }
    MooBufferPin *pin = NULL;
    void *data = NULL;
    if (moo_buffer_pin(buffer, &pin, &data) != MOO_BUFFER_OK) {
        moo_throw(moo_error("Puffer konnte fuer den nativen Aufruf nicht gesperrt werden (kein Speicher)"));
        return NULL;
    }
    *daten = data;
    *laenge = (uint64_t)moo_buffer_len(buffer);
    return pin;
}

MooValue moo_buffer_view_value(MooValue container, MooValue start, MooValue len) {
    MooBufferScalarType type;
    MooBuffer *buffer = buffer_of(container, &type);
    size_t s = 0, n = 0;
    int start_ok = buffer_index(start, &s);
    int len_ok = buffer_index(len, &n);
    moo_release(start);
    moo_release(len);
    /* P2-T48 D: Ansicht eines Strukturpuffers erbt dessen Struktur-ID. */
    if (buffer && type == MOO_BUFFER_SCALAR_NONE && moo_buffer_struct_id(buffer) && start_ok && len_ok) {
        MooBuffer *view = NULL;
        MooBufferStatus st = moo_buffer_view(buffer, s, n, &view);
        if (st == MOO_BUFFER_BOUNDS) {
            moo_throw(moo_error("puffer_ansicht: Bereich liegt ausserhalb des Puffers"));
            return moo_none();
        }
        if (st != MOO_BUFFER_OK) {
            moo_throw(moo_error("puffer_ansicht: Ansicht konnte nicht angelegt werden (kein Speicher)"));
            return moo_none();
        }
        MooValue out = moo_none();
        out.tag = MOO_BUFFER;
        moo_val_set_ptr(&out, view);
        return out;
    }
    if (!buffer || type == MOO_BUFFER_SCALAR_NONE) {
        moo_throw(moo_error("puffer_ansicht erwartet einen typisierten Puffer"));
        return moo_none();
    }
    if (!start_ok || !len_ok) {
        moo_throw(moo_error("puffer_ansicht: Start und Laenge muessen nichtnegative Ganzzahlen sein"));
        return moo_none();
    }
    MooValue out;
    MooBufferStatus status = moo_buffer_value_view(container, type, s, n, &out);
    if (status == MOO_BUFFER_BOUNDS) {
        moo_throw(moo_error("puffer_ansicht: Bereich liegt ausserhalb des Puffers"));
        return moo_none();
    }
    if (status != MOO_BUFFER_OK) {
        moo_throw(moo_error("puffer_ansicht: Ansicht konnte nicht angelegt werden (kein Speicher)"));
        return moo_none();
    }
    return out;
}
