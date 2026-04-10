#ifndef MOO_RUNTIME_H
#define MOO_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>

// === Tag-Definitionen ===
typedef enum {
    MOO_NUMBER  = 0,
    MOO_STRING  = 1,
    MOO_BOOL    = 2,
    MOO_NONE    = 3,
    MOO_LIST    = 4,
    MOO_DICT    = 5,
    MOO_FUNC    = 6,
    MOO_OBJECT  = 7,
    MOO_ERROR   = 8,
} MooTag;

// === Forward Declarations ===
typedef struct MooString MooString;
typedef struct MooList MooList;
typedef struct MooDict MooDict;
typedef struct MooObject MooObject;
typedef struct MooFunc MooFunc;
typedef struct MooValue MooValue;

// === MooValue: Der universelle Wert ===
struct MooValue {
    uint8_t tag;
    union {
        double number;
        MooString* string;
        bool boolean;
        MooList* list;
        MooDict* dict;
        MooFunc* func;
        MooObject* object;
        char* error_msg;
    } data;
};

// === String ===
struct MooString {
    char* chars;
    int32_t length;
    int32_t capacity;
};

// === List ===
struct MooList {
    MooValue* items;
    int32_t length;
    int32_t capacity;
};

// === Dict (einfache Hash-Map) ===
typedef struct {
    MooString* key;
    MooValue value;
    bool occupied;
} MooDictEntry;

struct MooDict {
    MooDictEntry* entries;
    int32_t count;
    int32_t capacity;
};

// === Function ===
struct MooFunc {
    void* fn_ptr;
    int32_t arity;
    char* name;
};

// === Object ===
typedef struct {
    MooString* name;
    MooValue value;
} MooProperty;

struct MooObject {
    char* class_name;
    MooProperty* properties;
    int32_t prop_count;
    int32_t prop_capacity;
    MooObject* parent;
};

// === Fehlerbehandlung ===
#define MOO_TRY_STACK_SIZE 64
extern jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
extern int moo_try_depth;
extern MooValue moo_last_error;

// === Konstruktoren ===
MooValue moo_number(double n);
MooValue moo_bool(bool b);
MooValue moo_none(void);
MooValue moo_error(const char* msg);

// === Typ-Pruefung ===
bool moo_is_number(MooValue v);
bool moo_is_string(MooValue v);
bool moo_is_bool(MooValue v);
bool moo_is_none(MooValue v);
bool moo_is_list(MooValue v);
bool moo_is_dict(MooValue v);
bool moo_is_truthy(MooValue v);
const char* moo_type_name(MooValue v);

// === Konvertierungen ===
double moo_as_number(MooValue v);
bool moo_as_bool(MooValue v);

// === String-Funktionen ===
MooValue moo_string_new(const char* chars);
MooValue moo_string_concat(MooValue a, MooValue b);
MooValue moo_string_length(MooValue s);
MooValue moo_string_index(MooValue s, MooValue idx);
MooValue moo_string_compare(MooValue a, MooValue b);
MooValue moo_string_contains(MooValue haystack, MooValue needle);
MooValue moo_string_split(MooValue s, MooValue delim);
MooValue moo_string_replace(MooValue s, MooValue old_s, MooValue new_s);
MooValue moo_string_trim(MooValue s);
MooValue moo_string_upper(MooValue s);
MooValue moo_string_lower(MooValue s);

// === Listen-Funktionen ===
MooValue moo_list_new(int32_t initial_capacity);
MooValue moo_list_from(MooValue* items, int32_t count);
void moo_list_append(MooValue list, MooValue item);
MooValue moo_list_get(MooValue list, MooValue index);
void moo_list_set(MooValue list, MooValue index, MooValue value);
MooValue moo_list_length(MooValue list);
MooValue moo_list_pop(MooValue list);
MooValue moo_list_contains(MooValue list, MooValue item);
MooValue moo_list_reverse(MooValue list);
MooValue moo_list_join(MooValue list, MooValue delim);
int32_t moo_list_iter_len(MooValue list);
MooValue moo_list_iter_get(MooValue list, int32_t index);

// === Dict-Funktionen ===
MooValue moo_dict_new(void);
MooValue moo_dict_get(MooValue dict, MooValue key);
void moo_dict_set(MooValue dict, MooValue key, MooValue value);
MooValue moo_dict_has(MooValue dict, MooValue key);
MooValue moo_dict_keys(MooValue dict);
MooValue moo_dict_values(MooValue dict);
MooValue moo_dict_length(MooValue dict);
void moo_dict_remove(MooValue dict, MooValue key);

// === Objekt-Funktionen ===
MooValue moo_object_new(const char* class_name);
MooValue moo_object_get(MooValue obj, const char* prop);
void moo_object_set(MooValue obj, const char* prop, MooValue value);
void moo_object_set_parent(MooValue obj, MooValue parent);

// === Arithmetik & Vergleiche ===
MooValue moo_add(MooValue a, MooValue b);
MooValue moo_sub(MooValue a, MooValue b);
MooValue moo_mul(MooValue a, MooValue b);
MooValue moo_div(MooValue a, MooValue b);
MooValue moo_mod(MooValue a, MooValue b);
MooValue moo_pow(MooValue a, MooValue b);
MooValue moo_neg(MooValue v);
MooValue moo_eq(MooValue a, MooValue b);
MooValue moo_neq(MooValue a, MooValue b);
MooValue moo_lt(MooValue a, MooValue b);
MooValue moo_gt(MooValue a, MooValue b);
MooValue moo_lte(MooValue a, MooValue b);
MooValue moo_gte(MooValue a, MooValue b);
MooValue moo_and(MooValue a, MooValue b);
MooValue moo_or(MooValue a, MooValue b);
MooValue moo_not(MooValue v);

// === Ausgabe ===
void moo_print(MooValue v);
MooValue moo_to_string(MooValue v);

// === Fehlerbehandlung ===
void moo_throw(MooValue error);

// === Speicher ===
void* moo_alloc(size_t size);
void* moo_realloc(void* ptr, size_t size);
void moo_free(void* ptr);

// === Stdlib ===
MooValue moo_abs(MooValue v);
MooValue moo_sqrt(MooValue v);
MooValue moo_round(MooValue v);
MooValue moo_floor(MooValue v);
MooValue moo_ceil(MooValue v);
MooValue moo_min(MooValue a, MooValue b);
MooValue moo_max(MooValue a, MooValue b);
MooValue moo_random(void);
MooValue moo_type_of(MooValue v);
MooValue moo_input(MooValue prompt);

#endif // MOO_RUNTIME_H
