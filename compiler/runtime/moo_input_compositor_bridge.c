#include "moo_input_compositor_bridge.h"

MooInputResult moo_input_target_create_for_surface(
    MooInputCore *input, const MooCompositor *compositor,
    MooInputHandle input_client, MooCompHandle compositor_client,
    MooCompHandle surface, uint32_t text_mode,
    MooInputHandle *out_target) {
    MooCompSurfaceInfo info;
    MooCompResult comp_result;
    if (!input || !compositor || !out_target ||
        compositor_client == MOO_COMP_HANDLE_INVALID ||
        surface == MOO_COMP_HANDLE_INVALID)
        return MOO_INPUT_INVALID;
    comp_result = moo_comp_surface_info(compositor, surface, &info);
    if (comp_result == MOO_COMP_STALE_HANDLE)
        return MOO_INPUT_STALE_HANDLE;
    if (comp_result != MOO_COMP_OK)
        return MOO_INPUT_INVALID;
    if (info.owner != compositor_client)
        return MOO_INPUT_ACCESS;
    return moo_input_target_create_trusted(input, input_client,
                                            (uint64_t)surface,
                                            text_mode, out_target);
}
