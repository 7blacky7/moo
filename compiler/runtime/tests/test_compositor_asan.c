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
#include "../moo_ui_host_parity_instrumentation.h"
#include "../moo_ui_host_parity_instrumentation_internal.h"

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
    uint32_t count;
    uint64_t generation[4];
    uint64_t frame_id[4];
    uint64_t present_sequence[4];
    uint64_t timestamp_ns[4];
} PresentObserverCapture;

static void capture_present_done(void *user, uint64_t generation,
                                 uint64_t frame_id,
                                 uint64_t present_sequence,
                                 uint64_t timestamp_ns) {
    PresentObserverCapture *capture = (PresentObserverCapture *)user;
    uint32_t index = capture->count;
    if (index < 4u) {
        capture->generation[index] = generation;
        capture->frame_id[index] = frame_id;
        capture->present_sequence[index] = present_sequence;
        capture->timestamp_ns[index] = timestamp_ns;
    }
    capture->count++;
}

static void fill_guard(uint8_t *bytes, size_t count) {
    memset(bytes, 0xa5, count);
}

static int guard_is(const uint8_t *bytes, size_t count) {
    size_t i;
    for (i = 0u; i < count; ++i)
        if (bytes[i] != 0xa5u) return 0;
    return 1;
}

static void fixture_init(Fixture *f, int32_t width, int32_t height) {
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
                           f->clients, CLIENT_CAP,
                           f->surfaces, SURFACE_CAP,
                           f->buffers, BUFFER_CAP,
                           f->frames, FRAME_CAP,
                           f->events, EVENT_CAP);
    CHECK(result == MOO_COMP_OK, "compositor init");
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

static void full_reference(const Fixture *f, uint8_t *pixels) {
    MooCompOutput output = f->output;
    MooCompRect full = {0, 0, output.width, output.height};
    uint8_t used[SURFACE_CAP];
    uint32_t emitted;
    output.pixels = pixels;
    memset(used, 0, sizeof(used));
    CHECK(moo_comp_raster_clear(&output, full,
                                f->core.config.background_r,
                                f->core.config.background_g,
                                f->core.config.background_b,
                                f->core.config.background_a) == MOO_COMP_OK,
          "reference clear");
    for (emitted = 0u; emitted < SURFACE_CAP; ++emitted) {
        uint32_t i;
        uint32_t best = UINT32_MAX;
        for (i = 0u; i < SURFACE_CAP; ++i) {
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
                CHECK(moo_comp_raster_blit(
                          &output, &f->buffers[buffer_index].view,
                          surface->x, surface->y,
                          surface->committed.scale,
                          surface->committed.opacity, full) == MOO_COMP_OK,
                      "reference blit");
            }
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
    before = moo_comp_state_hash(&f.core);
    damage_before = moo_comp_damage_count(&f.core);
    CHECK(moo_comp_surface_commit(&f.core, c1, surface) ==
              MOO_COMP_BAD_BUFFER, "incompatible scale commit rejected");
    CHECK(moo_comp_state_hash(&f.core) == before &&
          moo_comp_damage_count(&f.core) == damage_before,
          "invalid commit atomic");

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
    uint8_t red[4u] = {255u, 0u, 0u, 255u};
    uint8_t green[4u] = {0u, 255u, 0u, 255u};
    MooCompBufferView red_view = rgba_view(red, 1, 1);
    MooCompBufferView green_view = rgba_view(green, 1, 1);
    MooCompHandle ca, cb, sa, sb, ba, bb;
    MooCompRect one = {0, 0, 1, 1};
    MooCompEvent event;
    uint64_t frame_id = 0u;

    fixture_init(&f, 2, 2);
    CHECK(create_client_surface_buffer(&f, &red_view, &ca, &sa, &ba) ==
              MOO_COMP_OK, "occluded lower");
    CHECK(create_client_surface_buffer(&f, &green_view, &cb, &sb, &bb) ==
              MOO_COMP_OK, "occluding upper");
    CHECK(moo_comp_surface_set_position(&f.core, sa, -2, -2) == MOO_COMP_OK,
          "lower fully occluded offscreen");
    CHECK(commit_buffer(&f, ca, sa, ba, 1u, 255u, 1, 1, 0u) ==
              MOO_COMP_OK, "lower initial");
    CHECK(commit_buffer(&f, cb, sb, bb, 1u, 255u, 1, 1, 0u) ==
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

static uint64_t instrumentation_present(Fixture *f,
                                        uint64_t present_sequence,
                                        uint64_t timestamp_ns) {
    uint64_t frame_id = 0u;
    CHECK(moo_comp_build_frame(&f->core, &f->output, &frame_id) ==
              MOO_COMP_OK && frame_id != 0u,
          "instrumentation actual frame build");
    CHECK(moo_comp_present_done(&f->core, frame_id, present_sequence,
                                timestamp_ns) == MOO_COMP_OK,
          "instrumentation actual present done");
    return frame_id;
}

static void instrumentation_add_valid_helper_events(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    uint32_t wakeups, uint64_t expected_hash, uint64_t recovered_hash) {
    uint32_t index;
    CHECK(moo_ui_host_parity_helper_bind(instrumentation, generation) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "helper binds fixed generation");
    CHECK(moo_ui_host_parity_helper_begin_reduced_idle(
              instrumentation, generation, UINT64_C(1000000000)) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "helper begins reduced idle");
    for (index = 0u; index < wakeups; ++index)
        CHECK(moo_ui_host_parity_helper_record_wakeup(
                  instrumentation, generation,
                  UINT64_C(1100000000) + (uint64_t)index *
                      UINT64_C(100000000)) ==
                  MOO_UI_HOST_PARITY_RESULT_OK,
              "helper records monotonic wakeup");
    CHECK(moo_ui_host_parity_helper_end_reduced_idle(
              instrumentation, generation, UINT64_C(2000000000)) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "helper ends one-second reduced idle");
    CHECK(moo_ui_host_parity_helper_record_crash(
              instrumentation, generation, expected_hash) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "helper records injected child crash");
    CHECK(moo_ui_host_parity_helper_record_restart(
              instrumentation, generation, recovered_hash) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "helper records child restart state");
}

static int measurement_payload_zero(
    const MooUiHostParityMeasurement *measurement) {
    return measurement->sample_count == 0u &&
        measurement->value_a == 0u && measurement->value_b == 0u &&
        measurement->value_c == 0u && measurement->native_error == 0;
}

static void test_parity_clipboard_storage_fail_closed(void) {
    MooUiHostParityInstrumentation instrumentation;
    MooUiHostParityMeasurement measurement;

    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 91u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 91u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "clipboard storage fixture");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 4u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED &&
          measurement_payload_zero(&measurement),
          "unsealed clipboard exact zero unsupported");
    CHECK(moo_ui_host_parity_helper_record_clipboard(
              &instrumentation, 91u, 1u, 1u, 0u) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_seal_clipboard(
              &instrumentation, 91u) == MOO_UI_HOST_PARITY_RESULT_OK,
          "clipboard exact producer metrics seal independently");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 4u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
          measurement.sample_count == 2u &&
          measurement.value_a == 1u && measurement.value_b == 1u &&
          measurement.value_c == 0u && measurement.native_error == 0,
          "sealed clipboard exact pass metrics");
    CHECK(moo_ui_host_parity_helper_record_clipboard(
              &instrumentation, 91u, 1u, 1u, 0u) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.clipboard_invalid == 1u,
          "clipboard event after seal invalidates");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 4u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL &&
          measurement_payload_zero(&measurement),
          "invalid clipboard exact zero fail");

    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 92u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 92u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "clipboard missing-record fixture");
    CHECK(moo_ui_host_parity_helper_seal_clipboard(
              &instrumentation, 92u) == MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.clipboard_invalid == 1u,
          "clipboard cannot seal without producer record");

    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 93u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 93u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "clipboard mismatch fixture");
    CHECK(moo_ui_host_parity_helper_record_clipboard(
              &instrumentation, 93u, 1u, 1u, 1u) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_seal_clipboard(
              &instrumentation, 93u) == MOO_UI_HOST_PARITY_RESULT_OK,
          "clipboard measured mismatch seals as evidence");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 4u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL &&
          measurement.sample_count == 2u &&
          measurement.value_a == 1u && measurement.value_b == 1u &&
          measurement.value_c == 1u && measurement.native_error == 0,
          "clipboard integrity mismatch fails with exact metrics");

    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 94u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 94u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "clipboard duplicate fixture");
    CHECK(moo_ui_host_parity_helper_record_clipboard(
              &instrumentation, 94u, 1u, 1u, 0u) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_record_clipboard(
              &instrumentation, 94u, 1u, 1u, 0u) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.clipboard_recorded == 1u &&
          instrumentation.clipboard_invalid == 1u,
          "clipboard duplicate record rejected");

    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 95u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 95u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "clipboard empty fixture");
    CHECK(moo_ui_host_parity_helper_record_clipboard(
              &instrumentation, 95u, 0u, 0u, 0u) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.clipboard_recorded == 0u &&
          instrumentation.clipboard_invalid == 1u,
          "clipboard empty evidence rejected");

    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 96u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 96u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "clipboard bounds fixture");
    CHECK(moo_ui_host_parity_helper_record_clipboard(
              &instrumentation, 96u, 2u, 1u, 0u) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.clipboard_invalid == 1u,
          "clipboard out-of-range metrics rejected");

    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 97u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 97u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "clipboard generation fixture");
    CHECK(moo_ui_host_parity_helper_record_clipboard(
              &instrumentation, 98u, 1u, 1u, 0u) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.clipboard_invalid == 1u,
          "clipboard mixed generation rejected");
}

static void test_parity_devtools_storage_fail_closed(void) {
    Fixture f;
    MooUiHostParityInstrumentation instrumentation;
    MooUiHostParityMeasurement measurement;
    uint32_t index;

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 81u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 81u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "devtools storage fixture");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 7u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED &&
          measurement_payload_zero(&measurement),
          "unsealed devtools exact zero unsupported");
    CHECK(moo_ui_host_parity_helper_record_devtools_trace(
              &instrumentation, 81u, UINT64_C(0x300000001),
              UINT64_C(1), UINT64_C(0xabc), 0u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "devtools bounded trace record");
    CHECK(moo_ui_host_parity_helper_seal_devtools(
              &instrumentation, 81u) == MOO_UI_HOST_PARITY_RESULT_OK,
          "devtools independent seal");
    memset(&measurement, 0, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 7u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
          measurement.sample_count == 1u &&
          measurement.value_a == 1u && measurement.value_b == 1u &&
          measurement.value_c == 0u,
          "devtools sealed exact metrics");
    CHECK(moo_ui_host_parity_helper_record_devtools_trace(
              &instrumentation, 81u, UINT64_C(0x300000001),
              UINT64_C(2), UINT64_C(0xdef), 0u) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.devtools_invalid == 1u,
          "devtools event after seal invalidates");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 7u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL &&
          measurement_payload_zero(&measurement),
          "invalid devtools exact zero fail");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 82u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 82u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "devtools privacy fixture");
    CHECK(moo_ui_host_parity_helper_record_devtools_trace(
              &instrumentation, 82u, UINT64_C(1), UINT64_C(1),
              UINT64_C(1), 1u) == MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.devtools_invalid == 1u &&
          instrumentation.devtools_privacy_leaks == 1u,
          "privacy leak rejects trace and records failure");
    CHECK(moo_ui_host_parity_helper_seal_devtools(
              &instrumentation, 82u) == MOO_UI_HOST_PARITY_RESULT_INVALID,
          "privacy leak cannot seal");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 83u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 83u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "devtools overflow fixture");
    for (index = 0u;
         index < MOO_UI_HOST_PARITY_DEVTOOLS_TRACE_CAPACITY; ++index)
        CHECK(moo_ui_host_parity_helper_record_devtools_trace(
                  &instrumentation, 83u, (uint64_t)index + 1u,
                  (uint64_t)index + 1u, (uint64_t)index + 100u, 0u) ==
                  MOO_UI_HOST_PARITY_RESULT_OK,
              "devtools capacity record");
    CHECK(moo_ui_host_parity_helper_record_devtools_trace(
              &instrumentation, 83u, UINT64_C(99), UINT64_C(99),
              UINT64_C(99), 0u) == MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.devtools_trace_count ==
              MOO_UI_HOST_PARITY_DEVTOOLS_TRACE_CAPACITY &&
          instrumentation.devtools_invalid == 1u,
          "devtools fixed-storage overflow rejected");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 84u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 84u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "devtools generation fixture");
    CHECK(moo_ui_host_parity_helper_record_devtools_trace(
              &instrumentation, 85u, UINT64_C(1), UINT64_C(1),
              UINT64_C(1), 0u) == MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.devtools_invalid == 1u,
          "devtools mixed generation rejected");
}

static void test_parity_helper_probe_fail_closed(void) {
    Fixture f;
    MooUiHostParityInstrumentation instrumentation;
    MooUiHostParityMeasurement measurement;
    uint32_t index;

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 70u) == MOO_UI_HOST_PARITY_RESULT_OK,
          "unsealed probe fixture");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 5u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED &&
          measurement_payload_zero(&measurement),
          "unsealed probe is exact zero unsupported");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 71u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_instrumentation_bind_presenter(
              &instrumentation, &f.core) == MOO_UI_HOST_PARITY_RESULT_OK,
          "valid helper probe presenter");
    for (index = 0u; index < MOO_UI_HOST_PARITY_MIN_FRAME_SAMPLES; ++index)
        instrumentation_present(
            &f, (uint64_t)index + 1u,
            ((uint64_t)index + 1u) * UINT64_C(16667000));
    instrumentation_add_valid_helper_events(
        &instrumentation, 71u, 1u, UINT64_C(0xabc), UINT64_C(0xabc));
    CHECK(moo_ui_host_parity_instrumentation_seal(&instrumentation) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "valid helper probe seals");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 5u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
          measurement.sample_count == 2u &&
          measurement.value_a == MOO_UI_HOST_PARITY_MIN_FRAME_SAMPLES &&
          measurement.value_b == 16667u && measurement.value_c == 1u,
          "animation probe exact sealed helper metrics");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 6u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
          measurement.sample_count == 1u && measurement.value_a == 1u &&
          measurement.value_b == 1u && measurement.value_c == 0u,
          "crash probe exact restart integrity metrics");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 7u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED &&
          measurement_payload_zero(&measurement),
          "devtools remains exact zero unsupported");

    CHECK(moo_ui_host_parity_helper_record_wakeup(
              &instrumentation, 71u, UINT64_C(2100000000)) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.invalid == 1u,
          "helper event after seal invalidates generation");
    memset(&measurement, 0xa5, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 6u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL &&
          measurement_payload_zero(&measurement),
          "invalid sealed probe is exact zero fail");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 72u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 72u) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_begin_reduced_idle(
              &instrumentation, 72u, 100u) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_record_wakeup(
              &instrumentation, 72u, 200u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "duplicate wakeup fixture");
    CHECK(moo_ui_host_parity_helper_record_wakeup(
              &instrumentation, 72u, 200u) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.invalid == 1u,
          "duplicate wakeup timestamp rejected");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 73u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_instrumentation_bind_presenter(
              &instrumentation, &f.core) == MOO_UI_HOST_PARITY_RESULT_OK,
          "corruption probe presenter");
    instrumentation_add_valid_helper_events(
        &instrumentation, 73u, 0u, UINT64_C(0x111), UINT64_C(0x222));
    CHECK(moo_ui_host_parity_instrumentation_seal(&instrumentation) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "corrupt restart still seals evidence");
    memset(&measurement, 0, sizeof(measurement));
    CHECK(moo_ui_host_parity_instrumentation_probe(
              &instrumentation, 6u, &measurement) ==
              MOO_UI_HOST_PARITY_RESULT_OK &&
          measurement.evidence ==
              (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL &&
          measurement.value_a == 1u && measurement.value_b == 1u &&
          measurement.value_c == 1u,
          "crash corruption fails with exact metric");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 74u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_helper_bind(&instrumentation, 74u) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "restart-before-crash fixture");
    CHECK(moo_ui_host_parity_helper_record_restart(
              &instrumentation, 74u, UINT64_C(1)) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID &&
          instrumentation.invalid == 1u,
          "restart before injected crash rejected");
}

static void test_parity_instrumentation_sealed_provenance(void) {
    Fixture f;
    MooUiHostParityInstrumentation instrumentation;
    MooUiHostParityFrameMetrics metrics;
    uint64_t last_frame = 0u;
    uint32_t index;

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 55u) == MOO_UI_HOST_PARITY_RESULT_OK,
          "instrumentation init");
    CHECK(moo_ui_host_parity_instrumentation_bind_presenter(
              &instrumentation, &f.core) == MOO_UI_HOST_PARITY_RESULT_OK,
          "instrumentation binds actual presenter");
    for (index = 0u; index < MOO_UI_HOST_PARITY_MIN_FRAME_SAMPLES; ++index)
        last_frame = instrumentation_present(
            &f, (uint64_t)index + 1u,
            ((uint64_t)index + 1u) * UINT64_C(16667000));
    instrumentation_add_valid_helper_events(
        &instrumentation, 55u, 0u, UINT64_C(0x55), UINT64_C(0x55));
    CHECK(moo_ui_host_parity_instrumentation_seal(&instrumentation) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "instrumentation seals immutable window");
    memset(&metrics, 0, sizeof(metrics));
    CHECK(moo_ui_host_parity_instrumentation_frame_metrics(
              &instrumentation, &metrics) == MOO_UI_HOST_PARITY_RESULT_OK,
          "instrumentation sealed metrics");
    CHECK(metrics.frame_count == MOO_UI_HOST_PARITY_MIN_FRAME_SAMPLES &&
          metrics.generation == 55u && metrics.p99_frame_us == 16667u,
          "instrumentation provenance metadata and p99 exact");
    CHECK(instrumentation.frames[instrumentation.frame_count - 1u].frame_id ==
              last_frame,
          "instrumentation last actual frame id preserved");

    instrumentation_present(&f, 121u, UINT64_C(121) * UINT64_C(16667000));
    CHECK(instrumentation.invalid == 1u,
          "instrumentation rejects event after seal");
    CHECK(moo_ui_host_parity_instrumentation_frame_metrics(
              &instrumentation, &metrics) == MOO_UI_HOST_PARITY_RESULT_INVALID,
          "post-seal event invalidates metrics");
    fixture_guards(&f);

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 56u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_instrumentation_bind_presenter(
              &instrumentation, &f.core) == MOO_UI_HOST_PARITY_RESULT_OK,
          "backwards sequence fixture");
    instrumentation_present(&f, 2u, 2000u);
    instrumentation_present(&f, 1u, 3000u);
    CHECK(instrumentation.invalid == 1u && instrumentation.frame_count == 1u,
          "instrumentation rejects backwards sequence");
    CHECK(moo_ui_host_parity_instrumentation_seal(&instrumentation) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID,
          "invalid sequence cannot seal");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 57u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_instrumentation_bind_presenter(
              &instrumentation, &f.core) == MOO_UI_HOST_PARITY_RESULT_OK,
          "backwards timestamp fixture");
    instrumentation_present(&f, 1u, 3000u);
    instrumentation_present(&f, 2u, 2000u);
    CHECK(instrumentation.invalid == 1u && instrumentation.frame_count == 1u,
          "instrumentation rejects backwards timestamp");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 58u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_instrumentation_bind_presenter(
              &instrumentation, &f.core) == MOO_UI_HOST_PARITY_RESULT_OK,
          "mixed generation fixture");
    instrumentation.generation = 59u;
    instrumentation_present(&f, 1u, 1000u);
    CHECK(instrumentation.invalid == 1u && instrumentation.frame_count == 0u,
          "instrumentation rejects mixed generation provenance");

    fixture_init(&f, 2, 2);
    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 60u) == MOO_UI_HOST_PARITY_RESULT_OK &&
          moo_ui_host_parity_instrumentation_bind_presenter(
              &instrumentation, &f.core) == MOO_UI_HOST_PARITY_RESULT_OK,
          "overflow fixture");
    for (index = 0u; index <= MOO_UI_HOST_PARITY_FRAME_CAPACITY; ++index)
        instrumentation_present(&f, (uint64_t)index + 1u,
                                ((uint64_t)index + 1u) * UINT64_C(1000));
    CHECK(instrumentation.invalid == 1u &&
          instrumentation.frame_count == MOO_UI_HOST_PARITY_FRAME_CAPACITY,
          "instrumentation rejects fixed-storage overflow");
}

static void test_present_done_observer_provenance(void) {
    Fixture f;
    PresentObserverCapture capture;
    MooCompBufferView view;
    MooCompHandle client, surface, buffer;
    uint64_t first_frame = 0u;
    uint64_t second_frame = 0u;
    uint8_t pixels[2u * 2u * 4u];

    fixture_init(&f, 2, 2);
    memset(&capture, 0, sizeof(capture));
    fill_solid(pixels, 2, 2, 10u, 20u, 30u, 255u);
    view = rgba_view(pixels, 2, 2);
    CHECK(create_client_surface_buffer(&f, &view, &client, &surface,
                                       &buffer) == MOO_COMP_OK,
          "observer fixture objects");
    CHECK(moo_comp_present_done_observer_bind(&f.core, 0u,
                                               capture_present_done,
                                               &capture) == MOO_COMP_INVALID,
          "observer zero generation rejected");
    CHECK(moo_comp_present_done_observer_bind(&f.core, 77u,
                                               capture_present_done,
                                               &capture) == MOO_COMP_OK,
          "observer bind fixed generation");
    CHECK(moo_comp_present_done_observer_bind(&f.core, 78u,
                                               capture_present_done,
                                               &capture) == MOO_COMP_BAD_STATE,
          "observer mixed generation rebind rejected");

    CHECK(commit_buffer(&f, client, surface, buffer, 1u, 255u,
                        2, 2, 101u) == MOO_COMP_OK,
          "observer first commit");
    CHECK(moo_comp_build_frame(&f.core, &f.output, &first_frame) ==
              MOO_COMP_OK && first_frame != 0u,
          "observer first frame");
    CHECK(moo_comp_present_done(&f.core, first_frame + 1u, 9u, 9000u) ==
              MOO_COMP_BAD_STATE && capture.count == 0u,
          "observer only after successful transition");
    CHECK(moo_comp_present_done(&f.core, first_frame, 9u, 9000u) ==
              MOO_COMP_OK && capture.count == 1u,
          "observer exactly once after success");
    CHECK(capture.generation[0] == 77u &&
          capture.frame_id[0] == first_frame &&
          capture.present_sequence[0] == 9u &&
          capture.timestamp_ns[0] == 9000u,
          "observer first provenance exact");
    CHECK(moo_comp_present_done(&f.core, first_frame, 9u, 9000u) ==
              MOO_COMP_BAD_STATE && capture.count == 1u,
          "observer duplicate completion rejected");

    CHECK(commit_buffer(&f, client, surface, buffer, 1u, 255u,
                        2, 2, 102u) == MOO_COMP_OK,
          "observer second commit");
    CHECK(moo_comp_build_frame(&f.core, &f.output, &second_frame) ==
              MOO_COMP_OK && second_frame > first_frame,
          "observer second frame");
    CHECK(moo_comp_present_done(&f.core, second_frame, 8u, 8000u) ==
              MOO_COMP_OK && capture.count == 2u,
          "observer forwards actual out-of-order presenter data");
    CHECK(capture.generation[1] == 77u &&
          capture.frame_id[1] == second_frame &&
          capture.present_sequence[1] == 8u &&
          capture.timestamp_ns[1] == 8000u,
          "observer preserves out-of-order provenance for gate rejection");
    fixture_guards(&f);
}

int main(void) {
    test_parity_clipboard_storage_fail_closed();
    test_parity_devtools_storage_fail_closed();
    test_parity_helper_probe_fail_closed();
    test_parity_instrumentation_sealed_provenance();
    test_present_done_observer_provenance();
    test_three_clients_alpha_z_and_callbacks();
    test_scale_resize_and_atomic_rejects();
    test_owner_and_stale_reuse();
    test_occluded_callback_exactly_once();
    test_cursor_focus_topmost_disconnect();
    test_unsupported_clipboard_dnd_no_mutation();
    if (g_failures != 0) {
        fprintf(stderr, "P016-O3 COMPOSITOR FAIL: %d/%d checks failed\n",
                g_failures, g_checks);
        return 1;
    }
    printf("P016-O3 COMPOSITOR OK: %d checks\n", g_checks);
    return 0;
}
