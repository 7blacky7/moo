#include "moo_compositor_effects_gpu_d3d11.h"

#include <stdio.h>
#include <string.h>

#define W 17u
#define H 9u
#define ROW (W * 4u)
#define GUARD 32u

static int failures;
static unsigned checks;

#define CHECK(c, m) do { \
    if (c) { ++checks; } else { ++failures; printf("FAIL: %s\n", m); } \
} while (0)

static void
fill_pixels(uint8_t *pixels)
{
    uint32_t y;
    uint32_t x;
    for (y = 0u; y < H; ++y) {
        for (x = 0u; x < W; ++x) {
            size_t i = ((size_t)y * W + x) * 4u;
            pixels[i + 0u] = (uint8_t)(x * 13u + 1u);
            pixels[i + 1u] = (uint8_t)(y * 17u + 2u);
            pixels[i + 2u] = (uint8_t)((x ^ y) + 3u);
            pixels[i + 3u] = UINT8_C(255);
        }
    }
}

static MooCompResult
make_q0_graph(MooCompGpuGraph *graph, MooCompGpuPass *passes)
{
    MooCompGpuSurfaceInput input;
    memset(&input, 0, sizeof input);
    input.surface = UINT32_C(1);
    input.commit_sequence = UINT64_C(1);
    input.effective.fallback_policy = MOO_COMP_EFFECT_FALLBACK_REQUIRE;
    input.bounds.content_bounds = (MooCompRect){0, 0, (int32_t)W, (int32_t)H};
    input.bounds.visual_bounds = input.bounds.content_bounds;
    input.bounds.backdrop_sample_bounds = input.bounds.content_bounds;
    input.resources.content = UINT64_C(1);
    input.resources.output = UINT64_C(2);
    if (moo_comp_effects_gpu_graph_init(graph, passes, 4u) != MOO_COMP_OK)
        return MOO_COMP_BAD_STATE;
    return moo_comp_effects_gpu_graph_build(graph, &input);
}

int
main(void)
{
    uint8_t source_pixels[ROW * H];
    uint8_t guarded_output[GUARD + ROW * H + GUARD];
    uint8_t before[sizeof guarded_output];
    MooCompGpuPass passes[4];
    MooCompGpuGraph graph;
    MooCompGpuBackendState backend;
    MooCompGpuExecutionReport report;
    MooCompGpuExecutionReport report_before;
    MooCompGpuRgba8ConstView source;
    MooCompGpuRgba8View output;
    MooCompResult result;

    fill_pixels(source_pixels);
    memset(guarded_output, 0xa5, sizeof guarded_output);
    memcpy(before, guarded_output, sizeof before);
    memset(&report, 0x5a, sizeof report);
    report_before = report;
    source = (MooCompGpuRgba8ConstView){source_pixels, ROW, W, H};
    output = (MooCompGpuRgba8View){guarded_output + GUARD, ROW, W, H};

    CHECK(make_q0_graph(&graph, passes) == MOO_COMP_OK,
        "Q0 graph builds");
    CHECK(graph.count == 1u &&
        graph.passes[0].type == MOO_COMP_GPU_PASS_CONTENT,
        "Q0 graph is exactly CONTENT");
    CHECK(moo_comp_effects_gpu_backend_init(
        &backend, UINT64_C(1)) == MOO_COMP_OK, "backend init");

    result = moo_comp_effects_gpu_d3d11_execute_readback(
        &backend, &graph, MOO_COMP_GPU_QUALITY_Q0_CONTENT,
        &source, &output, &report);
#if defined(_WIN32)
    if (result == MOO_COMP_UNSUPPORTED) {
        puts("SKIP: no D3D11 hardware adapter");
        return 77;
    }
    CHECK(result == MOO_COMP_OK, "hardware Q0 execute/readback succeeds");
    CHECK(report.requested_quality == MOO_COMP_GPU_QUALITY_Q0_CONTENT &&
        report.executed_quality == MOO_COMP_GPU_QUALITY_Q0_CONTENT &&
        report.used_hardware_adapter == 1u,
        "report proves requested/executed Q0 hardware");
    CHECK(report.api_level >= UINT32_C(0xa000),
        "feature level is hardware D3D10+");
    CHECK(report.vendor_id != 0u && report.device_id != 0u,
        "hardware adapter identity is present");
    CHECK(report.graph_hash == graph.semantic_hash &&
        report.readback_hash != 0u, "report binds graph and readback hashes");
    CHECK(report.submission.serial == 1u &&
        backend.submitted_serial == 1u && backend.completed_serial == 1u,
        "submission lifecycle completes exactly once");
    CHECK(memcmp(source_pixels, output.pixels, ROW * H) == 0,
        "GPU readback pixels exactly match CPU source");
    CHECK(memcmp(guarded_output, before, GUARD) == 0 &&
        memcmp(guarded_output + GUARD + ROW * H,
            before + GUARD + ROW * H, GUARD) == 0,
        "readback preserves output guards");
    printf("P016 D3D11 GPU Q0 PROOF: vendor=%08x device=%08x feature=%08x graph=%016llx readback=%016llx\\n",
        (unsigned)report.vendor_id, (unsigned)report.device_id,
        (unsigned)report.api_level,
        (unsigned long long)report.graph_hash,
        (unsigned long long)report.readback_hash);

    memcpy(before, guarded_output, sizeof before);
    report_before = report;
    result = moo_comp_effects_gpu_d3d11_execute_readback(
        &backend, &graph, MOO_COMP_GPU_QUALITY_Q1_GEOMETRY,
        &source, &output, &report);
    CHECK(result == MOO_COMP_UNSUPPORTED,
        "Q1 is explicit unsupported, never a Q0 claim");
    CHECK(memcmp(guarded_output, before, sizeof before) == 0 &&
        memcmp(&report, &report_before, sizeof report) == 0 &&
        backend.submitted_serial == backend.completed_serial,
        "unsupported level changes no output/report/lifecycle");
#else
    CHECK(result == MOO_COMP_UNSUPPORTED,
        "non-Windows is explicit unsupported");
    CHECK(memcmp(guarded_output, before, sizeof before) == 0 &&
        memcmp(&report, &report_before, sizeof report) == 0,
        "non-Windows stub changes no output/report");
#endif

    if (failures != 0) {
        printf("P016 D3D11 GPU Q0 FAIL: %d/%u\n", failures, checks);
        return 1;
    }
    printf("P016 D3D11 GPU Q0 GREEN: %u\n", checks);
    return 0;
}
