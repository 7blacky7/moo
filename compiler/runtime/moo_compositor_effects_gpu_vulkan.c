#include "moo_compositor_effects_gpu_vulkan.h"

#include "moo_ki_gpu_api.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MOO_VK_PRESENT_HASH_OFFSET UINT64_C(1469598103934665603)
#define MOO_VK_PRESENT_HASH_PRIME UINT64_C(1099511628211)

static uint64_t
moo_vk_present_hash(const uint8_t *bytes, size_t count)
{
    uint64_t hash = MOO_VK_PRESENT_HASH_OFFSET;
    size_t i;
    for (i = 0u; i < count; ++i) {
        hash ^= bytes[i];
        hash *= MOO_VK_PRESENT_HASH_PRIME;
    }
    return hash == 0u ? UINT64_C(1) : hash;
}

static int
moo_vk_present_views_valid(const MooCompGpuRgba8ConstView *source,
    const MooCompGpuRgba8View *output, size_t *out_row_bytes,
    size_t *out_pixel_bytes)
{
    size_t row_bytes;
    if (source == NULL || output == NULL || out_row_bytes == NULL ||
        out_pixel_bytes == NULL || source->pixels == NULL ||
        output->pixels == NULL || source->width == 0u || source->height == 0u ||
        source->width != output->width || source->height != output->height)
        return 0;
    row_bytes = (size_t)source->width;
    if (row_bytes > SIZE_MAX / 4u)
        return 0;
    row_bytes *= 4u;
    if (source->stride < row_bytes || output->stride < row_bytes ||
        (size_t)source->height > SIZE_MAX / row_bytes)
        return 0;
    *out_row_bytes = row_bytes;
    *out_pixel_bytes = row_bytes * (size_t)source->height;
    return 1;
}

static int
moo_vk_present_q0_graph(const MooCompGpuGraph *graph, uint32_t width,
    uint32_t height, uint64_t *out_hash)
{
    const MooCompGpuPass *pass;
    uint64_t hash;
    if (graph == NULL || out_hash == NULL)
        return 0;
    hash = moo_comp_effects_gpu_graph_hash(graph);
    if (hash == 0u || hash != graph->semantic_hash || graph->count != 1u)
        return 0;
    pass = &graph->passes[0];
    if (pass->type != MOO_COMP_GPU_PASS_CONTENT || pass->flags != 0u ||
        pass->bounds.x != 0 || pass->bounds.y != 0 ||
        pass->bounds.width < 0 || pass->bounds.height < 0 ||
        (uint32_t)pass->bounds.width != width ||
        (uint32_t)pass->bounds.height != height)
        return 0;
    *out_hash = hash;
    return 1;
}

static void
moo_vk_present_mark_lost(MooCompGpuBackendState *backend)
{
    if (backend != NULL &&
        backend->status == MOO_COMP_GPU_BACKEND_READY)
        (void)moo_comp_effects_gpu_backend_mark_lost(backend);
}

MooCompResult
moo_comp_effects_gpu_vulkan_execute_readback(
    MooCompGpuBackendState *backend,
    const MooCompGpuGraph *graph,
    MooCompGpuQuality requested_quality,
    const MooCompGpuRgba8ConstView *source,
    MooCompGpuRgba8View *output,
    MooCompGpuExecutionReport *out_report)
{
    MooKiGpuDeviceInfo device;
    MooKiGpuTelemetrie telemetry;
    MooCompGpuExecutionReport report = {0};
    MooCompGpuSubmission submission;
    uint8_t *packed_source = NULL;
    uint8_t *packed_output = NULL;
    void *gpu_source = NULL;
    void *gpu_output = NULL;
    size_t row_bytes;
    size_t pixel_bytes;
    int64_t gpu_bytes;
    int64_t words;
    uint64_t graph_hash;
    uint64_t fence;
    uint32_t y;
    MooCompResult result = MOO_COMP_BAD_STATE;

    if (out_report == NULL ||
        !moo_vk_present_views_valid(
            source, output, &row_bytes, &pixel_bytes) ||
        backend == NULL ||
        (uint32_t)requested_quality >
            (uint32_t)MOO_COMP_GPU_QUALITY_Q3_BACKDROP)
        return MOO_COMP_INVALID;
    if (requested_quality != MOO_COMP_GPU_QUALITY_Q0_CONTENT)
        return MOO_COMP_UNSUPPORTED;
    if (!moo_vk_present_q0_graph(
            graph, source->width, source->height, &graph_hash))
        return MOO_COMP_UNSUPPORTED;
    if (backend->status == MOO_COMP_GPU_BACKEND_LOST)
        return MOO_COMP_UNSUPPORTED;
    if (backend->status != MOO_COMP_GPU_BACKEND_READY)
        return MOO_COMP_BAD_STATE;
    if (backend->submitted_serial != backend->completed_serial)
        return MOO_COMP_WOULD_BLOCK;
    if (pixel_bytes > (size_t)INT64_MAX || (pixel_bytes & 3u) != 0u)
        return MOO_COMP_LIMIT;
    if (!moo_ki_gpu_device_info(&device) ||
        (device.device_kind != MOO_KI_GPU_DEVICE_DISCRETE &&
         device.device_kind != MOO_KI_GPU_DEVICE_INTEGRATED) ||
        device.vendor_id == 0u || device.device_id == 0u)
        return MOO_COMP_UNSUPPORTED;

    gpu_bytes = (int64_t)pixel_bytes;
    words = gpu_bytes / 4;
    packed_source = (uint8_t *)malloc(pixel_bytes);
    packed_output = (uint8_t *)malloc(pixel_bytes);
    if (packed_source == NULL || packed_output == NULL) {
        result = MOO_COMP_LIMIT;
        goto cleanup;
    }
    for (y = 0u; y < source->height; ++y) {
        memcpy(packed_source + (size_t)y * row_bytes,
            source->pixels + (size_t)y * source->stride, row_bytes);
    }

    gpu_source = moo_ki_gpu_buf_belegen(gpu_bytes);
    gpu_output = moo_ki_gpu_buf_belegen(gpu_bytes);
    if (gpu_source == NULL || gpu_output == NULL) {
        result = MOO_COMP_UNSUPPORTED;
        goto cleanup;
    }

    moo_ki_gpu_telemetrie_reset();
    if (!moo_ki_gpu_upload(
            gpu_source, (const float *)(const void *)packed_source, gpu_bytes) ||
        !moo_ki_gpu_copy_res(
            gpu_source, gpu_output, words, INT64_C(0), INT64_C(0)) ||
        !moo_ki_gpu_download(
            gpu_output, (float *)(void *)packed_output, gpu_bytes)) {
        moo_vk_present_mark_lost(backend);
        result = MOO_COMP_BAD_STATE;
        goto cleanup;
    }
    moo_ki_gpu_telemetrie(&telemetry);
    if (telemetry.uploads != UINT64_C(1) ||
        telemetry.submits != UINT64_C(1) ||
        telemetry.downloads != UINT64_C(1) ||
        telemetry.cpu_fallbacks != UINT64_C(0)) {
        moo_vk_present_mark_lost(backend);
        result = MOO_COMP_BAD_STATE;
        goto cleanup;
    }

    fence = graph_hash ^ (backend->generation << 1u) ^
        (backend->submitted_serial + 1u);
    if (fence == MOO_COMP_GPU_FENCE_INVALID)
        fence = UINT64_C(1);
    result = moo_comp_effects_gpu_reserve_submission(
        backend, graph, fence, &submission);
    if (result != MOO_COMP_OK)
        goto cleanup;
    result = moo_comp_effects_gpu_complete_submission(backend, &submission);
    if (result != MOO_COMP_OK) {
        moo_vk_present_mark_lost(backend);
        goto cleanup;
    }

    for (y = 0u; y < output->height; ++y) {
        memcpy(output->pixels + (size_t)y * output->stride,
            packed_output + (size_t)y * row_bytes, row_bytes);
    }
    report.requested_quality = (uint32_t)requested_quality;
    report.executed_quality = MOO_COMP_GPU_QUALITY_Q0_CONTENT;
    report.used_hardware_adapter = 1u;
    report.api_level = device.api_version;
    report.vendor_id = device.vendor_id;
    report.device_id = device.device_id;
    report.graph_hash = graph_hash;
    report.readback_hash = moo_vk_present_hash(packed_output, pixel_bytes);
    report.submission = submission;
    *out_report = report;
    result = MOO_COMP_OK;

cleanup:
    moo_ki_gpu_buf_freigeben(gpu_output);
    moo_ki_gpu_buf_freigeben(gpu_source);
    free(packed_output);
    free(packed_source);
    return result;
}
