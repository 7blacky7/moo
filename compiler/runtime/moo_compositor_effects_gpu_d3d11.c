#include "moo_compositor_effects_gpu_d3d11.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int
moo_d3d11_views_valid(const MooCompGpuRgba8ConstView *source,
    const MooCompGpuRgba8View *output, size_t *out_row_bytes,
    size_t *out_pixel_bytes)
{
    size_t row_bytes;
    size_t pixel_bytes;

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
    pixel_bytes = row_bytes * (size_t)source->height;
    *out_row_bytes = row_bytes;
    *out_pixel_bytes = pixel_bytes;
    return 1;
}

#if defined(_WIN32)

#define COBJMACROS
#include <d3d11.h>
#include <dxgi.h>

#define MOO_D3D11_HASH_OFFSET UINT64_C(1469598103934665603)
#define MOO_D3D11_HASH_PRIME UINT64_C(1099511628211)

static uint64_t
moo_d3d11_hash_bytes(const uint8_t *bytes, size_t count)
{
    uint64_t hash = MOO_D3D11_HASH_OFFSET;
    size_t i;
    for (i = 0u; i < count; ++i) {
        hash ^= bytes[i];
        hash *= MOO_D3D11_HASH_PRIME;
    }
    return hash == 0u ? UINT64_C(1) : hash;
}

static int
moo_d3d11_q0_graph_valid(const MooCompGpuGraph *graph,
    uint32_t width, uint32_t height, uint64_t *out_hash)
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
moo_d3d11_release_texture(ID3D11Texture2D **texture)
{
    if (*texture != NULL) {
        ID3D11Texture2D_Release(*texture);
        *texture = NULL;
    }
}

MooCompResult
moo_comp_effects_gpu_d3d11_execute_readback(
    MooCompGpuBackendState *backend,
    const MooCompGpuGraph *graph,
    MooCompGpuQuality requested_quality,
    const MooCompGpuRgba8ConstView *source,
    MooCompGpuRgba8View *output,
    MooCompGpuExecutionReport *out_report)
{
    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    MooCompGpuExecutionReport report = {0};
    MooCompGpuSubmission submission = {0};
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *input_texture = NULL;
    ID3D11Texture2D *output_texture = NULL;
    ID3D11Texture2D *staging_texture = NULL;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIAdapter1 *adapter1 = NULL;
    DXGI_ADAPTER_DESC1 adapter_desc;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SUBRESOURCE_DATA initial;
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;
    uint8_t *packed = NULL;
    size_t row_bytes;
    size_t pixel_bytes;
    uint64_t graph_hash;
    uint64_t fence;
    uint32_t y;
    HRESULT hr;
    MooCompResult result = MOO_COMP_BAD_STATE;
    int submission_reserved = 0;

    if (out_report == NULL ||
        !moo_d3d11_views_valid(source, output, &row_bytes, &pixel_bytes) ||
        backend == NULL ||
        (uint32_t)requested_quality >
            (uint32_t)MOO_COMP_GPU_QUALITY_Q3_BACKDROP ||
        source->stride > UINT_MAX)
        return MOO_COMP_INVALID;
    if (requested_quality != MOO_COMP_GPU_QUALITY_Q0_CONTENT)
        return MOO_COMP_UNSUPPORTED;
    if (!moo_d3d11_q0_graph_valid(
            graph, source->width, source->height, &graph_hash))
        return MOO_COMP_UNSUPPORTED;
    if (backend->status == MOO_COMP_GPU_BACKEND_LOST)
        return MOO_COMP_UNSUPPORTED;
    if (backend->status != MOO_COMP_GPU_BACKEND_READY)
        return MOO_COMP_BAD_STATE;
    if (backend->submitted_serial != backend->completed_serial)
        return MOO_COMP_WOULD_BLOCK;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0u,
        levels, (UINT)(sizeof levels / sizeof levels[0]), D3D11_SDK_VERSION,
        &device, &feature_level, &context);
    if (FAILED(hr))
        return MOO_COMP_UNSUPPORTED;

    memset(&adapter_desc, 0, sizeof adapter_desc);
    hr = ID3D11Device_QueryInterface(
        device, &IID_IDXGIDevice, (void **)&dxgi_device);
    if (SUCCEEDED(hr))
        hr = IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    if (SUCCEEDED(hr))
        hr = IDXGIAdapter_QueryInterface(
            adapter, &IID_IDXGIAdapter1, (void **)&adapter1);
    if (SUCCEEDED(hr))
        hr = IDXGIAdapter1_GetDesc1(adapter1, &adapter_desc);
    if (FAILED(hr))
        goto cleanup;
    if ((adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u ||
        (adapter_desc.VendorId == UINT32_C(0x1414) &&
         adapter_desc.DeviceId == UINT32_C(0x008c))) {
        /* Microsoft Basic Render can expose Flags==0 in virtual machines. */
        result = MOO_COMP_UNSUPPORTED;
        goto cleanup;
    }

    memset(&desc, 0, sizeof desc);
    desc.Width = source->width;
    desc.Height = source->height;
    desc.MipLevels = 1u;
    desc.ArraySize = 1u;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1u;
    desc.Usage = D3D11_USAGE_DEFAULT;

    initial.pSysMem = source->pixels;
    initial.SysMemPitch = (UINT)source->stride;
    initial.SysMemSlicePitch = 0u;
    hr = ID3D11Device_CreateTexture2D(
        device, &desc, &initial, &input_texture);
    if (FAILED(hr))
        goto cleanup;
    hr = ID3D11Device_CreateTexture2D(
        device, &desc, NULL, &output_texture);
    if (FAILED(hr))
        goto cleanup;

    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(
        device, &desc, NULL, &staging_texture);
    if (FAILED(hr))
        goto cleanup;

    ID3D11DeviceContext_CopyResource(
        context, (ID3D11Resource *)output_texture,
        (ID3D11Resource *)input_texture);
    ID3D11DeviceContext_CopyResource(
        context, (ID3D11Resource *)staging_texture,
        (ID3D11Resource *)output_texture);

    fence = graph_hash ^ (backend->generation << 1u) ^
        (backend->submitted_serial + 1u);
    if (fence == MOO_COMP_GPU_FENCE_INVALID)
        fence = UINT64_C(1);
    result = moo_comp_effects_gpu_reserve_submission(
        backend, graph, fence, &submission);
    if (result != MOO_COMP_OK)
        goto cleanup;
    submission_reserved = 1;

    ID3D11DeviceContext_Flush(context);
    memset(&mapped, 0, sizeof mapped);
    hr = ID3D11DeviceContext_Map(
        context, (ID3D11Resource *)staging_texture, 0u,
        D3D11_MAP_READ, 0u, &mapped);
    if (FAILED(hr)) {
        (void)moo_comp_effects_gpu_backend_mark_lost(backend);
        result = MOO_COMP_BAD_STATE;
        goto cleanup;
    }

    packed = (uint8_t *)malloc(pixel_bytes);
    if (packed == NULL) {
        ID3D11DeviceContext_Unmap(
            context, (ID3D11Resource *)staging_texture, 0u);
        (void)moo_comp_effects_gpu_backend_mark_lost(backend);
        result = MOO_COMP_LIMIT;
        goto cleanup;
    }
    for (y = 0u; y < source->height; ++y) {
        memcpy(packed + (size_t)y * row_bytes,
            (const uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch,
            row_bytes);
    }
    ID3D11DeviceContext_Unmap(
        context, (ID3D11Resource *)staging_texture, 0u);

    result = moo_comp_effects_gpu_complete_submission(backend, &submission);
    if (result != MOO_COMP_OK)
        goto cleanup;
    submission_reserved = 0;

    for (y = 0u; y < output->height; ++y) {
        memcpy(output->pixels + (size_t)y * output->stride,
            packed + (size_t)y * row_bytes, row_bytes);
    }
    report.requested_quality = (uint32_t)requested_quality;
    report.executed_quality = MOO_COMP_GPU_QUALITY_Q0_CONTENT;
    report.used_hardware_adapter = 1u;
    report.api_level = (uint32_t)feature_level;
    report.vendor_id = adapter_desc.VendorId;
    report.device_id = adapter_desc.DeviceId;
    report.graph_hash = graph_hash;
    report.readback_hash = moo_d3d11_hash_bytes(packed, pixel_bytes);
    report.submission = submission;
    *out_report = report;
    result = MOO_COMP_OK;

cleanup:
    if (submission_reserved != 0 &&
        backend->status == MOO_COMP_GPU_BACKEND_READY)
        (void)moo_comp_effects_gpu_backend_mark_lost(backend);
    free(packed);
    moo_d3d11_release_texture(&staging_texture);
    moo_d3d11_release_texture(&output_texture);
    moo_d3d11_release_texture(&input_texture);
    if (adapter1 != NULL)
        IDXGIAdapter1_Release(adapter1);
    if (adapter != NULL)
        IDXGIAdapter_Release(adapter);
    if (dxgi_device != NULL)
        IDXGIDevice_Release(dxgi_device);
    if (context != NULL)
        ID3D11DeviceContext_Release(context);
    if (device != NULL)
        ID3D11Device_Release(device);
    return result;
}

#else

MooCompResult
moo_comp_effects_gpu_d3d11_execute_readback(
    MooCompGpuBackendState *backend,
    const MooCompGpuGraph *graph,
    MooCompGpuQuality requested_quality,
    const MooCompGpuRgba8ConstView *source,
    MooCompGpuRgba8View *output,
    MooCompGpuExecutionReport *out_report)
{
    size_t row_bytes;
    size_t pixel_bytes;

    if (out_report == NULL ||
        !moo_d3d11_views_valid(source, output, &row_bytes, &pixel_bytes) ||
        backend == NULL ||
        (uint32_t)requested_quality >
            (uint32_t)MOO_COMP_GPU_QUALITY_Q3_BACKDROP)
        return MOO_COMP_INVALID;
    (void)row_bytes;
    (void)pixel_bytes;
    (void)graph;
    return MOO_COMP_UNSUPPORTED;
}

#endif
