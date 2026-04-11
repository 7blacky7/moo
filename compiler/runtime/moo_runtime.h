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
    MOO_THREAD  = 9,
    MOO_CHANNEL = 10,
    MOO_DATABASE = 11,
    MOO_WINDOW   = 12,
    MOO_WINDOW3D = 13,
    MOO_REGEX    = 14,
    MOO_SOCKET   = 15,
    MOO_WEBSERVER = 16,
} MooTag;

// === Forward Declarations ===
typedef struct MooString MooString;
typedef struct MooList MooList;
typedef struct MooDict MooDict;
typedef struct MooObject MooObject;
typedef struct MooFunc MooFunc;
typedef struct MooThread MooThread;
typedef struct MooChannel MooChannel;
typedef struct MooValue MooValue;

// === MooValue: Der universelle Wert ===
// Layout: { uint64_t tag, uint64_t data } = 16 Bytes
// data wird als uint64_t gespeichert und bei Bedarf zu double/pointer gecastet.
// Das garantiert ABI-Kompatibilitaet mit LLVM { i64, i64 }.
struct MooValue {
    uint64_t tag;
    uint64_t data;
};

// Zugriff-Makros
static inline double moo_val_as_double(MooValue v) {
    double d;
    memcpy(&d, &v.data, sizeof(double));
    return d;
}
static inline void moo_val_set_double(MooValue* v, double d) {
    memcpy(&v->data, &d, sizeof(double));
}
static inline void* moo_val_as_ptr(MooValue v) {
    return (void*)(uintptr_t)v.data;
}
static inline void moo_val_set_ptr(MooValue* v, void* p) {
    v->data = (uint64_t)(uintptr_t)p;
}
static inline bool moo_val_as_bool(MooValue v) { return (bool)v.data; }
static inline void moo_val_set_bool(MooValue* v, bool b) { v->data = (uint64_t)b; }

// Typ-spezifische Getter (Kurzformen)
#define MV_NUM(v)    moo_val_as_double(v)
#define MV_STR(v)    ((MooString*)moo_val_as_ptr(v))
#define MV_LIST(v)   ((MooList*)moo_val_as_ptr(v))
#define MV_DICT(v)   ((MooDict*)moo_val_as_ptr(v))
#define MV_OBJ(v)    ((MooObject*)moo_val_as_ptr(v))
#define MV_FUNC(v)   ((MooFunc*)moo_val_as_ptr(v))
#define MV_BOOL(v)   moo_val_as_bool(v)
#define MV_ERR(v)    ((char*)moo_val_as_ptr(v))

// === Reference Counting ===
// Erstes Feld in jedem Heap-Objekt. Startet bei 1 bei Erstellung.
// moo_retain() erhoeht, moo_release() verringert und gibt bei 0 frei.
void moo_retain(MooValue v);
void moo_release(MooValue v);

// === String ===
struct MooString {
    int32_t refcount;
    char* chars;
    int32_t length;
    int32_t capacity;
};

// === List ===
struct MooList {
    int32_t refcount;
    bool frozen;
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
    int32_t refcount;
    bool frozen;
    MooDictEntry* entries;
    int32_t count;
    int32_t capacity;
};

// === Function ===
struct MooFunc {
    int32_t refcount;
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
    int32_t refcount;
    bool frozen;
    char* class_name;
    MooProperty* properties;
    int32_t prop_count;
    int32_t prop_capacity;
    MooObject* parent;
};

// === Thread ===
#include <pthread.h>

struct MooThread {
    pthread_t thread;
    MooValue result;
    bool done;
    pthread_mutex_t mutex;
};

// === Channel (Go-Style, buffered) ===
struct MooChannel {
    MooValue* buffer;
    int32_t capacity;
    int32_t count;
    int32_t read_pos;
    int32_t write_pos;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    bool closed;
};

// === Thread-Funktionen ===
MooValue moo_thread_spawn(MooValue func, MooValue arg);
MooValue moo_thread_wait(MooValue thread);
MooValue moo_thread_done(MooValue thread);

// === Channel-Funktionen ===
MooValue moo_channel_new(MooValue capacity);
void moo_channel_send(MooValue channel, MooValue value);
MooValue moo_channel_recv(MooValue channel);
void moo_channel_close(MooValue channel);

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
MooValue moo_string_slice(MooValue s, MooValue start, MooValue end);

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

// === Event-System ===
void moo_event_on(MooValue obj, MooValue event_name, MooValue callback);
void moo_event_emit(MooValue obj, MooValue event_name);

// === Immutable/Freeze ===
MooValue moo_freeze(MooValue v);
MooValue moo_is_frozen(MooValue v);

// === Currying ===
MooValue moo_curry(MooValue func, MooValue arg);

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

// === Bitwise ===
MooValue moo_bitand(MooValue a, MooValue b);
MooValue moo_bitor(MooValue a, MooValue b);
MooValue moo_bitxor(MooValue a, MooValue b);
MooValue moo_bitnot(MooValue v);
MooValue moo_lshift(MooValue a, MooValue b);
MooValue moo_rshift(MooValue a, MooValue b);

// === Raw Memory (GEFAEHRLICH) ===
MooValue moo_mem_read(MooValue addr, MooValue size);
void moo_mem_write(MooValue addr, MooValue value, MooValue size);

// === Ausgabe ===
void moo_print(MooValue v);
MooValue moo_to_string(MooValue v);

// === Fehlerbehandlung ===
void moo_throw(MooValue error);
void moo_try_enter(void);       // enter try block
int moo_try_check(void);        // 1 = error occurred
void moo_try_leave(void);       // leave try/catch
MooValue moo_get_error(void);   // get the caught error
extern int moo_error_flag;

// === Speicher ===
void* moo_alloc(size_t size);
void* moo_realloc(void* ptr, size_t size);
void moo_free(void* ptr);

// === Debugger ===
void moo_breakpoint(MooValue line_num);

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
MooValue moo_length(MooValue v);
MooValue moo_range(MooValue start, MooValue end);
MooValue moo_time(void);
MooValue moo_syscall(MooValue nr, MooValue arg1, MooValue arg2, MooValue arg3);
// Kern-Builtins (moo_core.c)
void moo_sleep(MooValue duration);
MooValue moo_env(MooValue name);
void moo_exit(MooValue code);
MooValue moo_to_number(MooValue v);
void moo_args_init(int argc, char** argv);
MooValue moo_args(void);

// === Datei-I/O ===
MooValue moo_file_read(MooValue path);
MooValue moo_file_write(MooValue path, MooValue content);
MooValue moo_file_append(MooValue path, MooValue content);
MooValue moo_file_lines(MooValue path);
MooValue moo_file_exists(MooValue path);
MooValue moo_file_delete(MooValue path);
MooValue moo_dir_list(MooValue path);

// === Kryptografie & Sicherheit ===
MooValue moo_sha256(MooValue input);
MooValue moo_secure_random(MooValue length);
MooValue moo_base64_encode(MooValue input);
MooValue moo_base64_decode(MooValue input);
MooValue moo_sanitize_html(MooValue input);
MooValue moo_sanitize_sql(MooValue input);

// Universelle Index-Ops (dispatcht nach Container-Typ)
MooValue moo_string_repeat(MooValue s, MooValue count);
MooValue moo_index_get(MooValue container, MooValue index);
void moo_index_set(MooValue container, MooValue index, MooValue value);

// === JSON ===
MooValue moo_json_parse(MooValue json_string);
MooValue moo_json_string(MooValue value);

// === HTTP ===
MooValue moo_http_get(MooValue url);
MooValue moo_http_post(MooValue url, MooValue body);

// === Datenbank ===
MooValue moo_db_connect(MooValue url);
MooValue moo_db_execute(MooValue db, MooValue sql);
MooValue moo_db_query(MooValue db, MooValue sql);
void moo_db_close(MooValue db);

// === Grafik (SDL2) ===
MooValue moo_window_create(MooValue title, MooValue width, MooValue height);
MooValue moo_window_is_open(MooValue window);
void moo_window_clear(MooValue window, MooValue color);
void moo_window_update(MooValue window);
void moo_window_close(MooValue window);
void moo_draw_rect(MooValue win, MooValue x, MooValue y, MooValue w, MooValue h, MooValue color);
void moo_draw_circle(MooValue win, MooValue cx, MooValue cy, MooValue r, MooValue color);
void moo_draw_line(MooValue win, MooValue x1, MooValue y1, MooValue x2, MooValue y2, MooValue color);
void moo_draw_pixel(MooValue win, MooValue x, MooValue y, MooValue color);

// === Grafik Input (SDL2) ===
MooValue moo_key_pressed(MooValue key);
MooValue moo_mouse_x(MooValue window);
MooValue moo_mouse_y(MooValue window);
MooValue moo_mouse_pressed(MooValue window);
void moo_delay(MooValue ms);
void moo_pump_events(void);

// === 3D Grafik (OpenGL + GLFW) ===
MooValue moo_3d_create(MooValue title, MooValue w, MooValue h);
MooValue moo_3d_is_open(MooValue win);
void moo_3d_clear(MooValue win, MooValue r, MooValue g, MooValue b);
void moo_3d_update(MooValue win);
void moo_3d_close(MooValue win);
void moo_3d_triangle(MooValue win, MooValue x1, MooValue y1, MooValue z1,
                     MooValue x2, MooValue y2, MooValue z2,
                     MooValue x3, MooValue y3, MooValue z3, MooValue color);
void moo_3d_cube(MooValue win, MooValue x, MooValue y, MooValue z, MooValue size, MooValue color);
void moo_3d_sphere(MooValue win, MooValue x, MooValue y, MooValue z,
                   MooValue radius, MooValue color, MooValue detail);
void moo_3d_camera(MooValue win, MooValue eyeX, MooValue eyeY, MooValue eyeZ,
                   MooValue lookX, MooValue lookY, MooValue lookZ);
void moo_3d_perspective(MooValue win, MooValue fov, MooValue near_val, MooValue far_val);
void moo_3d_rotate(MooValue win, MooValue angle, MooValue ax, MooValue ay, MooValue az);
void moo_3d_translate(MooValue win, MooValue x, MooValue y, MooValue z);
void moo_3d_push(MooValue win);
void moo_3d_pop(MooValue win);

// === 3D Input ===
MooValue moo_3d_key_pressed(MooValue win, MooValue key);

// === Result-Typ ===
MooValue moo_result_ok(MooValue value);
MooValue moo_result_err(MooValue msg);
MooValue moo_result_is_ok(MooValue result);
MooValue moo_result_is_err(MooValue result);
MooValue moo_result_unwrap(MooValue result);

// === Profiler ===
void moo_profile_enter(MooValue name);
void moo_profile_exit(MooValue name);
void moo_profile_report(void);

// === Netzwerk (TCP/UDP) ===
MooValue moo_tcp_server(MooValue port);
MooValue moo_tcp_connect(MooValue host, MooValue port);
MooValue moo_udp_socket(MooValue port);
MooValue moo_socket_accept(MooValue server);
MooValue moo_socket_read(MooValue sock, MooValue max_bytes);
void moo_socket_write(MooValue sock, MooValue data);
void moo_socket_close(MooValue sock);

// === Eval ===
MooValue moo_eval(MooValue code);

// === Webserver ===
MooValue moo_web_server(MooValue port);
MooValue moo_web_accept(MooValue server);
MooValue moo_web_respond(MooValue request, MooValue body, MooValue status);
MooValue moo_web_json(MooValue request, MooValue data);
void moo_web_close(MooValue server);
MooValue moo_web_file(MooValue request, MooValue filepath);
MooValue moo_web_template(MooValue request, MooValue html, MooValue vars);

#endif // MOO_RUNTIME_H
