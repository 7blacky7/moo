#include "moo_struct_box.h"
#include "moo_buffer.h"
#include "moo_struct_value.h"
#include "moo_struct_typed.h"
#include "moo_struct_box_priv.h"
#include "moo_rc.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int moo_struct_valid(const MooStructDescriptor* t) {
    if (!t || !t->name || !t->size || !t->alignment ||
        (t->alignment & (t->alignment - 1)) ||
        t->alignment > MOO_MAX_ALIGN || t->size % t->alignment ||
        (t->field_count && !t->fields)) return 0;
    for (size_t i = 0; i < t->field_count; ++i) {
        const MooStructField* f = &t->fields[i];
        if (!f->name || !f->size || f->offset > t->size ||
            f->size > t->size - f->offset) return 0;
        for (size_t j = 0; j < i; ++j) {
            const MooStructField* g = &t->fields[j];
            if (strcmp(f->name, g->name) == 0 ||
                (f->offset < g->offset + g->size && g->offset < f->offset + f->size))
                return 0;
        }
    }
    return 1;
}


/* Typed metadata is kept separate so the original C descriptor ABI stays stable. */
static int moo_struct_typed_i32_valid(const MooStructTypedDescriptor* t) {
    if (!t || t->version != MOO_STRUCT_TYPED_VERSION || !moo_struct_valid(t->base) ||
        t->field_count == 0 || t->field_count != t->base->field_count ||
        !t->field_types || t->base->alignment < _Alignof(int32_t)) return 0;
    for (size_t i = 0; i < t->field_count; ++i) {
        const MooStructField* f = &t->base->fields[i];
        if (t->field_types[i] != MOO_BUFFER_I32 || f->size != sizeof(int32_t) ||
            f->offset % _Alignof(int32_t)) return 0;
    }
    return 1;
}

MooStructStatus moo_struct_typed_i32_new(const MooStructTypedDescriptor* type,
                                         const void* source, MooValue* out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = (MooValue){MOO_NONE, 0};
    if (!moo_struct_typed_i32_valid(type)) return MOO_STRUCT_INVALID;
    return moo_struct_value_new(type->base, source, out);
}

MooStructStatus moo_struct_typed_i32_get(MooValue value, const MooStructTypedDescriptor* type,
                                         size_t field, int32_t* out) {
    if (!out || !moo_struct_typed_i32_valid(type)) return MOO_STRUCT_INVALID;
    if (field >= type->field_count) return MOO_STRUCT_FIELD;
    MooStructBox* box = NULL;
    MooStructStatus status = moo_struct_value_borrow(value, type->base, &box);
    if (status != MOO_STRUCT_OK) return status;
    return moo_struct_box_get(box, type->base->fields[field].name, out, sizeof(*out));
}

MooStructStatus moo_struct_typed_i32_set(MooValue value, const MooStructTypedDescriptor* type,
                                         size_t field, int32_t input) {
    if (!moo_struct_typed_i32_valid(type)) return MOO_STRUCT_INVALID;
    if (field >= type->field_count) return MOO_STRUCT_FIELD;
    MooStructBox* box = NULL;
    MooStructStatus status = moo_struct_value_borrow(value, type->base, &box);
    if (status != MOO_STRUCT_OK) return status;
    return moo_struct_box_set(box, type->base->fields[field].name, &input, sizeof(input));
}

static const MooStructField* moo_struct_field(const MooStructBox* b, const char* name) {
    for (size_t i = 0; i < b->type->field_count; ++i)
        if (strcmp(b->type->fields[i].name, name) == 0) return &b->type->fields[i];
    return NULL;
}

MooStructStatus moo_struct_box_new(const MooStructDescriptor* t, const void* src, MooStructBox** out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = NULL;
    if (!src || !moo_struct_valid(t)) return MOO_STRUCT_INVALID;
    MooStructBox* b = malloc(sizeof(*b));
    if (!b) return MOO_STRUCT_ALLOC;
    b->data = calloc(1, t->size);
    if (!b->data) { free(b); return MOO_STRUCT_ALLOC; }
    b->refcount = 1; b->type = t; b->typ = NULL; b->besitzer = NULL;
    for (size_t i = 0; i < t->field_count; ++i) {
        const MooStructField* f = &t->fields[i];
        memcpy(b->data + f->offset, (const unsigned char*)src + f->offset, f->size);
    }
    *out = b;
    return MOO_STRUCT_OK;
}


MooStructStatus moo_struct_value_new(const MooStructDescriptor* type, const void* source, MooValue* out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = (MooValue){MOO_NONE, 0};
    MooStructBox* box = NULL;
    MooStructStatus status = moo_struct_box_new(type, source, &box);
    if (status != MOO_STRUCT_OK) return status;
    out->tag = MOO_STRUCT;
    moo_val_set_ptr(out, box);
    return MOO_STRUCT_OK;
}

MooStructStatus moo_struct_value_borrow(MooValue value, const MooStructDescriptor* type, MooStructBox** out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = NULL;
    if (!type || value.tag != MOO_STRUCT || !moo_val_as_ptr(value)) return MOO_STRUCT_INVALID;
    MooStructBox* box = moo_val_as_ptr(value);
    if (box->type != type) return MOO_STRUCT_TYPE;
    *out = box;
    return MOO_STRUCT_OK;
}

MooStructStatus moo_struct_box_retain(MooStructBox* b) {
    if (!b) return MOO_STRUCT_INVALID;
    if (moo_rc_voll(&b->refcount)) return MOO_STRUCT_OVERFLOW;
    moo_rc_inc(&b->refcount);
    return MOO_STRUCT_OK;
}
void moo_struct_box_free(void* ptr) {
    MooStructBox* b = ptr;
    if (!b) return;
    MooStructBox* besitzer = b->besitzer;
    if (!besitzer) free(b->data);
    free(b);
    /* Sicht: Speicher gehoert der Wurzel, nur deren Referenz abgeben. */
    if (besitzer) moo_struct_box_drop(besitzer);
}
void moo_struct_box_drop(MooStructBox* b) {
    if (b && moo_rc_dec(&b->refcount)) moo_struct_box_free(b);
}
MooStructStatus moo_struct_box_copy(const MooStructBox* b, const MooStructDescriptor* t, void* out) {
    if (!b || !t || !out) return MOO_STRUCT_INVALID;
    if (b->type != t) return MOO_STRUCT_TYPE;
    memset(out, 0, t->size);
    for (size_t i = 0; i < t->field_count; ++i) {
        const MooStructField* f = &t->fields[i];
        memcpy((unsigned char*)out + f->offset, b->data + f->offset, f->size);
    }
    return MOO_STRUCT_OK;
}
MooStructStatus moo_struct_box_get(const MooStructBox* b, const char* field, void* out, size_t size) {
    if (!b || !field || !out) return MOO_STRUCT_INVALID;
    const MooStructField* f = moo_struct_field(b, field);
    if (!f || f->size != size) return MOO_STRUCT_FIELD;
    memcpy(out, b->data + f->offset, size);
    return MOO_STRUCT_OK;
}
MooStructStatus moo_struct_box_set(MooStructBox* b, const char* field, const void* value, size_t size) {
    if (!b || !field || !value) return MOO_STRUCT_INVALID;
    const MooStructField* f = moo_struct_field(b, field);
    if (!f || f->size != size) return MOO_STRUCT_FIELD;
    memmove(b->data + f->offset, value, size);
    return MOO_STRUCT_OK;
}

/* ---- P2-T49 (Plan 38c59c64 A1): Laufzeitbeschreibung Version 2 ---- */
static size_t moo_struktur_skalar_groesse(uint8_t skalar) {
    switch (skalar) {
    case MOO_BUFFER_I8: case MOO_BUFFER_U8: return 1;
    case MOO_BUFFER_I16: case MOO_BUFFER_U16: return 2;
    case MOO_BUFFER_I32: case MOO_BUFFER_U32: case MOO_BUFFER_F32: return 4;
    case MOO_BUFFER_I64: case MOO_BUFFER_U64: case MOO_BUFFER_F64: return 8;
    default: return 0;
    }
}

static int moo_struktur_typ_gueltig_tiefe(const MooStrukturTyp* t, uint32_t tiefe) {
    if (!t || tiefe > MOO_STRUKTUR_MAX_TIEFE || t->version != MOO_STRUKTUR_TYP_VERSION ||
        !t->typ_id || !t->feld_anzahl || !t->felder || !moo_struct_valid(&t->basis)) return 0;
    for (uint32_t i = 0; i < t->feld_anzahl; ++i) {
        const MooStrukturFeld* f = &t->felder[i];
        if (!f->name || !f->anzahl || !f->groesse || f->versatz > t->basis.size ||
            f->groesse > t->basis.size - f->versatz || f->groesse % f->anzahl) return 0;
        size_t element = f->groesse / f->anzahl;
        if (f->art == MOO_FELD_ARRAY) {
            if (f->element == MOO_FELD_ARRAY) return 0;
        } else if (f->art != f->element || f->anzahl != 1) {
            return 0;
        }
        switch (f->element) {
        case MOO_FELD_SKALAR:
            if (moo_struktur_skalar_groesse(f->skalar) != element) return 0;
            break;
        case MOO_FELD_BOOL:
            if (element != 1) return 0;
            break;
        case MOO_FELD_STRUKTUR:
            if (!f->struktur || f->struktur->basis.size != element ||
                !moo_struktur_typ_gueltig_tiefe(f->struktur, tiefe + 1)) return 0;
            break;
        default:
            return 0; /* Zeiger u. a. sind nicht boxbar (E5). */
        }
        for (uint32_t j = 0; j < i; ++j)
            if (strcmp(f->name, t->felder[j].name) == 0) return 0;
    }
    return 1;
}

int moo_struktur_typ_gueltig(const MooStrukturTyp* typ) {
    return moo_struktur_typ_gueltig_tiefe(typ, 0);
}

MooStructStatus moo_struct_box_new_typ(const MooStrukturTyp* typ, const void* source, MooStructBox** out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = NULL;
    if (!moo_struktur_typ_gueltig(typ)) return MOO_STRUCT_INVALID;
    MooStructStatus status = moo_struct_box_new(&typ->basis, source, out);
    if (status == MOO_STRUCT_OK) (*out)->typ = typ;
    return status;
}

MooStructStatus moo_struct_box_sicht(MooStructBox* besitzer, const MooStrukturTyp* typ,
                                     size_t versatz, MooStructBox** out) {
    if (!out) return MOO_STRUCT_INVALID;
    *out = NULL;
    if (!besitzer || !moo_struktur_typ_gueltig(typ) || versatz > besitzer->type->size ||
        typ->basis.size > besitzer->type->size - versatz) return MOO_STRUCT_INVALID;
    MooStructBox* wurzel = besitzer->besitzer ? besitzer->besitzer : besitzer;
    MooStructBox* b = malloc(sizeof(*b));
    if (!b) return MOO_STRUCT_ALLOC;
    MooStructStatus status = moo_struct_box_retain(wurzel);
    if (status != MOO_STRUCT_OK) { free(b); return status; }
    b->refcount = 1;
    b->type = &typ->basis;
    b->data = besitzer->data + versatz;
    b->typ = typ;
    b->besitzer = wurzel;
    *out = b;
    return MOO_STRUCT_OK;
}

const MooStrukturTyp* moo_struct_box_typ(const MooStructBox* b) {
    return b ? b->typ : NULL;
}

unsigned char* moo_struct_box_daten(const MooStructBox* b) {
    return b ? b->data : NULL;
}
