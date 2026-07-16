#include "moo_ui_host_parity_instrumentation.h"
#include "moo_ui_host_parity_instrumentation_internal.h"

static void moo_instrumentation_zero(void *destination, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)destination;
    uint32_t index;
    for (index = 0u; index < size; ++index) bytes[index] = 0u;
}

static void moo_instrumentation_present_done(
    void *user, uint64_t generation, uint64_t frame_id,
    uint64_t present_sequence, uint64_t timestamp_ns) {
    MooUiHostParityInstrumentation *instrumentation =
        (MooUiHostParityInstrumentation *)user;
    MooUiHostParityPresentRecord *record;
    const MooUiHostParityPresentRecord *previous;
    if (instrumentation == 0 ||
        instrumentation->version !=
            MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION)
        return;
    if (instrumentation->sealed != 0u ||
        instrumentation->invalid != 0u ||
        instrumentation->presenter_bound == 0u ||
        generation != instrumentation->generation || frame_id == 0u ||
        present_sequence == 0u || timestamp_ns == 0u ||
        instrumentation->frame_count >=
            MOO_UI_HOST_PARITY_FRAME_CAPACITY) {
        instrumentation->invalid = 1u;
        return;
    }
    if (instrumentation->frame_count != 0u) {
        previous = &instrumentation->frames[
            instrumentation->frame_count - 1u];
        if (previous->generation != generation ||
            frame_id <= previous->frame_id ||
            present_sequence <= previous->present_sequence ||
            timestamp_ns <= previous->timestamp_ns) {
            instrumentation->invalid = 1u;
            return;
        }
    }
    record = &instrumentation->frames[instrumentation->frame_count++];
    record->generation = generation;
    record->frame_id = frame_id;
    record->present_sequence = present_sequence;
    record->timestamp_ns = timestamp_ns;
}

MooUiHostParityResult moo_ui_host_parity_instrumentation_init(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation) {
    if (instrumentation == 0 || generation == 0u)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    moo_instrumentation_zero(
        instrumentation, (uint32_t)sizeof(*instrumentation));
    instrumentation->version =
        MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION;
    instrumentation->generation = generation;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_instrumentation_bind_presenter(
    MooUiHostParityInstrumentation *instrumentation, MooCompositor *core) {
    MooCompResult result;
    if (instrumentation == 0 || core == 0 ||
        instrumentation->version !=
            MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION ||
        instrumentation->sealed != 0u ||
        instrumentation->invalid != 0u ||
        instrumentation->presenter_bound != 0u)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    result = moo_comp_present_done_observer_bind(core,
        instrumentation->generation,
        moo_instrumentation_present_done, instrumentation);
    if (result != MOO_COMP_OK)
        return result == MOO_COMP_BAD_STATE
            ? MOO_UI_HOST_PARITY_RESULT_LOST
            : MOO_UI_HOST_PARITY_RESULT_INVALID;
    instrumentation->presenter_bound = 1u;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

static MooUiHostParityResult moo_instrumentation_helper_mutable(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation) {
    if (instrumentation == 0 ||
        instrumentation->version !=
            MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION ||
        instrumentation->sealed != 0u || instrumentation->invalid != 0u ||
        instrumentation->helper_bound == 0u ||
        generation != instrumentation->generation) {
        if (instrumentation != 0) instrumentation->invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_bind(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation) {
    if (instrumentation == 0 ||
        instrumentation->version !=
            MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION ||
        instrumentation->sealed != 0u || instrumentation->invalid != 0u ||
        instrumentation->helper_bound != 0u ||
        generation != instrumentation->generation) {
        if (instrumentation != 0) instrumentation->invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    instrumentation->helper_bound = 1u;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_begin_reduced_idle(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t timestamp_ns) {
    MooUiHostParityResult result =
        moo_instrumentation_helper_mutable(instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (timestamp_ns == 0u || instrumentation->reduced_idle_start_ns != 0u) {
        instrumentation->invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    instrumentation->reduced_idle_start_ns = timestamp_ns;
    instrumentation->reduced_last_wakeup_ns = timestamp_ns;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_record_wakeup(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t timestamp_ns) {
    MooUiHostParityResult result =
        moo_instrumentation_helper_mutable(instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (instrumentation->reduced_idle_start_ns == 0u ||
        instrumentation->reduced_idle_end_ns != 0u ||
        timestamp_ns <= instrumentation->reduced_last_wakeup_ns ||
        instrumentation->reduced_wakeups == UINT32_MAX) {
        instrumentation->invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    instrumentation->reduced_last_wakeup_ns = timestamp_ns;
    ++instrumentation->reduced_wakeups;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_end_reduced_idle(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t timestamp_ns) {
    MooUiHostParityResult result =
        moo_instrumentation_helper_mutable(instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (instrumentation->reduced_idle_start_ns == 0u ||
        instrumentation->reduced_idle_end_ns != 0u ||
        timestamp_ns < instrumentation->reduced_last_wakeup_ns ||
        timestamp_ns - instrumentation->reduced_idle_start_ns <
            UINT64_C(1000000000)) {
        instrumentation->invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    instrumentation->reduced_idle_end_ns = timestamp_ns;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_record_crash(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t expected_state_hash) {
    MooUiHostParityResult result =
        moo_instrumentation_helper_mutable(instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (expected_state_hash == 0u ||
        instrumentation->crash_injected != instrumentation->crash_restarts ||
        instrumentation->crash_injected == UINT32_MAX) {
        instrumentation->invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    instrumentation->crash_expected_state_hash = expected_state_hash;
    ++instrumentation->crash_injected;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_record_restart(
    MooUiHostParityInstrumentation *instrumentation,
    uint64_t generation, uint64_t recovered_state_hash) {
    MooUiHostParityResult result =
        moo_instrumentation_helper_mutable(instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (recovered_state_hash == 0u ||
        instrumentation->crash_restarts >= instrumentation->crash_injected) {
        instrumentation->invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    ++instrumentation->crash_restarts;
    if (recovered_state_hash != instrumentation->crash_expected_state_hash)
        ++instrumentation->crash_corruptions;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

static MooUiHostParityResult moo_instrumentation_clipboard_mutable(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation) {
    if (instrumentation == 0 ||
        instrumentation->version !=
            MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION ||
        instrumentation->invalid != 0u ||
        instrumentation->clipboard_sealed != 0u ||
        instrumentation->clipboard_invalid != 0u ||
        instrumentation->helper_bound == 0u ||
        generation != instrumentation->generation) {
        if (instrumentation != 0) instrumentation->clipboard_invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_record_clipboard(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    uint32_t roundtrips, uint32_t dragdrops,
    uint32_t integrity_mismatches) {
    MooUiHostParityResult result =
        moo_instrumentation_clipboard_mutable(instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (instrumentation->clipboard_recorded != 0u || roundtrips > 1u ||
        dragdrops > 1u || integrity_mismatches > 1u ||
        (roundtrips == 0u && dragdrops == 0u &&
         integrity_mismatches == 0u)) {
        instrumentation->clipboard_invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    instrumentation->clipboard_roundtrips = roundtrips;
    instrumentation->clipboard_dragdrops = dragdrops;
    instrumentation->clipboard_integrity_mismatches =
        integrity_mismatches;
    instrumentation->clipboard_recorded = 1u;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_seal_clipboard(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation) {
    MooUiHostParityResult result =
        moo_instrumentation_clipboard_mutable(instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (instrumentation->clipboard_recorded != 1u) {
        instrumentation->clipboard_invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    instrumentation->clipboard_sealed = 1u;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

static MooUiHostParityResult moo_instrumentation_devtools_mutable(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation) {
    if (instrumentation == 0 ||
        instrumentation->version !=
            MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION ||
        instrumentation->invalid != 0u ||
        instrumentation->devtools_sealed != 0u ||
        instrumentation->devtools_invalid != 0u ||
        instrumentation->helper_bound == 0u ||
        generation != instrumentation->generation) {
        if (instrumentation != 0) instrumentation->devtools_invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_record_devtools_trace(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    uint64_t node, uint64_t revision, uint64_t trace_hash,
    uint32_t privacy_leaks) {
    MooUiHostParityDevtoolsTraceRecord *record;
    MooUiHostParityResult result =
        moo_instrumentation_devtools_mutable(instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (node == 0u || revision == 0u || trace_hash == 0u ||
        privacy_leaks != 0u ||
        instrumentation->devtools_trace_count >=
            MOO_UI_HOST_PARITY_DEVTOOLS_TRACE_CAPACITY ||
        instrumentation->devtools_inspections == UINT32_MAX) {
        instrumentation->devtools_privacy_leaks += privacy_leaks;
        instrumentation->devtools_invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    record = &instrumentation->devtools_traces[
        instrumentation->devtools_trace_count++];
    record->node = node;
    record->revision = revision;
    record->trace_hash = trace_hash;
    ++instrumentation->devtools_inspections;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_helper_seal_devtools(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation) {
    MooUiHostParityResult result =
        moo_instrumentation_devtools_mutable(instrumentation, generation);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return result;
    if (instrumentation->devtools_inspections == 0u ||
        instrumentation->devtools_trace_count !=
            instrumentation->devtools_inspections ||
        instrumentation->devtools_privacy_leaks != 0u) {
        instrumentation->devtools_invalid = 1u;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    instrumentation->devtools_sealed = 1u;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_instrumentation_seal(
    MooUiHostParityInstrumentation *instrumentation) {
    if (instrumentation == 0 ||
        instrumentation->version !=
            MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION ||
        instrumentation->sealed != 0u ||
        instrumentation->invalid != 0u ||
        instrumentation->presenter_bound == 0u ||
        instrumentation->helper_bound == 0u ||
        instrumentation->reduced_idle_end_ns == 0u ||
        instrumentation->crash_injected == 0u ||
        instrumentation->crash_restarts != instrumentation->crash_injected)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    instrumentation->sealed = 1u;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

static uint64_t moo_instrumentation_p99_us(
    const MooUiHostParityInstrumentation *instrumentation) {
    uint64_t intervals[MOO_UI_HOST_PARITY_FRAME_CAPACITY - 1u];
    uint32_t count = instrumentation->frame_count - 1u;
    uint32_t index;
    for (index = 0u; index < count; ++index)
        intervals[index] =
            (instrumentation->frames[index + 1u].timestamp_ns -
             instrumentation->frames[index].timestamp_ns + UINT64_C(999)) /
            UINT64_C(1000);
    for (index = 1u; index < count; ++index) {
        uint64_t value = intervals[index];
        uint32_t cursor = index;
        while (cursor != 0u && intervals[cursor - 1u] > value) {
            intervals[cursor] = intervals[cursor - 1u];
            --cursor;
        }
        intervals[cursor] = value;
    }
    index = (uint32_t)(((uint64_t)count * UINT64_C(99) + UINT64_C(99)) /
        UINT64_C(100));
    return intervals[index == 0u ? 0u : index - 1u];
}

static uint64_t moo_instrumentation_wakeups_per_second(
    const MooUiHostParityInstrumentation *instrumentation) {
    uint64_t duration = instrumentation->reduced_idle_end_ns -
        instrumentation->reduced_idle_start_ns;
    uint64_t wakeups = (uint64_t)instrumentation->reduced_wakeups;
    if (wakeups > UINT64_MAX / UINT64_C(1000000000)) return UINT64_MAX;
    return (wakeups * UINT64_C(1000000000) + duration - 1u) / duration;
}

MooUiHostParityResult moo_ui_host_parity_instrumentation_frame_metrics(
    const MooUiHostParityInstrumentation *instrumentation,
    MooUiHostParityFrameMetrics *metrics) {
    if (instrumentation == 0 || metrics == 0 ||
        instrumentation->version !=
            MOO_UI_HOST_PARITY_INSTRUMENTATION_VERSION)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    if (instrumentation->sealed == 0u ||
        instrumentation->presenter_bound == 0u)
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    if (instrumentation->invalid != 0u ||
        instrumentation->frame_count <
            (uint32_t)MOO_UI_HOST_PARITY_MIN_FRAME_SAMPLES)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    metrics->frame_count = instrumentation->frame_count;
    metrics->reserved = 0u;
    metrics->generation = instrumentation->generation;
    metrics->p99_frame_us = moo_instrumentation_p99_us(instrumentation);
    return MOO_UI_HOST_PARITY_RESULT_OK;
}
static void moo_instrumentation_clear_measurement(
    MooUiHostParityMeasurement *measurement) {
    measurement->evidence =
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNMEASURED;
    measurement->sample_count = 0u;
    measurement->value_a = UINT64_C(0);
    measurement->value_b = UINT64_C(0);
    measurement->value_c = UINT64_C(0);
    measurement->native_error = 0;
}

MooUiHostParityResult moo_ui_host_parity_instrumentation_probe(
    const MooUiHostParityInstrumentation *instrumentation, uint32_t domain,
    MooUiHostParityMeasurement *measurement) {
    MooUiHostParityFrameMetrics metrics;
    uint64_t wakeups;
    if (instrumentation == 0 || measurement == 0 || domain < 4u || domain > 7u)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    moo_instrumentation_clear_measurement(measurement);
    if (domain == 4u) {
        if (instrumentation->invalid != 0u ||
            instrumentation->clipboard_invalid != 0u) {
            measurement->evidence =
                (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL;
            return MOO_UI_HOST_PARITY_RESULT_OK;
        }
        if (instrumentation->clipboard_sealed == 0u) {
            measurement->evidence =
                (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED;
            return MOO_UI_HOST_PARITY_RESULT_OK;
        }
        measurement->sample_count = 2u;
        measurement->value_a =
            (uint64_t)instrumentation->clipboard_roundtrips;
        measurement->value_b =
            (uint64_t)instrumentation->clipboard_dragdrops;
        measurement->value_c =
            (uint64_t)instrumentation->clipboard_integrity_mismatches;
        measurement->evidence =
            instrumentation->clipboard_recorded == 1u &&
            instrumentation->clipboard_roundtrips == 1u &&
            instrumentation->clipboard_dragdrops == 1u &&
            instrumentation->clipboard_integrity_mismatches == 0u
            ? (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS
            : (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL;
        return MOO_UI_HOST_PARITY_RESULT_OK;
    }
    if (domain == 7u) {
        if (instrumentation->invalid != 0u ||
            instrumentation->devtools_invalid != 0u) {
            measurement->evidence =
                (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL;
            return MOO_UI_HOST_PARITY_RESULT_OK;
        }
        if (instrumentation->devtools_sealed == 0u) {
            measurement->evidence =
                (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED;
            return MOO_UI_HOST_PARITY_RESULT_OK;
        }
        measurement->sample_count =
            instrumentation->devtools_trace_count;
        measurement->value_a =
            (uint64_t)instrumentation->devtools_inspections;
        measurement->value_b =
            (uint64_t)instrumentation->devtools_trace_count;
        measurement->value_c =
            (uint64_t)instrumentation->devtools_privacy_leaks;
        measurement->evidence =
            instrumentation->devtools_inspections != 0u &&
            instrumentation->devtools_trace_count ==
                instrumentation->devtools_inspections &&
            instrumentation->devtools_privacy_leaks == 0u
            ? (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS
            : (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL;
        return MOO_UI_HOST_PARITY_RESULT_OK;
    }
    if (instrumentation->sealed == 0u) {
        measurement->evidence =
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED;
        return MOO_UI_HOST_PARITY_RESULT_OK;
    }
    if (instrumentation->invalid != 0u) {
        measurement->evidence = (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL;
        return MOO_UI_HOST_PARITY_RESULT_OK;
    }
    if (domain == 5u) {
        if (moo_ui_host_parity_instrumentation_frame_metrics(
                instrumentation, &metrics) != MOO_UI_HOST_PARITY_RESULT_OK)
            return MOO_UI_HOST_PARITY_RESULT_INVALID;
        wakeups = moo_instrumentation_wakeups_per_second(instrumentation);
        measurement->sample_count = 2u;
        measurement->value_a = (uint64_t)metrics.frame_count;
        measurement->value_b = metrics.p99_frame_us;
        measurement->value_c = wakeups;
        measurement->evidence = metrics.p99_frame_us <=
                (uint64_t)MOO_UI_HOST_PARITY_MAX_P99_FRAME_US &&
            wakeups <=
                (uint64_t)MOO_UI_HOST_PARITY_MAX_IDLE_WAKEUPS_PER_SEC
            ? (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS
            : (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL;
    } else if (domain == 6u) {
        measurement->sample_count = instrumentation->crash_injected;
        measurement->value_a = (uint64_t)instrumentation->crash_injected;
        measurement->value_b = (uint64_t)instrumentation->crash_restarts;
        measurement->value_c = (uint64_t)instrumentation->crash_corruptions;
        measurement->evidence = instrumentation->crash_injected != 0u &&
                instrumentation->crash_restarts ==
                    instrumentation->crash_injected &&
                instrumentation->crash_corruptions == 0u
            ? (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS
            : (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL;
    }
    return MOO_UI_HOST_PARITY_RESULT_OK;
}
