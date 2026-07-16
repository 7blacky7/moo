/*
 * P016-O6 N8: isolated Win32 reduced-idle + crash/restart producer evidence.
 * No window, desktop input, DWM cadence, shell, or host message queue is used.
 */
#include "../moo_ui_host_parity_helper_win32.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define STATE_HASH UINT64_C(0x0123456789abcdef)
#define CRASH_EXIT_CODE UINT32_C(90)
#define FRAME_SAMPLES 120u

static int checks;
static int failures;
#define CHECK(c, m) do { ++checks; if (!(c)) { ++failures;     fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (m)); } } while (0)

#if defined(_WIN32)
static int qpc_ns(uint64_t *value) {
    LARGE_INTEGER counter, frequency;
    uint64_t seconds, remainder;
    if (value == 0 || QueryPerformanceFrequency(&frequency) == FALSE ||
        frequency.QuadPart <= 0 || QueryPerformanceCounter(&counter) == FALSE ||
        counter.QuadPart < 0) return 0;
    seconds = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
    remainder = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
    if (seconds > UINT64_MAX / UINT64_C(1000000000) ||
        remainder > UINT64_MAX / UINT64_C(1000000000)) return 0;
    *value = seconds * UINT64_C(1000000000) +
        (remainder * UINT64_C(1000000000)) / (uint64_t)frequency.QuadPart;
    return 1;
}

static int child_mode(int argc, char **argv) {
    if (argc != 2) return -1;
    if (strcmp(argv[1], "--crash-child") == 0) {
        (void)TerminateProcess(GetCurrentProcess(), (UINT)CRASH_EXIT_CODE);
        return 3;
    }
    if (strcmp(argv[1], "--restart-child") == 0) {
        wchar_t encoded[32];
        wchar_t *end = 0;
        DWORD flags = 0u;
        DWORD length = GetEnvironmentVariableW(
            L"MOO_PARITY_SENTINEL_HANDLE", encoded,
            (DWORD)(sizeof(encoded) / sizeof(encoded[0])));
        if (length == 0u ||
            length >= (DWORD)(sizeof(encoded) / sizeof(encoded[0])))
            return 5;
        {
            unsigned long long raw = wcstoull(encoded, &end, 16);
            if (end == encoded || *end != L'\0') return 6;
            if (GetHandleInformation((HANDLE)(uintptr_t)raw, &flags) != FALSE)
                return 7;
            if (GetLastError() != ERROR_INVALID_HANDLE) return 8;
        }
        printf("%016llx\n", (unsigned long long)STATE_HASH);
        return fflush(stdout) == 0 ? 0 : 4;
    }
    if (strcmp(argv[1], "--hang-child") == 0) {
        Sleep(2000u);
        return 0;
    }
    return -1;
}

static int init_compositor(MooCompositor *core,
                           MooCompClientSlot *clients,
                           MooCompSurfaceSlot *surfaces,
                           MooCompBufferSlot *buffers,
                           MooCompFrameSlot *frames,
                           MooCompEventSlot *events) {
    MooCompConfig config;
    config.output_width = 1;
    config.output_height = 1;
    config.background_r = 0u;
    config.background_g = 0u;
    config.background_b = 0u;
    config.background_a = 255u;
    return moo_comp_init(core, &config, clients, 1u, surfaces, 1u,
                         buffers, 1u, frames, 2u, events, 2u) == MOO_COMP_OK;
}
#endif

int main(int argc, char **argv) {
    MooUiHostParityInstrumentation instrumentation;
    MooUiHostParityResult result;
#if defined(_WIN32)
    int mode_result = child_mode(argc, argv);
    BOOL animations = TRUE;
    uint32_t native_supported = 0u;
    wchar_t executable_path[32768];
    DWORD executable_length;
    MooUiHostParityCrashProcessConfig crash_config;
    MooUiHostParityMeasurement measurement;
    MooCompositor core;
    MooCompClientSlot clients[1];
    MooCompSurfaceSlot surfaces[1];
    MooCompBufferSlot buffers[1];
    MooCompFrameSlot frames[2];
    MooCompEventSlot events[2];
    MooCompOutput output;
    uint8_t pixel[4] = {0u, 0u, 0u, 255u};
    uint64_t wall_start = 0u, wall_end = 0u, timestamp = 0u;
    uint64_t previous_timestamp = 0u, frame_id = 0u;
    uint32_t index;
    HANDLE padding_handles[64] = {0};
    HANDLE sentinel_handle = 0;
    SECURITY_ATTRIBUTES sentinel_security;
    wchar_t encoded_sentinel[32];
    if (mode_result >= 0) return mode_result;
#else
    (void)argc;
    (void)argv;
#endif

    CHECK(moo_ui_host_parity_instrumentation_init(
              &instrumentation, 91u) == MOO_UI_HOST_PARITY_RESULT_OK,
          "instrumentation init");
#if defined(_WIN32)
    CHECK(moo_ui_host_parity_helper_win32_bind(0, 91u) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID, "null bind fail closed");
    CHECK(moo_ui_host_parity_helper_win32_measure_reduced_idle(
              0, 91u, 1000u) == MOO_UI_HOST_PARITY_RESULT_INVALID,
          "null measurement fail closed");
    CHECK(moo_ui_host_parity_helper_win32_run_crash_cycle(
              &instrumentation, 91u, 0) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID,
          "null crash config fail closed");
    CHECK(moo_ui_host_parity_helper_win32_measure_reduced_idle(
              &instrumentation, 91u, 999u) ==
              MOO_UI_HOST_PARITY_RESULT_INVALID,
          "short idle window rejected");

    CHECK(init_compositor(&core, clients, surfaces, buffers, frames, events),
          "actual compositor init");
    output.pixels = pixel;
    output.buffer_bytes = sizeof(pixel);
    output.stride = sizeof(pixel);
    output.width = 1;
    output.height = 1;
    CHECK(moo_ui_host_parity_instrumentation_bind_presenter(
              &instrumentation, &core) == MOO_UI_HOST_PARITY_RESULT_OK,
          "actual presenter observer bind");
    for (index = 0u; index < FRAME_SAMPLES; ++index) {
        Sleep(1u);
        CHECK(qpc_ns(&timestamp) && timestamp > previous_timestamp,
              "actual frame QPC strictly increasing");
        CHECK(moo_comp_build_frame(&core, &output, &frame_id) == MOO_COMP_OK,
              "actual Moo frame build");
        CHECK(moo_comp_present_done(&core, frame_id, (uint64_t)index + 1u,
                                    timestamp) == MOO_COMP_OK,
              "actual Moo present completion");
        previous_timestamp = timestamp;
    }

    CHECK(moo_ui_host_parity_helper_win32_bind(
              &instrumentation, 91u) == MOO_UI_HOST_PARITY_RESULT_OK,
          "actual Win32 helper bind");
    CHECK(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0u,
                                &animations, 0u) != FALSE,
          "read-only reduced-motion query");
    CHECK(qpc_ns(&wall_start), "wall QPC start");
    result = moo_ui_host_parity_helper_win32_measure_reduced_idle(
        &instrumentation, 91u, 1000u);
    CHECK(qpc_ns(&wall_end) && wall_end >= wall_start, "wall QPC end");
    if (animations != FALSE) {
        CHECK(result == MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE,
              "animations-enabled system cannot claim reduced idle");
    } else {
        native_supported = 1u;
        CHECK(result == MOO_UI_HOST_PARITY_RESULT_OK,
              "actual isolated helper measurement");
        CHECK(instrumentation.reduced_idle_end_ns -
                  instrumentation.reduced_idle_start_ns >=
                      UINT64_C(1000000000),
              "actual helper QPC duration at least one second");
        CHECK(instrumentation.reduced_wakeups == 1u,
              "exactly one kernel-timeout wakeup");
        CHECK(wall_end - wall_start >= UINT64_C(1000000000),
              "wall duration at least one second");
    }

    executable_length = GetModuleFileNameW(
        0, executable_path,
        (DWORD)(sizeof(executable_path) / sizeof(executable_path[0])));
    CHECK(executable_length != 0u &&
          executable_length <
              (DWORD)(sizeof(executable_path) / sizeof(executable_path[0])),
          "self executable path");
    crash_config.executable_path = executable_path;
    crash_config.crash_argument = L"--crash-child";
    crash_config.restart_argument = L"--restart-child";
    crash_config.expected_state_hash = STATE_HASH;
    crash_config.crash_exit_code = CRASH_EXIT_CODE;
    crash_config.timeout_ms = 5000u;
    for (index = 0u; index < 64u; ++index) {
        padding_handles[index] = CreateEventW(0, TRUE, FALSE, 0);
        CHECK(padding_handles[index] != 0, "sentinel handle padding");
    }
    ZeroMemory(&sentinel_security, sizeof(sentinel_security));
    sentinel_security.nLength = sizeof(sentinel_security);
    sentinel_security.bInheritHandle = TRUE;
    sentinel_handle = CreateEventW(&sentinel_security, TRUE, FALSE, 0);
    CHECK(sentinel_handle != 0, "inheritable sentinel creation");
    CHECK(swprintf(encoded_sentinel,
                   sizeof(encoded_sentinel) / sizeof(encoded_sentinel[0]),
                   L"%llx",
                   (unsigned long long)(uintptr_t)sentinel_handle) > 0,
          "sentinel handle encoding");
    CHECK(SetEnvironmentVariableW(L"MOO_PARITY_SENTINEL_HANDLE",
                                  encoded_sentinel) != FALSE,
          "sentinel environment publication");
    CHECK(moo_ui_host_parity_helper_win32_run_crash_cycle(
              &instrumentation, 91u, &crash_config) ==
              MOO_UI_HOST_PARITY_RESULT_OK,
          "actual child crash restart cycle without sentinel leak");
    CHECK(SetEnvironmentVariableW(L"MOO_PARITY_SENTINEL_HANDLE", 0) != FALSE,
          "sentinel environment cleanup");
    if (sentinel_handle != 0) CloseHandle(sentinel_handle);
    for (index = 0u; index < 64u; ++index)
        if (padding_handles[index] != 0) CloseHandle(padding_handles[index]);
    CHECK(instrumentation.crash_injected == 1u &&
          instrumentation.crash_restarts == 1u &&
          instrumentation.crash_corruptions == 0u,
          "actual child state integrity exact");

    if (native_supported != 0u) {
        CHECK(moo_ui_host_parity_instrumentation_seal(&instrumentation) ==
                  MOO_UI_HOST_PARITY_RESULT_OK,
              "actual presenter helper child evidence seals");
        memset(&measurement, 0, sizeof(measurement));
        CHECK(moo_ui_host_parity_instrumentation_probe(
                  &instrumentation, 5u, &measurement) ==
                  MOO_UI_HOST_PARITY_RESULT_OK &&
              measurement.evidence ==
                  (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
              measurement.value_a == FRAME_SAMPLES &&
              measurement.value_b <=
                  (uint64_t)MOO_UI_HOST_PARITY_MAX_P99_FRAME_US &&
              measurement.value_c == 1u,
              "sealed animation energy exact pass");
        memset(&measurement, 0, sizeof(measurement));
        CHECK(moo_ui_host_parity_instrumentation_probe(
                  &instrumentation, 6u, &measurement) ==
                  MOO_UI_HOST_PARITY_RESULT_OK &&
              measurement.evidence ==
                  (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS &&
              measurement.value_a == 1u && measurement.value_b == 1u &&
              measurement.value_c == 0u,
              "sealed crash isolation exact pass");
    }

    {
        MooUiHostParityInstrumentation negative;
        MooUiHostParityCrashProcessConfig bad = crash_config;
        CHECK(moo_ui_host_parity_instrumentation_init(&negative, 92u) ==
                  MOO_UI_HOST_PARITY_RESULT_OK &&
              moo_ui_host_parity_helper_win32_bind(&negative, 92u) ==
                  MOO_UI_HOST_PARITY_RESULT_OK,
              "negative crash fixture");
        bad.crash_argument = L"--bad argument";
        CHECK(moo_ui_host_parity_helper_win32_run_crash_cycle(
                  &negative, 92u, &bad) ==
                  MOO_UI_HOST_PARITY_RESULT_INVALID &&
              negative.crash_injected == 0u,
              "unsafe child argument rejected without evidence");
        bad = crash_config;
        bad.crash_exit_code = CRASH_EXIT_CODE + 1u;
        CHECK(moo_ui_host_parity_helper_win32_run_crash_cycle(
                  &negative, 92u, &bad) ==
                  MOO_UI_HOST_PARITY_RESULT_INVALID &&
              negative.crash_injected == 0u,
              "wrong crash exit rejected without evidence");
        bad = crash_config;
        bad.crash_argument = L"--hang-child";
        bad.timeout_ms = 100u;
        CHECK(moo_ui_host_parity_helper_win32_run_crash_cycle(
                  &negative, 92u, &bad) ==
                  MOO_UI_HOST_PARITY_RESULT_LIMIT &&
              negative.crash_injected == 0u,
              "hung child terminated joined and fail closed");
    }
#else
    CHECK(moo_ui_host_parity_helper_win32_bind(
              &instrumentation, 91u) ==
              MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE,
          "non-Windows bind unavailable");
    result = moo_ui_host_parity_helper_win32_measure_reduced_idle(
        &instrumentation, 91u, 1000u);
    CHECK(result == MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE,
          "non-Windows measurement unavailable");
    CHECK(moo_ui_host_parity_helper_win32_run_crash_cycle(
              &instrumentation, 91u, 0) ==
              MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE,
          "non-Windows crash cycle unavailable");
#endif

    if (failures != 0) {
        fprintf(stderr, "P016-O6 N8 WIN32 HELPER FAIL: %d/%d\n",
                failures, checks);
        return 1;
    }
#if defined(_WIN32)
    if (native_supported != 0u)
        printf("P016-O6 N8 WIN32 INTEGRATED OK: %d checks\n", checks);
    else
        printf("P016-O6 N8 WIN32 INTEGRATED UNSUPPORTED: %d checks\n", checks);
#else
    printf("P016-O6 N8 WIN32 HELPER N/A: %d checks\n", checks);
#endif
    return 0;
}
