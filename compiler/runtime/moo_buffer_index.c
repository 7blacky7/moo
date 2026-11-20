#include "moo_buffer_index.h"
#include "moo_buffer_value.h"
#include "moo_buffer_priv.h"
#include "moo_struct_dyn.h"
#include <float.h>
#include <stdint.h>
#include <string.h>

/* Groesste Ganzzahl, bis zu der jede Ganzzahl exakt als double darstellbar ist. */
#define MOO_BUFFER_EXAKT_MAX 9007199254740992LL /* 2^53 */
/* Strukturdeskriptoren liegen am Speicher und gelten auch fuer Ansichten. */
#define MOO_BUFFER_STRUKTUR_DYN "Strukturpuffer ohne boxbare Feldbeschreibung ist dynamisch nicht zugreifbar"

typedef union {
    int8_t i8; uint8_t u8; int16_t i16; uint16_t u16; int32_t i32; uint32_t u32;
    int64_t i64; uint64_t u64; float f32; double f64;
} MooBufferElement;

/* P2-T23/T53 A3: strikte UTF-8-Pruefung (keine Ueberlangformen, keine
 * Surrogate, hoechstens U+10FFFF). NUL-Bytes sind erlaubt (Text ist binaersicher). */
static int buffer_index(MooValue index, size_t *out);

static int utf8_gueltig(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        if (c < 0x80u) { i++; continue; }
        size_t len;
        uint32_t cp, min;
        if (c >= 0xC2u && c <= 0xDFu) { len = 2; cp = c & 0x1Fu; min = 0x80u; }
        else if ((c & 0xF0u) == 0xE0u) { len = 3; cp = c & 0x0Fu; min = 0x800u; }
        else if (c >= 0xF0u && c <= 0xF4u) { len = 4; cp = c & 0x07u; min = 0x10000u; }
        else return 0;
        if (len > n - i) return 0;
        for (size_t k = 1; k < len; k++) {
            unsigned char f = s[i + k];
            if ((f & 0xC0u) != 0x80u) return 0;
            cp = (cp << 6) | (uint32_t)(f & 0x3Fu);
        }
        if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return 0;
        i += len;
    }
    return 1;
}

MooValue moo_buffer_from_text(MooValue text) {
    if (text.tag != MOO_STRING) {
        moo_throw(moo_error("puffer_aus_text erwartet einen Text"));
        return moo_none();
    }
    MooString *s = MV_STR(text);
    size_t n = s->length > 0 ? (size_t)s->length : 0;
    MooValue out = moo_none();
    MooBuffer *buffer = NULL;
    if (moo_buffer_value_new(MOO_BUFFER_U8, n, &out) != MOO_BUFFER_OK
        || moo_buffer_value_borrow(out, MOO_BUFFER_U8, &buffer) != MOO_BUFFER_OK) {
        moo_release(out);
        moo_throw(moo_error("puffer_aus_text: kein Speicher"));
        return moo_none();
    }
    if (n > 0) {
        MooBufferPin *pin = NULL;
        void *daten = NULL;
        if (moo_buffer_pin(buffer, &pin, &daten) != MOO_BUFFER_OK) {
            moo_release(out);
            moo_throw(moo_error("puffer_aus_text: Puffer nicht beschreibbar"));
            return moo_none();
        }
        memcpy(daten, s->chars, n);
        moo_buffer_unpin(pin);
    }
    return out;
}

MooValue moo_buffer_to_text(MooValue container, MooValue len) {
    MooBuffer *buffer = NULL;
    if (moo_buffer_value_borrow(container, MOO_BUFFER_U8, &buffer) != MOO_BUFFER_OK) {
        moo_release(len);
        moo_throw(moo_error("puffer_als_text erwartet puffer[u8]"));
        return moo_none();
    }
    size_t gesamt = moo_buffer_len(buffer), n = gesamt;
    if (len.tag != MOO_NONE) {
        int ok = buffer_index(len, &n);
        moo_release(len);
        if (!ok || n > gesamt) {
            moo_throw(moo_error("puffer_als_text: Laenge ausserhalb des Puffers"));
            return moo_none();
        }
    }
    if (n > (size_t)INT32_MAX) {
        moo_throw(moo_error("puffer_als_text: Text zu lang"));
        return moo_none();
    }
    if (n == 0) return moo_string_new("");
    MooBufferPin *pin = NULL;
    void *daten = NULL;
    if (moo_buffer_pin(buffer, &pin, &daten) != MOO_BUFFER_OK) {
        moo_throw(moo_error("puffer_als_text: Puffer nicht lesbar"));
        return moo_none();
    }
    MooValue text = moo_none();
    int gueltig = utf8_gueltig((const unsigned char *)daten, n);
    if (gueltig) text = moo_string_new_len((const char *)daten, (int32_t)n);
    moo_buffer_unpin(pin);
    if (!gueltig) {
        moo_throw(moo_error("puffer_als_text: kein gueltiges UTF-8"));
        return moo_none();
    }
    return text;
}

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

/* P2-T49 A4: Elementbreite je Skalartyp (0 = unbekannt). */
static size_t element_groesse(int32_t type) {
    switch (type) {
    case MOO_BUFFER_I8: case MOO_BUFFER_U8: return 1;
    case MOO_BUFFER_I16: case MOO_BUFFER_U16: return 2;
    case MOO_BUFFER_I32: case MOO_BUFFER_U32: case MOO_BUFFER_F32: return 4;
    case MOO_BUFFER_I64: case MOO_BUFFER_U64: case MOO_BUFFER_F64: return 8;
    default: return 0;
    }
}

/* Union-Mitglieder beginnen bei Versatz 0: die ersten n Bytes sind das Element. */
int moo_zahl_in_element(int32_t typ, double zahl, void *ziel) {
    MooBufferElement element;
    size_t n = element_groesse(typ);
    if (!ziel || !n || !element_aus_zahl((MooBufferScalarType)typ, zahl, &element)) return 0;
    memcpy(ziel, &element, n);
    return 1;
}

int moo_element_als_zahl(int32_t typ, const void *quelle, double *out) {
    MooBufferElement element;
    size_t n = element_groesse(typ);
    if (!quelle || !out || !n) return 0;
    memset(&element, 0, sizeof(element));
    memcpy(&element, quelle, n);
    return zahl_aus_element((MooBufferScalarType)typ, &element, out);
}

MooValue moo_buffer_index_get(MooValue container, MooValue index) {
    MooBufferScalarType type;
    MooBuffer *buffer = buffer_of(container, &type);
    MooBufferElement element;
    size_t i = 0;
    double result = 0.0;
    int ok = buffer_index(index, &i);
    moo_release(index);
    if (buffer && (moo_buffer_struct_id(buffer) || buffer->storage->bool_element)) {
        if (!ok || i >= buffer->len) {
            moo_throw(moo_error("Pufferzugriff: Index ausserhalb oder keine nichtnegative Ganzzahl"));
            return moo_none();
        }
        const unsigned char *p = buffer->storage->data
            + (buffer->start + i) * buffer->storage->elem_size;
        if (buffer->storage->bool_element) return moo_bool(*p != 0);
        if (!buffer->storage->struktur) {
            moo_throw(moo_error(MOO_BUFFER_STRUKTUR_DYN));
            return moo_none();
        }
        MooValue out = moo_none();
        moo_struct_box_value(buffer->storage->struktur, p, &out);
        return out; /* Kopie: Aenderungen an der Box wirken nicht auf p[i]. */
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
    moo_release(index);
    if (buffer && (moo_buffer_struct_id(buffer) || buffer->storage->bool_element)) {
        if (!index_ok || i >= buffer->len) {
            moo_throw(moo_error("Pufferzuweisung: Index ausserhalb oder keine nichtnegative Ganzzahl"));
        } else if (moo_buffer_geteilt(buffer)) {
            moo_throw(moo_error(MOO_GETEILT_TEXT));
        } else if (buffer->storage->bool_element) {
            if (value.tag != MOO_BOOL) moo_throw(moo_error("Bool-Array erwartet einen Wahrheitswert"));
            else {
                unsigned char b = MV_BOOL(value) ? 1u : 0u;
                moo_buffer_set(buffer, i, &b);
            }
        } else if (!buffer->storage->struktur) {
            moo_throw(moo_error(MOO_BUFFER_STRUKTUR_DYN));
        } else {
            /* Unbox prueft den nominalen Typ vor der ersten Aenderung und
             * kopiert Blattwerte; Padding im Zielelement bleibt null. */
            void *p = buffer->storage->data
                + (buffer->start + i) * buffer->storage->elem_size;
            moo_struct_unbox_value(&value, buffer->storage->struktur, p);
        }
        moo_release(value);
        return;
    }
    int value_ok = value.tag == MOO_NUMBER
        && element_aus_zahl(type, moo_val_as_double(value), &element);
    moo_release(value);
    if (!index_ok || !value_ok) {
        moo_throw(moo_error("Pufferzuweisung erwartet Index und passenden Elementwert (Ganzzahl im Bereich bzw. Kommazahl)"));
        return;
    }
    MooBufferStatus status = buffer ? moo_buffer_set_scalar(buffer, type, i, &element)
                                    : MOO_BUFFER_INVALID;
    if (status == MOO_BUFFER_PINNED)
        moo_throw(moo_error(MOO_GETEILT_TEXT));
    else if (status != MOO_BUFFER_OK)
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
    MooBufferStatus pin_status = moo_buffer_pin(buffer, &pin, &data);
    if (pin_status != MOO_BUFFER_OK) {
        moo_throw(moo_error(pin_status == MOO_BUFFER_ALIGNMENT
            ? "Native Puffer-Leihgabe: Arrayfeld nicht ABI-ausgerichtet; zuerst in eigenen Puffer kopieren"
            : "Puffer konnte fuer den nativen Aufruf nicht gesperrt werden (kein Speicher)"));
        return NULL;
    }
    *daten = data;
    *laenge = (uint64_t)moo_buffer_len(buffer);
    return pin;
}

struct MooNativeLeihe {
    MooBufferPin *pin;  /* MOO_BUFFER */
    MooTensor *tensor;  /* MOO_TENSOR (geliehen, kein retain: Aufruf ist synchron) */
    int32_t schreibbar;
};

static const char *leih_elementname(int32_t elementtyp) {
    static const char *const namen[] = { "Struktur", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "f32", "f64" };
    return (elementtyp >= 0 && elementtyp <= MOO_BUFFER_F64) ? namen[elementtyp] : "?";
}

static MooNativeLeihe *leih_fehler(const MooValue *wert, int32_t elementtyp, const char *grund) {
    char text[192];
    /* moo_type_name kennt Tensor/Frame nicht (\"Unbekannt\"); hier ausdruecklich benennen. */
    const char *erhalten = !wert ? "nichts"
        : wert->tag == MOO_TENSOR ? "Tensor"
        : wert->tag == MOO_FRAME ? "Frame"
        : moo_type_name(*wert);
    snprintf(text, sizeof(text), "Native Funktion erwartet puffer[%s], erhalten %s%s",
             leih_elementname(elementtyp), erhalten, grund);
    moo_throw(moo_error(text));
    return NULL;
}

static int leih_schreibschutz(const MooValue* wert, int32_t schreibbar) {
    if (!schreibbar) return 1;
    /* Nur nach der Typpruefung fuer Buffer/Frame/Tensor aufgerufen.
     * Den gemeinsamen RC-Praefix direkt nutzen, Buffer mit Speicherstatus.
     * So bleibt das Leihmodul auch einzeln ohne moo_memory verknuepfbar. */
    const void* p = moo_val_as_ptr(*wert);
    int geteilt = p && (wert->tag == MOO_BUFFER
        ? moo_buffer_geteilt((const MooBuffer*)p)
        : moo_rc_ist_geteilt((const int32_t*)p));
    if (!geteilt) return 1;
    moo_throw(moo_error("Native Leihgabe: geteilter Speicher ist eingefroren und nicht schreibbar"));
    return 0;
}

MooNativeLeihe *moo_native_leihen(const MooValue *wert, int32_t elementtyp, uint64_t struktur_id,
                                  int32_t schreibbar, void **daten, uint64_t *laenge) {
    if (daten) *daten = NULL;
    if (laenge) *laenge = 0;
    if (!wert || !daten || !laenge) return leih_fehler(wert, elementtyp, "");
    MooNativeLeihe *leihe = (MooNativeLeihe *)calloc(1, sizeof(*leihe));
    if (!leihe) {
        moo_throw(moo_error("Native Leihgabe: kein Speicher"));
        return NULL;
    }
    leihe->schreibbar = schreibbar;
    if (wert->tag == MOO_TENSOR) {
        if (elementtyp != MOO_BUFFER_F32 || struktur_id != 0) {
            free(leihe);
            return leih_fehler(wert, elementtyp, " (Tensoren nur als puffer[f32])");
        }
        if (!leih_schreibschutz(wert, schreibbar)) { free(leihe); return NULL; }
        MooTensor *t = MV_TENSOR(*wert);
        moo_tensor_host_sichern(t);
        if (moo_native_fehler_offen() || (t->size > 0 && !t->data)) {
            free(leihe);
            if (!moo_native_fehler_offen()) moo_throw(moo_error("Tensor hat keine Host-Sicht"));
            return NULL;
        }
        leihe->tensor = t;
        *daten = t->data;
        *laenge = t->size > 0 ? (uint64_t)t->size : 0;
        return leihe;
    }
    if (wert->tag == MOO_FRAME) {
        MooFrame *f = MV_FRAME(*wert);
        if (elementtyp != MOO_BUFFER_U8 || struktur_id != 0) {
            free(leihe);
            return leih_fehler(wert, elementtyp, " (Frames nur als puffer[u8])");
        }
        if (!f || f->height < 0 || f->stride < 0
            || (uint64_t)f->height > UINT64_MAX / ((uint64_t)f->stride ? (uint64_t)f->stride : 1u)) {
            free(leihe);
            return leih_fehler(wert, elementtyp, " (ungueltige Frame-Groesse)");
        }
        if (!leih_schreibschutz(wert, schreibbar)) { free(leihe); return NULL; }
        *daten = f->pixels;
        *laenge = f->pixels ? (uint64_t)f->height * (uint64_t)f->stride : 0;
        return leihe;
    }
    void *data = NULL;
    MooBufferPin *pin = NULL;
    MooBuffer *buffer = (wert->tag == MOO_BUFFER) ? moo_val_as_ptr(*wert) : NULL;
    int passt = 0;
    if (buffer && struktur_id != 0) {
        passt = moo_buffer_struct_id(buffer) == struktur_id;
    } else if (buffer && elementtyp >= MOO_BUFFER_I8 && elementtyp <= MOO_BUFFER_F64) {
        passt = moo_buffer_scalar_type(buffer) == (MooBufferScalarType)elementtyp;
    }
    if (!passt) {
        free(leihe);
        return leih_fehler(wert, elementtyp, !buffer ? ""
            : struktur_id != 0 ? " mit anderer Strukturdefinition" : " mit anderem Elementtyp");
    }
    if (!leih_schreibschutz(wert, schreibbar)) { free(leihe); return NULL; }
    MooBufferStatus pin_status = moo_buffer_pin(buffer, &pin, &data);
    if (pin_status != MOO_BUFFER_OK) {
        free(leihe);
        moo_throw(moo_error(pin_status == MOO_BUFFER_ALIGNMENT
            ? "Native Puffer-Leihgabe: Arrayfeld nicht ABI-ausgerichtet; zuerst in eigenen Puffer kopieren"
            : "Puffer konnte fuer den nativen Aufruf nicht gesperrt werden (kein Speicher)"));
        return NULL;
    }
    leihe->pin = pin;
    *daten = data;
    *laenge = (uint64_t)moo_buffer_len(buffer);
    return leihe;
}

void moo_native_zurueck(MooNativeLeihe *leihe) {
    if (!leihe) return;
    if (leihe->pin) moo_buffer_unpin(leihe->pin);
    /* Wie moo_tensor_setzen: nach Host-Schreiben ist data die einzige gueltige
       Repraesentation (store/GPU werden bei Bedarf neu gebildet). */
    if (leihe->tensor && leihe->schreibbar) leihe->tensor->valid = MOO_V_DATA;
    free(leihe);
}


/* Zeiger-ABI: keine plattformspezifische MooValue-Uebergabe. Keine globale
 * Registry, die Beschreibung lebt zusammen mit dem typisierten Speicher. */
void moo_buffer_struct_beschreiben(const MooValue *wert, const MooStrukturTyp *typ) {
    MooBuffer *b = wert && wert->tag == MOO_BUFFER ? moo_val_as_ptr(*wert) : NULL;
    if (!b) return; /* Der Konstruktor hat den Fehler bereits gesetzt. */
    if (!moo_struktur_typ_gueltig(typ) || b->storage->type_id != typ->typ_id
        || b->storage->elem_size != typ->basis.size) {
        moo_throw(moo_error("Strukturpuffer: unpassende Feldbeschreibung"));
        return;
    }
    b->storage->struktur = typ;
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
    MooBufferStatus pin_status = moo_buffer_pin(buffer, &pin, &data);
    if (pin_status != MOO_BUFFER_OK) {
        moo_throw(moo_error(pin_status == MOO_BUFFER_ALIGNMENT
            ? "Native Puffer-Leihgabe: Arrayfeld nicht ABI-ausgerichtet; zuerst in eigenen Puffer kopieren"
            : "Puffer konnte fuer den nativen Aufruf nicht gesperrt werden (kein Speicher)"));
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
    if (buffer && type == MOO_BUFFER_SCALAR_NONE
        && (moo_buffer_struct_id(buffer) || buffer->storage->bool_element) && start_ok && len_ok) {
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
