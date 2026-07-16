#ifndef MOO_UI_HOST_PARITY_H
#define MOO_UI_HOST_PARITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOO_UI_HOST_PARITY_VERSION UINT32_C(1)
#define MOO_UI_HOST_PARITY_DOMAIN_COUNT UINT32_C(8)

enum {
    MOO_UI_HOST_PARITY_TYPOGRAPHY = UINT64_C(1) << 0,
    MOO_UI_HOST_PARITY_INPUT_IME = UINT64_C(1) << 1,
    MOO_UI_HOST_PARITY_ACCESSIBILITY = UINT64_C(1) << 2,
    MOO_UI_HOST_PARITY_DPI_MONITORS = UINT64_C(1) << 3,
    MOO_UI_HOST_PARITY_CLIPBOARD_DRAG_DROP = UINT64_C(1) << 4,
    MOO_UI_HOST_PARITY_ANIMATION_ENERGY = UINT64_C(1) << 5,
    MOO_UI_HOST_PARITY_CRASH_ISOLATION = UINT64_C(1) << 6,
    MOO_UI_HOST_PARITY_DEVTOOLS = UINT64_C(1) << 7
};
#define MOO_UI_HOST_PARITY_ALL \
    (MOO_UI_HOST_PARITY_TYPOGRAPHY | \
     MOO_UI_HOST_PARITY_INPUT_IME | \
     MOO_UI_HOST_PARITY_ACCESSIBILITY | \
     MOO_UI_HOST_PARITY_DPI_MONITORS | \
     MOO_UI_HOST_PARITY_CLIPBOARD_DRAG_DROP | \
     MOO_UI_HOST_PARITY_ANIMATION_ENERGY | \
     MOO_UI_HOST_PARITY_CRASH_ISOLATION | \
     MOO_UI_HOST_PARITY_DEVTOOLS)

typedef enum {
    MOO_UI_HOST_PARITY_RESULT_OK = 0,
    MOO_UI_HOST_PARITY_RESULT_INVALID = 1,
    MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE = 2,
    MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR = 3,
    MOO_UI_HOST_PARITY_RESULT_LOST = 4,
    MOO_UI_HOST_PARITY_RESULT_LIMIT = 5
} MooUiHostParityResult;

typedef enum {
    MOO_UI_HOST_PARITY_NATIVE_UNINITIALIZED = 0,
    MOO_UI_HOST_PARITY_NATIVE_READY = 1,
    MOO_UI_HOST_PARITY_NATIVE_UNAVAILABLE = 2,
    MOO_UI_HOST_PARITY_NATIVE_ERROR = 3,
    MOO_UI_HOST_PARITY_NATIVE_LOST = 4
} MooUiHostParityNativeState;

typedef enum {
    MOO_UI_HOST_PARITY_EVIDENCE_UNMEASURED = 0,
    MOO_UI_HOST_PARITY_EVIDENCE_PASS = 1,
    MOO_UI_HOST_PARITY_EVIDENCE_FAIL = 2,
    MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED = 3,
    MOO_UI_HOST_PARITY_EVIDENCE_NOT_APPLICABLE = 4
} MooUiHostParityEvidence;

enum {
    MOO_UI_HOST_PARITY_TYPOGRAPHY_MAX_ERROR_MILLI_PX = 500,
    MOO_UI_HOST_PARITY_MIN_DPI = 48,
    MOO_UI_HOST_PARITY_MAX_DPI = 768,
    MOO_UI_HOST_PARITY_MAX_MONITORS = 64,
    MOO_UI_HOST_PARITY_MIN_FRAME_SAMPLES = 120,
    MOO_UI_HOST_PARITY_MAX_P99_FRAME_US = 50000,
    MOO_UI_HOST_PARITY_MAX_IDLE_WAKEUPS_PER_SEC = 10
};

/* Domain payloads:
 * TYPOGRAPHY: value_a=max baseline error in milli-pixels,
 *   value_b=max glyph-advance error in milli-pixels,
 *   value_c=fallback mismatches.
 * INPUT_IME: value_a=key/pointer sequences, value_b=IME updates,
 *   value_c=IME commits; all must be observed for PASS.
 * ACCESSIBILITY: value_a=roles traversed, value_b=properties read,
 *   value_c=actions invoked; all must be observed for PASS.
 * DPI_MONITORS: value_a=monitor count, value_b=min effective DPI,
 *   value_c=max effective DPI.
 * CLIPBOARD_DRAG_DROP: value_a=clipboard roundtrips,
 *   value_b=completed drag/drop sequences, value_c=integrity mismatches.
 * ANIMATION_ENERGY: value_a=frame samples, value_b=p99 frame time in us,
 *   value_c=idle wakeups/second with reduced motion; sample_count>=2 modes.
 * CRASH_ISOLATION: value_a=injected crashes, value_b=successful restarts,
 *   value_c=post-restart state corruptions.
 * DEVTOOLS: value_a=stable-node inspections, value_b=trace events,
 *   value_c=privacy/redaction leaks. */
typedef struct {
    uint32_t version;
    uint32_t domain;
    uint32_t evidence;
    uint32_t sample_count;
    uint64_t value_a;
    uint64_t value_b;
    uint64_t value_c;
    int32_t native_error;
    uint32_t reserved;
} MooUiHostParityMeasurement;

typedef struct {
    uint32_t version;
    uint32_t allow_partial;
    uint64_t required_domains;
    uint32_t reserved[4];
} MooUiHostParityRequest;

typedef struct {
    uint32_t version;
    uint32_t native_state;
    uint64_t measured_domains;
    uint64_t passed_domains;
    uint64_t failed_domains;
    uint64_t unsupported_domains;
    uint64_t not_applicable_domains;
    uint64_t missing_required_domains;
    uint64_t generation;
    int32_t native_error;
    uint32_t measurement_count;
    MooUiHostParityMeasurement measurements[MOO_UI_HOST_PARITY_DOMAIN_COUNT];
} MooUiHostParityReport;

typedef MooUiHostParityResult (*MooUiHostParityProbeFn)(
    void *user, uint32_t domain, MooUiHostParityMeasurement *measurement);
typedef void (*MooUiHostParityResetFn)(void *user);

typedef struct {
    MooUiHostParityProbeFn probe;
    MooUiHostParityResetFn reset;
} MooUiHostParityOps;

typedef struct {
    MooUiHostParityOps ops;
    void *user;
    uint64_t generation;
    uint32_t native_state;
    int32_t last_native_error;
} MooUiHostParityState;

MooUiHostParityRequest moo_ui_host_parity_request_default(void);
MooUiHostParityResult moo_ui_host_parity_init(
    MooUiHostParityState *state, const MooUiHostParityOps *ops, void *user);
MooUiHostParityResult moo_ui_host_parity_probe(
    MooUiHostParityState *state, const MooUiHostParityRequest *request,
    MooUiHostParityReport *report);
MooUiHostParityResult moo_ui_host_parity_mark_lost(
    MooUiHostParityState *state, int32_t native_error);
MooUiHostParityResult moo_ui_host_parity_recover(
    MooUiHostParityState *state);
void moo_ui_host_parity_shutdown(MooUiHostParityState *state);
uint64_t moo_ui_host_parity_generation(const MooUiHostParityState *state);

struct MooUiHostParityInstrumentation;
MooUiHostParityResult moo_ui_host_parity_init_win32(
    MooUiHostParityState *state);
MooUiHostParityResult moo_ui_host_parity_init_win32_instrumented(
    MooUiHostParityState *state,
    struct MooUiHostParityInstrumentation *instrumentation);
MooUiHostParityResult moo_ui_host_parity_init_cocoa(
    MooUiHostParityState *state);

#ifdef __cplusplus
}
#endif
#endif
