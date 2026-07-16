#include "../moo_input_compositor_bridge.h"

#include <stdio.h>

#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL:%d:%s\n", __LINE__, (m)); return 1; } } while (0)

int main(void) {
    MooCompositor comp;
    MooCompClientSlot comp_clients[3];
    MooCompSurfaceSlot surfaces[4];
    MooCompBufferSlot buffers[2];
    MooCompFrameSlot frames[3];
    MooCompEventSlot comp_events[8];
    MooCompConfig cc = {64, 64, 0, 0, 0, 255};
    MooInputCore input;
    MooInputClientSlot input_clients[2];
    MooInputTargetSlot targets[4];
    MooInputEventSlot events[8];
    MooA11yNodeSlot nodes[4];
    MooInputConfig ic = {8u, 4u, MOO_INPUT_FEATURE_POINTER};
    MooCompHandle owner;
    MooCompHandle foreign;
    MooCompHandle surface;
    MooCompHandle replacement;
    MooInputHandle client;
    MooInputHandle target;
    MooInputHandle bound_target;

    CHECK(moo_comp_init(&comp, &cc, comp_clients, 3u, surfaces, 4u,
          buffers, 2u, frames, 3u, comp_events, 8u) == MOO_COMP_OK, "comp init");
    CHECK(moo_comp_client_create(&comp, &owner) == MOO_COMP_OK, "owner");
    CHECK(moo_comp_client_create(&comp, &foreign) == MOO_COMP_OK, "foreign");
    CHECK(moo_comp_surface_create(&comp, owner, &surface) == MOO_COMP_OK,
          "surface");
    CHECK(moo_input_init(&input, &ic, input_clients, 2u, targets, 4u,
          events, 8u, nodes, 4u) == MOO_INPUT_OK, "input init");
    CHECK(moo_input_client_create(&input, 0u, &client) == MOO_INPUT_OK,
          "input client");
    CHECK(moo_input_target_create_for_surface(&input, &comp, client, foreign,
          surface, 0u, &target) == MOO_INPUT_ACCESS, "foreign surface denied");
    CHECK(moo_input_target_create_for_surface(&input, &comp, client, owner,
          surface, 0u, &target) == MOO_INPUT_OK, "owned surface accepted");
    bound_target = target;
    CHECK(moo_comp_surface_destroy(&comp, owner, surface) == MOO_COMP_OK,
          "surface destroy");
    CHECK(moo_input_target_destroy(&input, client, bound_target,
          UINT64_C(1)) == MOO_INPUT_OK,
          "host lifecycle destroys bound input target");
    CHECK(moo_input_target_create_for_surface(&input, &comp, client, owner,
          surface, 0u, &target) == MOO_INPUT_STALE_HANDLE,
          "stale surface denied");
    CHECK(moo_comp_surface_create(&comp, owner, &replacement) == MOO_COMP_OK,
          "replacement surface");
    CHECK((uint32_t)replacement == (uint32_t)surface &&
          (uint32_t)(replacement >> 32u) != (uint32_t)(surface >> 32u),
          "reused surface slot advances generation");
    CHECK(moo_input_target_create_for_surface(&input, &comp, client, foreign,
          replacement, 0u, &target) == MOO_INPUT_ACCESS,
          "replacement preserves owner isolation");
    CHECK(moo_input_pointer_motion(&input, bound_target, 1, 1, 0, 0,
          UINT64_C(2), UINT64_C(2)) == MOO_INPUT_STALE_HANDLE,
          "explicitly destroyed bound input target is stale");
    puts("P016-O4-COMPOSITOR-BRIDGE-OK");
    return 0;
}
