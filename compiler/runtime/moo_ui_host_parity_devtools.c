#include "moo_ui_host_parity_devtools.h"
#include "moo_ui_host_parity_instrumentation_internal.h"

#include <string.h>

#define MOO_DEVTOOLS_TRACE_BYTES UINT32_C(96)

static void moo_devtools_put_u32(
    uint8_t *trace, uint32_t *offset, uint32_t value) {
    uint32_t index;
    for (index = 0u; index < 4u; ++index)
        trace[(*offset)++] = (uint8_t)(value >> (index * 8u));
}

static void moo_devtools_put_u64(
    uint8_t *trace, uint32_t *offset, uint64_t value) {
    uint32_t index;
    for (index = 0u; index < 8u; ++index)
        trace[(*offset)++] = (uint8_t)(value >> (index * 8u));
}

static uint32_t moo_devtools_password_is_redacted(
    const MooA11yNodeData *data) {
    uint32_t index;
    if (data == 0 ||
        (data->states & MOO_A11Y_STATE_PASSWORD) == 0u ||
        data->value.length != 0u)
        return 0u;
    for (index = 0u; index < MOO_INPUT_TEXT_CAPACITY; ++index)
        if (data->value.bytes[index] != 0u) return 0u;
    return 1u;
}

static uint32_t moo_devtools_build_trace(
    MooInputHandle node, uint64_t revision, const MooA11yNodeData *data,
    uint8_t trace[MOO_DEVTOOLS_TRACE_BYTES]) {
    uint32_t offset = 0u;
    memset(trace, 0, MOO_DEVTOOLS_TRACE_BYTES);
    moo_devtools_put_u64(trace, &offset, node);
    moo_devtools_put_u64(trace, &offset, revision);
    moo_devtools_put_u64(trace, &offset, data->parent);
    moo_devtools_put_u64(trace, &offset, data->labelled_by);
    moo_devtools_put_u64(trace, &offset, data->described_by);
    moo_devtools_put_u64(trace, &offset, data->controls);
    moo_devtools_put_u32(trace, &offset, data->role);
    moo_devtools_put_u32(trace, &offset, data->states);
    moo_devtools_put_u32(trace, &offset, data->actions);
    moo_devtools_put_u32(trace, &offset, (uint32_t)data->bounds.x);
    moo_devtools_put_u32(trace, &offset, (uint32_t)data->bounds.y);
    moo_devtools_put_u32(trace, &offset, (uint32_t)data->bounds.width);
    moo_devtools_put_u32(trace, &offset, (uint32_t)data->bounds.height);
    moo_devtools_put_u32(trace, &offset, data->value.length);
    return offset;
}

static uint32_t moo_devtools_trace_contains(
    const uint8_t *trace, uint32_t trace_length,
    const uint8_t *secret, uint32_t secret_length) {
    uint32_t offset;
    if (secret == 0 || secret_length == 0u || secret_length > trace_length)
        return 0u;
    for (offset = 0u; offset <= trace_length - secret_length; ++offset)
        if (memcmp(trace + offset, secret, secret_length) == 0) return 1u;
    return 0u;
}

static uint64_t moo_devtools_trace_hash(
    const uint8_t *trace, uint32_t trace_length) {
    uint32_t index;
    uint64_t hash = UINT64_C(1469598103934665603);
    for (index = 0u; index < trace_length; ++index) {
        hash ^= (uint64_t)trace[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash == 0u ? UINT64_C(1469598103934665603) : hash;
}

static MooUiHostParityResult moo_devtools_input_result(
    MooInputResult result) {
    if (result == MOO_INPUT_LIMIT) return MOO_UI_HOST_PARITY_RESULT_LIMIT;
    if (result == MOO_INPUT_UNSUPPORTED)
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    return MOO_UI_HOST_PARITY_RESULT_INVALID;
}

MooUiHostParityResult moo_ui_host_parity_devtools_inspect_password_node(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    const MooUiHostParityDevtoolsInspection *inspection) {
    MooA11yNodeData first;
    MooA11yNodeData second;
    uint8_t first_trace[MOO_DEVTOOLS_TRACE_BYTES];
    uint8_t second_trace[MOO_DEVTOOLS_TRACE_BYTES];
    uint64_t first_revision = 0u;
    uint64_t second_revision = 0u;
    uint64_t trace_hash;
    uint32_t first_length;
    uint32_t second_length;
    uint32_t privacy_leaks;
    MooInputResult result;
    if (instrumentation == 0 || generation == 0u || inspection == 0 ||
        inspection->core == 0 ||
        inspection->privileged_reader == MOO_INPUT_HANDLE_INVALID ||
        inspection->node == MOO_INPUT_HANDLE_INVALID ||
        inspection->expected_revision == 0u ||
        inspection->seeded_secret == 0 ||
        inspection->seeded_secret_length == 0u ||
        inspection->seeded_secret_length > MOO_INPUT_TEXT_CAPACITY)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    result = moo_a11y_node_read(inspection->core,
        inspection->privileged_reader, inspection->node,
        &first, &first_revision);
    if (result != MOO_INPUT_OK) return moo_devtools_input_result(result);
    if (first_revision != inspection->expected_revision ||
        !moo_devtools_password_is_redacted(&first))
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    first_length = moo_devtools_build_trace(
        inspection->node, first_revision, &first, first_trace);
    result = moo_a11y_node_read(inspection->core,
        inspection->privileged_reader, inspection->node,
        &second, &second_revision);
    if (result != MOO_INPUT_OK) return moo_devtools_input_result(result);
    if (second_revision != first_revision ||
        !moo_devtools_password_is_redacted(&second))
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    second_length = moo_devtools_build_trace(
        inspection->node, second_revision, &second, second_trace);
    if (second_length != first_length ||
        memcmp(first_trace, second_trace, first_length) != 0)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    privacy_leaks = moo_devtools_trace_contains(
        first_trace, first_length, inspection->seeded_secret,
        inspection->seeded_secret_length);
    trace_hash = moo_devtools_trace_hash(first_trace, first_length);
    return moo_ui_host_parity_helper_record_devtools_trace(
        instrumentation, generation, inspection->node, first_revision,
        trace_hash, privacy_leaks);
}

MooUiHostParityResult moo_ui_host_parity_devtools_seal(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation) {
    return moo_ui_host_parity_helper_seal_devtools(
        instrumentation, generation);
}
