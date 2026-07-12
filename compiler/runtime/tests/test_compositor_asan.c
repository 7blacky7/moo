/*
 * P016-O3: Headless deterministic compositor gates.
 *
 * Link (no UI/toolkit):
 *   cc -std=c11 -Wall -Wextra -Werror -Icompiler/runtime \
 *      compiler/runtime/tests/test_compositor_asan.c \
 *      compiler/runtime/moo_compositor_core.c \
 *      compiler/runtime/moo_compositor_raster.c \
 *      -o /tmp/test_compositor
 *
 * The same link set is intended for ASan/LSan and UBSan.
 */
#include "../moo_compositor_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CLIENT_CAP 8u
#define SURFACE_CAP 16u
#define BUFFER_CAP 16u
#define FRAME_CAP 32u
#define EVENT_CAP 64u
#define GUARD_SIZE 64u
#define OUTPUT_MAX_BYTES (16u * 16u * 4u)
#define DIFFERENTIAL_SEEDS 3u
#define DIFFERENTIAL_OPS_PER_SEED 4096u

static int g_checks;
static int g_failures;

#define CHECK(cond, msg) do {                                                \
    g_checks++;                                                              \
    if (!(cond)) {                                                           \
        g_failures++;                                                        \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));      \
    }                                                                        \
} while (0)

typedef struct {
    uint8_t tables_prefix[GUARD_SIZE];
    MooCompClientSlot clients[CLIENT_CAP];
    MooCompSurfaceSlot surfaces[SURFACE_CAP];
    MooCompBufferSlot buffers[BUFFER_CAP];
    MooCompFrameSlot frames[FRAME_CAP];
    MooCompEventSlot events[EVENT_CAP];
    uint8_t tables_suffix[GUARD_SIZE];

    MooCompositor core;
    uint8_t output_storage[GUARD_SIZE + OUTPUT_MAX_BYTES + GUARD_SIZE];
    size_t output_bytes;
    MooCompOutput output;
} Fixture;

typedef struct {
    MooCompositor core;
    MooCompClientSlot clients[CLIENT_CAP];
    MooCompSurfaceSlot surfaces[SURFACE_CAP];
    MooCompBufferSlot buffers[BUFFER_CAP];
    MooCompFrameSlot frames[FRAME_CAP];
    MooCompEventSlot events[EVENT_CAP];
    uint8_t output[OUTPUT_MAX_BYTES];
    size_t output_bytes;
    uint64_t state_hash;
} StateSnapshot;

typedef struct {
    MooCompRect rects[MOO_COMP_OUTPUT_DAMAGE_CAPACITY];
    uint32_t count;
} DamageSnapshot;

static void fill_guard(uint8_t *bytes, size_t count) {
    memset(bytes, 0xa5, count);
}

static int guard_is(const uint8_t *bytes, size_t count) {
    size_t i;
    for (i = 0u; i < count; ++i)
        if (bytes[i] != 0xa5u) return 0;
    return 1;
}

static void fixture_init_caps(Fixture *f, int32_t width, int32_t height,
                              uint32_t client_capacity,
                              uint32_t surface_capacity,
                              uint32_t buffer_capacity,
                              uint32_t frame_capacity,
                              uint32_t event_capacity) {
    MooCompConfig config;
    MooCompResult result;
    memset(f, 0, sizeof(*f));
    fill_guard(f->tables_prefix, GUARD_SIZE);
    fill_guard(f->tables_suffix, GUARD_SIZE);
    f->output_bytes = (size_t)width * (size_t)height * 4u;
    CHECK(width > 0 && height > 0 && f->output_bytes <= OUTPUT_MAX_BYTES,
          "fixture dimensions");
    memset(f->output_storage, 0xcc, sizeof(f->output_storage));
    fill_guard(f->output_storage, GUARD_SIZE);
    fill_guard(f->output_storage + GUARD_SIZE + f->output_bytes, GUARD_SIZE);
    f->output.pixels = f->output_storage + GUARD_SIZE;
    f->output.buffer_bytes = f->output_bytes;
    f->output.stride = (size_t)width * 4u;
    f->output.width = width;
    f->output.height = height;

    config.output_width = width;
    config.output_height = height;
    config.background_r = 0u;
    config.background_g = 0u;
    config.background_b = 0u;
    config.background_a = 255u;
    result = moo_comp_init(&f->core, &config,
                           f->clients, client_capacity,
                           f->surfaces, surface_capacity,
                           f->buffers, buffer_capacity,
                           f->frames, frame_capacity,
                           f->events, event_capacity);
    CHECK(result == MOO_COMP_OK, "compositor init");
}

static void fixture_init(Fixture *f, int32_t width, int32_t height) {
    fixture_init_caps(f, width, height, CLIENT_CAP, SURFACE_CAP, BUFFER_CAP,
                      FRAME_CAP, EVENT_CAP);
}

static void state_snapshot_take(const Fixture *f, StateSnapshot *snapshot) {
    snapshot->core = f->core;
    memcpy(snapshot->clients, f->clients, sizeof(snapshot->clients));
    memcpy(snapshot->surfaces, f->surfaces, sizeof(snapshot->surfaces));
    memcpy(snapshot->buffers, f->buffers, sizeof(snapshot->buffers));
    memcpy(snapshot->frames, f->frames, sizeof(snapshot->frames));
    memcpy(snapshot->events, f->events, sizeof(snapshot->events));
    snapshot->output_bytes = f->output_bytes;
    memcpy(snapshot->output, f->output.pixels, f->output_bytes);
    snapshot->state_hash = moo_comp_state_hash(&f->core);
}

static int state_snapshot_equal(const Fixture *f,
                                const StateSnapshot *snapshot) {
    return snapshot->output_bytes == f->output_bytes &&
           snapshot->state_hash == moo_comp_state_hash(&f->core) &&
           memcmp(&snapshot->core, &f->core, sizeof(f->core)) == 0 &&
           memcmp(snapshot->clients, f->clients,
                  sizeof(snapshot->clients)) == 0 &&
           memcmp(snapshot->surfaces, f->surfaces,
                  sizeof(snapshot->surfaces)) == 0 &&
           memcmp(snapshot->buffers, f->buffers,
                  sizeof(snapshot->buffers)) == 0 &&
           memcmp(snapshot->frames, f->frames,
                  sizeof(snapshot->frames)) == 0 &&
           memcmp(snapshot->events, f->events,
                  sizeof(snapshot->events)) == 0 &&
           memcmp(snapshot->output, f->output.pixels, f->output_bytes) == 0;
}

static void fixture_guards(const Fixture *f) {
    CHECK(guard_is(f->tables_prefix, GUARD_SIZE), "tables prefix canary");
    CHECK(guard_is(f->tables_suffix, GUARD_SIZE), "tables suffix canary");
    CHECK(guard_is(f->output_storage, GUARD_SIZE), "output prefix canary");
    CHECK(guard_is(f->output_storage + GUARD_SIZE + f->output_bytes, GUARD_SIZE),
          "output suffix canary");
}

static MooCompBufferView rgba_view(const uint8_t *pixels,
                                   int32_t width, int32_t height) {
    MooCompBufferView view;
    view.pixels = pixels;
    view.buffer_bytes = (size_t)width * (size_t)height * 4u;
    view.stride = (size_t)width * 4u;
    view.width = width;
    view.height = height;
    view.format = MOO_COMP_FORMAT_RGBA8888;
    return view;
}

static void fill_solid(uint8_t *pixels, int32_t width, int32_t height,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int32_t x;
    int32_t y;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            size_t p = ((size_t)y * (size_t)width + (size_t)x) * 4u;
            pixels[p + 0u] = r;
            pixels[p + 1u] = g;
            pixels[p + 2u] = b;
            pixels[p + 3u] = a;
        }
    }
}

static uint64_t output_hash(const MooCompOutput *output) {
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t dimensions[2];
    size_t i;
    int32_t y;
    dimensions[0] = (uint32_t)output->width;
    dimensions[1] = (uint32_t)output->height;
    for (i = 0u; i < 2u; ++i) {
        uint32_t value = dimensions[i];
        uint32_t byte_index;
        for (byte_index = 0u; byte_index < 4u; ++byte_index) {
            hash ^= (uint8_t)(value >> (byte_index * 8u));
            hash *= UINT64_C(1099511628211);
        }
    }
    for (y = 0; y < output->height; ++y) {
        size_t row_bytes = (size_t)output->width * 4u;
        const uint8_t *row = output->pixels + (size_t)y * output->stride;
        for (i = 0u; i < row_bytes; ++i) {
            hash ^= row[i];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static int pixel_is(const MooCompOutput *output, int32_t x, int32_t y,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t *p;
    if (!output || x < 0 || y < 0 ||
        x >= output->width || y >= output->height) return 0;
    p = output->pixels + (size_t)y * output->stride + (size_t)x * 4u;
    return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

static MooCompResult create_client_surface_buffer(
    Fixture *f, const MooCompBufferView *view,
    MooCompHandle *client, MooCompHandle *surface, MooCompHandle *buffer) {
    MooCompResult result;
    result = moo_comp_client_create(&f->core, client);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_buffer_register(&f->core, *client, view, buffer);
    if (result != MOO_COMP_OK) return result;
    return moo_comp_surface_create(&f->core, *client, surface);
}

static MooCompResult commit_buffer(Fixture *f, MooCompHandle client,
                                   MooCompHandle surface, MooCompHandle buffer,
                                   uint32_t scale, uint32_t opacity,
                                   int32_t buffer_width, int32_t buffer_height,
                                   uint64_t frame_token) {
    MooCompRect damage = {0, 0, buffer_width, buffer_height};
    MooCompResult result;
    result = moo_comp_surface_attach(&f->core, client, surface, buffer);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_surface_set_scale(&f->core, client, surface, scale);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_surface_set_opacity(&f->core, client, surface, opacity);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_surface_damage(&f->core, client, surface, damage);
    if (result != MOO_COMP_OK) return result;
    if (frame_token != 0u) {
        result = moo_comp_surface_frame(&f->core, client, surface, frame_token);
        if (result != MOO_COMP_OK) return result;
    }
    return moo_comp_surface_commit(&f->core, client, surface);
}

static uint32_t handle_slot(MooCompHandle handle) {
    uint32_t encoded = (uint32_t)handle & UINT32_C(0x0fffffff);
    return encoded == 0u ? UINT32_MAX : encoded - 1u;
}

static int surface_before(const MooCompSurfaceSlot *a, uint32_t ai,
                          const MooCompSurfaceSlot *b, uint32_t bi) {
    if (a->z_sequence != b->z_sequence)
        return a->z_sequence < b->z_sequence;
    return ai < bi;
}

static void reference_blend(uint8_t *dst, const uint8_t *src,
                            uint32_t opacity) {
    uint64_t sa = ((uint64_t)src[3] * opacity + 127u) / 255u;
    uint64_t inv;
    uint64_t out_a_n;
    uint64_t channel_n;
    uint32_t c;
    if (sa == 0u) return;
    if (sa == 255u) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255u;
        return;
    }
    inv = 255u - sa;
    out_a_n = sa * 255u + (uint64_t)dst[3] * inv;
    for (c = 0u; c < 3u; ++c) {
        channel_n = (uint64_t)src[c] * sa * 255u +
                    (uint64_t)dst[c] * (uint64_t)dst[3] * inv;
        dst[c] = (uint8_t)((channel_n + out_a_n / 2u) / out_a_n);
    }
    dst[3] = (uint8_t)((out_a_n + 127u) / 255u);
}

static void reference_blit(uint8_t *pixels, int32_t output_width,
                           int32_t output_height,
                           const MooCompBufferView *view,
                           int32_t dst_x, int32_t dst_y,
                           uint32_t scale, uint32_t opacity) {
    int32_t logical_width = view->width / (int32_t)scale;
    int32_t logical_height = view->height / (int32_t)scale;
    int32_t y;
    for (y = 0; y < logical_height; ++y) {
        int64_t oy = (int64_t)dst_y + y;
        int32_t x;
        if (oy < 0 || oy >= output_height) continue;
        for (x = 0; x < logical_width; ++x) {
            int64_t ox = (int64_t)dst_x + x;
            size_t src_offset;
            size_t dst_offset;
            if (ox < 0 || ox >= output_width) continue;
            src_offset = (size_t)(y * (int32_t)scale) * view->stride +
                         (size_t)(x * (int32_t)scale) * 4u;
            dst_offset = ((size_t)oy * (size_t)output_width +
                          (size_t)ox) * 4u;
            reference_blend(pixels + dst_offset,
                            view->pixels + src_offset, opacity);
        }
    }
}

static void full_reference(const Fixture *f, uint8_t *pixels) {
    uint8_t used[SURFACE_CAP];
    uint32_t emitted;
    size_t pixel_count = (size_t)f->output.width *
                         (size_t)f->output.height;
    size_t p;
    for (p = 0u; p < pixel_count; ++p) {
        pixels[p * 4u + 0u] = f->core.config.background_r;
        pixels[p * 4u + 1u] = f->core.config.background_g;
        pixels[p * 4u + 2u] = f->core.config.background_b;
        pixels[p * 4u + 3u] = f->core.config.background_a;
    }
    memset(used, 0, sizeof(used));
    for (emitted = 0u; emitted < f->core.surface_capacity; ++emitted) {
        uint32_t i;
        uint32_t best = UINT32_MAX;
        for (i = 0u; i < f->core.surface_capacity; ++i) {
            const MooCompSurfaceSlot *slot = &f->surfaces[i];
            if (used[i] || !slot->live ||
                slot->committed.buffer == MOO_COMP_HANDLE_INVALID) continue;
            if (best == UINT32_MAX ||
                surface_before(slot, i, &f->surfaces[best], best))
                best = i;
        }
        if (best == UINT32_MAX) break;
        used[best] = 1u;
        {
            const MooCompSurfaceSlot *surface = &f->surfaces[best];
            uint32_t buffer_index = handle_slot(surface->committed.buffer);
            CHECK(buffer_index < BUFFER_CAP && f->buffers[buffer_index].live,
                  "reference buffer lookup");
            if (buffer_index < BUFFER_CAP && f->buffers[buffer_index].live) {
                reference_blit(pixels, f->output.width, f->output.height,
                               &f->buffers[buffer_index].view,
                               surface->x, surface->y,
                               surface->committed.scale,
                               surface->committed.opacity);
            }
        }
    }
    if (f->core.cursor.visible) {
        uint32_t buffer_index = handle_slot(f->core.cursor.buffer);
        CHECK(buffer_index < f->core.buffer_capacity &&
              f->buffers[buffer_index].live, "reference cursor buffer");
        if (buffer_index < f->core.buffer_capacity &&
            f->buffers[buffer_index].live) {
            reference_blit(pixels, f->output.width, f->output.height,
                           &f->buffers[buffer_index].view,
                           f->core.cursor.x - f->core.cursor.hotspot_x,
                           f->core.cursor.y - f->core.cursor.hotspot_y,
                           f->core.cursor.scale, 255u);
        }
    }
}

static void check_partial_equals_full(const Fixture *f, const char *message) {
    uint8_t reference[OUTPUT_MAX_BYTES];
    memset(reference, 0, sizeof(reference));
    full_reference(f, reference);
    CHECK(memcmp(reference, f->output.pixels, f->output_bytes) == 0, message);
}

static int damage_covers(const MooCompositor *core, int32_t x, int32_t y) {
    uint32_t i;
    for (i = 0u; i < moo_comp_damage_count(core); ++i) {
        MooCompRect rect;
        if (moo_comp_damage_get(core, i, &rect) == MOO_COMP_OK &&
            x >= rect.x && y >= rect.y &&
            (int64_t)x < (int64_t)rect.x + rect.width &&
            (int64_t)y < (int64_t)rect.y + rect.height)
            return 1;
    }
    return 0;
}

static void damage_snapshot_take(const MooCompositor *core,
                                 DamageSnapshot *snapshot) {
    uint32_t i;
    snapshot->count = moo_comp_damage_count(core);
    CHECK(snapshot->count <= MOO_COMP_OUTPUT_DAMAGE_CAPACITY,
          "damage count bounded");
    for (i = 0u; i < snapshot->count; ++i) {
        CHECK(moo_comp_damage_get(core, i, &snapshot->rects[i]) == MOO_COMP_OK,
              "damage snapshot get");
    }
}

static int damage_snapshot_covers(const DamageSnapshot *snapshot,
                                  int32_t x, int32_t y) {
    uint32_t i;
    for (i = 0u; i < snapshot->count; ++i) {
        const MooCompRect *rect = &snapshot->rects[i];
        if (x >= rect->x && y >= rect->y &&
            (int64_t)x < (int64_t)rect->x + rect->width &&
            (int64_t)y < (int64_t)rect->y + rect->height)
            return 1;
    }
    return 0;
}

static void build_present(Fixture *f, uint64_t present_sequence) {
    uint64_t frame_id = 0u;
    CHECK(moo_comp_build_frame(&f->core, &f->output, &frame_id) == MOO_COMP_OK,
          "build frame");
    CHECK(frame_id != 0u, "nonzero frame id");
    CHECK(moo_comp_present_done(&f->core, frame_id, present_sequence,
                                present_sequence * 1000u) == MOO_COMP_OK,
          "present done");
}

static void expect_frame_event(Fixture *f, MooCompHandle client,
                               uint64_t token, uint32_t status,
                               uint64_t present_sequence) {
    MooCompEvent event;
    memset(&event, 0, sizeof(event));
    CHECK(moo_comp_next_event(&f->core, client, &event) == MOO_COMP_OK,
          "frame event available");
    CHECK(event.type == MOO_COMP_EVENT_FRAME_DONE, "frame event type");
    CHECK(event.token == token, "frame token");
    CHECK(event.status == status, "frame status");
    CHECK(event.present_sequence == present_sequence, "present sequence");
    CHECK(moo_comp_next_event(&f->core, client, &event) ==
              MOO_COMP_WOULD_BLOCK,
          "frame event exactly once");
}

static void pop_frame_event(Fixture *f, MooCompHandle client,
                            uint64_t token, uint32_t status,
                            uint64_t present_sequence) {
    MooCompEvent event;
    memset(&event, 0, sizeof(event));
    CHECK(moo_comp_next_event(&f->core, client, &event) == MOO_COMP_OK,
          "pop frame event");
    CHECK(event.type == MOO_COMP_EVENT_FRAME_DONE,
          "popped frame event type");
    CHECK(event.token == token, "popped frame token");
    if (event.status != status)
        fprintf(stderr, "frame token %llu status=%u expected=%u\n",
                (unsigned long long)event.token, event.status, status);
    CHECK(event.status == status, "popped frame status");
    CHECK(event.present_sequence == present_sequence,
          "popped present sequence");
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    *state = x;
    return x;
}

static uint32_t drain_events(Fixture *f, MooCompHandle client) {
    MooCompEvent event;
    uint32_t count = 0u;
    while (moo_comp_next_event(&f->core, client, &event) == MOO_COMP_OK)
        count++;
    return count;
}

static void verify_partial_present(Fixture *f, uint64_t sequence) {
    DamageSnapshot damage;
    uint8_t before[OUTPUT_MAX_BYTES];
    uint8_t reference[OUTPUT_MAX_BYTES];
    uint64_t frame_id = 0u;
    int outside_unchanged = 1;
    int full_diff_covered = 1;
    int32_t x;
    int32_t y;
    memcpy(before, f->output.pixels, f->output_bytes);
    memset(reference, 0, sizeof(reference));
    full_reference(f, reference);
    damage_snapshot_take(&f->core, &damage);
    CHECK(moo_comp_build_frame(&f->core, &f->output, &frame_id) == MOO_COMP_OK,
          "differential build");
    for (y = 0; y < f->output.height; ++y) {
        for (x = 0; x < f->output.width; ++x) {
            size_t offset = ((size_t)y * (size_t)f->output.width +
                             (size_t)x) * 4u;
            int covered = damage_snapshot_covers(&damage, x, y);
            if (!covered &&
                memcmp(before + offset, f->output.pixels + offset, 4u) != 0)
                outside_unchanged = 0;
            if (memcmp(before + offset, reference + offset, 4u) != 0 &&
                !covered)
                full_diff_covered = 0;
        }
    }
    CHECK(outside_unchanged, "bytes outside damage unchanged");
    CHECK(full_diff_covered, "full recomposition diff covered by damage");
    CHECK(memcmp(reference, f->output.pixels, f->output_bytes) == 0,
          "partial output equals independent full reference");
    CHECK(moo_comp_present_done(&f->core, frame_id, sequence,
                                sequence * 1000u) == MOO_COMP_OK,
          "differential present");
}

static void test_independent_alpha_matrix(void) {
    static const uint8_t edge[] = {0u, 1u, 127u, 128u, 254u, 255u};
    uint32_t si;
    uint32_t di;
    uint32_t oi;
    for (si = 0u; si < sizeof(edge); ++si) {
        for (di = 0u; di < sizeof(edge); ++di) {
            for (oi = 0u; oi < sizeof(edge); ++oi) {
                uint8_t src[4u] = {17u, 129u, 241u, edge[si]};
                uint8_t actual[4u] = {233u, 71u, 5u, edge[di]};
                uint8_t expected[4u];
                MooCompBufferView view = rgba_view(src, 1, 1);
                MooCompOutput output;
                MooCompRect one = {0, 0, 1, 1};
                memcpy(expected, actual, sizeof(expected));
                reference_blend(expected, src, edge[oi]);
                output.pixels = actual;
                output.buffer_bytes = sizeof(actual);
                output.stride = 4u;
                output.width = 1;
                output.height = 1;
                CHECK(moo_comp_raster_blit(&output, &view, 0, 0, 1u,
                                           edge[oi], one) == MOO_COMP_OK,
                      "alpha matrix raster accepted");
                CHECK(memcmp(actual, expected, sizeof(actual)) == 0,
                      "alpha matrix independent reference");
            }
        }
    }
}

static void test_seeded_differential_damage(void) {
    static const uint32_t seeds[DIFFERENTIAL_SEEDS] = {
        UINT32_C(1), UINT32_C(0x00c0ffee), UINT32_C(0xffffffff)
    };
    static const uint8_t opacity_values[] = {0u, 1u, 127u, 128u, 254u, 255u};
    static const uint32_t scales[] = {1u, 2u, 4u};
    uint32_t seed_index;
    for (seed_index = 0u; seed_index < DIFFERENTIAL_SEEDS; ++seed_index) {
        Fixture f;
        uint8_t pixels[3u][2u][4u * 4u * 4u];
        MooCompHandle clients[3u];
        MooCompHandle surfaces[3u];
        MooCompHandle buffers[3u][2u];
        uint32_t active_buffer[3u] = {0u, 0u, 0u};
        uint32_t rng = seeds[seed_index];
        uint64_t sequence = 1000u + (uint64_t)seed_index *
                            DIFFERENTIAL_OPS_PER_SEED;
        uint32_t i;
        fixture_init(&f, 8, 8);
        for (i = 0u; i < 3u; ++i) {
            uint32_t variant;
            uint32_t p;
            for (variant = 0u; variant < 2u; ++variant) {
                for (p = 0u; p < 16u; ++p) {
                    size_t off = (size_t)p * 4u;
                    pixels[i][variant][off + 0u] =
                        (uint8_t)(31u + i * 73u + variant * 41u + p * 7u);
                    pixels[i][variant][off + 1u] =
                        (uint8_t)(211u - i * 37u + variant * 19u - p * 3u);
                    pixels[i][variant][off + 2u] =
                        (uint8_t)(17u + i * 29u + variant * 97u + p * 11u);
                    pixels[i][variant][off + 3u] =
                        (p % 5u == 0u) ? 128u : 255u;
                }
            }
            {
                MooCompBufferView view0 = rgba_view(pixels[i][0], 4, 4);
                MooCompBufferView view1 = rgba_view(pixels[i][1], 4, 4);
                CHECK(moo_comp_client_create(&f.core, &clients[i]) == MOO_COMP_OK,
                      "differential client");
                CHECK(moo_comp_surface_create(&f.core, clients[i],
                                              &surfaces[i]) == MOO_COMP_OK,
                      "differential surface");
                CHECK(moo_comp_buffer_register(&f.core, clients[i], &view0,
                                               &buffers[i][0]) == MOO_COMP_OK,
                      "differential buffer zero");
                CHECK(moo_comp_buffer_register(&f.core, clients[i], &view1,
                                               &buffers[i][1]) == MOO_COMP_OK,
                      "differential buffer one");
                CHECK(moo_comp_surface_set_position(&f.core, surfaces[i],
                                                    (int32_t)i * 2 - 1,
                                                    (int32_t)i) == MOO_COMP_OK,
                      "differential initial position");
                CHECK(commit_buffer(&f, clients[i], surfaces[i], buffers[i][0],
                                    1u, 255u, 4, 4, 0u) == MOO_COMP_OK,
                      "differential initial commit");
            }
        }
        verify_partial_present(&f, sequence++);
        for (i = 0u; i < DIFFERENTIAL_OPS_PER_SEED; ++i) {
            uint32_t value = xorshift32(&rng);
            uint32_t which = value % 3u;
            uint32_t operation = (value >> 8u) % 6u;
            MooCompResult result = MOO_COMP_OK;
            if (operation == 0u) {
                int32_t x = (int32_t)((value >> 12u) % 14u) - 4;
                int32_t y = (int32_t)((value >> 20u) % 14u) - 4;
                result = moo_comp_surface_set_position(&f.core, surfaces[which],
                                                       x, y);
            } else if (operation == 1u) {
                result = moo_comp_surface_raise(&f.core, surfaces[which]);
            } else if (operation == 2u) {
                uint32_t opacity = opacity_values[(value >> 16u) %
                    (sizeof(opacity_values) / sizeof(opacity_values[0]))];
                result = moo_comp_surface_set_opacity(&f.core, clients[which],
                                                      surfaces[which], opacity);
                if (result == MOO_COMP_OK)
                    result = moo_comp_surface_commit(&f.core, clients[which],
                                                     surfaces[which]);
            } else if (operation == 3u) {
                MooCompRect rect;
                rect.x = (int32_t)((value >> 12u) % 4u);
                rect.y = (int32_t)((value >> 16u) % 4u);
                rect.width = 1 + (int32_t)((value >> 20u) %
                                           (uint32_t)(4 - rect.x));
                rect.height = 1 + (int32_t)((value >> 24u) %
                                            (uint32_t)(4 - rect.y));
                result = moo_comp_surface_damage(&f.core, clients[which],
                                                 surfaces[which], rect);
                if (result == MOO_COMP_OK)
                    result = moo_comp_surface_commit(&f.core, clients[which],
                                                     surfaces[which]);
            } else if (operation == 4u) {
                uint32_t next = active_buffer[which] ^ 1u;
                MooCompRect full = {0, 0, 4, 4};
                result = moo_comp_surface_attach(&f.core, clients[which],
                                                 surfaces[which],
                                                 buffers[which][next]);
                if (result == MOO_COMP_OK)
                    result = moo_comp_surface_damage(&f.core, clients[which],
                                                     surfaces[which], full);
                if (result == MOO_COMP_OK)
                    result = moo_comp_surface_commit(&f.core, clients[which],
                                                     surfaces[which]);
                if (result == MOO_COMP_OK) active_buffer[which] = next;
            } else {
                uint32_t scale = scales[(value >> 16u) %
                    (sizeof(scales) / sizeof(scales[0]))];
                result = moo_comp_surface_set_scale(&f.core, clients[which],
                                                    surfaces[which], scale);
                if (result == MOO_COMP_OK)
                    result = moo_comp_surface_commit(&f.core, clients[which],
                                                     surfaces[which]);
            }
            CHECK(result == MOO_COMP_OK, "differential operation accepted");
            verify_partial_present(&f, sequence++);
            (void)drain_events(&f, clients[0]);
            (void)drain_events(&f, clients[1]);
            (void)drain_events(&f, clients[2]);
        }
        fixture_guards(&f);
    }
}

static void test_three_clients_alpha_z_and_callbacks(void) {
    Fixture f;
    uint8_t red[3u * 2u * 4u];
    uint8_t green[2u * 2u * 4u];
    uint8_t blue[1u * 3u * 4u];
    MooCompBufferView red_view;
    MooCompBufferView green_view;
    MooCompBufferView blue_view;
    MooCompHandle ca, cb, cc, sa, sb, sc, ba, bb, bc;
    MooCompEvent event;
    uint64_t frame_id = 0u;

    fixture_init(&f, 4, 3);
    fill_solid(red, 3, 2, 255u, 0u, 0u, 255u);
    fill_solid(green, 2, 2, 0u, 255u, 0u, 255u);
    fill_solid(blue, 1, 3, 0u, 0u, 255u, 255u);
    red_view = rgba_view(red, 3, 2);
    green_view = rgba_view(green, 2, 2);
    blue_view = rgba_view(blue, 1, 3);

    CHECK(create_client_surface_buffer(&f, &red_view, &ca, &sa, &ba) ==
              MOO_COMP_OK, "client A");
    CHECK(create_client_surface_buffer(&f, &green_view, &cb, &sb, &bb) ==
              MOO_COMP_OK, "client B");
    CHECK(create_client_surface_buffer(&f, &blue_view, &cc, &sc, &bc) ==
              MOO_COMP_OK, "client C");
    CHECK(moo_comp_surface_set_position(&f.core, sa, 0, 0) == MOO_COMP_OK,
          "position A");
    CHECK(moo_comp_surface_set_position(&f.core, sb, 1, 0) == MOO_COMP_OK,
          "position B");
    CHECK(moo_comp_surface_set_position(&f.core, sc, 3, 0) == MOO_COMP_OK,
          "position C");
    CHECK(commit_buffer(&f, ca, sa, ba, 1u, 255u, 3, 2, 101u) ==
              MOO_COMP_OK, "commit A");
    CHECK(commit_buffer(&f, cb, sb, bb, 1u, 128u, 2, 2, 201u) ==
              MOO_COMP_OK, "commit B alpha");
    CHECK(commit_buffer(&f, cc, sc, bc, 1u, 255u, 1, 3, 301u) ==
              MOO_COMP_OK, "commit C");

    CHECK(moo_comp_build_frame(&f.core, &f.output, &frame_id) == MOO_COMP_OK,
          "initial build");
    CHECK(output_hash(&f.output) == UINT64_C(0x93b367fb85bd6271),
          "three-client alpha golden");
    CHECK(pixel_is(&f.output, 1, 0, 127u, 128u, 0u, 255u),
          "integer opacity source-over");
    check_partial_equals_full(&f, "initial partial equals full");
    CHECK(moo_comp_next_event(&f.core, ca, &event) == MOO_COMP_WOULD_BLOCK,
          "A callback not before present");
    CHECK(moo_comp_next_event(&f.core, cb, &event) == MOO_COMP_WOULD_BLOCK,
          "B callback not before present");
    CHECK(moo_comp_next_event(&f.core, cc, &event) == MOO_COMP_WOULD_BLOCK,
          "C callback not before present");
    CHECK(moo_comp_present_done(&f.core, frame_id, 10u, 10000u) == MOO_COMP_OK,
          "initial present");
    expect_frame_event(&f, ca, 101u, MOO_COMP_FRAME_PRESENTED, 10u);
    expect_frame_event(&f, cb, 201u, MOO_COMP_FRAME_PRESENTED, 10u);
    expect_frame_event(&f, cc, 301u, MOO_COMP_FRAME_PRESENTED, 10u);

    CHECK(moo_comp_surface_raise(&f.core, sa) == MOO_COMP_OK, "raise A");
    build_present(&f, 11u);
    CHECK(output_hash(&f.output) == UINT64_C(0xc14943feaa697171),
          "stable z-order golden");
    check_partial_equals_full(&f, "raise partial equals full");
    fixture_guards(&f);
}

static void test_scale_resize_and_atomic_rejects(void) {
    Fixture f;
    uint8_t scaled[4u * 4u * 4u];
    uint8_t yellow[4u] = {255u, 255u, 0u, 255u};
    uint8_t odd[3u * 3u * 4u];
    MooCompBufferView scaled_view;
    MooCompBufferView yellow_view;
    MooCompBufferView odd_view;
    MooCompBufferView bad_view;
    MooCompHandle c1, c2, surface, scaled_buffer, yellow_buffer, odd_buffer;
    MooCompHandle ignored_surface, ignored_buffer;
    MooCompSurfaceInfo info;
    MooCompRect invalid_rect = {0, 0, -1, 1};
    StateSnapshot snapshot;
    uint64_t before;
    uint32_t damage_before;

    fixture_init(&f, 4, 3);
    fill_solid(scaled, 4, 4, 9u, 9u, 9u, 255u);
    /* scale=2 samples the top-left pixel of each 2x2 buffer cell. */
    scaled[0u] = 255u; scaled[1u] = 0u; scaled[2u] = 0u; scaled[3u] = 255u;
    scaled[8u] = 0u; scaled[9u] = 255u; scaled[10u] = 0u; scaled[11u] = 255u;
    scaled[32u] = 0u; scaled[33u] = 0u; scaled[34u] = 255u; scaled[35u] = 255u;
    scaled[40u] = 255u; scaled[41u] = 255u; scaled[42u] = 255u; scaled[43u] = 255u;
    fill_solid(odd, 3, 3, 1u, 2u, 3u, 255u);
    scaled_view = rgba_view(scaled, 4, 4);
    yellow_view = rgba_view(yellow, 1, 1);
    odd_view = rgba_view(odd, 3, 3);

    CHECK(create_client_surface_buffer(&f, &scaled_view,
                                       &c1, &surface, &scaled_buffer) ==
              MOO_COMP_OK, "scale client");
    CHECK(moo_comp_client_create(&f.core, &c2) == MOO_COMP_OK, "second owner");
    CHECK(moo_comp_surface_set_position(&f.core, surface, 1, 1) == MOO_COMP_OK,
          "scale position");
    CHECK(commit_buffer(&f, c1, surface, scaled_buffer, 2u, 255u, 4, 4, 0u) ==
              MOO_COMP_OK, "scale commit");
    build_present(&f, 20u);
    CHECK(output_hash(&f.output) == UINT64_C(0xd79738662fd6bcea),
          "integer scale golden");
    CHECK(moo_comp_surface_info(&f.core, surface, &info) == MOO_COMP_OK &&
          info.logical_width == 2 && info.logical_height == 2 &&
          info.scale == 2u, "logical scale info");
    check_partial_equals_full(&f, "scale partial equals full");

    CHECK(moo_comp_buffer_register(&f.core, c1, &yellow_view, &yellow_buffer) ==
              MOO_COMP_OK, "resize buffer");
    CHECK(commit_buffer(&f, c1, surface, yellow_buffer, 1u, 255u, 1, 1, 0u) ==
              MOO_COMP_OK, "resize commit");
    CHECK(damage_covers(&f.core, 2, 2), "resize damages old exposed bounds");
    build_present(&f, 21u);
    CHECK(output_hash(&f.output) == UINT64_C(0x5bd753c9214ae14c),
          "resize old-new golden");
    check_partial_equals_full(&f, "resize partial equals full");

    before = moo_comp_state_hash(&f.core);
    damage_before = moo_comp_damage_count(&f.core);
    CHECK(moo_comp_surface_set_scale(&f.core, c1, surface, 0u) ==
              MOO_COMP_INVALID, "scale zero rejected");
    CHECK(moo_comp_state_hash(&f.core) == before &&
          moo_comp_damage_count(&f.core) == damage_before,
          "scale reject no mutation");

    before = moo_comp_state_hash(&f.core);
    CHECK(moo_comp_surface_set_opacity(&f.core, c1, surface, 256u) ==
              MOO_COMP_INVALID, "opacity 256 rejected");
    CHECK(moo_comp_state_hash(&f.core) == before,
          "opacity reject no mutation");

    before = moo_comp_state_hash(&f.core);
    CHECK(moo_comp_surface_damage(&f.core, c1, surface, invalid_rect) ==
              MOO_COMP_INVALID, "negative damage rejected");
    CHECK(moo_comp_state_hash(&f.core) == before,
          "damage reject no mutation");

    before = moo_comp_state_hash(&f.core);
    CHECK(moo_comp_surface_attach(&f.core, c2, surface, yellow_buffer) ==
              MOO_COMP_ACCESS, "cross-owner attach rejected");
    CHECK(moo_comp_state_hash(&f.core) == before,
          "cross-owner no mutation");

    bad_view = yellow_view;
    bad_view.buffer_bytes = 3u;
    before = moo_comp_state_hash(&f.core);
    CHECK(moo_comp_buffer_register(&f.core, c1, &bad_view, &ignored_buffer) ==
              MOO_COMP_BAD_BUFFER, "short buffer rejected");
    CHECK(moo_comp_state_hash(&f.core) == before,
          "bad register no mutation");

    CHECK(moo_comp_buffer_register(&f.core, c1, &odd_view, &odd_buffer) ==
              MOO_COMP_OK, "odd buffer registered");
    CHECK(moo_comp_surface_attach(&f.core, c1, surface, odd_buffer) ==
              MOO_COMP_OK, "odd pending attach");
    CHECK(moo_comp_surface_set_scale(&f.core, c1, surface, 2u) ==
              MOO_COMP_OK, "odd pending scale");
    state_snapshot_take(&f, &snapshot);
    before = moo_comp_state_hash(&f.core);
    damage_before = moo_comp_damage_count(&f.core);
    CHECK(moo_comp_surface_commit(&f.core, c1, surface) ==
              MOO_COMP_BAD_BUFFER, "incompatible scale commit rejected");
    CHECK(moo_comp_state_hash(&f.core) == before &&
          moo_comp_damage_count(&f.core) == damage_before,
          "invalid commit atomic");
    CHECK(state_snapshot_equal(&f, &snapshot),
          "invalid commit bitwise no mutation");

    (void)ignored_surface;
    fixture_guards(&f);
}

static void test_owner_and_stale_reuse(void) {
    Fixture f;
    uint8_t pixel[4u] = {1u, 2u, 3u, 255u};
    MooCompBufferView view = rgba_view(pixel, 1, 1);
    MooCompHandle c1, c2, b1, s1, s2;
    uint64_t before;

    fixture_init(&f, 2, 2);
    CHECK(moo_comp_client_create(&f.core, &c1) == MOO_COMP_OK, "owner client");
    CHECK(moo_comp_client_create(&f.core, &c2) == MOO_COMP_OK, "other client");
    CHECK(moo_comp_buffer_register(&f.core, c1, &view, &b1) == MOO_COMP_OK,
          "owner buffer");
    CHECK(moo_comp_surface_create(&f.core, c1, &s1) == MOO_COMP_OK,
          "first surface");
    before = moo_comp_state_hash(&f.core);
    CHECK(moo_comp_surface_destroy(&f.core, c2, s1) == MOO_COMP_ACCESS,
          "cross-owner destroy");
    CHECK(moo_comp_state_hash(&f.core) == before,
          "cross-owner destroy no mutation");

    CHECK(moo_comp_surface_destroy(&f.core, c1, s1) == MOO_COMP_OK,
          "destroy first surface");
    CHECK(moo_comp_surface_create(&f.core, c1, &s2) == MOO_COMP_OK,
          "reuse surface slot");
    CHECK(handle_slot(s1) == handle_slot(s2) && s1 != s2,
          "slot reused with new generation");
    before = moo_comp_state_hash(&f.core);
    CHECK(moo_comp_surface_attach(&f.core, c1, s1, b1) ==
              MOO_COMP_STALE_HANDLE, "stale surface rejected");
    CHECK(moo_comp_state_hash(&f.core) == before,
          "stale surface no mutation");
    fixture_guards(&f);
}

static void test_occluded_callback_exactly_once(void) {
    Fixture f;
    uint8_t red[2u * 2u * 4u];
    uint8_t green[2u * 2u * 4u];
    MooCompBufferView red_view;
    MooCompBufferView green_view;
    MooCompHandle ca, cb, sa, sb, ba, bb;
    MooCompRect one = {0, 0, 2, 2};
    MooCompEvent event;
    uint64_t frame_id = 0u;

    fixture_init(&f, 2, 2);
    fill_solid(red, 2, 2, 255u, 0u, 0u, 255u);
    fill_solid(green, 2, 2, 0u, 255u, 0u, 255u);
    red_view = rgba_view(red, 2, 2);
    green_view = rgba_view(green, 2, 2);
    CHECK(create_client_surface_buffer(&f, &red_view, &ca, &sa, &ba) ==
              MOO_COMP_OK, "occluded lower");
    CHECK(create_client_surface_buffer(&f, &green_view, &cb, &sb, &bb) ==
              MOO_COMP_OK, "occluding upper");
    CHECK(commit_buffer(&f, ca, sa, ba, 1u, 255u, 2, 2, 0u) ==
              MOO_COMP_OK, "lower initial");
    CHECK(commit_buffer(&f, cb, sb, bb, 1u, 255u, 2, 2, 0u) ==
              MOO_COMP_OK, "upper initial");
    build_present(&f, 30u);
    CHECK(pixel_is(&f.output, 0, 0, 0u, 255u, 0u, 255u),
          "upper opaque wins");

    CHECK(moo_comp_surface_frame(&f.core, ca, sa, 900u) == MOO_COMP_OK,
          "occluded frame token");
    CHECK(moo_comp_surface_damage(&f.core, ca, sa, one) == MOO_COMP_OK,
          "occluded damage");
    CHECK(moo_comp_surface_commit(&f.core, ca, sa) == MOO_COMP_OK,
          "occluded commit");
    CHECK(moo_comp_damage_count(&f.core) == 0u,
          "opaque overlap culls lower-only damage");
    CHECK(moo_comp_build_frame(&f.core, &f.output, &frame_id) == MOO_COMP_OK,
          "occluded build");
    CHECK(moo_comp_next_event(&f.core, ca, &event) == MOO_COMP_WOULD_BLOCK,
          "occluded callback not before present");
    CHECK(moo_comp_present_done(&f.core, frame_id, 31u, 31000u) ==
              MOO_COMP_OK, "occluded present");
    expect_frame_event(&f, ca, 900u, MOO_COMP_FRAME_OCCLUDED, 31u);
    fixture_guards(&f);
}

static void test_cursor_focus_topmost_disconnect(void) {
    Fixture f;
    uint8_t under[3u * 3u * 4u];
    uint8_t cursor[2u * 2u * 4u] = {
        255u, 0u, 0u, 255u,  0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u
    };
    MooCompBufferView under_view;
    MooCompBufferView cursor_view = rgba_view(cursor, 2, 2);
    MooCompHandle c1, c2, surface, under_buffer, cursor_buffer;
    uint64_t before;

    fixture_init(&f, 3, 3);
    fill_solid(under, 3, 3, 0u, 255u, 0u, 255u);
    under_view = rgba_view(under, 3, 3);
    CHECK(create_client_surface_buffer(&f, &under_view,
                                       &c1, &surface, &under_buffer) ==
              MOO_COMP_OK, "cursor owner surface");
    CHECK(moo_comp_client_create(&f.core, &c2) == MOO_COMP_OK,
          "cursor other client");
    CHECK(moo_comp_buffer_register(&f.core, c1, &cursor_view, &cursor_buffer) ==
              MOO_COMP_OK, "cursor buffer");
    CHECK(commit_buffer(&f, c1, surface, under_buffer, 1u, 255u, 3, 3, 0u) ==
              MOO_COMP_OK, "under commit");
    build_present(&f, 40u);

    before = moo_comp_state_hash(&f.core);
    CHECK(moo_comp_cursor_set_buffer(&f.core, c1, cursor_buffer,
                                     0, 0, 1u) == MOO_COMP_ACCESS,
          "cursor needs pointer focus");
    CHECK(moo_comp_state_hash(&f.core) == before,
          "unfocused cursor no mutation");
    CHECK(moo_comp_pointer_focus(&f.core, c1) == MOO_COMP_OK,
          "pointer focus");
    CHECK(moo_comp_pointer_position(&f.core, 1, 1) == MOO_COMP_OK,
          "pointer position");
    CHECK(moo_comp_cursor_set_buffer(&f.core, c1, cursor_buffer,
                                     0, 0, 1u) == MOO_COMP_OK,
          "focused cursor set");
    build_present(&f, 41u);
    CHECK(pixel_is(&f.output, 1, 1, 255u, 0u, 0u, 255u),
          "cursor is topmost");

    before = moo_comp_state_hash(&f.core);
    CHECK(moo_comp_cursor_hide(&f.core, c2) == MOO_COMP_ACCESS,
          "nonfocus cannot hide cursor");
    CHECK(moo_comp_state_hash(&f.core) == before,
          "foreign cursor hide no mutation");

    CHECK(moo_comp_client_disconnect(&f.core, c1) == MOO_COMP_OK,
          "cursor owner disconnect");
    CHECK(f.core.cursor.visible == 0u &&
          f.core.cursor.buffer == MOO_COMP_HANDLE_INVALID,
          "disconnect clears cursor state");
    build_present(&f, 42u);
    CHECK(pixel_is(&f.output, 1, 1, 0u, 0u, 0u, 255u),
          "disconnect removes cursor and owned surface");
    fixture_guards(&f);
}

static void test_buffer_busy_release_and_pending(void) {
    Fixture f;
    uint8_t a[4u] = {255u, 0u, 0u, 255u};
    uint8_t b[4u] = {0u, 255u, 0u, 255u};
    uint8_t c[4u] = {0u, 0u, 255u, 255u};
    MooCompBufferView av = rgba_view(a, 1, 1);
    MooCompBufferView bv = rgba_view(b, 1, 1);
    MooCompBufferView cv = rgba_view(c, 1, 1);
    MooCompHandle client, surface, ba, bb, bc;
    MooCompBufferInfo info;
    MooCompEvent event;
    StateSnapshot snapshot;
    fixture_init(&f, 2, 2);
    CHECK(moo_comp_client_create(&f.core, &client) == MOO_COMP_OK,
          "buffer client");
    CHECK(moo_comp_surface_create(&f.core, client, &surface) == MOO_COMP_OK,
          "buffer surface");
    CHECK(moo_comp_buffer_register(&f.core, client, &av, &ba) == MOO_COMP_OK,
          "buffer A register");
    CHECK(moo_comp_buffer_register(&f.core, client, &bv, &bb) == MOO_COMP_OK,
          "buffer B register");
    CHECK(moo_comp_buffer_register(&f.core, client, &cv, &bc) == MOO_COMP_OK,
          "buffer C register");
    CHECK(commit_buffer(&f, client, surface, ba, 1u, 255u, 1, 1, 0u) ==
              MOO_COMP_OK, "buffer A busy");
    build_present(&f, 50u);

    state_snapshot_take(&f, &snapshot);
    CHECK(moo_comp_buffer_unregister(&f.core, client, ba) ==
              MOO_COMP_BAD_STATE, "busy buffer unregister rejected");
    CHECK(state_snapshot_equal(&f, &snapshot),
          "busy unregister bitwise no mutation");

    CHECK(commit_buffer(&f, client, surface, bb, 1u, 255u, 1, 1, 0u) ==
              MOO_COMP_OK, "replace busy buffer");
    CHECK(moo_comp_buffer_info(&f.core, ba, &info) == MOO_COMP_OK &&
          info.ref_count == 0u, "old buffer released ref");
    CHECK(moo_comp_next_event(&f.core, client, &event) == MOO_COMP_OK &&
          event.type == MOO_COMP_EVENT_BUFFER_RELEASE && event.object == ba,
          "buffer release event exact object");
    CHECK(moo_comp_next_event(&f.core, client, &event) ==
              MOO_COMP_WOULD_BLOCK, "buffer release exactly once");
    CHECK(moo_comp_buffer_unregister(&f.core, client, ba) == MOO_COMP_OK,
          "released buffer unregisters");
    CHECK(moo_comp_buffer_info(&f.core, ba, &info) ==
              MOO_COMP_STALE_HANDLE, "unregistered buffer stale");

    CHECK(moo_comp_surface_attach(&f.core, client, surface, bc) == MOO_COMP_OK,
          "pending buffer attach");
    state_snapshot_take(&f, &snapshot);
    CHECK(moo_comp_buffer_unregister(&f.core, client, bc) ==
              MOO_COMP_BAD_STATE, "pending buffer unregister rejected");
    CHECK(state_snapshot_equal(&f, &snapshot),
          "pending unregister bitwise no mutation");
    CHECK(moo_comp_surface_attach(&f.core, client, surface,
                                  MOO_COMP_HANDLE_INVALID) == MOO_COMP_OK,
          "replace pending attach with detach");
    CHECK(moo_comp_buffer_unregister(&f.core, client, bc) == MOO_COMP_OK,
          "superseded pending buffer unregisters");
    fixture_guards(&f);
}

static void test_buffer_release_queue_backpressure_recovery(void) {
    Fixture f;
    uint8_t red[4u] = {255u, 0u, 0u, 255u};
    uint8_t green[4u] = {0u, 255u, 0u, 255u};
    MooCompBufferView red_view = rgba_view(red, 1, 1);
    MooCompBufferView green_view = rgba_view(green, 1, 1);
    MooCompHandle client, surface, red_buffer, green_buffer;
    MooCompRect one = {0, 0, 1, 1};
    MooCompBufferInfo red_before;
    MooCompBufferInfo red_after;
    MooCompBufferInfo green_after;
    MooCompEvent event;
    StateSnapshot snapshot;
    uint32_t frame_events = 0u;
    uint32_t release_events = 0u;
    uint32_t i;

    /* One client => event quota is exactly EVENT_CAP(4). */
    fixture_init_caps(&f, 2, 2, 1u, 2u, 3u, 4u, 4u);
    CHECK(moo_comp_client_create(&f.core, &client) == MOO_COMP_OK,
          "release pressure client");
    CHECK(moo_comp_surface_create(&f.core, client, &surface) == MOO_COMP_OK,
          "release pressure surface");
    CHECK(moo_comp_buffer_register(&f.core, client, &red_view, &red_buffer) ==
              MOO_COMP_OK, "release pressure red buffer");
    CHECK(moo_comp_buffer_register(&f.core, client, &green_view,
                                   &green_buffer) == MOO_COMP_OK,
          "release pressure green buffer");
    CHECK(commit_buffer(&f, client, surface, red_buffer,
                        1u, 255u, 1, 1, 0u) == MOO_COMP_OK,
          "release pressure initial commit");
    build_present(&f, 100u);

    for (i = 0u; i < 4u; ++i) {
        CHECK(moo_comp_surface_frame(&f.core, client, surface,
                                     5000u + i) == MOO_COMP_OK,
              "fill release event quota token");
        CHECK(moo_comp_surface_commit(&f.core, client, surface) == MOO_COMP_OK,
              "fill release event quota commit");
        build_present(&f, 101u + i);
    }
    CHECK(moo_comp_buffer_info(&f.core, red_buffer, &red_before) ==
              MOO_COMP_OK && red_before.ref_count == 1u,
          "old buffer busy before blocked release");
    CHECK(moo_comp_surface_attach(&f.core, client, surface, green_buffer) ==
              MOO_COMP_OK, "pending release-producing attach");
    CHECK(moo_comp_surface_damage(&f.core, client, surface, one) == MOO_COMP_OK,
          "pending release-producing damage");
    state_snapshot_take(&f, &snapshot);
    CHECK(moo_comp_surface_commit(&f.core, client, surface) ==
              MOO_COMP_WOULD_BLOCK,
          "release-producing commit blocks on full owner event quota");
    CHECK(state_snapshot_equal(&f, &snapshot),
          "blocked release commit has bitwise no mutation");
    CHECK(moo_comp_buffer_info(&f.core, red_buffer, &red_after) ==
              MOO_COMP_OK && red_after.ref_count == red_before.ref_count,
          "blocked release preserves old refcount");
    CHECK(moo_comp_buffer_info(&f.core, green_buffer, &green_after) ==
              MOO_COMP_OK && green_after.ref_count == 0u,
          "blocked release preserves new refcount");

    CHECK(moo_comp_next_event(&f.core, client, &event) == MOO_COMP_OK &&
          event.type == MOO_COMP_EVENT_FRAME_DONE && event.token == 5000u,
          "one event drain opens exactly one quota slot");
    CHECK(moo_comp_surface_commit(&f.core, client, surface) == MOO_COMP_OK,
          "release-producing commit recovers after one drain");
    CHECK(moo_comp_buffer_info(&f.core, red_buffer, &red_after) ==
              MOO_COMP_OK && red_after.ref_count == 0u,
          "successful recovery releases old ref");
    CHECK(moo_comp_buffer_info(&f.core, green_buffer, &green_after) ==
              MOO_COMP_OK && green_after.ref_count == 1u,
          "successful recovery acquires new ref");

    while (moo_comp_next_event(&f.core, client, &event) == MOO_COMP_OK) {
        if (event.type == MOO_COMP_EVENT_BUFFER_RELEASE) {
            release_events++;
            CHECK(event.object == red_buffer,
                  "recovered release event names old buffer");
        } else if (event.type == MOO_COMP_EVENT_FRAME_DONE) {
            frame_events++;
            CHECK(event.token >= 5001u && event.token <= 5003u,
                  "remaining frame event token after recovery");
        } else {
            CHECK(0, "unexpected event under release pressure");
        }
    }
    CHECK(frame_events == 3u, "three queued frame events remain after drain");
    CHECK(release_events == 1u, "exactly one recovered BUFFER_RELEASE event");
    CHECK(moo_comp_buffer_unregister(&f.core, client, red_buffer) == MOO_COMP_OK,
          "released old buffer unregisters after queue recovery");
    CHECK(moo_comp_next_event(&f.core, client, &event) ==
              MOO_COMP_WOULD_BLOCK,
          "unregister does not duplicate BUFFER_RELEASE");
    build_present(&f, 105u);
    CHECK(pixel_is(&f.output, 0, 0, 0u, 255u, 0u, 255u),
          "normal presentation recovers after release backpressure");
    fixture_guards(&f);
}

static void test_callback_statuses_and_duplicate_token(void) {
    Fixture f;
    uint8_t pixel[4u] = {9u, 8u, 7u, 255u};
    MooCompBufferView view = rgba_view(pixel, 1, 1);
    MooCompHandle client, buffer, first, second;
    MooCompEvent event;
    MooCompRect one = {0, 0, 1, 1};
    StateSnapshot snapshot;
    uint64_t frame_id = 0u;
    fixture_init(&f, 2, 2);
    CHECK(moo_comp_client_create(&f.core, &client) == MOO_COMP_OK,
          "callback client");
    CHECK(moo_comp_buffer_register(&f.core, client, &view, &buffer) ==
              MOO_COMP_OK, "callback buffer");
    CHECK(moo_comp_surface_create(&f.core, client, &first) == MOO_COMP_OK,
          "callback first surface");
    CHECK(moo_comp_surface_create(&f.core, client, &second) == MOO_COMP_OK,
          "callback second surface");
    CHECK(moo_comp_surface_set_position(&f.core, second, 1, 0) == MOO_COMP_OK,
          "callback second position");
    CHECK(commit_buffer(&f, client, first, buffer, 1u, 255u, 1, 1, 0u) ==
              MOO_COMP_OK, "callback initial first");
    CHECK(commit_buffer(&f, client, second, buffer, 1u, 255u, 1, 1, 0u) ==
              MOO_COMP_OK, "callback initial second");
    build_present(&f, 60u);

    CHECK(moo_comp_surface_frame(&f.core, client, first, 1001u) == MOO_COMP_OK,
          "first pending token");
    state_snapshot_take(&f, &snapshot);
    CHECK(moo_comp_surface_frame(&f.core, client, second, 1001u) ==
              MOO_COMP_BAD_STATE, "duplicate owner token rejected");
    CHECK(state_snapshot_equal(&f, &snapshot),
          "duplicate token bitwise no mutation");
    CHECK(moo_comp_surface_damage(&f.core, client, first, one) == MOO_COMP_OK,
          "first token damage");
    CHECK(moo_comp_surface_commit(&f.core, client, first) == MOO_COMP_OK,
          "first token commit");
    CHECK(moo_comp_surface_frame(&f.core, client, first, 1002u) == MOO_COMP_OK,
          "superseding token");
    CHECK(moo_comp_surface_damage(&f.core, client, first, one) == MOO_COMP_OK,
          "superseding damage");
    CHECK(moo_comp_surface_commit(&f.core, client, first) == MOO_COMP_OK,
          "superseding commit");
    CHECK(moo_comp_build_frame(&f.core, &f.output, &frame_id) == MOO_COMP_OK,
          "superseded build");
    CHECK(moo_comp_present_done(&f.core, frame_id, 61u, 61000u) == MOO_COMP_OK,
          "superseded present");
    pop_frame_event(&f, client, 1001u, MOO_COMP_FRAME_SUPERSEDED, 61u);
    pop_frame_event(&f, client, 1002u, MOO_COMP_FRAME_PRESENTED, 61u);
    CHECK(moo_comp_next_event(&f.core, client, &event) ==
              MOO_COMP_WOULD_BLOCK, "superseded events exactly once");

    CHECK(moo_comp_surface_frame(&f.core, client, second, 1003u) == MOO_COMP_OK,
          "cancel token");
    CHECK(moo_comp_surface_damage(&f.core, client, second, one) == MOO_COMP_OK,
          "cancel damage");
    CHECK(moo_comp_surface_commit(&f.core, client, second) == MOO_COMP_OK,
          "cancel commit");
    CHECK(moo_comp_surface_destroy(&f.core, client, second) == MOO_COMP_OK,
          "destroy callback surface");
    CHECK(moo_comp_build_frame(&f.core, &f.output, &frame_id) == MOO_COMP_OK,
          "cancelled build");
    CHECK(moo_comp_present_done(&f.core, frame_id, 62u, 62000u) == MOO_COMP_OK,
          "cancelled present");
    expect_frame_event(&f, client, 1003u, MOO_COMP_FRAME_CANCELLED, 62u);
    fixture_guards(&f);
}

static void test_handle_wrap_wrong_kind_and_capacities(void) {
    uint8_t pixel[4u] = {1u, 2u, 3u, 255u};
    MooCompBufferView view = rgba_view(pixel, 1, 1);
    {
        Fixture f;
        MooCompHandle client, buffer, wrapped, next;
        StateSnapshot snapshot;
        fixture_init_caps(&f, 2, 2, 2u, 2u, 2u, 2u, 2u);
        CHECK(moo_comp_client_create(&f.core, &client) == MOO_COMP_OK,
              "wrap client");
        CHECK(moo_comp_buffer_register(&f.core, client, &view, &buffer) ==
                  MOO_COMP_OK, "wrap buffer");
        f.surfaces[0].generation = UINT32_MAX;
        CHECK(moo_comp_surface_create(&f.core, client, &wrapped) == MOO_COMP_OK &&
              handle_slot(wrapped) == 0u, "max generation surface");
        CHECK(moo_comp_surface_destroy(&f.core, client, wrapped) == MOO_COMP_OK,
              "destroy max generation surface");
        CHECK(f.surfaces[0].generation == 0u,
              "wrapped generation quarantined");
        CHECK(moo_comp_surface_create(&f.core, client, &next) == MOO_COMP_OK &&
              handle_slot(next) == 1u, "quarantine skips wrapped slot");
        state_snapshot_take(&f, &snapshot);
        CHECK(moo_comp_surface_attach(&f.core, client, wrapped, buffer) ==
                  MOO_COMP_STALE_HANDLE, "wrapped old handle stale");
        CHECK(state_snapshot_equal(&f, &snapshot),
              "wrapped stale handle no mutation");
        state_snapshot_take(&f, &snapshot);
        CHECK(moo_comp_surface_attach(&f.core, client, next, next) ==
                  MOO_COMP_WRONG_KIND, "wrong-kind buffer rejected");
        CHECK(state_snapshot_equal(&f, &snapshot),
              "wrong-kind bitwise no mutation");
        fixture_guards(&f);
    }
    {
        Fixture f;
        MooCompHandle clients[3u];
        StateSnapshot snapshot;
        fixture_init_caps(&f, 2, 2, 2u, 2u, 2u, 2u, 2u);
        CHECK(moo_comp_client_create(&f.core, &clients[0]) == MOO_COMP_OK,
              "client cap first");
        CHECK(moo_comp_client_create(&f.core, &clients[1]) == MOO_COMP_OK,
              "client cap second");
        state_snapshot_take(&f, &snapshot);
        CHECK(moo_comp_client_create(&f.core, &clients[2]) == MOO_COMP_LIMIT,
              "client cap plus one");
        CHECK(state_snapshot_equal(&f, &snapshot),
              "client cap bitwise no mutation");
        fixture_guards(&f);
    }
    {
        Fixture f;
        MooCompHandle client, surfaces[3u], buffers[3u];
        StateSnapshot snapshot;
        fixture_init_caps(&f, 2, 2, 1u, 2u, 2u, 2u, 2u);
        CHECK(moo_comp_client_create(&f.core, &client) == MOO_COMP_OK,
              "object cap client");
        CHECK(moo_comp_surface_create(&f.core, client, &surfaces[0]) ==
                  MOO_COMP_OK, "surface cap first");
        CHECK(moo_comp_surface_create(&f.core, client, &surfaces[1]) ==
                  MOO_COMP_OK, "surface cap second");
        state_snapshot_take(&f, &snapshot);
        CHECK(moo_comp_surface_create(&f.core, client, &surfaces[2]) ==
                  MOO_COMP_LIMIT, "surface cap plus one");
        CHECK(state_snapshot_equal(&f, &snapshot),
              "surface cap bitwise no mutation");
        CHECK(moo_comp_buffer_register(&f.core, client, &view, &buffers[0]) ==
                  MOO_COMP_OK, "buffer cap first");
        CHECK(moo_comp_buffer_register(&f.core, client, &view, &buffers[1]) ==
                  MOO_COMP_OK, "buffer cap second");
        state_snapshot_take(&f, &snapshot);
        CHECK(moo_comp_buffer_register(&f.core, client, &view, &buffers[2]) ==
                  MOO_COMP_LIMIT, "buffer cap plus one");
        CHECK(state_snapshot_equal(&f, &snapshot),
              "buffer cap bitwise no mutation");
        fixture_guards(&f);
    }
}

static void test_damage_capacity_and_recovery(void) {
    Fixture f;
    uint8_t pixels[4u * 4u * 4u];
    MooCompBufferView view;
    MooCompHandle client, surface, buffer;
    uint32_t i;
    fill_solid(pixels, 4, 4, 44u, 55u, 66u, 255u);
    view = rgba_view(pixels, 4, 4);
    fixture_init(&f, 8, 8);
    CHECK(create_client_surface_buffer(&f, &view, &client, &surface, &buffer) ==
              MOO_COMP_OK, "damage cap objects");
    CHECK(commit_buffer(&f, client, surface, buffer, 1u, 255u, 4, 4, 0u) ==
              MOO_COMP_OK, "damage cap initial");
    build_present(&f, 70u);
    for (i = 0u; i < MOO_COMP_SURFACE_DAMAGE_CAPACITY + 1u; ++i) {
        MooCompRect rect = {(int32_t)(i % 4u), (int32_t)((i / 4u) % 4u),
                            1, 1};
        CHECK(moo_comp_surface_damage(&f.core, client, surface, rect) ==
                  MOO_COMP_OK, "surface damage cap accepts conservatively");
    }
    CHECK(f.surfaces[handle_slot(surface)].pending.damage_count == 1u &&
          f.surfaces[handle_slot(surface)].pending.damage[0].width == INT32_MAX,
          "surface damage cap collapses to conservative full");
    CHECK(moo_comp_surface_commit(&f.core, client, surface) == MOO_COMP_OK,
          "collapsed damage commits");
    for (i = 0u; i < MOO_COMP_OUTPUT_DAMAGE_CAPACITY + 8u; ++i) {
        CHECK(moo_comp_surface_set_position(&f.core, surface,
                                            (int32_t)(i % 5u),
                                            (int32_t)((i * 3u) % 5u)) ==
                  MOO_COMP_OK, "output damage pressure move");
        CHECK(moo_comp_damage_count(&f.core) <= MOO_COMP_OUTPUT_DAMAGE_CAPACITY,
              "output damage count never exceeds cap");
    }
    verify_partial_present(&f, 71u);
    CHECK(moo_comp_surface_damage(&f.core, client, surface,
                                  (MooCompRect){0, 0, 1, 1}) == MOO_COMP_OK,
          "damage works after collapse");
    CHECK(moo_comp_surface_commit(&f.core, client, surface) == MOO_COMP_OK,
          "commit works after collapse");
    verify_partial_present(&f, 72u);
    fixture_guards(&f);
}

static void setup_two_client_quota_fixture(Fixture *f,
                                           MooCompHandle clients[2u],
                                           MooCompHandle surfaces[2u],
                                           MooCompHandle buffers[2u],
                                           uint8_t pixels[2u][4u]) {
    uint32_t i;
    fixture_init_caps(f, 2, 2, 2u, 4u, 4u, 8u, 8u);
    for (i = 0u; i < 2u; ++i) {
        MooCompBufferView view;
        pixels[i][0] = i == 0u ? 255u : 0u;
        pixels[i][1] = i == 1u ? 255u : 0u;
        pixels[i][2] = 0u;
        pixels[i][3] = 255u;
        view = rgba_view(pixels[i], 1, 1);
        CHECK(create_client_surface_buffer(f, &view, &clients[i],
                                           &surfaces[i], &buffers[i]) ==
                  MOO_COMP_OK, "quota client objects");
        CHECK(moo_comp_surface_set_position(&f->core, surfaces[i],
                                            (int32_t)i, 0) == MOO_COMP_OK,
              "quota initial position");
        CHECK(commit_buffer(f, clients[i], surfaces[i], buffers[i],
                            1u, 255u, 1, 1, 0u) == MOO_COMP_OK,
              "quota initial commit");
    }
    build_present(f, 80u);
}

static void test_frame_event_quota_fairness_recovery(void) {
    {
        Fixture f;
        uint8_t pixels[2u][4u];
        MooCompHandle clients[2u], surfaces[2u], buffers[2u];
        MooCompEvent event;
        StateSnapshot snapshot;
        uint64_t frame_id = 0u;
        uint32_t i;
        setup_two_client_quota_fixture(&f, clients, surfaces, buffers, pixels);
        for (i = 0u; i < 4u; ++i) {
            CHECK(moo_comp_surface_frame(&f.core, clients[0], surfaces[0],
                                         2000u + i) == MOO_COMP_OK,
                  "A frame quota reservation");
            CHECK(moo_comp_surface_commit(&f.core, clients[0], surfaces[0]) ==
                      MOO_COMP_OK, "A frame quota commit");
        }
        state_snapshot_take(&f, &snapshot);
        CHECK(moo_comp_surface_frame(&f.core, clients[0], surfaces[0], 2004u) ==
                  MOO_COMP_LIMIT, "A frame quota plus one");
        CHECK(state_snapshot_equal(&f, &snapshot),
              "frame quota reject bitwise no mutation");
        CHECK(moo_comp_surface_frame(&f.core, clients[1], surfaces[1], 2100u) ==
                  MOO_COMP_OK, "B frame reservation survives A saturation");
        CHECK(moo_comp_surface_commit(&f.core, clients[1], surfaces[1]) ==
                  MOO_COMP_OK, "B frame commit survives A saturation");
        CHECK(moo_comp_build_frame(&f.core, &f.output, &frame_id) == MOO_COMP_OK,
              "quota frame build");
        CHECK(moo_comp_present_done(&f.core, frame_id, 81u, 81000u) ==
                  MOO_COMP_OK, "quota frame present");
        for (i = 0u; i < 3u; ++i)
            pop_frame_event(&f, clients[0], 2000u + i,
                            MOO_COMP_FRAME_SUPERSEDED, 81u);
        pop_frame_event(&f, clients[0], 2003u,
                        MOO_COMP_FRAME_PRESENTED, 81u);
        CHECK(moo_comp_next_event(&f.core, clients[0], &event) ==
                  MOO_COMP_WOULD_BLOCK, "A frame quota exact event count");
        pop_frame_event(&f, clients[1], 2100u,
                        MOO_COMP_FRAME_PRESENTED, 81u);
        CHECK(moo_comp_surface_frame(&f.core, clients[0], surfaces[0], 2005u) ==
                  MOO_COMP_OK, "A frame quota recovers after present/drain");
        CHECK(moo_comp_surface_commit(&f.core, clients[0], surfaces[0]) ==
                  MOO_COMP_OK, "A recovered frame commit");
        build_present(&f, 82u);
        expect_frame_event(&f, clients[0], 2005u,
                           MOO_COMP_FRAME_PRESENTED, 82u);
        fixture_guards(&f);
    }
    {
        Fixture f;
        uint8_t pixels[2u][4u];
        MooCompHandle clients[2u], surfaces[2u], buffers[2u];
        MooCompEvent event;
        StateSnapshot snapshot;
        uint32_t i;
        setup_two_client_quota_fixture(&f, clients, surfaces, buffers, pixels);
        for (i = 0u; i < 4u; ++i) {
            CHECK(moo_comp_surface_frame(&f.core, clients[0], surfaces[0],
                                         3000u + i) == MOO_COMP_OK,
                  "A event quota token");
            CHECK(moo_comp_surface_commit(&f.core, clients[0], surfaces[0]) ==
                      MOO_COMP_OK, "A event quota commit");
            build_present(&f, 90u + i);
        }
        state_snapshot_take(&f, &snapshot);
        CHECK(moo_comp_surface_frame(&f.core, clients[0], surfaces[0], 3004u) ==
                  MOO_COMP_WOULD_BLOCK, "A combined event quota plus one");
        CHECK(state_snapshot_equal(&f, &snapshot),
              "event quota reject bitwise no mutation");
        CHECK(moo_comp_surface_frame(&f.core, clients[1], surfaces[1], 3100u) ==
                  MOO_COMP_OK, "B event reservation survives A saturation");
        CHECK(moo_comp_surface_commit(&f.core, clients[1], surfaces[1]) ==
                  MOO_COMP_OK, "B event commit survives A saturation");
        build_present(&f, 94u);
        expect_frame_event(&f, clients[1], 3100u,
                           MOO_COMP_FRAME_PRESENTED, 94u);
        CHECK(moo_comp_next_event(&f.core, clients[0], &event) == MOO_COMP_OK &&
              event.token == 3000u, "drain one A event for recovery");
        CHECK(moo_comp_surface_frame(&f.core, clients[0], surfaces[0], 3004u) ==
                  MOO_COMP_OK, "A event quota recovers after one drain");
        CHECK(moo_comp_surface_commit(&f.core, clients[0], surfaces[0]) ==
                  MOO_COMP_OK, "A recovered event commit");
        build_present(&f, 95u);
        for (i = 1u; i < 4u; ++i)
            pop_frame_event(&f, clients[0], 3000u + i,
                            MOO_COMP_FRAME_PRESENTED, 90u + i);
        pop_frame_event(&f, clients[0], 3004u,
                        MOO_COMP_FRAME_PRESENTED, 95u);
        CHECK(moo_comp_next_event(&f.core, clients[0], &event) ==
                  MOO_COMP_WOULD_BLOCK, "event quota recovery exact queue");
        fixture_guards(&f);
    }
}

static void test_state_hash_semantic_coverage(void) {
    Fixture a;
    Fixture b;
    uint8_t pa[4u] = {1u, 2u, 3u, 255u};
    uint8_t pb[4u] = {1u, 2u, 3u, 255u};
    MooCompBufferView va = rgba_view(pa, 1, 1);
    MooCompBufferView vb = rgba_view(pb, 1, 1);
    MooCompHandle ca, cb, sa, sb, ba, bb;
    uint64_t equal_a;
    uint64_t equal_b;
    fixture_init(&a, 2, 2);
    fixture_init(&b, 2, 2);
    CHECK(create_client_surface_buffer(&a, &va, &ca, &sa, &ba) == MOO_COMP_OK,
          "hash replica A");
    CHECK(create_client_surface_buffer(&b, &vb, &cb, &sb, &bb) == MOO_COMP_OK,
          "hash replica B");
    equal_a = moo_comp_state_hash(&a.core);
    equal_b = moo_comp_state_hash(&b.core);
    CHECK(equal_a == equal_b, "state hash excludes pointer identity");
    CHECK(moo_comp_surface_damage(&a.core, ca, sa,
                                  (MooCompRect){0, 0, 1, 1}) == MOO_COMP_OK,
          "hash damage A");
    CHECK(moo_comp_surface_damage(&b.core, cb, sb,
                                  (MooCompRect){1, 0, 1, 1}) == MOO_COMP_OK,
          "hash damage B");
    CHECK(moo_comp_state_hash(&a.core) != moo_comp_state_hash(&b.core),
          "state hash includes damage rectangle content");
    CHECK(moo_comp_surface_frame(&a.core, ca, sa, 4001u) == MOO_COMP_OK,
          "hash pending token A");
    CHECK(moo_comp_surface_frame(&b.core, cb, sb, 4002u) == MOO_COMP_OK,
          "hash pending token B");
    CHECK(moo_comp_state_hash(&a.core) != moo_comp_state_hash(&b.core),
          "state hash includes pending frame token");
    CHECK(moo_comp_pointer_position(&a.core, 0, 0) == MOO_COMP_OK,
          "hash cursor A");
    CHECK(moo_comp_pointer_position(&b.core, 1, 0) == MOO_COMP_OK,
          "hash cursor B");
    CHECK(moo_comp_state_hash(&a.core) != moo_comp_state_hash(&b.core),
          "state hash includes cursor geometry");
    fixture_guards(&a);
    fixture_guards(&b);
}

static void test_unsupported_clipboard_dnd_no_mutation(void) {
    static const uint32_t unsupported[] = {
        MOO_COMP_REQUEST_CLIPBOARD_SET,
        MOO_COMP_REQUEST_CLIPBOARD_OFFER,
        MOO_COMP_REQUEST_CLIPBOARD_RECEIVE,
        MOO_COMP_REQUEST_DND_BEGIN,
        MOO_COMP_REQUEST_DND_ACCEPT,
        MOO_COMP_REQUEST_DND_DROP,
        MOO_COMP_REQUEST_DND_CANCEL
    };
    Fixture f;
    MooCompHandle client;
    MooCompRequest request;
    uint64_t before;
    uint32_t damage_before;
    size_t i;

    fixture_init(&f, 2, 2);
    CHECK((MOO_COMP_FEATURES_V1 &
          (MOO_COMP_FEATURE_CLIPBOARD | MOO_COMP_FEATURE_DRAG_AND_DROP)) == 0u,
          "unsupported features not advertised");
    CHECK(moo_comp_client_create(&f.core, &client) == MOO_COMP_OK,
          "stub client");
    for (i = 0u; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        memset(&request, 0, sizeof(request));
        request.header.version = MOO_COMP_PROTOCOL_VERSION;
        request.header.opcode = unsupported[i];
        request.header.byte_length = (uint32_t)sizeof(request);
        before = moo_comp_state_hash(&f.core);
        damage_before = moo_comp_damage_count(&f.core);
        CHECK(moo_comp_dispatch_stub(&f.core, client, &request) ==
                  MOO_COMP_UNSUPPORTED, "clipboard/dnd strict unsupported");
        CHECK(moo_comp_state_hash(&f.core) == before &&
              moo_comp_damage_count(&f.core) == damage_before,
              "unsupported request no mutation");
    }
    fixture_guards(&f);
}

int main(void) {
    test_independent_alpha_matrix();
    test_three_clients_alpha_z_and_callbacks();
    test_scale_resize_and_atomic_rejects();
    test_owner_and_stale_reuse();
    test_occluded_callback_exactly_once();
    test_cursor_focus_topmost_disconnect();
    test_buffer_busy_release_and_pending();
    test_buffer_release_queue_backpressure_recovery();
    test_callback_statuses_and_duplicate_token();
    test_handle_wrap_wrong_kind_and_capacities();
    test_damage_capacity_and_recovery();
    test_frame_event_quota_fairness_recovery();
    test_state_hash_semantic_coverage();
    test_seeded_differential_damage();
    test_unsupported_clipboard_dnd_no_mutation();
    if (g_failures != 0) {
        fprintf(stderr, "P016-O3 COMPOSITOR FAIL: %d/%d checks failed\n",
                g_failures, g_checks);
        return 1;
    }
    printf("P016-O3 COMPOSITOR OK: %d checks\n", g_checks);
    return 0;
}
