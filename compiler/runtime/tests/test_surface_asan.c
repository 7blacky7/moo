#include "../moo_runtime.h"
#include "../moo_surface_core.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition, message) do {                                      \
    if (!(condition)) {                                                     \
        fprintf(stderr, "FAIL:%d: %s\n", __LINE__, (message));             \
        failures++;                                                         \
    }                                                                       \
} while (0)

/* Minimal Moo runtime scaffolding. The harness links the real moo_memory.c so
 * MOO_SURFACE retain/release dispatch is exercised without desktop/toolkit
 * dependencies or the large full-runtime link set. */
MooValue moo_none(void) {
    MooValue value = {MOO_NONE, 0u};
    return value;
}

MooValue moo_bool(bool boolean) {
    MooValue value = {MOO_BOOL, boolean ? 1u : 0u};
    return value;
}

MooValue moo_number(double number) {
    MooValue value;
    value.tag = MOO_NUMBER;
    moo_val_set_double(&value, number);
    return value;
}

MooValue moo_string_new(const char* chars) {
    size_t length = chars ? strlen(chars) : 0u;
    MooString* string = (MooString*)calloc(1u, sizeof(MooString));
    MooValue value;
    if (!string || length > INT32_MAX) abort();
    string->chars = (char*)malloc(length + 1u);
    if (!string->chars) abort();
    memcpy(string->chars, chars ? chars : "", length + 1u);
    string->refcount = 1;
    string->length = (int32_t)length;
    string->capacity = (int32_t)length + 1;
    value.tag = MOO_STRING;
    moo_val_set_ptr(&value, string);
    return value;
}

MooValue moo_dict_new(void) {
    MooDict* dict = (MooDict*)calloc(1u, sizeof(MooDict));
    MooValue value;
    if (!dict) abort();
    dict->capacity = 8;
    dict->entries = (MooDictEntry*)calloc((size_t)dict->capacity,
                                          sizeof(MooDictEntry));
    if (!dict->entries) abort();
    dict->refcount = 1;
    value.tag = MOO_DICT;
    moo_val_set_ptr(&value, dict);
    return value;
}

void moo_dict_set(MooValue dict_value, MooValue key, MooValue value) {
    MooDict* dict = MV_DICT(dict_value);
    int32_t i;
    for (i = 0; i < dict->capacity; ++i) {
        if (!dict->entries[i].occupied) {
            dict->entries[i].occupied = true;
            dict->entries[i].key = MV_STR(key);
            dict->entries[i].value = value;
            dict->count++;
            return;
        }
    }
    abort();
}

MooValue moo_dict_get(MooValue dict_value, MooValue key) {
    /* Wie moo_dict.c: konsumiert den Key (Transfer), Owning-Rueckgabe. */
    MooDict* dict = MV_DICT(dict_value);
    MooValue result = moo_none();
    int32_t i;
    for (i = 0; i < dict->capacity; ++i) {
        if (dict->entries[i].occupied &&
            strcmp(dict->entries[i].key->chars, MV_STR(key)->chars) == 0) {
            result = dict->entries[i].value;
            moo_retain(result);
            break;
        }
    }
    moo_release(key);
    return result;
}

MooValue moo_frame_new_take(int width, int height, uint8_t* pixels) {
    MooFrame* frame;
    MooValue value;
    if (!pixels || width <= 0 || height <= 0 || width > INT_MAX / 4) {
        free(pixels);
        return moo_none();
    }
    frame = (MooFrame*)calloc(1u, sizeof(MooFrame));
    if (!frame) {
        free(pixels);
        return moo_none();
    }
    frame->refcount = 1;
    frame->width = width;
    frame->height = height;
    frame->format = MOO_FRAME_FMT_RGBA8;
    frame->stride = width * 4;
    frame->pixels = pixels;
    value.tag = MOO_FRAME;
    moo_val_set_ptr(&value, frame);
    return value;
}

void moo_frame_free(void* ptr) {
    MooFrame* frame = (MooFrame*)ptr;
    if (!frame) return;
    free(frame->pixels);
    free(frame);
}

MooValue moo_error(const char* message) {
    (void)message;
    return moo_none();
}
void moo_throw(MooValue error) { (void)error; }
void moo_socket_free(void* ptr) { (void)ptr; }
void moo_thread_free(void* ptr) { (void)ptr; }
void moo_channel_free(void* ptr) { (void)ptr; }
void moo_db_free(void* ptr) { (void)ptr; }
void moo_db_stmt_free(void* ptr) { (void)ptr; }
void moo_window_free(void* ptr) { (void)ptr; }
void moo_web_free(void* ptr) { (void)ptr; }
void moo_voxel_free(void* ptr) { (void)ptr; }
void moo_gif_handle_free(void* ptr) { (void)ptr; }
void moo_video_handle_free(void* ptr) { (void)ptr; }
void moo_tensor_free(void* ptr) { (void)ptr; }
void moo_kamera_free(void* ptr) { (void)ptr; }
void moo_mikro_free(void* ptr) { (void)ptr; }

static MooValue number(double value) {
    return moo_number(value);
}

static MooValue rgba_call_clear(MooValue surface, double r, double g,
                                double b, double a) {
    return moo_surface_clear(surface, number(r), number(g), number(b), number(a));
}

static bool dict_channel(MooValue value, const char* name, int expected) {
    MooDict* dict;
    int32_t i;
    if (value.tag != MOO_DICT) return false;
    dict = MV_DICT(value);
    for (i = 0; i < dict->capacity; ++i) {
        MooDictEntry* entry = &dict->entries[i];
        if (entry->occupied && strcmp(entry->key->chars, name) == 0) {
            return entry->value.tag == MOO_NUMBER &&
                   MV_NUM(entry->value) == (double)expected;
        }
    }
    return false;
}

static void test_core_contract(void) {
    uint8_t guarded[64u + 8u * 8u * 4u + 64u];
    uint8_t* pixels = guarded + 64u;
    MooSurfaceCore core;
    MooSurfaceColor transparent = {0u, 0u, 0u, 0u};
    MooSurfaceColor red50 = {100u, 50u, 25u, 128u};
    MooSurfaceColor green = {0u, 255u, 0u, 255u};
    MooSurfaceColor got;
    uint64_t before;
    int32_t width;
    uint32_t i;

    memset(guarded, 0xc3, sizeof(guarded));
    memset(pixels, 0, 8u * 8u * 4u);
    CHECK(!moo_surface_core_init(&core, pixels, 1u, 8, 8, 32u),
          "init rejects undersized buffer");
    CHECK(!moo_surface_core_init(&core, pixels, 8u * 8u * 4u, 8, 8, 31u),
          "init rejects short stride");
    CHECK(moo_surface_core_init(&core, pixels, 8u * 8u * 4u, 8, 8, 32u),
          "valid core init");

    CHECK(moo_surface_core_clear(&core, transparent), "clear");
    CHECK(moo_surface_core_rect(&core, 0, 0, 1, 1, red50), "alpha rect");
    CHECK(moo_surface_core_read_pixel(&core, 0, 0, &got), "read border pixel");
    CHECK(got.r == 100u && got.g == 50u && got.b == 25u && got.a == 128u,
          "straight alpha over transparent preserves source RGB");

    before = moo_surface_core_hash(&core);
    CHECK(!moo_surface_core_rect(&core, 0, 0, -1, 1, green),
          "negative rectangle rejected");
    CHECK(moo_surface_core_hash(&core) == before,
          "rejected rectangle is fail-closed");

    CHECK(moo_surface_core_clip_push(&core, 2, 2, 2, 2), "clip push");
    CHECK(moo_surface_core_rect(&core, -100, -100, 300, 300, green),
          "large rect clipped");
    CHECK(moo_surface_core_read_pixel(&core, 2, 2, &got) && got.g == 255u,
          "clip interior changed");
    CHECK(moo_surface_core_read_pixel(&core, 1, 1, &got) && got.g == 0u,
          "clip exterior untouched");
    CHECK(moo_surface_core_clip_pop(&core), "clip pop");
    CHECK(!moo_surface_core_clip_pop(&core), "root clip cannot pop");

    before = moo_surface_core_hash(&core);
    CHECK(moo_surface_core_clip_push(&core, INT32_MAX, 0, 1, 1),
          "fully-right INT32_MAX clip becomes valid empty clip");
    CHECK(moo_surface_core_valid(&core), "right empty clip keeps core valid");
    CHECK(moo_surface_core_rect(&core, 0, 0, 8, 8, green),
          "draw under right empty clip is a no-op");
    CHECK(moo_surface_core_hash(&core) == before,
          "right empty clip leaves pixels untouched");
    CHECK(moo_surface_core_clip_pop(&core), "right empty clip pops");

    CHECK(moo_surface_core_clip_push(&core, 0, INT32_MAX, 1, 1),
          "fully-below INT32_MAX clip becomes valid empty clip");
    CHECK(moo_surface_core_valid(&core), "below empty clip keeps core valid");
    CHECK(moo_surface_core_rect(&core, 0, 0, 8, 8, green),
          "draw under below empty clip is a no-op");
    CHECK(moo_surface_core_hash(&core) == before,
          "below empty clip leaves pixels untouched");
    CHECK(moo_surface_core_clip_pop(&core), "below empty clip pops");
    CHECK(moo_surface_core_rect(&core, 7, 7, 1, 1, green) &&
          moo_surface_core_read_pixel(&core, 7, 7, &got) && got.g == 255u,
          "draw remains functional after offscreen clips");

    for (i = 0u; i < MOO_SURFACE_CLIP_CAPACITY - 1u; ++i)
        CHECK(moo_surface_core_clip_push(&core, 0, 0, 8, 8),
              "clip stack fill");
    CHECK(!moo_surface_core_clip_push(&core, 0, 0, 8, 8),
          "clip overflow rejected");
    for (i = 0u; i < MOO_SURFACE_CLIP_CAPACITY - 1u; ++i)
        CHECK(moo_surface_core_clip_pop(&core), "clip stack unwind");

    CHECK(moo_surface_core_roundrect(&core, 0, 0, 8, 8, 3, red50),
          "roundrect borders");
    CHECK(!moo_surface_core_roundrect(&core, 0, 0, 8, 8, 5, red50),
          "oversized radius rejected");
    CHECK(moo_surface_core_circle(&core, INT32_MAX, 3, INT32_MAX, red50),
          "huge clipped circle terminates");
    CHECK(moo_surface_core_line(&core, INT32_MIN, INT32_MIN,
                                INT32_MAX, INT32_MAX, green),
          "huge clipped line terminates");

    CHECK(moo_surface_core_text_width_3x5("A\nBC", 4u, 2, &width) &&
          width == 14, "3x5 multiline width");
    CHECK(!moo_surface_core_text_width_3x5("A", 1u, INT32_MAX, &width),
          "3x5 width overflow rejected");
    CHECK(moo_surface_core_text_3x5(&core, 0, 0, 1, "A?", 2u, green),
          "3x5 text draw");

    for (i = 0u; i < 64u; ++i) {
        CHECK(guarded[i] == 0xc3, "core prefix canary");
        CHECK(guarded[64u + 8u * 8u * 4u + i] == 0xc3,
              "core suffix canary");
    }
}

typedef struct {
    int32_t refcount;
    uint32_t magic;
    uint8_t* allocation;
    size_t allocation_bytes;
    size_t pixel_bytes;
    MooSurfaceCore core;
} MooSurfaceHarnessLayout;

static void test_wrapper_contract(void) {
    MooValue surface = moo_surface_new(number(8), number(8));
    MooValue pixel;
    MooValue hash;
    MooValue frame;
    MooSurfaceHarnessLayout* layout;
    uint8_t saved_guard;
    uint8_t saved_pixel;
    int32_t i;

    CHECK(surface.tag == MOO_SURFACE, "surface constructor");
    CHECK(moo_surface_new(number(NAN), number(1)).tag == MOO_NONE,
          "NaN width rejected before cast");
    CHECK(moo_surface_new(number(INFINITY), number(1)).tag == MOO_NONE,
          "infinite width rejected before cast");
    CHECK(moo_surface_new(number(1.5), number(1)).tag == MOO_NONE,
          "fractional width rejected");
    CHECK(MV_BOOL(rgba_call_clear(surface, 1, 2, 3, 255)), "wrapper clear");

    pixel = moo_surface_read_pixel(surface, number(0), number(0));
    CHECK(dict_channel(pixel, "rot", 1) &&
          dict_channel(pixel, "gruen", 2) &&
          dict_channel(pixel, "blau", 3) &&
          dict_channel(pixel, "alpha", 255), "pixel dict channels");
    moo_release(pixel);

    hash = moo_surface_hash(surface);
    CHECK(hash.tag == MOO_STRING && MV_STR(hash)->length == 16,
          "hash is exact 16-char hex text");
    moo_release(hash);

    CHECK(!MV_BOOL(moo_surface_rect(surface, number(NAN), number(0),
                                     number(1), number(1), number(9),
                                     number(9), number(9), number(255))),
          "NaN geometry rejected");
    pixel = moo_surface_read_pixel(surface, number(0), number(0));
    CHECK(dict_channel(pixel, "rot", 1), "NaN path did not mutate");
    moo_release(pixel);

    for (i = 0; i < (int32_t)MOO_SURFACE_CLIP_CAPACITY - 1; ++i)
        CHECK(MV_BOOL(moo_surface_clip_push(surface, number(0), number(0),
                                            number(8), number(8))),
              "wrapper clip fill");
    CHECK(!MV_BOOL(moo_surface_clip_push(surface, number(0), number(0),
                                         number(8), number(8))),
          "wrapper clip overflow");
    for (i = 0; i < (int32_t)MOO_SURFACE_CLIP_CAPACITY - 1; ++i)
        CHECK(MV_BOOL(moo_surface_clip_pop(surface)), "wrapper clip unwind");
    CHECK(!MV_BOOL(moo_surface_clip_pop(surface)), "wrapper root clip");

    layout = (MooSurfaceHarnessLayout*)moo_val_as_ptr(surface);
    saved_guard = layout->allocation[0];
    saved_pixel = layout->core.pixels[0];
    layout->allocation[0] ^= 0xffu;
    CHECK(!MV_BOOL(moo_surface_rect(surface, number(0), number(0),
                                    number(8), number(8), number(255),
                                    number(0), number(0), number(255))),
          "corrupt canary rejects mutation");
    CHECK(layout->core.pixels[0] == saved_pixel,
          "canary failure is fail-closed");
    layout->allocation[0] = saved_guard;

    CHECK(MV_BOOL(rgba_call_clear(surface, 200, 10, 20, 255)),
          "prepare immutable snapshot");
    frame = moo_surface_snapshot_to_frame(surface);
    CHECK(frame.tag == MOO_FRAME, "snapshot returns frame");
    CHECK(MV_FRAME(frame)->pixels[0] == 200u, "snapshot copied pixels");
    CHECK(MV_BOOL(rgba_call_clear(surface, 1, 2, 3, 255)),
          "mutate after snapshot");
    CHECK(MV_FRAME(frame)->pixels[0] == 200u,
          "frame snapshot remains immutable");
    moo_release(frame);

    moo_retain(surface);
    CHECK(layout->refcount == 2, "surface retain");
    moo_release(surface);
    CHECK(layout->refcount == 1, "surface intermediate release");
    moo_release(surface);
}

int main(void) {
    test_core_contract();
    test_wrapper_contract();
    if (failures != 0) {
        fprintf(stderr, "test_surface_asan: %d failures\n", failures);
        return 1;
    }
    puts("test_surface_asan: PASS");
    return 0;
}
