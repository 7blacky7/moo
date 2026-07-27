#include "../moo_ui_pointer_meta.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

static int checks;
static int failures;

#define CHECK(condition, message) do { \
    checks++; \
    if (!(condition)) { \
        failures++; \
        fprintf(stderr, "FAIL:%d:%s\n", __LINE__, (message)); \
    } \
} while (0)

static MooUiPointerMeta click(
    MooUiPointerTracker *tracker,
    uint32_t button,
    double x,
    double y,
    uint32_t native_time,
    uint64_t receive_time,
    MooUiPointerThresholds thresholds) {
    MooUiPointerMeta meta = moo_ui_pointer_press(
        tracker, button, x, y, native_time, receive_time, 1u,
        thresholds, 0u);
    (void)moo_ui_pointer_release(tracker, button);
    return meta;
}

static void test_threshold_helpers(void) {
    MooUiPointerThresholds linux_t = moo_ui_pointer_linux_thresholds();
    MooUiPointerThresholds even_t =
        moo_ui_pointer_rectangle_thresholds(500u, 4u, 6u);
    MooUiPointerThresholds odd_t =
        moo_ui_pointer_rectangle_thresholds(500u, 5u, 1u);
    MooUiPointerThresholds invalid_t =
        moo_ui_pointer_rectangle_thresholds(500u, 0u, 1u);

    CHECK(linux_t.valid && linux_t.interval_ms == 400u,
          "linux interval");
    CHECK(linux_t.min_dx == -5.0 && linux_t.max_dx == 5.0 &&
          linux_t.min_dy == -5.0 && linux_t.max_dy == 5.0,
          "linux inclusive 5px");
    CHECK(even_t.valid && even_t.min_dx == -2.0 &&
          even_t.max_dx == 1.0 && even_t.min_dy == -3.0 &&
          even_t.max_dy == 2.0, "even rectangle is exact size");
    CHECK(odd_t.valid && odd_t.min_dx == -2.0 &&
          odd_t.max_dx == 2.0 && odd_t.min_dy == 0.0 &&
          odd_t.max_dy == 0.0, "odd and one-pixel rectangles");
    CHECK(!invalid_t.valid, "zero metric fails closed");
}

static void test_click_automaton(void) {
    MooUiPointerTracker tracker;
    MooUiPointerThresholds t = moo_ui_pointer_linux_thresholds();
    MooUiPointerMeta meta;

    moo_ui_pointer_tracker_init(&tracker);
    meta = click(&tracker, 1u, 10.0, 20.0, 100u, 1000u, t);
    CHECK(meta.valid && meta.click_count == 1u, "first click");
    meta = click(&tracker, 1u, 15.0, 25.0, 500u, 1200u, t);
    CHECK(meta.click_count == 2u, "exact time and position threshold");
    meta = click(&tracker, 1u, 15.0, 25.0, 600u, 1300u, t);
    CHECK(meta.click_count == 1u, "triple restarts at one");

    moo_ui_pointer_tracker_reset(&tracker);
    (void)click(&tracker, 1u, 0.0, 0.0, 10u, 10u, t);
    meta = click(&tracker, 1u, 6.0, 0.0, 11u, 11u, t);
    CHECK(meta.click_count == 1u, "position threshold plus one");
    meta = click(&tracker, 1u, 6.0, 0.0, 412u, 12u, t);
    CHECK(meta.click_count == 1u, "time threshold plus one");

    moo_ui_pointer_tracker_reset(&tracker);
    (void)click(&tracker, 1u, 0.0, 0.0, 10u, 10u, t);
    meta = click(&tracker, 3u, 0.0, 0.0, 11u, 11u, t);
    CHECK(meta.click_count == 1u, "button mismatch");
    meta = click(&tracker, 3u, 0.0, 0.0, 12u, 12u, t);
    CHECK(meta.click_count == 2u, "mismatch reseeds new button");

    moo_ui_pointer_tracker_reset(&tracker);
    (void)click(&tracker, 1u, NAN, 0.0, 1u, 1u, t);
    meta = click(&tracker, 1u, 0.0, 0.0, 2u, 2u, t);
    CHECK(meta.click_count == 1u, "nonfinite does not seed");
}

static void test_two_time_contract(void) {
    MooUiPointerTracker tracker;
    MooUiPointerThresholds t = moo_ui_pointer_linux_thresholds();
    MooUiPointerMeta meta;

    moo_ui_pointer_tracker_init(&tracker);
    (void)click(&tracker, 1u, 0.0, 0.0, 100u, 100u, t);
    meta = click(&tracker, 1u, 0.0, 0.0, 10100u, 101u, t);
    CHECK(meta.click_count == 1u, "native ten seconds beats receipt one ms");

    moo_ui_pointer_tracker_reset(&tracker);
    (void)click(&tracker, 1u, 0.0, 0.0, 100u, 100u, t);
    meta = click(&tracker, 1u, 0.0, 0.0, 300u, 1100u, t);
    CHECK(meta.click_count == 2u, "native 200ms survives receipt one second");

    moo_ui_pointer_tracker_reset(&tracker);
    (void)click(&tracker, 1u, 0.0, 0.0, 100u, 100u, t);
    meta = click(&tracker, 1u, 0.0, 0.0, 300u,
                 100u + (UINT64_C(1) << 32), t);
    CHECK(meta.click_count == 1u, "receipt epoch expires candidate");

    moo_ui_pointer_tracker_reset(&tracker);
    (void)click(&tracker, 1u, 0.0, 0.0, UINT32_MAX - 100u, 100u, t);
    meta = click(&tracker, 1u, 0.0, 0.0, 50u, 300u, t);
    CHECK(meta.click_count == 2u, "single native wrap is close");

    moo_ui_pointer_tracker_reset(&tracker);
    (void)click(&tracker, 1u, 0.0, 0.0, 1000u, 1000u, t);
    meta = click(&tracker, 1u, 0.0, 0.0, 900u, 1100u, t);
    CHECK(meta.click_count == 1u, "backwards native time is far");

    moo_ui_pointer_tracker_reset(&tracker);
    (void)click(&tracker, 1u, 0.0, 0.0, 100u, 1000u, t);
    meta = click(&tracker, 1u, 0.0, 0.0, 200u, 900u, t);
    CHECK(meta.click_count == 1u, "backwards receipt expires candidate");
}

static void test_press_release_and_chords(void) {
    static const uint32_t sequences[4][4] = {
        {1u, 3u, 3u, 1u},
        {1u, 3u, 1u, 3u},
        {3u, 1u, 1u, 3u},
        {3u, 1u, 3u, 1u}
    };
    MooUiPointerThresholds t = moo_ui_pointer_linux_thresholds();
    uint32_t s;

    for (s = 0u; s < 4u; ++s) {
        MooUiPointerTracker tracker;
        MooUiPointerMeta first;
        MooUiPointerMeta second;
        MooUiPointerMeta released_first;
        MooUiPointerMeta released_second;
        moo_ui_pointer_tracker_init(&tracker);
        first = moo_ui_pointer_press(
            &tracker, sequences[s][0], 1.0, 1.0, 100u, 100u, 1u,
            t, MOO_INPUT_MOD_SHIFT);
        second = moo_ui_pointer_press(
            &tracker, sequences[s][1], 2.0, 2.0, 200u, 200u, 1u,
            t, MOO_INPUT_MOD_CONTROL);
        released_first = moo_ui_pointer_release(&tracker, sequences[s][2]);
        released_second = moo_ui_pointer_release(&tracker, sequences[s][3]);
        CHECK(first.valid && second.valid, "both C press slots active");
        CHECK(released_first.valid &&
              released_first.native_time_ms ==
                  (sequences[s][2] == sequences[s][0] ? 100u : 200u),
              "first release matches same button");
        CHECK(released_second.valid &&
              released_second.native_time_ms ==
                  (sequences[s][3] == sequences[s][0] ? 100u : 200u),
              "second release matches same button");
        CHECK(!tracker.active[0].valid && !tracker.active[2].valid,
              "all chord slots clear");
    }

    {
        MooUiPointerTracker tracker;
        MooUiPointerMeta first;
        MooUiPointerMeta duplicate;
        MooUiPointerMeta release;
        moo_ui_pointer_tracker_init(&tracker);
        first = moo_ui_pointer_press(
            &tracker, 1u, 0.0, 0.0, 10u, 10u, 1u, t, 0u);
        duplicate = moo_ui_pointer_press(
            &tracker, 1u, 0.0, 0.0, 11u, 11u, 1u, t, 0u);
        release = moo_ui_pointer_release(&tracker, 1u);
        CHECK(first.click_count == 1u && duplicate.click_count == 1u,
              "duplicate down is one one");
        CHECK(release.valid && release.native_time_ms == 11u,
              "duplicate release follows newest down");
        CHECK(!moo_ui_pointer_release(&tracker, 1u).valid,
              "release without press invalid");
    }
}

static int tls_thread(void *unused) {
    MooUiPointerMeta meta;
    MooUiPointerMeta out;
    (void)unused;
    memset(&meta, 0, sizeof(meta));
    meta.native_time_ms = 77u;
    meta.click_count = 2u;
    meta.valid = 1u;
    moo_ui_pointer_publish(MOO_UI_POINTER_DOMAIN_WAYLAND, 22u,
                           MOO_UI_POINTER_PHASE_PRESS, 3u, meta);
    return moo_ui_pointer_take(
               MOO_UI_POINTER_DOMAIN_WAYLAND, 22u,
               MOO_UI_POINTER_PHASE_PRESS, 3u, &out) &&
           out.native_time_ms == 77u;
}

static void test_tls_snapshot(void) {
    MooUiPointerMeta meta;
    MooUiPointerMeta out;
    thrd_t thread;
    int thread_ok = 0;

    memset(&meta, 0, sizeof(meta));
    meta.modifiers = MOO_INPUT_MOD_SHIFT;
    meta.native_time_ms = UINT32_MAX;
    meta.click_count = 2u;
    meta.valid = 1u;
    moo_ui_pointer_publish(MOO_UI_POINTER_DOMAIN_X11, 11u,
                           MOO_UI_POINTER_PHASE_PRESS, 1u, meta);
    CHECK(!moo_ui_pointer_take(
               MOO_UI_POINTER_DOMAIN_X11, 12u,
               MOO_UI_POINTER_PHASE_PRESS, 1u, &out) &&
          !out.valid && out.click_count == 1u,
          "mismatch is neutral without consuming");
    CHECK(!moo_ui_pointer_take(
               MOO_UI_POINTER_DOMAIN_WAYLAND, 11u,
               MOO_UI_POINTER_PHASE_PRESS, 1u, &out) &&
          !out.valid && out.click_count == 1u,
          "wrong domain is neutral without consuming");

    CHECK(thrd_create(&thread, tls_thread, NULL) == thrd_success,
          "tls thread create");
    CHECK(thrd_join(thread, &thread_ok) == thrd_success && thread_ok,
          "tls thread isolated snapshot");

    CHECK(moo_ui_pointer_take(
              MOO_UI_POINTER_DOMAIN_X11, 11u,
              MOO_UI_POINTER_PHASE_PRESS, 1u, &out) &&
          out.native_time_ms == UINT32_MAX &&
          out.modifiers == MOO_INPUT_MOD_SHIFT &&
          out.click_count == 2u,
          "native time and metadata are bit-exact");
    CHECK(!moo_ui_pointer_take(
               MOO_UI_POINTER_DOMAIN_X11, 11u,
               MOO_UI_POINTER_PHASE_PRESS, 1u, &out) &&
          !out.valid && out.click_count == 1u,
          "snapshot is one shot");

    moo_ui_pointer_publish(UINT32_MAX, 44u,
                           MOO_UI_POINTER_PHASE_PRESS, 1u, meta);
    CHECK(!moo_ui_pointer_take(UINT32_MAX, 44u,
                               MOO_UI_POINTER_PHASE_PRESS, 1u, &out) &&
          !out.valid && out.click_count == 1u,
          "unknown domain is rejected");

    moo_ui_pointer_publish(MOO_UI_POINTER_DOMAIN_WIN32, 33u,
                           MOO_UI_POINTER_PHASE_RELEASE, 2u, meta);
    moo_ui_pointer_clear(MOO_UI_POINTER_DOMAIN_WAYLAND, 33u);
    moo_ui_pointer_clear(MOO_UI_POINTER_DOMAIN_WIN32, 34u);
    CHECK(moo_ui_pointer_take(
              MOO_UI_POINTER_DOMAIN_WIN32, 33u,
              MOO_UI_POINTER_PHASE_RELEASE, 2u, &out) && out.valid,
          "foreign domain and handle clear preserve owner");
    moo_ui_pointer_publish(MOO_UI_POINTER_DOMAIN_WIN32, 33u,
                           MOO_UI_POINTER_PHASE_RELEASE, 2u, meta);
    moo_ui_pointer_clear(MOO_UI_POINTER_DOMAIN_WIN32, 33u);
    CHECK(!moo_ui_pointer_take(
               MOO_UI_POINTER_DOMAIN_WIN32, 33u,
               MOO_UI_POINTER_PHASE_RELEASE, 2u, &out),
          "owner clear removes callback-edge state");
}

static void test_modifier_mask(void) {
    uint32_t all = moo_ui_pointer_modifiers(
        1u, 1u, 1u, 1u, 1u, 1u, 0u);
    uint32_t altgr = moo_ui_pointer_modifiers(
        0u, 0u, 0u, 0u, 0u, 0u, 1u);
    CHECK(all == (MOO_INPUT_MOD_SHIFT | MOO_INPUT_MOD_CONTROL |
                  MOO_INPUT_MOD_ALT | MOO_INPUT_MOD_SUPER |
                  MOO_INPUT_MOD_CAPS_LOCK | MOO_INPUT_MOD_NUM_LOCK),
          "canonical modifier bits");
    CHECK(altgr == (MOO_INPUT_MOD_ALT_GR | MOO_INPUT_MOD_ALT),
          "altgr implies alt");
    CHECK(moo_ui_pointer_modifiers_from_state(
              UINT32_C(0xe4), UINT32_C(0x01), UINT32_C(0x02),
              UINT32_C(0x20), UINT32_C(0x08), UINT32_C(0x10),
              UINT32_C(0x40), UINT32_C(0x04)) ==
              (MOO_INPUT_MOD_ALT | MOO_INPUT_MOD_ALT_GR |
               MOO_INPUT_MOD_NUM_LOCK),
          "rebound native masks map canonically and ignore unknown bits");
}

int main(void) {
    uint64_t monotonic_ms = 0u;
    test_threshold_helpers();
    test_click_automaton();
    test_two_time_contract();
    test_press_release_and_chords();
    test_tls_snapshot();
    test_modifier_mask();
    CHECK(moo_ui_pointer_monotonic_ms(&monotonic_ms),
          "production monotonic source available");
    if (failures) {
        fprintf(stderr, "C2A-B1 FAIL checks=%d failures=%d\n",
                checks, failures);
        return 1;
    }
    printf("C2A-B1 OK checks=%d\n", checks);
    return 0;
}
