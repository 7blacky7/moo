#include "moo_ui_host_parity_helper_win32.h"
#include "moo_ui_host_parity_instrumentation_internal.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct {
    MooUiHostParityInstrumentation *instrumentation;
    uint64_t generation;
    uint32_t duration_ms;
    HANDLE cancel_event;
} MooParityIdleThreadContext;

static uint32_t moo_helper_qpc_ns(uint64_t *timestamp_ns) {
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    uint64_t seconds;
    uint64_t remainder;
    if (timestamp_ns == 0 || QueryPerformanceFrequency(&frequency) == FALSE ||
        frequency.QuadPart <= 0 || QueryPerformanceCounter(&counter) == FALSE ||
        counter.QuadPart < 0)
        return 0u;
    seconds = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
    remainder = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
    if (seconds > UINT64_MAX / UINT64_C(1000000000) ||
        remainder > UINT64_MAX / UINT64_C(1000000000))
        return 0u;
    *timestamp_ns = seconds * UINT64_C(1000000000) +
        (remainder * UINT64_C(1000000000)) /
            (uint64_t)frequency.QuadPart;
    return *timestamp_ns != 0u;
}

static DWORD WINAPI moo_helper_idle_thread(void *user) {
    MooParityIdleThreadContext *context =
        (MooParityIdleThreadContext *)user;
    DWORD waited;
    uint64_t start_ns;
    uint64_t end_ns;
    if (context == 0 || context->cancel_event == 0 ||
        !moo_helper_qpc_ns(&start_ns))
        return (DWORD)ERROR_INVALID_DATA;
    if (moo_ui_host_parity_helper_begin_reduced_idle(
            context->instrumentation, context->generation, start_ns) !=
            MOO_UI_HOST_PARITY_RESULT_OK)
        return (DWORD)ERROR_INVALID_STATE;
    waited = WaitForSingleObject(context->cancel_event, context->duration_ms);
    if (waited != WAIT_TIMEOUT || !moo_helper_qpc_ns(&end_ns)) {
        if (waited == WAIT_OBJECT_0) return (DWORD)ERROR_CANCELLED;
        return waited == WAIT_FAILED ? GetLastError() : (DWORD)ERROR_TIMEOUT;
    }
    if (moo_ui_host_parity_helper_record_wakeup(
            context->instrumentation, context->generation, end_ns) !=
            MOO_UI_HOST_PARITY_RESULT_OK ||
        moo_ui_host_parity_helper_end_reduced_idle(
            context->instrumentation, context->generation, end_ns) !=
            MOO_UI_HOST_PARITY_RESULT_OK)
        return (DWORD)ERROR_INVALID_DATA;
    return (DWORD)ERROR_SUCCESS;
}

static uint32_t moo_helper_mode_is_safe(const wchar_t *mode) {
    size_t index;
    if (mode == 0 || mode[0] != L'-' || mode[1] != L'-' || mode[2] == L'\0')
        return 0u;
    for (index = 2u; mode[index] != L'\0'; ++index) {
        wchar_t value = mode[index];
        if (!((value >= L'a' && value <= L'z') ||
              (value >= L'A' && value <= L'Z') ||
              (value >= L'0' && value <= L'9') ||
              value == L'-' || value == L'_'))
            return 0u;
    }
    return index <= 64u;
}

static MooUiHostParityResult moo_helper_collect_process(
    const wchar_t *executable_path, const wchar_t *argument,
    uint32_t timeout_ms, char *output, DWORD output_capacity,
    DWORD *output_size, DWORD *exit_code) {
    SECURITY_ATTRIBUTES security;
    STARTUPINFOEXW startup;
    PROCESS_INFORMATION process;
    HANDLE read_pipe = 0;
    HANDLE write_pipe = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = 0;
    wchar_t *command_line;
    size_t path_length;
    size_t argument_length;
    size_t command_chars;
    SIZE_T attribute_bytes = 0u;
    DWORD waited;
    DWORD joined;
    DWORD used = 0u;
    DWORD got = 0u;
    DWORD read_error = ERROR_SUCCESS;
    DWORD creation_flags = CREATE_NO_WINDOW;
    BOOL process_created;
    uint32_t capture = output != 0 && output_size != 0 && output_capacity != 0u;
    if (executable_path == 0 || executable_path[0] == L'\0' ||
        !moo_helper_mode_is_safe(argument) || timeout_ms == 0u ||
        exit_code == 0 || (capture == 0u && (output != 0 || output_size != 0)))
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    for (path_length = 0u; executable_path[path_length] != L'\0'; ++path_length) {
        if (executable_path[path_length] == L'"' || path_length >= 32760u)
            return MOO_UI_HOST_PARITY_RESULT_INVALID;
    }
    argument_length = wcslen(argument);
    if (path_length > 32760u - argument_length - 5u)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    command_chars = path_length + argument_length + 5u;
    command_line = (wchar_t *)HeapAlloc(
        GetProcessHeap(), 0u, command_chars * sizeof(wchar_t));
    if (command_line == 0)
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    command_line[0] = L'"';
    CopyMemory(command_line + 1u, executable_path,
        path_length * sizeof(wchar_t));
    command_line[path_length + 1u] = L'"';
    command_line[path_length + 2u] = L' ';
    CopyMemory(command_line + path_length + 3u, argument,
        argument_length * sizeof(wchar_t));
    command_line[path_length + argument_length + 3u] = L'\0';

    ZeroMemory(&security, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (capture != 0u &&
        (CreatePipe(&read_pipe, &write_pipe, &security, 0u) == FALSE ||
         SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0u) == FALSE)) {
        if (read_pipe != 0) CloseHandle(read_pipe);
        if (write_pipe != 0) CloseHandle(write_pipe);
        HeapFree(GetProcessHeap(), 0u, command_line);
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    ZeroMemory(&startup, sizeof(startup));
    startup.StartupInfo.cb = sizeof(startup.StartupInfo);
    if (capture != 0u) {
        (void)InitializeProcThreadAttributeList(0, 1u, 0u, &attribute_bytes);
        if (attribute_bytes == 0u) {
            CloseHandle(read_pipe);
            CloseHandle(write_pipe);
            HeapFree(GetProcessHeap(), 0u, command_line);
            return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        }
        attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(
            GetProcessHeap(), 0u, attribute_bytes);
        if (attributes == 0) {
            CloseHandle(read_pipe);
            CloseHandle(write_pipe);
            HeapFree(GetProcessHeap(), 0u, command_line);
            return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        }
        if (InitializeProcThreadAttributeList(
                attributes, 1u, 0u, &attribute_bytes) == FALSE) {
            HeapFree(GetProcessHeap(), 0u, attributes);
            CloseHandle(read_pipe);
            CloseHandle(write_pipe);
            HeapFree(GetProcessHeap(), 0u, command_line);
            return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        }
        if (UpdateProcThreadAttribute(attributes, 0u,
                PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &write_pipe,
                sizeof(write_pipe), 0, 0) == FALSE) {
            DeleteProcThreadAttributeList(attributes);
            HeapFree(GetProcessHeap(), 0u, attributes);
            CloseHandle(read_pipe);
            CloseHandle(write_pipe);
            HeapFree(GetProcessHeap(), 0u, command_line);
            return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        }
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = write_pipe;
        startup.StartupInfo.hStdError = write_pipe;
        startup.StartupInfo.hStdInput = write_pipe;
        startup.lpAttributeList = attributes;
        creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    ZeroMemory(&process, sizeof(process));
    process_created = CreateProcessW(executable_path, command_line, 0, 0,
        capture != 0u ? TRUE : FALSE, creation_flags, 0, 0,
        &startup.StartupInfo, &process);
    if (attributes != 0) {
        DeleteProcThreadAttributeList(attributes);
        HeapFree(GetProcessHeap(), 0u, attributes);
    }
    if (process_created == FALSE) {
        if (read_pipe != 0) CloseHandle(read_pipe);
        if (write_pipe != 0) CloseHandle(write_pipe);
        HeapFree(GetProcessHeap(), 0u, command_line);
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    HeapFree(GetProcessHeap(), 0u, command_line);
    CloseHandle(process.hThread);
    if (write_pipe != 0) {
        CloseHandle(write_pipe);
        write_pipe = 0;
    }
    waited = WaitForSingleObject(process.hProcess, timeout_ms);
    if (waited != WAIT_OBJECT_0) {
        (void)TerminateProcess(process.hProcess, (UINT)ERROR_TIMEOUT);
        joined = WaitForSingleObject(process.hProcess, INFINITE);
        if (joined != WAIT_OBJECT_0) {
            if (read_pipe != 0) CloseHandle(read_pipe);
            CloseHandle(process.hProcess);
            return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        }
        if (read_pipe != 0) CloseHandle(read_pipe);
        CloseHandle(process.hProcess);
        return waited == WAIT_TIMEOUT ? MOO_UI_HOST_PARITY_RESULT_LIMIT :
            MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    if (GetExitCodeProcess(process.hProcess, exit_code) == FALSE) {
        if (read_pipe != 0) CloseHandle(read_pipe);
        CloseHandle(process.hProcess);
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    CloseHandle(process.hProcess);
    if (capture != 0u) {
        while (used < output_capacity &&
               ReadFile(read_pipe, output + used, output_capacity - used,
                   &got, 0) != FALSE && got != 0u)
            used += got;
        read_error = GetLastError();
        CloseHandle(read_pipe);
        if (used == output_capacity || (read_error != ERROR_BROKEN_PIPE &&
            read_error != ERROR_SUCCESS))
            return MOO_UI_HOST_PARITY_RESULT_INVALID;
        *output_size = used;
    }
    return MOO_UI_HOST_PARITY_RESULT_OK;
}

static uint32_t moo_helper_parse_state_hash(
    const char *text, DWORD size, uint64_t *state_hash) {
    DWORD index;
    uint64_t value = 0u;
    if (text == 0 || state_hash == 0 ||
        !((size == 16u) || (size == 17u && text[16] == '\n') ||
          (size == 18u && text[16] == '\r' && text[17] == '\n')))
        return 0u;
    for (index = 0u; index < 16u; ++index) {
        uint32_t digit;
        char character = text[index];
        if (character >= '0' && character <= '9')
            digit = (uint32_t)(character - '0');
        else if (character >= 'a' && character <= 'f')
            digit = (uint32_t)(character - 'a') + 10u;
        else if (character >= 'A' && character <= 'F')
            digit = (uint32_t)(character - 'A') + 10u;
        else
            return 0u;
        value = (value << 4u) | (uint64_t)digit;
    }
    *state_hash = value;
    return 1u;
}
#endif

MooUiHostParityResult moo_ui_host_parity_helper_win32_bind(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation) {
#if defined(_WIN32)
    return moo_ui_host_parity_helper_bind(instrumentation, generation);
#else
    (void)instrumentation;
    (void)generation;
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
#endif
}

MooUiHostParityResult moo_ui_host_parity_helper_win32_measure_reduced_idle(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    uint32_t duration_ms) {
#if defined(_WIN32)
    MooParityIdleThreadContext context;
    HANDLE thread;
    DWORD waited;
    DWORD joined;
    DWORD thread_result = (DWORD)ERROR_GEN_FAILURE;
    BOOL animations_enabled = TRUE;
    if (instrumentation == 0 || generation == 0u || duration_ms < 1000u ||
        duration_ms > UINT32_MAX - 5000u)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0u,
            &animations_enabled, 0u) == FALSE)
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    if (animations_enabled != FALSE)
        return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
    context.instrumentation = instrumentation;
    context.generation = generation;
    context.duration_ms = duration_ms;
    context.cancel_event = CreateEventW(0, TRUE, FALSE, 0);
    if (context.cancel_event == 0)
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    thread = CreateThread(0, 0u, moo_helper_idle_thread, &context, 0u, 0);
    if (thread == 0) {
        CloseHandle(context.cancel_event);
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    }
    waited = WaitForSingleObject(thread, duration_ms + 5000u);
    if (waited != WAIT_OBJECT_0) {
        (void)SetEvent(context.cancel_event);
        joined = WaitForSingleObject(thread, INFINITE);
        if (joined != WAIT_OBJECT_0) {
            CloseHandle(thread);
            CloseHandle(context.cancel_event);
            return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
        }
    }
    (void)GetExitCodeThread(thread, &thread_result);
    CloseHandle(thread);
    CloseHandle(context.cancel_event);
    if (waited == WAIT_TIMEOUT)
        return MOO_UI_HOST_PARITY_RESULT_LIMIT;
    if (waited != WAIT_OBJECT_0 || thread_result != (DWORD)ERROR_SUCCESS)
        return MOO_UI_HOST_PARITY_RESULT_SYSTEM_ERROR;
    return MOO_UI_HOST_PARITY_RESULT_OK;
#else
    (void)instrumentation;
    (void)generation;
    (void)duration_ms;
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
#endif
}

MooUiHostParityResult moo_ui_host_parity_helper_win32_run_crash_cycle(
    MooUiHostParityInstrumentation *instrumentation, uint64_t generation,
    const MooUiHostParityCrashProcessConfig *config) {
#if defined(_WIN32)
    char recovered_text[20];
    DWORD recovered_size = 0u;
    DWORD exit_code = STILL_ACTIVE;
    uint64_t recovered_hash = 0u;
    MooUiHostParityResult result;
    if (instrumentation == 0 || generation == 0u || config == 0 ||
        config->expected_state_hash == 0u || config->crash_exit_code == 0u ||
        config->crash_exit_code == (uint32_t)STILL_ACTIVE ||
        config->timeout_ms < 100u || config->timeout_ms > 60000u)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    result = moo_helper_collect_process(config->executable_path,
        config->crash_argument, config->timeout_ms, 0, 0u, 0, &exit_code);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK)
        return result;
    if (exit_code != (DWORD)config->crash_exit_code)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    result = moo_ui_host_parity_helper_record_crash(
        instrumentation, generation, config->expected_state_hash);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK)
        return result;
    exit_code = STILL_ACTIVE;
    result = moo_helper_collect_process(config->executable_path,
        config->restart_argument, config->timeout_ms, recovered_text,
        (DWORD)sizeof(recovered_text), &recovered_size, &exit_code);
    if (result != MOO_UI_HOST_PARITY_RESULT_OK)
        return result;
    if (exit_code != 0u || !moo_helper_parse_state_hash(
            recovered_text, recovered_size, &recovered_hash) ||
        recovered_hash != config->expected_state_hash)
        return MOO_UI_HOST_PARITY_RESULT_INVALID;
    return moo_ui_host_parity_helper_record_restart(
        instrumentation, generation, recovered_hash);
#else
    (void)instrumentation;
    (void)generation;
    (void)config;
    return MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE;
#endif
}
