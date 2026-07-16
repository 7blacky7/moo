#ifndef MOO_COMPOSITOR_EFFECTS_GPU_PRESENTER_H
#define MOO_COMPOSITOR_EFFECTS_GPU_PRESENTER_H

#include <stddef.h>
#include <stdint.h>

#include "moo_compositor_effects_gpu.h"

/*
 * Ordered presenter quality contract. Higher values are strict supersets:
 * Q0 transfers unmodified CONTENT through an attested hardware GPU; Q1 adds
 * corner clipping and affine content; Q2 adds shadows; Q3 adds the complete
 * lower-z backdrop chain. Backends return UNSUPPORTED instead of substituting
 * CPU work or claiming a quality they did not execute.
 */
typedef enum {
    MOO_COMP_GPU_QUALITY_Q0_CONTENT = 0,
    MOO_COMP_GPU_QUALITY_Q1_GEOMETRY = 1,
    MOO_COMP_GPU_QUALITY_Q2_SHADOW = 2,
    MOO_COMP_GPU_QUALITY_Q3_BACKDROP = 3
} MooCompGpuQuality;

typedef struct {
    const uint8_t *pixels;
    size_t stride;
    uint32_t width;
    uint32_t height;
} MooCompGpuRgba8ConstView;

typedef struct {
    uint8_t *pixels;
    size_t stride;
    uint32_t width;
    uint32_t height;
} MooCompGpuRgba8View;

typedef struct {
    uint32_t requested_quality;
    uint32_t executed_quality;
    uint32_t used_hardware_adapter;
    uint32_t api_level;
    uint32_t vendor_id;
    uint32_t device_id;
    uint64_t graph_hash;
    uint64_t readback_hash;
    MooCompGpuSubmission submission;
} MooCompGpuExecutionReport;

#endif
