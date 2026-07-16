#include "moo_ui_host_parity_clipboard_win32.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <new>
#include <string.h>
#include <wchar.h>

namespace {
static void zero_metrics(MooUiHostParityClipboardWorkerMetrics *m) {
    m->clipboard_roundtrips = 0u;
    m->dragdrop_sequences = 0u;
    m->integrity_mismatches = 0u;
    m->native_error = 0;
}
static bool has_prefix(const wchar_t *v, const wchar_t *p) {
    if (v == nullptr || p == nullptr) return false;
    return wcsncmp(v, p, wcslen(p)) == 0;
}
static bool object_name(HANDLE object, wchar_t *buffer, DWORD capacity) {
    DWORD bytes = 0u;
    if (object == nullptr || buffer == nullptr || capacity < 2u) return false;
    buffer[0] = L'\0';
    if (GetUserObjectInformationW(object, UOI_NAME, buffer,
            capacity * static_cast<DWORD>(sizeof(wchar_t)), &bytes) == FALSE)
        return false;
    buffer[capacity - 1u] = L'\0';
    return bytes > sizeof(wchar_t);
}
static bool isolated_context(
    const MooUiHostParityClipboardWorkerConfig *config) {
    wchar_t station_name[128];
    wchar_t desktop_name[128];
    if (!has_prefix(config->expected_window_station, L"MooParity-") ||
        !has_prefix(config->expected_desktop, L"MooParityDesk-"))
        return false;
    if (!object_name(GetProcessWindowStation(), station_name, 128u) ||
        !object_name(GetThreadDesktop(GetCurrentThreadId()),
            desktop_name, 128u))
        return false;
    return wcscmp(station_name, config->expected_window_station) == 0 &&
        wcscmp(desktop_name, config->expected_desktop) == 0 &&
        wcscmp(station_name, L"WinSta0") != 0 &&
        wcscmp(desktop_name, L"Default") != 0;
}

class DataObject final : public IDataObject {
public:
    DataObject(UINT format, const uint8_t *payload, uint32_t length)
        : refs_(1), format_(format), length_(length) {
        memcpy(payload_, payload, length);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID iid, void **out) override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_IDataObject)) {
            *out = static_cast<IDataObject *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG value = InterlockedDecrement(&refs_);
        if (value == 0) delete this;
        return static_cast<ULONG>(value);
    }
    HRESULT STDMETHODCALLTYPE GetData(
        FORMATETC *format, STGMEDIUM *medium) override {
        HGLOBAL global;
        uint8_t *bytes;
        uint32_t total;
        if (format == nullptr || medium == nullptr) return E_POINTER;
        if (format->cfFormat != format_ ||
            (format->tymed & TYMED_HGLOBAL) == 0u ||
            format->dwAspect != DVASPECT_CONTENT ||
            format->lindex != -1)
            return DV_E_FORMATETC;
        total = static_cast<uint32_t>(sizeof(uint32_t)) + length_;
        global = GlobalAlloc(GMEM_MOVEABLE, total);
        if (global == nullptr) return STG_E_MEDIUMFULL;
        bytes = static_cast<uint8_t *>(GlobalLock(global));
        if (bytes == nullptr) {
            GlobalFree(global);
            return STG_E_MEDIUMFULL;
        }
        bytes[0] = static_cast<uint8_t>(length_);
        bytes[1] = static_cast<uint8_t>(length_ >> 8u);
        bytes[2] = static_cast<uint8_t>(length_ >> 16u);
        bytes[3] = static_cast<uint8_t>(length_ >> 24u);
        memcpy(bytes + sizeof(uint32_t), payload_, length_);
        GlobalUnlock(global);
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = global;
        medium->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(
        FORMATETC *, STGMEDIUM *) override {
        return DATA_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *format) override {
        if (format == nullptr) return E_POINTER;
        return format->cfFormat == format_ &&
            (format->tymed & TYMED_HGLOBAL) != 0u &&
            format->dwAspect == DVASPECT_CONTENT &&
            format->lindex == -1 ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(
        FORMATETC *, FORMATETC *output) override {
        if (output == nullptr) return E_POINTER;
        output->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }
    HRESULT STDMETHODCALLTYPE SetData(
        FORMATETC *, STGMEDIUM *, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(
        DWORD, IEnumFORMATETC **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(
        FORMATETC *, DWORD, IAdviseSink *, DWORD *) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
private:
    ~DataObject() = default;
    LONG refs_;
    UINT format_;
    uint32_t length_;
    uint8_t payload_[MOO_UI_HOST_PARITY_CLIPBOARD_MAX_PAYLOAD];
};

static bool medium_matches(
    const STGMEDIUM *medium, const uint8_t *payload, uint32_t length) {
    const uint8_t *bytes;
    SIZE_T size;
    uint32_t encoded;
    bool equal;
    if (medium == nullptr || medium->tymed != TYMED_HGLOBAL ||
        medium->hGlobal == nullptr)
        return false;
    size = GlobalSize(medium->hGlobal);
    bytes = static_cast<const uint8_t *>(GlobalLock(medium->hGlobal));
    if (bytes == nullptr) return false;
    equal = false;
    if (size >= sizeof(uint32_t)) {
        encoded = static_cast<uint32_t>(bytes[0]) |
            (static_cast<uint32_t>(bytes[1]) << 8u) |
            (static_cast<uint32_t>(bytes[2]) << 16u) |
            (static_cast<uint32_t>(bytes[3]) << 24u);
        equal = encoded == length &&
            size == static_cast<SIZE_T>(sizeof(uint32_t) + length) &&
            memcmp(bytes + sizeof(uint32_t), payload, length) == 0;
    }
    GlobalUnlock(medium->hGlobal);
    return equal;
}

#if defined(MOO_UI_HOST_PARITY_TESTING)
extern "C" int moo_ui_host_parity_clipboard_win32_test_medium_matches(
    uintptr_t global_handle, const uint8_t *payload, uint32_t length) {
    STGMEDIUM medium;
    if (global_handle == static_cast<uintptr_t>(0) || payload == nullptr ||
        length == 0u ||
        length > MOO_UI_HOST_PARITY_CLIPBOARD_MAX_PAYLOAD)
        return 0;
    ZeroMemory(&medium, sizeof(medium));
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = reinterpret_cast<HGLOBAL>(global_handle);
    return medium_matches(&medium, payload, length) ? 1 : 0;
}
#endif

static LRESULT CALLBACK drag_window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(window, message, wparam, lparam);
}

class DropSource final : public IDropSource {
public:
    DropSource() : refs_(1), queries_(0u) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID iid, void **out) override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_IDropSource)) {
            *out = static_cast<IDropSource *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG value = InterlockedDecrement(&refs_);
        if (value == 0) delete this;
        return static_cast<ULONG>(value);
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(
        BOOL escape_pressed, DWORD) override {
        ++queries_;
        return escape_pressed != FALSE
            ? DRAGDROP_S_CANCEL : DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
    uint32_t queries() const { return queries_; }
private:
    ~DropSource() = default;
    LONG refs_;
    uint32_t queries_;
};

class DropTarget final : public IDropTarget {
public:
    DropTarget(UINT format, const uint8_t *payload, uint32_t length)
        : refs_(1), format_(format), payload_(payload), length_(length),
          enters_(0u), overs_(0u), drops_(0u), mismatch_(false) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID iid, void **out) override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_IDropTarget)) {
            *out = static_cast<IDropTarget *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG value = InterlockedDecrement(&refs_);
        if (value == 0) delete this;
        return static_cast<ULONG>(value);
    }
    HRESULT STDMETHODCALLTYPE DragEnter(
        IDataObject *object, DWORD, POINTL, DWORD *effect) override {
        FORMATETC requested;
        ++enters_;
        if (effect == nullptr || object == nullptr) return E_INVALIDARG;
        ZeroMemory(&requested, sizeof(requested));
        requested.cfFormat = static_cast<CLIPFORMAT>(format_);
        requested.dwAspect = DVASPECT_CONTENT;
        requested.lindex = -1;
        requested.tymed = TYMED_HGLOBAL;
        if (FAILED(object->QueryGetData(&requested))) {
            *effect = DROPEFFECT_NONE;
            return S_OK;
        }
        *effect = (*effect & DROPEFFECT_COPY) != 0u
            ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(
        DWORD, POINTL, DWORD *effect) override {
        ++overs_;
        if (effect == nullptr) return E_INVALIDARG;
        *effect = (*effect & DROPEFFECT_COPY) != 0u
            ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(
        IDataObject *object, DWORD, POINTL, DWORD *effect) override {
        FORMATETC requested;
        STGMEDIUM medium;
        HRESULT result;
        ++drops_;
        if (effect == nullptr || object == nullptr) return E_INVALIDARG;
        ZeroMemory(&requested, sizeof(requested));
        requested.cfFormat = static_cast<CLIPFORMAT>(format_);
        requested.dwAspect = DVASPECT_CONTENT;
        requested.lindex = -1;
        requested.tymed = TYMED_HGLOBAL;
        ZeroMemory(&medium, sizeof(medium));
        result = object->GetData(&requested, &medium);
        if (FAILED(result)) {
            *effect = DROPEFFECT_NONE;
            return result;
        }
        mismatch_ = !medium_matches(&medium, payload_, length_);
        ReleaseStgMedium(&medium);
        *effect = mismatch_ ? DROPEFFECT_NONE : DROPEFFECT_COPY;
        return S_OK;
    }
    uint32_t enters() const { return enters_; }
    uint32_t overs() const { return overs_; }
    uint32_t drops() const { return drops_; }
    bool mismatch() const { return mismatch_; }
private:
    ~DropTarget() = default;
    LONG refs_;
    UINT format_;
    const uint8_t *payload_;
    uint32_t length_;
    uint32_t enters_;
    uint32_t overs_;
    uint32_t drops_;
    bool mismatch_;
};

enum class DragResult { unavailable, pass, mismatch };

static DragResult run_real_dragdrop(
    IDataObject *object, UINT format,
    const uint8_t *payload, uint32_t length, HRESULT *native_result) {
    static const wchar_t class_name[] =
        L"MooUiHostParityPrivateDropTarget";
    WNDCLASSW window_class;
    POINT cursor;
    HWND window = nullptr;
    DropSource *source = nullptr;
    DropTarget *target = nullptr;
    DWORD effect = DROPEFFECT_NONE;
    HRESULT result = E_FAIL;
    ATOM atom;
    bool registered_drop = false;
    DragResult outcome = DragResult::unavailable;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = drag_window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    atom = RegisterClassW(&window_class);
    if (atom == 0u) goto cleanup;
    if (GetCursorPos(&cursor) == FALSE) goto cleanup;
    window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        class_name, L"", WS_POPUP, cursor.x - 1, cursor.y - 1,
        3, 3, nullptr, nullptr, window_class.hInstance, nullptr);
    if (window == nullptr) goto cleanup;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    if (UpdateWindow(window) == FALSE ||
        WindowFromPoint(cursor) != window)
        goto cleanup;
    source = new (std::nothrow) DropSource();
    target = new (std::nothrow) DropTarget(format, payload, length);
    if (source == nullptr || target == nullptr) goto cleanup;
    result = RegisterDragDrop(window, target);
    if (FAILED(result)) goto cleanup;
    registered_drop = true;
    result = DoDragDrop(object, source, DROPEFFECT_COPY, &effect);
    if (result != DRAGDROP_S_DROP || effect != DROPEFFECT_COPY ||
        source->queries() == 0u || target->enters() != 1u ||
        target->drops() != 1u)
        goto cleanup;
    outcome = target->mismatch()
        ? DragResult::mismatch : DragResult::pass;
cleanup:
    if (native_result != nullptr) *native_result = result;
    if (registered_drop) (void)RevokeDragDrop(window);
    if (target != nullptr) target->Release();
    if (source != nullptr) source->Release();
    if (window != nullptr) DestroyWindow(window);
    if (atom != 0u)
        (void)UnregisterClassW(class_name, window_class.hInstance);
    return outcome;
}
}
#endif

extern "C" MooUiHostParityResult
moo_ui_host_parity_clipboard_win32_worker_run(
    const MooUiHostParityClipboardWorkerConfig *config,
    MooUiHostParityClipboardWorkerMetrics *metrics) {
#if defined(_WIN32)
    HRESULT initialized;
    HRESULT result = E_FAIL;
    UINT format;
    DataObject *source = nullptr;
    IDataObject *retrieved = nullptr;
    FORMATETC requested;
    STGMEDIUM medium;
    bool clipboard_owned = false;
    bool equal;
    DragResult drag_result = DragResult::unavailable;
    if (metrics == nullptr) return MOO_UI_HOST_PARITY_RESULT_INVALID;
    zero_metrics(metrics);
    if (config == nullptr || config->payload == nullptr ||
        config->payload_length == 0u ||
        config->payload_length > MOO_UI_HOST_PARITY_CLIPBOARD_MAX_PAYLOAD ||
        config->expected_window_station == nullptr ||
        config->expected_desktop == nullptr)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    if (!isolated_context(config))
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    initialized = OleInitialize(nullptr);
    if (FAILED(initialized)) {
        metrics->native_error = static_cast<int32_t>(initialized);
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    }
    format = RegisterClipboardFormatW(
        L"Moo.UiHostParity.Private.Binary.v1");
    if (format == 0u) {
        metrics->native_error = static_cast<int32_t>(GetLastError());
        OleUninitialize();
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    }
    source = new (std::nothrow) DataObject(
        format, config->payload, config->payload_length);
    if (source == nullptr) {
        OleUninitialize();
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    result = OleSetClipboard(source);
    if (FAILED(result)) goto unavailable;
    clipboard_owned = true;
    result = OleGetClipboard(&retrieved);
    if (FAILED(result) || retrieved == nullptr) goto unavailable;
    ZeroMemory(&requested, sizeof(requested));
    requested.cfFormat = static_cast<CLIPFORMAT>(format);
    requested.dwAspect = DVASPECT_CONTENT;
    requested.lindex = -1;
    requested.tymed = TYMED_HGLOBAL;
    ZeroMemory(&medium, sizeof(medium));
    result = retrieved->GetData(&requested, &medium);
    if (FAILED(result)) goto unavailable;
    equal = medium_matches(&medium, config->payload, config->payload_length);
    ReleaseStgMedium(&medium);
    if (equal) {
        metrics->clipboard_roundtrips = 1u;
        drag_result = run_real_dragdrop(source, format,
            config->payload, config->payload_length, &result);
        if (drag_result == DragResult::unavailable) goto unavailable;
        if (drag_result == DragResult::pass)
            metrics->dragdrop_sequences = 1u;
        else
            metrics->integrity_mismatches = 1u;
    } else {
        metrics->integrity_mismatches = 1u;
    }
    result = OleSetClipboard(nullptr);
    if (FAILED(result)) goto unavailable;
    clipboard_owned = false;
    retrieved->Release();
    source->Release();
    OleUninitialize();
    return MOO_UI_HOST_PARITY_RESULT_OK;
unavailable:
    if (retrieved != nullptr) retrieved->Release();
    if (clipboard_owned) (void)OleSetClipboard(nullptr);
    source->Release();
    OleUninitialize();
    zero_metrics(metrics);
    metrics->native_error = static_cast<int32_t>(result);
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
#else
    (void)config;
    if (metrics != nullptr) {
        metrics->clipboard_roundtrips = 0u;
        metrics->dragdrop_sequences = 0u;
        metrics->integrity_mismatches = 0u;
        metrics->native_error = 0;
    }
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
#endif
}
