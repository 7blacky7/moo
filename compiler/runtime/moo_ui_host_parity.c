#include "moo_ui_host_parity.h"

static void moo_parity_zero(void *destination, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)destination;
    uint32_t index;
    for (index = 0u; index < size; ++index) bytes[index] = 0u;
}

static uint64_t moo_parity_domain_bit(uint32_t index) {
    return UINT64_C(1) << index;
}

static void moo_parity_advance_generation(MooUiHostParityState *state) {
    if (state->generation < UINT64_MAX) ++state->generation;
}

static uint32_t moo_parity_evidence_valid(uint32_t evidence) {
    return evidence <= (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_NOT_APPLICABLE;
}

static uint32_t moo_parity_measurement_valid(
    const MooUiHostParityMeasurement *measurement, uint32_t domain) {
    if (measurement == 0 ||
        measurement->version != MOO_UI_HOST_PARITY_VERSION ||
        measurement->domain != domain ||
        !moo_parity_evidence_valid(measurement->evidence) ||
        measurement->reserved != 0u)
        return 0u;
    if (measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
        measurement->sample_count == 0u)
        return 0u;
    if ((measurement->evidence ==
             (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED ||
         measurement->evidence ==
             (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_NOT_APPLICABLE) &&
        measurement->sample_count != 0u)
        return 0u;
    if (measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
        domain == 0u &&
        (measurement->value_a >
             (uint64_t)MOO_UI_HOST_PARITY_TYPOGRAPHY_MAX_ERROR_MILLI_PX ||
         measurement->value_b >
             (uint64_t)MOO_UI_HOST_PARITY_TYPOGRAPHY_MAX_ERROR_MILLI_PX ||
         measurement->value_c != UINT64_C(0)))
        return 0u;
    if (measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
        domain == 1u &&
        (measurement->value_a == UINT64_C(0) ||
         measurement->value_b == UINT64_C(0) ||
         measurement->value_c == UINT64_C(0)))
        return 0u;
    if (measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
        domain == 2u &&
        (measurement->value_a == UINT64_C(0) ||
         measurement->value_b == UINT64_C(0) ||
         measurement->value_c == UINT64_C(0)))
        return 0u;
    if (measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
        domain == 3u &&
        (measurement->value_a == UINT64_C(0) ||
         measurement->value_a > (uint64_t)MOO_UI_HOST_PARITY_MAX_MONITORS ||
         measurement->value_b < (uint64_t)MOO_UI_HOST_PARITY_MIN_DPI ||
         measurement->value_b > (uint64_t)MOO_UI_HOST_PARITY_MAX_DPI ||
         measurement->value_c < measurement->value_b ||
         measurement->value_c > (uint64_t)MOO_UI_HOST_PARITY_MAX_DPI))
        return 0u;
    if (measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
        domain == 4u &&
        (measurement->value_a == UINT64_C(0) ||
         measurement->value_b == UINT64_C(0) ||
         measurement->value_c != UINT64_C(0)))
        return 0u;
    if (measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
        domain == 5u &&
        (measurement->sample_count < 2u ||
         measurement->value_a < (uint64_t)MOO_UI_HOST_PARITY_MIN_FRAME_SAMPLES ||
         measurement->value_b > (uint64_t)MOO_UI_HOST_PARITY_MAX_P99_FRAME_US ||
         measurement->value_c >
             (uint64_t)MOO_UI_HOST_PARITY_MAX_IDLE_WAKEUPS_PER_SEC))
        return 0u;
    if (measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
        domain == 6u &&
        (measurement->value_a == UINT64_C(0) ||
         measurement->value_b == UINT64_C(0) ||
         measurement->value_c != UINT64_C(0)))
        return 0u;
    if (measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
        domain == 7u &&
        (measurement->value_a == UINT64_C(0) ||
         measurement->value_b == UINT64_C(0) ||
         measurement->value_c != UINT64_C(0)))
        return 0u;
    return 1u;
}

static uint32_t moo_parity_request_valid(
    const MooUiHostParityRequest *request) {
    uint32_t index;
    if (request == 0 ||
        request->version != MOO_UI_HOST_PARITY_VERSION ||
        request->allow_partial > 1u ||
        (request->required_domains &
         ~((uint64_t)MOO_UI_HOST_PARITY_ALL)) != UINT64_C(0))
        return 0u;
    for (index = 0u; index < 4u; ++index) {
        if (request->reserved[index] != 0u) return 0u;
    }
    return 1u;
}

MooUiHostParityRequest moo_ui_host_parity_request_default(void) {
    MooUiHostParityRequest request;
    moo_parity_zero(&request, (uint32_t)sizeof(request));
    request.version = MOO_UI_HOST_PARITY_VERSION;
    request.required_domains = MOO_UI_HOST_PARITY_ALL;
    return request;
}

MooUiHostParityResult moo_ui_host_parity_init(
    MooUiHostParityState *state, const MooUiHostParityOps *ops, void *user) {
    if (state == 0 || ops == 0 || ops->probe == 0)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    moo_parity_zero(state, (uint32_t)sizeof(*state));
    state->ops = *ops;
    state->user = user;
    state->generation = UINT64_C(1);
    state->native_state = (uint32_t)MOO_UI_HOST_PARITY_NATIVE_READY;
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_probe(
    MooUiHostParityState *state, const MooUiHostParityRequest *request,
    MooUiHostParityReport *report) {
    MooUiHostParityReport candidate;
    uint32_t index;
    if (state == 0 || report == 0 || !moo_parity_request_valid(request) ||
        state->ops.probe == 0)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    if (state->native_state ==
        (uint32_t)MOO_UI_HOST_PARITY_NATIVE_LOST)
        return MOO_UI_HOST_PARITY_RESULT_LOST;
    if (state->native_state !=
        (uint32_t)MOO_UI_HOST_PARITY_NATIVE_READY)
        return state->native_state ==
                (uint32_t)MOO_UI_HOST_PARITY_NATIVE_ERROR
            ? MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR
            : MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;

    moo_parity_zero(&candidate, (uint32_t)sizeof(candidate));
    candidate.version = MOO_UI_HOST_PARITY_VERSION;
    candidate.native_state = state->native_state;
    candidate.generation = state->generation;
    candidate.measurement_count = MOO_UI_HOST_PARITY_DOMAIN_COUNT;
    for (index = 0u; index < MOO_UI_HOST_PARITY_DOMAIN_COUNT; ++index) {
        MooUiHostParityMeasurement measurement;
        MooUiHostParityResult result;
        uint64_t bit = moo_parity_domain_bit(index);
        moo_parity_zero(&measurement, (uint32_t)sizeof(measurement));
        measurement.version = MOO_UI_HOST_PARITY_VERSION;
        measurement.domain = index;
        result = state->ops.probe(state->user, index, &measurement);
        if (result == MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE) {
            moo_parity_zero(&measurement, (uint32_t)sizeof(measurement));
            measurement.version = MOO_UI_HOST_PARITY_VERSION;
            measurement.domain = index;
            measurement.evidence =
                (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED;
        } else if (result != MOO_UI_HOST_PARITY_RESULT_OK) {
            state->last_native_error = measurement.native_error;
            state->native_state =
                result == MOO_UI_HOST_PARITY_RESULT_LOST
                ? (uint32_t)MOO_UI_HOST_PARITY_NATIVE_LOST
                : (uint32_t)MOO_UI_HOST_PARITY_NATIVE_ERROR;
            if (result == MOO_UI_HOST_PARITY_RESULT_LOST)
                moo_parity_advance_generation(state);
            return result;
        }
        if (!moo_parity_measurement_valid(&measurement, index))
            return MOO_UI_HOST_PARITY_RESULT_INVALID;
        candidate.measurements[index] = measurement;
        if (measurement.evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS) {
            candidate.measured_domains |= bit;
            candidate.passed_domains |= bit;
        } else if (measurement.evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_FAIL) {
            candidate.measured_domains |= bit;
            candidate.failed_domains |= bit;
        } else if (measurement.evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED) {
            candidate.unsupported_domains |= bit;
        } else if (measurement.evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_NOT_APPLICABLE) {
            candidate.not_applicable_domains |= bit;
        }
    }
    candidate.missing_required_domains =
        request->required_domains & ~candidate.passed_domains;
    candidate.native_error = state->last_native_error;
    *report = candidate;
    if (candidate.missing_required_domains != UINT64_C(0) &&
        request->allow_partial == 0u)
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    return candidate.failed_domains != UINT64_C(0)
        ? MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR
        : MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_mark_lost(
    MooUiHostParityState *state, int32_t native_error) {
    if (state == 0 || state->ops.probe == 0)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    if (state->native_state ==
        (uint32_t)MOO_UI_HOST_PARITY_NATIVE_LOST)
        return MOO_UI_HOST_PARITY_RESULT_LOST;
    state->native_state = (uint32_t)MOO_UI_HOST_PARITY_NATIVE_LOST;
    state->last_native_error = native_error;
    moo_parity_advance_generation(state);
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

MooUiHostParityResult moo_ui_host_parity_recover(
    MooUiHostParityState *state) {
    if (state == 0 || state->ops.probe == 0)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    if (state->ops.reset != 0) state->ops.reset(state->user);
    state->native_state = (uint32_t)MOO_UI_HOST_PARITY_NATIVE_READY;
    state->last_native_error = 0;
    moo_parity_advance_generation(state);
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

void moo_ui_host_parity_shutdown(MooUiHostParityState *state) {
    if (state == 0) return;
    if (state->ops.reset != 0) state->ops.reset(state->user);
    moo_parity_zero(state, (uint32_t)sizeof(*state));
}

uint64_t moo_ui_host_parity_generation(const MooUiHostParityState *state) {
    return state == 0 ? UINT64_C(0) : state->generation;
}
