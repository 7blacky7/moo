#include "moo_ui_host_parity_win32_dwrite.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwrite.h>

static const IID moo_iid_idwrite_factory = {
    0xb859ee5a, 0xd838, 0x4b5b,
    {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}
};

static UINT32 moo_typography_length(const WCHAR *text) {
    UINT32 length = 0u;
    while (text[length] != L'\0' && length < 1024u) ++length;
    return length;
}

static uint64_t moo_typography_milli_diff(float left, float right) {
    const float difference = left >= right ? left - right : right - left;
    return static_cast<uint64_t>(difference * 1000.0f + 0.5f);
}

static HRESULT moo_typography_last_error(void) {
    const DWORD error = GetLastError();
    return error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(error);
}

extern "C" MooUiHostParityResult
moo_ui_host_parity_win32_measure_typography(
    MooUiHostParityMeasurement *measurement) {
    static const WCHAR *const samples[] = {
        L"Moo UI", L"Hamburgefontsiv 0123456789", L"Gr\u00f6\u00dfe"
    };
    static const int pixel_sizes[] = {12, 16, 24};
    IDWriteFactory *factory = nullptr;
    IDWriteFontCollection *collection = nullptr;
    IDWriteFontFamily *family = nullptr;
    IDWriteFont *font = nullptr;
    IDWriteFontFace *face = nullptr;
    IDWriteTextFormat *format = nullptr;
    IDWriteTextLayout *layout = nullptr;
    HDC device = nullptr;
    HFONT selected_font = nullptr;
    HGDIOBJ previous_font = nullptr;
    HRESULT initialized;
    HRESULT result;
    UINT32 family_index = 0u;
    BOOL family_exists = FALSE;
    bool co_uninitialize = false;
    uint64_t max_baseline_error = 0u;
    uint64_t max_advance_error = 0u;
    uint64_t missing_glyphs = 0u;
    float pixels_per_dip;
    if (measurement == nullptr) return MOO_UI_HOST_PARITY_RESULT_INVALID;
    initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(initialized)) {
        co_uninitialize = true;
    } else if (initialized != RPC_E_CHANGED_MODE) {
        measurement->native_error = static_cast<int32_t>(initialized);
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        moo_iid_idwrite_factory, reinterpret_cast<IUnknown **>(&factory));
    if (FAILED(result)) goto unavailable;
    result = factory->GetSystemFontCollection(&collection, FALSE);
    if (FAILED(result)) goto unavailable;
    result = collection->FindFamilyName(
        L"Segoe UI", &family_index, &family_exists);
    if (FAILED(result) || family_exists == FALSE) goto unavailable;
    result = collection->GetFontFamily(family_index, &family);
    if (FAILED(result)) goto unavailable;
    result = family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    if (FAILED(result)) goto unavailable;
    result = font->CreateFontFace(&face);
    if (FAILED(result)) goto unavailable;
    device = CreateCompatibleDC(nullptr);
    if (device == nullptr) {
        result = moo_typography_last_error();
        goto unavailable;
    }
    pixels_per_dip =
        static_cast<float>(GetDeviceCaps(device, LOGPIXELSX)) / 96.0f;
    if (pixels_per_dip <= 0.0f) {
        result = E_FAIL;
        goto unavailable;
    }
    for (UINT32 size_index = 0u; size_index < 3u; ++size_index) {
        TEXTMETRICW text_metrics;
        WCHAR selected_face[64];
        const int pixel_size = pixel_sizes[size_index];
        selected_font = CreateFontW(-pixel_size, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
            L"Segoe UI");
        if (selected_font == nullptr) {
            result = moo_typography_last_error();
            goto unavailable;
        }
        previous_font = SelectObject(device, selected_font);
        if (previous_font == nullptr ||
            GetTextMetricsW(device, &text_metrics) == FALSE)
            goto gdi_error;
        if (GetTextFaceW(device, 64, selected_face) <= 0 ||
            lstrcmpiW(selected_face, L"Segoe UI") != 0)
            ++missing_glyphs;
        result = factory->CreateTextFormat(L"Segoe UI", collection,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            static_cast<FLOAT>(pixel_size) / pixels_per_dip,
            L"en-us", &format);
        if (FAILED(result)) goto unavailable;
        for (UINT32 sample_index = 0u; sample_index < 3u; ++sample_index) {
            const WCHAR *text = samples[sample_index];
            const UINT32 length = moo_typography_length(text);
            SIZE gdi_extent;
            DWRITE_TEXT_METRICS metrics;
            DWRITE_LINE_METRICS line;
            UINT32 line_count = 0u;
            if (GetTextExtentPoint32W(device, text,
                    static_cast<int>(length), &gdi_extent) == FALSE)
                goto gdi_error;
            result = factory->CreateGdiCompatibleTextLayout(text, length,
                format, 4096.0f, 512.0f, pixels_per_dip, nullptr,
                FALSE, &layout);
            if (FAILED(result)) goto unavailable;
            result = layout->GetMetrics(&metrics);
            if (FAILED(result)) goto unavailable;
            result = layout->GetLineMetrics(&line, 1u, &line_count);
            if (FAILED(result) || line_count != 1u) goto unavailable;
            uint64_t difference = moo_typography_milli_diff(
                static_cast<float>(text_metrics.tmAscent),
                line.baseline * pixels_per_dip);
            if (difference > max_baseline_error)
                max_baseline_error = difference;
            difference = moo_typography_milli_diff(
                static_cast<float>(gdi_extent.cx),
                metrics.widthIncludingTrailingWhitespace * pixels_per_dip);
            if (difference > max_advance_error)
                max_advance_error = difference;
            for (UINT32 character = 0u; character < length; ++character) {
                const UINT32 codepoint =
                    static_cast<UINT32>(static_cast<uint16_t>(text[character]));
                WORD gdi_glyph = 0u;
                UINT16 dwrite_glyph = 0u;
                if (GetGlyphIndicesW(device, &text[character], 1,
                        &gdi_glyph, GGI_MARK_NONEXISTING_GLYPHS) != 1u ||
                    FAILED(face->GetGlyphIndices(
                        &codepoint, 1u, &dwrite_glyph)) ||
                    gdi_glyph == static_cast<WORD>(0xffffu) ||
                    dwrite_glyph == 0u)
                    ++missing_glyphs;
            }
            layout->Release();
            layout = nullptr;
        }
        format->Release();
        format = nullptr;
        (void)SelectObject(device, previous_font);
        previous_font = nullptr;
        DeleteObject(selected_font);
        selected_font = nullptr;
    }
    measurement->sample_count = 9u;
    measurement->value_a = max_baseline_error;
    measurement->value_b = max_advance_error;
    measurement->value_c = missing_glyphs;
    measurement->evidence =
        max_baseline_error <=
                static_cast<uint64_t>(
                    MOO_UI_HOST_PARITY_TYPOGRAPHY_MAX_ERROR_MILLI_PX) &&
            max_advance_error <=
                static_cast<uint64_t>(
                    MOO_UI_HOST_PARITY_TYPOGRAPHY_MAX_ERROR_MILLI_PX) &&
            missing_glyphs == 0u
        ? static_cast<uint32_t>(MOO_UI_HOST_PARITY_EVIDENCE_PASS)
        : static_cast<uint32_t>(MOO_UI_HOST_PARITY_EVIDENCE_FAIL);
    result = S_OK;
    goto cleanup;

gdi_error:
    result = moo_typography_last_error();
unavailable:
    measurement->native_error = static_cast<int32_t>(result);
cleanup:
    if (layout != nullptr) layout->Release();
    if (format != nullptr) format->Release();
    if (previous_font != nullptr) (void)SelectObject(device, previous_font);
    if (selected_font != nullptr) DeleteObject(selected_font);
    if (device != nullptr) DeleteDC(device);
    if (face != nullptr) face->Release();
    if (font != nullptr) font->Release();
    if (family != nullptr) family->Release();
    if (collection != nullptr) collection->Release();
    if (factory != nullptr) factory->Release();
    if (co_uninitialize) CoUninitialize();
    return result == S_OK
        ? MOO_UI_HOST_PARITY_RESULT_OK
        : MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
}
#else
extern "C" MooUiHostParityResult
moo_ui_host_parity_win32_measure_typography(
    MooUiHostParityMeasurement *measurement) {
    (void)measurement;
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
}
#endif
