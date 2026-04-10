# moo Compiler — Runtime & Typsystem Architektur

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Den moo-Compiler so erweitern, dass er ALLE Features der Sprache nativ kompilieren kann — mit einem erweiterbaren Typsystem, das nie umgebaut werden muss.

**Architecture:** Tagged-Value-System mit C-Runtime. Jeder moo-Wert ist ein `MooValue`-Struct mit Tag (Typ) und Union (Daten). Eine kleine C-Runtime-Library stellt Speicherverwaltung, String-Ops, Listen-Ops, Dict-Ops und OOP-Dispatch bereit. Der LLVM-Codegen erzeugt Aufrufe in diese Runtime.

**Tech Stack:** Rust (Compiler), LLVM/inkwell (Codegen), C (Runtime-Library), cc-Linker

---

## Architektur-Entscheidung: Warum Tagged Values + C-Runtime?

### Problem
Bisher kennt der Compiler nur `f64`. Für Strings, Listen, Dicts, Objekte brauchen wir ein dynamisches Typsystem.

### Alternativen bewertet

| Ansatz | Pro | Contra |
|--------|-----|--------|
| **Statisches Typsystem** | Schnellster Code | Erfordert Typ-Inferenz, komplexer Parser, bricht moo-Philosophie ("einfach") |
| **Alles in LLVM IR** | Kein C nötig | Extrem viel LLVM-Code für String/List/Dict-Ops, unwartbar |
| **Tagged Values + C-Runtime** | Erweiterbar, wartbar, schnell genug | Braucht C-Compiler zum Linken |
| **Bytecode VM** | Portabel | Langsamer als native, zweites Kompilierungsziel |

**Entscheidung: Tagged Values + C-Runtime.** Gründe:
1. **Erweiterbar** — neuer Typ = neuer Tag + neue Runtime-Funktionen, kein Umbau nötig
2. **Wartbar** — String/List/Dict-Logik in lesbarem C statt in LLVM IR
3. **Schnell** — immer noch native Maschinencode, Runtime wird mitgelinkt
4. **Zukunftssicher** — Closures, Async, Generics können später als neue Tags dazukommen

### MooValue Layout (64-bit)

```c
// Tag-Definitionen
enum MooTag {
    MOO_NUMBER  = 0,
    MOO_STRING  = 1,
    MOO_BOOL    = 2,
    MOO_NONE    = 3,
    MOO_LIST    = 4,
    MOO_DICT    = 5,
    MOO_FUNC    = 6,
    MOO_OBJECT  = 7,
    MOO_ERROR   = 8,
    // Erweiterbar: MOO_CLOSURE = 9, MOO_FUTURE = 10, ...
};

// Ein moo-Wert: 16 Bytes (tag + union)
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
    } data;
};
```

---

## Dateistruktur

### Neue Dateien

```
compiler/
├── runtime/
│   ├── moo_runtime.h        # MooValue, Tags, alle Funktions-Deklarationen
│   ├── moo_value.c          # MooValue Konstruktoren, Tag-Checks, Konvertierungen
│   ├── moo_string.c         # String-Operationen (concat, length, index, compare)
│   ├── moo_list.c           # Listen-Operationen (create, append, get, set, length, iterate)
│   ├── moo_dict.c           # Dictionary-Operationen (create, get, set, keys, has)
│   ├── moo_object.c         # OOP: Objekt-Erstellung, Property-Access, Methoden-Dispatch
│   ├── moo_print.c          # zeige/show: Typ-bewusste Ausgabe
│   ├── moo_ops.c            # Arithmetik, Vergleiche, logische Ops auf MooValues
│   ├── moo_error.c          # Fehlerbehandlung: try/catch via setjmp/longjmp
│   └── moo_memory.c         # Speicherverwaltung (malloc/free Wrapper, später GC)
├── src/
│   ├── codegen.rs           # REFACTOR: MooValue statt f64, Runtime-Calls
│   ├── runtime_bindings.rs  # NEU: LLVM-Deklarationen aller Runtime-Funktionen
│   ├── type_info.rs         # NEU: Compile-Time Typ-Tracking für Optimierungen
│   └── ... (rest bleibt)
```

### Bestehende Dateien (nur erweitert)

```
compiler/src/
├── ast.rs          # Bleibt — AST ist bereits vollständig
├── tokens.rs       # Bleibt — Tokens sind bereits vollständig
├── lexer.rs        # Bleibt — Lexer ist bereits vollständig
├── parser.rs       # Bleibt — Parser ist bereits vollständig
├── main.rs         # Erweitern: Runtime-Kompilierung + Linking
├── codegen.rs      # REFACTOR: compile_expr gibt MooValue statt f64 zurück
└── Cargo.toml      # Erweitern: cc-crate für Runtime-Kompilierung
```

---

## Feature-Vollständigkeitsliste

Alles was der Compiler können muss, nach Priorität:

### Phase 1: Runtime-Fundament (Tasks 1-4)
- [ ] MooValue-Struct und Tag-System
- [ ] Runtime-Deklarationen in LLVM
- [ ] Zahlen als MooValue (Migration von f64)
- [ ] Typ-bewusstes `zeige`/`show`

### Phase 2: Strings (Tasks 5-6)
- [ ] String-Literale als MooValue
- [ ] String-Konkatenation (`+`)
- [ ] String-Vergleiche (`==`, `!=`)
- [ ] String-Methoden (`.length`, Index `[0]`)

### Phase 3: Listen (Tasks 7-8)
- [ ] Listen-Literale `[1, 2, 3]`
- [ ] Index-Zugriff `liste[0]`
- [ ] Index-Zuweisung `liste[0] = x`
- [ ] `.append()`, `.length`
- [ ] For-Schleife über Listen

### Phase 4: Dictionaries (Tasks 9-10)
- [ ] Dict-Literale `{"key": value}`
- [ ] Key-Zugriff `dict["key"]`
- [ ] Key-Zuweisung `dict["key"] = x`
- [ ] `.keys()`, `.has()`

### Phase 5: Kontrollfluss-Erweiterungen (Tasks 11-12)
- [ ] Break/Continue in Schleifen
- [ ] Match/Switch-Statement
- [ ] For-Schleife mit Range

### Phase 6: Funktionen++ (Tasks 13-14)
- [ ] Default-Parameter
- [ ] Funktionen als Werte (MooFunc)
- [ ] Lambdas / anonyme Funktionen
- [ ] Closures (Variablen aus äußerem Scope)

### Phase 7: OOP (Tasks 15-17)
- [ ] Klassen-Definition → MooObject-Prototyp
- [ ] `neu`/`new` → Objekt-Instanziierung
- [ ] `selbst`/`this` → Pointer auf aktuelles Objekt
- [ ] Property-Zugriff/Zuweisung (`obj.name`, `obj.name = x`)
- [ ] Methoden-Aufrufe (`obj.methode()`)
- [ ] Vererbung (Parent-Prototyp-Kette)

### Phase 8: Fehlerbehandlung (Tasks 18-19)
- [ ] `versuche`/`try` + `fange`/`catch` via setjmp/longjmp
- [ ] `wirf`/`throw` → MooError
- [ ] Fehlerobjekt im catch-Block verfügbar

### Phase 9: Module (Task 20)
- [ ] `importiere`/`import` → Separate .moo-Dateien kompilieren
- [ ] `exportiere`/`export` → Symbole sichtbar machen
- [ ] Multi-File-Kompilierung

### Phase 10: Stdlib (Task 21)
- [ ] Mathe-Funktionen (abs, sqrt, round, min, max, random)
- [ ] String-Funktionen (split, join, replace, contains, trim)
- [ ] Listen-Funktionen (sort, reverse, map, filter, find)
- [ ] Typ-Funktionen (typ_von/type_of, ist_zahl/is_number, ...)
- [ ] I/O (lese/read → Stdin, Datei-Operationen)

### Phase 11: Zukunft (kein Task, nur Architektur-Vorbereitung)
- Async/Await (MOO_FUTURE tag)
- Generics/Templates
- Pattern Matching (erweitert)
- Garbage Collector statt malloc/free
- Cross-Compilation
- Self-Hosting (moo-Compiler in moo geschrieben)

---

## Tasks

### Task 1: C-Runtime Header und Value-System

**Files:**
- Create: `compiler/runtime/moo_runtime.h`
- Create: `compiler/runtime/moo_value.c`
- Create: `compiler/runtime/moo_memory.c`

- [ ] **Step 1: Runtime-Header mit MooValue-Definition**

```c
// compiler/runtime/moo_runtime.h
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
    MooObject* parent;  // Prototyp-Kette für Vererbung
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

// === Typ-Prüfung ===
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
MooValue moo_string_replace(MooValue s, MooValue old, MooValue new_s);
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
MooValue moo_list_sort(MooValue list);
MooValue moo_list_join(MooValue list, MooValue delim);
// Iteration
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
MooValue moo_object_call_method(MooValue obj, const char* method, MooValue* args, int32_t argc);
void moo_object_set_parent(MooValue obj, MooValue parent);

// === Arithmetik & Vergleiche (auf MooValues) ===
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
// Mathe
MooValue moo_abs(MooValue v);
MooValue moo_sqrt(MooValue v);
MooValue moo_round(MooValue v);
MooValue moo_floor(MooValue v);
MooValue moo_ceil(MooValue v);
MooValue moo_min(MooValue a, MooValue b);
MooValue moo_max(MooValue a, MooValue b);
MooValue moo_random(void);

// Typ-Prüfung
MooValue moo_type_of(MooValue v);

// I/O
MooValue moo_input(MooValue prompt);

#endif // MOO_RUNTIME_H
```

- [ ] **Step 2: moo_value.c — Konstruktoren und Typ-Checks**

```c
// compiler/runtime/moo_value.c
#include "moo_runtime.h"

// Konstruktoren
MooValue moo_number(double n) {
    MooValue v;
    v.tag = MOO_NUMBER;
    v.data.number = n;
    return v;
}

MooValue moo_bool(bool b) {
    MooValue v;
    v.tag = MOO_BOOL;
    v.data.boolean = b;
    return v;
}

MooValue moo_none(void) {
    MooValue v;
    v.tag = MOO_NONE;
    v.data.number = 0;
    return v;
}

MooValue moo_error(const char* msg) {
    MooValue v;
    v.tag = MOO_ERROR;
    v.data.error_msg = strdup(msg);
    return v;
}

// Typ-Prüfung
bool moo_is_number(MooValue v) { return v.tag == MOO_NUMBER; }
bool moo_is_string(MooValue v) { return v.tag == MOO_STRING; }
bool moo_is_bool(MooValue v)   { return v.tag == MOO_BOOL; }
bool moo_is_none(MooValue v)   { return v.tag == MOO_NONE; }
bool moo_is_list(MooValue v)   { return v.tag == MOO_LIST; }
bool moo_is_dict(MooValue v)   { return v.tag == MOO_DICT; }

bool moo_is_truthy(MooValue v) {
    switch (v.tag) {
        case MOO_NUMBER: return v.data.number != 0.0;
        case MOO_STRING: return v.data.string->length > 0;
        case MOO_BOOL:   return v.data.boolean;
        case MOO_NONE:   return false;
        case MOO_LIST:   return v.data.list->length > 0;
        case MOO_DICT:   return v.data.dict->count > 0;
        default:         return true;
    }
}

const char* moo_type_name(MooValue v) {
    switch (v.tag) {
        case MOO_NUMBER: return "Zahl";
        case MOO_STRING: return "Text";
        case MOO_BOOL:   return "Wahrheitswert";
        case MOO_NONE:   return "Nichts";
        case MOO_LIST:   return "Liste";
        case MOO_DICT:   return "Wörterbuch";
        case MOO_FUNC:   return "Funktion";
        case MOO_OBJECT: return "Objekt";
        case MOO_ERROR:  return "Fehler";
        default:         return "Unbekannt";
    }
}

double moo_as_number(MooValue v) {
    if (v.tag == MOO_NUMBER) return v.data.number;
    if (v.tag == MOO_BOOL) return v.data.boolean ? 1.0 : 0.0;
    return 0.0;
}

bool moo_as_bool(MooValue v) {
    return moo_is_truthy(v);
}

MooValue moo_type_of(MooValue v) {
    return moo_string_new(moo_type_name(v));
}
```

- [ ] **Step 3: moo_memory.c — Speicherverwaltung**

```c
// compiler/runtime/moo_memory.c
#include "moo_runtime.h"

void* moo_alloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "moo: Speicher voll!\n");
        exit(1);
    }
    return ptr;
}

void* moo_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "moo: Speicher voll!\n");
        exit(1);
    }
    return new_ptr;
}

void moo_free(void* ptr) {
    free(ptr);
}
```

- [ ] **Step 4: Kompilieren und testen dass die Runtime als .o funktioniert**

```bash
cd compiler/runtime
cc -c -fPIC -O2 moo_value.c -o moo_value.o
cc -c -fPIC -O2 moo_memory.c -o moo_memory.o
```

- [ ] **Step 5: Commit**

```bash
git add compiler/runtime/
git commit --author="Moritz Kolar <moritz.kolar@gmail.com>" -m "feat(runtime): MooValue Tag-System und Speicherverwaltung"
```

---

### Task 2: String-Runtime

**Files:**
- Create: `compiler/runtime/moo_string.c`

- [ ] **Step 1: String-Operationen implementieren**

```c
// compiler/runtime/moo_string.c
#include "moo_runtime.h"

MooValue moo_string_new(const char* chars) {
    MooValue v;
    v.tag = MOO_STRING;
    MooString* s = moo_alloc(sizeof(MooString));
    int32_t len = strlen(chars);
    s->length = len;
    s->capacity = len + 1;
    s->chars = moo_alloc(s->capacity);
    memcpy(s->chars, chars, len + 1);
    v.data.string = s;
    return v;
}

MooValue moo_string_concat(MooValue a, MooValue b) {
    // Beide zu String konvertieren
    MooValue sa = moo_to_string(a);
    MooValue sb = moo_to_string(b);
    int32_t len = sa.data.string->length + sb.data.string->length;
    char* buf = moo_alloc(len + 1);
    memcpy(buf, sa.data.string->chars, sa.data.string->length);
    memcpy(buf + sa.data.string->length, sb.data.string->chars, sb.data.string->length);
    buf[len] = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_string_length(MooValue s) {
    if (s.tag != MOO_STRING) return moo_number(0);
    return moo_number((double)s.data.string->length);
}

MooValue moo_string_index(MooValue s, MooValue idx) {
    if (s.tag != MOO_STRING) return moo_none();
    int32_t i = (int32_t)moo_as_number(idx);
    if (i < 0 || i >= s.data.string->length) return moo_none();
    char buf[2] = { s.data.string->chars[i], '\0' };
    return moo_string_new(buf);
}

MooValue moo_string_compare(MooValue a, MooValue b) {
    if (a.tag != MOO_STRING || b.tag != MOO_STRING) return moo_bool(false);
    return moo_bool(strcmp(a.data.string->chars, b.data.string->chars) == 0);
}

MooValue moo_string_contains(MooValue haystack, MooValue needle) {
    if (haystack.tag != MOO_STRING || needle.tag != MOO_STRING) return moo_bool(false);
    return moo_bool(strstr(haystack.data.string->chars, needle.data.string->chars) != NULL);
}

MooValue moo_string_split(MooValue s, MooValue delim) {
    if (s.tag != MOO_STRING || delim.tag != MOO_STRING) return moo_list_new(0);
    MooValue result = moo_list_new(4);
    char* str = strdup(s.data.string->chars);
    char* token = strtok(str, delim.data.string->chars);
    while (token) {
        moo_list_append(result, moo_string_new(token));
        token = strtok(NULL, delim.data.string->chars);
    }
    moo_free(str);
    return result;
}

MooValue moo_string_replace(MooValue s, MooValue old, MooValue new_s) {
    if (s.tag != MOO_STRING || old.tag != MOO_STRING || new_s.tag != MOO_STRING)
        return s;
    char* src = s.data.string->chars;
    char* find = old.data.string->chars;
    char* repl = new_s.data.string->chars;
    int find_len = old.data.string->length;
    int repl_len = new_s.data.string->length;
    // Einfache Implementierung: Buffer mit genug Platz
    int buf_size = s.data.string->length * 2 + repl_len + 1;
    char* buf = moo_alloc(buf_size);
    char* dst = buf;
    while (*src) {
        if (strncmp(src, find, find_len) == 0) {
            memcpy(dst, repl, repl_len);
            dst += repl_len;
            src += find_len;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_string_trim(MooValue s) {
    if (s.tag != MOO_STRING) return s;
    char* start = s.data.string->chars;
    char* end = start + s.data.string->length - 1;
    while (start <= end && (*start == ' ' || *start == '\t' || *start == '\n')) start++;
    while (end >= start && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
    int len = end - start + 1;
    char* buf = moo_alloc(len + 1);
    memcpy(buf, start, len);
    buf[len] = '\0';
    MooValue result = moo_string_new(buf);
    moo_free(buf);
    return result;
}

MooValue moo_string_upper(MooValue s) {
    if (s.tag != MOO_STRING) return s;
    char* buf = strdup(s.data.string->chars);
    for (int i = 0; buf[i]; i++) {
        if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
    }
    MooValue result = moo_string_new(buf);
    free(buf);
    return result;
}

MooValue moo_string_lower(MooValue s) {
    if (s.tag != MOO_STRING) return s;
    char* buf = strdup(s.data.string->chars);
    for (int i = 0; buf[i]; i++) {
        if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] += 32;
    }
    MooValue result = moo_string_new(buf);
    free(buf);
    return result;
}
```

- [ ] **Step 2: Commit**

---

### Task 3: Listen-Runtime

**Files:**
- Create: `compiler/runtime/moo_list.c`

- [ ] **Step 1: Listen-Operationen implementieren**

```c
// compiler/runtime/moo_list.c
#include "moo_runtime.h"

MooValue moo_list_new(int32_t initial_capacity) {
    if (initial_capacity < 4) initial_capacity = 4;
    MooValue v;
    v.tag = MOO_LIST;
    MooList* l = moo_alloc(sizeof(MooList));
    l->length = 0;
    l->capacity = initial_capacity;
    l->items = moo_alloc(sizeof(MooValue) * initial_capacity);
    v.data.list = l;
    return v;
}

MooValue moo_list_from(MooValue* items, int32_t count) {
    MooValue v = moo_list_new(count);
    for (int32_t i = 0; i < count; i++) {
        moo_list_append(v, items[i]);
    }
    return v;
}

static void list_grow(MooList* l) {
    l->capacity *= 2;
    l->items = moo_realloc(l->items, sizeof(MooValue) * l->capacity);
}

void moo_list_append(MooValue list, MooValue item) {
    MooList* l = list.data.list;
    if (l->length >= l->capacity) list_grow(l);
    l->items[l->length++] = item;
}

MooValue moo_list_get(MooValue list, MooValue index) {
    int32_t i = (int32_t)moo_as_number(index);
    MooList* l = list.data.list;
    if (i < 0) i += l->length; // Negative Indices
    if (i < 0 || i >= l->length) return moo_none();
    return l->items[i];
}

void moo_list_set(MooValue list, MooValue index, MooValue value) {
    int32_t i = (int32_t)moo_as_number(index);
    MooList* l = list.data.list;
    if (i < 0) i += l->length;
    if (i >= 0 && i < l->length) {
        l->items[i] = value;
    }
}

MooValue moo_list_length(MooValue list) {
    return moo_number((double)list.data.list->length);
}

MooValue moo_list_pop(MooValue list) {
    MooList* l = list.data.list;
    if (l->length == 0) return moo_none();
    return l->items[--l->length];
}

MooValue moo_list_contains(MooValue list, MooValue item) {
    MooList* l = list.data.list;
    for (int32_t i = 0; i < l->length; i++) {
        MooValue eq = moo_eq(l->items[i], item);
        if (eq.data.boolean) return moo_bool(true);
    }
    return moo_bool(false);
}

MooValue moo_list_reverse(MooValue list) {
    MooList* l = list.data.list;
    for (int32_t i = 0; i < l->length / 2; i++) {
        MooValue tmp = l->items[i];
        l->items[i] = l->items[l->length - 1 - i];
        l->items[l->length - 1 - i] = tmp;
    }
    return list;
}

// Iteration helpers
int32_t moo_list_iter_len(MooValue list) {
    if (list.tag != MOO_LIST) return 0;
    return list.data.list->length;
}

MooValue moo_list_iter_get(MooValue list, int32_t index) {
    return list.data.list->items[index];
}

MooValue moo_list_join(MooValue list, MooValue delim) {
    MooList* l = list.data.list;
    if (l->length == 0) return moo_string_new("");
    MooValue result = moo_to_string(l->items[0]);
    for (int32_t i = 1; i < l->length; i++) {
        result = moo_string_concat(result, delim);
        result = moo_string_concat(result, moo_to_string(l->items[i]));
    }
    return result;
}
```

- [ ] **Step 2: Commit**

---

### Task 4: Dict-Runtime

**Files:**
- Create: `compiler/runtime/moo_dict.c`

- [ ] **Step 1: Dictionary-Operationen implementieren**

```c
// compiler/runtime/moo_dict.c
#include "moo_runtime.h"

#define DICT_INITIAL_CAPACITY 16
#define DICT_LOAD_FACTOR 0.75

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    while (*str) {
        hash = ((hash << 5) + hash) + (unsigned char)*str++;
    }
    return hash;
}

MooValue moo_dict_new(void) {
    MooValue v;
    v.tag = MOO_DICT;
    MooDict* d = moo_alloc(sizeof(MooDict));
    d->count = 0;
    d->capacity = DICT_INITIAL_CAPACITY;
    d->entries = moo_alloc(sizeof(MooDictEntry) * d->capacity);
    memset(d->entries, 0, sizeof(MooDictEntry) * d->capacity);
    v.data.dict = d;
    return v;
}

static int32_t dict_find_slot(MooDict* d, const char* key) {
    uint32_t hash = hash_string(key);
    int32_t idx = hash % d->capacity;
    while (d->entries[idx].occupied) {
        if (strcmp(d->entries[idx].key->chars, key) == 0) return idx;
        idx = (idx + 1) % d->capacity;
    }
    return idx;
}

static void dict_grow(MooDict* d) {
    int32_t old_cap = d->capacity;
    MooDictEntry* old = d->entries;
    d->capacity *= 2;
    d->entries = moo_alloc(sizeof(MooDictEntry) * d->capacity);
    memset(d->entries, 0, sizeof(MooDictEntry) * d->capacity);
    d->count = 0;
    for (int32_t i = 0; i < old_cap; i++) {
        if (old[i].occupied) {
            int32_t slot = dict_find_slot(d, old[i].key->chars);
            d->entries[slot] = old[i];
            d->count++;
        }
    }
    moo_free(old);
}

void moo_dict_set(MooValue dict, MooValue key, MooValue value) {
    MooDict* d = dict.data.dict;
    MooValue key_str = moo_to_string(key);
    if ((double)d->count / d->capacity > DICT_LOAD_FACTOR) dict_grow(d);
    int32_t slot = dict_find_slot(d, key_str.data.string->chars);
    if (!d->entries[slot].occupied) {
        d->entries[slot].key = key_str.data.string;
        d->entries[slot].occupied = true;
        d->count++;
    }
    d->entries[slot].value = value;
}

MooValue moo_dict_get(MooValue dict, MooValue key) {
    MooDict* d = dict.data.dict;
    MooValue key_str = moo_to_string(key);
    int32_t slot = dict_find_slot(d, key_str.data.string->chars);
    if (d->entries[slot].occupied) return d->entries[slot].value;
    return moo_none();
}

MooValue moo_dict_has(MooValue dict, MooValue key) {
    MooDict* d = dict.data.dict;
    MooValue key_str = moo_to_string(key);
    int32_t slot = dict_find_slot(d, key_str.data.string->chars);
    return moo_bool(d->entries[slot].occupied);
}

MooValue moo_dict_keys(MooValue dict) {
    MooDict* d = dict.data.dict;
    MooValue list = moo_list_new(d->count);
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].occupied) {
            MooValue key;
            key.tag = MOO_STRING;
            key.data.string = d->entries[i].key;
            moo_list_append(list, key);
        }
    }
    return list;
}

MooValue moo_dict_values(MooValue dict) {
    MooDict* d = dict.data.dict;
    MooValue list = moo_list_new(d->count);
    for (int32_t i = 0; i < d->capacity; i++) {
        if (d->entries[i].occupied) {
            moo_list_append(list, d->entries[i].value);
        }
    }
    return list;
}

MooValue moo_dict_length(MooValue dict) {
    return moo_number((double)dict.data.dict->count);
}

void moo_dict_remove(MooValue dict, MooValue key) {
    MooDict* d = dict.data.dict;
    MooValue key_str = moo_to_string(key);
    int32_t slot = dict_find_slot(d, key_str.data.string->chars);
    if (d->entries[slot].occupied) {
        d->entries[slot].occupied = false;
        d->count--;
    }
}
```

- [ ] **Step 2: Commit**

---

### Task 5: Ops, Print und Error-Runtime

**Files:**
- Create: `compiler/runtime/moo_ops.c`
- Create: `compiler/runtime/moo_print.c`
- Create: `compiler/runtime/moo_error.c`

- [ ] **Step 1: Arithmetik und Vergleiche auf MooValues**

```c
// compiler/runtime/moo_ops.c
#include "moo_runtime.h"

MooValue moo_add(MooValue a, MooValue b) {
    // String + irgendwas = Konkatenation
    if (a.tag == MOO_STRING || b.tag == MOO_STRING)
        return moo_string_concat(a, b);
    // Zahl + Zahl
    return moo_number(moo_as_number(a) + moo_as_number(b));
}

MooValue moo_sub(MooValue a, MooValue b) {
    return moo_number(moo_as_number(a) - moo_as_number(b));
}

MooValue moo_mul(MooValue a, MooValue b) {
    return moo_number(moo_as_number(a) * moo_as_number(b));
}

MooValue moo_div(MooValue a, MooValue b) {
    double divisor = moo_as_number(b);
    if (divisor == 0.0) {
        moo_throw(moo_error("Division durch Null!"));
        return moo_none();
    }
    return moo_number(moo_as_number(a) / divisor);
}

MooValue moo_mod(MooValue a, MooValue b) {
    return moo_number(fmod(moo_as_number(a), moo_as_number(b)));
}

MooValue moo_pow(MooValue a, MooValue b) {
    return moo_number(pow(moo_as_number(a), moo_as_number(b)));
}

MooValue moo_neg(MooValue v) {
    return moo_number(-moo_as_number(v));
}

MooValue moo_eq(MooValue a, MooValue b) {
    if (a.tag != b.tag) return moo_bool(false);
    switch (a.tag) {
        case MOO_NUMBER: return moo_bool(a.data.number == b.data.number);
        case MOO_STRING: return moo_string_compare(a, b);
        case MOO_BOOL:   return moo_bool(a.data.boolean == b.data.boolean);
        case MOO_NONE:   return moo_bool(true);
        default:         return moo_bool(false);
    }
}

MooValue moo_neq(MooValue a, MooValue b) {
    return moo_bool(!moo_eq(a, b).data.boolean);
}

MooValue moo_lt(MooValue a, MooValue b) {
    return moo_bool(moo_as_number(a) < moo_as_number(b));
}

MooValue moo_gt(MooValue a, MooValue b) {
    return moo_bool(moo_as_number(a) > moo_as_number(b));
}

MooValue moo_lte(MooValue a, MooValue b) {
    return moo_bool(moo_as_number(a) <= moo_as_number(b));
}

MooValue moo_gte(MooValue a, MooValue b) {
    return moo_bool(moo_as_number(a) >= moo_as_number(b));
}

MooValue moo_and(MooValue a, MooValue b) {
    return moo_bool(moo_is_truthy(a) && moo_is_truthy(b));
}

MooValue moo_or(MooValue a, MooValue b) {
    return moo_bool(moo_is_truthy(a) || moo_is_truthy(b));
}

MooValue moo_not(MooValue v) {
    return moo_bool(!moo_is_truthy(v));
}
```

- [ ] **Step 2: moo_print.c — Typ-bewusste Ausgabe**

```c
// compiler/runtime/moo_print.c
#include "moo_runtime.h"

MooValue moo_to_string(MooValue v) {
    char buf[64];
    switch (v.tag) {
        case MOO_NUMBER: {
            double n = v.data.number;
            if (n == (int64_t)n)
                snprintf(buf, sizeof(buf), "%lld", (long long)n);
            else
                snprintf(buf, sizeof(buf), "%g", n);
            return moo_string_new(buf);
        }
        case MOO_STRING: return v;
        case MOO_BOOL:
            return moo_string_new(v.data.boolean ? "wahr" : "falsch");
        case MOO_NONE:
            return moo_string_new("nichts");
        case MOO_LIST: {
            // [elem1, elem2, ...]
            MooList* l = v.data.list;
            MooValue result = moo_string_new("[");
            for (int32_t i = 0; i < l->length; i++) {
                if (i > 0) result = moo_string_concat(result, moo_string_new(", "));
                MooValue item_str = moo_to_string(l->items[i]);
                if (l->items[i].tag == MOO_STRING) {
                    result = moo_string_concat(result, moo_string_new("\""));
                    result = moo_string_concat(result, item_str);
                    result = moo_string_concat(result, moo_string_new("\""));
                } else {
                    result = moo_string_concat(result, item_str);
                }
            }
            return moo_string_concat(result, moo_string_new("]"));
        }
        case MOO_DICT: {
            MooDict* d = v.data.dict;
            MooValue result = moo_string_new("{");
            bool first = true;
            for (int32_t i = 0; i < d->capacity; i++) {
                if (!d->entries[i].occupied) continue;
                if (!first) result = moo_string_concat(result, moo_string_new(", "));
                first = false;
                result = moo_string_concat(result, moo_string_new("\""));
                MooValue key; key.tag = MOO_STRING; key.data.string = d->entries[i].key;
                result = moo_string_concat(result, key);
                result = moo_string_concat(result, moo_string_new("\": "));
                result = moo_string_concat(result, moo_to_string(d->entries[i].value));
            }
            return moo_string_concat(result, moo_string_new("}"));
        }
        case MOO_OBJECT: {
            snprintf(buf, sizeof(buf), "<Objekt %s>", v.data.object->class_name);
            return moo_string_new(buf);
        }
        case MOO_ERROR:
            return moo_string_new(v.data.error_msg);
        default:
            return moo_string_new("<unbekannt>");
    }
}

void moo_print(MooValue v) {
    MooValue s = moo_to_string(v);
    printf("%s\n", s.data.string->chars);
}
```

- [ ] **Step 3: moo_error.c — Fehlerbehandlung mit setjmp/longjmp**

```c
// compiler/runtime/moo_error.c
#include "moo_runtime.h"

jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
int moo_try_depth = 0;
MooValue moo_last_error;

void moo_throw(MooValue error) {
    moo_last_error = error;
    if (moo_try_depth > 0) {
        moo_try_depth--;
        longjmp(moo_try_stack[moo_try_depth], 1);
    } else {
        fprintf(stderr, "Unbehandelter Fehler: ");
        moo_print(error);
        exit(1);
    }
}
```

- [ ] **Step 4: Commit**

---

### Task 6: Objekt-Runtime

**Files:**
- Create: `compiler/runtime/moo_object.c`

- [ ] **Step 1: OOP-Operationen implementieren**

```c
// compiler/runtime/moo_object.c
#include "moo_runtime.h"

MooValue moo_object_new(const char* class_name) {
    MooValue v;
    v.tag = MOO_OBJECT;
    MooObject* obj = moo_alloc(sizeof(MooObject));
    obj->class_name = strdup(class_name);
    obj->prop_count = 0;
    obj->prop_capacity = 8;
    obj->properties = moo_alloc(sizeof(MooProperty) * obj->prop_capacity);
    obj->parent = NULL;
    v.data.object = obj;
    return v;
}

static int32_t find_property(MooObject* obj, const char* name) {
    for (int32_t i = 0; i < obj->prop_count; i++) {
        if (strcmp(obj->properties[i].name->chars, name) == 0) return i;
    }
    return -1;
}

MooValue moo_object_get(MooValue obj_val, const char* prop) {
    MooObject* obj = obj_val.data.object;
    // Suche in aktueller Instanz
    int32_t idx = find_property(obj, prop);
    if (idx >= 0) return obj->properties[idx].value;
    // Suche in Prototyp-Kette (Vererbung)
    if (obj->parent) {
        MooValue parent_val;
        parent_val.tag = MOO_OBJECT;
        parent_val.data.object = obj->parent;
        return moo_object_get(parent_val, prop);
    }
    return moo_none();
}

void moo_object_set(MooValue obj_val, const char* prop, MooValue value) {
    MooObject* obj = obj_val.data.object;
    int32_t idx = find_property(obj, prop);
    if (idx >= 0) {
        obj->properties[idx].value = value;
        return;
    }
    // Neues Property
    if (obj->prop_count >= obj->prop_capacity) {
        obj->prop_capacity *= 2;
        obj->properties = moo_realloc(obj->properties, sizeof(MooProperty) * obj->prop_capacity);
    }
    MooString* name_str = moo_alloc(sizeof(MooString));
    int32_t len = strlen(prop);
    name_str->chars = strdup(prop);
    name_str->length = len;
    name_str->capacity = len + 1;
    obj->properties[obj->prop_count].name = name_str;
    obj->properties[obj->prop_count].value = value;
    obj->prop_count++;
}

void moo_object_set_parent(MooValue obj, MooValue parent) {
    if (obj.tag == MOO_OBJECT && parent.tag == MOO_OBJECT) {
        obj.data.object->parent = parent.data.object;
    }
}
```

- [ ] **Step 2: Commit**

---

### Task 7: Stdlib-Runtime

**Files:**
- Create: `compiler/runtime/moo_stdlib.c`

- [ ] **Step 1: Mathematische und I/O-Funktionen**

```c
// compiler/runtime/moo_stdlib.c
#include "moo_runtime.h"
#include <time.h>

static bool random_seeded = false;

MooValue moo_abs(MooValue v) {
    return moo_number(fabs(moo_as_number(v)));
}

MooValue moo_sqrt(MooValue v) {
    return moo_number(sqrt(moo_as_number(v)));
}

MooValue moo_round(MooValue v) {
    return moo_number(round(moo_as_number(v)));
}

MooValue moo_floor(MooValue v) {
    return moo_number(floor(moo_as_number(v)));
}

MooValue moo_ceil(MooValue v) {
    return moo_number(ceil(moo_as_number(v)));
}

MooValue moo_min(MooValue a, MooValue b) {
    double na = moo_as_number(a), nb = moo_as_number(b);
    return moo_number(na < nb ? na : nb);
}

MooValue moo_max(MooValue a, MooValue b) {
    double na = moo_as_number(a), nb = moo_as_number(b);
    return moo_number(na > nb ? na : nb);
}

MooValue moo_random(void) {
    if (!random_seeded) {
        srand((unsigned)time(NULL));
        random_seeded = true;
    }
    return moo_number((double)rand() / RAND_MAX);
}

MooValue moo_input(MooValue prompt) {
    if (prompt.tag == MOO_STRING) {
        printf("%s", prompt.data.string->chars);
        fflush(stdout);
    }
    char buf[1024];
    if (fgets(buf, sizeof(buf), stdin)) {
        // Newline entfernen
        int len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        return moo_string_new(buf);
    }
    return moo_none();
}
```

- [ ] **Step 2: Commit**

---

### Task 8: Build-System — Runtime automatisch kompilieren und linken

**Files:**
- Modify: `compiler/Cargo.toml`
- Create: `compiler/build.rs`
- Modify: `compiler/src/main.rs`

- [ ] **Step 1: Cargo build.rs zum automatischen Kompilieren der C-Runtime**

```rust
// compiler/build.rs
fn main() {
    cc::Build::new()
        .file("runtime/moo_value.c")
        .file("runtime/moo_memory.c")
        .file("runtime/moo_string.c")
        .file("runtime/moo_list.c")
        .file("runtime/moo_dict.c")
        .file("runtime/moo_ops.c")
        .file("runtime/moo_print.c")
        .file("runtime/moo_error.c")
        .file("runtime/moo_object.c")
        .file("runtime/moo_stdlib.c")
        .include("runtime")
        .opt_level(2)
        .flag("-fPIC")
        .compile("moo_runtime");

    println!("cargo:rerun-if-changed=runtime/");
}
```

- [ ] **Step 2: Cargo.toml — cc-Crate als Build-Dependency**

Hinzufügen:
```toml
[build-dependencies]
cc = "1"
```

- [ ] **Step 3: main.rs — Runtime-Archiv beim Linken verwenden**

Die Linker-Zeile in `compile()` ändern, sodass die `libmoo_runtime.a` mitgelinkt wird:

```rust
let runtime_lib = format!(
    "{}/build/{}/out/libmoo_runtime.a",
    env!("CARGO_MANIFEST_DIR"),
    // Wir brauchen den Target-Build-Ordner nicht exakt — die .a liegt im OUT_DIR
    "moo_runtime"
);
// Alternative: Runtime-Pfad aus dem Build-Output finden
```

Tatsächlich speichert `build.rs` die .a in Cargos Build-Verzeichnis. Für den Linker müssen wir die Pfade als Compile-Time-Konstante einbetten oder die .a-Dateien anders zugänglich machen. Pragmatischster Ansatz: Runtime als separate .a kompilieren und den Pfad per CLI-Option oder Environment-Variable übergeben.

- [ ] **Step 4: Commit**

---

### Task 9: LLVM Runtime-Bindings

**Files:**
- Create: `compiler/src/runtime_bindings.rs`

- [ ] **Step 1: Alle Runtime-Funktionen als LLVM-Deklarationen**

In dieser Datei deklarieren wir jede C-Runtime-Funktion als LLVM-Funktion, damit der Codegen sie aufrufen kann. Das `MooValue`-Struct wird als LLVM-StructType abgebildet.

```rust
// compiler/src/runtime_bindings.rs
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::types::{BasicMetadataTypeEnum, StructType};
use inkwell::values::FunctionValue;
use inkwell::AddressSpace;

pub struct RuntimeBindings<'ctx> {
    pub moo_value_type: StructType<'ctx>,
    // Konstruktoren
    pub moo_number: FunctionValue<'ctx>,
    pub moo_string_new: FunctionValue<'ctx>,
    pub moo_bool: FunctionValue<'ctx>,
    pub moo_none: FunctionValue<'ctx>,
    pub moo_list_new: FunctionValue<'ctx>,
    pub moo_list_from: FunctionValue<'ctx>,
    pub moo_dict_new: FunctionValue<'ctx>,
    pub moo_object_new: FunctionValue<'ctx>,
    // Ops
    pub moo_add: FunctionValue<'ctx>,
    pub moo_sub: FunctionValue<'ctx>,
    pub moo_mul: FunctionValue<'ctx>,
    pub moo_div: FunctionValue<'ctx>,
    pub moo_mod: FunctionValue<'ctx>,
    pub moo_pow: FunctionValue<'ctx>,
    pub moo_neg: FunctionValue<'ctx>,
    pub moo_eq: FunctionValue<'ctx>,
    pub moo_neq: FunctionValue<'ctx>,
    pub moo_lt: FunctionValue<'ctx>,
    pub moo_gt: FunctionValue<'ctx>,
    pub moo_lte: FunctionValue<'ctx>,
    pub moo_gte: FunctionValue<'ctx>,
    pub moo_and: FunctionValue<'ctx>,
    pub moo_or: FunctionValue<'ctx>,
    pub moo_not: FunctionValue<'ctx>,
    // String
    pub moo_string_concat: FunctionValue<'ctx>,
    pub moo_string_length: FunctionValue<'ctx>,
    pub moo_string_index: FunctionValue<'ctx>,
    // List
    pub moo_list_append: FunctionValue<'ctx>,
    pub moo_list_get: FunctionValue<'ctx>,
    pub moo_list_set: FunctionValue<'ctx>,
    pub moo_list_length: FunctionValue<'ctx>,
    pub moo_list_iter_len: FunctionValue<'ctx>,
    pub moo_list_iter_get: FunctionValue<'ctx>,
    // Dict
    pub moo_dict_get: FunctionValue<'ctx>,
    pub moo_dict_set: FunctionValue<'ctx>,
    pub moo_dict_has: FunctionValue<'ctx>,
    pub moo_dict_keys: FunctionValue<'ctx>,
    // Object
    pub moo_object_get: FunctionValue<'ctx>,
    pub moo_object_set: FunctionValue<'ctx>,
    pub moo_object_set_parent: FunctionValue<'ctx>,
    // Print
    pub moo_print: FunctionValue<'ctx>,
    pub moo_to_string: FunctionValue<'ctx>,
    // Error
    pub moo_throw: FunctionValue<'ctx>,
    pub moo_is_truthy: FunctionValue<'ctx>,
    // Stdlib
    pub moo_abs: FunctionValue<'ctx>,
    pub moo_sqrt: FunctionValue<'ctx>,
    pub moo_input: FunctionValue<'ctx>,
    pub moo_type_of: FunctionValue<'ctx>,
    pub moo_random: FunctionValue<'ctx>,
}

impl<'ctx> RuntimeBindings<'ctx> {
    pub fn declare(context: &'ctx Context, module: &Module<'ctx>) -> Self {
        let ptr_type = context.ptr_type(AddressSpace::default());
        let i32_type = context.i32_type();
        let i8_type = context.i8_type();
        let f64_type = context.f64_type();
        let void_type = context.void_type();
        let bool_type = context.bool_type();

        // MooValue = { i8, [8 x i8] } — Tag + 8 Bytes Union (passend für double/pointer)
        // Wir repräsentieren MooValue als { i8, double } da double der größte Union-Member ist
        let moo_value_type = context.struct_type(
            &[i8_type.into(), f64_type.into()],
            false,
        );
        let mv = BasicMetadataTypeEnum::from(moo_value_type);

        // Helper: Funktion deklarieren
        macro_rules! declare_fn {
            ($name:expr, $ret:expr, $params:expr) => {
                module.add_function($name, $ret.fn_type($params, false), None)
            };
        }

        // MooValue-returning functions (1 param)
        let mv1 = &[mv];
        let mv2 = &[mv, mv];

        Self {
            moo_value_type,
            // Konstruktoren
            moo_number: declare_fn!("moo_number", moo_value_type, &[f64_type.into()]),
            moo_string_new: declare_fn!("moo_string_new", moo_value_type, &[ptr_type.into()]),
            moo_bool: declare_fn!("moo_bool", moo_value_type, &[bool_type.into()]),
            moo_none: declare_fn!("moo_none", moo_value_type, &[]),
            moo_list_new: declare_fn!("moo_list_new", moo_value_type, &[i32_type.into()]),
            moo_list_from: declare_fn!("moo_list_from", moo_value_type, &[ptr_type.into(), i32_type.into()]),
            moo_dict_new: declare_fn!("moo_dict_new", moo_value_type, &[]),
            moo_object_new: declare_fn!("moo_object_new", moo_value_type, &[ptr_type.into()]),
            // Ops
            moo_add: declare_fn!("moo_add", moo_value_type, mv2),
            moo_sub: declare_fn!("moo_sub", moo_value_type, mv2),
            moo_mul: declare_fn!("moo_mul", moo_value_type, mv2),
            moo_div: declare_fn!("moo_div", moo_value_type, mv2),
            moo_mod: declare_fn!("moo_mod", moo_value_type, mv2),
            moo_pow: declare_fn!("moo_pow", moo_value_type, mv2),
            moo_neg: declare_fn!("moo_neg", moo_value_type, mv1),
            moo_eq: declare_fn!("moo_eq", moo_value_type, mv2),
            moo_neq: declare_fn!("moo_neq", moo_value_type, mv2),
            moo_lt: declare_fn!("moo_lt", moo_value_type, mv2),
            moo_gt: declare_fn!("moo_gt", moo_value_type, mv2),
            moo_lte: declare_fn!("moo_lte", moo_value_type, mv2),
            moo_gte: declare_fn!("moo_gte", moo_value_type, mv2),
            moo_and: declare_fn!("moo_and", moo_value_type, mv2),
            moo_or: declare_fn!("moo_or", moo_value_type, mv2),
            moo_not: declare_fn!("moo_not", moo_value_type, mv1),
            // String
            moo_string_concat: declare_fn!("moo_string_concat", moo_value_type, mv2),
            moo_string_length: declare_fn!("moo_string_length", moo_value_type, mv1),
            moo_string_index: declare_fn!("moo_string_index", moo_value_type, mv2),
            // List
            moo_list_append: declare_fn!("moo_list_append", void_type, mv2),
            moo_list_get: declare_fn!("moo_list_get", moo_value_type, mv2),
            moo_list_set: declare_fn!("moo_list_set", void_type, &[mv, mv, mv]),
            moo_list_length: declare_fn!("moo_list_length", moo_value_type, mv1),
            moo_list_iter_len: declare_fn!("moo_list_iter_len", i32_type, mv1),
            moo_list_iter_get: declare_fn!("moo_list_iter_get", moo_value_type, &[mv, i32_type.into()]),
            // Dict
            moo_dict_get: declare_fn!("moo_dict_get", moo_value_type, mv2),
            moo_dict_set: declare_fn!("moo_dict_set", void_type, &[mv, mv, mv]),
            moo_dict_has: declare_fn!("moo_dict_has", moo_value_type, mv2),
            moo_dict_keys: declare_fn!("moo_dict_keys", moo_value_type, mv1),
            // Object
            moo_object_get: declare_fn!("moo_object_get", moo_value_type, &[mv, ptr_type.into()]),
            moo_object_set: declare_fn!("moo_object_set", void_type, &[mv, ptr_type.into(), mv]),
            moo_object_set_parent: declare_fn!("moo_object_set_parent", void_type, mv2),
            // Print
            moo_print: declare_fn!("moo_print", void_type, mv1),
            moo_to_string: declare_fn!("moo_to_string", moo_value_type, mv1),
            // Error
            moo_throw: declare_fn!("moo_throw", void_type, mv1),
            moo_is_truthy: declare_fn!("moo_is_truthy", bool_type, mv1),
            // Stdlib
            moo_abs: declare_fn!("moo_abs", moo_value_type, mv1),
            moo_sqrt: declare_fn!("moo_sqrt", moo_value_type, mv1),
            moo_input: declare_fn!("moo_input", moo_value_type, mv1),
            moo_type_of: declare_fn!("moo_type_of", moo_value_type, mv1),
            moo_random: declare_fn!("moo_random", moo_value_type, &[]),
        }
    }
}
```

- [ ] **Step 2: Commit**

---

### Task 10: Codegen Refactor — MooValue statt f64

**Files:**
- Modify: `compiler/src/codegen.rs` (komplettes Rewrite)
- Modify: `compiler/src/main.rs`

Dies ist der größte Task. Der Codegen muss von `f64` auf `MooValue` umgestellt werden. Jede `compile_expr` gibt jetzt ein `MooValue`-Struct zurück, und alle Operationen gehen über Runtime-Calls.

**Schlüsselprinzip:** Der Codegen wird NICHT mehr direkt LLVM-Arithmetik machen, sondern für alles die Runtime aufrufen. Das ist etwas langsamer als direkte f64-Ops, aber:
- Funktioniert mit ALLEN Typen
- Ist erweiterbar (neuer Typ = nur Runtime erweitern)
- Kann später durch Typ-Inferenz wieder optimiert werden

- [ ] **Step 1: codegen.rs komplett mit Runtime-Calls neu schreiben**

Kernänderungen:
- `compile_expr` gibt `StructValue<'ctx>` (MooValue) zurück statt `BasicValueEnum`
- `compile_assignment` speichert `MooValue` statt `f64`
- `compile_show` ruft `moo_print()` auf
- Alle BinaryOps rufen `moo_add/sub/mul/...` auf
- String-Literale rufen `moo_string_new()` auf
- Listen-Literale erstellen über `moo_list_new()` + `moo_list_append()`
- Dict-Literale über `moo_dict_new()` + `moo_dict_set()`
- For-Schleifen über `moo_list_iter_len()` + `moo_list_iter_get()`
- Klassen über `moo_object_new()` + `moo_object_set()`
- Try/Catch über `setjmp` / `moo_throw()`

- [ ] **Step 2: main.rs Linker-Kommando anpassen für Runtime**

- [ ] **Step 3: Testen mit beispiel.moo**

- [ ] **Step 4: Commit**

---

### Task 11: Integration Tests

**Files:**
- Create: `compiler/tests/test_basic.moo`
- Create: `compiler/tests/test_strings.moo`
- Create: `compiler/tests/test_lists.moo`
- Create: `compiler/tests/test_dicts.moo`
- Create: `compiler/tests/test_oop.moo`
- Create: `compiler/tests/test_errors.moo`
- Create: `compiler/tests/run_tests.sh`

Jede Test-Datei kompiliert ein .moo-Programm, führt es aus und prüft den Output.

- [ ] **Step 1: Test-Dateien und Runner**
- [ ] **Step 2: Alle Tests grün**
- [ ] **Step 3: Commit**

---

## Erweiterungs-Punkte (kein Umbau nötig)

Wenn diese Architektur steht, können folgende Features **nur durch Erweiterung** hinzugefügt werden:

| Feature | Was zu tun ist |
|---------|---------------|
| Neuer Datentyp (z.B. Set) | Neuen Tag + `moo_set.c` + Runtime-Bindings + Codegen-Case |
| Neue Methode (z.B. `.sort()`) | Nur in `moo_list.c` + Codegen MethodCall-Dispatch |
| Neue Stdlib-Funktion | Nur in `moo_stdlib.c` + Runtime-Bindings + Codegen builtins-Map |
| Async/Await | Neuer Tag `MOO_FUTURE` + `moo_async.c` + Codegen |
| Garbage Collector | `moo_memory.c` ersetzen, Rest bleibt |
| Cross-Compilation | Nur `write_object()` Target-Triple ändern |
| Neues Codegen-Backend (WASM) | Neuer Generator neben `codegen.rs` |
