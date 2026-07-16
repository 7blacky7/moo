#ifndef MOO_UI_HOST_PARITY_INSTRUMENTATION_INTERNAL_H
#define MOO_UI_HOST_PARITY_INSTRUMENTATION_INTERNAL_H

#include "moo_ui_host_parity_instrumentation.h"

MooUiHostParityResult moo_ui_host_parity_helper_bind(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation);
MooUiHostParityResult moo_ui_host_parity_helper_begin_reduced_idle(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t timestamp_ns);
MooUiHostParityResult moo_ui_host_parity_helper_record_wakeup(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t timestamp_ns);
MooUiHostParityResult moo_ui_host_parity_helper_end_reduced_idle(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t timestamp_ns);
MooUiHostParityResult moo_ui_host_parity_helper_record_crash(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t expected_state_hash);
MooUiHostParityResult moo_ui_host_parity_helper_record_restart(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t recovered_state_hash);
MooUiHostParityResult moo_ui_host_parity_helper_record_devtools_trace(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    uint64_t node, uint64_t revision, uint64_t trace_hash,
    uint32_t privacy_leaks);
MooUiHostParityResult moo_ui_host_parity_helper_seal_devtools(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation);
MooUiHostParityResult moo_ui_host_parity_helper_record_clipboard(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    uint32_t roundtrips, uint32_t dragdrops,
    uint32_t integrity_mismatches);
MooUiHostParityResult moo_ui_host_parity_helper_seal_clipboard(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation);

#endif
