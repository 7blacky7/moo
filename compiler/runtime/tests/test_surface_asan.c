/* Resync 2026-07-27: Diese Datei war von der Ignore-Regel
 * compiler/runtime/tests/* erfasst und stand nicht in der Freigabeliste.
 * Erweiterungen landeten deshalb in der Datenbank, aber nie auf der Platte -
 * der Sanitizer lief still auf dem alten Harness und meldete PASS fuer
 * Tests, die es lokal gar nicht gab. Freigaberegel angelegt am 2026-07-27. */

#include "../moo_runtime.h"
#include "../moo_surface_core.h"

MooValue moo_surface_text_stil(MooValue value, MooValue x, MooValue y,
                               MooValue skala, MooValue text, MooValue stil,
                               MooValue r, MooValue g, MooValue b, MooValue a);
MooValue moo_surface_text_breite_stil(MooValue text, MooValue skala,
                                      MooValue stil);
MooValue moo_surface_text_glyphen_status(MooValue text, MooValue stil);

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

/* NATIVE-UI-1c: moo_surface_roundrect_stapel liest seine Stufenliste ueber
 * moo_list_length/moo_list_get — Stub-Semantik wie moo_list.c (Owning-
 * Rueckgabe mit retain; Index kommt hier immer als MOO_NUMBER). */
MooValue moo_list_length(MooValue list) {
    if (list.tag != MOO_LIST) return moo_number(0);
    return moo_number((double)MV_LIST(list)->length);
}

MooValue moo_list_get(MooValue list, MooValue index) {
    MooList* l;
    int32_t i;
    if (list.tag != MOO_LIST || index.tag != MOO_NUMBER) return moo_none();
    l = MV_LIST(list);
    i = (int32_t)MV_NUM(index);
    if (i < 0) i += l->length;
    if (i < 0 || i >= l->length) return moo_none();
    {
        MooValue v = l->items[i];
        moo_retain(v);
        return v;
    }
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

static bool dict_boolean(MooValue value, const char* name, bool expected) {
    MooDict* dict;
    int32_t i;
    if (value.tag != MOO_DICT) return false;
    dict = MV_DICT(value);
    for (i = 0; i < dict->capacity; ++i) {
        MooDictEntry* entry = &dict->entries[i];
        if (entry->occupied && strcmp(entry->key->chars, name) == 0) {
            return entry->value.tag == MOO_BOOL &&
                   MV_BOOL(entry->value) == expected;
        }
    }
    return false;
}

static void check_text_width(const char* bytes, int style, double expected,
                             const char* message) {
    MooValue text = moo_string_new(bytes);
    MooValue width1 = moo_surface_text_breite_stil(text, number(1), number(style));
    MooValue width2 = moo_surface_text_breite_stil(text, number(2), number(style));
    MooValue width3 = moo_surface_text_breite_stil(text, number(3), number(style));
    CHECK(width1.tag == MOO_NUMBER && MV_NUM(width1) == expected, message);
    CHECK(width2.tag == MOO_NUMBER && MV_NUM(width2) == expected * 2.0,
          "styled width scale 2");
    CHECK(width3.tag == MOO_NUMBER && MV_NUM(width3) == expected * 3.0,
          "styled width scale 3");
    if (style == 0) {
        MooValue legacy = moo_surface_text_breite(text, number(1));
        CHECK(legacy.tag == MOO_NUMBER && MV_NUM(legacy) == expected,
              "legacy width delegates to face 0");
    }
    moo_release(text);
}

static void check_text_hash(const char* bytes, int style, const char* expected,
                            const char* message) {
    MooValue text = moo_string_new(bytes);
    MooValue surface = moo_surface_new(number(240), number(40));
    MooValue hash;
    CHECK(surface.tag == MOO_SURFACE, "font hash surface");
    CHECK(MV_BOOL(rgba_call_clear(surface, 0, 0, 0, 255)),
          "font hash clear");
    CHECK(MV_BOOL(moo_surface_text_stil(
        surface, number(2), number(2), number(1), text, number(style),
        number(255), number(255), number(255), number(255))), message);
    hash = moo_surface_hash(surface);
    CHECK(hash.tag == MOO_STRING && strcmp(MV_STR(hash)->chars, expected) == 0,
          message);
    moo_release(hash);
    if (style == 0) {
        CHECK(MV_BOOL(rgba_call_clear(surface, 0, 0, 0, 255)),
              "legacy hash clear");
        CHECK(MV_BOOL(moo_surface_text(
            surface, number(2), number(2), number(1), text,
            number(255), number(255), number(255), number(255))),
            "legacy render hash path");
        hash = moo_surface_hash(surface);
        CHECK(hash.tag == MOO_STRING &&
              strcmp(MV_STR(hash)->chars, expected) == 0,
              "legacy wrapper preserves independent pixel baseline");
        moo_release(hash);
    }
    moo_release(surface);
    moo_release(text);
}

static void check_multiline_line_advance(int scale, const char* expected) {
    MooValue text = moo_string_new("A\nA");
    MooValue surface = moo_surface_new(number(100), number(140));
    MooValue hash;
    CHECK(MV_BOOL(rgba_call_clear(surface, 0, 0, 0, 255)),
          "multiline clear");
    CHECK(MV_BOOL(moo_surface_text_stil(
        surface, number(2), number(2), number(scale), text, number(0),
        number(255), number(255), number(255), number(255))),
        "styled multiline render");
    hash = moo_surface_hash(surface);
    CHECK(hash.tag == MOO_STRING && strcmp(MV_STR(hash)->chars, expected) == 0,
          "line advance is 20 * scale in styled renderer");
    moo_release(hash);
    CHECK(MV_BOOL(rgba_call_clear(surface, 0, 0, 0, 255)),
          "legacy multiline clear");
    CHECK(MV_BOOL(moo_surface_text(
        surface, number(2), number(2), number(scale), text,
        number(255), number(255), number(255), number(255))),
        "legacy multiline render");
    hash = moo_surface_hash(surface);
    CHECK(hash.tag == MOO_STRING && strcmp(MV_STR(hash)->chars, expected) == 0,
          "line advance is 20 * scale in legacy renderer");
    moo_release(hash);
    moo_release(surface);
    moo_release(text);
}

static void check_glyph_status(const char* bytes, int style, bool complete,
                               int invalid_utf8, int missing_glyphs,
                               const char* message) {
    MooValue text = moo_string_new(bytes);
    MooValue status = moo_surface_text_glyphen_status(text, number(style));
    CHECK(status.tag == MOO_DICT, message);
    CHECK(dict_boolean(status, "vollstaendig", complete), message);
    CHECK(dict_channel(status, "ungueltig_utf8", invalid_utf8), message);
    CHECK(dict_channel(status, "fehlende_glyphen", missing_glyphs), message);
    moo_release(status);
    moo_release(text);
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

static void test_font_contract(void) {
    MooValue surface = moo_surface_new(number(240), number(40));
    MooValue text = moo_string_new("A");
    MooValue invalid;
    int style;
    static const double face_mii_widths[5] = {22.0, 26.0, 22.0, 26.0, 30.0};
    static const double face_omega_widths[5] = {12.0, 14.0, 12.0, 14.0, 10.0};
    static const char* face_hashes[5] = {
        "6f4e992d5f370abe", "fb24d8ff5ea94694", "e62f0abd314646a4",
        "4831f32ea6912317", "6fd6dfafd6358605"
    };

    CHECK(surface.tag == MOO_SURFACE, "font contract surface");
    CHECK(!MV_BOOL(moo_surface_text_stil(
        surface, number(0), number(0), number(1), text, number(5),
        number(255), number(255), number(255), number(255))),
        "styled render rejects style 5");
    CHECK(!MV_BOOL(moo_surface_text_stil(
        surface, number(0), number(0), number(1), text, number(1.5),
        number(255), number(255), number(255), number(255))),
        "styled render rejects fractional style");
    CHECK(!MV_BOOL(moo_surface_text_stil(
        surface, number(0), number(0), number(1), text, number(NAN),
        number(255), number(255), number(255), number(255))),
        "styled render rejects NaN style");
    CHECK(!MV_BOOL(moo_surface_text_stil(
        surface, number(0), number(0), number(1), text, number(INFINITY),
        number(255), number(255), number(255), number(255))),
        "styled render rejects infinite style");
    invalid = moo_surface_text_breite_stil(text, number(1), number(5));
    CHECK(invalid.tag == MOO_NONE, "styled width rejects style 5 with none");
    invalid = moo_surface_text_breite_stil(text, number(0), number(0));
    CHECK(invalid.tag == MOO_NONE, "styled width rejects scale 0 with none");
    invalid = moo_surface_text_breite_stil(number(1), number(1), number(0));
    CHECK(invalid.tag == MOO_NONE, "styled width rejects non-text with none");
    invalid = moo_surface_text_glyphen_status(text, number(1.5));
    CHECK(invalid.tag == MOO_NONE, "glyph status rejects fractional style");
    invalid = moo_surface_text_glyphen_status(number(1), number(0));
    CHECK(invalid.tag == MOO_NONE, "glyph status rejects non-text");
    CHECK(MV_NUM(moo_surface_text_breite(text, number(0))) == 0.0,
          "legacy invalid width remains zero");

    check_text_width("Hallo Welt", 0, 81.0, "ASCII baseline width");
    check_text_width("\xE2\x80\x93", 0, 8.0, "old extra baseline width");
    check_text_hash("Hallo Welt", 0, "0d1e2da136a03977",
                    "ASCII baseline pixel hash");
    check_text_hash("\xE2\x80\x93", 0, "e2206fbfbe85c375",
                    "old extra baseline pixel hash");
    check_text_width("\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F",
                     0, 76.0, "valid two-byte umlaut and Latin1 widths");
    check_text_hash("\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F",
                    0, "8fb7603a89cfafeb",
                    "valid two-byte umlaut and Latin1 pixel hash");
    check_multiline_line_advance(1, "f7abc20ef1291e4d");
    check_multiline_line_advance(2, "44e5a952a62906ed");
    check_multiline_line_advance(3, "2ef42812cb3d42dd");

    check_text_width("\xC0\x80", 0, 16.0, "C0 80 maximal subparts");
    check_text_width("\xC0\xAF", 0, 16.0, "C0 AF maximal subparts");
    check_text_width("\xC1\x81", 0, 16.0, "C1 81 maximal subparts");
    check_text_width("\xC0\xA0", 0, 16.0, "C0 A0 maximal subparts");
    check_text_width("\xC1\xBF", 0, 16.0, "C1 BF maximal subparts");
    check_text_width("\xE0\x80\xAE", 0, 24.0, "E0 80 AE maximal subparts");
    check_text_width("\xE0\x80\xAF", 0, 24.0, "E0 80 AF maximal subparts");
    check_text_width("\xE0\x80\x81", 0, 24.0, "E0 80 81 maximal subparts");
    check_text_width("\xE0\x9F\xBF", 0, 24.0, "E0 9F BF maximal subparts");
    check_text_width("\xED\xA0\x80", 0, 24.0, "surrogate maximal subparts");
    check_text_hash("\xC0\x80", 0, "5929295c8a07339d",
                    "two invalid bytes render two fallbacks");
    check_text_hash("\xE0\x80\xAF", 0, "3c25445719b9e505",
                    "three invalid bytes render three fallbacks");

    check_text_width("\xE2\x82", 0, 8.0, "truncated 3-byte maximal prefix");
    check_text_width("\xF0\x9F", 0, 8.0, "truncated 4-byte prefix two");
    check_text_width("\xF0\x9F\x98", 0, 8.0, "truncated 4-byte prefix three");
    check_text_width("\xF0\x9F" "AB", 0, 30.0, "bad fourth-byte continuation");
    check_text_width("\xF0\x9F\x98" "A", 0, 19.0, "truncated 4-byte then ASCII");
    check_text_width("\xF0\x80\x80\x80", 0, 32.0, "overlong 4-byte split");
    check_text_width("\xF5\x80\x80\x80", 0, 32.0, "above Unicode split");
    check_text_width("\xF0\x9F\x98\x80", 0, 8.0, "valid missing emoji");
    check_text_width("\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 0, 16.0,
                     "two valid missing emoji");
    check_text_width("X\xF0\x9F\x98\x80Y", 0, 29.0,
                     "valid missing emoji preserves suffix");
    check_text_width("\xC3" "A", 0, 19.0, "bad 2-byte continuation");
    check_text_width("\xE2" "AB", 0, 30.0, "bad 3-byte continuation");
    check_text_width("\xC3\xC3" "A", 0, 27.0, "adjacent bad leads");
    check_text_width("\x80", 0, 8.0, "lone continuation");
    check_text_width("\x80\x81", 0, 16.0, "two lone continuations");
    check_text_width("\xFE\xFF", 0, 16.0, "forbidden lead bytes");
    check_text_width("\xC3", 0, 8.0, "truncated 2-byte lead");
    check_text_width("\xF0", 0, 8.0, "truncated 4-byte lead");

    check_text_width("\xCE\xA9", 0, 12.0, "Omega advance");
    check_text_width("\xE2\x80\xA2", 0, 9.0, "bullet advance");
    check_text_width("\xE2\x86\x92", 0, 13.0, "right arrow advance");
    check_text_width("\xE2\x9C\x93", 0, 13.0, "check advance");
    check_text_width("\xF0\x9D\x95\x9A", 0, 6.0, "four-byte embedded glyph");
    check_text_hash("\xCE\xA9", 0, "20fe5e8a3923dfaf", "Omega pixel hash");
    check_text_hash("\xE2\x80\xA2", 0, "af152603dcf5ee4e", "bullet pixel hash");
    check_text_hash("\xE2\x86\x92", 0, "984dc4a964716284", "arrow pixel hash");
    check_text_hash("\xE2\x9C\x93", 0, "b23b5d83fc8a5345", "check pixel hash");
    check_text_hash("\xF0\x9D\x95\x9A", 0, "0ec4bd5db4b9821d",
                    "four-byte embedded pixel hash");

    for (style = 0; style < 5; ++style) {
        check_text_width("Mii", style, face_mii_widths[style],
                         "face-specific Mii width");
        check_text_width("\xCE\xA9", style, face_omega_widths[style],
                         "face-specific Omega width");
        check_text_hash("Mii", style, face_hashes[style],
                        "face-specific Mii pixel hash");
    }

    check_glyph_status("Hallo \xCE\xA9", 0, true, 0, 0,
                       "complete glyph status");
    check_glyph_status("\xC0\x80", 0, false, 2, 0,
                       "invalid UTF-8 status counts maximal subparts");
    check_glyph_status("\xED\xA0\x80", 0, false, 3, 0,
                       "surrogate status counts bytes");
    check_glyph_status("\xF0\x9F\x98\x80", 0, false, 0, 1,
                       "valid missing glyph status");
    check_glyph_status("\xF0\x9D\x95\x9A", 4, true, 0, 0,
                       "four-byte embedded glyph complete in mono");

    moo_release(text);
    moo_release(surface);
}

int main(void) {
    test_core_contract();
    test_font_contract();
    test_wrapper_contract();
    if (failures != 0) {
        fprintf(stderr, "test_surface_asan: %d failures\n", failures);
        return 1;
    }
    puts("test_surface_asan: PASS");
    return 0;
}
