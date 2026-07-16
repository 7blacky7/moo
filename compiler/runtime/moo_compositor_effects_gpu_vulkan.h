#ifndef MOO_COMPOSITOR_EFFECTS_GPU_VULKAN_H
#define MOO_COMPOSITOR_EFFECTS_GPU_VULKAN_H

#include "moo_compositor_effects_gpu_presenter.h"

/*
 * Hardware-attested Vulkan Q0 executor/readback.
 *
 * Q0 accepts exactly one unflagged full-surface CONTENT pass. The path packs
 * RGBA8888 rows, uploads once to a resident Vulkan buffer, executes one
 * copy.comp queue submission, downloads once, and requires exact telemetry
 * (1 upload, 1 submit, 1 download, 0 fallbacks). CPU/VIRTUAL/OTHER Vulkan
 * devices and Q1-Q3 are UNSUPPORTED. Output/report stay unchanged on failure.
 */
MooCompResult moo_comp_effects_gpu_vulkan_execute_readback(
    MooCompGpuBackendState *backend,
    const MooCompGpuGraph *graph,
    MooCompGpuQuality requested_quality,
    const MooCompGpuRgba8ConstView *source,
    MooCompGpuRgba8View *output,
    MooCompGpuExecutionReport *out_report);

#endif
