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
    MooInputHandle client;
    MooInputHandle target;

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
    CHECK(moo_comp_surface_destroy(&comp, owner, surface) == MOO_COMP_OK,
          "surface destroy");
    CHECK(moo_input_target_create_for_surface(&input, &comp, client, owner,
          surface, 0u, &target) == MOO_INPUT_STALE_HANDLE,
          "stale surface denied");
    puts("P016-O4-COMPOSITOR-BRIDGE-OK");
    return 0;
}
