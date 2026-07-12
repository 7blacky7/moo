#ifndef MOO_INPUT_COMPOSITOR_BRIDGE_H
#define MOO_INPUT_COMPOSITOR_BRIDGE_H

#include "moo_compositor_core.h"
#include "moo_input_core.h"

/* Trusted policy bridge: the compositor proves that surface is live and owned
 * by compositor_client before an input target can reference it. */
MooInputResult moo_input_target_create_for_surface(
    MooInputCore *input, const MooCompositor *compositor,
    MooInputHandle input_client, MooCompHandle compositor_client,
    MooCompHandle surface, uint32_t text_mode,
    MooInputHandle *out_target);

#endif
