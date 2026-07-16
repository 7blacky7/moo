#include "moo_ui_host_parity_clipboard_launcher_win32.h"
#include <string.h>
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#endif

namespace {
static const uint8_t wire_magic[8] = {
    UINT8_C(0x4d), UINT8_C(0x4f), UINT8_C(0x4f), UINT8_C(0x4e),
    UINT8_C(0x39), UINT8_C(0x56), UINT8_C(0x31), UINT8_C(0x00)
};
static void zero_metrics(MooUiHostParityClipboardWorkerMetrics *m) {
    m->clipboard_roundtrips = 0u;
    m->dragdrop_sequences = 0u;
    m->integrity_mismatches = 0u;
    m->native_error = 0;
}
static void put_u32(uint8_t *p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
    p[2] = static_cast<uint8_t>(value >> 16u);
    p[3] = static_cast<uint8_t>(value >> 24u);
}
static uint32_t wire_hash(const uint8_t *wire) {
    uint32_t hash = UINT32_C(2166136261);
    uint32_t i;
    for (i = 0u; i < 24u; ++i) {
        hash ^= static_cast<uint32_t>(wire[i]);
        hash *= UINT32_C(16777619);
    }
    return hash;
}
#if defined(_WIN32)
static uint32_t get_u32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8u) |
        (static_cast<uint32_t>(p[2]) << 16u) |
        (static_cast<uint32_t>(p[3]) << 24u);
}
static bool parse_wire(
    const uint8_t *wire, uint32_t length,
    MooUiHostParityClipboardWorkerMetrics *metrics) {
    uint32_t roundtrips;
    uint32_t dragdrops;
    uint32_t mismatches;
    if (length != MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE ||
        memcmp(wire, wire_magic, sizeof(wire_magic)) != 0 ||
        get_u32(wire + 24u) != wire_hash(wire))
        return false;
    roundtrips = get_u32(wire + 8u);
    dragdrops = get_u32(wire + 12u);
    mismatches = get_u32(wire + 16u);
    if (roundtrips > 1u || dragdrops > 1u || mismatches > 1u)
        return false;
    metrics->clipboard_roundtrips = roundtrips;
    metrics->dragdrop_sequences = dragdrops;
    metrics->integrity_mismatches = mismatches;
    metrics->native_error = static_cast<int32_t>(get_u32(wire + 20u));
    return true;
}
static bool safe_executable(const wchar_t *path) {
    size_t length;
    bool drive_absolute;
    bool unc_absolute;
    if (path == nullptr || path[0] == L'\0') return false;
    for (length = 0u; length <= 1024u; ++length) {
        if (path[length] == L'"') return false;
        if (path[length] == L'\0') break;
    }
    if (length == 0u || length > 1024u) return false;
    drive_absolute = length >= 3u &&
        ((path[0] >= L'A' && path[0] <= L'Z') ||
         (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
    unc_absolute = length >= 3u && path[0] == L'\\' && path[1] == L'\\';
    return drive_absolute || unc_absolute;
}

static bool terminate_and_join(
    HANDLE process, HANDLE *job, DWORD *error) {
    DWORD waited;
    if (process == nullptr || error == nullptr) return false;
    waited = WaitForSingleObject(process, 0u);
    if (waited == WAIT_OBJECT_0) return true;
    if (TerminateProcess(process, ERROR_TIMEOUT) != FALSE) {
        waited = WaitForSingleObject(process, 5000u);
        if (waited == WAIT_OBJECT_0) return true;
        if (waited == WAIT_FAILED) *error = GetLastError();
    } else {
        *error = GetLastError();
    }
    if (job != nullptr && *job != nullptr) {
        CloseHandle(*job);
        *job = nullptr;
        waited = WaitForSingleObject(process, 5000u);
        if (waited == WAIT_OBJECT_0) return true;
        if (waited == WAIT_FAILED) *error = GetLastError();
    }
    if (*error == ERROR_SUCCESS) *error = ERROR_TIMEOUT;
    return false;
}
static wchar_t hex_digit(uint32_t value) {
    return static_cast<wchar_t>(
        value < 10u ? L'0' + value : L'a' + (value - 10u));
}
static bool build_command(
    const MooUiHostParityClipboardLauncherConfig *config,
    const wchar_t *station, const wchar_t *desktop,
    wchar_t command[2048]) {
    wchar_t hex[MOO_UI_HOST_PARITY_CLIPBOARD_MAX_PAYLOAD * 2u + 1u];
    uint32_t i;
    int written;
    for (i = 0u; i < config->payload_length; ++i) {
        hex[i * 2u] = hex_digit(config->payload[i] >> 4u);
        hex[i * 2u + 1u] = hex_digit(config->payload[i] & 15u);
    }
    hex[config->payload_length * 2u] = L'\0';
    written = _snwprintf(command, 2048,
        L"\"%ls\" --moo-n9-worker %ls %ls %ls",
        config->executable_path, station, desktop, hex);
    command[2047] = L'\0';
    return written > 0 && written < 2048;
}
#endif
}

extern "C" MooUiHostParityResult moo_ui_host_parity_clipboard_wire_encode(
    const MooUiHostParityClipboardWorkerMetrics *metrics,
    uint8_t wire[MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE]) {
    if (metrics == nullptr || wire == nullptr ||
        metrics->clipboard_roundtrips > 1u ||
        metrics->dragdrop_sequences > 1u ||
        metrics->integrity_mismatches > 1u)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    memcpy(wire, wire_magic, sizeof(wire_magic));
    put_u32(wire + 8u, metrics->clipboard_roundtrips);
    put_u32(wire + 12u, metrics->dragdrop_sequences);
    put_u32(wire + 16u, metrics->integrity_mismatches);
    put_u32(wire + 20u, static_cast<uint32_t>(metrics->native_error));
    put_u32(wire + 24u, wire_hash(wire));
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

extern "C" MooUiHostParityResult
moo_ui_host_parity_clipboard_win32_launcher_run(
    const MooUiHostParityClipboardLauncherConfig *config,
    MooUiHostParityClipboardWorkerMetrics *metrics) {
#if defined(_WIN32)
    HWINSTA original_station;
    HWINSTA private_station = nullptr;
    HDESK private_desktop = nullptr;
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    HANDLE null_input = INVALID_HANDLE_VALUE;
    HANDLE null_error = INVALID_HANDLE_VALUE;
    HANDLE inherited_handles[3];
    HANDLE job = nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
    SIZE_T attribute_bytes = 0u;
    SECURITY_ATTRIBUTES security;
    STARTUPINFOEXW startup;
    PROCESS_INFORMATION process;
    wchar_t station_name[64];
    wchar_t desktop_name[64];
    wchar_t desktop_path[130];
    wchar_t command[2048];
    uint8_t output[MOO_UI_HOST_PARITY_CLIPBOARD_WIRE_SIZE + 1u];
    DWORD output_length = 0u;
    DWORD got = 0u;
    DWORD exit_code = STILL_ACTIVE;
    DWORD waited;
    DWORD error = ERROR_SUCCESS;
    BOOL process_created = FALSE;
    BOOL switched = FALSE;
    BOOL attributes_initialized = FALSE;
    MooUiHostParityResult outcome = MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    if (metrics == nullptr) return MOO_UI_HOST_PARITY_RESULT_INVALID;
    zero_metrics(metrics);
    if (config == nullptr || !safe_executable(config->executable_path) ||
        config->payload == nullptr || config->payload_length == 0u ||
        config->payload_length > MOO_UI_HOST_PARITY_CLIPBOARD_MAX_PAYLOAD ||
        config->timeout_ms < 100u || config->timeout_ms > 60000u)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    original_station = GetProcessWindowStation();
    if (original_station == nullptr) {
        metrics->native_error = static_cast<int32_t>(GetLastError());
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    }
    (void)_snwprintf(station_name, 64, L"MooParity-%08lx-%08lx",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetTickCount()));
    station_name[63] = L'\0';
    (void)_snwprintf(desktop_name, 64, L"MooParityDesk-%08lx",
        static_cast<unsigned long>(GetTickCount() ^ GetCurrentProcessId()));
    desktop_name[63] = L'\0';
    if (_snwprintf(desktop_path, 130, L"%ls\\%ls",
            station_name, desktop_name) <= 0) {
        metrics->native_error = ERROR_INVALID_NAME;
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    desktop_path[129] = L'\0';
    private_station = CreateWindowStationW(
        station_name, 0u, WINSTA_ALL_ACCESS, nullptr);
    if (private_station == nullptr) {
        metrics->native_error = static_cast<int32_t>(GetLastError());
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    }
    if (SetProcessWindowStation(private_station) == FALSE) {
        error = GetLastError();
        goto cleanup;
    }
    switched = TRUE;
    private_desktop = CreateDesktopW(desktop_name, nullptr, nullptr, 0u,
        DESKTOP_CREATEWINDOW | DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL |
        DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    if (private_desktop == nullptr) {
        error = GetLastError();
        goto cleanup;
    }
    if (!build_command(config, station_name, desktop_name, command)) {
        error = ERROR_INVALID_PARAMETER;
        outcome = MOO_UI_HOST_PARITY_RESULT_INVALID;
        goto cleanup;
    }
    ZeroMemory(&security, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (CreatePipe(&read_pipe, &write_pipe, &security, 0u) == FALSE ||
        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0u) == FALSE) {
        error = GetLastError();
        goto cleanup;
    }
    null_input = CreateFileW(L"NUL", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    null_error = CreateFileW(L"NUL", GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (null_input == INVALID_HANDLE_VALUE ||
        null_error == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        goto cleanup;
    }
    job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        error = GetLastError();
        goto cleanup;
    }
    ZeroMemory(&job_limits, sizeof(job_limits));
    job_limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job, JobObjectExtendedLimitInformation,
            &job_limits, sizeof(job_limits)) == FALSE) {
        error = GetLastError();
        goto cleanup;
    }
    (void)InitializeProcThreadAttributeList(
        nullptr, 1u, 0u, &attribute_bytes);
    if (attribute_bytes == 0u) {
        error = GetLastError();
        goto cleanup;
    }
    attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0u, attribute_bytes));
    if (attributes == nullptr) {
        error = ERROR_OUTOFMEMORY;
        outcome = MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        goto cleanup;
    }
    if (InitializeProcThreadAttributeList(
            attributes, 1u, 0u, &attribute_bytes) == FALSE) {
        error = GetLastError();
        goto cleanup;
    }
    attributes_initialized = TRUE;
    inherited_handles[0] = write_pipe;
    inherited_handles[1] = null_input;
    inherited_handles[2] = null_error;
    if (UpdateProcThreadAttribute(attributes, 0u,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited_handles,
            sizeof(inherited_handles), nullptr, nullptr) == FALSE) {
        error = GetLastError();
        goto cleanup;
    }
    ZeroMemory(&startup, sizeof(startup));
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = write_pipe;
    startup.StartupInfo.hStdError = null_error;
    startup.StartupInfo.hStdInput = null_input;
    startup.lpAttributeList = attributes;
    startup.StartupInfo.lpDesktop = desktop_path;
    ZeroMemory(&process, sizeof(process));
    process_created = CreateProcessW(config->executable_path, command,
        nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        nullptr, nullptr, &startup.StartupInfo, &process);
    if (process_created == FALSE) {
        error = GetLastError();
        goto cleanup;
    }
    if (AssignProcessToJobObject(job, process.hProcess) == FALSE) {
        error = GetLastError();
        outcome = MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        goto cleanup;
    }
    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        error = GetLastError();
        outcome = MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        goto cleanup;
    }
    CloseHandle(process.hThread);
    process.hThread = nullptr;
    CloseHandle(write_pipe);
    write_pipe = nullptr;
    CloseHandle(null_input);
    null_input = INVALID_HANDLE_VALUE;
    CloseHandle(null_error);
    null_error = INVALID_HANDLE_VALUE;
    waited = WaitForSingleObject(process.hProcess, config->timeout_ms);
    if (waited != WAIT_OBJECT_0) {
        error = waited == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        outcome = waited == WAIT_TIMEOUT
            ? MOO_UI_HOST_PARITY_RESULT_LIMIT
            : MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        if (!terminate_and_join(process.hProcess, &job, &error))
            outcome = MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        goto cleanup;
    }
    if (GetExitCodeProcess(process.hProcess, &exit_code) == FALSE) {
        error = GetLastError();
        goto cleanup;
    }
    while (output_length < sizeof(output) &&
        ReadFile(read_pipe, output + output_length,
            static_cast<DWORD>(sizeof(output) - output_length),
            &got, nullptr) != FALSE && got != 0u)
        output_length += got;
    if (exit_code != 0u || !parse_wire(output, output_length, metrics)) {
        error = exit_code != 0u ? exit_code : ERROR_INVALID_DATA;
        goto cleanup;
    }
    outcome = MOO_UI_HOST_PARITY_RESULT_OK;
cleanup:
    if (process_created != FALSE && process.hProcess != nullptr &&
        WaitForSingleObject(process.hProcess, 0u) != WAIT_OBJECT_0 &&
        !terminate_and_join(process.hProcess, &job, &error))
        outcome = MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    if (process_created != FALSE && process.hThread != nullptr)
        CloseHandle(process.hThread);
    if (process_created != FALSE && process.hProcess != nullptr)
        CloseHandle(process.hProcess);
    if (job != nullptr) CloseHandle(job);
    if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
    if (null_error != INVALID_HANDLE_VALUE) CloseHandle(null_error);
    if (write_pipe != nullptr) CloseHandle(write_pipe);
    if (read_pipe != nullptr) CloseHandle(read_pipe);
    if (attributes_initialized != FALSE)
        DeleteProcThreadAttributeList(attributes);
    if (attributes != nullptr)
        HeapFree(GetProcessHeap(), 0u, attributes);
    if (switched != FALSE &&
        SetProcessWindowStation(original_station) == FALSE) {
        error = GetLastError();
        outcome = MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    } else {
        switched = FALSE;
    }
    if (private_desktop != nullptr && !CloseDesktop(private_desktop)) {
        error = GetLastError();
        outcome = MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    if (private_station != nullptr && switched == FALSE &&
        !CloseWindowStation(private_station)) {
        error = GetLastError();
        outcome = MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    if (outcome != MOO_UI_HOST_PARITY_RESULT_OK) {
        zero_metrics(metrics);
        metrics->native_error = static_cast<int32_t>(
            error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
    }
    return outcome;
#else
    (void)config;
    if (metrics != nullptr) zero_metrics(metrics);
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
#endif
}
