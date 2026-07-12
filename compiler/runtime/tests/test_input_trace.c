#include "../moo_input_core.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STEPS 20000u

typedef struct {
    MooInputCore core;
    MooInputClientSlot clients[2];
    MooInputTargetSlot targets[4];
    MooInputEventSlot events[64];
    MooA11yNodeSlot nodes[4];
    MooInputHandle client;
    MooInputHandle target[2];
} Twin;

static uint32_t rng_state = UINT32_C(0x6d6f6f34);

static uint32_t random_u32(void) {
    uint32_t x = rng_state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    rng_state = x;
    return x;
}

static int setup(Twin *t) {
    MooInputConfig config;
    memset(t, 0, sizeof(*t));
    config.max_events_per_client = 48u;
    config.max_a11y_depth = 4u;
    config.features = MOO_INPUT_FEATURE_POINTER | MOO_INPUT_FEATURE_TOUCH |
        MOO_INPUT_FEATURE_STYLUS | MOO_INPUT_FEATURE_KEYBOARD |
        MOO_INPUT_FEATURE_IME | MOO_INPUT_FEATURE_HIGH_CONTRAST |
        MOO_INPUT_FEATURE_REDUCED_MOTION;
    if (moo_input_init(&t->core, &config, t->clients, 2u, t->targets, 4u,
        t->events, 64u, t->nodes, 4u) != MOO_INPUT_OK) return 0;
    if (moo_input_client_create(&t->core, 0u, &t->client) != MOO_INPUT_OK)
        return 0;
    if (moo_input_target_create_trusted(&t->core, t->client, 1u, 1u,
        &t->target[0]) != MOO_INPUT_OK) return 0;
    if (moo_input_target_create_trusted(&t->core, t->client, 2u, 1u,
        &t->target[1]) != MOO_INPUT_OK) return 0;
    return moo_input_set_focus(&t->core, t->target[0], 0u, 1u, 1u) ==
           MOO_INPUT_OK;
}

static uint32_t popcount32(uint32_t x) {
    uint32_t n = 0u;
    while (x) { n += x & 1u; x >>= 1u; }
    return n;
}

static uint32_t popcount_keys(const MooInputCore *core) {
    uint32_t p;
    uint32_t n = 0u;
    for (p = 0u; p < 256u; ++p)
        if ((core->key_bits[p / 64u] &
             (UINT64_C(1) << (p % 64u))) != 0u) n++;
    return n;
}

static uint32_t live_touches(const MooInputCore *core) {
    uint32_t i;
    uint32_t n = 0u;
    for (i = 0u; i < MOO_INPUT_MAX_TOUCHES; ++i)
        if (core->touches[i].live) n++;
    return n;
}

static int equal_events(Twin *a, Twin *b) {
    MooInputEvent ea;
    MooInputEvent eb;
    for (;;) {
        MooInputResult ra = moo_input_next_event(&a->core, a->client, &ea);
        MooInputResult rb = moo_input_next_event(&b->core, b->client, &eb);
        if (ra != rb) return 0;
        if (ra == MOO_INPUT_WOULD_BLOCK) return 1;
        if (ra != MOO_INPUT_OK || memcmp(&ea, &eb, sizeof(ea)) != 0) return 0;
    }
}

static MooInputResult apply(Twin *t, uint32_t op, uint32_t r,
                            uint64_t serial) {
    uint32_t physical;
    uint32_t word;
    uint64_t bit;
    const uint8_t text[] = {'x'};
    switch (op) {
    case 0:
        return moo_input_pointer_motion(&t->core, t->target[r & 1u],
            (int32_t)(r & 255u), (int32_t)((r >> 8u) & 255u), 1, -1,
            serial, serial);
    case 1:
        if (t->core.pointer_target == MOO_INPUT_HANDLE_INVALID)
            return moo_input_pointer_motion(&t->core, t->target[0],
                                             0, 0, 0, 0, serial, serial);
        return moo_input_pointer_axis(&t->core, (int32_t)(r & 255u) - 128,
                                      120, serial, serial);
    case 2:
        if (t->core.pointer_target == MOO_INPUT_HANDLE_INVALID)
            return moo_input_pointer_motion(&t->core, t->target[0],
                                             0, 0, 0, 0, serial, serial);
        if ((t->core.pointer_buttons & 1u) != 0u)
            return moo_input_pointer_button(&t->core, 0u, MOO_INPUT_RELEASED,
                                             0, 0, serial, serial);
        return moo_input_pointer_button(&t->core, 0u, MOO_INPUT_PRESSED,
                                         0, 0, serial, serial);
    case 3:
        physical = 4u + (r & 3u);
        word = physical / 64u;
        bit = UINT64_C(1) << (physical % 64u);
        return moo_input_key(&t->core, physical, 'a' + (r & 3u),
            (t->core.key_bits[word] & bit) ? MOO_INPUT_RELEASED :
                                             MOO_INPUT_PRESSED,
            0u, serial, serial);
    case 4:
        if (t->core.ime_active)
            return moo_input_text_commit(&t->core, t->core.focus_epoch,
                t->core.ime_revision + 1u, text, 1u, serial, serial);
        return moo_input_text_preedit(&t->core, t->core.focus_epoch,
            t->core.ime_revision + 1u, text, 1u, 0u, 1u, serial, serial);
    case 5: {
        uint32_t id = r & 3u;
        uint32_t i;
        for (i = 0u; i < MOO_INPUT_MAX_TOUCHES; ++i)
            if (t->core.touches[i].live && t->core.touches[i].id == id)
                return moo_input_touch(&t->core, t->target[0], id,
                    MOO_INPUT_TOUCH_UP, 1, 2, 0u, serial, serial);
        return moo_input_touch(&t->core, t->target[0], id,
            MOO_INPUT_TOUCH_DOWN, 1, 2, 100u, serial, serial);
    }
    case 6:
        return moo_input_stylus(&t->core, t->target[r & 1u], 1, 2,
                                100u, -10, 10, 0u, serial, serial);
    case 7:
        return moo_input_set_focus(&t->core, t->target[r & 1u], 9u,
                                   serial, serial);
    default:
        return moo_input_set_preferences(&t->core, r & 1u,
                                         (r >> 1u) & 1u, serial, serial);
    }
}

int main(void) {
    Twin a;
    Twin b;
    uint64_t serial = 2u;
    uint32_t step;
    if (!setup(&a) || !setup(&b)) return 2;
    if (!equal_events(&a, &b)) return 3;
    for (step = 0u; step < STEPS; ++step) {
        uint32_t r = random_u32();
        uint32_t op = r % 9u;
        MooInputResult ra = apply(&a, op, r, serial);
        MooInputResult rb = apply(&b, op, r, serial);
        uint32_t reserved_model;
        if (ra != rb) {
            fprintf(stderr, "result drift step=%u op=%u %u/%u\n",
                    step, op, ra, rb);
            return 1;
        }
        if (ra == MOO_INPUT_OK) serial++;
        if (moo_input_state_hash(&a.core) != moo_input_state_hash(&b.core)) {
            fprintf(stderr, "hash drift step=%u op=%u\n", step, op);
            return 1;
        }
        reserved_model = popcount_keys(&a.core) + live_touches(&a.core) +
            popcount32(a.core.pointer_buttons) + (a.core.ime_active ? 1u : 0u);
        if (a.clients[0].reserved_cleanup != reserved_model) {
            fprintf(stderr, "reservation drift step=%u got=%u want=%u\n",
                    step, a.clients[0].reserved_cleanup, reserved_model);
            return 1;
        }
        if (!equal_events(&a, &b)) {
            fprintf(stderr, "event drift step=%u\n", step);
            return 1;
        }
    }
    printf("P016-O4-TRACE-OK steps=%u hash=%llu\n", STEPS,
           (unsigned long long)moo_input_state_hash(&a.core));
    return 0;
}
