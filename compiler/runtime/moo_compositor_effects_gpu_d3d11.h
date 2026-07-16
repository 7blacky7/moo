#ifndef MOO_COMPOSITOR_EFFECTS_GPU_D3D11_H
#define MOO_COMPOSITOR_EFFECTS_GPU_D3D11_H

#include "moo_compositor_effects_gpu_presenter.h"

/*
 * Windows D3D11 hardware-only executor/readback foundation.
 *
 * The current implementation truthfully supports Q0 only: exactly one
 * unflagged CONTENT pass covering the complete RGBA8888 input. It uploads the
 * source into a DEFAULT GPU texture, enqueues GPU CopyResource work, copies to
 * a STAGING texture, and maps that texture for readback. WARP/reference and
 * CPU emulation are never selected. Q1-Q3 return MOO_COMP_UNSUPPORTED.
 *
 * The output view and report are unchanged on every non-OK result. On
 * non-Windows hosts this function is an explicit UNSUPPORTED stub.
 */
MooCompResult moo_comp_effects_gpu_d3d11_execute_readback(
    MooCompGpuBackendState *backend,
    const MooCompGpuGraph *graph,
    MooCompGpuQuality requested_quality,
    const MooCompGpuRgba8ConstView *source,
    MooCompGpuRgba8View *output,
    MooCompGpuExecutionReport *out_report);

#endif
