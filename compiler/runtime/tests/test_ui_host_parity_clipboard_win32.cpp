#include "../moo_ui_host_parity_clipboard_launcher_win32.h"
#include "../moo_ui_host_parity_clipboard_win32.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
int main(void) {
    puts("P016 N9 CLIPBOARD LAUNCHER SKIP: 77");
    return 77;
}
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL: %s\n", (message)); \
    } \
} while (0)

static void zero_metrics(MooUiHostParityClipboardWorkerMetrics *metrics) {
    metrics->clipboard_roundtrips = 0u;
    metrics->dragdrop_sequences = 0u;
    metrics->integrity_mismatches = 0u;
    metrics->native_error = 0;
}

static int metrics_zero(
    const MooUiHostParityClipboardWorkerMetrics *metrics) {
    return metrics->clipboard_roundtrips == 0u &&
        metrics->dragdrop_sequences == 0u &&
        metrics->integrity_mismatches == 0u;
}

static HGLOBAL make_medium(const uint8_t *bytes, SIZE_T size) {
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, size);
    uint8_t *destination;
    if (global == nullptr) return nullptr;
    destination = static_cast<uint8_t *>(GlobalLock(global));
    if (destination == nullptr) {
        GlobalFree(global);
        return nullptr;
    }
    memcpy(destination, bytes, size);
    GlobalUnlock(global);
    return global;
}

static void run_medium_case(
    const uint8_t *bytes, SIZE_T size, const uint8_t *payload,
    uint32_t length, int expected, const char *label) {
    HGLOBAL global = make_medium(bytes, size);
    UINT flags;
    CHECK(global != nullptr, "allocate adversarial HGLOBAL medium");
    if (global == nullptr) return;
    CHECK(moo_ui_host_parity_clipboard_win32_test_medium_matches(
              reinterpret_cast<uintptr_t>(global), payload, length) == expected,
          label);
    flags = GlobalFlags(global);
    CHECK(flags != GMEM_INVALID_HANDLE && (flags & GMEM_LOCKCOUNT) == 0u,
          "medium verifier releases every successful GlobalLock");
    CHECK(GlobalFree(global) == nullptr,
          "adversarial HGLOBAL medium frees after verification");
}

static int hex_value(wchar_t value) {
    if (value >= L'0' && value <= L'9') return (int)(value - L'0');
    if (value >= L'a' && value <= L'f') return (int)(value - L'a') + 10;
    if (value >= L'A' && value <= L'F') return (int)(value - L'A') + 10;
    return -1;
}

static int decode_payload(
    const wchar_t *text, uint8_t *payload, uint32_t *length) {
    size_t chars;
    size_t index;
    if (text == nullptr || payload == nullptr || length == nullptr)
        return 0;
    chars = wcslen(text);
    if (chars == 0u || (chars & 1u) != 0u ||
        chars > MOO_UI_HOST_PARITY_CLIPBOARD_MAX_PAYLOAD * 2u)
        return 0;
    *length = (uint32_t)(chars / 2u);
    for (index = 0u; index < chars; index += 2u) {
        int high = hex_value(text[index]);
        int low = hex_value(text[index + 1u]);
        if (high < 0 || low < 0) return 0;
        payload[index / 2u] = (uint8_t)((high << 4) | low);
    }
    return 1;
}

static int write_all(const uint8_t *bytes, DWORD length) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD offset = 0u;
    if (output == nullptr || output == INVALID_HANDLE_VALUE) return 0;
    while (offset < length) {
        DWORD wrote = 0u;
        if (WriteFile(output, bytes + offset, length - offset,
                &wrote, nullptr) == FALSE || wrote == 0u)
            return 0;
        offset += wrote;
    }
    return 1;
}

static int worker_mode(int argc, wchar_t **argv) {
    MooUiHostParityClipboardWorkerConfig config;
    MooUiHostParityClipboardWorkerMetrics metrics;
    MooUiHostParityResult result;
    uint8_t payload[MOO_UI_HOST_PARITY_CLIPBOARD_MAX_PAYLOAD];
    uint8_t wire[MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE + 1u];
    uint32_t payload_length = 0u;
    if (argc != 5 || wcscmp(argv[1], L"--moo-n9-worker") != 0 ||
        !decode_payload(argv[4], payload, &payload_length))
        return 80;
    if (payload[0] == UINT8_C(0xf0)) {
        for (;;) Sleep(1000u);
    }
    zero_metrics(&metrics);
    if (payload[0] == UINT8_C(0xf1)) {
        memset(wire, 0, MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE);
        return write_all(wire, MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE)
            ? 0 : 82;
    }
    if (payload[0] == UINT8_C(0xf2)) {
        if (moo_ui_host_parity_clipboard_wire_encode(&metrics, wire) !=
            MOO_UI_HOST_PARITY_RESULT_OK)
            return 83;
        wire[MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE] = UINT8_C(0xa5);
        return write_all(wire,
            MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE + 1u) ? 0 : 84;
    }
    config.expected_window_station = argv[2];
    config.expected_desktop = argv[3];
    config.payload = payload;
    config.payload_length = payload_length;
    result = moo_ui_host_parity_clipboard_win32_worker_run(&config, &metrics);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) return 85;
    if (moo_ui_host_parity_clipboard_wire_encode(&metrics, wire) !=
        MOO_UI_HOST_PARITY_RESULT_OK)
        return 86;
    return write_all(wire, MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE) ? 0 : 87;
}

static int user_object_name(
    HANDLE object, wchar_t *name, DWORD capacity) {
    DWORD bytes = 0u;
    if (object == nullptr || name == nullptr || capacity < 2u) return 0;
    name[0] = L'\0';
    if (GetUserObjectInformationW(object, UOI_NAME, name,
            capacity * (DWORD)sizeof(wchar_t), &bytes) == FALSE)
        return 0;
    name[capacity - 1u] = L'\0';
    return bytes > sizeof(wchar_t);
}

static void run_case(
    const wchar_t *executable, const uint8_t *payload, uint32_t length,
    uint32_t timeout_ms, MooUiHostParityResult expected,
    MooUiHostParityResult expected_alternative, const char *label) {
    MooUiHostParityClipboardLauncherConfig config;
    MooUiHostParityClipboardWorkerMetrics metrics;
    MooUiHostParityResult result;
    wchar_t station_before[128];
    wchar_t station_after[128];
    DWORD sequence_before;
    DWORD handles_before = 0u;
    DWORD handles_after = 0u;
    CHECK(user_object_name(GetProcessWindowStation(),
              station_before, 128u),
          "capture original station");
    sequence_before = GetClipboardSequenceNumber();
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_before) != FALSE,
          "capture launcher handle count before");
    config.executable_path = executable;
    config.payload = payload;
    config.payload_length = length;
    config.timeout_ms = timeout_ms;
    memset(&metrics, 0xa5, sizeof(metrics));
    result = moo_ui_host_parity_clipboard_win32_launcher_run(&config, &metrics);
    CHECK(result == expected || result == expected_alternative, label);
    CHECK(metrics_zero(&metrics), "negative launcher metrics exact zero");
    CHECK(GetClipboardSequenceNumber() == sequence_before,
          "interactive clipboard sequence unchanged");
    CHECK(user_object_name(GetProcessWindowStation(),
              station_after, 128u) &&
          wcscmp(station_after, station_before) == 0,
          "original process window station restored");
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after) != FALSE,
          "capture launcher handle count after");
    fprintf(stderr,
        "CASE %s result=%u native_error=%d handles=%lu/%lu metrics=%u/%u/%u\n",
        label, (unsigned)result, (int)metrics.native_error,
        (unsigned long)handles_before, (unsigned long)handles_after,
        metrics.clipboard_roundtrips, metrics.dragdrop_sequences,
        metrics.integrity_mismatches);
    CHECK(handles_after == handles_before,
          "launcher handles fully closed");
}

int wmain(int argc, wchar_t **argv) {
    wchar_t executable[32768];
    DWORD executable_length;
    wchar_t station_before[128];
    wchar_t station_after[128];
    DWORD sequence_before;
    DWORD handles_before = 0u;
    DWORD handles_after = 0u;
    MooUiHostParityClipboardLauncherConfig config;
    MooUiHostParityClipboardWorkerMetrics metrics;
    MooUiHostParityResult result;
    static const uint8_t valid_payload[] = {
        UINT8_C(0x4d), UINT8_C(0x4f), UINT8_C(0x4f), UINT8_C(0x00),
        UINT8_C(0xa5), UINT8_C(0x5a), UINT8_C(0x16), UINT8_C(0x09)
    };
    static const uint8_t malformed_payload[] = { UINT8_C(0xf1) };
    static const uint8_t tail_payload[] = { UINT8_C(0xf2) };
    static const uint8_t hang_payload[] = { UINT8_C(0xf0) };
    static const uint8_t medium_payload[] = {
        UINT8_C(0x4d), UINT8_C(0x4f), UINT8_C(0x4f)
    };
    static const uint8_t medium_undersize[] = {
        UINT8_C(0x03), UINT8_C(0x00), UINT8_C(0x00)
    };
    static const uint8_t medium_exact[] = {
        UINT8_C(0x03), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
        UINT8_C(0x4d), UINT8_C(0x4f), UINT8_C(0x4f)
    };
    static const uint8_t medium_prefix_tail[] = {
        UINT8_C(0x03), UINT8_C(0x00), UINT8_C(0x00), UINT8_C(0x00),
        UINT8_C(0x4d), UINT8_C(0x4f), UINT8_C(0x4f), UINT8_C(0xa5)
    };

    if (argc > 1) return worker_mode(argc, argv);
    executable_length = GetModuleFileNameW(
        nullptr, executable, (DWORD)(sizeof(executable) / sizeof(executable[0])));
    CHECK(executable_length != 0u &&
          executable_length <
              (DWORD)(sizeof(executable) / sizeof(executable[0])),
          "absolute self executable path");

    config.executable_path = L"relative-worker.exe";
    config.payload = valid_payload;
    config.payload_length = (uint32_t)sizeof(valid_payload);
    config.timeout_ms = 1000u;
    memset(&metrics, 0xa5, sizeof(metrics));
    CHECK(moo_ui_host_parity_clipboard_win32_launcher_run(
              &config, &metrics) == MOO_UI_HOST_PARITY_RESULT_INVALID &&
          metrics_zero(&metrics),
          "relative executable rejected exact zero");

    run_medium_case(medium_undersize, sizeof(medium_undersize),
        medium_payload, (uint32_t)sizeof(medium_payload), 0,
        "undersize HGLOBAL envelope rejected");
    run_medium_case(medium_exact, sizeof(medium_exact),
        medium_payload, (uint32_t)sizeof(medium_payload), 1,
        "exact HGLOBAL envelope accepted");
    run_medium_case(medium_prefix_tail, sizeof(medium_prefix_tail),
        medium_payload, (uint32_t)sizeof(medium_payload), 0,
        "valid HGLOBAL prefix plus tail rejected");

    config.executable_path = executable;
    config.payload = malformed_payload;
    config.payload_length = (uint32_t)sizeof(malformed_payload);
    config.timeout_ms = 2000u;
    sequence_before = GetClipboardSequenceNumber();
    memset(&metrics, 0xa5, sizeof(metrics));
    CHECK(moo_ui_host_parity_clipboard_win32_launcher_run(
              &config, &metrics) == MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE &&
          metrics_zero(&metrics) &&
          GetClipboardSequenceNumber() == sequence_before,
          "warm private launcher initialization remains isolated");

    run_case(executable, malformed_payload,
        (uint32_t)sizeof(malformed_payload), 2000u,
        MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE,
        MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE,
        "malformed worker wire rejected");
    run_case(executable, tail_payload,
        (uint32_t)sizeof(tail_payload), 2000u,
        MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE,
        MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE,
        "extra-tail worker wire rejected");
    run_case(executable, hang_payload,
        (uint32_t)sizeof(hang_payload), 200u,
        MOO_UI_HOST_PARITY_RESULT_LIMIT,
        MOO_UI_HOST_PARITY_RESULT_LIMIT,
        "hung worker bounded and terminated");

    CHECK(user_object_name(GetProcessWindowStation(),
              station_before, 128u),
          "capture station before real private worker");
    sequence_before = GetClipboardSequenceNumber();
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_before) != FALSE,
          "capture real worker handle count before");
    config.executable_path = executable;
    config.payload = valid_payload;
    config.payload_length = (uint32_t)sizeof(valid_payload);
    config.timeout_ms = 5000u;
    memset(&metrics, 0xa5, sizeof(metrics));
    result = moo_ui_host_parity_clipboard_win32_launcher_run(&config, &metrics);
    CHECK((result == MOO_UI_HOST_PARITY_RESULT_OK &&
              metrics.clipboard_roundtrips == 1u &&
              metrics.dragdrop_sequences == 1u &&
              metrics.integrity_mismatches == 0u &&
              metrics.native_error == 0) ||
          (result == MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE &&
              metrics_zero(&metrics)),
          "real hidden private OLE lifecycle passes or exact unsupported");
    CHECK(GetClipboardSequenceNumber() == sequence_before,
          "real private lifecycle leaves interactive clipboard unchanged");
    CHECK(user_object_name(GetProcessWindowStation(),
              station_after, 128u) &&
          wcscmp(station_after, station_before) == 0,
          "real private lifecycle restores station");
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after) != FALSE &&
          handles_after == handles_before,
          "real private lifecycle closes all launcher handles");

    if (failures != 0u) {
        fprintf(stderr, "P016 N9 CLIPBOARD LAUNCHER FAIL: %u/%u\n",
            failures, checks);
        return 1;
    }
    printf("P016 N9 CLIPBOARD LAUNCHER OK: checks=%u result=%u metrics=%u/%u/%u\n",
        checks, (unsigned)result, metrics.clipboard_roundtrips,
        metrics.dragdrop_sequences, metrics.integrity_mismatches);
    return 0;
}
#endif
