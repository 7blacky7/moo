#ifndef MOO_UI_HOST_PARITY_DEVTOOLS_H
#define MOO_UI_HOST_PARITY_DEVTOOLS_H

#include "moo_input_core.h"
#include "moo_ui_host_parity_instrumentation.h"

typedef struct {
    const MooInputCore *core;
    MooInputHandle privileged_reader;
    MooInputHandle node;
    uint64_t expected_revision;
    const uint8_t *seeded_secret;
    uint32_t seeded_secret_length;
} MooUiHostParityDevtoolsInspection;

MooUiHostParityResult moo_ui_host_parity_devtools_inspect_password_node(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    const MooUiHostParityDevtoolsInspection *inspection);
MooUiHostParityResult moo_ui_host_parity_devtools_seal(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation);

#endif
