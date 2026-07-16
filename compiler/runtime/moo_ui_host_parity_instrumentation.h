#ifndef MOO_UI_HOST_PARITY_INSTRUMENTATION_H
#define MOO_UI_HOST_PARITY_INSTRUMENTATION_H

#include "moo_compositor_core.h"
#include "moo_ui_host_parity.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION UINT32_C(1)
#define MOO_UI_HOST_PARITY_FRAME_CAPACITY UINT32_C(256)
#define MOO_UI_HOST_PARITY_DEVTOOLS_TRACE_CAPACITY UINT32_C(16)

typedef struct {
    uint64_t generation;
    uint64_t frame_id;
    uint64_t present_sequence;
    uint64_t timestamp_ns;
} MooUiHostParityPresentRecord;

typedef struct {
    uint64_t node;
    uint64_t revision;
    uint64_t trace_hash;
} MooUiHostParityDevtoolsTraceRecord;

typedef struct MooUiHostParityInstrumentation {
    uint32_t version;
    uint32_t sealed;
    uint32_t invalid;
    uint32_t presenter_bound;
    uint32_t frame_count;
    uint32_t helper_bound;
    uint64_t generation;
    uint64_t reduced_idle_start_ns;
    uint64_t reduced_idle_end_ns;
    uint64_t reduced_last_wakeup_ns;
    uint64_t crash_expected_state_hash;
    uint32_t reduced_wakeups;
    uint32_t crash_injected;
    uint32_t crash_restarts;
    uint32_t crash_corruptions;
    uint32_t devtools_sealed;
    uint32_t devtools_invalid;
    uint32_t devtools_inspections;
    uint32_t devtools_trace_count;
    uint32_t devtools_privacy_leaks;
    uint32_t devtools_reserved;
    uint32_t clipboard_sealed;
    uint32_t clipboard_invalid;
    uint32_t clipboard_recorded;
    uint32_t clipboard_roundtrips;
    uint32_t clipboard_dragdrops;
    uint32_t clipboard_integrity_mismatches;
    MooUiHostParityPresentRecord frames[MOO_UI_HOST_PARITY_FRAME_CAPACITY];
    MooUiHostParityDevtoolsTraceRecord
        devtools_traces[MOO_UI_HOST_PARITY_DEVTOOLS_TRACE_CAPACITY];
} MooUiHostParityInstrumentation;

typedef struct {
    uint32_t frame_count;
    uint32_t reserved;
    uint64_t generation;
    uint64_t p99_frame_us;
} MooUiHostParityFrameMetrics;

MooUiHostParityResult moo_ui_host_parity_instrumentation_init(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation);
MooUiHostParityResult moo_ui_host_parity_instrumentation_bind_presenter(
    MooUiHostParityInstrumentation *instrumentation, MooCompositor *core);
MooUiHostParityResult moo_ui_host_parity_instrumentation_seal(
    MooUiHostParityInstrumentation *instrumentation);
MooUiHostParityResult moo_ui_host_parity_instrumentation_frame_metrics(
    const MooUiHostParityInstrumentation *instrumentation,
    MooUiHostParityFrameMetrics *metrics);
MooUiHostParityResult moo_ui_host_parity_instrumentation_probe(
    const MooUiHostParityInstrumentation *instrumentation, uint32_t domain,
    MooUiHostParityMeasurement *measurement);

#ifdef __cplusplus
}
#endif
#endif
